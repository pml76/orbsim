//
// Tests for view/Camera.hpp: the camera, and the one place 64-bit simulation
// coordinates become the 32-bit numbers a graphics card consumes (M1-11;
// ADR 0012 for where this lives, ADR 0021 for the frames, ADR 0022 for why the
// camera validates itself).
//
// **Nothing here includes a Vulkan or SDL header**, and that is the link
// graph's doing rather than anyone's discipline: this suite links orbsim_view,
// which links orbsim_core and nothing else.
//
// **Where the numbers come from.** Every budget below was measured before it
// was written down, and three of the task document's figures did not survive
// that (register decisions 115 and 118):
//
//   * **the naive path does not fail at 400 km.** The document asked that
//     narrowing before subtracting break the 0.05 px budget "by a wide margin"
//     with a camera at 400 km altitude looking at a point at Earth radius.
//     Measured, it lands at 5.8e-4 px -- 86 times *inside* the budget --
//     because half a metre at a range of 400 km is 1.25 microradians. That
//     case is still here, asserted to pass, so the claim cannot quietly come
//     back; the teeth are at 1 AU (95 times over) and at short range (8.6
//     times over at 1 km, and 111 times at 100 m);
//   * **"orthonormal to 1e-15" is unreachable**, and the reason is a coupling
//     that was measured rather than assumed. The view matrix's orthonormality
//     residual tracks how far the orientation is from unit length, at about
//     eight to one across six decades. `Camera` admits 16 ulp, so the matrix
//     is 135 ulp at worst -- measured over 200,000 orientations sitting on
//     that boundary -- and the budget here is 270, twice that;
//   * **the round trip is 132 ulp, not 1e-14.** Same cause: 1e-14 is 45 ulp,
//     which the admitted orientation error alone now exceeds outright.
//
// And one claim the document did not ask for is here because nothing else
// would catch the classic error: **the camera's own axes must land on view
// space's axes**. Orthonormality, the determinant and the round trip are all
// blind to a view matrix that forgot to invert the orientation -- the inverse
// of a rotation is a rotation, and it round-trips perfectly against its own
// inverse.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/Frame.hpp"
#include "view/Mat4.hpp"
#include "view/Projection.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

using namespace orb;
using namespace orb::view;

