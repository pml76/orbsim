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
#include <expected>
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
// *(A namespace-scope `kCountMaximum` stood here until 2026-09-24. It was one
// `std::uint32_t`, which stopped being enough when `Bytes` arrived on a 64-bit
// representation; the limit is `Count::kMaximum` now, per representation, and
// each concrete type asserts its own below.)*

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
template <typename Derived, typename Rep = std::uint32_t>
    requires std::unsigned_integral<Rep>
struct Count {
    // The last value this representation holds. A member rather than one
    // namespace-scope constant, since 2026-09-24 and `Bytes`: a 32-bit limit
    // in a 64-bit type's guard would have refused every value above four
    // billion, which is most of what a byte count is for.
    static constexpr Rep kMaximum = std::numeric_limits<Rep>::max();

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }

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
        const bool wouldWrap = other.value_ > kMaximum - value_;
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

    // The test is written so that it cannot itself overflow: kMaximum
    // divided by the scale is the largest value that still fits, so the product
    // is checked without ever forming it. A scale of zero is separated out
    // because dividing by it is undefined behaviour, and because zero is the
    // one scale that can never overflow.
    //
    // Constrained rather than taking a plain parameter: one of that type would
    // accept an f64 or an int through an implicit conversion, and
    // `Texels{2} * 1.5` would compile and mean `* 1`. Any unsigned integer no
    // wider than the representation is allowed, so `Bytes{n} * 2U` reads
    // naturally without anybody having to write `2ULL`, and widening it is
    // lossless by construction.
    template <std::unsigned_integral Scale>
        requires(sizeof(Scale) <= sizeof(Rep))
    [[nodiscard]] constexpr Derived operator*(Scale scale) const noexcept {
        const Rep widened = scale;
        const bool wouldWrap = widened != 0U && value_ > kMaximum / widened;
        ORBSIM_EXPECTS(!wouldWrap);
        if (wouldWrap) stopConstantEvaluation();
        return Derived{value_ * widened};
    }

    // The mirrored spelling, so `4U * Texels{16}` reads as it should. Routed
    // through the member, so the guard lives in one place.
    template <std::unsigned_integral Scale>
        requires(sizeof(Scale) <= sizeof(Rep))
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
    Rep value_{};

    // Only the named derived type may construct its base, which is what stops
    // `struct Other : Count<Texels>` from compiling by accident.
    friend Derived;
    constexpr Count() noexcept = default;
    explicit constexpr Count(Rep v) noexcept : value_(v) {}

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

// What a count's named operations refuse, when refusing is all they can do
// (register decision 141).
//
// It lives here rather than beside `UnitError` in core/Units.hpp for the
// reason decision 90 put `EphemerisError` in astro/Sun.hpp: an error belongs
// to the module that can produce it, and this header is below that one -- a
// `UnitError` is not reachable from here, and reaching for one would invert
// the include order the top of this file describes.
//
// One name per failure, saying what it means rather than which predicate
// failed, as every error enum in this project is written.
enum class CountError : std::uint8_t {
    ZeroBlockSize, // no number of empty blocks fills anything
};

// **No `describe()` yet, deliberately, and this is the one place this header
// departs from ADR 0002's shape.** Every other error enum in the project has
// one, written as a `switch` with no `default:` so that adding an enumerator
// is a -Wswitch error rather than a silent "unknown". With a single
// enumerator that switch is what `readability-trivial-switch` reports, and
// register decision 91 ruled that exact case for `describe(EphemerisError)`:
// the answer there was a suppression at the one line, which is the owner's to
// grant and has not been asked for here.
//
// Nothing is lost while the enum has one value -- the name says what the
// failure is, and there is no caller wanting text. Whoever adds the second
// enumerator writes `describe()` then, and by then the switch is not trivial.
// Recorded in docs/STATUS.md so it is not left to memory.

// A number of bytes, on a **64-bit** representation (register decision 140).
//
// **Why it is not 32 bits, unlike its two siblings.** A texel count and a
// mebibyte budget are comfortable in 32 bits -- four billion texels is not a
// texture and four million mebibytes is four tebibytes. A byte count is not:
// `VkDeviceSize` is a `uint64_t`, checked in the pinned header; a KTX2 file
// carries 64-bit offsets and lengths; and M1-26 parses those **from an
// untrusted file**, where a silent 32-bit truncation of a 64-bit field is not
// an inconvenience but the memory-safety defect the task exists to prevent.
//
// **Why it is a Count at all**, when Count forbids division and byte
// arithmetic is exactly where integer division is meant: because the rule is
// worth keeping and the two operations that need it are worth naming.
// alignedUpTo and howManyFit are below, each with its own precondition, which
// is the same answer toRadians gives to "a conversion is a named function".
struct Bytes : Count<Bytes, std::uint64_t> {
    constexpr Bytes() noexcept = default;
    explicit constexpr Bytes(std::uint64_t v) noexcept : Count{v} {}

