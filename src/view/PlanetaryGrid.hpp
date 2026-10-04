#ifndef ORBSIM_VIEW_PLANETARYGRID_HPP
#define ORBSIM_VIEW_PLANETARYGRID_HPP
//
// A latitude and longitude wireframe on a sphere, turned by a body's
// orientation, and the part of it an eye outside the sphere can see (M1-20;
// ADR 0012 for why it is here, register decisions 319-336 for its shape).
//
// **Made in the body's own frame, then turned.** Each vertex is first worked
// out in the frame that turns with the body -- x toward latitude 0, longitude
// 0, z toward the north pole -- where the equator lies in z = 0 and the prime
// meridian in y = 0 exactly, and is then turned into the world frame by the
// quaternion the caller hands in. For the Earth that is
// astro/EarthOrientation.hpp's rotation at the epoch, conjugated; this library
// does not see src/astro, so it is told the rotation rather than asking for
// it. The sphere's centre is the world's origin.
//
// **How far from the sphere a vertex lands, in doubles.** The task asked for
// 1e-9 m, which is finer than a double holds at the Earth's radius: two
// neighbouring doubles there are 2^-30 m, 9.3e-10 m, apart. Measured over the
// Earth grid, worst, with tests/test_planetary_grid.cpp's exact residual: see
// kGridRadiusBudgetSpacings there, which is twice the measurement.
//
// **What can be seen.** A wireframe is see-through, so the far half would be
// drawn over the near half. A point P on a sphere of radius R about the
// origin is in sight of an eye E outside it exactly when E . P > R^2 -- the
// eye is above the plane tangent at P -- and so the horizon is the plane
// E . P = R^2. `visiblePart` keeps the near side of that plane and cuts a
// segment that crosses it at the plane, so that lines end on the limb rather
// than one segment short of it or one segment past it.
//
#include "core/Attributes.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/LineBatch.hpp"
#include "view/VertexLayout.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace orb::view {

// The WGS-84 semi-major axis, the Earth's equatorial radius: 6 378 137 m
// exactly, a defining constant of the system (NIMA TR8350.2, third edition,
// table 3.1). The grid is a sphere of this radius until M1-49 brings the
// ellipsoid -- whose view/Ellipsoid.hpp is where this constant moves then.
inline constexpr Metres kWgs84SemiMajorAxis{6'378'137.0};

// How finely a grid is drawn, as whole numbers so that no spacing has to be
// rounded to land on the poles or close a circle: `meridians` evenly spaced
// from longitude 0 eastward; `parallelsPerHemisphere` evenly spaced between
// the equator and each pole, the equator always drawn besides; and every line
// cut into segments of a quarter circle divided by `segmentsPerQuarterCircle`.
// Filled in by field name, so the three numbers cannot be transposed.
struct GridCounts {
    std::uint32_t meridians{};
    std::uint32_t parallelsPerHemisphere{};
    std::uint32_t segmentsPerQuarterCircle{};
};

// What a grid can be asked for and not exist as. Reported, because the counts
// will one day come from a quality setting or a scenario (ADR 0002).
enum class GridError : std::uint8_t {
    NoMeridians,     // zero meridians: the prime meridian at least is drawn
    NoSegments,      // zero segments per quarter circle: no line has a length
    TooManyVertices, // the line-list vertex count does not fit in 32 bits
};

[[nodiscard]] constexpr std::string_view describe(GridError error) noexcept {
    switch (error) {
    case GridError::NoMeridians:
        return "a grid needs at least one meridian, the prime meridian";
    case GridError::NoSegments:
        return "a grid needs at least one segment per quarter circle";
    case GridError::TooManyVertices:
        return "the grid's line-list vertex count does not fit in 32 bits";
    }
    return "unknown grid error";
}

