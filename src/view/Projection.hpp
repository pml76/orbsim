#ifndef ORBSIM_VIEW_PROJECTION_HPP
#define ORBSIM_VIEW_PROJECTION_HPP
//
// The perspective projection, reversed and with no far plane (M1-10; ADR 0003
// for the decision, ADR 0012 for where it lives, ADRs 0020 and 0021 for what
// `Projection` carries).
//
// **The conventions, stated once, here.** Every one of them is a decision
// somebody downstream would otherwise have to rediscover from a symptom:
//
//   * **view space is right-handed and looks down -z.** A point in front of
//     the camera has a negative z, and its distance from the camera is -z;
//   * **clip space is Vulkan's**: x and y run from -1 to +1 with **y
//     downward**, and depth runs from 0 to 1;
//   * **the y flip lives in this matrix and nowhere else.** It is one negated
//     entry, visible in one place, rather than a sign scattered through the
//     shaders for each author to remember;
//   * **depth is reversed**: the near plane maps to **1.0** and infinity to
//     **0.0**. There is no far plane at all.
//
// **Do not "fix" the reversed depth.** It is [ADR 0003](../../docs/adr/0003-reverse-z-depth.md),
// accepted 2026-09-05, and it is what lets a cockpit panel a metre away and a
// planet a hundred million kilometres away resolve against each other in one
// 32-bit depth buffer. The depth buffer is cleared to 0.0 and every pipeline
// compares with GREATER to match; a pipeline built with LESS out of habit
// draws nothing at all, which is a symptom that points nowhere near its cause.
// **Any projection this project ever adds must map the near plane to 1**, or
// it inherits that failure.
//
// **The matrix.** With `f = 1 / tan(verticalFov / 2)`, column-major:
//
//     f/aspect    0      0      0
//        0       -f      0      0          <- the y flip
//        0        0      0   nearPlane     <- constant: the depth numerator
//        0        0     -1      0          <- the depth denominator is -z
//
// The third row being **constant** is the whole trick, and it is worth naming
// because it is not obvious from the entries: the projection hands back a
// numerator that never varies -- it is always the near plane -- over a
// denominator that is the distance to the point. So
//
//     depth = nearPlane / distance
//
// exactly, to one correctly rounded division, at every distance and every near
// plane. Near gives 1, ten times near gives 0.1, and infinity gives 0 as a
// limit rather than as a clamp.
//
// **What this buys, measured** (tests/test_projection.cpp). Two points one
// metre apart at 1000 km from the camera land on depth values **9 to 14 ulp
// apart** in a 32-bit float buffer, across near planes from 1 cm to 100 m.
// Under a conventional 0-to-1 projection with a far plane at 1e9 m the same
// two points are **bit-identical** -- not merely closer, but indistinguishable,
// which is the z-fighting ADR 0003 exists to avoid.
//
// **Where it runs out, stated rather than left to be discovered.** The
// separation shrinks with distance, and at about 100,000 km two points one
// metre apart also become indistinguishable. That is a limit of 32-bit depth
// and not of this formulation -- for scale, the conventional projection
// reaches the same limit at 1000 km, a hundred times closer. At planetary
// distances the objects being resolved are not a metre apart.
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Mat4.hpp"

#include <cmath>
#include <cstdint>
#include <expected>
#include <string_view>

#include <mp-units/framework.h>

