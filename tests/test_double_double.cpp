//
// core/DoubleDouble.hpp: the exactness claims, checked against something that
// is not the code under test.
//
// The strongest of these is the product. twoProduct reconstructs the rounding
// error of a * b from Dekker's splitting -- six operations on the two halves of
// each operand -- while std::fma computes a * b with a single rounding, so
// fma(a, b, -(a * b)) is that same error by a completely different route, one
// that is usually a hardware instruction. If the two agree bit for bit over a
// few hundred thousand operands spanning every scale a double reaches, the
// splitting is right. That is the check this file is built around, and it is
// independent in the sense VERIFICATION.md asks for: neither implementation
// can hide an error behind the other.
//
// The sum is checked against integer arithmetic instead: for integer-valued
// operands whose exact sum needs more than 53 bits, the exact answer is an
// integer that fits in an std::int64_t, so it can be computed without any
// floating point at all.
//
// What cannot be checked from inside the language is that the compiler is not
// contracting a multiply-add or reassociating a sum, which would make
// twoProduct inexact. `-ffp-contract=off` is set for the whole tree and
// -ffast-math is never set; if either changed, the product test here is what
// would notice, which is the other reason it is written this way.
//
#include "core/DoubleDouble.hpp"
#include "core/Scalar.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <random>

namespace {

using orb::DoubleDouble;
using orb::exact;
using orb::f64;
using orb::isFinite;
using orb::nearlyEqual;
using orb::quickTwoSum;
using orb::split;
using orb::sqrtOf;
using orb::toDouble;
using orb::Tolerance;
using orb::twoProduct;
using orb::twoSum;

// Bit-for-bit equality of two doubles, which is the claim everywhere in this
// file: an exactness property is not "close", it is equal. nearlyEqual with a
// zero tolerance is how this codebase spells that (core/Scalar.hpp), and it
// reports false for a NaN on either side, which the tests below want.
[[nodiscard]] bool identical(f64 got, f64 want) { return nearlyEqual(got, want, Tolerance{0.0}); }

// Written down so a failure can be reproduced, as VERIFICATION.md requires.
constexpr std::uint64_t kSeed = 20260912;

// The case counts are set by what the Debug tree costs, not by what is
// available: an unoptimised double-double operation plus a Catch2 assertion is
// about 200 us here, so 200,000 product cases added 40 seconds to every `check`
// and 20,000 adds four. Nothing is lost by that. These are exactness
// properties, not rare events -- a wrong splitting constant or a dropped cross
// term fails on essentially every case, which is why the mutation tests kill
// every planted defect at these counts (scratchpad, 2026-09-12).
constexpr int kProductCases = 20000;
constexpr int kSumCases = 20000;
constexpr int kSplitCases = 5000;
constexpr int kNewtonCases = 10000;
constexpr int kQuickSumCases = 10000;

class Sampler {
public:
    // A double with a uniformly random 52-bit mantissa and an exponent drawn
    // uniformly from a range, which is what covers "every scale" rather than
    // "every value between 0 and 1".
    [[nodiscard]] f64 atExponent(int lo, int hi) {
        const f64 mantissa = 1.0 + (unit_(rng_) * 0.5);
        const int exponent = lo + static_cast<int>(unit_(rng_) * static_cast<f64>(hi - lo));
        const f64 sign = (unit_(rng_) < 0.5) ? -1.0 : 1.0;
        return sign * std::scalbn(mantissa, exponent);
    }

    [[nodiscard]] f64 unit() { return unit_(rng_); }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSeed};
    std::uniform_real_distribution<f64> unit_{0.0, 1.0};
};

} // namespace

// The central claim: Dekker's splitting and a fused multiply-add agree on what
// a * b discarded, everywhere.
//
// The exponent range is chosen so that a * b neither overflows nor falls into
// the subnormals, because there the error term is not representable and
// neither implementation claims it is: the product of two operands at 2^-400
// is 2^-800, and its error term 2^-853, which is below the smallest subnormal.
TEST_CASE("the error term of a product is exactly what an fma reports", "[core][dd]") {
    Sampler sampler;
    for (int i = 0; i < kProductCases; ++i) {
        const f64 a = sampler.atExponent(-400, 400);
        const f64 b = sampler.atExponent(-400, 400);
        const f64 product = a * b;
        if (!isFinite(product) || std::abs(product) < 0x1p-900) continue;

        const DoubleDouble got = twoProduct(a, b);
        const f64 wantError = std::fma(a, b, -product);
        CAPTURE(kSeed, i, a, b);
        INFO(std::format("product {:.17g}, splitting says the error is {:.17g}, fma says {:.17g}",
                         product,
                         got.lo,
                         wantError));
        REQUIRE(identical(got.hi, product));
        REQUIRE(identical(got.lo, wantError));
    }
}