// Counts a grid can be made from: validated once, so that everything after
// the factory may rely on them (ADR 0022's shape, applied to a composite as
// view::Camera applies it).
class GridLayout {
public:
    [[nodiscard]] static constexpr std::expected<GridLayout, GridError>
    from(const GridCounts& counts) noexcept {
        if (counts.meridians == 0) return std::unexpected(GridError::NoMeridians);
        if (counts.segmentsPerQuarterCircle == 0) return std::unexpected(GridError::NoSegments);
        // In 64 bits, where none of the three products can wrap: each factor
        // is below 2^32 and at most three are multiplied, the largest by 4.
        const std::uint64_t meridians = counts.meridians;
        const std::uint64_t parallels = (2 * std::uint64_t{counts.parallelsPerHemisphere}) + 1;
        const std::uint64_t perQuarter = counts.segmentsPerQuarterCircle;
        const std::uint64_t meridianSegments = meridians * 2 * perQuarter;
        const std::uint64_t parallelSegments = parallels * 4 * perQuarter;
        const std::uint64_t vertices = 2 * (meridianSegments + parallelSegments);
        if (vertices > std::uint64_t{VertexCount::kMaximum}) {
            return std::unexpected(GridError::TooManyVertices);
        }
        return GridLayout{counts};
    }

    [[nodiscard]] constexpr GridCounts counts() const noexcept { return counts_; }

    // Every segment of the grid as a line list, two vertices each: what a
    // LineBatch must hold to draw the whole of it, seen from anywhere.
    [[nodiscard]] constexpr VertexCount vertexCount() const noexcept {
        const std::uint32_t parallels = (2 * counts_.parallelsPerHemisphere) + 1;
        const std::uint32_t perQuarter = counts_.segmentsPerQuarterCircle;
        return VertexCount{2 *
                           ((counts_.meridians * 2 * perQuarter) + (parallels * 4 * perQuarter))};
    }

private:
    explicit constexpr GridLayout(const GridCounts& counts) noexcept : counts_(counts) {}

    GridCounts counts_;
};

// The task's grid (register decision 326): meridians every 15 degrees,
// parallels every 10 degrees -- eight to each side of the equator, the last at
// 80 degrees -- and segments of half a degree, which stand at most 0.13 px
// off the true curve at 400 km in a probe frame. 41 760 vertices.
inline constexpr GridCounts kEarthGridCounts{
    .meridians = 24,
    .parallelsPerHemisphere = 8,
    .segmentsPerQuarterCircle = 180,
};

