//
// Tests for view/Projection.hpp: the reversed perspective projection with no
// far plane (M1-10; ADR 0003 for the decision it makes real).
//
// **Nothing here includes a Vulkan or SDL header**, and that is the link
// graph's doing rather than anyone's discipline: this suite links orbsim_view,
// which links orbsim_core and nothing else (ADR 0012).
//
// The layout of the matrix -- the y flip, the constant depth row, the near
// plane's slot, the absent far plane -- is asserted at compile time beside the
// function that builds it, because that half needs no tangent and a
// static_assert cannot rot. What is left for this file is the arithmetic.
//
// **Where the numbers come from.** Every tolerance below was measured before
// it was written down, and four of the task document's figures did not survive
// that (register decisions 99-104):
//
//   * **"ten times the near plane gives 0.1" is not exact.** Measured over
//     200,000 near planes from 1 mm to 10 km, the worst deviation is 1 ulp of
//     the result -- at 7.3 m, for instance, the quotient is one ulp below the
//     nearest double to 0.1. The budget is 2 ulp, twice the measurement;
//   * **"a point at 1e13 m gives a depth under 1e-12" holds only below a
//     10 m near plane.** The figure is 10 / 1e13: the sentence silently
//     assumed a near plane. What is asserted here is the underlying rule --
//     depth is the near plane over the distance, exactly -- with the readable
//     instance kept beside it at a stated near plane;
//   * **the near-plane case cannot be run at a near plane of 1 m.** Dividing
//     and multiplying agree there, so the case would accept a projection whose
//     perspective divide multiplies -- which is precisely the bug M1-09's
//     mutation pass left surviving and handed to this task by name. Measured:
//     at 1 m the case accepts every wrong divide constructible; at any other
//     near plane it accepts three of five. So it also checks the two numbers
//     *behind* the quotient, which is what makes it catch all five;
//   * **the frustum edge is not exact either.** Worst 2.0 ulp in x and 1.0 in
//     y over 200,000 random (field of view, aspect ratio) pairs, so 4 ulp.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Mat4.hpp"
#include "view/Projection.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>

using namespace orb;
using namespace orb::view;

