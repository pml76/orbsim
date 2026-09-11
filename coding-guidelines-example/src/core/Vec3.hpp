#ifndef ORBEX_CORE_VEC3_HPP
#define ORBEX_CORE_VEC3_HPP
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
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <numbers>

namespace orbex {

using f32 = float;
using f64 = double;

// [S8] constexpr, not #define. It has a type and it obeys scope; the
// preprocessor gives you neither.
inline constexpr f64 kPi = std::numbers::pi_v<f64>;
inline constexpr f64 kTau = 2.0 * kPi;

// [S19] The bits of a double, for the one comparison whose claim is bit
// identity: a determinism check. +0.0 and -0.0 are equal as numbers and differ
// here, and a NaN, never equal to itself as a number, is identical to its own
// bits -- which is why `==` is not bit identity, and why this exists.
[[nodiscard]] constexpr std::uint64_t bitsOf(f64 v) noexcept {
    return std::bit_cast<std::uint64_t>(v);
}

// [S2] A strong type for the third argument of nearlyEqual. Without it the
// signature is (f64, f64, f64) and `nearlyEqual(a, tolerance, b)` compiles
// silently -- exactly the transposition I.24 is about.
struct Tolerance {
    f64 value{};

    constexpr Tolerance() noexcept = default;
    explicit constexpr Tolerance(f64 v) noexcept : value(v) {}

    // [S11] Ordering, and no `==`: a defaulted <=> brings a defaulted `==`
    // with it, and on a double that is the comparison section 11 forbids and
    // -Wfloat-equal reports. Every strong type in the example does the same.
    [[nodiscard]] constexpr auto operator<=>(const Tolerance&) const noexcept = default;
    bool operator==(const Tolerance&) const = delete;
};

// [S11] The only float comparison this example permits. `==` on doubles is
// wrong at runtime and equally wrong inside a static_assert, because it is the
// same arithmetic either way. An exact result is compared with a zero
// tolerance; a determinism check, where bit identity is the claim, says so by
// name, with bitIdentical().
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

    // [S6] Designated initialisers even here, where the order is obvious: the
    // habit is what keeps `{y, x, z}` from compiling somewhere it is not.
    [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const noexcept {
        return {.x = x + other.x, .y = y + other.y, .z = z + other.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept {
        return {.x = x - other.x, .y = y - other.y, .z = z - other.z};
    }

    [[nodiscard]] constexpr Vec3 operator*(f64 scale) const noexcept {
        return {.x = x * scale, .y = y * scale, .z = z * scale};
    }

    // [S19] Bit identity, which is precisely what a determinism test needs --
    // and which a defaulted `==` is not: it says +0.0 equals -0.0, and a NaN
    // equals nothing. It is deliberately not an approximate comparison either;
    // nearlyEqual is for that, and confusing the two is how a determinism test
    // stops testing. [S11] So Vec3 has no `==` at all.
    [[nodiscard]] constexpr bool bitIdentical(const Vec3& other) const noexcept {
        return bitsOf(x) == bitsOf(other.x) && bitsOf(y) == bitsOf(other.y) &&
               bitsOf(z) == bitsOf(other.z);
    }
};

[[nodiscard]] constexpr f64 dot(const Vec3& a, const Vec3& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {
        .x = (a.y * b.z) - (a.z * b.y),
        .y = (a.z * b.x) - (a.x * b.z),
        .z = (a.x * b.y) - (a.y * b.x),
    };
}

[[nodiscard]] constexpr f64 lengthSquared(const Vec3& v) noexcept { return dot(v, v); }

// [S3] Not constexpr, because std::sqrt only becomes constexpr in C++26.
// Everything above it is, which is the point of the rule: mark what can be, and
// let the caller decide when to evaluate it.
[[nodiscard]] inline f64 length(const Vec3& v) noexcept { return std::sqrt(lengthSquared(v)); }

// [S3] A static_assert is a unit test that costs nothing at runtime, runs on
// every build whether or not anyone invokes the test suite, and cannot rot.
inline constexpr Vec3 kUnitX{.x = 1.0, .y = 0.0, .z = 0.0};
inline constexpr Vec3 kUnitY{.x = 0.0, .y = 1.0, .z = 0.0};
inline constexpr Vec3 kUnitZ{.x = 0.0, .y = 0.0, .z = 1.0};
inline constexpr Vec3 kOneTwoThree{.x = 1.0, .y = 2.0, .z = 3.0};

// Every result below is exact, so the tolerance is zero. A vector is compared
// through the squared length of the difference, which is zero only when every
// component is: the components are small integers, so no nonzero difference
// can square to an underflowed zero.
static_assert(nearlyEqual(lengthSquared(cross(kUnitX, kUnitY) - kUnitZ), 0.0, Tolerance{0.0}));
static_assert(nearlyEqual(dot(kOneTwoThree, Vec3{.x = 4.0, .y = 5.0, .z = 6.0}),
                          32.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared(Vec3{.x = 3.0, .y = 4.0, .z = 0.0}), 25.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared(kOneTwoThree - Vec3{.x = 1.0, .y = 2.0, .z = 3.0}),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared((kOneTwoThree * 2.0) - Vec3{.x = 2.0, .y = 4.0, .z = 6.0}),
                          0.0,
                          Tolerance{0.0}));

// [S19] bitIdentical compares every component, and tells the two zeros apart.
static_assert(kOneTwoThree.bitIdentical(Vec3{.x = 1.0, .y = 2.0, .z = 3.0}));
static_assert(!Vec3{}.bitIdentical(Vec3{.x = -0.0, .y = 0.0, .z = 0.0}) &&
              !Vec3{}.bitIdentical(Vec3{.x = 0.0, .y = -0.0, .z = 0.0}) &&
              !Vec3{}.bitIdentical(Vec3{.x = 0.0, .y = 0.0, .z = -0.0}));
static_assert(!std::equality_comparable<Tolerance> && !std::equality_comparable<Vec3>,
              "exact equality of a double is spelled out");

} // namespace orbex

#endif // ORBEX_CORE_VEC3_HPP
