#ifndef ORBSIM_VIEW_CAMERA_HPP
#define ORBSIM_VIEW_CAMERA_HPP
//
// The camera, and the one place 64-bit simulation coordinates become the
// 32-bit numbers a graphics card consumes (M1-11; ADR 0012 for where this
// lives, ADR 0021 for the frames, ADR 0022 for why the camera validates
// itself).
//
// **The number this file exists to protect.** A vertex on Earth's surface is
// 6.371e6 m from the origin, and the gap between neighbouring 32-bit floating
// point numbers there is **exactly 0.5 m** -- measured, not estimated. Narrow
// a world coordinate directly and it lands on a half-metre lattice; subtract
// the camera position in 64 bits first and only the small difference is
// narrowed, where the gap is centimetres. `toRenderSpace` below is the only
// place in `src/` that narrows, and `grep static_cast<f32> src/` is the audit.
//
// **What that buys, as a law rather than as one number.** The screen-space
// error of the narrowing is
//
//     error <= kNarrowingBound * (screenHeightPixels / 2) * 2^-24 * focalLength
//
// where 2^-24 is the relative resolution of a 32-bit float and focalLength is
// 1 / tan(verticalFov / 2). **It does not contain the distance from the
// origin, and that is the whole claim of camera-relative rendering.** Measured
// at 0.683 of that bound across six decades of world magnitude -- Earth
// radius, lunar distance, 1 AU, 4.5e12 m -- and across fields of view from 10
// to 120 degrees, with the ratio constant to three digits. At 1920x1080 and a
// 45-degree field of view the bound is 1.55e-4 px.
//
// **Where the naive path actually fails, because it is not where the plan
// said.** Narrowing before subtracting was expected to break the 0.05 px
// acceptance criterion with a camera at 400 km altitude. Measured, it does
// not: it lands at 5.8e-4 px there, 86 times inside the budget, because half a
// metre at a range of 400 km is 1.25 microradians. It fails at **1 AU** (4.7
// px, 95 times over) and at **short range** (0.43 px at 1 km, 5.6 px at 100 m,
// 142.9 px at 1 km from lunar distance). The error grows with the world
// magnitude and shrinks with the range, and the suite asserts it where it
// genuinely bites. Register decision 115.
//
// **The conventions are `view/Projection.hpp`'s**, stated there once: view
// space is right-handed and looks down -z, clip space is Vulkan's with y
// downward, and depth is reversed. Nothing here restates them.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Mat4.hpp"
#include "view/Projection.hpp"

#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace orb::view {

// The ways a camera can be asked for and not exist. Reported rather than
// asserted, because a scenario file or a control loop can produce every one of
// them (ADR 0002, VERIFICATION.md rule 7).
//
// **One value per argument**, so the caller is told which argument to fix --
// the form `ProjectionError` set, and each name says what is wrong physically
// rather than which predicate failed (register decision 111).
enum class CameraError : std::uint8_t {
    NotFinitePosition,  // a component is infinite or not a number
    NotUnitOrientation, // the quaternion is not a rotation
    InvalidFieldOfView, // not inside (0, pi); a NaN lands here too
    InvalidNearPlane,   // not finite, or not greater than zero
};

[[nodiscard]] constexpr std::string_view describe(CameraError error) noexcept {
    switch (error) {
    case CameraError::NotFinitePosition:
        return "the camera position must be finite in every component";
    case CameraError::NotUnitOrientation:
        return "the camera orientation must be a unit quaternion";
    case CameraError::InvalidFieldOfView:
        return "the vertical field of view must be between zero and pi";
    case CameraError::InvalidNearPlane:
        return "the near plane must be finite and greater than zero";
    }
    return "unknown camera error";
}