namespace {

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260922ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 20'000;

constexpr f64 kEps = std::numeric_limits<f64>::epsilon();
constexpr f64 kNotANumber = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();

// The screen the budget is stated at. Not a unit and not a type: register
// decision 105 ruled that a pixel unit and a screen frame wait for M1-80's
// layout, so the viewport transform is written out here, once.
constexpr f64 kScreenWidth = 1920.0;
constexpr f64 kScreenHeight = 1080.0;

// The relative resolution of a 32-bit float: half its epsilon. This is the
// only property of `f32` the law below depends on.
constexpr f64 kUnitRoundoff32 = 0x1p-24;

// The narrowing's screen error, as a law rather than as a number at one field
// of view:
//
//     error <= kNarrowingBound * (screenHeight / 2) * 2^-24 * focalLength
//
// Measured worst **0.683** of that bound over twelve geometries spanning six
// decades of world magnitude and fields of view from 10 to 120 degrees, with
// the ratio constant to three digits wherever the range was 400 km. The whole
// chain computed in 32 bits, as the graphics card does it, measures 1.55 of
// the bound's coefficient -- so one bound covers both paths, which is why
// there is one constant here and not two.
constexpr f64 kNarrowingBound = 2.0;

// Phase A's stated acceptance criterion, which the law above beats by a factor
// of about 320 at a 45-degree field of view. Both are asserted: the law is the
// regression guard, this is the promise the milestone made.
constexpr f64 kAcceptancePixels = 0.05;

// The view matrix, over the worst orientation `Camera::from` will admit.
//
// **This number is `kUnitQuaternionTolerance` times the coupling, and nothing
// else.** Measured 135 ulp for the orthonormality residual and for the
// determinant, and 132 for the round trip, over 200,000 orientations nudged to
// the 16 ulp boundary in both directions. Twice the worst of those, as this
// project's budgets are (register decisions 54 and 85). One constant covers
// all three claims because one thing dominates all three.
constexpr f64 kViewMatrixUlps = 270.0;

// The narrowing's independence from the distance to the origin is a claim
// about *shape*, not about accuracy, so its margin is deliberately generous:
// measured 0.06 % between Earth radius and 1 AU, asserted at 5 %.
constexpr f64 kFlatnessFraction = 0.05;

constexpr Direction kUpHint{0.0, 0.0, 1.0};
constexpr Metres kNearPlane{0.05};
constexpr Aspect kScreenAspect{kScreenWidth / kScreenHeight};

// A direction with no small, zero or equal components, so that no coordinate
// drops out and no transposition cancels. A function rather than a namespace
// constant because `normalize` is not `constexpr`, and a namespace object that
// needs a dynamic initialiser is what `-Wglobal-constructors` reports.
[[nodiscard]] Direction probeDirection() noexcept {
    return normalize(Direction{0.53, -0.41, 0.74});
}

[[nodiscard]] Radians degrees(f64 value) noexcept { return Radians{value * kPi / 180.0}; }

// The tolerance `Camera::from` admits, in ulp of 1.0, so that the boundary
// cases below track the constant rather than restating it.
const f64 kAdmittedUlps = kUnitQuaternionTolerance.value() / kEps;

// A quaternion whose norm sits a stated number of ulp from one, **measured
// rather than computed**.
//
// Scaling a normalised quaternion by `1 + n*eps` does not land at `n` ulp:
// `normalize` itself leaves up to 1.5 ulp (measured over 500,000 draws), so
// the two errors add and a probe aimed at 3 ulp can arrive at 4.5 and be
// refused. This suite found that out by failing. Dividing by the measured norm
// lands within one rounding of the target instead.
[[nodiscard]] Quat atUlpsOffUnit(const Quat& q, f64 ulps) noexcept {
    const f64 norm = std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    const f64 factor = (1.0 + (ulps * kEps)) / norm;
    return Quat{q.w * factor, q.x * factor, q.y * factor, q.z * factor};
}

[[nodiscard]] Position along(const Direction& direction, Metres distance) noexcept {
    return Position{direction.x.value() * distance.value(),
                    direction.y.value() * distance.value(),
                    direction.z.value() * distance.value()};
}

[[nodiscard]] Camera cameraOrFail(const Position& position,
                                  const Quat& orientation,
                                  Radians verticalFov,
                                  Metres nearPlane) {
    const auto made = Camera::from(position, orientation, verticalFov, nearPlane);
    REQUIRE(made.has_value());
    return *made;
}

// The error a refused camera reports.
//
// **`REQUIRE(!made.has_value())` before `made.error()`, always.** Reading
// `error()` on a `std::expected` that holds a value is undefined behaviour and
// in practice compares equal to the zero enumerator -- so a refusal case
// written without this guard passes while the factory accepts the value it was
// written to refuse. That is not hypothetical: it is what M1-87's mutation
// pass found in `tests/test_units_validated.cpp`, and it is VERIFICATION.md
// rule 23's fourth example.
[[nodiscard]] CameraError refusedError(const Position& position,
                                       const Quat& orientation,
                                       Radians verticalFov,
                                       Metres nearPlane) {
    const auto made = Camera::from(position, orientation, verticalFov, nearPlane);
    REQUIRE(!made.has_value());
    return made.error();
}

// A camera-to-world orientation that looks inward along `outward`, so that a
// point at `outward * radius` sits on the camera's axis. Built through
// `quaternionFrom`, which has a suite of its own, rather than through a second
// transcription of the identities (VERIFICATION.md rule 2).
[[nodiscard]] Quat lookInwardAlong(const Direction& outward) noexcept {
    const Direction right = normalize(cross(kUpHint, outward));
    const Direction up = cross(outward, right);
    // Columns are the camera's own axes in world coordinates. View space looks
    // down -z, so the camera's +z is the outward direction.
    const std::array<std::array<f64, 3>, 3> byColumn{
        {
            {{right.x.value(), right.y.value(), right.z.value()}},
            {{up.x.value(), up.y.value(), up.z.value()}},
            {{outward.x.value(), outward.y.value(), outward.z.value()}},
        },
    };
    RotationMatrix m{};
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            m.rows.at(row).at(column) = byColumn.at(column).at(row);
        }
    }
    return quaternionFrom(m);
}

// An unremarkable camera's three arguments, for the cases that are about the
// fourth. Functions rather than namespace objects, because `lookInwardAlong`
// is not `constexpr` and a namespace object needing a dynamic initialiser is
// what `-Wglobal-constructors` reports.
[[nodiscard]] Quat sane() noexcept { return lookInwardAlong(probeDirection()); }
[[nodiscard]] Position where() noexcept { return along(probeDirection(), Metres{6.771e6}); }
[[nodiscard]] Radians fov() noexcept { return degrees(45.0); }

[[nodiscard]] Projection projectionOrFail(Radians verticalFov) {
    const auto made = infiniteReverseZPerspective(verticalFov, kScreenAspect, kNearPlane);
    REQUIRE(made.has_value());
    return *made;
}

// How far the camera walks, and in what steps. Twenty metres of travel is
// enough for the claim and costs milliseconds.
constexpr std::size_t kWalkSteps = 200;
constexpr f64 kWalkStepMetres = 0.1;

