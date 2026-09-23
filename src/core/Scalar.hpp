#ifndef ORBSIM_CORE_SCALAR_HPP
#define ORBSIM_CORE_SCALAR_HPP
//
// Scalar foundations for the simulation core: the floating-point aliases, the
// circle constants, and the base every strong scalar type is built on.
//
// This is the bottom of the core's include order -- Scalar -> Units -> Math ->
// Orbit -- and it depends on nothing but the standard library and
// core/Contract.hpp, which is itself only <cassert> and two macros. It exists
// as a separate header so that Units.hpp can define Radians and Seconds
// *before* Math.hpp needs them for rotations and integration; when the strong
// types lived above the vector maths, every rotation took a bare f64 angle.
//
// *(The sentence above read "nothing but the standard library" until
// 2026-09-23, which was true until Count<> below needed a precondition. The
// dependency is one header deep and acyclic -- Contract.hpp includes nothing
// of ours -- but the claim had to be corrected rather than quietly outgrown.)*
//
#include "core/Contract.hpp"

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
// Both bases present the same surface -- value(), ordering, arithmetic that
// keeps the type -- so code reading a scalar does not have to know which one it
// came from. Count<> below is the third, and it differs in the places where
// being integral genuinely changes the answer: it has `==` and no
// bitIdentical, because for a whole number those are the same claim. Each
// difference is written out beside it.
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

// --- counts: the integral sibling of Quantity (M1-12) -----------------------
//
// Register decision 19 and ADR 0001's update of 2026-09-08. A texel count, a
// mip level and a cache budget in mebibytes are **counts**: whole numbers with
// no meaningful fractional part. An f64 base invites a division that truncates
// without saying so, or a texture 2047.9999 texels wide. So they get a base of
// their own.

// The largest number a count can hold. Named once, because it appears both in
// the guards below and in the proofs of them, and a limit written twice is one
// fact with two chances to rot.
inline constexpr std::uint32_t kCountMaximum = std::numeric_limits<std::uint32_t>::max();

// And it really is the last value the type holds, checked against the
// language's own wrapping rule rather than against the definition one line
// above -- which would be the code agreeing with itself. Written because
// preparing the mutation pass found that nothing else here would notice a
// limit that was one too small: every guard and every proof of a guard reads
// this constant, so they all move together and all keep agreeing.
static_assert(kCountMaximum + 1U == 0U, "one past the limit is where an unsigned count wraps");

// Half of what a count's arithmetic does instead of wrapping round: the half
// that speaks to the compiler. The other half is the ORBSIM_EXPECTS beside
// each call, which speaks to a Debug run.
//
// **Deliberately not constexpr, and that is the entire mechanism.** An
// expression that reaches a call to a function which is not constexpr is not a
// constant expression, so `Texels{1} - Texels{2}` fails the build and names
// this function -- in **every** tree, because that is a rule of the language
// rather than a property of NDEBUG.
//
// ORBSIM_EXPECTS on its own would not do it, and the difference was measured
// rather than assumed on 2026-09-23. assert expands to nothing under NDEBUG,
// and the underflow then becomes a perfectly good constant expression worth
// 4,294,967,295: the Debug tree refused it and the RelWithDebInfo tree did
// not. A guard that quietly stops guarding in the shipped configuration looks
// exactly like one that works, which is VERIFICATION.md rule 23.
//
// **It costs nothing.** The body is empty, so it inlines away, the branch dies
// with it, and a guarded subtraction emits mov, sub, ret -- the three
// instructions an unguarded one emits, compared side by side at -O2 -DNDEBUG.
// Both claims were confirmed on all three front ends in both configurations --
// clang 23.1, gcc-14 14.3.0 and MSVC 14.51 -- with a positive control, since
// "not a constant expression" and "the proof is broken" are otherwise the
// same answer.
//
// **Empty rather than holding the assertion itself, and that was not the first
// attempt.** It was `{ ORBSIM_EXPECTS(false); }`, which is tidier and which
// three Windows trees accepted. The `linux-sanitize` preset rejected it the
// same day: once assertions are live, a function whose only statement is
// `assert(false)` never returns -- glibc marks the failure path, so clang can
// see it -- and -Wmissing-noreturn says so. **The attribute would have been a
// lie**, because under NDEBUG the body is empty and the function does return,
// and calling a [[noreturn]] function that returns is undefined behaviour.
// MSVC's assert does not mark its failure path, which is why the Windows
// builds were silent. That is VERIFICATION.md rule 20 in one episode, and the
// fix is better than what it replaced: the assertion now sits where the values
// are and names the condition rather than the constant `false`.
//
// **What none of it catches** is a bad value arriving at run time in a release
// build. In Debug the assertions catch it; when counts start coming out of
// configuration files, reporting them is fromConfig's job (ADR 0007).
inline void stopConstantEvaluation() noexcept {}

