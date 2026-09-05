#pragma once
//
// Scalar foundations for the simulation core: the floating-point aliases, the
// circle constants, and the base every strong scalar type is built on.
//
// This is the bottom of the core's include order -- Scalar -> Units -> Math ->
// Orbit -- and it depends on nothing but the standard library. It exists as a
// separate header so that Units.hpp can define Radians and Seconds *before*
// Math.hpp needs them for rotations and integration; when the strong types
// lived above the vector maths, every rotation took a bare f64 angle.
//
#include <cmath>
#include <compare>
#include <numbers>
#include <type_traits>

namespace orb {

using f32 = float;
using f64 = double;

inline constexpr f64 kPi = std::numbers::pi_v<f64>;
inline constexpr f64 kTau = 2.0 * kPi;

// The base for every strong scalar type: one f64, no implicit conversion in
// either direction, comparison and unit-preserving arithmetic.
//
// Each concrete type is a struct deriving from this (CRTP), so that Radians
// and Degrees are distinct types the compiler can tell apart, while the code
// that makes them behave like numbers is written once. At -O2 the whole thing
// disappears: `Radians{a} + Radians{b}` is one addsd.
//
// Arithmetic that keeps the unit lives here: sum and difference of the same
// type, scaling by a plain number, negation. Arithmetic that *changes* the
// unit -- Metres divided by Seconds -- is deliberately absent. Writing that as
// a named function with a typed result is the whole point of the exercise;
// a generic `operator/` returning f64 would quietly reopen the hole.
template <typename Derived> struct Quantity {
    f64 value{};

    [[nodiscard]] constexpr auto operator<=>(const Quantity&) const noexcept = default;

    [[nodiscard]] constexpr Derived operator-() const noexcept { return Derived{-value}; }
    [[nodiscard]] constexpr Derived operator+(Derived other) const noexcept {
        return Derived{value + other.value};
    }
    [[nodiscard]] constexpr Derived operator-(Derived other) const noexcept {
        return Derived{value - other.value};
    }
    [[nodiscard]] constexpr Derived operator*(f64 scale) const noexcept {
        return Derived{value * scale};
    }
    [[nodiscard]] constexpr Derived operator/(f64 scale) const noexcept {
        return Derived{value / scale};
    }
    [[nodiscard]] friend constexpr Derived operator*(f64 scale, Derived quantity) noexcept {
        return Derived{quantity.value * scale};
    }

    constexpr Derived& operator+=(Derived other) noexcept {
        value += other.value;
        return derived();
    }
    constexpr Derived& operator-=(Derived other) noexcept {
        value -= other.value;
        return derived();
    }

private:
    // Only the named derived type may construct its base, which is what stops
    // `struct Other : Quantity<Radians>` from compiling by accident.
    friend Derived;
    constexpr Quantity() noexcept = default;
    explicit constexpr Quantity(f64 v) noexcept : value(v) {}

    constexpr Derived& derived() noexcept { return static_cast<Derived&>(*this); }
};

// A tolerance is its own type so that `nearlyEqual(a, tolerance, b)` cannot
// compile. Without it the signature is (f64, f64, f64) and the third argument
// transposes with the second in silence -- I.24 exactly.
struct Tolerance : Quantity<Tolerance> {
    constexpr Tolerance() noexcept = default;
    explicit constexpr Tolerance(f64 v) noexcept : Quantity{v} {}
};

// The only float comparison this codebase permits. `==` on doubles is wrong at
// runtime and equally wrong inside a static_assert, because it is the same
// arithmetic either way. The exception is a determinism check, where bit
// identity is the actual claim being made.
//
// The two compared values are interchangeable -- |a-b| is commutative -- so
// transposing them cannot produce a wrong answer.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr bool nearlyEqual(f64 a, f64 b, Tolerance tolerance) noexcept {
    const f64 difference = a > b ? a - b : b - a;
    return difference <= tolerance.value;
}

// Angle wrapping on bare doubles, for use inside the orbital arithmetic where
// unwrapping to f64 once at the top of a function is clearer than wrapping
// every intermediate. The strong-typed overloads are in core/Units.hpp.

// Wrap an angle into [0, tau).
[[nodiscard]] inline f64 wrapTau(f64 a) noexcept {
    a = std::fmod(a, kTau);
    return a < 0.0 ? a + kTau : a;
}

// Wrap an angle into (-pi, pi].
[[nodiscard]] inline f64 wrapPi(f64 a) noexcept {
    a = wrapTau(a);
    return a > kPi ? a - kTau : a;
}

// Compile-time proofs of the properties the rest of the codebase relies on.
static_assert(sizeof(Tolerance) == sizeof(f64), "a strong type must cost nothing");
static_assert(std::is_trivially_copyable_v<Tolerance>);
static_assert(!std::is_convertible_v<f64, Tolerance>, "construction must be explicit");
static_assert(!std::is_convertible_v<Tolerance, f64>, "no silent way back to a bare double");
static_assert(Tolerance{1.0} + Tolerance{2.0} == Tolerance{3.0});
static_assert(Tolerance{3.0} - Tolerance{2.0} == Tolerance{1.0});
static_assert(-Tolerance{1.0} == Tolerance{-1.0});
static_assert(Tolerance{2.0} * 3.0 == Tolerance{6.0});
static_assert(3.0 * Tolerance{2.0} == Tolerance{6.0});
static_assert(Tolerance{6.0} / 3.0 == Tolerance{2.0});
static_assert(Tolerance{1.0} < Tolerance{2.0});
static_assert(nearlyEqual(1.0, 1.0 + 1e-16, Tolerance{1e-15}));
static_assert(!nearlyEqual(1.0, 1.1, Tolerance{1e-15}));

} // namespace orb