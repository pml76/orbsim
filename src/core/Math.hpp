#ifndef ORBSIM_CORE_MATH_HPP
#define ORBSIM_CORE_MATH_HPP
//
// Vector and quaternion maths for the simulation core.
//
// Everything in the physics domain is f64 and expressed in SI units (metres,
// seconds, kilograms, radians). Conversion to f32 happens only at the boundary
// with the renderer, after the camera-relative transform has shrunk the
// magnitudes to something f32 can represent without visible jitter.
//
// Frame convention: right-handed, Z "up" (ecliptic north), X toward the
// reference direction (vernal equinox). This is the usual astrodynamics
// convention and keeps the orbital-element formulae in their textbook form.
//
// Scalars and the strong unit types live below this header (core/Scalar.hpp,
// core/Units.hpp), which is what lets a rotation take a Radians rather than a
// bare double. **Vec3 components carry their unit too, since 2026-09-17**
// (ADR 0019 step 2); before that they were bare f64 and a vector's unit was a
// property of the struct holding it, written in a comment on StateVector::pos.
// Quat stays dimensionless, because a rotation is.
//
#include "core/Units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orb {

// ---------------------------------------------------------------- Vec3 -----

// A vector whose components carry their unit, since 2026-09-17 (ADR 0019
// step 2). `Vec3<units::kMetre>` is a position and `Vec3<units::kMetre /
// units::kSecond>` a velocity, and the two do not add.
//
// The parameter is an **mp-units reference**, not one of the nine named types,
// and that is the point. `cross(r, v)` is m2/s and its own dot product is
// m4/s2; neither has a name in core/Units.hpp and neither should. Templating
// on the reference lets the result unit be *computed* -- `R1 * R2` -- so the
// chain stays typed however far it runs, which a fixed list of names cannot do.
//
// The vector operations are ours and only the unit algebra beneath them is the
// library's: mp-units has no `vector_product` on quantities, in v2.5.0 or on
// master.
template <auto kReference> struct Vec3 {
    using Component = Scalar<kReference>;

    // Public by design; see the note on Quantity::value in core/Scalar.hpp.
    // A vector's components are its interface, there is no invariant to
    // protect, and x() y() z() would be three trivial accessors (C.131).
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    Component x{};
    Component y{};
    Component z{};
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    constexpr Vec3() noexcept = default;
    constexpr Vec3(Component x_, Component y_, Component z_) noexcept : x(x_), y(y_), z(z_) {}

    // "these many of my unit", which is the spelling every existing call site
    // uses. Safe where `Scalar(f64)` would not be, because three components
    // cannot be mistaken for a conversion from a single number.
    constexpr Vec3(f64 x_, f64 y_, f64 z_) noexcept
        : x(Component{x_}), y(Component{y_}), z(Component{z_}) {}

    [[nodiscard]] constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
    [[nodiscard]] constexpr Vec3 operator+(const Vec3& v) const noexcept {
        return {x + v.x, y + v.y, z + v.z};
    }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& v) const noexcept {
        return {x - v.x, y - v.y, z - v.z};
    }
    [[nodiscard]] constexpr Vec3 operator*(f64 s) const noexcept { return {x * s, y * s, z * s}; }
    [[nodiscard]] constexpr Vec3 operator/(f64 s) const noexcept { return {x / s, y / s, z / s}; }

    constexpr Vec3& operator+=(const Vec3& v) noexcept {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& v) noexcept {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }
    constexpr Vec3& operator*=(f64 s) noexcept {
        x = x * s;
        y = y * s;
        z = z * s;
        return *this;
    }
    constexpr Vec3& operator/=(f64 s) noexcept {
        x = x / s;
        y = y / s;
        z = z / s;
        return *this;
    }

    // No `==`: on doubles it is the comparison CODING_GUIDELINES section 11
    // forbids (ADR 0017). Comparison goes through nearlyEqual on a length or a
    // component, and a determinism check, where bit identity is the claim, says
    // so by name. Two Vec3 of different units are simply different types, so
    // nothing needs deleting here the way it does on Scalar.
    [[nodiscard]] constexpr bool bitIdentical(const Vec3& other) const noexcept {
        return x.bitIdentical(other.x) && y.bitIdentical(other.y) && z.bitIdentical(other.z);
    }
};