namespace {

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260921ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 20'000;

constexpr f64 kEpsilon = std::numeric_limits<f64>::epsilon();

// Measured worst 1 ulp of the result over 200,000 near planes; twice that is
// this project's rule for a budget (register decisions 54 and 85).
constexpr f64 kQuotientUlps = 2.0;

// Measured worst 2.0 ulp in x and 1.0 in y over 200,000 (fov, aspect) pairs.
constexpr f64 kEdgeUlps = 4.0;

// Two points one metre apart at 1000 km separate by 9 to 14 ulp of 32-bit
// depth across near planes from 1 cm to 100 m. Four is about half the worst,
// which is the right direction for a floor: it still fails if the mechanism
// degrades, and leaves room for another toolchain's rounding.
constexpr std::int64_t kMinimumDepthSeparationUlps = 4;

// The near planes the claims are checked at. **None of them is 1 m**, and the
// header comment above says why. They span four orders of magnitude, because a
// projection that only works at the scale somebody tried is the defect this
// project already shipped once in the propagator.
constexpr std::array<f64, 5> kNearPlanes{{0.01, 0.05, 0.25, 2.0, 1000.0}};

// A field of view and an aspect ratio that are unremarkable, for the cases
// that are not about either.
constexpr Radians kOrdinaryFov{1.0};
constexpr Aspect kOrdinaryAspect{16.0 / 9.0};

constexpr f64 kOrdinaryNearPlane = 0.05;
const Metres kOrdinaryNear{kOrdinaryNearPlane};

const f64 kNotANumber = std::numeric_limits<f64>::quiet_NaN();

// The magnitudes no positive, finite quantity may take. Shared by the near
// plane and the aspect ratio, which have the same admissibility rule.
const std::array<f64, 4> kUnusableMagnitudes{
    {0.0, -1.0, std::numeric_limits<f64>::infinity(), kNotANumber},
};

[[nodiscard]] Projection projectionOrFail(Radians fov, Aspect aspect, Metres nearPlane) {
    const auto made = infiniteReverseZPerspective(fov, aspect, nearPlane);
    REQUIRE(made.has_value());
    return *made;
}

// A point on the camera's axis, at a given distance in front of it. View space
// looks down -z, so "in front" is negative.
[[nodiscard]] ViewPoint onAxisAt(f64 distance) {
    return ViewPoint{.xyz = Position{0.0, 0.0, -distance}, .w = Dimensionless{1.0}};
}

// The error a refused set of arguments reports. Separate from the cases below
// so that each of them stays one claim rather than a wall of assertions.
[[nodiscard]] ProjectionError refusedError(Radians fov, Aspect aspect, Metres nearPlane) {
    const auto made = infiniteReverseZPerspective(fov, aspect, nearPlane);
    // `REQUIRE(!x)` rather than `REQUIRE_FALSE(x)`, here and below, and the
    // reason is not style: REQUIRE_FALSE combines two of Catch2's result
    // flags into a value its own enumeration does not name, and
    // clang-analyzer-optin.core.EnumCastOutOfRange reports that as an error
    // under this project's WarningsAsErrors: '*'. The two spellings assert
    // exactly the same thing, so the spelling is what gives way rather than
    // the check (CLAUDE.md rule 7 -- a suppression is the owner's decision).
    REQUIRE(!made.has_value());
    return made.error();
}

// The depth a point lands on: through transform() and the perspective divide,
// which is the only route a projection may be applied by (decision 93).
[[nodiscard]] f64 depthOf(const Projection& projection, const ViewPoint& point) {
    return perspectiveDivide(transform(projection, point)).v.z.value();
}

// 32-bit float ulp distance, through the bit pattern: for positive floats the
// bit patterns increase with the value, so their difference counts the
// representable numbers between the two.
//
// The two arguments are interchangeable -- a distance is symmetric and the
// result is an absolute one -- so transposing them cannot produce a wrong
// answer. That is the reason core/Scalar.hpp gives on nearlyEqual and
// view/Mat4.hpp gives on bitIdentical, and the form register decision 96 set
// for a provably symmetric pair: the suppression at the site, with its reason.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::int64_t ulpsApart(float left, float right) {
    const auto l = static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(left));
    const auto r = static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(right));
    return l > r ? l - r : r - l;
}

// How far in front of the camera the frustum-edge cases put their points. Far
// enough that the near plane is not involved, near enough that nothing is
// near the limits of a double.
constexpr f64 kEdgeDistance = 1000.0;

// A field of view whose half-angle tangent is known **without calling the
// tangent function**, so that an error in the field-of-view conversion cannot
// cancel itself out against the test's own arithmetic (VERIFICATION.md rule 2).
struct EdgeAnchor {
    Radians fov;
    f64 halfAngleTangent{};
};

// The two planes of the control projection below, named as a pair. **Not two
// adjacent `Metres` parameters**: those transpose in silence, and a near and a
// far plane the wrong way round would quietly invert the control and make the
// comparison it exists for meaningless.
struct DepthRange {
    Metres nearPlane;
    Metres farPlane;
};

// **The control, and it lives here deliberately.** A conventional projection
// -- near at 0, far at 1 -- exists only to lose the comparison below, so it
// must never be reachable from anything that draws. Depth is
// `A - B/distance` with `A = far/(far - near)` and `B = near*far/(far - near)`,
// which puts A in the depth row's z slot negated and B in its constant slot
// negated, since the divide is by the distance rather than by -z.
[[nodiscard]] Projection
conventionalPerspective(Dimensionless focalLength, Aspect aspect, const DepthRange& range) {
    const f64 n = range.nearPlane.value();
    const f64 f = range.farPlane.value();
    const f64 a = f / (f - n);
    const f64 b = (n * f) / (f - n);
    Projection m{};
    m.set(Row{0}, Column{0}, focalLength.value() / aspect.value());
    m.set(Row{1}, Column{1}, -focalLength.value());
    m.set(Row{2}, Column{2}, -a);
    m.set(Row{2}, Column{3}, -b);
    m.set(Row{3}, Column{2}, -1.0);
    return m;
}

} // namespace