// Where the camera is, which way it faces, and what it sees.
//
// **A validated type, not an aggregate** (ADR 0022, register decision 116).
// The task document specified a plain struct; it was written on 2026-09-08,
// two weeks before a bounded scalar was made to validate itself, and three of
// these four members have exactly the kind of bound that record is about. The
// orientation is the one that matters most: a quaternion that is not unit
// length produces a matrix that is **finite, plausible and not a rotation**,
// which is the failure shape ADR 0022 exists to remove.
//
// **Still trivially copyable**, which the task asked for and which a private
// constructor does not cost -- measured before the shape was chosen, and
// asserted below.
//
// **The orientation is camera-to-world**, the same direction a vessel's
// attitude runs in (`core/Math.hpp`'s note on `Quat`), so a camera bolted to a
// vessel takes that vessel's quaternion unchanged. `viewMatrix` conjugates it.
//
// **Not `constexpr`, and the reason is register decision 99's.** Validation
// calls `std::sqrt` and `std::isfinite`, neither of which is a constant
// expression on all three front ends, so the factory is `inline` and what can
// be checked at compile time is checked below instead.
class Camera {
public:
    [[nodiscard]] static std::expected<Camera, CameraError> from(const Position& position,
                                                                 const Quat& orientation,
                                                                 Radians verticalFov,
                                                                 Metres nearPlane) noexcept;

    [[nodiscard]] constexpr Position position() const noexcept { return position_; }
    [[nodiscard]] constexpr Quat orientation() const noexcept { return orientation_; }
    [[nodiscard]] constexpr Radians verticalFov() const noexcept { return verticalFov_; }
    [[nodiscard]] constexpr Metres nearPlane() const noexcept { return nearPlane_; }

private:
    // Private, so that the only camera that exists is one `from` accepted --
    // the same shape `Eccentricity` and `GravParam` took with ADR 0022.
    //
    // **The only constructor, and it initialises all four members.** The task
    // document asked for every member to be default-initialised, which was the
    // right requirement for the aggregate it assumed; a type with no default
    // state gets the stronger version of the same guarantee instead, because
    // there is no path that leaves a member unset. The four parameter types
    // are all different, so none of them can be transposed
    // (`bugprone-easily-swappable-parameters`, non-negotiable 1).
    constexpr Camera(const Position& position,
                     const Quat& orientation,
                     Radians verticalFov,
                     Metres nearPlane) noexcept
        : position_(position),
          orientation_(orientation),
          verticalFov_(verticalFov),
          nearPlane_(nearPlane) {}

    Position position_;
    Quat orientation_;
    Radians verticalFov_;
    Metres nearPlane_;
};

static_assert(std::is_trivially_copyable_v<Camera>,
              "a camera is a value: copying one is a memcpy, as the task asked");
static_assert(!std::is_default_constructible_v<Camera>,
              "and there is no default camera: one comes from the factory or not at all");

// What a vertex shader consumes. `f32`, because that is what a graphics card
// has, and no unit and no frame, because that is where the type system stops:
// GLSL has neither.
//
// **Three floats and no padding**, asserted below, because this is copied into
// a buffer the GPU reads by offset.
struct Vec3f {
    f32 x{};
    f32 y{};
    f32 z{};
};

static_assert(std::is_trivially_copyable_v<Vec3f>);
static_assert(sizeof(Vec3f) == 3 * sizeof(f32),
              "the GPU reads this by offset: three floats, tightly packed");

// No `==`: on floats it is the comparison CODING_GUIDELINES section 11 forbids
// and `-Wfloat-equal` reports. Bit identity by name instead, as `Vec3`, `Quat`
// and `FramedVec3` each have it.
//
// Free rather than a member, because a member function would stop
// `misc-non-private-member-variables-in-classes` ignoring this struct's public
// members -- the measurement `view/Mat4.hpp` records for `FramedVec3`. The two
// arguments are interchangeable, bit equality being symmetric, so transposing
// them cannot produce a wrong answer: the reason `core/Scalar.hpp` gives on
// `nearlyEqual`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr bool bitIdentical(const Vec3f& left, const Vec3f& right) noexcept {
    return bitsOf(left.x) == bitsOf(right.x) && bitsOf(left.y) == bitsOf(right.y) &&
           bitsOf(left.z) == bitsOf(right.z);
}