// A direction: the result of normalising anything. `one` multiplies away, so
// `cross(Direction, Vec3<R>)` is a `Vec3<R>` and the algebra stays closed.
using Direction = Vec3<mp_units::one>;

// The vector quantities this project names. Anything else -- r x v in m2/s,
// say -- is spelled as the Vec3 the algebra produces and needs no name.
using Position = Vec3<units::kMetre>;
using Velocity = Vec3<units::kMetre / units::kSecond>;
using AngularVelocity = Vec3<units::kRadian / units::kSecond>;

// r x v. m2/s, which is also the unit of kinematic viscosity -- the collision
// ADR 0019 cites as the reason the dimension system had to model kinds and not
// only dimensions.
using SpecificAngularMomentum = Vec3<units::kMetre*(units::kMetre / units::kSecond)>;

template <auto R> [[nodiscard]] constexpr Vec3<R> operator*(f64 s, const Vec3<R>& v) noexcept {
    return v * s;
}

// Scaling by a quantity, which changes the unit: a velocity times a duration
// is a displacement, and the type now says so.
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator*(const Vec3<R1>& v, Scalar<R2> s) noexcept {
    return Vec3<R1 * R2>{v.x * s, v.y * s, v.z * s};
}
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator*(Scalar<R2> s, const Vec3<R1>& v) noexcept {
    return v * s;
}
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator/(const Vec3<R1>& v, Scalar<R2> s) noexcept {
    return Vec3<R1 / R2>{v.x / s, v.y / s, v.z / s};
}