// Above 2^996 Dekker's splitting constant overflows on its own, and the failure
// is silent: the product stays finite and only the error term becomes a NaN.
// core/DoubleDouble.hpp scales the operand for the split there, and this is the
// case that says so. Without that path the second REQUIRE fails and the third
// reports a NaN.
TEST_CASE("splitting survives the top of the range", "[core][dd]") {
    Sampler sampler;
    for (int i = 0; i < kSplitCases; ++i) {
        // One operand above the ceiling, the other small enough to keep the
        // product finite.
        const f64 big = sampler.atExponent(997, 1023);
        const f64 small = sampler.atExponent(-200, -20);
        const f64 product = big * small;
        if (!isFinite(product) || std::abs(product) < 0x1p-900) continue;

        const DoubleDouble halves = split(big);
        CAPTURE(kSeed, i, big, small);
        INFO(std::format("split({:.17g}) = {:.17g} + {:.17g}", big, halves.hi, halves.lo));
        REQUIRE(identical(halves.hi + halves.lo, big));

        const DoubleDouble got = twoProduct(big, small);
        INFO(std::format("product {:.17g}, error {:.17g}, fma says {:.17g}",
                         product,
                         got.lo,
                         std::fma(big, small, -product)));
        REQUIRE(isFinite(got.lo));
        REQUIRE(identical(got.lo, std::fma(big, small, -product)));
    }
}

// The sum, against integer arithmetic. Operands are integers of up to 62 bits
// scaled by a power of two, so their exact sum is an integer that std::int64_t
// holds exactly, while the rounded sum is not.
TEST_CASE("the error term of a sum is exact", "[core][dd]") {
    Sampler sampler;
    for (int i = 0; i < kSumCases; ++i) {
        // Up to 2^62, so a + b cannot overflow an int64_t.
        const auto pick = [&sampler] {
            const f64 magnitude = sampler.unit() * 0x1p62;
            const f64 sign = (sampler.unit() < 0.5) ? -1.0 : 1.0;
            return static_cast<std::int64_t>(sign * std::trunc(magnitude));
        };
        const std::int64_t first = pick();
        const std::int64_t second = pick();
        const f64 a = static_cast<f64>(first);
        const f64 b = static_cast<f64>(second);
        // static_cast<f64> of an int64 can round; the exact sum has to be of
        // the doubles that went in, not of the integers they came from.
        if (static_cast<std::int64_t>(a) != first) continue;
        if (static_cast<std::int64_t>(b) != second) continue;

        const DoubleDouble got = twoSum(a, b);
        const std::int64_t exactSum = first + second;
        const f64 rounded = a + b;
        const std::int64_t discarded = exactSum - static_cast<std::int64_t>(rounded);

        CAPTURE(kSeed, i, first, second);
        INFO(std::format("{} + {} = {}, rounded to {:.17g}, error term {:.17g} against {}",
                         first,
                         second,
                         exactSum,
                         rounded,
                         got.lo,
                         discarded));
        REQUIRE(identical(got.hi, rounded));
        REQUIRE(identical(got.lo, static_cast<f64>(discarded)));
    }
}

// What the whole header exists for: a subtraction that cancels keeps the digits
// a double alone cannot hold.
TEST_CASE("a cancelling difference keeps what a double loses", "[core][dd]") {
    SECTION("one ulp below a power of two") {
        // (1 + 2^-60) - 1 is 2^-60 exactly. In doubles, 1 + 2^-60 rounds to 1
        // and the difference is zero.
        const DoubleDouble sum = twoSum(1.0, 0x1p-60);
        INFO("the double alone loses it entirely");
        REQUIRE(identical((1.0 + 0x1p-60) - 1.0, 0.0));
        INFO("the pair keeps it");
        REQUIRE(identical(toDouble(sum - exact(1.0)), 0x1p-60));
    }

    SECTION("the eccentricity vector's cancellation, in miniature") {
        // r v^2 / mu is 1 + d on a nearly circular orbit, and d is the whole
        // answer. At d = 1e-17 a double has none of it.
        const f64 d = 1e-17;
        const DoubleDouble onePlus = twoSum(1.0, d);
        const DoubleDouble recovered = onePlus - exact(1.0);
        INFO(std::format("recovered {:.17g}, want {:.17g}", toDouble(recovered), d));
        REQUIRE(identical(toDouble(recovered), d));
    }
}