// A point on the screen. Local to this file and going no further: register
// decision 105 ruled that a pixel unit and a screen frame wait for M1-80.
struct ScreenPoint {
    f64 x{};
    f64 y{};
};

[[nodiscard]] f64 focalLengthOf(Radians verticalFov) noexcept {
    return 1.0 / std::tan(verticalFov.value() / 2.0);
}

// The conditioning law the narrowing's screen error is measured against: the
// relative resolution of a 32-bit float, times the pixels per radian of this
// screen and field of view. **It contains no distance**, which is the property
// camera-relative rendering exists to produce.
[[nodiscard]] f64 narrowingLawPixels(Radians verticalFov) noexcept {
    return (kScreenHeight / 2.0) * kUnitRoundoff32 * focalLengthOf(verticalFov);
}

// A uniformly random rotation, through the quaternion `core/Math.hpp` already
// has: four normals normalised is the standard way to draw one without
// clustering at the poles. The shape, and the one suppression on the
// generator, are `tests/test_view_math.cpp`'s -- copied rather than reinvented,
// because a seed written down is VERIFICATION.md rule 12 and the check that
// objects to it is asking for the opposite.
class Sampler {
public:
    [[nodiscard]] Quat rotation() {
        return normalize(Quat{normal_(rng_), normal_(rng_), normal_(rng_), normal_(rng_)});
    }

    [[nodiscard]] Position anywhere(f64 magnitude) {
        return Position{
            uniform_(rng_) * magnitude, uniform_(rng_) * magnitude, uniform_(rng_) * magnitude};
    }

    [[nodiscard]] Direction direction() {
        return normalize(Direction{normal_(rng_), normal_(rng_), normal_(rng_)});
    }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSweepSeed};
    std::normal_distribution<f64> normal_{0.0, 1.0};
    std::uniform_real_distribution<f64> uniform_{-1.0, 1.0};
};

// A displacement into view space, and back out of it. Two names rather than
// one template, because the frames are the point: `inverseRigid` of a
// world-to-view transform is a view-to-world one, and the type says so.
//
// `transformDirection` rather than `transformPoint` throughout: these are
// displacements. The two agree here, the translation being zero, and the name
// says which was meant.
[[nodiscard]] Position viewOf(const WorldToView& view, const Position& worldOffset) noexcept {
    return transformDirection(view, WorldPosition{.v = worldOffset}).v;
}

[[nodiscard]] Position worldOf(const Transform<kView, kWorld>& inverse,
                               const Position& viewOffset) noexcept {
    return transformDirection(inverse, ViewPosition{.v = viewOffset}).v;
}

[[nodiscard]] ScreenPoint pixelsOf(const Projection& projection,
                                   const Position& viewRelative) noexcept {
    const ViewPoint p{.xyz = viewRelative, .w = Dimensionless{1.0}};
    const auto ndc = perspectiveDivide(transform(projection, p));
    return ScreenPoint{
        .x = ndc.v.x.value() * (kScreenWidth / 2.0),
        .y = ndc.v.y.value() * (kScreenHeight / 2.0),
    };
}

[[nodiscard]] Position widened(const Vec3f& v) noexcept {
    return Position{static_cast<f64>(v.x), static_cast<f64>(v.y), static_cast<f64>(v.z)};
}

// **The naive path: narrow first, subtract afterwards.** This and
// `pixelsIn32Bits` below are this suite's two exceptions to CLAUDE.md
// non-negotiable 8, granted by register decision 119 on the grounds decision
// 103 granted `tests/test_projection.cpp` its one: the claim *is* about what a
// 32-bit float can hold, and it cannot be made in 64-bit arithmetic.
//
// Its signature deliberately mirrors `toRenderSpace`'s -- a world point and a
// camera, not two positions -- so that it reads as the twin it is, and so that
// no two arguments of one type sit next to each other.
[[nodiscard]] Position narrowedBeforeSubtracting(const Position& worldMetres,
                                                 const Camera& camera) noexcept {
    const Position cameraPosition = camera.position();
    return Position{static_cast<f64>(static_cast<f32>(worldMetres.x.value()) -
                                     static_cast<f32>(cameraPosition.x.value())),
                    static_cast<f64>(static_cast<f32>(worldMetres.y.value()) -
                                     static_cast<f32>(cameraPosition.y.value())),
                    static_cast<f64>(static_cast<f32>(worldMetres.z.value()) -
                                     static_cast<f32>(cameraPosition.z.value()))};
}

