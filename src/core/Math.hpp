#pragma once
//
// Double-precision math for the simulation core.
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
#include <cmath>
#include <numbers>

namespace orb {

using f32 = float;
using f64 = double;

inline constexpr f64 kPi  = std::numbers::pi_v<f64>;
inline constexpr f64 kTau = 2.0 * kPi;

inline constexpr f64 deg(f64 radians) { return radians * (180.0 / kPi); }
inline constexpr f64 rad(f64 degrees) { return degrees * (kPi / 180.0); }

// Wrap an angle into [0, tau).
inline f64 wrapTau(f64 a) {
    a = std::fmod(a, kTau);
    return a < 0.0 ? a + kTau : a;
}

// Wrap an angle into (-pi, pi].
inline f64 wrapPi(f64 a) {
    a = wrapTau(a);
    return a > kPi ? a - kTau : a;
}

// ---------------------------------------------------------------- Vec3 -----

struct Vec3 {
    f64 x{}, y{}, z{};

    constexpr Vec3() = default;
    constexpr Vec3(f64 x_, f64 y_, f64 z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    constexpr Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    constexpr Vec3 operator*(f64 s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(f64 s) const { return {x / s, y / s, z / s}; }

    constexpr Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    constexpr Vec3& operator*=(f64 s) { x *= s; y *= s; z *= s; return *this; }
    constexpr Vec3& operator/=(f64 s) { x /= s; y /= s; z /= s; return *this; }

    constexpr bool operator==(const Vec3&) const = default;
};

constexpr Vec3 operator*(f64 s, const Vec3& v) { return v * s; }

constexpr f64 dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

constexpr f64 lengthSq(const Vec3& v) { return dot(v, v); }
inline f64 length(const Vec3& v) { return std::sqrt(dot(v, v)); }

// Returns the zero vector for a zero-length input rather than NaN; callers in
// the orbital code rely on this to stay well-behaved in degenerate orbits.
inline Vec3 normalize(const Vec3& v) {
    const f64 len = length(v);
    return len > 0.0 ? v / len : Vec3{};
}

inline f64 distance(const Vec3& a, const Vec3& b) { return length(a - b); }

// Angle between two vectors, robust near 0 and pi where acos(dot) loses
// precision catastrophically.
inline f64 angleBetween(const Vec3& a, const Vec3& b) {
    const Vec3 na = normalize(a);
    const Vec3 nb = normalize(b);
    return 2.0 * std::atan2(length(na - nb), length(na + nb));
}

// Rotate v about a unit axis by `angle` radians (Rodrigues' formula).
inline Vec3 rotateAxis(const Vec3& v, const Vec3& axis, f64 angle) {
    const f64 c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}

// ---------------------------------------------------------------- Quat -----

// Unit quaternion, w + xi + yj + zk, representing a body->world rotation.
struct Quat {
    f64 w{1.0}, x{}, y{}, z{};

    constexpr Quat() = default;
    constexpr Quat(f64 w_, f64 x_, f64 y_, f64 z_) : w(w_), x(x_), y(y_), z(z_) {}

    static Quat fromAxisAngle(const Vec3& axis, f64 angle) {
        const Vec3 a = normalize(axis);
        const f64 h = angle * 0.5, s = std::sin(h);
        return {std::cos(h), a.x * s, a.y * s, a.z * s};
    }

    constexpr Quat conjugate() const { return {w, -x, -y, -z}; }

    constexpr Quat operator*(const Quat& q) const {
        return {w * q.w - x * q.x - y * q.y - z * q.z,
                w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w};
    }

    // Rotate a vector from body space into world space.
    Vec3 rotate(const Vec3& v) const {
        const Vec3 u{x, y, z};
        const Vec3 t = cross(u, v) * 2.0;
        return v + t * w + cross(u, t);
    }

    Vec3 inverseRotate(const Vec3& v) const { return conjugate().rotate(v); }
};

inline Quat normalize(const Quat& q) {
    const f64 n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return n > 0.0 ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{};
}

// Integrate an orientation by an angular-velocity vector (world frame) over dt.
// Uses the exact exponential map rather than the linearised q += 0.5*w*q*dt, so
// it stays a unit quaternion under large time steps.
inline Quat integrateAngularVelocity(const Quat& q, const Vec3& omega, f64 dt) {
    const f64 theta = length(omega) * dt;
    if (theta < 1e-12) return q;
    return normalize(Quat::fromAxisAngle(omega, theta) * q);
}

} // namespace orb
