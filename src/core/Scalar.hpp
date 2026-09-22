#ifndef ORBSIM_CORE_SCALAR_HPP
#define ORBSIM_CORE_SCALAR_HPP
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
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace orb {

using f32 = float;
using f64 = double;

inline constexpr f64 kPi = std::numbers::pi_v<f64>;
inline constexpr f64 kTau = 2.0 * kPi;

// The bits of a double, for comparisons whose claim is bit identity -- a
// determinism check. +0.0 and -0.0 are equal as numbers and differ here; a NaN
// is never equal to itself as a number and is identical to its own bits.
[[nodiscard]] constexpr std::uint64_t bitsOf(f64 v) noexcept {
    return std::bit_cast<std::uint64_t>(v);
}

// The same for a 32-bit float, so that the one type this project keeps in 32
// bits -- `view/Camera.hpp`'s `Vec3f`, at the GPU boundary -- says bit
// identity with the same word as everything else does (M1-11).
//
// **An overload rather than a template**, so that the return type is the width
// of the argument. Written out because `bitsOf(someFloat)` would otherwise
// promote to `f64` and draw `-Wdouble-promotion`, which is an error here: a
// promotion is exact, so the comparison would still be right, and a warning
// that fires on correct code is a warning that teaches nothing.
[[nodiscard]] constexpr std::uint32_t bitsOf(f32 v) noexcept {
    return std::bit_cast<std::uint32_t>(v);
}

// The base for a strong scalar that is *not* a physical quantity: one f64, no
// implicit conversion in either direction, ordering, and arithmetic that keeps
// the type.
//
// Until 2026-09-17 this was the base for everything, the nine units included.
// Those moved to mp-units and are built on Unit<> in core/Units.hpp (ADR 0019),
// which gives them something this template structurally cannot: a dimension.
// `Metres / Seconds` now yields a velocity rather than failing to compile,
// because mp-units knows what the division of a length by a time *is*, and a
// per-type CRTP base cannot.
//
// What is left here is for scalars that take no part in dimensional analysis.
// Tolerance is the only one, and belongs here rather than in a dimensionless
// mp-units kind because it is a parameter of a comparison between two bare
// doubles, not a measurement of anything.
//
// Both bases present the same surface -- value(), ordering, no `==`,
// bitIdentical, arithmetic that keeps the type -- so code reading a scalar does
// not have to know which one it came from.
template <typename Derived> struct Quantity {
    // An accessor rather than a public member since 2026-09-17. It was a public
    // member, on the argument that `value` IS the interface of a unit type and
    // a getter would be the trivial accessor C.131 tells you not to write. That
    // argument was sound while the f64 lived here; it stopped being sound when
    // the physical types moved to mp-units, where the number lives in the base
    // class and is reached through a function. One spelling across both bases
    // is worth more than the argument was, and it takes a suppression of
    // misc-non-private-member-variables-in-classes with it.
    [[nodiscard]] constexpr f64 value() const noexcept { return value_; }

    // Ordering, and no `==`. The defaulted <=> gives <, >, <= and >=; `==` is
    // deleted, because on a double it is the comparison CODING_GUIDELINES
    // section 11 forbids and -Wfloat-equal reports (ADR 0017). Exact equality
    // is spelled out instead: nearlyEqual with a zero tolerance for a value,
    // bitIdentical below when bit identity is the claim.
    [[nodiscard]] constexpr auto operator<=>(const Quantity&) const noexcept = default;
    bool operator==(const Quantity&) const = delete;

    [[nodiscard]] constexpr bool bitIdentical(Derived other) const noexcept {
        return bitsOf(value_) == bitsOf(other.value_);
    }

    [[nodiscard]] constexpr Derived operator-() const noexcept { return Derived{-value_}; }
    [[nodiscard]] constexpr Derived operator+(Derived other) const noexcept {
        return Derived{value_ + other.value_};
    }
    [[nodiscard]] constexpr Derived operator-(Derived other) const noexcept {
        return Derived{value_ - other.value_};
    }
    [[nodiscard]] constexpr Derived operator*(f64 scale) const noexcept {
        return Derived{value_ * scale};
    }
    [[nodiscard]] constexpr Derived operator/(f64 scale) const noexcept {
        return Derived{value_ / scale};
    }
    [[nodiscard]] friend constexpr Derived operator*(f64 scale, Derived quantity) noexcept {
        return Derived{quantity.value_ * scale};
    }

    constexpr Derived& operator+=(Derived other) noexcept {
        value_ += other.value_;
        return derived();
    }
    constexpr Derived& operator-=(Derived other) noexcept {
        value_ -= other.value_;
        return derived();
    }

private:
    f64 value_{};

    // Only the named derived type may construct its base, which is what stops
    // `struct Other : Quantity<Radians>` from compiling by accident.
    friend Derived;
    constexpr Quantity() noexcept = default;
    explicit constexpr Quantity(f64 v) noexcept : value_(v) {}

    [[nodiscard]] constexpr Derived& derived() noexcept { return static_cast<Derived&>(*this); }
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
// identity is the actual claim being made, and it says so by name:
// bitIdentical() on Quantity, Vec3 and Quat.
//
// The two compared values are interchangeable -- |a-b| is commutative -- so
// transposing them cannot produce a wrong answer.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr bool nearlyEqual(f64 a, f64 b, Tolerance tolerance) noexcept {
    const f64 difference = a > b ? a - b : b - a;
    return difference <= tolerance.value();
}

// Finiteness in a constant expression, which <cmath>'s isfinite is not until
// C++26. NaN fails both comparisons and each infinity fails one of them, so
// this is exactly std::isfinite without leaving constant evaluation.
//
// **Reach for this, not std::isfinite, anywhere a constant expression might
// evaluate it.** It lived in core/DoubleDouble.hpp until 2026-09-14, which put
// it out of reach of core/Time.hpp -- and Time.hpp used std::isfinite inside a
// constexpr validator, which clang and libstdc++ both accept as an extension
// and MSVC does not. That cost nothing until MSVC was built for the first time,
// and then cost the constexpr-ness of kJ2000 and kUnixEpoch and the two
// static_asserts that read them: eight errors, one assumption. It belongs here,
// beside nearlyEqual, because it is a scalar predicate and not a double-double
// one.
[[nodiscard]] constexpr bool isFinite(f64 x) noexcept {
    return x >= -std::numeric_limits<f64>::max() && x <= std::numeric_limits<f64>::max();
}

static_assert(isFinite(0.0) && isFinite(-1e308) && isFinite(std::numeric_limits<f64>::max()) &&
                  isFinite(std::numeric_limits<f64>::denorm_min()),
              "every finite double is finite, including the extremes");
static_assert(!isFinite(std::numeric_limits<f64>::infinity()) &&
                  !isFinite(-std::numeric_limits<f64>::infinity()),
              "an infinity is not finite");

// **Nothing about a NaN is asserted at compile time here, deliberately.** MSVC's
// constant evaluator disagrees with its own runtime about NaN comparisons:
// measured 2026-09-17, `NaN <= max` is *true* during constant evaluation and
// false at run time, where clang and gcc say false in both. So a NaN claim in a
// static_assert is a claim about the evaluator rather than about the value. The
// NaN behaviour of isFinite, isNaN and absOf is tested at run time instead --
// tests/test_double_double.cpp, "the scalar predicates agree about NaN" -- and
// PROJECT_STATE section 8 carries the measurement.

// NaN in a constant expression, and without `==`. The usual spelling is
// `x != x`, which -Wfloat-equal reports and this codebase does not allow; a NaN
// is instead the only value that fails *both* of isFinite's comparisons, where
// an infinity fails exactly one. Added 2026-09-17 so that elementsAreUsable in
// orbit/Orbit.cpp can be constexpr: it has to tell a NaN semi-major axis from an
// infinite one, because a parabola's is legitimately infinite.
[[nodiscard]] constexpr bool isNaN(f64 x) noexcept {
    return !(x >= -std::numeric_limits<f64>::max()) && !(x <= std::numeric_limits<f64>::max());
}

static_assert(!isNaN(0.0) && !isNaN(-1e308) && !isNaN(std::numeric_limits<f64>::max()) &&
                  !isNaN(std::numeric_limits<f64>::infinity()) &&
                  !isNaN(-std::numeric_limits<f64>::infinity()),
              "nothing that is not a NaN is one, an infinity included");

// Magnitude in a constant expression; <cmath>'s fabs is not one before C++26
// either.
//
// By clearing the sign bit rather than testing `x < 0.0`, which was the first
// attempt and was wrong: **-0.0 < 0.0 is false**, so the comparison form hands
// -0.0 straight back instead of +0.0. The static_assert below caught it, which
// is the argument for writing the assert before believing the function.
// Clearing the bit also leaves a NaN a NaN and an infinity infinite, with no
// branch at all.
[[nodiscard]] constexpr f64 absOf(f64 x) noexcept {
    constexpr std::uint64_t kSignBit = 0x8000'0000'0000'0000ULL;
    return std::bit_cast<f64>(bitsOf(x) & ~kSignBit);
}

static_assert(nearlyEqual(absOf(-3.5), 3.5, Tolerance{0.0}) &&
                  nearlyEqual(absOf(3.5), 3.5, Tolerance{0.0}) &&
                  nearlyEqual(absOf(-0.0), 0.0, Tolerance{0.0}) &&
                  bitsOf(absOf(-0.0)) == bitsOf(0.0),
              "magnitude, and -0.0 comes back as +0.0");
static_assert(!isFinite(absOf(-std::numeric_limits<f64>::infinity())),
              "an infinity stays infinite");

// Angle wrapping on bare doubles. These exist because `Radians` is defined a
// header later, in core/Units.hpp, which is where the typed overloads live and
// where they belong -- these are the implementation underneath them and are the
// one place the unwrapping happens.
//
// The comment here used to say they were "for use inside the orbital arithmetic
// where unwrapping to f64 once at the top of a function is clearer than
// wrapping every intermediate". That was a rationale nothing exercised, checked
// on 2026-09-13: every call site in `src/orbit/` and `tests/` takes the
// `Radians` overload, and the only callers of these two are `wrapPi` itself and
// the pair in Units.hpp. Prefer the typed overload; reach for these only if you
// are writing the typed one.

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
// Exact results, compared the one way this codebase compares doubles: through
// nearlyEqual, here with a zero tolerance.
static_assert(nearlyEqual((Tolerance{1.0} + Tolerance{2.0}).value(), 3.0, Tolerance{0.0}));
static_assert(nearlyEqual((Tolerance{3.0} - Tolerance{2.0}).value(), 1.0, Tolerance{0.0}));
static_assert(nearlyEqual((-Tolerance{1.0}).value(), -1.0, Tolerance{0.0}));
static_assert(nearlyEqual((Tolerance{2.0} * 3.0).value(), 6.0, Tolerance{0.0}));
static_assert(nearlyEqual((3.0 * Tolerance{2.0}).value(), 6.0, Tolerance{0.0}));
static_assert(nearlyEqual((Tolerance{6.0} / 3.0).value(), 2.0, Tolerance{0.0}));
static_assert(Tolerance{1.0} < Tolerance{2.0});
static_assert(nearlyEqual(1.0, 1.0 + 1e-16, Tolerance{1e-15}));
static_assert(!nearlyEqual(1.0, 1.1, Tolerance{1e-15}));
static_assert(!std::equality_comparable<Tolerance>, "exact equality of a double is spelled out");
static_assert(Tolerance{0.0}.bitIdentical(Tolerance{0.0}) &&
                  !Tolerance{0.0}.bitIdentical(Tolerance{-0.0}),
              "bit identity tells the two zeros apart, as a determinism check needs");

} // namespace orb

#endif // ORBSIM_CORE_SCALAR_HPP