TEST_CASE("the near plane maps to exactly one, and to the right two numbers") {
    // **Three assertions, not one, and it is worth being exact about what the
    // extra two buy -- because the first answer written here was wrong.**
    //
    // At the near plane the depth numerator and denominator are the same
    // number, so the quotient is 1 for a whole family of wrong divides:
    // taking the operands the wrong way round, returning a hard-coded 1, and
    // squaring the ratio all give 1 when handed two equal numbers. **No
    // near-plane check can ever see those**, whatever it asserts, because
    // "the near plane maps to 1" *is* the statement that the two numbers are
    // equal. The mutation pass confirms it: those three die in the
    // ten-times-near and monotonic cases, never here.
    //
    // What the extra two assertions do catch is a wrong **matrix**, at run
    // time and through the real entry point -- the near plane in the wrong
    // slot, a bottom row that is not -z, a depth row that is not constant.
    // The static_asserts beside the builder check the same layout, but on a
    // hand-made instance; these check what infiniteReverseZPerspective
    // actually returns after computing a focal length from an angle.
    //
    // The near plane must still not be 1 m. There, `n*n` and `n/n` are both
    // 1, so even the multiplying divide -- M1-09's survivor, handed to this
    // task by name -- would pass.
    for (const f64 nearPlane : kNearPlanes) {
        const Projection projection =
            projectionOrFail(kOrdinaryFov, kOrdinaryAspect, Metres{nearPlane});
        const ClipPoint clip = transform(projection, onAxisAt(nearPlane));
        CAPTURE(nearPlane);

        // The numerator is the near plane, whatever the point was.
        REQUIRE(nearlyEqual(clip.xyz.z.value(), nearPlane, Tolerance{0.0}));
        // The denominator is the distance to the point.
        REQUIRE(nearlyEqual(clip.w.value(), nearPlane, Tolerance{0.0}));
        // And only then: the quotient is exactly one. ADR 0003's convention,
        // and the reason a pipeline comparing with LESS draws nothing.
        REQUIRE(nearlyEqual(perspectiveDivide(clip).v.z.value(), 1.0, Tolerance{0.0}));
    }
}

TEST_CASE("depth is the near plane over the distance, exactly") {
    // The rule the projection actually implements, rather than one of its
    // consequences. It is exact because the depth row is constant: the
    // numerator is the near plane with nothing added to it, so the only
    // arithmetic left is a single correctly rounded division.
    constexpr std::array<f64, 7> kDistances{{0.1, 1.0, 1.0e3, 1.0e6, 1.0e9, 1.0e13, 1.0e20}};
    for (const f64 nearPlane : kNearPlanes) {
        const Projection projection =
            projectionOrFail(kOrdinaryFov, kOrdinaryAspect, Metres{nearPlane});
        for (const f64 distance : kDistances) {
            CAPTURE(nearPlane, distance);
            REQUIRE(bitsOf(depthOf(projection, onAxisAt(distance))) ==
                    bitsOf(nearPlane / distance));
        }
    }

    // The same rule read as the task document reads it, at a near plane where
    // the figure it quotes is true. 1e-12 is 10 / 1e13, so the document's
    // sentence was assuming a near plane of at most 10 m without saying so.
    const Projection projection = projectionOrFail(kOrdinaryFov, kOrdinaryAspect, Metres{1.0});
    REQUIRE(depthOf(projection, onAxisAt(1.0e13)) < 1.0e-12);

    // Ten times the near plane gives a tenth. Not exact at every near plane --
    // measured 1 ulp worst over 200,000 of them -- so 2 ulp, not zero.
    for (const f64 nearPlane : kNearPlanes) {
        const Projection tenth = projectionOrFail(kOrdinaryFov, kOrdinaryAspect, Metres{nearPlane});
        const f64 depth = depthOf(tenth, onAxisAt(10.0 * nearPlane));
        CAPTURE(nearPlane, depth);
        REQUIRE(nearlyEqual(depth, 0.1, Tolerance{kQuotientUlps * 0.1 * kEpsilon}));
    }
}

