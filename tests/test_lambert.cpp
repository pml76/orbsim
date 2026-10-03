//
// Tests for view/Lambert.hpp: the albedo, the lambert probes' patch, and the
// Lambert constants narrowed for the shader (M1-18; register decisions 258
// and 267-271).
//
// **Nothing here includes a Vulkan or SDL header**: this suite links
// orbsim_view, which links orbsim_core and nothing else. What the GPU makes of
// these numbers is tests/test_radiometry.cpp's, from the read-back frames.
//
// **Where the expected values come from.** The patch's corners, its normal and
// whether it fills the frame are worked out below from the placement the
// register states -- a 100 m square, 10 m ahead, tilted about the horizontal
// axis -- with this file's own trigonometry, and never read back from
// squarePatch to be compared with itself (VERIFICATION.md rule 2).
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Lambert.hpp"
#include "view/PushConstants.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string_view>

using namespace orb;
using namespace orb::view;

namespace {

constexpr f64 kNotANumber = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();

// The placement the register states (decision 270): a 100 m square centred
// 10 m in front of a camera at the origin looking down -z.
constexpr f64 kSide = 100.0;
constexpr f64 kHalfSide = kSide / 2.0;
constexpr f64 kAhead = 10.0;

// The probe camera's frame (decision 190): 45 degrees vertically, 1280x720.
constexpr f64 kHalfFovVertical = std::numbers::pi / 8.0;
constexpr f64 kAspect = 1280.0 / 720.0;
constexpr f64 kNearPlane = 1.0;

// A patch's corners and normal, worked out here: the untilted square spans x
// and y and faces +z; tilting it by `tilt` about the horizontal axis, top away
// from the camera, turns its "up" edge direction to (0, cos, -sin) and its
// normal to (0, sin, cos).
struct ExpectedPatch {
    Position centre;
    Direction across; // the edge direction along x
    Direction up;     // the edge direction "up" the patch
    Direction normal;
};

[[nodiscard]] ExpectedPatch expectedPatch(f64 tiltRadians) {
    const f64 c = std::cos(tiltRadians);
    const f64 s = std::sin(tiltRadians);
    return {
        .centre = Position{0.0, 0.0, -kAhead},
        .across = Direction{1.0, 0.0, 0.0},
        .up = Direction{0.0, c, -s},
        .normal = Direction{0.0, s, c},
    };
}

[[nodiscard]] PatchPlacement placement(f64 tiltRadians) {
    return {
        .centre = Position{0.0, 0.0, -kAhead},
        .side = Metres{kSide},
        .tilt = Radians{tiltRadians},
    };
}

// One ulp-scale tolerance for coordinates of tens of metres built from one
// sine and one cosine: a few units in the last place of 50.
constexpr Tolerance kCoordinateTolerance{1e-12};
constexpr Tolerance kDirectionTolerance{4.0 * std::numeric_limits<f64>::epsilon()};

// What was produced and what was worked out, named so that the two cannot be
// handed over the wrong way round (non-negotiable 1).
template <typename V> struct Pair {
    V got;
    V want;
};

template <typename V> void requireNear(const Pair<V>& pair, Tolerance tolerance) {
    const V& got = pair.got;
    const V& want = pair.want;
    INFO("got (" << got.x.value() << ", " << got.y.value() << ", " << got.z.value() << "), want ("
                 << want.x.value() << ", " << want.y.value() << ", " << want.z.value() << ")");
    REQUIRE(nearlyEqual(got.x.value(), want.x.value(), tolerance));
    REQUIRE(nearlyEqual(got.y.value(), want.y.value(), tolerance));
    REQUIRE(nearlyEqual(got.z.value(), want.z.value(), tolerance));
}

// Which corner: +1 or -1 along each edge direction, in units of the half side.
struct Corner {
    f64 across{};
    f64 up{};
};

[[nodiscard]] Position cornerOf(const ExpectedPatch& p, Corner corner) {
    const f64 a = kHalfSide * corner.across;
    const f64 u = kHalfSide * corner.up;
    const Position across{p.across.x.value() * a, p.across.y.value() * a, p.across.z.value() * a};
    const Position up{p.up.x.value() * u, p.up.y.value() * u, p.up.z.value() * u};
    return p.centre + across + up;
}

// The patch, as the test expects it: two triangles, each counter-clockwise
// seen from the side the normal points to -- (-,-) (+,-) (+,+), then
// (-,-) (+,+) (-,+).
void requirePatch(f64 tiltRadians) {
    const ExpectedPatch want = expectedPatch(tiltRadians);
    const SquarePatch got = squarePatch(placement(tiltRadians));
    const auto corners = std::to_array({
        cornerOf(want, {.across = -1.0, .up = -1.0}),
        cornerOf(want, {.across = 1.0, .up = -1.0}),
        cornerOf(want, {.across = 1.0, .up = 1.0}),
        cornerOf(want, {.across = -1.0, .up = -1.0}),
        cornerOf(want, {.across = 1.0, .up = 1.0}),
        cornerOf(want, {.across = -1.0, .up = 1.0}),
    });
    for (std::size_t i = 0; i < corners.size(); ++i) {
        INFO("vertex " << i << ", tilt " << tiltRadians << " rad");
        requireNear(Pair<Position>{.got = got.vertices.at(i), .want = corners.at(i)},
                    kCoordinateTolerance);
    }
    requireNear(Pair<Direction>{.got = got.normal, .want = want.normal}, kDirectionTolerance);
}

// The frame's four corner rays, from a camera at the origin looking down -z.
[[nodiscard]] std::array<Direction, 4> cornerRays() {
    const f64 v = std::tan(kHalfFovVertical);
    const f64 h = kAspect * v;
    return std::to_array({
        Direction{-h, -v, -1.0},
        Direction{h, -v, -1.0},
        Direction{h, v, -1.0},
        Direction{-h, v, -1.0},
    });
}

// Where a ray from the origin meets the expected patch's plane, in the
// patch's own coordinates, and how far along the ray. The ray's direction is
// not normalised, so the distance is in units of its z component: a point at
// `along` is `along` metres in front of the camera.
struct Hit {
    f64 along{};
    f64 acrossMetres{};
    f64 upMetres{};
};

[[nodiscard]] Hit hitOf(const ExpectedPatch& p, const Direction& ray) {
    const f64 toPlane = dot(p.normal, p.centre).value();
    const f64 alongRay = dot(p.normal, ray).value();
    const f64 along = toPlane / alongRay;
    const Position point{Metres{ray.x.value() * along},
                         Metres{ray.y.value() * along},
                         Metres{ray.z.value() * along}};
    const Position offset = point - p.centre;
    return {
        .along = along,
        .acrossMetres = dot(p.across, offset).value(),
        .upMetres = dot(p.up, offset).value(),
    };
}

} // namespace

