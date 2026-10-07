//
// Tests for view/PlanetaryGrid.hpp: the latitude and longitude wireframe, its
// orientation at the probes' epoch, and the part of it an eye can see (M1-20;
// register decisions 319-336).
//
// **Nothing here includes a Vulkan or SDL header**: this suite links
// orbsim_view, which links orbsim_core and nothing else.
//
// **Where the expected values come from.** The counts are worked out by hand
// from the layout's definition. The orientation is held two ways: bit for bit
// to astro/EarthOrientation.hpp's rotation, which is the wiring -- the grid
// turned by exactly the quaternion it was handed -- and, independently, to
// the committed Skyfield fixture (data/skyfield/earth-orientation.txt), an
// implementation of the IAU models that is not ERFA's, within M1-07's 0.1 mas
// (VERIFICATION.md rules 2 and 3). The horizon is checked against the plane
// worked out on paper for an eye on an axis.
//
#include "FixtureFile.hpp"
#include "astro/EarthOrientation.hpp"
#include "core/DoubleDouble.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/LineBatch.hpp"
#include "view/PlanetaryGrid.hpp"
#include "view/VertexLayout.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

using namespace orb;
using namespace orb::view;
using namespace orb::test;

namespace {

constexpr f64 kRadius = 6'378'137.0; // WGS-84 a, written out (register decision 329)

// Two neighbouring doubles at the Earth's radius are 2^-30 m apart: 6 378 137
// lies in [2^22, 2^23), where a double's 52 fraction bits leave 2^(22-52).
constexpr f64 kSpacingAtRadius = 9.313225746154785e-10;
static_assert(kSpacingAtRadius == 1.0 / 1'073'741'824.0, "2^-30 exactly");

// **How far from the sphere a vertex may land, in those spacings** (register
// decision 328). The task asked for 1e-9 m, about one spacing, which a double
// cannot promise: measured on 2026-10-04 over the task's grid with the exact
// residual below, worst 1.454 spacings in the body's own frame -- the
// rounding of r cos(lat) cos(lon) and its neighbours -- and 2.570 once turned
// by the epoch's rotation, which adds the quaternion's own. Each budget is
// twice its measurement. (The turned figure was 2.152 on the row first
// chosen, 2026-09-29: it depends on the rotation, so a new epoch is measured
// again.)
constexpr f64 kBodyFixedBudgetSpacings = 2.91;
constexpr f64 kTurnedBudgetSpacings = 5.14;

// The horizon's points (register decision 338): how far from the sphere, in
// spacings at the radius, and from the horizon plane, in metres. Measured on
// 2026-10-04 over 720 points from each of the six eyes below: worst 1.197
// spacings, and 1.15e-9 m -- which is the resolution of E . P itself, a
// difference of two terms near 4e13 m^2 whose last bit, 0.0078 m^2, divided
// by |E|, is about 1e-9 m. Each budget is twice its measurement.
constexpr f64 kHorizonSphereBudgetSpacings = 2.39;
constexpr f64 kHorizonPlaneBudgetMetres = 2.31e-9;

// How far a parallel or a meridian may be from where its spacing puts it: a
// micrometre. The tests form each angle their own way, 15 j degrees times
// pi / 180 against the code's 2 pi j / 24, and the two differ by up to one
// step of a double near 6 radians -- 5.6e-9 m at the radius, measured on
// 2026-10-04 -- where a spacing off by one moves a line by hundreds of
// kilometres.
constexpr f64 kSpacingToleranceMetres = 1e-6;

[[nodiscard]] constexpr f64 degreesToRadians(f64 degrees) noexcept {
    return degrees * std::numbers::pi / 180.0;
}

// M1-07's budget against the same fixture: 0.1 mas, in radians.
constexpr f64 kFixtureBudgetRadians = 0.1e-3 / 3600.0 * std::numbers::pi / 180.0;

// The probes' epoch (register decision 323): one row of the Skyfield
// fixture, 2025-07-30 near 06:29 TT, with that row's own UT1. Both fractions
// are exact binary fractions, as the fixture writes them.
constexpr JulianDate kEpochTt{.day = 2'460'886.5, .fraction = 0.2699127197265625};
constexpr JulianDate kEpochUt1{.day = 2'460'886.5, .fraction = 0.26911163330078125};

[[nodiscard]] GridLayout layoutOf(const GridCounts& counts) {
    const auto layout = GridLayout::from(counts);
    REQUIRE(layout.has_value());
    return *layout;
}

[[nodiscard]] Quat epochRotation() {
    const auto tt = TtTime::fromJulianDate(kEpochTt);
    const auto ut1 = Ut1Time::fromJulianDate(kEpochUt1);
    REQUIRE(tt.has_value());
    REQUIRE(ut1.has_value());
    // The grid wants body-fixed to world; ERFA's rotation runs the other way.
    return earthFixedFromInertial(*tt, *ut1).conjugate();
}

// The fixture's celestial-to-terrestrial matrix at the epoch's row.
[[nodiscard]] RotationMatrix fixtureMatrixAtEpoch() {
    auto fixture = readEarthOrientation(std::filesystem::path{dataDirectory()} / "skyfield" /
                                        "earth-orientation.txt");
    INFO((fixture.has_value() ? "" : describe(fixture.error())));
    REQUIRE(fixture.has_value());
    const auto tt = TtTime::fromJulianDate(kEpochTt);
    REQUIRE(tt.has_value());
    const auto row = std::ranges::find_if(fixture->rows, [&](const RotationAtEpoch& r) noexcept {
        return r.tt.modifiedJulianDay() == tt->modifiedJulianDay() &&
               r.tt.picosecondOfDay() == tt->picosecondOfDay();
    });
    REQUIRE(row != fixture->rows.end());
    return row->celestialToTerrestrial;
}

// Plain doubles for the independent half: this file's own arithmetic.
struct Plain {
    f64 x{};
    f64 y{};
    f64 z{};
};

[[nodiscard]] Plain plainOf(const Position& p) noexcept {
    return {.x = p.x.value(), .y = p.y.value(), .z = p.z.value()};
}

// M^T v: the fixture's matrix takes celestial to terrestrial, so its
// transpose takes a body-fixed point to the world frame.
[[nodiscard]] Plain transposeTimes(const RotationMatrix& m, const Plain& v) noexcept {
    const auto& r = m.rows;
    return {
        .x = (r.at(0).at(0) * v.x) + (r.at(1).at(0) * v.y) + (r.at(2).at(0) * v.z),
        .y = (r.at(0).at(1) * v.x) + (r.at(1).at(1) * v.y) + (r.at(2).at(1) * v.z),
        .z = (r.at(0).at(2) * v.x) + (r.at(1).at(2) * v.y) + (r.at(2).at(2) * v.z),
    };
}

// Two directions to compare. A struct rather than two parameters of one type
// (non-negotiable 1), although the angle between them is symmetric.
struct DirectionPair {
    Plain measured;
    Plain expected;
};

// The angle between two vectors, by atan2 of the cross and dot products,
// which stays accurate for small angles where acos does not.
[[nodiscard]] f64 angleBetweenPlain(const DirectionPair& pair) noexcept {
    const Plain& a = pair.measured;
    const Plain& b = pair.expected;
    const f64 cx = (a.y * b.z) - (a.z * b.y);
    const f64 cy = (a.z * b.x) - (a.x * b.z);
    const f64 cz = (a.x * b.y) - (a.y * b.x);
    return std::atan2(std::sqrt((cx * cx) + (cy * cy) + (cz * cz)),
                      (a.x * b.x) + (a.y * b.y) + (a.z * b.z));
}

// |p| - R without rounding the sum of squares: each square and the sum carried
// in double-double, so that the residual measured is the vertex's and not the
// measurement's. |p|^2 - R^2 = (|p| - R)(|p| + R), and |p| + R is 2R to far
// better than the one significant figure a budget needs.
[[nodiscard]] f64 distanceFromSphere(const Position& p) noexcept {
    const DoubleDouble squares = twoProduct(p.x.value(), p.x.value()) +
                                 twoProduct(p.y.value(), p.y.value()) +
                                 twoProduct(p.z.value(), p.z.value());
    const DoubleDouble excess = squares - twoProduct(kRadius, kRadius);
    return toDouble(excess) / (2.0 * kRadius);
}

[[nodiscard]] const GridPolyline& onlyLine(const PlanetaryGrid& grid, GridLine which) {
    const auto lines = grid.lines();
    REQUIRE(std::ranges::count(lines, which, &GridPolyline::line) == 1);
    return *std::ranges::find(lines, which, &GridPolyline::line);
}

// The largest residual over every vertex of `grid`, in spacings at the radius.
[[nodiscard]] f64 worstResidualSpacings(const PlanetaryGrid& grid) {
    f64 worst = 0.0;
    for (const GridPolyline& line : grid.lines()) {
        for (const Position& p : line.points) {
            worst = std::max(worst, std::abs(distanceFromSphere(p)) / kSpacingAtRadius);
        }
    }
    return worst;
}

// A camera from constants the factory accepts: the probes' field of view and
// near plane, unrotated, at `position`.
[[nodiscard]] Camera cameraAt(const Position& position) {
    const auto camera =
        Camera::from(position, Quat{}, Radians{std::numbers::pi / 4.0}, Metres{1.0});
    REQUIRE(camera.has_value());
    return *camera;
}

// A colour's four channels as their bit patterns, so two colours compare by
// bit identity with ==, the shape tests/test_line_batch.cpp uses.
[[nodiscard]] std::array<std::uint32_t, 4> colourBits(const Rgba& colour) noexcept {
    return std::to_array({
        bitsOf(colour.r),
        bitsOf(colour.g),
        bitsOf(colour.b),
        bitsOf(colour.a),
    });
}

// How many of `lines` are of `which` kind.
[[nodiscard]] std::ptrdiff_t countOf(std::span<const GridPolyline> lines, GridLine which) {
    return std::ranges::count(lines, which, &GridPolyline::line);
}

// How many lines have other than the number of points their kind gives: a
// meridian 2 s + 1, a parallel 4 s + 1, for s segments per quarter circle.
[[nodiscard]] std::ptrdiff_t linesOfWrongLength(std::span<const GridPolyline> lines,
                                                std::size_t perQuarter) {
    return std::ranges::count_if(lines, [perQuarter](const GridPolyline& line) noexcept {
        const bool meridian =
            line.line == GridLine::Meridian || line.line == GridLine::PrimeMeridian;
        return line.points.size() != (meridian ? 2 * perQuarter : 4 * perQuarter) + 1;
    });
}

[[nodiscard]] std::size_t segmentsOf(std::span<const GridPolyline> lines) {
    std::size_t segments = 0;
    for (const GridPolyline& line : lines) {
        segments += line.points.size() - 1;
    }
    return segments;
}

// The part of a segment visiblePart saw, or a failed test. `value_or` after
// the REQUIRE rather than `*`: bugprone-unchecked-optional-access cannot see
// that a failed REQUIRE ends the case, and a `return` after Catch2's FAIL is
// code MSVC knows cannot run (C4702) -- found by the windows-msvc tree.
[[nodiscard]] Segment seenOrFail(const std::optional<Segment>& seen) {
    REQUIRE(seen.has_value());
    return seen.value_or(Segment{});
}

} // namespace

// --- the layout ---------------------------------------------------------------

TEST_CASE("a grid's counts are refused by name where no grid can be made of them") {
    const auto noMeridians = GridLayout::from(
        {.meridians = 0, .parallelsPerHemisphere = 8, .segmentsPerQuarterCircle = 180});
    REQUIRE(!noMeridians.has_value());
    CHECK(noMeridians.error() == GridError::NoMeridians);

    const auto noSegments = GridLayout::from(
        {.meridians = 24, .parallelsPerHemisphere = 8, .segmentsPerQuarterCircle = 0});
    REQUIRE(!noSegments.has_value());
    CHECK(noSegments.error() == GridError::NoSegments);

    // 2 x (1 x 2 x s + 1 x 4 x s) = 12 s vertices: one past 2^32 - 1 at the
    // smallest s that reaches it, and the largest s that does not.
    constexpr std::uint32_t kFirstTooLarge = 357'913'942; // 12 s > 4 294 967 295
    const auto tooLarge = GridLayout::from(
        {.meridians = 1, .parallelsPerHemisphere = 0, .segmentsPerQuarterCircle = kFirstTooLarge});
    REQUIRE(!tooLarge.has_value());
    CHECK(tooLarge.error() == GridError::TooManyVertices);
    const auto largest = GridLayout::from({
        .meridians = 1,
        .parallelsPerHemisphere = 0,
        .segmentsPerQuarterCircle = kFirstTooLarge - 1,
    });
    REQUIRE(largest.has_value());
    CHECK(largest->vertexCount() == VertexCount{4'294'967'292U});
}

TEST_CASE("the task's grid has the lines and the vertex counts its spacing gives") {
    // 23 plain meridians and the prime meridian, 16 plain parallels and the
    // equator. A meridian is 2 x 180 segments, so 361 points; a parallel
    // 4 x 180, so 721 points, its last its first.
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    const auto lines = grid.lines();
    REQUIRE(lines.size() == 41);
    CHECK(countOf(lines, GridLine::Meridian) == 23);
    CHECK(countOf(lines, GridLine::Parallel) == 16);
    CHECK(linesOfWrongLength(lines, 180) == 0);
    CHECK(segmentsOf(lines) == 20'880U);
    CHECK(layoutOf(kEarthGridCounts).vertexCount() == VertexCount{41'760U});
}

TEST_CASE("a small grid has the lines and the vertex counts worked out on paper") {
    // 3 meridians of 2 x 2 segments, 5 points each; the equator and one
    // parallel each side of 4 x 2 segments, 9 points each: 12 + 24 = 36
    // segments, 72 vertices.
    constexpr GridCounts kSmall{
        .meridians = 3,
        .parallelsPerHemisphere = 1,
        .segmentsPerQuarterCircle = 2,
    };
    const PlanetaryGrid grid = PlanetaryGrid::make(Metres{kRadius}, layoutOf(kSmall), Quat{});
    REQUIRE(grid.lines().size() == 6);
    CHECK(countOf(grid.lines(), GridLine::Meridian) == 2);
    CHECK(countOf(grid.lines(), GridLine::Parallel) == 2);
    CHECK(linesOfWrongLength(grid.lines(), 2) == 0);
    CHECK(segmentsOf(grid.lines()) == 36U);
    CHECK(layoutOf(kSmall).vertexCount() == VertexCount{72U});
}

TEST_CASE("the grid's lines come in the order they are drawn, the coloured two last") {
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    const auto lines = grid.lines();
    REQUIRE(lines.size() >= 2);
    const auto coloured = lines.last(2);
    CHECK(coloured.front().line == GridLine::Equator);
    CHECK(coloured.back().line == GridLine::PrimeMeridian);
    const auto plain = lines.first(lines.size() - 2);
    CHECK(countOf(plain, GridLine::Meridian) + countOf(plain, GridLine::Parallel) ==
          static_cast<std::ptrdiff_t>(plain.size()));
}

// --- where the vertices are ---------------------------------------------------

TEST_CASE("in the body's own frame the equator lies in z = 0 and the prime meridian in y = 0") {
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    const auto& equator = onlyLine(grid, GridLine::Equator).points;
    const auto& meridian = onlyLine(grid, GridLine::PrimeMeridian).points;
    CHECK(std::ranges::count_if(equator, [](const Position& p) noexcept {
              return !nearlyEqual(p.z.value(), 0.0, Tolerance{0.0});
          }) == 0);
    CHECK(std::ranges::count_if(meridian, [](const Position& p) noexcept {
              return !nearlyEqual(p.y.value(), 0.0, Tolerance{0.0});
          }) == 0);
    // Longitude 0, not 180.
    CHECK(std::ranges::count_if(meridian,
                                [](const Position& p) noexcept { return p.x.value() < 0.0; }) == 0);
}

TEST_CASE("the parallels are every 10 degrees") {
    // In the body's own frame, worked out from the task's spacing: parallel
    // k, south to north, at latitude 10 k degrees for k = -8 ... 8 without 0,
    // so z = R sin(10 k degrees) on its first point. A micrometre, for the
    // reason kSpacingToleranceMetres gives.
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    std::vector<f64> found;
    for (const GridPolyline& line : grid.lines()) {
        if (line.line == GridLine::Parallel) found.push_back(line.points.front().z.value());
    }
    std::vector<f64> expected;
    for (int k = -8; k <= 8; ++k) {
        if (k != 0) expected.push_back(kRadius * std::sin(degreesToRadians(10.0 * k)));
    }
    REQUIRE(found.size() == expected.size());
    CHECK(std::ranges::count_if(std::views::zip(found, expected), [](const auto& pair) noexcept {
              return std::abs(std::get<0>(pair) - std::get<1>(pair)) > kSpacingToleranceMetres;
          }) == 0);
}

TEST_CASE("the meridians are every 15 degrees") {
    // Meridian j, from 15 degrees east, at longitude 15 j degrees, so its
    // point at the equator is (R cos, R sin, 0) of that longitude.
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    std::vector<Position> atEquator;
    for (const GridPolyline& line : grid.lines()) {
        if (line.line == GridLine::Meridian) atEquator.push_back(line.points.at(180));
    }
    REQUIRE(atEquator.size() == 23);
    std::size_t wrong = 0;
    f64 longitudeDegrees = 15.0;
    for (const Position& p : atEquator) {
        const f64 longitude = degreesToRadians(longitudeDegrees);
        const bool off =
            std::abs(p.x.value() - (kRadius * std::cos(longitude))) > kSpacingToleranceMetres ||
            std::abs(p.y.value() - (kRadius * std::sin(longitude))) > kSpacingToleranceMetres;
        if (off) ++wrong;
        longitudeDegrees += 15.0;
    }
    CHECK(wrong == 0);
}

TEST_CASE("a meridian runs south to north and a parallel eastward, closing on itself") {
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    const auto& meridian = onlyLine(grid, GridLine::PrimeMeridian).points;
    CHECK(meridian.front().z.value() < 0.0);
    CHECK(meridian.back().z.value() > 0.0);
    const auto& equator = onlyLine(grid, GridLine::Equator).points;
    CHECK(equator.front().bitIdentical(equator.back()));
    // A quarter of the way round the equator is longitude 90 east, +y.
    CHECK(equator.at(180).y.value() > kRadius * (1.0 - 1e-15));
}

TEST_CASE("every vertex lies on the sphere, to the resolution of a double") {
    const f64 bodyFixed = worstResidualSpacings(
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{}));
    const f64 turned = worstResidualSpacings(
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), epochRotation()));
    INFO("worst " << bodyFixed << " spacings in the body's frame and " << turned
                  << " turned, against budgets of " << kBodyFixedBudgetSpacings << " and "
                  << kTurnedBudgetSpacings);
    CHECK(bodyFixed <= kBodyFixedBudgetSpacings);
    CHECK(turned <= kTurnedBudgetSpacings);
}

TEST_CASE("the grid is turned by exactly the rotation it is handed") {
    // The wiring: latitude 0, longitude 0 is (R, 0, 0) in the body's frame,
    // exactly, so the grid's point there is that point turned by the
    // quaternion -- bit for bit.
    const Quat rotation = epochRotation();
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), rotation);
    const Position atOrigin = onlyLine(grid, GridLine::PrimeMeridian).points.at(180);
    CHECK(atOrigin.bitIdentical(rotation.rotate(Position{kRadius, 0.0, 0.0})));
    CHECK(onlyLine(grid, GridLine::Equator).points.at(0).bitIdentical(atOrigin));
}

TEST_CASE("the prime meridian, the equator and the pole are where Skyfield puts them") {
    // Independent of ERFA: the fixture's matrix at the epoch's row, applied
    // here in plain doubles, to three points chosen so that no symmetry can
    // hide a fault -- a mirror, a transposed rotation or a swapped axis moves
    // at least one of them by degrees.
    const RotationMatrix fixture = fixtureMatrixAtEpoch();
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), epochRotation());
    const auto& meridian = onlyLine(grid, GridLine::PrimeMeridian).points;
    const auto& equator = onlyLine(grid, GridLine::Equator).points;
    struct Check {
        std::string_view what;
        Position grid;
        Plain bodyFixed;
    };
    const std::array<Check, 3> checks{
        {
            Check{
                .what = "latitude 0, longitude 0",
                .grid = meridian.at(180),
                .bodyFixed = {.x = kRadius, .y = 0.0, .z = 0.0},
            },
            Check{
                .what = "latitude 0, longitude 90 east",
                .grid = equator.at(180),
                .bodyFixed = {.x = 0.0, .y = kRadius, .z = 0.0},
            },
            Check{
                .what = "the north pole",
                .grid = meridian.at(360),
                .bodyFixed = {.x = 0.0, .y = 0.0, .z = kRadius},
            },
        },
    };
    for (const Check& check : checks) {
        const f64 angle = angleBetweenPlain({
            .measured = plainOf(check.grid),
            .expected = transposeTimes(fixture, check.bodyFixed),
        });
        INFO(check.what << ": " << angle << " rad from Skyfield, budget " << kFixtureBudgetRadians);
        CHECK(angle <= kFixtureBudgetRadians);
    }
}

