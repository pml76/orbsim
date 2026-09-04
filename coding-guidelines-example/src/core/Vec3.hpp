#pragma once
//
// A double-precision 3-vector, and the one float comparison this example
// permits.
//
// [S11] f64 throughout. The simulation never sees an f32; the single place
// where narrowing happens is gfx::toCameraRelative, which holds every
// static_cast<f32> in the example.
//
// [S14] Self-contained: this header includes everything it needs and compiles
// on its own. The build proves that mechanically rather than trusting it --
// see the orbex_header_selfcheck target in CMakeLists.txt.
//
#include <cmath>
#include <compare>
#include <numbers>

namespace orbex {

using f32 = float;
using f64 = double;

// [S8] constexpr, not #define. It has a type and it obeys scope; the
// preprocessor gives you neither.
inline constexpr f64 kPi = std::numbers::pi_v<f64>;
inline constexpr f64 kTau = 2.0 * kPi;

// [S2] A strong type for the third argument of nearlyEqual. Without it the
// signature is (f64, f64, f64) and `nearlyEqual(a, tolerance, b)` compiles
// silently -- exactly the transposition I.24 is about.
struct Tolerance {
    f64 value{};

    constexpr Tolerance() noexcept = default;
    explicit constexpr Tolerance(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Tolerance&) const noexcept = default;
};

// [S11] The only float comparison this example permits. `==` on doubles is
// wrong at runtime and equally wrong inside a static_assert, because it is the
// same arithmetic either way.
//
// The remaining two parameters are genuinely interchangeable -- |a-b| is
// commutative -- so transposing them cannot produce a wrong answer.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr bool nearlyEqual(f64 a, f64 b, Tolerance tolerance) noexcept {
    const f64 difference = a > b ? a - b : b - a;
    return difference <= tolerance.value;
}

// [S4] Rule of Zero: no destructor, no copy, no assignment, no move. The
// compiler writes all of them and cannot get them wrong.
// [S6] Default member initializers apply to every constructor at once,
// including the one somebody adds next year, so a Vec3 cannot be created
// uninitialized.
struct Vec3 {
    f64 x{};
    f64 y{};
    f64 z{};

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const noexcept {
        return {x + other.x, y + other.y, z + other.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }

    [[nodiscard]] constexpr Vec3 operator*(f64 scale) const noexcept {
        return {x * scale, y * scale, z * scale};
    }

    // [S19] Bit-exact equality, which is precisely what a determinism test
    // needs. It is deliberately not an approximate comparison; nearlyEqual is
    // for that, and confusing the two is how a determinism test stops testing.
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

[[nodiscard]] constexpr f64 dot(const Vec3& a, const Vec3& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

[[nodiscard]] constexpr f64 lengthSquared(const Vec3& v) noexcept { return dot(v, v); }

// [S3] Not constexpr, because std::sqrt only becomes constexpr in C++26.
// Everything above it is, which is the point of the rule: mark what can be, and
// let the caller decide when to evaluate it.
[[nodiscard]] inline f64 length(const Vec3& v) noexcept { return std::sqrt(lengthSquared(v)); }

// [S3] A static_assert is a unit test that costs nothing at runtime, runs on
// every build whether or not anyone invokes the test suite, and cannot rot.
static_assert(cross(Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}) == Vec3{0.0, 0.0, 1.0});
static_assert(nearlyEqual(dot(Vec3{1.0, 2.0, 3.0}, Vec3{4.0, 5.0, 6.0}), 32.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared(Vec3{3.0, 4.0, 0.0}), 25.0, Tolerance{0.0}));
static_assert(Vec3{1.0, 2.0, 3.0} - Vec3{1.0, 2.0, 3.0} == Vec3{});
static_assert(Vec3{1.0, 2.0, 3.0} * 2.0 == Vec3{2.0, 4.0, 6.0});

} // namespace orbex
