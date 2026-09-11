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
// bare double. Vec3 components stay f64: a vector's unit is a property of the
// quantity it represents (a position, a velocity), and that is carried by the
// struct holding it, as StateVector does.
//
#include "core/Units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orb {

// ---------------------------------------------------------------- Vec3 -----

struct Vec3 {
    // Public by design; see the note on Quantity::value in core/Scalar.hpp.
    // A vector's components are its interface, there is no invariant to
    // protect, and x() y() z() would be three trivial accessors (C.131).
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    f64 x{};
    f64 y{};
    f64 z{};
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    constexpr Vec3() noexcept = default;
    constexpr Vec3(f64 x_, f64 y_, f64 z_) noexcept : x(x_), y(y_), z(z_) {}

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
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr Vec3& operator/=(f64 s) noexcept {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    // No `==`: on doubles it is the comparison CODING_GUIDELINES section 11
    // forbids (ADR 0017). Comparison goes through nearlyEqual on a length or a
    // component, and a determinism check, where bit identity is the claim,
    // says so by name.
    [[nodiscard]] constexpr bool bitIdentical(const Vec3& other) const noexcept {
        return bitsOf(x) == bitsOf(other.x) && bitsOf(y) == bitsOf(other.y) &&
               bitsOf(z) == bitsOf(other.z);
    }
};

[[nodiscard]] constexpr Vec3 operator*(f64 s, const Vec3& v) noexcept { return v * s; }

[[nodiscard]] constexpr f64 dot(const Vec3& a, const Vec3& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

[[nodiscard]] constexpr f64 lengthSq(const Vec3& v) noexcept { return dot(v, v); }

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
[[nodiscard]] inline f64 length(const Vec3& v) noexcept {
    constexpr f64 kSmallestExactSum = 0x1p-969;
    const f64 sq = dot(v, v);
    if (sq >= kSmallestExactSum && sq <= std::numeric_limits<f64>::max()) return std::sqrt(sq);
    const f64 largest = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    // Zero, or an infinite component: nothing to scale, and the plain sum
    // already gives 0 or infinity. A NaN component comes out NaN either way.
    if (!(largest > 0.0) || !std::isfinite(largest)) return std::sqrt(sq);
    const int exponent = std::ilogb(largest);
    const Vec3 scaled{
        std::scalbn(v.x, -exponent), std::scalbn(v.y, -exponent), std::scalbn(v.z, -exponent)};
    return std::scalbn(std::sqrt(dot(scaled, scaled)), exponent);
}

// Returns the zero vector for a zero-length input rather than NaN; callers in
// the orbital code rely on this to stay well-behaved in degenerate orbits.
[[nodiscard]] inline Vec3 normalize(const Vec3& v) noexcept {
    const f64 len = length(v);
    return len > 0.0 ? v / len : Vec3{};
}

[[nodiscard]] inline f64 distance(const Vec3& a, const Vec3& b) noexcept { return length(a - b); }

// Angle between two vectors, robust near 0 and pi where acos(dot) loses
// precision catastrophically.
[[nodiscard]] inline Radians angleBetween(const Vec3& a, const Vec3& b) noexcept {
    const Vec3 na = normalize(a);
    const Vec3 nb = normalize(b);
    return Radians{2.0 * std::atan2(length(na - nb), length(na + nb))};
}

// Rotate v about a unit axis (Rodrigues' formula).
[[nodiscard]] inline Vec3 rotateAxis(const Vec3& v, const Vec3& axis, Radians angle) noexcept {
    const f64 c = std::cos(angle.value);
    const f64 s = std::sin(angle.value);
    return (v * c) + (cross(axis, v) * s) + (axis * (dot(axis, v) * (1.0 - c)));
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

    [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, Radians angle) noexcept {
        const Vec3 a = normalize(axis);
        const f64 h = angle.value * 0.5;
        const f64 s = std::sin(h);
        return {std::cos(h), a.x * s, a.y * s, a.z * s};
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept { return {w, -x, -y, -z}; }

    [[nodiscard]] constexpr Quat operator*(const Quat& q) const noexcept {
        return {(w * q.w) - (x * q.x) - (y * q.y) - (z * q.z),
                (w * q.x) + (x * q.w) + (y * q.z) - (z * q.y),
                (w * q.y) - (x * q.z) + (y * q.w) + (z * q.x),
                (w * q.z) + (x * q.y) - (y * q.x) + (z * q.w)};
    }

    // Rotate a vector from body space into world space.
    [[nodiscard]] Vec3 rotate(const Vec3& v) const noexcept {
        const Vec3 u{x, y, z};
        const Vec3 t = cross(u, v) * 2.0;
        return v + (t * w) + cross(u, t);
    }

    [[nodiscard]] Vec3 inverseRotate(const Vec3& v) const noexcept { return conjugate().rotate(v); }

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

// Below this angle a rotation is indistinguishable from the identity at f64
// resolution: the quaternion's vector part would be smaller than one ulp of
// its scalar part, so applying it is pure rounding noise. Skipping it keeps
// a stationary body's orientation bit-stable across idle frames.
inline constexpr Radians kNegligibleRotation{1e-12};

// Integrate an orientation by an angular-velocity vector (world frame, rad/s)
// over dt. Uses the exact exponential map rather than the linearised
// q += 0.5*w*q*dt, so it stays a unit quaternion under large time steps.
[[nodiscard]] inline Quat
integrateAngularVelocity(const Quat& q, const Vec3& omega, Seconds dt) noexcept {
    const Radians theta{length(omega) * dt.value};
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
static_assert(nearlyEqual(lengthSq(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) - Vec3{0, 0, 1}),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(cross(Vec3{0, 1, 0}, Vec3{1, 0, 0}) - Vec3{0, 0, -1}),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}), 32.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(Vec3{3, 4, 0}), 25.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(Vec3{1, 2, 3} - Vec3{1, 2, 3}), 0.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(-Vec3{1, -2, 3} - Vec3{-1, 2, -3}), 0.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq((Vec3{1, 2, 3} * 2.0) - Vec3{2, 4, 6}), 0.0, Tolerance{0.0}));
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
static_assert(Vec3{1, 2, 3}.bitIdentical(Vec3{1, 2, 3}));
static_assert(!Vec3{}.bitIdentical(Vec3{-0.0, 0, 0}) && !Vec3{}.bitIdentical(Vec3{0, -0.0, 0}) &&
              !Vec3{}.bitIdentical(Vec3{0, 0, -0.0}));
static_assert(Quat{}.bitIdentical(Quat{}));
static_assert(!Quat{0, 0, 0, 0}.bitIdentical(Quat{-0.0, 0, 0, 0}) &&
              !Quat{}.bitIdentical(Quat{1, -0.0, 0, 0}) &&
              !Quat{}.bitIdentical(Quat{1, 0, -0.0, 0}) &&
              !Quat{}.bitIdentical(Quat{1, 0, 0, -0.0}));

} // namespace orb

#endif // ORBSIM_CORE_MATH_HPP