// The whole chain in 32 bits, which is what the graphics card actually does:
// it does not narrow once at the end, it works in 32 bits throughout. Only the
// entries this projection has are read -- rows 0 and 1 have one each, and the
// bottom row turns -z into a distance -- and every one is read *from the
// matrix* rather than written in, so a wrong entry still shows here.
[[nodiscard]] ScreenPoint pixelsIn32Bits(const Projection& projection,
                                         const WorldToView& view,
                                         const Vec3f& renderSpace) noexcept {
    std::array<f32, 3> v{};
    for (std::size_t row = 0; row < 3; ++row) {
        v.at(row) = (static_cast<f32>(view.linear(Row{row}, Column{0}).value()) * renderSpace.x) +
                    (static_cast<f32>(view.linear(Row{row}, Column{1}).value()) * renderSpace.y) +
                    (static_cast<f32>(view.linear(Row{row}, Column{2}).value()) * renderSpace.z);
    }
    const auto scaleX = static_cast<f32>(projection.linear(Row{0}, Column{0}).value());
    const auto scaleY = static_cast<f32>(projection.linear(Row{1}, Column{1}).value());
    const f32 w = static_cast<f32>(projection.bottomRow(Column{2}).value()) * v.at(2);
    return ScreenPoint{
        .x = static_cast<f64>((scaleX * v.at(0)) / w) * (kScreenWidth / 2.0),
        .y = static_cast<f64>((scaleY * v.at(1)) / w) * (kScreenHeight / 2.0),
    };
}

// The claims that hold for every camera, lifted out of the sweeps below.
//
// **Catch2's REQUIRE is expensive in cognitive complexity** -- it expands to a
// loop and a catch -- so four of them inside a `for` puts a case over the
// threshold of 25. Lifting them into a flat function is the fix the check
// asks for, and no assertion was dropped doing it.
void requireTranslationIsExactlyZero(const WorldToView& view) {
    // Exactly zero, not nearly: the translation is set, never computed, so bit
    // identity is the right claim -- the argument `view/Mat4.hpp` makes for
    // `isAffine`, which is the other half of it.
    REQUIRE(view.translation(Row{0}).bitIdentical(Metres{0.0}));
    REQUIRE(view.translation(Row{1}).bitIdentical(Metres{0.0}));
    REQUIRE(view.translation(Row{2}).bitIdentical(Metres{0.0}));
    REQUIRE(isAffine(view));
}

// One of the camera's own axes, turned into world coordinates and then back
// into view space. The orientation runs camera-to-world, so each must come
// back as the axis it started as.
void requireAxisComesBack(const WorldToView& view, const Quat& attitude, std::size_t axis) {
    const std::array<Position, 3> axes{
        {
            Position{1.0, 0.0, 0.0},
            Position{0.0, 1.0, 0.0},
            Position{0.0, 0.0, 1.0},
        },
    };
    const Position expected = axes.at(axis);
    const Position inView = viewOf(view, attitude.rotate(expected));
    const Tolerance tolerance{kViewMatrixUlps * kEps};
    CAPTURE(axis, inView.x.value(), inView.y.value(), inView.z.value());
    REQUIRE(nearlyEqual(inView.x.value(), expected.x.value(), tolerance));
    REQUIRE(nearlyEqual(inView.y.value(), expected.y.value(), tolerance));
    REQUIRE(nearlyEqual(inView.z.value(), expected.z.value(), tolerance));
}

[[nodiscard]] f64 orthonormalityResidual(const WorldToView& m) noexcept {
    f64 worst = 0.0;
    for (std::size_t a = 0; a < 3; ++a) {
        for (std::size_t b = 0; b < 3; ++b) {
            f64 sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += m.linear(Row{a}, Column{k}).value() * m.linear(Row{b}, Column{k}).value();
            }
            worst = std::max(worst, std::abs(sum - (a == b ? 1.0 : 0.0)));
        }
    }
    return worst;
}

[[nodiscard]] f64 determinantOf(const WorldToView& m) noexcept {
    const auto e = [&m](std::size_t row, std::size_t column) {
        return m.linear(Row{row}, Column{column}).value();
    };
    return (e(0, 0) * ((e(1, 1) * e(2, 2)) - (e(1, 2) * e(2, 1)))) -
           (e(0, 1) * ((e(1, 0) * e(2, 2)) - (e(1, 2) * e(2, 0)))) +
           (e(0, 2) * ((e(1, 0) * e(2, 1)) - (e(1, 1) * e(2, 0))));
}

// One geometry the jitter claim is made at. A struct rather than three
// parameters, because `worldMagnitude` and `range` are both lengths and would
// transpose in silence (non-negotiable 1).
struct Geometry {
    Metres worldMagnitude;
    Metres range;
    Radians verticalFov;
};

struct PathErrors {
    f64 render{};   // narrowed once, the rest in 64 bits: what this task owns
    f64 render32{}; // the whole chain in 32 bits: what the graphics card does
    f64 naive{};    // narrowed before the subtraction: the defect
};