namespace orb::view {

// The window's width divided by its height.
//
// **Its own kind, not a bare number and not a bare `Dimensionless`.** A ratio
// with no kind accepts any other ratio -- a cosine, a count, a focal length --
// and this one is passed beside exactly such a value below. The kind is what
// makes `perspectiveFromFocalLength(aspect, focalLength, ...)` a compile error
// rather than a stretched image nobody can account for, and it is the same
// mechanism `Eccentricity` uses in core/Units.hpp. Here rather than there
// because an aspect ratio is render-side and CODING_GUIDELINES section 12
// pushes the dependency that way.
inline constexpr struct AspectKind final : mp_units::quantity_spec<mp_units::dimensionless> {
} kAspectKind;

using Aspect = Scalar<kAspectKind[mp_units::one]>;

// The ways a projection can be asked for and not exist. Reported rather than
// asserted, because a scenario file or a configuration can produce every one
// of them (ADR 0002, VERIFICATION.md rule 7).
//
// **One value per argument**, so the caller is told which argument to fix,
// with the exact condition written into describe() below. The alternative --
// one value per predicate -- would split each argument in two without giving
// a caller anything to do differently about it.
enum class ProjectionError : std::uint8_t {
    InvalidNearPlane,   // not finite, or not greater than zero
    InvalidAspect,      // not finite, or not greater than zero
    InvalidFieldOfView, // not inside (0, pi); a NaN lands here too
};

[[nodiscard]] constexpr std::string_view describe(ProjectionError error) noexcept {
    switch (error) {
    case ProjectionError::InvalidNearPlane:
        return "the near plane must be finite and greater than zero";
    case ProjectionError::InvalidAspect:
        return "the aspect ratio must be finite and greater than zero";
    case ProjectionError::InvalidFieldOfView:
        return "the vertical field of view must be between zero and pi";
    }
    return "unknown projection error";
}

namespace detail {

// The two admissibility tests, named rather than written inline.
//
// **Named because the negation has to stay a negation.** What each caller
// needs is "refuse unless this holds", and a NaN makes every comparison false,
// so `!(v > 0.0)` refuses a NaN while `v <= 0.0` accepts it. Written inline,
// `readability-simplify-boolean-expr` asks for exactly that rewrite and would
// turn a refusal into an acceptance. Negating a named predicate gives the
// check nothing to simplify and says what is meant besides.
[[nodiscard]] inline bool isFinitePositive(f64 value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

// An angle strictly between zero and half a turn. No finiteness test is
// needed: an infinite angle fails the upper bound and a NaN fails both.
[[nodiscard]] constexpr bool isUsableFieldOfView(f64 radians) noexcept {
    return radians > 0.0 && radians < kPi;
}

// The matrix, from a focal length that has already been computed.
//
// **Split out so that the layout can be checked at compile time.** The public
// entry point below cannot be `constexpr` in any useful sense -- measured
// 2026-09-21, clang 23.1.0 and MSVC 19.51 both refuse `std::tan` in a constant
// expression while gcc-14 accepts it as an extension, so a `static_assert`
// against it would be green on one front end and red on two, which is exactly
// the disagreement the second and third compilers exist to surface. Everything
// except that one tangent *can* run at compile time, and the entries are the
// part most likely to be mistyped, so they are asserted below rather than
// merely tested. M1-09's mutation pass killed seven of its fourteen planted
// bugs before a test ran; this is the same lever.
//
// Internal because a focal length is a less self-explanatory argument than a
// field of view, and only one caller should ever exist.
[[nodiscard]] constexpr Projection
perspectiveFromFocalLength(Dimensionless focalLength, Aspect aspect, Metres nearPlane) noexcept {
    ORBSIM_EXPECTS(focalLength.value() > 0.0 && aspect.value() > 0.0 && nearPlane.value() > 0.0);
    Projection m{};
    m.set(Row{0}, Column{0}, focalLength.value() / aspect.value());
    // Negated: Vulkan's y runs down the screen where view space's runs up.
    m.set(Row{1}, Column{1}, -focalLength.value());
    // Row 2 is constant, so the depth numerator does not depend on the point.
    m.set(Row{2}, Column{3}, nearPlane.value());
    // Row 3 turns the point's -z into a positive distance.
    m.set(Row{3}, Column{2}, -1.0);
    return m;
}

} // namespace detail

// The projection ADR 0003 describes. See the header comment for the
// conventions and for why the depth is the way round it is.
[[nodiscard]] inline std::expected<Projection, ProjectionError>
infiniteReverseZPerspective(Radians verticalFov, Aspect aspect, Metres nearPlane) noexcept {
    // Finiteness is checked as well as positivity, and that is not belt and
    // braces: **infinity is greater than zero**, so a positivity test alone
    // accepts an infinite near plane and returns a matrix of infinities that
    // draws nothing.
    if (!detail::isFinitePositive(nearPlane.value())) {
        return std::unexpected(ProjectionError::InvalidNearPlane);
    }
    if (!detail::isFinitePositive(aspect.value())) {
        return std::unexpected(ProjectionError::InvalidAspect);
    }
    if (!detail::isUsableFieldOfView(verticalFov.value())) {
        return std::unexpected(ProjectionError::InvalidFieldOfView);
    }
    const Dimensionless focalLength{1.0 / std::tan(verticalFov.value() / 2.0)};
    return detail::perspectiveFromFocalLength(focalLength, aspect, nearPlane);
}

// Compile-time tests of the layout, which is the half of this file that does
// not need a tangent. A static_assert costs nothing at runtime, runs on every
// build whether or not the suite is invoked, and cannot rot
// (CODING_GUIDELINES section 3).

inline constexpr Dimensionless kFocalForAssertions{2.0};
inline constexpr Aspect kAspectForAssertions{4.0};
inline constexpr Metres kNearForAssertions{0.05};
inline constexpr Projection kProjectionForAssertions = detail::perspectiveFromFocalLength(
    kFocalForAssertions, kAspectForAssertions, kNearForAssertions);

static_assert(nearlyEqual(kProjectionForAssertions.linear(Row{0}, Column{0}).value(),
                          0.5,
                          Tolerance{0.0}),
              "the horizontal scale is the focal length over the aspect ratio");
static_assert(nearlyEqual(kProjectionForAssertions.linear(Row{1}, Column{1}).value(),
                          -2.0,
                          Tolerance{0.0}),
              "and the vertical scale is negated: the y flip, in one place");
static_assert(nearlyEqual(kProjectionForAssertions.linear(Row{2}, Column{2}).value(),
                          0.0,
                          Tolerance{0.0}),
              "the depth numerator does not depend on the point's z -- the row is constant");
static_assert(nearlyEqual(kProjectionForAssertions.translation(Row{2}).value(),
                          0.05,
                          Tolerance{0.0}),
              "the depth numerator is the near plane, and it is in metres");
static_assert(nearlyEqual(kProjectionForAssertions.bottomRow(Column{2}).value(),
                          -1.0,
                          Tolerance{0.0}),
              "the depth denominator is -z, which is the distance from the camera");
static_assert(nearlyEqual(kProjectionForAssertions.corner().value(), 0.0, Tolerance{0.0}),
              "and there is no constant term under it: this is where a far plane would live");

static_assert(!isAffine(kProjectionForAssertions),
              "a perspective projection is not affine, which is what makes transformPoint "
              "refuse it at compile time (register decision 93)");

} // namespace orb::view

#endif // ORBSIM_VIEW_PROJECTION_HPP