// Division and the square root are the two operations that are not exact: each
// is one Newton correction on a double's worth of answer, which doubles the
// correct digits. The property asserted is the one a caller relies on --
// reconstructing the input to double-double precision, far beyond what a single
// double could show.
TEST_CASE("the quotient and the square root are correct beyond a double", "[core][dd]") {
    Sampler sampler;
    constexpr f64 kDoubleDoubleEpsilon = 0x1p-100; // about 4 ulp of 2^-106
    for (int i = 0; i < kNewtonCases; ++i) {
        const f64 a = sampler.atExponent(-200, 200);
        const f64 b = sampler.atExponent(-200, 200);

        const DoubleDouble quotient = exact(a) / exact(b);
        const DoubleDouble reconstructed = quotient * exact(b);
        const f64 quotientError = std::abs(toDouble((reconstructed - exact(a)) / exact(a)));
        CAPTURE(kSeed, i, a, b);
        INFO(std::format("(a/b)*b recovers a to {:.3g}", quotientError));
        REQUIRE(quotientError <= kDoubleDoubleEpsilon);

        const f64 positive = std::abs(a);
        const DoubleDouble root = sqrtOf(exact(positive));
        const f64 rootError =
            std::abs(toDouble(((root * root) - exact(positive)) / exact(positive)));
        INFO(std::format("sqrt(a)^2 recovers a to {:.3g}", rootError));
        REQUIRE(rootError <= kDoubleDoubleEpsilon);
    }
}

// An intermediate that legitimately overflows must stay an overflow. Forming a
// correction term out of infinities gives inf - inf, and a NaN is worse than
// the overflow it came from: the overflow is still the correctly rounded
// answer, and a postcondition that looks only for NaN would let the NaN
// through as a success. Every one of these returned a NaN at some point while
// the header was being written.
TEST_CASE("an overflow stays an overflow rather than becoming a NaN", "[core][dd]") {
    constexpr f64 kInf = std::numeric_limits<f64>::infinity();
    constexpr f64 kHuge = 0x1p1000;

    SECTION("a product that overflows") {
        const DoubleDouble got = exact(kHuge) * exact(kHuge);
        REQUIRE(std::isinf(toDouble(got)));
        REQUIRE_FALSE(std::isnan(got.lo));
    }

    SECTION("a sum that overflows") {
        const DoubleDouble got = exact(0x1.fp1023) + exact(0x1.fp1023);
        REQUIRE(std::isinf(toDouble(got)));
        REQUIRE_FALSE(std::isnan(got.lo));
    }

    SECTION("a quotient by an infinity") {
        const DoubleDouble got = exact(1.0) / exact(kInf);
        INFO(std::format("1 / inf = {:.17g}", toDouble(got)));
        REQUIRE(identical(toDouble(got), 0.0));
    }

    SECTION("a quotient of an infinity") {
        const DoubleDouble got = exact(kInf) / exact(2.0);
        REQUIRE(std::isinf(toDouble(got)));
    }

    SECTION("the square root of an infinity") {
        REQUIRE(std::isinf(toDouble(sqrtOf(exact(kInf)))));
    }

    SECTION("the square root of a NaN propagates rather than collapsing to zero") {
        // Zero here would be the worst answer available: it is a plausible
        // length, so nothing downstream would notice.
        const DoubleDouble got = sqrtOf(exact(std::numeric_limits<f64>::quiet_NaN()));
        REQUIRE(std::isnan(toDouble(got)));
    }

    SECTION("the square root of zero and of a negative rounding") {
        REQUIRE(identical(toDouble(sqrtOf(exact(0.0))), 0.0));
        REQUIRE(identical(toDouble(sqrtOf(exact(-0.0))), 0.0));
        // A sum of squares can round just below zero; a length of zero is the
        // answer there, not a NaN.
        REQUIRE(identical(toDouble(sqrtOf(exact(-0x1p-1070))), 0.0));
    }
}

// quickTwoSum carries a precondition -- |a| >= |b| -- and the tests above use
// it only through the operators, which satisfy it by construction. This pins
// the contract itself so a future rewrite cannot quietly weaken it.
TEST_CASE("quickTwoSum is exact when its precondition holds", "[core][dd]") {
    Sampler sampler;
    for (int i = 0; i < kQuickSumCases; ++i) {
        const f64 a = sampler.atExponent(-200, 200);
        const f64 b = sampler.atExponent(-260, -60) * std::abs(a);
        if (!isFinite(b) || std::abs(b) > std::abs(a)) continue;

        const DoubleDouble got = quickTwoSum(a, b);
        const DoubleDouble reference = twoSum(a, b);
        CAPTURE(kSeed, i, a, b);
        INFO(std::format("quick {:.17g} + {:.17g}, general {:.17g} + {:.17g}",
                         got.hi,
                         got.lo,
                         reference.hi,
                         reference.lo));
        REQUIRE(identical(got.hi, reference.hi));
        REQUIRE(identical(got.lo, reference.lo));
    }
}