// The base for a count: one std::uint32_t, no dimension, no fractional part.
// The same CRTP shape as Quantity above and the same `friend Derived` trick, so
// `struct Other : Count<Texels>` does not compile.
//
// **Where the surface differs from Quantity's**, each difference being the type
// being whole rather than fractional:
//
//   * **`==` is provided rather than deleted.** On a double, exact equality is
//     what CODING_GUIDELINES section 11 forbids and -Wfloat-equal reports; on a
//     whole number it is simply the comparison, with nothing to approximate.
//     bitIdentical has no counterpart here for the same reason -- for these
//     values `==` already *is* bit identity, so a second spelling would be two
//     names for one claim.
//   * **There is no division at all**, by another count or by a plain number.
//     Either truncates in silence, which is the defect the integral base exists
//     to prevent. A ratio of two counts is a named function returning f64, and
//     it will be written when something wants one.
//   * **The scale of a multiplication is exactly a std::uint32_t**, so a call
//     site writes `* 2U`. `* 2` does not compile, because an int would be a
//     signed-to-unsigned conversion that -Wsign-conversion reports and ADR 0017
//     makes an error; `* 1.5` does not compile either, which is the point --
//     the implicit f64-to-unsigned narrowing would have given 1.
//   * **Nothing wraps round.** See stopConstantEvaluation above.
template <typename Derived> struct Count {
    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }

    // The defaulted <=> gives <, >, <= and >=, and brings a defaulted == with
    // it. See the note above for why this base has one where Quantity does not.
    [[nodiscard]] constexpr auto operator<=>(const Count&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Count&) const noexcept = default;

    // Each of the three reads the same way, and the two lines after the
    // condition are the two halves of one precondition: the assertion is for a
    // value that arrives at run time in a Debug build, and the call is what
    // refuses the expression while the compiler is evaluating it. See
    // stopConstantEvaluation above for why neither does the other's job.
    [[nodiscard]] constexpr Derived operator+(Derived other) const noexcept {
        const bool wouldWrap = other.value_ > kCountMaximum - value_;
        ORBSIM_EXPECTS(!wouldWrap);
        if (wouldWrap) stopConstantEvaluation();
        return Derived{value_ + other.value_};
    }
    [[nodiscard]] constexpr Derived operator-(Derived other) const noexcept {
        const bool wouldWrap = other.value_ > value_;
        ORBSIM_EXPECTS(!wouldWrap);
        if (wouldWrap) stopConstantEvaluation();
        return Derived{value_ - other.value_};
    }

    // The test is written so that it cannot itself overflow: kCountMaximum
    // divided by the scale is the largest value that still fits, so the product
    // is checked without ever forming it. A scale of zero is separated out
    // because dividing by it is undefined behaviour, and because zero is the
    // one scale that can never overflow.
    //
    // Constrained to std::uint32_t exactly rather than taking one: a parameter
    // of that type would accept an f64 or an int through an implicit
    // conversion, and `Texels{2} * 1.5` would compile and mean `* 1`.
    template <std::same_as<std::uint32_t> Scale>
    [[nodiscard]] constexpr Derived operator*(Scale scale) const noexcept {
        const bool wouldWrap = scale != 0U && value_ > kCountMaximum / scale;
        ORBSIM_EXPECTS(!wouldWrap);
        if (wouldWrap) stopConstantEvaluation();
        return Derived{value_ * scale};
    }

    // The mirrored spelling, so `4U * Texels{16}` reads as it should. Routed
    // through the member, so the guard lives in one place.
    template <std::same_as<std::uint32_t> Scale>
    [[nodiscard]] friend constexpr Derived operator*(Scale scale, Derived count) noexcept {
        return count * scale;
    }

    constexpr Derived& operator+=(Derived other) noexcept {
        derived() = derived() + other;
        return derived();
    }
    constexpr Derived& operator-=(Derived other) noexcept {
        derived() = derived() - other;
        return derived();
    }

private:
    std::uint32_t value_{};

    // Only the named derived type may construct its base, which is what stops
    // `struct Other : Count<Texels>` from compiling by accident.
    friend Derived;
    constexpr Count() noexcept = default;
    explicit constexpr Count(std::uint32_t v) noexcept : value_(v) {}

    [[nodiscard]] constexpr Derived& derived() noexcept { return static_cast<Derived&>(*this); }
};

// A number of texels -- the elements of a texture. An edge length or a total;
// two of them deliberately do not multiply, so an edge length cannot become an
// area without somebody writing that conversion down and naming its unit.
// First fields: the atmosphere's lookup tables in M1-46.
struct Texels : Count<Texels> {
    constexpr Texels() noexcept = default;
    explicit constexpr Texels(std::uint32_t v) noexcept : Count{v} {}
};

