#include "view/PlanetaryGrid.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/LineBatch.hpp"
#include "view/VertexLayout.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace orb::view {
namespace {

// Where on the sphere, in the body's own frame. A struct rather than two
// angles in a row, which would transpose in silence (non-negotiable 1).
struct LatitudeLongitude {
    Radians latitude;
    Radians longitude;
};

// A point of the sphere in the body's own frame, from its latitude and
// longitude, and then turned into the world frame. Latitude 0 gives z = R x
// sin(0) = 0 exactly and longitude 0 gives y = R cos(lat) sin(0) = 0 exactly,
// which is what puts the equator and the prime meridian on their planes.
[[nodiscard]] Position
pointAt(const LatitudeLongitude& where, Metres radius, const Quat& worldFromBodyFixed) {
    const f64 r = radius.value();
    const f64 c = std::cos(where.latitude.value());
    const Position bodyFixed{r * c * std::cos(where.longitude.value()),
                             r * c * std::sin(where.longitude.value()),
                             r * std::sin(where.latitude.value())};
    return worldFromBodyFixed.rotate(bodyFixed);
}

// An angle as a whole number of steps, `perQuarter` of them to a quarter
// circle. A struct, since the two counts are both whole numbers and convert
// into each other (non-negotiable 1).
struct QuarterSteps {
    std::int64_t steps{};
    std::uint32_t perQuarter{};
};

// Formed as one product and one division, so that step 0 is exactly zero.
[[nodiscard]] Radians angleOf(const QuarterSteps& angle) noexcept {
    return Radians{(static_cast<f64>(angle.steps) * (kPi / 2.0)) /
                   static_cast<f64>(angle.perQuarter)};
}

// The meridian at `longitude`, from the south pole to the north.
[[nodiscard]] GridPolyline meridianAt(GridLine line,
                                      Radians longitude,
                                      Metres radius,
                                      std::uint32_t perQuarter,
                                      const Quat& worldFromBodyFixed) {
    GridPolyline polyline{.line = line, .points = {}};
    const std::int64_t steps = 2 * std::int64_t{perQuarter};
    polyline.points.reserve(static_cast<std::size_t>(steps) + 1);
    for (std::int64_t i = 0; i <= steps; ++i) {
        const Radians latitude =
            angleOf({.steps = i - std::int64_t{perQuarter}, .perQuarter = perQuarter});
        polyline.points.push_back(
            pointAt({.latitude = latitude, .longitude = longitude}, radius, worldFromBodyFixed));
    }
    return polyline;
}

// The parallel at `latitude`, eastward from longitude 0, closed on its own
// first point -- copied, not computed again at 2 pi, where sin and cos would
// not return exactly what they did at 0.
[[nodiscard]] GridPolyline parallelAt(GridLine line,
                                      Radians latitude,
                                      Metres radius,
                                      std::uint32_t perQuarter,
                                      const Quat& worldFromBodyFixed) {
    GridPolyline polyline{.line = line, .points = {}};
    const std::int64_t steps = 4 * std::int64_t{perQuarter};
    polyline.points.reserve(static_cast<std::size_t>(steps) + 1);
    for (std::int64_t k = 0; k < steps; ++k) {
        const Radians longitude = angleOf({.steps = k, .perQuarter = perQuarter});
        polyline.points.push_back(
            pointAt({.latitude = latitude, .longitude = longitude}, radius, worldFromBodyFixed));
    }
    polyline.points.push_back(polyline.points.front());
    return polyline;
}

// The colour of a line of the grid.
[[nodiscard]] Rgba colourOf(GridLine line, const GridColours& colours) noexcept {
    switch (line) {
    case GridLine::Meridian:
    case GridLine::Parallel:
        return colours.grid;
    case GridLine::Equator:
        return colours.equator;
    case GridLine::PrimeMeridian:
        return colours.primeMeridian;
    }
    return colours.grid;
}

// The coordinate axis least aligned with `direction`: crossed with it, it
// gives a vector across the direction that is never near zero in length.
[[nodiscard]] Direction leastAlignedAxis(const Direction& direction) noexcept {
    const f64 ax = absOf(direction.x.value());
    const f64 ay = absOf(direction.y.value());
    const f64 az = absOf(direction.z.value());
    if (ax <= ay && ax <= az) return Direction{1.0, 0.0, 0.0};
    if (ay <= az) return Direction{0.0, 1.0, 0.0};
    return Direction{0.0, 0.0, 1.0};
}

// Calls `visit(segment, colour)` for every visible piece of the grid, in the
// grid's order. Used twice by addVisibleGrid -- once to count, once to add --
// so that what is counted and what is added cannot differ.
template <typename Visit>
void forEachVisiblePiece(const PlanetaryGrid& grid,
                         const GridColours& colours,
                         const Position& eye,
                         const Visit& visit) {
    for (const GridPolyline& polyline : grid.lines()) {
        const Rgba colour = colourOf(polyline.line, colours);
        for (const auto [from, to] : polyline.points | std::views::pairwise) {
            if (const auto piece = visiblePart({.from = from, .to = to}, eye, grid.radius())) {
                visit(*piece, colour);
            }
        }
    }
}

} // namespace