template <auto R1, auto R2>
[[nodiscard]] constexpr auto dot(const Vec3<R1>& a, const Vec3<R2>& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

template <auto R1, auto R2>
[[nodiscard]] constexpr auto cross(const Vec3<R1>& a, const Vec3<R2>& b) noexcept {
    return Vec3<R1 * R2>{
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
}

template <auto R> [[nodiscard]] constexpr auto lengthSq(const Vec3<R>& v) noexcept {
    return dot(v, v);
}

// |v|, to 2 ulp at every scale a double reaches.
//
// sqrt(dot(v, v)) is that good only while the sum of squares stays in the
// normal range. Below about 1.5e-154 the squares fall into the subnormals,
// which keep fewer significant bits the smaller they get; above about 1.3e154
// they overflow. The first cost an eccentricity its sign: the fuzzer found a
// position of 1.25e-158 m whose length came out 7e-9 too long, and a
// hyperbola read as an ellipse (2026-09-11, tests/test_orbit_scales.cpp).
//
// So the sum is taken exactly as before wherever that loses nothing -- at or
// above 2^-969 a subnormal square's rounding error is below 2^-106 of the sum,
// and a finite sum of non-negative terms had none overflow -- which leaves
// every such result bit-identical to before (measured on 5 million vectors,
// on both toolchains). Outside that, each component is scaled by the power of
// two that brings the largest into [1, 2), which is exact because only the
// exponent changes, and the square root is scaled back by the same power,
// exact again. Per component, because scalbn(1.0, -k) itself overflows when
// the largest component is subnormal.
//
// The arithmetic runs on the numerical values rather than on the quantities,
// deliberately: scalbn and ilogb have no unit-aware form. The expression
// structure is unchanged from the f64 version, which is what keeps the
// measured bit-identity above true.
template <auto R> [[nodiscard]] inline Scalar<R> length(const Vec3<R>& v) noexcept {
    constexpr f64 kSmallestExactSum = 0x1p-969;
    const f64 sq = lengthSq(v).value();
    if (sq >= kSmallestExactSum && sq <= std::numeric_limits<f64>::max()) {
        return Scalar<R>{std::sqrt(sq)};
    }
    const f64 largest =
        std::max({std::abs(v.x.value()), std::abs(v.y.value()), std::abs(v.z.value())});
    // Zero, or an infinite component: nothing to scale, and the plain sum
    // already gives 0 or infinity. A NaN component comes out NaN either way.
    if (!(largest > 0.0) || !std::isfinite(largest)) return Scalar<R>{std::sqrt(sq)};
    const int exponent = std::ilogb(largest);
    const Vec3<R> scaled{
        std::scalbn(v.x.value(), -exponent),
        std::scalbn(v.y.value(), -exponent),
        std::scalbn(v.z.value(), -exponent),
    };
    return Scalar<R>{std::scalbn(std::sqrt(lengthSq(scaled).value()), exponent)};
}

// Returns the zero vector for a zero-length input rather than NaN; callers in
// the orbital code rely on this to stay well-behaved in degenerate orbits.
//
// **Keeps its unit rather than returning a dimensionless direction.** The
// owner's call, 2026-09-17. Mathematically a unit vector has no unit, and
// typing it `Direction` would let rotateAxis prove that its axis is a
// direction; the cost was restating the unit at every call site that assigns a
// normalised vector back into a position. So the type here is a little
// generous -- it says metres, and the value is a pure ratio. Reach for
// directionOf() where the dimensionless form is wanted.
template <auto R> [[nodiscard]] inline Vec3<R> normalize(const Vec3<R>& v) noexcept {
    const Scalar<R> len = length(v);
    return len > Scalar<R>{} ? v / len.value() : Vec3<R>{};
}

// The same thing typed honestly, for the places that want a direction rather
// than a position that happens to be one unit long.
template <auto R> [[nodiscard]] inline Direction directionOf(const Vec3<R>& v) noexcept {
    const Vec3<R> n = normalize(v);
    return Direction{n.x.value(), n.y.value(), n.z.value()};
}

template <auto R>
[[nodiscard]] inline Scalar<R> distance(const Vec3<R>& a, const Vec3<R>& b) noexcept {
    return length(a - b);
}

// Angle between two vectors, robust near 0 and pi where acos(dot) loses
// precision catastrophically. The two may carry different units -- the angle
// between a position and a velocity is an ordinary thing to want, and it is
// dimensionless however either is measured.
template <auto R1, auto R2>
[[nodiscard]] inline Radians angleBetween(const Vec3<R1>& a, const Vec3<R2>& b) noexcept {
    const Direction na = directionOf(a);
    const Direction nb = directionOf(b);
    return Radians{2.0 * std::atan2(length(na - nb).value(), length(na + nb).value())};
}

// Rotate v about an axis (Rodrigues' formula). The axis is normalised here, so
// its unit does not matter and only its direction is used; the result carries
// v's unit, because rotating something does not change what is being measured.
template <auto R1, auto R2>
[[nodiscard]] inline Vec3<R1>
rotateAxis(const Vec3<R1>& v, const Vec3<R2>& axis, Radians angle) noexcept {
    const f64 c = std::cos(angle.value());
    const f64 s = std::sin(angle.value());
    const Direction u = directionOf(axis);
    return (v * c) + (cross(u, v) * s) + (u * (dot(u, v) * (1.0 - c)));
}

// ---------------------------------------------------------------- Quat -----

// Unit quaternion, w + xi + yj + zk, representing a body->world rotation.
struct Quat {
    // Public by design; see the note on Quantity::value in core/Scalar.hpp.
    // Unit length is a precondition of the rotation functions rather than an
    // invariant this struct maintains -- normalize() is a free function, and
    // integrateAngularVelocity re-normalises deliberately -- so there is
    // nothing here for encapsulation to protect.
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    f64 w{1.0};
    f64 x{};
    f64 y{};
    f64 z{};
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    constexpr Quat() noexcept = default;
    constexpr Quat(f64 w_, f64 x_, f64 y_, f64 z_) noexcept : w(w_), x(x_), y(y_), z(z_) {}

    // The axis may be in any unit: it is normalised here and only its
    // direction is used.
    template <auto R>
    [[nodiscard]] static Quat fromAxisAngle(const Vec3<R>& axis, Radians angle) noexcept {
        const Direction a = directionOf(axis);
        const f64 h = angle.value() * 0.5;
        const f64 s = std::sin(h);
        return {std::cos(h), a.x.value() * s, a.y.value() * s, a.z.value() * s};
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept { return {w, -x, -y, -z}; }

    [[nodiscard]] constexpr Quat operator*(const Quat& q) const noexcept {
        return {(w * q.w) - (x * q.x) - (y * q.y) - (z * q.z),
                (w * q.x) + (x * q.w) + (y * q.z) - (z * q.y),
                (w * q.y) - (x * q.z) + (y * q.w) + (z * q.x),
                (w * q.z) + (x * q.y) - (y * q.x) + (z * q.w)};
    }

    // Rotate a vector from body space into world space. The unit comes back
    // unchanged -- rotating a position gives a position -- which works out
    // because the quaternion's own vector part is dimensionless and `one`
    // multiplies away in the unit algebra.
    template <auto R> [[nodiscard]] Vec3<R> rotate(const Vec3<R>& v) const noexcept {
        const Direction u{x, y, z};
        const Vec3<R> t = cross(u, v) * 2.0;
        return v + (t * w) + cross(u, t);
    }

    template <auto R> [[nodiscard]] Vec3<R> inverseRotate(const Vec3<R>& v) const noexcept {
        return conjugate().rotate(v);
    }

    // No `==`, for the reason given on Vec3; bit identity by name instead.
    [[nodiscard]] constexpr bool bitIdentical(const Quat& other) const noexcept {
        return bitsOf(w) == bitsOf(other.w) && bitsOf(x) == bitsOf(other.x) &&
               bitsOf(y) == bitsOf(other.y) && bitsOf(z) == bitsOf(other.z);
    }
};

[[nodiscard]] inline Quat normalize(const Quat& q) noexcept {
    const f64 n = std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    return n > 0.0 ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{};
}

// Below this rotation *per step*, integrating is skipped, so that a stationary
// body's orientation stays bit-identical across idle frames rather than
// drifting by a rounding error per frame. That is the whole reason for the
// threshold, and it is why the value is deliberately far above the resolution
// of the type: a rate small enough to be skipped is one nothing can show. At
// 60 Hz, 1e-12 rad per step is 6e-11 rad/s, which accumulates 0.045 arcsec in
// an hour.
//
// **It used to claim something else, and the claim was wrong by four orders of
// magnitude.** The comment here said the quaternion's vector part would be
// smaller than one ulp of its scalar part. fromAxisAngle halves the angle, so
// at 1e-12 rad the vector part is sin(5e-13) = 5e-13 while one ulp of the
// scalar part (which is ~1) is 2.2e-16 -- about 2250 ulps, not less than one.
// The angle where that criterion really holds is about 4.4e-16 rad, and a
// threshold there would not buy the bit-stability above. Corrected 2026-09-13;
// the number did not move, because the number was never what was wrong.
//
// Nothing exercises this yet: integrateAngularVelocity has no caller (6-DOF is
// docs/plan/realism.md section 1.4), so the first test of it should assert the
// bit-stability this constant exists for.
inline constexpr Radians kNegligibleRotation{1e-12};

// Integrate an orientation by an angular-velocity vector (world frame, rad/s)
// over dt. Uses the exact exponential map rather than the linearised
// q += 0.5*w*q*dt, so it stays a unit quaternion under large time steps.
[[nodiscard]] inline Quat
integrateAngularVelocity(const Quat& q, const AngularVelocity& omega, Seconds dt) noexcept {
    // rad/s times s is rad, and the type system now checks that rather than
    // taking it on trust: before ADR 0019 this line multiplied two bare
    // doubles and named the result Radians.
    const Radians theta = length(omega) * dt;
    if (theta < kNegligibleRotation) return q;
    return normalize(Quat::fromAxisAngle(omega, theta) * q);
}

// Compile-time tests. A static_assert is a unit test that costs nothing at
// runtime, runs on every build whether or not the suite is invoked, and cannot
// rot. std::sqrt is not constexpr until C++26, so length() is not covered here.
//
// Every result is exact, so the tolerance is zero. A vector is compared through
// the squared length of the difference, which is zero only when every
// component is: the components are small integers, so no nonzero difference
// can square to an underflowed zero.
static_assert(nearlyEqual(
    lengthSq(cross(Direction{1, 0, 0}, Direction{0, 1, 0}) - Direction{0, 0, 1}).value(),
    0.0,
    Tolerance{0.0}));
static_assert(nearlyEqual(
    lengthSq(cross(Direction{0, 1, 0}, Direction{1, 0, 0}) - Direction{0, 0, -1}).value(),
    0.0,
    Tolerance{0.0}));
static_assert(nearlyEqual(dot(Direction{1, 2, 3}, Direction{4, 5, 6}).value(),
                          32.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(Direction{3, 4, 0}).value(), 25.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(Direction{1, 2, 3} - Direction{1, 2, 3}).value(),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(-Direction{1, -2, 3} - Direction{-1, 2, -3}).value(),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq((Direction{1, 2, 3} * 2.0) - Direction{2, 4, 6}).value(),
                          0.0,
                          Tolerance{0.0}));

// What the units buy, asserted rather than described. None of these compiled
// before 2026-09-17, and the first two are the ones ADR 0019 exists for.
static_assert(std::is_same_v<decltype(cross(Position{}, Velocity{})),
                             Vec3<units::kMetre*(units::kMetre / units::kSecond)>>,
              "r x v is m2/s, a unit with no name in this codebase");
static_assert(std::is_same_v<decltype(dot(Position{}, Velocity{})),
                             Scalar<units::kMetre*(units::kMetre / units::kSecond)>>,
              "and r . v is the same unit, as a scalar");
static_assert(std::is_same_v<decltype(length(Position{})), Metres>,
              "the length of a position is a length");
static_assert(std::is_same_v<decltype(Velocity{} * Seconds{}), Position>,
              "a velocity times a duration is a displacement");
template <typename A, typename B>
concept vecAddable = requires(const A& x, const B& y) { x + y; };
static_assert(vecAddable<Position, Position>);
static_assert(!vecAddable<Position, Velocity>, "a position plus a velocity must not compile");
// Quat has no subtraction, so it is compared component by component.
// Conjugating the identity gives -0 components: equal to +0 as numbers, which
// is the claim, and not bit-identical to them, which is why this is not
// bitIdentical.
static_assert(
    [] {
        const Quat q = Quat{}.conjugate();
        return nearlyEqual(q.w, 1.0, Tolerance{0.0}) && nearlyEqual(q.x, 0.0, Tolerance{0.0}) &&
               nearlyEqual(q.y, 0.0, Tolerance{0.0}) && nearlyEqual(q.z, 0.0, Tolerance{0.0});
    }(),
    "the identity is its own conjugate");
static_assert(
    [] {
        const Quat q = Quat{} * Quat{0, 1, 0, 0};
        return nearlyEqual(q.w, 0.0, Tolerance{0.0}) && nearlyEqual(q.x, 1.0, Tolerance{0.0}) &&
               nearlyEqual(q.y, 0.0, Tolerance{0.0}) && nearlyEqual(q.z, 0.0, Tolerance{0.0});
    }(),
    "identity is the unit");
static_assert(
    [] {
        const Quat q = Quat{0, 1, 0, 0} * Quat{0, 1, 0, 0};
        return nearlyEqual(q.w, -1.0, Tolerance{0.0}) && nearlyEqual(q.x, 0.0, Tolerance{0.0}) &&
               nearlyEqual(q.y, 0.0, Tolerance{0.0}) && nearlyEqual(q.z, 0.0, Tolerance{0.0});
    }(),
    "i * i = -1");
static_assert(!Quat{}.conjugate().bitIdentical(Quat{}), "the conjugate's zeros are negative");
// bitIdentical compares every component, and tells the two zeros apart.
static_assert(Direction{1, 2, 3}.bitIdentical(Direction{1, 2, 3}));
static_assert(!Direction{}.bitIdentical(Direction{-0.0, 0, 0}) &&
              !Direction{}.bitIdentical(Direction{0, -0.0, 0}) &&
              !Direction{}.bitIdentical(Direction{0, 0, -0.0}));
static_assert(Quat{}.bitIdentical(Quat{}));
static_assert(!Quat{0, 0, 0, 0}.bitIdentical(Quat{-0.0, 0, 0, 0}) &&
              !Quat{}.bitIdentical(Quat{1, -0.0, 0, 0}) &&
              !Quat{}.bitIdentical(Quat{1, 0, -0.0, 0}) &&
              !Quat{}.bitIdentical(Quat{1, 0, 0, -0.0}));

} // namespace orb

#endif // ORBSIM_CORE_MATH_HPP