// Walk the camera past a fixed world point and record the worst screen-space
// error of each path against the same chain computed entirely in 64 bits.
[[nodiscard]] PathErrors walk(const Geometry& geometry) {
    const Direction outward = probeDirection();
    const Position point = along(outward, geometry.worldMagnitude);
    const Direction tangent = normalize(cross(outward, kUpHint));
    const Quat attitude = lookInwardAlong(outward);
    const Projection projection = projectionOrFail(geometry.verticalFov);
    const Position start = along(outward, geometry.worldMagnitude + geometry.range);

    PathErrors worst{};
    for (std::size_t step = 0; step < kWalkSteps; ++step) {
        const Metres travelled{static_cast<f64>(step) * kWalkStepMetres};
        const Camera camera = cameraOrFail(
            start + along(tangent, travelled), attitude, geometry.verticalFov, kNearPlane);
        const WorldToView view = viewMatrix(camera);
        const ScreenPoint exact = pixelsOf(projection, viewOf(view, point - camera.position()));
        // One parameter rather than two, so that `measured` and `exact` cannot
        // be transposed -- the form non-negotiable 1 asks for, reached by
        // closing over the reference instead of passing it.
        const auto errorFrom = [&exact](const ScreenPoint& measured) {
            return std::hypot(measured.x - exact.x, measured.y - exact.y);
        };

        const Vec3f relative = toRenderSpace(point, camera);
        worst.render = std::max(worst.render,
                                errorFrom(pixelsOf(projection, viewOf(view, widened(relative)))));
        worst.naive =
            std::max(worst.naive,
                     errorFrom(pixelsOf(projection,
                                        viewOf(view, narrowedBeforeSubtracting(point, camera)))));
        worst.render32 =
            std::max(worst.render32, errorFrom(pixelsIn32Bits(projection, view, relative)));
    }
    return worst;
}

} // namespace

// ---------------------------------------------------------------- refusals --

// **Four cases rather than four sections of one.** Catch2 expands every
// REQUIRE into a loop and a catch, so a handful of them inside a `for` inside
// a `SECTION` reaches a cognitive complexity of 60 against a threshold of 25.
// The split is what the check is for; **no assertion was dropped to make it**,
// which is the failure M1-87's mutation pass found when a similar split
// quietly removed a `has_value()` guard (VERIFICATION.md rule 23).

TEST_CASE("a camera with a position that is not finite is refused") {
    for (const f64 bad : {kInfinity, -kInfinity, kNotANumber}) {
        CAPTURE(bad);
        REQUIRE(refusedError(Position{bad, 0.0, 0.0}, sane(), fov(), kNearPlane) ==
                CameraError::NotFinitePosition);
        REQUIRE(refusedError(Position{0.0, bad, 0.0}, sane(), fov(), kNearPlane) ==
                CameraError::NotFinitePosition);
        REQUIRE(refusedError(Position{0.0, 0.0, bad}, sane(), fov(), kNearPlane) ==
                CameraError::NotFinitePosition);
    }
}

TEST_CASE("a camera whose orientation is not a rotation is refused") {
    const Quat unit = sane();
    const std::array<Quat, 4> notRotations{
        {
            Quat{0.0, 0.0, 0.0, 0.0},                                     // the zero quaternion
            Quat{2.0 * unit.w, 2.0 * unit.x, 2.0 * unit.y, 2.0 * unit.z}, // scaled
            Quat{kNotANumber, 0.0, 0.0, 0.0},                             // not a number
            Quat{1.0, 0.0, 0.0, 1.0},                                     // norm sqrt(2)
        },
    };
    for (const Quat& bad : notRotations) {
        REQUIRE(refusedError(where(), bad, fov(), kNearPlane) == CameraError::NotUnitOrientation);
    }
}

TEST_CASE("a camera with a field of view outside (0, pi) is refused") {
    for (const f64 bad : {0.0, -1.0, kPi, kPi + 0.1, kInfinity, kNotANumber}) {
        CAPTURE(bad);
        REQUIRE(refusedError(where(), sane(), Radians{bad}, kNearPlane) ==
                CameraError::InvalidFieldOfView);
    }
}

TEST_CASE("a camera whose near plane is not finite and positive is refused") {
    for (const f64 bad : {0.0, -1.0, kInfinity, kNotANumber}) {
        CAPTURE(bad);
        REQUIRE(refusedError(where(), sane(), fov(), Metres{bad}) == CameraError::InvalidNearPlane);
    }
}