static_assert(GridLayout::from(kEarthGridCounts).has_value(), "the task's grid can be made");
static_assert(GridLayout::from(kEarthGridCounts)->vertexCount() == VertexCount{41'760U},
              "24 meridians of 360 segments and 17 parallels of 720, two vertices each");
namespace detail {
inline constexpr GridCounts kSmallestGridCounts{
    .meridians = 1,
    .parallelsPerHemisphere = 0,
    .segmentsPerQuarterCircle = 1,
};
} // namespace detail
static_assert(GridLayout::from(detail::kSmallestGridCounts)->vertexCount() == VertexCount{12U},
              "the smallest grid: one meridian of 2 segments and the equator of 4");

// Which line of the grid a polyline is, for its colour.
enum class GridLine : std::uint8_t {
    Meridian,      // every meridian but the prime meridian
    Parallel,      // every parallel but the equator
    Equator,       //
    PrimeMeridian, // longitude 0
};

// One line of the grid, in world metres.
//
// **A meridian runs from the south pole to the north**, 2 x segmentsPerQuarter
// segments, so its point i is at latitude -90 + 90 i / segmentsPerQuarter
// degrees. **A parallel runs eastward from longitude 0 and closes on its own
// first point**, 4 x segmentsPerQuarter segments, so its point k is at
// longitude 90 k / segmentsPerQuarter degrees, and its last point is its first,
// copied rather than computed again at 360 degrees.
struct GridPolyline {
    GridLine line{};
    std::vector<Position> points;
};

// The lines of a grid, made once and kept: a few dozen allocations when it is
// made, none when it is drawn.
class PlanetaryGrid {
public:
    // The grid of `layout` on a sphere of `radius` about the origin, each
    // vertex turned into the world frame by `worldFromBodyFixed`. The radius
    // must be finite and positive and the quaternion a rotation -- defects in
    // the caller otherwise, since both are constants or come from a function
    // that cannot fail.
    //
    // **The lines come in the order they are drawn**: the plain meridians and
    // parallels first, then the equator, then the prime meridian, so that the
    // two coloured lines are drawn over the grey ones where they cross.
    [[nodiscard]] static PlanetaryGrid
    make(Metres radius, const GridLayout& layout, const Quat& worldFromBodyFixed);

    [[nodiscard]] Metres radius() const noexcept { return radius_; }
    [[nodiscard]] std::span<const GridPolyline> lines() const noexcept ORBSIM_LIFETIMEBOUND {
        return lines_;
    }

private:
    PlanetaryGrid(Metres radius, std::vector<GridPolyline> lines) noexcept;

    Metres radius_;
    std::vector<GridPolyline> lines_;
};

// The part of `world` an eye at `eye` can see on a sphere of `radius` about
// the origin, or nothing. Both ends are meant to be on the sphere; a segment
// whose ends are on opposite sides of the horizon plane E . P = R^2 is cut
// where it meets the plane, and keeps its visible end exactly. An end on the
// plane counts as hidden, so a segment touching the horizon at one end alone
// is kept whole only on its visible side. An eye inside the sphere sees
// nothing, since E . P <= |E| R < R^2 for every P on it.
[[nodiscard]] std::optional<Segment>
visiblePart(const Segment& world, const Position& eye, Metres radius) noexcept;

// The horizon seen from an eye outside a sphere about the origin: the circle
// where the sphere's visible part ends, which is the limb (register decision
// 338, at the owner's request, so that where the grid's lines end can be
// judged against where the planet's edge is).
//
// **Every point P of it is on the sphere and on the horizon plane E . P =
// R^2**, so it lies at distance R from the origin and R^2 / |E| along the
// eye's direction: a circle centred at (R / |E|)^2 E, of radius
// R sqrt(1 - (R / |E|)^2), in the plane perpendicular to E. `across` and
// `around` are two unit vectors spanning that plane.
struct Horizon {
    Position centre;
    Metres radius;
    Direction across;
    Direction around;
};

// The horizon from `eye`, or none when the eye is not outside the sphere --
// on it or inside it there is no circle to draw.
[[nodiscard]] std::optional<Horizon> horizonFrom(const Position& eye, Metres sphereRadius) noexcept;

// The point of `horizon` at `angle` from `across` toward `around`.
[[nodiscard]] Position pointOn(const Horizon& horizon, Radians angle) noexcept;

// How many segments the horizon is drawn in: half a degree each, as the
// grid's lines are (register decision 326), which at 400 km keeps a segment
// within 0.01 px of the circle.
inline constexpr std::uint32_t kHorizonSegments = 720;

// The grid's colours, as radiance in W/(m^2 sr) (register decision 327, from
// decision 279's): the plain lines the `lines` probe's white square, 130 in
// each channel, and the equator and prime meridian the X and Y axes' red and
// green, which the owner approved on screen with M1-19. The horizon is the Z
// axis's blue (register decision 338), approved the same day.
struct GridColours {
    Rgba grid;
    Rgba equator;
    Rgba primeMeridian;
    Rgba horizon;
};

inline constexpr GridColours kGridColours{
    .grid = {.r = 130.0F, .g = 130.0F, .b = 130.0F, .a = 1.0F},
    .equator = kAxisXColour,
    .primeMeridian = kAxisYColour,
    .horizon = kAxisZColour,
};

// Adds to `batch` the part of `grid` the camera can see, in `colours`, in the
// grid's own order. **Refused whole, as every LineBatch add is**: the visible
// pieces are counted first, and if they do not fit, nothing is added.
[[nodiscard]] std::expected<void, LineBatchError> addVisibleGrid(LineBatch& batch,
                                                                 const PlanetaryGrid& grid,
                                                                 const GridColours& colours,
                                                                 const Camera& camera);

// Adds the horizon the camera sees on a sphere of `sphereRadius` about the
// origin, kHorizonSegments segments in `colour`; nothing, successfully, when
// the camera is not outside the sphere. Refused whole if it does not fit.
[[nodiscard]] std::expected<void, LineBatchError>
addHorizon(LineBatch& batch, Metres sphereRadius, Rgba colour, const Camera& camera);

static_assert(describe(GridError::NoMeridians) != describe(GridError::NoSegments),
              "each error says what is wrong");
static_assert(describe(GridError::NoSegments) != describe(GridError::TooManyVertices));

} // namespace orb::view

#endif // ORBSIM_VIEW_PLANETARYGRID_HPP