// --- what can be seen ---------------------------------------------------------

TEST_CASE("a segment wholly on the near side of the horizon comes back unchanged") {
    const Position eye{kRadius + 400e3, 0.0, 0.0};
    const Segment near{
        .from = Position{kRadius, 0.0, 0.0},
        .to = Position{kRadius * std::cos(0.01), kRadius * std::sin(0.01), 0.0},
    };
    const Segment seen = seenOrFail(visiblePart(near, eye, Metres{kRadius}));
    CHECK(seen.from.bitIdentical(near.from));
    CHECK(seen.to.bitIdentical(near.to));
}

TEST_CASE("a segment wholly beyond the horizon is not seen at all") {
    const Position eye{kRadius + 400e3, 0.0, 0.0};
    const Segment far{.from = Position{-kRadius, 0.0, 0.0}, .to = Position{0.0, kRadius, 0.0}};
    CHECK(!visiblePart(far, eye, Metres{kRadius}).has_value());
    // And from inside the sphere, nothing is.
    const Segment near{.from = Position{kRadius, 0.0, 0.0}, .to = Position{0.0, 0.0, kRadius}};
    CHECK(!visiblePart(near, Position{1000.0, 0.0, 0.0}, Metres{kRadius}).has_value());
}

TEST_CASE("a segment across the horizon is cut on the horizon plane, its seen end kept") {
    // An eye at distance d on +x sees the sphere down to the plane x = R^2/d,
    // worked out on paper. The chord from (R, 0, 0) to (-R, 0, 0) crosses it
    // there. A micrometre is far below a pixel at any range a probe draws
    // and far above the nanometres rounding costs.
    const f64 d = kRadius + 400e3;
    const Position eye{d, 0.0, 0.0};
    const Position seenEnd{kRadius, 0.0, 0.0};
    const Position hiddenEnd{-kRadius, 0.0, 0.0};
    constexpr f64 kMicrometre = 1e-6;

    const f64 plane = (kRadius * kRadius) / d;

    const Segment forward =
        seenOrFail(visiblePart({.from = seenEnd, .to = hiddenEnd}, eye, Metres{kRadius}));
    CHECK(forward.from.bitIdentical(seenEnd));
    CHECK(std::abs(forward.to.x.value() - plane) <= kMicrometre);

    const Segment backward =
        seenOrFail(visiblePart({.from = hiddenEnd, .to = seenEnd}, eye, Metres{kRadius}));
    CHECK(backward.to.bitIdentical(seenEnd));
    CHECK(std::abs(backward.from.x.value() - plane) <= kMicrometre);
}