PlanetaryGrid::PlanetaryGrid(Metres radius, std::vector<GridPolyline> lines) noexcept
    : radius_(radius), lines_(std::move(lines)) {}

PlanetaryGrid
PlanetaryGrid::make(Metres radius, const GridLayout& layout, const Quat& worldFromBodyFixed) {
    // Computed outside the assertions, so that both trees use the same
    // functions (CODING_GUIDELINES section 2's shape for an asserted result).
    [[maybe_unused]] const bool usableRadius = isFinite(radius.value()) && radius.value() > 0.0;
    [[maybe_unused]] const bool rotation =
        isUnitQuaternion(worldFromBodyFixed, kUnitQuaternionTolerance);
    ORBSIM_EXPECTS(usableRadius);
    ORBSIM_EXPECTS(rotation);

    const GridCounts counts = layout.counts();
    const std::uint32_t perQuarter = counts.segmentsPerQuarterCircle;
    const std::uint32_t perHemisphere = counts.parallelsPerHemisphere;
    std::vector<GridPolyline> lines;
    lines.reserve(std::size_t{counts.meridians} + (2 * std::size_t{perHemisphere}) + 1);

    // The plain meridians, from 360/m degrees eastward; the prime meridian,
    // index 0, is drawn last.
    for (std::uint32_t j = 1; j < counts.meridians; ++j) {
        const Radians longitude{(static_cast<f64>(j) * kTau) / static_cast<f64>(counts.meridians)};
        lines.push_back(
            meridianAt(GridLine::Meridian, longitude, radius, perQuarter, worldFromBodyFixed));
    }
    // The plain parallels, south to north, at k quarter circles over
    // (perHemisphere + 1) on each side of the equator.
    const std::int64_t bands = std::int64_t{perHemisphere} + 1;
    for (std::int64_t k = -std::int64_t{perHemisphere}; k <= std::int64_t{perHemisphere}; ++k) {
        if (k == 0) continue;
        const Radians latitude{(static_cast<f64>(k) * (kPi / 2.0)) / static_cast<f64>(bands)};
        lines.push_back(
            parallelAt(GridLine::Parallel, latitude, radius, perQuarter, worldFromBodyFixed));
    }
    lines.push_back(
        parallelAt(GridLine::Equator, Radians{0.0}, radius, perQuarter, worldFromBodyFixed));
    lines.push_back(
        meridianAt(GridLine::PrimeMeridian, Radians{0.0}, radius, perQuarter, worldFromBodyFixed));
    return PlanetaryGrid{radius, std::move(lines)};
}

std::optional<Segment>
visiblePart(const Segment& world, const Position& eye, Metres radius) noexcept {
    // How far above the horizon plane each end is, as E . P - R^2, in square
    // metres. Positive is in sight. Near the plane both terms are about
    // 4e13 m^2, so the difference is good to a few hundredths of a square
    // metre -- which, divided by |E| of several million metres, places the cut
    // to about a nanometre.
    const f64 rSquared = radius.value() * radius.value();
    const f64 above = dot(eye, world.from).value() - rSquared;
    const f64 aboveTo = dot(eye, world.to).value() - rSquared;
    const bool fromSeen = above > 0.0;
    const bool toSeen = aboveTo > 0.0;
    if (fromSeen && toSeen) return world;
    if (!fromSeen && !toSeen) return std::nullopt;
    // Exactly one end is in sight, so the two heights have opposite signs (or
    // the hidden one is zero) and the denominator is not zero: the plane is
    // crossed at the fraction `above / (above - aboveTo)` along the segment.
    const f64 t = above / (above - aboveTo);
    const Position cut = world.from + ((world.to - world.from) * t);
    if (fromSeen) return Segment{.from = world.from, .to = cut};
    return Segment{.from = cut, .to = world.to};
}