TEST_CASE("depth decreases strictly with distance") {
    // A fixed geometric grid rather than a random draw: it covers the whole
    // interval instead of sampling it, reproduces identically on every
    // machine, and needs no seed -- the argument tests/test_sun.cpp makes for
    // its daily grids. A reversed comparison or a sign slip fails here.
    const Projection projection = projectionOrFail(kOrdinaryFov, kOrdinaryAspect, Metres{1.0});
    f64 previous = std::numeric_limits<f64>::infinity();
    std::size_t steps = 0;
    for (int decade = -1; decade <= 13; ++decade) {
        for (int k = 0; k < 100; ++k) {
            const f64 distance = std::pow(10.0, static_cast<f64>(decade) + (k / 100.0));
            const f64 depth = depthOf(projection, onAxisAt(distance));
            CAPTURE(distance, depth, previous);
            REQUIRE(depth < previous);
            previous = depth;
            ++steps;
        }
    }
    REQUIRE(steps == 1500);
    // It really did reach zero from one: near maps to 1 and the far end is
    // under 1e-13, so the sweep spans the whole of the depth range.
    REQUIRE(previous < 1.0e-13);
}

TEST_CASE("two points a metre apart at 1000 km are distinct in a 32-bit depth buffer") {
    // **The test ADR 0003 exists for**, and the one place this project narrows
    // to 32 bits outside the GPU boundary function (CLAUDE.md non-negotiable 8,
    // ruled on 2026-09-21): the claim *is* about what a 32-bit depth buffer can
    // hold, so it cannot be made in 64-bit arithmetic.
    constexpr f64 kRange = 1.0e6;
    constexpr Metres kConventionalFar{1.0e9};

    for (const f64 nearPlane : kNearPlanes) {
        const Projection projection =
            projectionOrFail(kOrdinaryFov, kOrdinaryAspect, Metres{nearPlane});
        const auto near32 = static_cast<float>(depthOf(projection, onAxisAt(kRange)));
        const auto far32 = static_cast<float>(depthOf(projection, onAxisAt(kRange + 1.0)));
        CAPTURE(nearPlane, ulpsApart(near32, far32));
        REQUIRE(ulpsApart(near32, far32) >= kMinimumDepthSeparationUlps);

        // The same sum the graphics card itself performs: it works in 32 bits
        // throughout rather than narrowing once at the end. Measured identical,
        // so this cannot fail on its own -- it is here because it is what the
        // hardware does, and a formulation that were only good in 64 bits
        // would be no use.
        const float hardware1 = static_cast<float>(nearPlane) / static_cast<float>(kRange);
        const float hardware2 = static_cast<float>(nearPlane) / static_cast<float>(kRange + 1.0);
        CAPTURE(ulpsApart(hardware1, hardware2));
        REQUIRE(ulpsApart(hardware1, hardware2) >= kMinimumDepthSeparationUlps);
    }

    // And the comparison that gives the test teeth. Under a conventional
    // projection the two depths are not merely closer together -- they are the
    // same 32-bit number, so nothing downstream could tell the two points
    // apart at all. If this ever stops being bit-identical, the control has
    // changed and the claim above needs re-deriving rather than trusting.
    const Projection control =
        conventionalPerspective(Dimensionless{1.0 / std::tan(kOrdinaryFov.value() / 2.0)},
                                kOrdinaryAspect,
                                DepthRange{.nearPlane = Metres{1.0}, .farPlane = kConventionalFar});
    const auto controlNear = static_cast<float>(depthOf(control, onAxisAt(kRange)));
    const auto controlFar = static_cast<float>(depthOf(control, onAxisAt(kRange + 1.0)));
    CAPTURE(ulpsApart(controlNear, controlFar));
    REQUIRE(ulpsApart(controlNear, controlFar) == 0);

    // Not vacuous: the control is a working projection, and does separate the
    // two points when they are close enough to the camera for it to.
    const auto controlClose1 = static_cast<float>(depthOf(control, onAxisAt(1.0e3)));
    const auto controlClose2 = static_cast<float>(depthOf(control, onAxisAt(1.0e3 + 1.0)));
    REQUIRE(ulpsApart(controlClose1, controlClose2) > 0);
}