TEST_CASE("seen from far above the north pole, the northern half is drawn and nothing else") {
    // From 10^12 m up the z axis the horizon plane is z = R^2 / 10^12, 41 m
    // above the equator, so: the 8 northern parallels whole, 720 segments
    // each; every meridian's northern 179 segments whole and the one across
    // the equator cut, 180 pieces each; the equator and the south, nothing.
    // 2 x (8 x 720 + 24 x 180) = 20 160 vertices.
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    const Camera camera = cameraAt(Position{0.0, 0.0, 1e12});
    LineBatch batch{layoutOf(kEarthGridCounts).vertexCount()};
    REQUIRE(addVisibleGrid(batch, grid, kGridColours, camera).has_value());
    REQUIRE(batch.size() == VertexCount{20'160U});

    // In the grid's order: the prime meridian last, 360 green vertices, and
    // grey before them, since the equator is out of sight.
    const auto vertices = batch.vertices();
    constexpr std::size_t kGreen = 360;
    const auto green = colourBits(kGridColours.primeMeridian);
    const auto grey = colourBits(kGridColours.grid);
    CHECK(std::ranges::count_if(vertices.last(kGreen), [&green](const LineVertex& v) {
              return colourBits(v.colour) == green;
          }) == static_cast<std::ptrdiff_t>(kGreen));
    CHECK(std::ranges::count_if(vertices.first(vertices.size() - kGreen),
                                [&grey](const LineVertex& v) {
                                    return colourBits(v.colour) == grey;
                                }) == static_cast<std::ptrdiff_t>(vertices.size() - kGreen));
}

TEST_CASE("a grid that does not fit is refused whole, the batch unchanged") {
    const PlanetaryGrid grid =
        PlanetaryGrid::make(Metres{kRadius}, layoutOf(kEarthGridCounts), Quat{});
    const Camera camera = cameraAt(Position{0.0, 0.0, 1e12});
    // Two vertices already in it, and room for one segment fewer than the
    // grid needs.
    LineBatch batch{VertexCount{20'160U}};
    REQUIRE(batch
                .addSegment({.from = Position{0.0, 0.0, 0.0}, .to = Position{1.0, 0.0, 0.0}},
                            kGridColours.grid,
                            camera)
                .has_value());
    const auto refused = addVisibleGrid(batch, grid, kGridColours, camera);
    REQUIRE(!refused.has_value());
    CHECK(refused.error() == LineBatchError::OverCapacity);
    CHECK(batch.size() == VertexCount{2U});
}

// --- the horizon --------------------------------------------------------------

namespace {

// Eyes to look from: 400 km up along each axis and an oblique direction, the
// probes' 1 km, and 1 AU away, where the horizon is nearly a great circle.
// The last, in the equatorial plane between x and y, is leastAlignedAxis's
// third case, z least aligned, which no eye reached until the phase A gate
// read the coverage (register decision 413). It runs that line; it cannot
// judge it, and nothing can: crossing a direction with a coordinate axis is
// exact, and length() is exact at every scale, so any axis not parallel to
// the eye gives the same horizon -- measured, returning x there instead
// passes every case here, and scripts/mutants/m1-23.json declares it so.
[[nodiscard]] std::vector<Position> horizonEyes() {
    const f64 up = kRadius + 400e3;
    const f64 oblique = up * std::numbers::inv_sqrt3;
    const f64 equatorial = up * std::numbers::sqrt2 / 2.0;
    return {
        Position{up, 0.0, 0.0},
        Position{0.0, -up, 0.0},
        Position{0.0, 0.0, up},
        Position{oblique, -oblique, oblique},
        Position{kRadius + 1e3, 0.0, 0.0},
        Position{1.495978707e11, 0.0, 0.0},
        Position{equatorial, equatorial, 0.0},
    };
}

// The horizon from an eye outside the sphere, or a failed test; written as
// seenOrFail is, for its reasons.
[[nodiscard]] Horizon horizonOrFail(const Position& eye) {
    const std::optional<Horizon> horizon = horizonFrom(eye, Metres{kRadius});
    REQUIRE(horizon.has_value());
    return horizon.value_or(Horizon{});
}

} // namespace

TEST_CASE("there is no horizon from on or inside the sphere") {
    CHECK(!horizonFrom(Position{1000.0, 0.0, 0.0}, Metres{kRadius}).has_value());
    CHECK(!horizonFrom(Position{0.0, 0.0, 0.0}, Metres{kRadius}).has_value());
    CHECK(!horizonFrom(Position{kRadius, 0.0, 0.0}, Metres{kRadius}).has_value());
}

TEST_CASE("from an eye on an axis the horizon is the circle worked out on paper") {
    // An eye at distance d on +x: the circle x = R^2/d, of radius
    // R sqrt(1 - R^2/d^2), about the x axis.
    const f64 d = kRadius + 400e3;
    const Horizon horizon = horizonOrFail(Position{d, 0.0, 0.0});
    constexpr f64 kMicrometre = 1e-6; // far below a pixel, far above rounding
    CHECK(std::abs(horizon.centre.x.value() - ((kRadius * kRadius) / d)) <= kMicrometre);
    CHECK(std::abs(horizon.centre.y.value()) <= kMicrometre);
    CHECK(std::abs(horizon.centre.z.value()) <= kMicrometre);
    const f64 radius = kRadius * std::sqrt(1.0 - ((kRadius * kRadius) / (d * d)));
    CHECK(std::abs(horizon.radius.value() - radius) <= kMicrometre);
    // The two directions span the plane x = const: unit, perpendicular to x
    // and to each other.
    CHECK(std::abs(horizon.across.x.value()) <= 1e-15);
    CHECK(std::abs(horizon.around.x.value()) <= 1e-15);
    CHECK(std::abs(dot(horizon.across, horizon.around).value()) <= 1e-15);
}

TEST_CASE("every point of the horizon is on the sphere and on the horizon plane") {
    // Distance from the sphere in spacings at the radius, and from the plane
    // E . P = R^2 in metres, over 720 points from each eye.
    f64 worstSphere = 0.0;
    f64 worstPlane = 0.0;
    for (const Position& eye : horizonEyes()) {
        const Horizon horizon = horizonOrFail(eye);
        const f64 eyeDistance = length(eye).value();
        for (std::uint32_t k = 0; k < kHorizonSegments; ++k) {
            const Position p =
                pointOn(horizon,
                        Radians{(static_cast<f64>(k) * kTau) / static_cast<f64>(kHorizonSegments)});
            worstSphere = std::max(worstSphere, std::abs(distanceFromSphere(p)) / kSpacingAtRadius);
            const f64 abovePlane = (dot(eye, p).value() - (kRadius * kRadius)) / eyeDistance;
            worstPlane = std::max(worstPlane, std::abs(abovePlane));
        }
    }
    INFO("worst " << worstSphere << " spacings from the sphere and " << worstPlane
                  << " m from the plane, against " << kHorizonSphereBudgetSpacings << " and "
                  << kHorizonPlaneBudgetMetres);
    CHECK(worstSphere <= kHorizonSphereBudgetSpacings);
    CHECK(worstPlane <= kHorizonPlaneBudgetMetres);
}

TEST_CASE("the horizon is drawn in 720 segments in its colour, closed") {
    const Camera camera = cameraAt(Position{kRadius + 400e3, 0.0, 0.0});
    LineBatch batch{VertexCount{2 * kHorizonSegments}};
    REQUIRE(addHorizon(batch, Metres{kRadius}, kGridColours.horizon, camera).has_value());
    REQUIRE(batch.size() == VertexCount{2 * kHorizonSegments});
    const auto blue = colourBits(kGridColours.horizon);
    CHECK(std::ranges::count_if(batch.vertices(), [&blue](const LineVertex& v) {
              return colourBits(v.colour) == blue;
          }) == static_cast<std::ptrdiff_t>(2 * kHorizonSegments));
    // Closed: the last vertex is the first.
    CHECK(bitIdentical(batch.vertices().front().position, batch.vertices().back().position));
}

TEST_CASE("a horizon that does not fit is refused whole") {
    const Camera camera = cameraAt(Position{kRadius + 400e3, 0.0, 0.0});
    LineBatch small{VertexCount{(2 * kHorizonSegments) - 2}};
    const auto refused = addHorizon(small, Metres{kRadius}, kGridColours.horizon, camera);
    REQUIRE(!refused.has_value());
    CHECK(refused.error() == LineBatchError::OverCapacity);
    CHECK(small.size() == VertexCount{0U});
}

TEST_CASE("from inside the sphere no horizon is drawn, and that is not an error") {
    LineBatch inside{VertexCount{2 * kHorizonSegments}};
    REQUIRE(
        addHorizon(inside, Metres{kRadius}, kGridColours.horizon, cameraAt(Position{1.0, 0.0, 0.0}))
            .has_value());
    CHECK(inside.size() == VertexCount{0U});
}

TEST_CASE("the grid's colours are decision 327's radiances") {
    // Written out here, not read back from the header (VERIFICATION.md rule 2).
    CHECK(colourBits(kGridColours.grid) ==
          colourBits({.r = 130.0F, .g = 130.0F, .b = 130.0F, .a = 1.0F}));
    CHECK(colourBits(kGridColours.equator) ==
          colourBits({.r = 437.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}));
    CHECK(colourBits(kGridColours.primeMeridian) ==
          colourBits({.r = 0.0F, .g = 130.0F, .b = 0.0F, .a = 1.0F}));
    CHECK(colourBits(kGridColours.horizon) ==
          colourBits({.r = 0.0F, .g = 0.0F, .b = 1288.0F, .a = 1.0F})); // decision 338
    CHECK(nearlyEqual(kWgs84SemiMajorAxis.value(), kRadius, Tolerance{0.0}));
}
