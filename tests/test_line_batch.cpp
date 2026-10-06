//
// Tests for view/LineBatch.hpp: the lines a frame draws, gathered without a
// graphics card (M1-19; register decisions 279 and 285).
//
// **Nothing here includes a Vulkan or SDL header**: this suite links
// orbsim_view, which links orbsim_core and nothing else. Where the GPU puts
// these vertices is the `lines` probe's, and tests/test_probe_lines.cpp's.
//
// **Where the expected values come from.** Each vertex is held bit for bit to
// `toRenderSpace` of the world point it was made from, called here -- the
// claim is that the batch does no arithmetic of its own on a position, so the
// one function that narrows is the reference, not the thing under test
// (test_camera.cpp holds that function to its own budget). The axis colours
// are written out from register decision 279's numbers, and their equal
// brightness is worked out again here from Rec. 709's luminance weights,
// never read back from the header to be compared with itself (VERIFICATION.md
// rule 2).
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/LineBatch.hpp"
#include "view/VertexLayout.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <span>
#include <vector>

using namespace orb;
using namespace orb::view;

namespace {

// A camera from constants the factory accepts: the probes' 45-degree field of
// view and 1 m near plane, unrotated, at `position`.
[[nodiscard]] Camera cameraAt(const Position& position) {
    const auto camera =
        Camera::from(position, Quat{}, Radians{std::numbers::pi / 4.0}, Metres{1.0});
    REQUIRE(camera.has_value());
    return *camera;
}

// A colour's four channels as their bit patterns, so two colours compare by
// bit identity with ==, and +0 is told from -0.
[[nodiscard]] std::array<std::uint32_t, 4> colourBits(const Rgba& colour) noexcept {
    // std::to_array, because MSVC (C5246) and gcc (-Wmissing-braces) want a
    // std::array's inner braces -- the shape src/view/Lambert.cpp settled on.
    return std::to_array({
        bitsOf(colour.r),
        bitsOf(colour.g),
        bitsOf(colour.b),
        bitsOf(colour.a),
    });
}

// The batch's vertices, copied, so a test reads them with bounds-checked at().
[[nodiscard]] std::vector<LineVertex> verticesOf(const LineBatch& batch) {
    return {batch.vertices().begin(), batch.vertices().end()};
}

// The vertex at `index` is `world` through toRenderSpace, in `colour`.
void requireVertex(const std::vector<LineVertex>& vertices,
                   std::size_t index,
                   const Position& world,
                   const Camera& camera,
                   const Rgba& colour) {
    INFO("vertex " << index << " of " << vertices.size());
    REQUIRE(index < vertices.size());
    CHECK(bitIdentical(vertices.at(index).position, toRenderSpace(world, camera)));
    CHECK(colourBits(vertices.at(index).colour) == colourBits(colour));
}

constexpr Rgba kWhite{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F};

} // namespace

TEST_CASE("a polyline of n points is 2(n - 1) vertices, each segment's ends in order") {
    const Camera camera = cameraAt(Position{0.5, -2.0, 7.0});
    const std::array<Position, 6> points{
        {
            Position{0.0, 0.0, 0.0},
            Position{1.0, 0.0, 0.0},
            Position{1.0, 2.0, 0.0},
            Position{1.0, 2.0, -3.0},
            Position{-4.0, 2.0, -3.0},
            Position{-4.0, 5.0, 6.0},
        },
    };
    for (std::size_t n = 2; n <= points.size(); ++n) {
        INFO("n = " << n);
        LineBatch batch{VertexCount{64U}};
        REQUIRE(batch.addPolyline(std::span(points).first(n), kWhite, camera).has_value());
        const std::vector<LineVertex> vertices = verticesOf(batch);
        REQUIRE(vertices.size() == 2 * (n - 1));
        CHECK(batch.size() == VertexCount{static_cast<std::uint32_t>(2 * (n - 1))});
        for (std::size_t segment = 0; segment + 1 < n; ++segment) {
            requireVertex(vertices, 2 * segment, points.at(segment), camera, kWhite);
            requireVertex(vertices, (2 * segment) + 1, points.at(segment + 1), camera, kWhite);
        }
    }
}

TEST_CASE("a polyline of no point or one point adds nothing, and is not an error") {
    const Camera camera = cameraAt(Position{0.0, 0.0, 0.0});
    const std::array<Position, 1> one{{Position{3.0, 4.0, 5.0}}};
    LineBatch batch{VertexCount{4U}};
    CHECK(batch.addPolyline(std::span<const Position>{}, kWhite, camera).has_value());
    CHECK(batch.addPolyline(one, kWhite, camera).has_value());
    CHECK(batch.vertices().empty());
    CHECK(batch.size() == VertexCount{0U});
}