TEST_CASE("the frustum edge lands on the screen edge, without asking the tangent function") {
    // Two anchors whose edge point is computed **without std::tan**, so that
    // an error in the field-of-view conversion cannot cancel itself out
    // against the test's own arithmetic (VERIFICATION.md rule 2). At 90
    // degrees the half-angle tangent is 1 and at 60 degrees it is 1/sqrt(3),
    // and std::numbers supplies the second. Measured: std::tan is itself 1 ulp
    // off at the first and 0.87 ulp at the second, so these are independent
    // rather than exact, and carry the same 4 ulp as the sweep.
    const std::array<EdgeAnchor, 2> anchors{
        {
            {.fov = Radians{kPi / 2.0}, .halfAngleTangent = 1.0},
            {.fov = Radians{kPi / 3.0}, .halfAngleTangent = std::numbers::inv_sqrt3_v<f64>},
        },
    };
    for (const auto& [fov, halfAngleTangent] : anchors) {
        for (const f64 aspect : {1.0, 16.0 / 9.0, 0.5}) {
            const Projection projection = projectionOrFail(fov, Aspect{aspect}, Metres{0.05});
            const f64 halfHeight = kEdgeDistance * halfAngleTangent;
            CAPTURE(fov.value(), aspect);

            const ViewPoint right{
                .xyz = Position{halfHeight * aspect, 0.0, -kEdgeDistance},
                .w = Dimensionless{1.0},
            };
            const auto rightNdc = perspectiveDivide(transform(projection, right));
            REQUIRE(nearlyEqual(rightNdc.v.x.value(), 1.0, Tolerance{kEdgeUlps * kEpsilon}));

            // **Up in view space is down on screen**, and the sign is the
            // claim. A check on the magnitude alone passes whether the y flip
            // is present, missing **or applied twice** -- which would render
            // the whole scene upside down with a green suite.
            const ViewPoint up{
                .xyz = Position{0.0, halfHeight, -kEdgeDistance},
                .w = Dimensionless{1.0},
            };
            const auto upNdc = perspectiveDivide(transform(projection, up));
            REQUIRE(nearlyEqual(upNdc.v.y.value(), -1.0, Tolerance{kEdgeUlps * kEpsilon}));
        }
    }
}

TEST_CASE("the frustum edge lands on the screen edge across the admissible range") {
    // A sweep over the whole range, which the two anchors cannot cover. This
    // half does route through std::tan on both sides, so it checks the aspect
    // handling, the signs and the rows rather than the conversion.
    constexpr f64 kDistance = kEdgeDistance;
    // Seeded deliberately, which is the whole point: a failure has to be
    // reproducible (VERIFICATION.md rule 12), and the seed is written down
    // above. Same suppression and same reason as tests/test_view_math.cpp.
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng{kSweepSeed};
    std::uniform_real_distribution<f64> fovDistribution{0.01, kPi - 0.01};
    // Drawn as a power of ten rather than through logarithms, so the sweep
    // spans 0.1 to 10 and 0.01 to 100 without naming a logarithm of ten.
    std::uniform_real_distribution<f64> aspectExponent{-1.0, 1.0};
    std::uniform_real_distribution<f64> nearExponent{-2.0, 2.0};
    f64 worstX = 0.0;
    f64 worstY = 0.0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const Radians fov{fovDistribution(rng)};
        const f64 aspect = std::pow(10.0, aspectExponent(rng));
        const Projection projection =
            projectionOrFail(fov, Aspect{aspect}, Metres{std::pow(10.0, nearExponent(rng))});
        const f64 halfHeight = kDistance * std::tan(fov.value() / 2.0);

        const ViewPoint corner{
            .xyz = Position{halfHeight * aspect, halfHeight, -kDistance},
            .w = Dimensionless{1.0},
        };
        const auto ndc = perspectiveDivide(transform(projection, corner));
        CAPTURE(i, fov.value(), aspect);
        worstX = std::max(worstX, std::abs(ndc.v.x.value() - 1.0) / kEpsilon);
        worstY = std::max(worstY, std::abs(ndc.v.y.value() + 1.0) / kEpsilon);
    }
    CAPTURE(worstX, worstY);
    REQUIRE(worstX <= kEdgeUlps);
    REQUIRE(worstY <= kEdgeUlps);
}