TEST_CASE("the orientation tolerance is a boundary, and it is checked from both sides") {
    // **The case that gives the refusals above their teeth.** Without it, a
    // factory that refused *every* quaternion would pass every assertion in
    // the orientation section above (VERIFICATION.md rule 23).
    const Quat unit = lookInwardAlong(probeDirection());
    const Position where = along(probeDirection(), Metres{6.771e6});
    const Radians fov = degrees(45.0);

    // Inside the tolerance: accepted, at three quarters of it either way. The
    // probes are placed relative to the constant rather than at a written
    // number, so that changing the constant moves them with it.
    REQUIRE(Camera::from(where, atUlpsOffUnit(unit, 0.75 * kAdmittedUlps), fov, kNearPlane)
                .has_value());
    REQUIRE(Camera::from(where, atUlpsOffUnit(unit, -0.75 * kAdmittedUlps), fov, kNearPlane)
                .has_value());

    // Outside it: refused, at twice it either way -- and still a quaternion
    // every other tolerance in this project would happily call unit.
    REQUIRE(refusedError(where, atUlpsOffUnit(unit, 2.0 * kAdmittedUlps), fov, kNearPlane) ==
            CameraError::NotUnitOrientation);
    REQUIRE(refusedError(where, atUlpsOffUnit(unit, -2.0 * kAdmittedUlps), fov, kNearPlane) ==
            CameraError::NotUnitOrientation);

    // And what every producer in this project makes is accepted, which is the
    // half that says the tolerance is usable rather than merely strict.
    REQUIRE(
        Camera::from(where, Quat::fromAxisAngle(probeDirection(), Radians{0.77}), fov, kNearPlane)
            .has_value());
    REQUIRE(Camera::from(where, normalize(Quat{1.0, 2.0, 3.0, 4.0}), fov, kNearPlane).has_value());
    REQUIRE(Camera::from(where, Quat{}, fov, kNearPlane).has_value());
}

// ------------------------------------------------------------ the view matrix

TEST_CASE("the view matrix is a pure rotation") {
    Sampler sampler;

    f64 worstOrtho = 0.0;
    f64 worstDeterminant = 0.0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        // Half the sweep sits as close to the tolerance as a probe can be
        // placed, in both directions, so the budget is measured against the
        // worst orientation the factory will admit rather than the best.
        const f64 offset = (i % 2 == 0 ? 1.0 : -1.0) * 0.75 * kAdmittedUlps;
        const Camera camera = cameraOrFail(sampler.anywhere(1.0e7),
                                           atUlpsOffUnit(sampler.rotation(), offset),
                                           degrees(45.0),
                                           kNearPlane);
        const WorldToView view = viewMatrix(camera);
        requireTranslationIsExactlyZero(view);
        worstOrtho = std::max(worstOrtho, orthonormalityResidual(view));
        worstDeterminant = std::max(worstDeterminant, std::abs(determinantOf(view) - 1.0));
    }
    CAPTURE(worstOrtho / kEps, worstDeterminant / kEps);
    REQUIRE(worstOrtho <= kViewMatrixUlps * kEps);
    // The determinant is asked for its value and not just its sign: an
    // orthonormal matrix has determinant +1 or -1, and -1 is a reflection.
    REQUIRE(worstDeterminant <= kViewMatrixUlps * kEps);
}

TEST_CASE("the view matrix takes the camera's own axes to view space's axes") {
    // **The only case here that can tell a view matrix from its inverse.**
    // A rotation and its inverse are both rotations, both have determinant +1,
    // and each round-trips perfectly against the other, so every other claim
    // in this file passes if `viewMatrix` forgets to conjugate.
    const Direction outward = probeDirection();
    const Quat attitude = lookInwardAlong(outward);
    const Camera camera =
        cameraOrFail(along(outward, Metres{6.771e6}), attitude, degrees(45.0), kNearPlane);
    const WorldToView view = viewMatrix(camera);

    for (std::size_t axis = 0; axis < 3; ++axis) {
        requireAxisComesBack(view, attitude, axis);
    }

    // And the point the camera looks at is straight ahead: down -z, at the
    // range, which is what makes "looks down -z" a tested claim rather than a
    // comment in `view/Projection.hpp`.
    const Position target = along(outward, Metres{6.371e6});
    const Position ahead = viewOf(view, target - camera.position());
    CAPTURE(ahead.x.value(), ahead.y.value(), ahead.z.value());
    REQUIRE(ahead.z.value() < 0.0);
    REQUIRE(nearlyEqual(ahead.z.value(), -4.0e5, Tolerance{1.0e-6}));
}

