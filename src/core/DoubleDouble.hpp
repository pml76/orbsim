#ifndef ORBSIM_CORE_DOUBLEDOUBLE_HPP
#define ORBSIM_CORE_DOUBLEDOUBLE_HPP
//
// Double-double arithmetic: a number carried as an unevaluated sum of two
// doubles, hi + lo, where |lo| is at most half an ulp of hi. That is about 31
// significant decimal digits, and it is here for one reason -- the conversion
// from a state vector to orbital elements is a chain of subtractions that
// cancel, and on a nearly radial or a nearly parabolic orbit the cancellation
// takes very nearly every digit a double has.
//
// What it bought, measured against 60-digit references over 56,532 states
// across nine families (2026-09-12), worst relative error per family before
// and after -- angles absolute, in radians:
//
//   ordinary                2.2e-14   ->  1.0e-15
//   nearly radial     slr   1.1e-5    ->  1.1e-16
//   near-parabolic    sma   7.1e-4    ->  2.0e-16
//   hyperbolic asymptote    3.7e-8    ->  1.0e-15
//   nearly circular   ecc   1.7e-7    ->  1.1e-16
//   at 1e-112 m       slr   0.98      ->  9.3e-17
//
// Why Dekker's splitting rather than std::fma. twoProduct is two lines with a
// fused multiply-add and would be quicker. It would also give a different
// answer on a target without the instruction, and this codebase asserts that
// the same inputs produce the same bits. Splitting uses nothing but +, - and
// *, each correctly rounded by IEEE 754, so the answer is identical on every
// conforming target: measured identical across clang on Windows, clang under
// WSL and gcc-14, over the same 56,532 states.
//
// What the exactness rests on. twoProduct is exact only while the compiler
// neither contracts a multiply-add nor reassociates a sum. `-ffp-contract=off`
// is set for the whole tree and `-ffast-math` is never set (CMakeLists.txt);
// both are load-bearing for this header rather than a preference. There is no
// way to assert it from inside the language, so it is written here instead.
//
// Two limits are not obvious and both produced real defects while this was
// being written, so both are handled rather than documented away:
//
//   * Dekker's splitting constant is 2^27 + 1, so the multiplication inside
//     `split` overflows once |a| passes 2^996 -- silently, because the product
//     being corrected can still be finite, and what comes back is a correct hi
//     with a NaN error term. A state whose r v^2 / mu is 1.6e305, a perfectly
//     ordinary double, reached this and returned a NaN eccentricity.
//   * An intermediate that legitimately overflows must not become a NaN. Every
//     operation below falls back to the plain double result the moment its own
//     output stops being finite, instead of computing a correction term out of
//     infinities: inf - inf inside an error term is worse than the overflow it
//     came from, because the overflow is still the correctly rounded answer.
//
// Callers working at extreme scales should still scale their inputs by a power
// of two first -- exact, and what `scaleByTwoPower` is for -- because a product
// of two quantities near the top of the range overflows whatever this header
// does about it.
//
// Where the algorithms come from. The splitting and the exact product are
// Dekker (1971), "A floating-point technique for extending the available
// precision"; the exact sum is Knuth's, TAOCP volume 2. The remedy for the
// split's overflow above 2^996, and the shape of the division and the square
// root as one Newton correction each, are Bailey, Hida and Li's QD library.
// None of it is copied: this is our own implementation of published
// algorithms, which is why there is no entry in THIRD_PARTY.md.
//
#include "core/Scalar.hpp"

#include <cmath>
#include <limits>