// A memory budget in units of 2^20 bytes -- mebibytes rather than megabytes,
// because a graphics allocation is a power of two and 10^6 bytes is a different
// number. Thirty-two bits reach 4,194,303 MiB, which is four tebibytes. First
// field: the tile cache's budget in M1-34 and M1-59.
struct Mebibytes : Count<Mebibytes> {
    constexpr Mebibytes() noexcept = default;
    explicit constexpr Mebibytes(std::uint32_t v) noexcept : Count{v} {}
};

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

// --- and the same for the counts (M1-12) ------------------------------------
//
// Almost everything Count buys is one of these, which is why
// tests/test_render_quality.cpp holds so little: a refusal cannot be asserted
// at run time, and an assertion here runs on every build whether or not anyone
// invokes the suite. Each negative claim below was written inverted once and
// watched failing before it was believed.

// Concepts rather than bare requires-expressions, for the reason core/Units.hpp
// gives beside `addable`: a requires-expression on non-dependent operands is
// diagnosed rather than evaluated.
template <typename A, typename B>
concept divisible = requires(const A& x, const B& y) { x / y; };
template <typename A, typename B>
concept multipliable = requires(const A& x, const B& y) { x * y; };

static_assert(sizeof(Texels) == sizeof(std::uint32_t), "a strong type must cost nothing");
static_assert(sizeof(Mebibytes) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<Texels>);
static_assert(std::is_trivially_copyable_v<Mebibytes>);
static_assert(!std::is_convertible_v<std::uint32_t, Texels>, "construction must be explicit");
static_assert(!std::is_convertible_v<Texels, std::uint32_t>, "no silent way back to a bare number");
// Constructibility as well as convertibility, which answer different questions
// -- what happens by accident, and what happens when somebody writes the braces
// on purpose. core/Units.hpp records the day the two disagreed.
static_assert(!std::is_convertible_v<Texels, Mebibytes>, "and no way across either");
static_assert(!std::is_constructible_v<Texels, Mebibytes>, "not even with the braces written out");
static_assert(!std::is_constructible_v<Mebibytes, Texels>);
static_assert(Texels{}.value() == 0U, "a default count is zero, not whatever was on the stack");

static_assert((Texels{1024U} + Texels{24U}).value() == 1048U);
static_assert((Texels{1024U} - Texels{24U}).value() == 1000U);
static_assert((Texels{16U} * 4U).value() == 64U);
static_assert((4U * Texels{16U}).value() == 64U, "and the scale reads on either side");
static_assert(Texels{1U} < Texels{2U});
static_assert(Texels{2U} == Texels{2U}, "a whole number compares exactly, so == is the right word");
static_assert(Texels{1U} != Texels{2U});

static_assert(!divisible<Texels, Texels>,
              "a ratio of counts would truncate, so it is not an operator");
static_assert(!divisible<Texels, std::uint32_t>, "nor is a count over a plain number");
static_assert(!divisible<Texels, f64>);
static_assert(!multipliable<Texels, Texels>,
              "an edge length times an edge length is an area, which has no type here");
static_assert(!multipliable<Texels, Mebibytes>);
static_assert(!multipliable<Texels, f64>, "and a fractional scale would silently mean its floor");
static_assert(!multipliable<Texels, int>, "a signed scale has to be spelled unsigned");

// **The guard, proved where it has to hold: inside a constant expression.**
// A count that would wrap is refused while the compiler is working the
// expression out, so a bad literal is a build failure rather than four billion
// of something.
template <typename F>
concept constantEvaluable = requires { typename std::bool_constant<(F{}(), true)>; };

static_assert(!constantEvaluable<decltype([] { return (Texels{1U} - Texels{2U}).value(); })>,
              "a subtraction that would go below zero is refused as the compiler evaluates it");
static_assert(
    !constantEvaluable<decltype([] { return (Texels{kCountMaximum} + Texels{1U}).value(); })>,
    "and an addition that would carry past the end");
static_assert(!constantEvaluable<decltype([] { return (Texels{kCountMaximum} * 2U).value(); })>,
              "and a product that would not fit");
// **The positive control**, and it is not decoration: without it, "this is not
// a constant expression" and "this proof is broken" are the same answer, which
// is VERIFICATION.md rule 23 exactly. Measured against the unguarded form on
// 2026-09-23, where the same concept answered yes.
static_assert(constantEvaluable<decltype([] { return (Texels{1024U} - Texels{24U}).value(); })>,
              "while every operation that does fit is still a constant expression");

} // namespace orb

#endif // ORBSIM_CORE_SCALAR_HPP