TEST_CASE("a displacement round-trips through view space") {
    Sampler sampler;

    f64 worst = 0.0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const Camera camera =
            cameraOrFail(Position{}, sampler.rotation(), degrees(45.0), kNearPlane);
        const WorldToView view = viewMatrix(camera);

        // A displacement in **metres**, not a dimensionless `Direction`: since
        // ADR 0020 the view matrix maps metres to metres, so `Vec3<one>` does
        // not compile here at all. The task document asked for a direction;
        // the type system answered.
        const Direction u = sampler.direction();
        const Position there{u.x.value(), u.y.value(), u.z.value()};
        const Position back = worldOf(inverseRigid(view), viewOf(view, there));
        worst = std::max({
            worst,
            std::abs(back.x.value() - there.x.value()),
            std::abs(back.y.value() - there.y.value()),
            std::abs(back.z.value() - there.z.value()),
        });
    }
    CAPTURE(worst / kEps);
    REQUIRE(worst <= kViewMatrixUlps * kEps);
}

// ----------------------------------------------------------- the narrowing --

TEST_CASE("the narrowing is exact, and it is the subtraction that makes it so") {
    // A camera position 32 bits cannot hold: 6771000.3 sits between 6771000.0
    // and 6771000.5, the two neighbouring 32-bit floats there, and rounds to
    // the upper one. So does a point 0.1 m beyond it.
    const Camera camera =
        cameraOrFail(Position{6771000.3, 0.0, 0.0}, Quat{}, degrees(45.0), kNearPlane);
    const Position target{6771000.4, 0.0, 0.0};

    // Subtracting first: 0.1 m, narrowed once, which is the nearest 32-bit
    // float to a tenth of a metre.
    const Vec3f relative = toRenderSpace(target, camera);
    REQUIRE(bitIdentical(relative, Vec3f{.x = 0.1F, .y = 0.0F, .z = 0.0F}));

    // Narrowing first: both operands land on 6771000.5 and the difference is
    // **exactly zero**. The camera has moved 10 cm and the renderer cannot
    // tell. That is the crawl this file exists to prevent, in one line.
    const Position naive = narrowedBeforeSubtracting(target, camera);
    REQUIRE(nearlyEqual(naive.x.value(), 0.0, Tolerance{0.0}));

    // A difference every width holds exactly comes back untouched.
    const Camera exactCamera =
        cameraOrFail(Position{6771000.0, 0.0, 0.0}, Quat{}, degrees(45.0), kNearPlane);
    const Vec3f exact = toRenderSpace(Position{6771001.5, 2.25, -3.0}, exactCamera);
    REQUIRE(bitIdentical(exact, Vec3f{.x = 1.5F, .y = 2.25F, .z = -3.0F}));

    // **And bit identity looks at all three components.** Added because the
    // mutation pass found it missing: a `bitIdentical` that compared only `x`
    // survived every assertion above, since each of them differs from its
    // expected value in `x` or not at all. Two mutants now die here
    // (VERIFICATION.md rule 19, and rule 23 -- a check nothing can fail is not
    // a check).
    REQUIRE(!bitIdentical(exact, Vec3f{.x = 1.5F, .y = 2.26F, .z = -3.0F}));
    REQUIRE(!bitIdentical(exact, Vec3f{.x = 1.5F, .y = 2.25F, .z = -3.01F}));
}

TEST_CASE("the jitter budget holds, and does not depend on the distance to the origin") {
    const std::array<Geometry, 9> geometries{
        {
            {
                .worldMagnitude = Metres{6.371e6},
                .range = Metres{4.0e5},
                .verticalFov = degrees(45.0),
            },
            {
                .worldMagnitude = Metres{6.371e6},
                .range = Metres{4.0e5},
                .verticalFov = degrees(10.0),
            },
            {
                .worldMagnitude = Metres{6.371e6},
                .range = Metres{4.0e5},
                .verticalFov = degrees(120.0),
            },
            {
                .worldMagnitude = Metres{6.371e6},
                .range = Metres{1.0e3},
                .verticalFov = degrees(45.0),
            },
            {
                .worldMagnitude = Metres{6.371e6},
                .range = Metres{1.0e2},
                .verticalFov = degrees(60.0),
            },
            {
                .worldMagnitude = Metres{3.844e8},
                .range = Metres{4.0e5},
                .verticalFov = degrees(45.0),
            },
            {
                .worldMagnitude = Metres{3.844e8},
                .range = Metres{1.0e3},
                .verticalFov = degrees(10.0),
            },
            {
                .worldMagnitude = Metres{1.495978707e11},
                .range = Metres{4.0e5},
                .verticalFov = degrees(45.0),
            },
            {
                .worldMagnitude = Metres{4.5e12},
                .range = Metres{4.0e5},
                .verticalFov = degrees(45.0),
            },
        },
    };

    for (const Geometry& geometry : geometries) {
        const PathErrors errors = walk(geometry);
        const f64 law = narrowingLawPixels(geometry.verticalFov);
        CAPTURE(geometry.worldMagnitude.value(),
                geometry.range.value(),
                geometry.verticalFov.value(),
                errors.render,
                errors.render32,
                law);

        // The law, which is the regression guard: no distance appears in it.
        REQUIRE(errors.render <= kNarrowingBound * law);
        // And the same bound covers the chain computed entirely in 32 bits,
        // which is what the graphics card will do with these numbers.
        REQUIRE(errors.render32 <= kNarrowingBound * law);
        // Phase A's stated acceptance criterion, which the above beats by
        // about 320 times at a 45-degree field of view.
        REQUIRE(errors.render <= kAcceptancePixels);
        REQUIRE(errors.render32 <= kAcceptancePixels);
    }
}