TEST_CASE("a segment is its two ends, in order") {
    const Camera camera = cameraAt(Position{-1.0, 2.0, 3.0});
    const Segment segment{.from = Position{10.0, 0.0, 0.0}, .to = Position{0.0, -20.0, 5.0}};
    const Rgba colour{.r = 2.0F, .g = 3.0F, .b = 5.0F, .a = 1.0F};
    LineBatch batch{VertexCount{2U}};
    REQUIRE(batch.addSegment(segment, colour, camera).has_value());
    REQUIRE(batch.vertices().size() == 2);
    requireVertex(verticesOf(batch), 0, segment.from, camera, colour);
    requireVertex(verticesOf(batch), 1, segment.to, camera, colour);
}

TEST_CASE("addAxes is three segments, X red, Y green and Z blue, in that order") {
    const Camera camera = cameraAt(Position{3.0, 3.0, 2.0});
    const Position origin{0.25, -0.5, 1.0};
    constexpr f64 kLength = 2.0;
    LineBatch batch{VertexCount{6U}};
    REQUIRE(batch.addAxes(origin, Metres{kLength}, camera).has_value());
    const std::vector<LineVertex> vertices = verticesOf(batch);
    REQUIRE(vertices.size() == 6);

    // Register decision 279's radiances, written out rather than read from
    // the header.
    const Rgba red{.r = 437.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    const Rgba green{.r = 0.0F, .g = 130.0F, .b = 0.0F, .a = 1.0F};
    const Rgba blue{.r = 0.0F, .g = 0.0F, .b = 1288.0F, .a = 1.0F};
    const Position x{origin.x.value() + kLength, origin.y.value(), origin.z.value()};
    const Position y{origin.x.value(), origin.y.value() + kLength, origin.z.value()};
    const Position z{origin.x.value(), origin.y.value(), origin.z.value() + kLength};
    requireVertex(vertices, 0, origin, camera, red);
    requireVertex(vertices, 1, x, camera, red);
    requireVertex(vertices, 2, origin, camera, green);
    requireVertex(vertices, 3, y, camera, green);
    requireVertex(vertices, 4, origin, camera, blue);
    requireVertex(vertices, 5, z, camera, blue);
}

TEST_CASE("the axis colours are equally bright to the eye, as decision 279 derives them") {
    // Rec. 709's luminance weights (ITU-R BT.709-6, item 3.2). Each colour's
    // luminance is its one channel times that channel's weight; green's is
    // the target, and red's and blue's are whole numbers rounded from the
    // exact value, so each may be off by half a unit times its own weight.
    constexpr f64 kRedWeight = 0.2126;
    constexpr f64 kGreenWeight = 0.7152;
    constexpr f64 kBlueWeight = 0.0722;
    const f64 target = kGreenWeight * static_cast<f64>(kAxisYColour.g);
    CHECK(std::abs((kRedWeight * static_cast<f64>(kAxisXColour.r)) - target) <= 0.5 * kRedWeight);
    CHECK(std::abs((kBlueWeight * static_cast<f64>(kAxisZColour.b)) - target) <= 0.5 * kBlueWeight);
    // And each axis is one channel only, opaque.
    CHECK(colourBits(kAxisXColour) ==
          colourBits(Rgba{.r = 437.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}));
    CHECK(colourBits(kAxisYColour) ==
          colourBits(Rgba{.r = 0.0F, .g = 130.0F, .b = 0.0F, .a = 1.0F}));
    CHECK(colourBits(kAxisZColour) ==
          colourBits(Rgba{.r = 0.0F, .g = 0.0F, .b = 1288.0F, .a = 1.0F}));
}

namespace {

// One camera of the sweep below: the whole lattice as one polyline, every
// vertex held to toRenderSpace.
void requirePolylineSeenFrom(const std::vector<Position>& points, const Position& cameraPosition) {
    INFO("camera at " << cameraPosition.x.value() << ", " << cameraPosition.y.value() << ", "
                      << cameraPosition.z.value());
    const Camera camera = cameraAt(cameraPosition);
    LineBatch batch{VertexCount{static_cast<std::uint32_t>(2 * points.size())}};
    REQUIRE(batch.addPolyline(points, kWhite, camera).has_value());
    const std::vector<LineVertex> vertices = verticesOf(batch);
    REQUIRE(vertices.size() == 2 * (points.size() - 1));
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        requireVertex(vertices, 2 * i, points.at(i), camera, kWhite);
        requireVertex(vertices, (2 * i) + 1, points.at(i + 1), camera, kWhite);
    }
}

} // namespace