namespace {

// Every corner ray of the frame meets the patch tilted by `tiltRadians`
// inside its square and beyond the near plane.
void requirePatchFillsFrame(f64 tiltRadians) {
    const ExpectedPatch p = expectedPatch(tiltRadians);
    for (const Direction& ray : cornerRays()) {
        const Hit hit = hitOf(p, ray);
        INFO("tilt " << tiltRadians << ", ray (" << ray.x.value() << ", " << ray.y.value()
                     << "): along " << hit.along << ", across " << hit.acrossMetres << ", up "
                     << hit.upMetres);
        REQUIRE(hit.along > kNearPlane);
        REQUIRE(std::abs(hit.acrossMetres) < kHalfSide);
        REQUIRE(std::abs(hit.upMetres) < kHalfSide);
    }
}

// Each component of a narrowed vector, bit for bit. A struct, so the two
// cannot be given the wrong way round (non-negotiable 1).
struct Components {
    Vec4f got;
    std::array<f32, 4> want;
};

void requireComponents(const Components& c, std::string_view what) {
    for (std::size_t i = 0; i < 4; ++i) {
        INFO(what << " component " << i);
        REQUIRE(bitsOf(c.got.at(i)) == bitsOf(c.want.at(i)));
    }
}

} // namespace

// --- the albedo ---------------------------------------------------------------

TEST_CASE("an albedo from 0 to 1 is accepted, both ends included") {
    for (const f64 value : {0.0, 0.3, 1.0}) {
        const auto albedo = Albedo::from(value);
        INFO("albedo " << value);
        REQUIRE(albedo.has_value());
        REQUIRE(bitsOf(albedo->value()) == bitsOf(value));
    }
}

TEST_CASE("an albedo outside 0 to 1 is refused by name") {
    const f64 belowZero = std::nextafter(0.0, -1.0);
    const f64 aboveOne = std::nextafter(1.0, 2.0);
    for (const f64 value : {belowZero, aboveOne, -0.3, 1.3}) {
        const auto albedo = Albedo::from(value);
        INFO("albedo " << value);
        REQUIRE(!albedo.has_value());
        REQUIRE(albedo.error() == AlbedoError::OutsideZeroToOne);
    }
}