TEST_CASE("the narrowing's error is flat across six decades of world magnitude") {
    // **The claim camera-relative rendering is actually about.** The three
    // geometries differ only in how far the origin is; if the error tracked
    // that, this is where it would show.
    const Radians fov = degrees(45.0);
    const f64 atEarthRadius =
        walk({.worldMagnitude = Metres{6.371e6}, .range = Metres{4.0e5}, .verticalFov = fov})
            .render;
    const f64 atLunarDistance =
        walk({.worldMagnitude = Metres{3.844e8}, .range = Metres{4.0e5}, .verticalFov = fov})
            .render;
    const f64 atOneAu =
        walk({.worldMagnitude = Metres{1.495978707e11}, .range = Metres{4.0e5}, .verticalFov = fov})
            .render;

    CAPTURE(atEarthRadius, atLunarDistance, atOneAu);
    REQUIRE(atEarthRadius > 0.0);
    for (const f64 other : {atLunarDistance, atOneAu}) {
        REQUIRE(std::abs(other - atEarthRadius) <= kFlatnessFraction * atEarthRadius);
    }
}

TEST_CASE("the naive path fails the budget where it bites, and passes where it does not") {
    // **The half that gives the budget teeth, and the half that records a
    // measurement the plan got wrong.** Register decision 115.
    const Radians fov = degrees(45.0);

    SECTION("at 1 AU it is 95 times over") {
        const PathErrors errors = walk(
            {.worldMagnitude = Metres{1.495978707e11}, .range = Metres{4.0e5}, .verticalFov = fov});
        CAPTURE(errors.naive, errors.render);
        REQUIRE(errors.naive > 20.0 * kAcceptancePixels);
    }

    SECTION("at short range it is over too") {
        const PathErrors atOneKilometre =
            walk({.worldMagnitude = Metres{6.371e6}, .range = Metres{1.0e3}, .verticalFov = fov});
        CAPTURE(atOneKilometre.naive);
        REQUIRE(atOneKilometre.naive > 2.0 * kAcceptancePixels);

        const PathErrors atOneHundredMetres =
            walk({.worldMagnitude = Metres{6.371e6}, .range = Metres{1.0e2}, .verticalFov = fov});
        CAPTURE(atOneHundredMetres.naive);
        REQUIRE(atOneHundredMetres.naive > 20.0 * kAcceptancePixels);
    }

    SECTION("but at 400 km from Earth radius it is inside the budget, which the plan did not "
            "expect") {
        const PathErrors errors =
            walk({.worldMagnitude = Metres{6.371e6}, .range = Metres{4.0e5}, .verticalFov = fov});
        CAPTURE(errors.naive, errors.render);
        // Measured 5.8e-4 px, 86 times inside the budget. Asserted so that the
        // task document's original claim cannot come back unnoticed.
        REQUIRE(errors.naive < kAcceptancePixels);
        // It is still an order of magnitude worse than doing it properly --
        // invisible, not harmless -- and that is what says the two paths are
        // genuinely different here rather than the test comparing a thing with
        // itself (VERIFICATION.md rule 23).
        REQUIRE(errors.naive > 5.0 * errors.render);
    }
}

// --------------------------------------------- the projection, temporarily --

TEST_CASE("the projection a camera implies") {
    // **`projectionOf` is temporary and M1-13 deletes it** (register decision
    // 117), and so does this case.
    const Camera camera = cameraOrFail(Position{}, Quat{}, degrees(45.0), kNearPlane);

    const auto made = projectionOf(camera, kScreenAspect);
    REQUIRE(made.has_value());
    // Bit-identical to asking `view/Projection.hpp` directly, because that is
    // all it does. Anything else would be a second projection to keep in step.
    REQUIRE(made->bitIdentical(projectionOrFail(degrees(45.0))));

    // The only error it can report, because `Camera` has already refused
    // everything that could produce the other two.
    const auto refused = projectionOf(camera, Aspect{0.0});
    REQUIRE(!refused.has_value());
    REQUIRE(refused.error() == ProjectionError::InvalidAspect);
}