TEST_CASE("every position is toRenderSpace's, bit for bit, at every scale") {
    // A lattice rather than a random sweep: every combination of these
    // magnitudes and directions, for the points and the camera alike -- from
    // the origin through a metre, Earth's radius, the Moon's distance and the
    // astronomical unit -- so the camera-relative difference is both tiny and
    // enormous, and nothing needs a seed.
    const std::array<f64, 6> magnitudes{{0.0, 1.0, 1.0e3, 6.371e6, 3.844e8, 1.495978707e11}};
    const std::array<Direction, 4> directions{
        {
            Direction{1.0, 0.0, 0.0},
            Direction{0.0, -1.0, 0.0},
            Direction{0.6, 0.0, -0.8},
            Direction{-0.48, 0.6, 0.64},
        },
    };
    std::vector<Position> points;
    for (const f64 magnitude : magnitudes) {
        for (const Direction& d : directions) {
            points.emplace_back(
                d.x.value() * magnitude, d.y.value() * magnitude, d.z.value() * magnitude);
        }
    }
    for (const Position& cameraPosition : points) {
        requirePolylineSeenFrom(points, cameraPosition);
    }
}

namespace {

// Refused, and for the one reason there is. has_value() is asked first:
// error() on an expected that holds a value is undefined, and compares equal
// to the first enumerator in practice (VERIFICATION.md rule 23's fourth case).
void requireOverCapacity(const std::expected<void, LineBatchError>& result) {
    REQUIRE(!result.has_value());
    CHECK(result.error() == LineBatchError::OverCapacity);
}

// The batch holds exactly `before`, bit for bit.
void requireUnchanged(const LineBatch& batch, const std::vector<LineVertex>& before) {
    const std::vector<LineVertex> after = verticesOf(batch);
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        INFO("vertex " << i);
        CHECK(bitIdentical(after.at(i).position, before.at(i).position));
        CHECK(colourBits(after.at(i).colour) == colourBits(before.at(i).colour));
    }
}

} // namespace

TEST_CASE("past the capacity is refused by name, and leaves the batch unchanged") {
    const Camera camera = cameraAt(Position{0.0, 0.0, 5.0});
    const Segment first{.from = Position{0.0, 0.0, 0.0}, .to = Position{1.0, 0.0, 0.0}};
    const Segment second{.from = Position{0.0, 1.0, 0.0}, .to = Position{1.0, 1.0, 0.0}};
    const std::array<Position, 3> threePoints{
        {
            Position{0.0, 2.0, 0.0},
            Position{1.0, 2.0, 0.0},
            Position{2.0, 2.0, 0.0},
        },
    };

    LineBatch batch{VertexCount{4U}};
    REQUIRE(batch.addSegment(first, kWhite, camera).has_value());
    const std::vector<LineVertex> before = verticesOf(batch);

    // A polyline needing four vertices where two are left: refused whole,
    // not cut to the one segment that would fit.
    requireOverCapacity(batch.addPolyline(threePoints, kWhite, camera));
    // The axes need six.
    requireOverCapacity(batch.addAxes(Position{0.0, 0.0, 0.0}, Metres{1.0}, camera));
    requireUnchanged(batch, before);

    // Exactly full is allowed; one segment more is not, and changes nothing.
    REQUIRE(batch.addSegment(second, kWhite, camera).has_value());
    CHECK(batch.size() == VertexCount{4U});
    const std::vector<LineVertex> full = verticesOf(batch);
    requireOverCapacity(batch.addSegment(first, kWhite, camera));
    requireUnchanged(batch, full);
}

TEST_CASE("filling, clearing and refilling never moves the storage") {
    // The capacity test the task asks for beside reading the code: the
    // vertices stay where the constructor put them while the batch is filled
    // to capacity, emptied and filled again -- a reallocation would move them.
    const Camera camera = cameraAt(Position{0.0, 0.0, 5.0});
    LineBatch batch{VertexCount{6U}};
    CHECK(batch.capacity() == VertexCount{6U});
    REQUIRE(batch.addAxes(Position{0.0, 0.0, 0.0}, Metres{1.0}, camera).has_value());
    const LineVertex* const storage = batch.vertices().data();

    batch.clear();
    CHECK(batch.vertices().empty());
    CHECK(batch.size() == VertexCount{0U});
    CHECK(batch.capacity() == VertexCount{6U});

    REQUIRE(batch.addAxes(Position{1.0, 1.0, 1.0}, Metres{2.0}, camera).has_value());
    CHECK(batch.size() == VertexCount{6U});
    CHECK(batch.vertices().data() == storage);
}

TEST_CASE("an empty batch has nothing to draw, and that is not an error") {
    const LineBatch batch{VertexCount{8U}};
    CHECK(batch.vertices().empty());
    CHECK(batch.size() == VertexCount{0U});
    CHECK(batch.capacity() == VertexCount{8U});
}