TEST_CASE("a near plane that cannot be used is refused by name") {
    for (const f64 nearPlane : kUnusableMagnitudes) {
        CAPTURE(nearPlane);
        // **Infinity is greater than zero**, so a positivity test alone lets
        // an infinite near plane through and builds a matrix of infinities
        // that draws nothing. That is why the finiteness test is there, and
        // this is the case that says so.
        REQUIRE(refusedError(kOrdinaryFov, kOrdinaryAspect, Metres{nearPlane}) ==
                ProjectionError::InvalidNearPlane);
    }
}

TEST_CASE("an aspect ratio that cannot be used is refused by name") {
    for (const f64 aspect : kUnusableMagnitudes) {
        CAPTURE(aspect);
        REQUIRE(refusedError(kOrdinaryFov, Aspect{aspect}, kOrdinaryNear) ==
                ProjectionError::InvalidAspect);
    }
}

TEST_CASE("a field of view that cannot be used is refused by name") {
    // A half turn is refused as well as zero and the non-finite values: at pi
    // the frustum has opened out flat and the focal length has collapsed.
    const std::array<f64, 5> unusable{
        {0.0, kPi, -1.0, std::numeric_limits<f64>::infinity(), kNotANumber},
    };
    for (const f64 fov : unusable) {
        CAPTURE(fov);
        REQUIRE(refusedError(Radians{fov}, kOrdinaryAspect, kOrdinaryNear) ==
                ProjectionError::InvalidFieldOfView);
    }
}

TEST_CASE("the admissible extremes are accepted, and each error says something different") {
    // Not a function that refuses everything: the far corners of the
    // admissible range still produce a projection.
    REQUIRE(infiniteReverseZPerspective(Radians{kPi * 0.999}, kOrdinaryAspect, Metres{1e-9})
                .has_value());
    REQUIRE(infiniteReverseZPerspective(Radians{1e-9}, Aspect{1e-9}, Metres{1e9}).has_value());

    REQUIRE(describe(ProjectionError::InvalidNearPlane) !=
            describe(ProjectionError::InvalidAspect));
    REQUIRE(describe(ProjectionError::InvalidAspect) !=
            describe(ProjectionError::InvalidFieldOfView));
    REQUIRE(!describe(ProjectionError::InvalidNearPlane).empty());
}

TEST_CASE("a point behind the camera lands back on screen, and is told apart by name") {
    // **The hazard `isInFrontOfCamera` exists for** (M1-11, register decision
    // 122), and the measurement that earned it its place. A negative w is the
    // distance in front of the camera come out negative, and dividing by it
    // flips both signs -- so the point does not vanish, it reappears mirrored.
    const Projection projection = projectionOrFail(kOrdinaryFov, kOrdinaryAspect, kOrdinaryNear);

    const ViewPoint inFront{.xyz = Position{0.5, 0.4, -10.0}, .w = Dimensionless{1.0}};
    const ViewPoint behind{.xyz = Position{-0.5, -0.4, 10.0}, .w = Dimensionless{1.0}};

    const ClipPoint frontClip = transform(projection, inFront);
    const ClipPoint behindClip = transform(projection, behind);

    // The names say it, which is the whole point of the function.
    REQUIRE(isInFrontOfCamera(frontClip));
    REQUIRE(!isInFrontOfCamera(behindClip));

    // And the arithmetic does not: x and y are **bit-identical**, not merely
    // close. Nothing downstream of the divide could tell these apart.
    const auto frontNdc = perspectiveDivide(frontClip);
    const auto behindNdc = perspectiveDivide(behindClip);
    REQUIRE(frontNdc.v.x.bitIdentical(behindNdc.v.x));
    REQUIRE(frontNdc.v.y.bitIdentical(behindNdc.v.y));

    // The depth is what differs, and it leaves the unit interval -- which is
    // the signal a caller would have to know to look for, and the reason the
    // predicate above is better than expecting them to.
    REQUIRE(frontNdc.v.z.value() > 0.0);
    REQUIRE(behindNdc.v.z.value() < 0.0);

    // A point on the camera plane is the loud case, and it is now a
    // precondition rather than an infinity. Asserted in Debug; here the claim
    // is only that the predicate refuses it, since w is not greater than zero.
    const ClipPoint onThePlane =
        transform(projection, ViewPoint{.xyz = Position{0.5, 0.4, 0.0}, .w = Dimensionless{1.0}});
    REQUIRE(!isInFrontOfCamera(onThePlane));
}