    // The next multiple of `alignment` at or above this value.
    //
    // A member rather than a free `alignUp(Bytes, Bytes)`, and that is not
    // only taste: two adjacent parameters of one type are what non-negotiable
    // 1 forbids and bugprone-easily-swappable-parameters reports, and
    // `offset.alignedUpTo(step)` cannot be written backwards.
    //
    // The alignment must be a power of two and not zero. That is asserted
    // rather than made unrepresentable, because every alignment in sight is
    // either a literal or a device limit read once at start-up; the day one
    // arrives from a configuration file is the day it earns a validated type
    // of its own (ADR 0022).
    [[nodiscard]] constexpr Bytes alignedUpTo(Bytes alignment) const noexcept {
        const std::uint64_t step = alignment.value();
        const bool usable = step != 0U && (step & (step - 1U)) == 0U;
        ORBSIM_EXPECTS(usable);
        if (!usable) stopConstantEvaluation();
        // **Masks, not a modulo**, and that is not a micro-optimisation: under
        // NDEBUG the assertion above is gone, so a step of zero would reach a
        // `%` and divide by zero, which is undefined behaviour rather than
        // merely a wrong answer. `clang-analyzer-core.DivideZero` said so on
        // 2026-09-24 and was right. An alignment is a power of two, so the
        // masking form is the natural one anyway -- and with a step of zero it
        // produces a carry that the guarded `+` below refuses, which is a
        // defined wrong answer loudly refused rather than an undefined one.
        const std::uint64_t mask = step - 1U;
        const std::uint64_t remainder = value() & mask;
        if (remainder == 0U) return *this;
        // The rounding up can itself carry past the end, and does not get to
        // do so quietly: `Bytes{kMaximum}.alignedUpTo(Bytes{16})` has no
        // answer, and says so rather than returning a small number.
        return *this + Bytes{step - remainder};
    }

    // How many blocks of `each` fit in this many bytes -- a count of things, so
    // a plain number rather than a quantity of bytes.
    //
    // This is the division Count refuses, given a name so that the refusal
    // stays the rule and the exception is visible.
    //
    // **It reports rather than asserting**, which is the one place these two
    // named operations differ, and the reason is that the failure has no
    // defined answer. An assertion vanishes under NDEBUG and would leave a
    // division by zero -- undefined behaviour, where every other guard in this
    // header only ever prevented a *defined* wrong answer.
    // `clang-analyzer-core.DivideZero` is what pointed that out. Returning a
    // number on the bad path would have been the third option ADR 0002
    // separates from reporting and asserting, so it reports.
    //
    // **The compile-time refusal comes free**, and is the same mechanism
    // `eccentricity()` uses: unwrapping a failed expected is not a constant
    // expression, so `Bytes{100}.howManyFit(Bytes{0}).value()` fails the build
    // rather than the test run.
    [[nodiscard]] constexpr std::expected<std::uint64_t, CountError>
    howManyFit(Bytes each) const noexcept {
        const std::uint64_t size = each.value();
        if (size == 0U) return std::unexpected(CountError::ZeroBlockSize);
        return value() / size;
    }
};