TEST_CASE("an albedo that is not finite is refused by name") {
    for (const f64 value : {kNotANumber, kInfinity, -kInfinity}) {
        const auto albedo = Albedo::from(value);
        INFO("albedo " << value);
        REQUIRE(!albedo.has_value());
        REQUIRE(albedo.error() == AlbedoError::NotFinite);
    }
    REQUIRE(describe(AlbedoError::NotFinite) != describe(AlbedoError::OutsideZeroToOne));
}

// --- the patch ----------------------------------------------------------------

TEST_CASE("the untilted patch is the 100 m square 10 m ahead, facing the camera") {
    requirePatch(0.0);
}

TEST_CASE("the tilted patch turns about the horizontal axis, top away from the camera") {
    // 60 degrees (decision 269). The normal is (0, sin 60, cos 60): its
    // component toward a Sun straight behind the camera is cos 60, one half.
    requirePatch(std::numbers::pi / 3.0);
    // And a second angle, so that a sine and a cosine swapped cannot pass by
    // being equal at one of them -- at 45 degrees they are.
    requirePatch(0.25);
}

TEST_CASE("both triangles face the way the normal does") {
    // The winding a back-face cull keeps (decision 271): the cross product of
    // each triangle's two edges points along the normal.
    for (const f64 tilt : {0.0, std::numbers::pi / 3.0}) {
        const SquarePatch patch = squarePatch(placement(tilt));
        for (std::size_t t = 0; t < 2; ++t) {
            const Position a = patch.vertices.at(3 * t);
            const Position b = patch.vertices.at((3 * t) + 1);
            const Position c = patch.vertices.at((3 * t) + 2);
            const auto facing = cross(b - a, c - a);
            const Direction want = expectedPatch(tilt).normal;
            INFO("triangle " << t << ", tilt " << tilt);
            REQUIRE(dot(facing, want).value() > 0.0);
            // Parallel, not merely on the same side: the area is half the
            // square's, so the length is kSide^2, all of it along the normal.
            REQUIRE(nearlyEqual(dot(facing, want).value(), kSide * kSide, Tolerance{1e-9}));
        }
    }
}

TEST_CASE("the patch fills the probe's frame, untilted and tilted") {
    // Every corner ray of the frame meets the patch inside its square and
    // beyond the near plane; the edges between corners then do too, the square
    // being convex. This is the measurement behind decision 269, held: a patch
    // turned 60 degrees about the *vertical* axis would leave the frame's side
    // empty, since the frame sees 36.4 degrees either side and the patch runs
    // off at 30.
    requirePatchFillsFrame(0.0);
    requirePatchFillsFrame(std::numbers::pi / 3.0);
}

// --- the constants the shader reads -------------------------------------------

TEST_CASE("the Lambert constants reach the shader's block unchanged but for the narrowing") {
    const Mat4f matrix = [] {
        Mat4f m{};
        for (std::size_t i = 0; i < m.size(); ++i) {
            m.at(i) = static_cast<f32>(i) + 0.5F; // sixteen different values
        }
        return m;
    }();
    const f64 sin60 = std::sin(std::numbers::pi / 3.0);
    const f64 cos60 = std::cos(std::numbers::pi / 3.0);
    const LambertLighting lighting{
        .albedo = Albedo::from(0.3).value(),
        .irradiance = Irradiance{1361.0},
        .surfaceNormal = Direction{0.0, sin60, cos60},
        .towardSun = Direction{0.0, 0.0, 1.0},
    };
    const LambertPushConstants block = toShaderLambert(lighting, matrix);

    for (std::size_t i = 0; i < matrix.size(); ++i) {
        INFO("matrix element " << i);
        REQUIRE(bitsOf(block.viewProjection.at(i)) == bitsOf(matrix.at(i)));
    }
    // Each the nearest f32 to the double, which is what one static_cast gives.
    REQUIRE(bitsOf(block.albedo) == bitsOf(static_cast<f32>(0.3)));
    REQUIRE(bitsOf(block.irradiance) == bitsOf(1361.0F));
    requireComponents(
        {
            .got = block.surfaceNormal,
            .want = std::to_array({0.0F, static_cast<f32>(sin60), static_cast<f32>(cos60), 0.0F}),
        },
        "normal");
    requireComponents({.got = block.towardSun, .want = std::to_array({0.0F, 0.0F, 1.0F, 0.0F})},
                      "sun");
    // cos 60 in double is 0.5000000000000001; narrowed, it is one half
    // exactly, so the shader's dot product with a Sun along +z is exactly one
    // half -- the tilted probe's radiance is half the untilted one's to the
    // last bit of the factor.
    REQUIRE(bitsOf(block.surfaceNormal.at(2)) == bitsOf(0.5F));
}