std::expected<void, LineBatchError> addVisibleGrid(LineBatch& batch,
                                                   const PlanetaryGrid& grid,
                                                   const GridColours& colours,
                                                   const Camera& camera) {
    const Position eye = camera.position();
    std::uint64_t pieces = 0;
    forEachVisiblePiece(grid, colours, eye, [&pieces](const Segment&, const Rgba&) { ++pieces; });
    // Two vertices a piece, against what is left; in 64 bits, where neither
    // side can wrap, since a grid's pieces are at most its segments.
    const std::uint64_t left = std::uint64_t{batch.capacity().value()} - batch.size().value();
    if (2 * pieces > left) return std::unexpected(LineBatchError::OverCapacity);
    forEachVisiblePiece(grid, colours, eye, [&](const Segment& piece, const Rgba& colour) {
        // Room was counted above, so this cannot be refused.
        [[maybe_unused]] const auto added = batch.addSegment(piece, colour, camera);
        ORBSIM_ENSURES(added.has_value());
    });
    return {};
}

std::optional<Horizon> horizonFrom(const Position& eye, Metres sphereRadius) noexcept {
    const f64 r = sphereRadius.value();
    const f64 d = length(eye).value();
    if (!(d > r)) return std::nullopt;
    // The circle's radius is R sqrt(1 - (R/d)^2), written as
    // R sqrt((d - R)(d + R)) / d: from 1 km up, 1 - (R/d)^2 is 3e-4 and
    // forming it from (R/d)^2 would lose four digits, while d - R is exact
    // there (Sterbenz: d and R are within a factor of two).
    const f64 radius = (r * std::sqrt((d - r) * (d + r))) / d;
    const f64 scale = r / d;
    const Direction toEye = eye / length(eye);
    const Direction across = directionOf(cross(toEye, leastAlignedAxis(toEye)));
    return Horizon{
        .centre = eye * (scale * scale),
        .radius = Metres{radius},
        .across = across,
        .around = cross(toEye, across),
    };
}

Position pointOn(const Horizon& horizon, Radians angle) noexcept {
    const f64 a = horizon.radius.value() * std::cos(angle.value());
    const f64 b = horizon.radius.value() * std::sin(angle.value());
    const Position& c = horizon.centre;
    return Position{c.x.value() + (a * horizon.across.x.value()) + (b * horizon.around.x.value()),
                    c.y.value() + (a * horizon.across.y.value()) + (b * horizon.around.y.value()),
                    c.z.value() + (a * horizon.across.z.value()) + (b * horizon.around.z.value())};
}

std::expected<void, LineBatchError>
addHorizon(LineBatch& batch, Metres sphereRadius, Rgba colour, const Camera& camera) {
    const std::optional<Horizon> horizon = horizonFrom(camera.position(), sphereRadius);
    if (!horizon.has_value()) return {};
    const std::uint64_t left = std::uint64_t{batch.capacity().value()} - batch.size().value();
    if (2 * std::uint64_t{kHorizonSegments} > left) {
        return std::unexpected(LineBatchError::OverCapacity);
    }
    // Closed on its own first point, copied rather than computed again at
    // 2 pi, as a parallel is.
    const Position first = pointOn(*horizon, Radians{0.0});
    Position from = first;
    for (std::uint32_t k = 1; k <= kHorizonSegments; ++k) {
        const Position to = k == kHorizonSegments
                                ? first
                                : pointOn(*horizon,
                                          Radians{(static_cast<f64>(k) * kTau) /
                                                  static_cast<f64>(kHorizonSegments)});
        // Room was counted above, so this cannot be refused.
        [[maybe_unused]] const auto added =
            batch.addSegment({.from = from, .to = to}, colour, camera);
        ORBSIM_ENSURES(added.has_value());
        from = to;
    }
    return {};
}

} // namespace orb::view