namespace orb {

// The unevaluated sum hi + lo. A plain aggregate with no member functions and
// no invariant to protect: every operation is a free function below, and the
// representation is the interface. `{x, 0.0}` is the exact double x, which is
// what `exact()` spells.
struct DoubleDouble {
    f64 hi{};
    f64 lo{};
};

// Finiteness in a constant expression, which <cmath>'s isfinite is not until
// C++26. NaN fails both comparisons and each infinity fails one of them, so
// this is exactly std::isfinite without leaving constant evaluation.
[[nodiscard]] constexpr bool isFinite(f64 x) noexcept {
    return x >= -std::numeric_limits<f64>::max() && x <= std::numeric_limits<f64>::max();
}

// An exact double, widened.
[[nodiscard]] constexpr DoubleDouble exact(f64 a) noexcept { return {.hi = a, .lo = 0.0}; }

// The nearest double to the pair. This is where the extra digits are spent.
[[nodiscard]] constexpr f64 toDouble(DoubleDouble a) noexcept { return a.hi + a.lo; }

// The exact sum of two doubles, on the precondition that |a| >= |b|. Three
// operations rather than six; `twoSum` is the one to reach for when the
// ordering is not known.
[[nodiscard]] constexpr DoubleDouble quickTwoSum(f64 a, f64 b) noexcept {
    const f64 sum = a + b;
    if (!isFinite(sum)) return {.hi = sum, .lo = 0.0};
    return {.hi = sum, .lo = b - (sum - a)};
}

// The exact sum of any two doubles: hi is their rounded sum and lo is exactly
// the rounding error it made (Knuth's two-sum).
[[nodiscard]] constexpr DoubleDouble twoSum(f64 a, f64 b) noexcept {
    const f64 sum = a + b;
    if (!isFinite(sum)) return {.hi = sum, .lo = 0.0};
    const f64 shifted = sum - a;
    return {.hi = sum, .lo = (a - (sum - shifted)) + (b - shifted)};
}

// Dekker's split: a = hi + lo exactly, with each half carrying at most 26 of
// the 53 significant bits, so a product of two halves is exact.
//
// Above 2^996 the split is taken on a copy scaled down by 2^28 and the halves
// scaled back, both exact, because 2^27 * a would otherwise overflow. See the
// note at the top of this file: the failure without this is silent.
[[nodiscard]] constexpr DoubleDouble split(f64 a) noexcept {
    constexpr f64 kSplitConstant = 134217729.0; // 2^27 + 1
    constexpr f64 kSplitCeiling = 0x1p996;
    constexpr f64 kSplitDown = 0x1p-28;
    constexpr f64 kSplitUp = 0x1p28;
    if (a > kSplitCeiling || a < -kSplitCeiling) {
        const f64 small = a * kSplitDown;
        const f64 shifted = kSplitConstant * small;
        const f64 hi = shifted - (shifted - small);
        return {.hi = hi * kSplitUp, .lo = (small - hi) * kSplitUp};
    }
    const f64 shifted = kSplitConstant * a;
    const f64 hi = shifted - (shifted - a);
    return {.hi = hi, .lo = a - hi};
}

// The exact product of two doubles: hi is their rounded product and lo is
// exactly the rounding error, reconstructed from the four partial products of
// the split halves.
[[nodiscard]] constexpr DoubleDouble twoProduct(f64 a, f64 b) noexcept {
    const f64 product = a * b;
    if (!isFinite(product)) return {.hi = product, .lo = 0.0};
    const DoubleDouble x = split(a);
    const DoubleDouble y = split(b);
    const f64 error = (((x.hi * y.hi) - product) + (x.hi * y.lo) + (x.lo * y.hi)) + (x.lo * y.lo);
    return {.hi = product, .lo = error};
}

[[nodiscard]] constexpr DoubleDouble operator-(DoubleDouble a) noexcept {
    return {.hi = -a.hi, .lo = -a.lo};
}

[[nodiscard]] constexpr DoubleDouble operator+(DoubleDouble a, DoubleDouble b) noexcept {
    const DoubleDouble sum = twoSum(a.hi, b.hi);
    if (!isFinite(sum.hi)) return {.hi = sum.hi, .lo = 0.0};
    return quickTwoSum(sum.hi, sum.lo + (a.lo + b.lo));
}

[[nodiscard]] constexpr DoubleDouble operator-(DoubleDouble a, DoubleDouble b) noexcept {
    return a + (-b);
}

[[nodiscard]] constexpr DoubleDouble operator*(DoubleDouble a, DoubleDouble b) noexcept {
    const DoubleDouble product = twoProduct(a.hi, b.hi);
    if (!isFinite(product.hi)) return {.hi = product.hi, .lo = 0.0};
    return quickTwoSum(product.hi, product.lo + ((a.hi * b.lo) + (a.lo * b.hi)));
}

// One Newton correction on the leading quotient, which doubles the correct
// digits and is all a double-double can hold.
//
// A finite leading quotient is not on its own enough to proceed: an infinite
// divisor gives a quotient of zero, and the residual below would then form
// inf * 0. The guard covers that case too, and a zero divisor through the
// first test.
[[nodiscard]] constexpr DoubleDouble operator/(DoubleDouble a, DoubleDouble b) noexcept {
    const f64 quotient = a.hi / b.hi;
    if (!isFinite(quotient) || !isFinite(a.hi) || !isFinite(b.hi)) {
        return {.hi = quotient, .lo = 0.0};
    }
    const DoubleDouble residual = a - (b * exact(quotient));
    return quickTwoSum(quotient, residual.hi / b.hi);
}

// Multiply by a power of two: exact until it over- or underflows. scalbn
// rather than a multiplication by 2^k, because forming 2^k as a double
// overflows for k beyond 1023 while scalbn does not.
[[nodiscard]] inline DoubleDouble scaleByTwoPower(DoubleDouble a, int exponent) noexcept {
    return {.hi = std::scalbn(a.hi, exponent), .lo = std::scalbn(a.lo, exponent)};
}

// The square root, to double-double precision, by one Newton step in the
// extended precision: x <- (x + a/x) / 2.
//
// A NaN propagates rather than collapsing to zero. Reporting a zero length for
// a vector whose components did not survive is the one outcome worse than
// reporting nothing -- it would give a circular orbit's eccentricity to an
// escape trajectory, and a postcondition looking only for NaN would pass it.
[[nodiscard]] inline DoubleDouble sqrtOf(DoubleDouble a) noexcept {
    if (std::isnan(a.hi)) return {.hi = a.hi, .lo = 0.0};
    if (!(a.hi > 0.0)) return {.hi = 0.0, .lo = 0.0}; // zero, and a negative from rounding
    if (!isFinite(a.hi)) return {.hi = a.hi, .lo = 0.0};
    const f64 approximate = std::sqrt(a.hi);
    // Halving both halves is exact, so the pair stays a valid double-double.
    const DoubleDouble improved = exact(approximate) + (a / exact(approximate));
    return {.hi = improved.hi * 0.5, .lo = improved.lo * 0.5};
}

// Compile-time proofs of the properties everything above rests on. A
// static_assert is a unit test that costs nothing at runtime, and these are
// the claims the orbital conversion depends on. Compared through nearlyEqual
// with a zero tolerance, which is how this codebase compares doubles exactly.
//
// The exactness claim, stated as it is used: the error term is not small, it
// is the whole of what the rounded operation discarded.
static_assert(sizeof(DoubleDouble) == 2 * sizeof(f64), "two doubles and nothing else");
static_assert(std::is_trivially_copyable_v<DoubleDouble>);
static_assert(std::is_aggregate_v<DoubleDouble>, "no constructor, so {hi, lo} stays available");

static_assert(isFinite(0.0) && isFinite(1.0) && isFinite(-1.0));
static_assert(!isFinite(std::numeric_limits<f64>::infinity()));
static_assert(!isFinite(-std::numeric_limits<f64>::infinity()));
static_assert(!isFinite(std::numeric_limits<f64>::quiet_NaN()));

// 1 + 2^-60 is not a double. twoSum keeps the part that does not fit, exactly.
static_assert(nearlyEqual(twoSum(1.0, 0x1p-60).hi, 1.0, Tolerance{0.0}));
static_assert(nearlyEqual(twoSum(1.0, 0x1p-60).lo, 0x1p-60, Tolerance{0.0}));
// And it does not care which way round they come.
static_assert(nearlyEqual(twoSum(0x1p-60, 1.0).lo, 0x1p-60, Tolerance{0.0}));
// A sum that is exact already leaves nothing behind.
static_assert(nearlyEqual(twoSum(1.0, 2.0).hi, 3.0, Tolerance{0.0}));
static_assert(nearlyEqual(twoSum(1.0, 2.0).lo, 0.0, Tolerance{0.0}));

// (2^27 + 1)^2 needs 55 bits, so it is not a double; the error term is the
// 2^0 bit the product had to drop.
static_assert(nearlyEqual(twoProduct(134217729.0, 134217729.0).hi,
                          18014398777917440.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(twoProduct(134217729.0, 134217729.0).lo, 1.0, Tolerance{0.0}));
// The split halves recombine to the original, at every scale, including above
// the ceiling where the scaled path runs.
static_assert(nearlyEqual(split(3.0).hi + split(3.0).lo, 3.0, Tolerance{0.0}));
static_assert(nearlyEqual(split(0x1p1000).hi + split(0x1p1000).lo, 0x1p1000, Tolerance{0.0}));
static_assert(nearlyEqual(split(-0x1.8p1020).hi + split(-0x1.8p1020).lo,
                          -0x1.8p1020,
                          Tolerance{0.0}));
// A product big enough to overflow the split, small enough to be a double.
// Without the ceiling in split() the error term here is a NaN.
static_assert(isFinite(twoProduct(0x1p1000, 0x1p-100).lo));

// The sum that cancels: (1 + 2^-60) - 1 is 2^-60 exactly, which a double alone
// cannot say at all.
static_assert(nearlyEqual(toDouble(twoSum(1.0, 0x1p-60) - exact(1.0)), 0x1p-60, Tolerance{0.0}));
// Dividing by an infinity is zero, and reaches that without the guard having
// to invent anything.
static_assert(nearlyEqual(toDouble(exact(1.0) / exact(std::numeric_limits<f64>::infinity())),
                          0.0,
                          Tolerance{0.0}));

// What is *not* asserted here, deliberately: that an operation which overflows
// comes back infinite rather than NaN. It is true, and
// tests/test_double_double.cpp checks every case of it -- but it cannot be a
// static_assert, because an operation that overflows is not a constant
// expression. gcc-14 enforces that and refuses to compile the assertion;
// clang evaluates it to an infinity and accepts. The second toolchain exists
// to find exactly this kind of disagreement, and the resolution is to make the
// claim at runtime, where both agree, rather than to write an assertion that
// one compiler permits and the other does not.

} // namespace orb

#endif