namespace detail {

// May this value be narrowed to 32 bits at all?
//
// **Not pedantry: the narrowing is undefined behaviour otherwise.** Converting
// a `double` whose magnitude exceeds `f32`'s maximum is undefined by
// [conv.double] rather than merely inexact, and UndefinedBehaviorSanitizer
// reports it as `float-cast-overflow` -- fatally, since the `linux-sanitize`
// preset builds with `-fno-sanitize-recover=all`. It takes a camera-relative
// offset beyond 3.40e38 m to reach, where the observable universe is 8.8e26 m
// across, so this is the "cannot happen unless this code is wrong" that
// ADR 0002 says to assert rather than report.
//
// **In the header rather than in the .cpp**, for the reason `isAffine` and
// `core/Math.hpp`'s `isRotation` are public: a precondition nothing can check
// from outside is one nobody can test. It also keeps it from becoming an
// unused function under `NDEBUG`, where the assertion that is its only caller
// compiles away.
[[nodiscard]] constexpr bool isNarrowable(f64 value) noexcept {
    return isFinite(value) && absOf(value) <= static_cast<f64>(std::numeric_limits<f32>::max());
}

static_assert(isNarrowable(0.0) && isNarrowable(-1.495978707e11),
              "everything this simulation can produce narrows");
static_assert(!isNarrowable(std::numeric_limits<f64>::infinity()) &&
                  !isNarrowable(-std::numeric_limits<f64>::infinity()),
              "an infinity does not");
static_assert(!isNarrowable(1.0e39) && !isNarrowable(-1.0e39),
              "and neither does a finite value past 32-bit range, which is the "
              "undefined one and the reason this exists");

} // namespace detail

// The world-to-view transform: **the rotation only, with the translation
// identically zero.**
//
// A view matrix with no translation looks like a bug to anyone who has written
// one before, so: the translation has already been applied, in 64 bits, by the
// subtraction inside `toRenderSpace`. Putting it here as well would move the
// point twice, and putting it *only* here would be the naive path this whole
// file exists to avoid -- a translation of 6.8e6 m applied to a 32-bit vertex
// re-introduces the half-metre lattice that the subtraction removed.
//
// **The one declared frame change in the project.** A rotation is within a
// frame (ADR 0021), so what makes this matrix world-to-view is the camera's
// definition of view space and not any property of the arithmetic. That
// declaration is `retargetFrame`, called here and nowhere else in `src/`.
//
// No precondition is asserted: `Camera` cannot hold an orientation that is not
// a rotation, so an assertion here would be the unreachable defensive code
// register decision 112 removed three of.
[[nodiscard]] WorldToView viewMatrix(const Camera& camera) noexcept;

// **The one place `f64` becomes `f32`. Subtract in `f64`, then narrow.**
//
// Three `static_cast<f32>` in one function, exactly as
// `coding-guidelines-example/src/render/PathUpload.cpp` does it. Anything else
// in `src/` that narrows is a defect, and `-Wconversion` is what finds it.
//
// **A span overload belongs here when M1-19 needs one**, beside this function
// rather than anywhere else, so that the narrowing stays greppable in one
// place: the worked example's `toCameraRelative` is the shape to copy.
[[nodiscard]] Vec3f toRenderSpace(const Position& worldMetres, const Camera& camera) noexcept;

// The projection this camera implies, for a given window shape.
//
// **Temporary: delete this when M1-13 lands** (register decision 117). It
// exists so that M1-11's suite can drive the whole chain through one entry
// point and so that the camera's field of view and near plane are not
// decoration for two tasks; M1-13 builds the real pipeline and owns the
// projection from then on. `m1-13-pipelines.md` carries the obligation to
// remove it, so that it is on somebody's checklist rather than in a comment
// nobody opens.
//
// **Only `InvalidAspect` is reachable.** A `Camera` has already been refused
// if its field of view or near plane could produce the other two, and it was
// refused by *these same predicates* -- `Camera.cpp` calls
// `view/Projection.hpp`'s rather than writing a second pair that could drift
// from them (VERIFICATION.md rule 2). The error type is passed straight
// through rather than narrowed to a new enum, because inventing one would say
// a caller could do something different about it.
[[nodiscard]] std::expected<Projection, ProjectionError> projectionOf(const Camera& camera,
                                                                      Aspect aspect) noexcept;

// Compile-time tests. A `static_assert` is a unit test that costs nothing at
// run time, runs on every build whether or not the suite is invoked, and
// cannot rot (CODING_GUIDELINES section 3).

static_assert(describe(CameraError::NotFinitePosition) != describe(CameraError::NotUnitOrientation),
              "each error says which argument to fix");
static_assert(describe(CameraError::NotUnitOrientation) !=
              describe(CameraError::InvalidFieldOfView));
static_assert(describe(CameraError::InvalidFieldOfView) != describe(CameraError::InvalidNearPlane));
static_assert(!describe(CameraError::NotFinitePosition).empty(), "and every one of them says it");

} // namespace orb::view

#endif // ORBSIM_VIEW_CAMERA_HPP
