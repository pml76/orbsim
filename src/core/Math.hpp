#pragma once
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

#include <cmath>

namespace orb {

// ---------------------------------------------------------------- Vec3 -----

struct Vec3 {
    f64 x{};
    f64 y{};
    f64 z{};

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

    // Exact comparison, for the compile-time tests below and for determinism
    // checks, where bit identity is the claim. Approximate comparison goes
    // through nearlyEqual on a length or a component.
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

[[nodiscard]] constexpr Vec3 operator*(f64 s, const Vec3& v) noexcept { return v * s; }

[[nodiscard]] constexpr f64 dot(const Vec3& a, const Vec3& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

[[nodiscard]] constexpr f64 lengthSq(const Vec3& v) noexcept { return dot(v, v); }
[[nodiscard]] inline f64 length(const Vec3& v) noexcept { return std::sqrt(dot(v, v)); }

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
    f64 w{1.0};
    f64 x{};
    f64 y{};
    f64 z{};

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

    [[nodiscard]] constexpr bool operator==(const Quat&) const noexcept = default;
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
static_assert(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 0, 1});
static_assert(cross(Vec3{0, 1, 0}, Vec3{1, 0, 0}) == Vec3{0, 0, -1});
static_assert(nearlyEqual(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}), 32.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSq(Vec3{3, 4, 0}), 25.0, Tolerance{0.0}));
static_assert(Vec3{1, 2, 3} - Vec3{1, 2, 3} == Vec3{});
static_assert(-Vec3{1, -2, 3} == Vec3{-1, 2, -3});
static_assert(Vec3{1, 2, 3} * 2.0 == Vec3{2, 4, 6});
static_assert(Quat{}.conjugate() == Quat{});
static_assert(Quat{} * Quat{0, 1, 0, 0} == Quat{0, 1, 0, 0}, "identity is the unit");
static_assert(Quat{0, 1, 0, 0} * Quat{0, 1, 0, 0} == Quat{-1, 0, 0, 0}, "i * i = -1");

} // namespace orb