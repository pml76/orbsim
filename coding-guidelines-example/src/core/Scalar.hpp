#ifndef ORBEX_CORE_SCALAR_HPP
#define ORBEX_CORE_SCALAR_HPP
//
// Scalar foundations: the floating-point aliases, the circle constants, the
// one float comparison this example permits, and the strong type its tolerance
// argument uses.
//
// [S14] Self-contained: this header includes everything it needs and compiles
// on its own. The build proves that mechanically rather than trusting it --
// see the orbex_header_selfcheck target in CMakeLists.txt.
//
// This is the bottom of the include order -- Scalar -> Units -> Vec3 -- and it
// depends on nothing but the standard library. It was split out of Vec3.hpp on
// 2026-09-17, when Units.hpp grew a dependency on mp-units and Vec3 grew one on
// Units: the old order had Units.hpp including Vec3.hpp, and units on a vector
// need it the other way round.
//
#include <bit>
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
//
// It is *not* one of the mp-units quantities in Units.hpp, deliberately. A
// tolerance is a parameter of a comparison between two bare doubles, not a
// measurement of anything, so it has no dimension to analyse and nothing to
// gain from a units library. Knowing which of your strong types are physical
// quantities and which are not is part of the exercise.
struct Tolerance {
    // [S11] An accessor, so that every strong scalar in the example is read
    // the same way -- `t.value()`. The mp-units quantities in Units.hpp keep
    // their number in a base class and cannot expose it as a member, and one
    // spelling across all of them is worth more than the one saved call.
    [[nodiscard]] constexpr f64 value() const noexcept { return value_; }

    constexpr Tolerance() noexcept = default;
    explicit constexpr Tolerance(f64 v) noexcept : value_(v) {}

    // [S11] Ordering, and no `==`: a defaulted <=> brings a defaulted `==`
    // with it, and on a double that is the comparison section 11 forbids and
    // -Wfloat-equal reports. Every strong type in the example does the same.
    [[nodiscard]] constexpr auto operator<=>(const Tolerance&) const noexcept = default;
    bool operator==(const Tolerance&) const = delete;

private:
    f64 value_{};
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
    return difference <= tolerance.value();
}

static_assert(!std::equality_comparable<Tolerance>, "exact equality of a double is spelled out");
static_assert(nearlyEqual(1.0, 1.0 + 1e-16, Tolerance{1e-15}));
static_assert(!nearlyEqual(1.0, 1.1, Tolerance{1e-15}));

} // namespace orbex

#endif // ORBEX_CORE_SCALAR_HPP