// Mebibytes to bytes, **widening before it multiplies**, which is the whole
// reason it is a named function rather than a multiplication at each call
// site: 4,096 MiB is exactly 2^32 bytes, so the obvious
// `Mebibytes{4096}.value() * 1048576U` is a 32-bit multiplication that
// produces **zero**. M1-34's tile cache is where that would have bitten.
[[nodiscard]] constexpr Bytes toBytes(Mebibytes mebibytes) noexcept {
    constexpr std::uint64_t kBytesPerMebibyte = 1024ULL * 1024ULL;
    return Bytes{std::uint64_t{mebibytes.value()} * kBytesPerMebibyte};
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

// Each representation's limit really is the last value it holds, checked
// against the language's own wrapping rule rather than against the definition
// -- which would be the code agreeing with itself. Preparing M1-12's mutation
// pass found that nothing else here would notice a limit one too small, since
// every guard and every proof of a guard reads it.
static_assert(Texels::kMaximum + 1U == 0U, "one past the limit is where an unsigned count wraps");
static_assert(Mebibytes::kMaximum + 1U == 0U);
static_assert(Bytes::kMaximum + 1U == 0U, "and the same for the 64-bit representation");

static_assert(sizeof(Texels) == sizeof(std::uint32_t), "a strong type must cost nothing");
static_assert(sizeof(Bytes) == sizeof(std::uint64_t), "and a byte count is the wider one");
static_assert(std::is_trivially_copyable_v<Bytes>);
static_assert(!std::is_convertible_v<std::uint64_t, Bytes>, "construction must be explicit");
static_assert(!std::is_convertible_v<Bytes, std::uint64_t>);
static_assert(!std::is_constructible_v<Bytes, Texels>, "and no count converts into another");
static_assert(!std::is_constructible_v<Texels, Bytes>);
static_assert(!std::is_constructible_v<Bytes, Mebibytes>,
              "not even the one a conversion exists for: toBytes says it by name");

// The wider representation is what this type was added for, so the claim is
// made where 32 bits would have failed.
static_assert((Bytes{4'294'967'296ULL} + Bytes{1ULL}).value() == 4'294'967'297ULL,
              "a byte count passes four billion without wrapping");
static_assert(toBytes(Mebibytes{4096U}).value() == 4'294'967'296ULL,
              "and 4,096 MiB is exactly 2^32 bytes, which a 32-bit multiply makes zero");
static_assert(toBytes(Mebibytes{512U}).value() == 536'870'912ULL);

// The two named operations, and the rule they are the exception to.
static_assert(Bytes{1000U}.alignedUpTo(Bytes{256U}).value() == 1024U);
static_assert(Bytes{1024U}.alignedUpTo(Bytes{256U}).value() == 1024U, "already aligned, unchanged");
static_assert(Bytes{0U}.alignedUpTo(Bytes{16U}).value() == 0U);
static_assert(Bytes{1000U}.howManyFit(Bytes{256U}) == 3U, "how many whole blocks fit, not four");
static_assert(Bytes{1024U}.howManyFit(Bytes{256U}) == 4U);
static_assert(!divisible<Bytes, Bytes>, "division is still not an operator, only a named function");

// A scale may be any unsigned integer no wider than the representation, so a
// byte count scales without anybody writing `2ULL`, and a 32-bit count cannot
// be scaled by a 64-bit number.
static_assert((Bytes{8U} * 2U).value() == 16U);
static_assert(!multipliable<Texels, std::uint64_t>,
              "a wider scale than the representation must not compile");
static_assert(!multipliable<Bytes, f64>);
static_assert(!multipliable<Bytes, int>);
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
    !constantEvaluable<decltype([] { return (Texels{Texels::kMaximum} + Texels{1U}).value(); })>,
    "and an addition that would carry past the end");
static_assert(!constantEvaluable<decltype([] { return (Texels{Texels::kMaximum} * 2U).value(); })>,
              "and a product that would not fit");
// **The positive control**, and it is not decoration: without it, "this is not
// a constant expression" and "this proof is broken" are the same answer, which
// is VERIFICATION.md rule 23 exactly. Measured against the unguarded form on
// 2026-09-23, where the same concept answered yes.
static_assert(constantEvaluable<decltype([] { return (Texels{1024U} - Texels{24U}).value(); })>,
              "while every operation that does fit is still a constant expression");
// **The preconditions of the two named operations, proved where they bite.**
// A run-time case cannot make these claims: the suite can only pass alignments
// that are powers of two, because anything else is forbidden, so dropping the
// power-of-two requirement changes nothing any case can see. M1-12's mutation
// pass found exactly that -- the mutant survived until these four lines
// existed. What refuses a bad alignment is the same mechanism that refuses a
// wraparound: a call the compiler cannot evaluate.
static_assert(
    !constantEvaluable<decltype([] { return Bytes{100U}.alignedUpTo(Bytes{3U}).value(); })>,
    "an alignment that is not a power of two is refused as the compiler evaluates it");
static_assert(
    !constantEvaluable<decltype([] { return Bytes{100U}.alignedUpTo(Bytes{0U}).value(); })>,
    "and so is an alignment of zero");
// A block size of zero is **reported** rather than refused outright, because
// it is the one failure here with no defined answer (see howManyFit). So the
// claim is that it reports, by name -- and `has_value()` is checked before
// `error()` is read, which is not decoration: reading `error()` on an expected
// that holds a value is undefined behaviour and compares equal to the zero
// enumerator, so an assertion without the guard passes while the function
// quietly accepts what it should refuse. M1-87's mutation pass found exactly
// that.
static_assert(!Bytes{100U}.howManyFit(Bytes{0U}).has_value(), "a block size of zero has no answer");
static_assert(Bytes{100U}.howManyFit(Bytes{0U}).error() == CountError::ZeroBlockSize,
              "and it is refused by that name, not by a generic one");
static_assert(Bytes{100U}.howManyFit(Bytes{16U}).has_value(), "-- the control");
// And unwrapping the refusal is still a build failure, which is what
// `eccentricity()` relies on: a bad literal never reaches a test run.
static_assert(
    !constantEvaluable<decltype([] { return Bytes{100U}.howManyFit(Bytes{0U}).value(); })>,
    "unwrapping the refusal is not a constant expression, so a bad literal fails the build");
static_assert(
    constantEvaluable<decltype([] { return Bytes{100U}.alignedUpTo(Bytes{16U}).value(); })>,
    "-- the control, without which these three say nothing");

} // namespace orb

#endif // ORBSIM_CORE_SCALAR_HPP
