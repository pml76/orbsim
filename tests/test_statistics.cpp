//
// Tests for view/FrameStatistics.hpp: the percentiles and the mean the
// benchmark prints (M1-22; register decisions 366 and 375).
//
// **Nothing here is checked against the code's own arithmetic.** The small
// samples are worked by hand, with fractions a double holds exactly, so the
// expected value is exact and is asserted bit for bit; the larger sample's
// values come from scripts/statistics-reference.py, in exact rational
// arithmetic and checked there against Python's own statistics.quantiles,
// which is the same convention written another way.
//
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "tests/OrbitTestSupport.hpp"
#include "view/FrameStatistics.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

using namespace orb;
using namespace orb::view;
using orb::test::WithinRelTo;

namespace {

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();

[[nodiscard]] std::vector<Seconds> secondsOf(std::span<const f64> values) {
    std::vector<Seconds> made;
    made.reserve(values.size());
    std::ranges::transform(
        values, std::back_inserter(made), [](f64 v) noexcept { return Seconds{v}; });
    return made;
}

[[nodiscard]] Seconds percentileOf(std::span<const Seconds> sample, Fraction p) {
    const auto got = percentile(sample, p);
    REQUIRE(got.has_value());
    return *got;
}

// All three functions refuse `sample`, each with `error`. `REQUIRE(!x)`
// rather than REQUIRE_FALSE, for the reason tests/test_projection.cpp gives:
// the latter trips clang-analyzer-optin.core.EnumCastOutOfRange inside
// Catch2. And the refusal is required before its error is read, which is
// undefined on an expected that holds a value (VERIFICATION.md rule 23).
void checkRefused(const std::vector<Seconds>& sample, StatisticsError error) {
    const auto p = percentile(sample, fraction(0.5));
    REQUIRE(!p.has_value());
    CHECK(p.error() == error);
    const auto average = mean(sample);
    REQUIRE(!average.has_value());
    CHECK(average.error() == error);
    const auto summary = summarise(sample);
    REQUIRE(!summary.has_value());
    CHECK(summary.error() == error);
}

// Bit identity, the claim wherever the expected value is exact.
[[nodiscard]] bool sameBits(Seconds got, f64 want) { return got.bitIdentical(Seconds{want}); }

// The frame times 1 to 10, out of order, and the four of an even-sized
// sample: small enough to work by hand.
constexpr auto kOneToTen = std::to_array<f64>({7.0, 2.0, 10.0, 4.0, 1.0, 9.0, 3.0, 8.0, 6.0, 5.0});
constexpr auto kFour = std::to_array<f64>({4.0, 1.0, 3.0, 2.0});

// From scripts/statistics-reference.py, unedited: 37 frame times in seconds,
// seed 20261004, whole multiples of 100 ns between 2 ms and 30 ms.
constexpr auto kSample = std::to_array<f64>({
    0.0111544, 0.0048391, 0.0178742, 0.0225966, 0.0163877, 0.029428,  0.0119991, 0.0086452,
    0.0269564, 0.0095946, 0.0226322, 0.0208467, 0.0129136, 0.0132255, 0.0069575, 0.0150516,
    0.0114022, 0.0160068, 0.0228437, 0.0156851, 0.0119422, 0.0101309, 0.014986,  0.0058693,
    0.0097661, 0.0240627, 0.0244101, 0.0135378, 0.021365,  0.0051976, 0.0140028, 0.0166161,
    0.0222759, 0.0213408, 0.0153364, 0.0167202, 0.0191815,
});
constexpr f64 kSampleMedian = 0.0153364;         // statistics.quantiles: 1 ulp away
constexpr f64 kSampleP95 = 0.024919359999999995; // statistics.quantiles: 1 ulp away
constexpr f64 kSampleP99 = 0.028538223999999997; // statistics.quantiles: the same
constexpr f64 kSampleMax = 0.029428;
constexpr f64 kSampleMean = 0.01577788108108108;

// How far a percentile or the mean may be from the exact value, relative to
// it, as WithinRelTo measures it. Measured 2026-10-04 with every digit: the
// median and the mean are the exact value to the bit, p95 is two ulp from it
// (3.3e-16) and p99 one (2.2e-16), from rounding h = (n - 1) p and the
// interpolation. Set at about twice the larger (VERIFICATION.md rule 4).
// *(A first reading of 1.4e-16 came from Catch2's printout, which stops at
// 16 digits and hid the second ulp; the figures above are from the same
// arithmetic replayed in full.)*
constexpr Tolerance kReferenceRelative{7e-16};

} // namespace

TEST_CASE("percentiles of a small sample, worked by hand") {
    const std::vector<Seconds> sample = secondsOf(kOneToTen);
    // h = 9 p. At p = 0 the smallest, at 1 the largest; at 1/4, h = 2.25,
    // a quarter of the way from 3 to 4; at 3/4, h = 6.75.
    CHECK(sameBits(percentileOf(sample, fraction(0.0)), 1.0));
    CHECK(sameBits(percentileOf(sample, fraction(1.0)), 10.0));
    CHECK(sameBits(percentileOf(sample, fraction(0.25)), 3.25));
    CHECK(sameBits(percentileOf(sample, fraction(0.75)), 7.75));
    CHECK(sameBits(percentileOf(sample, fraction(0.5)), 5.5));
}

TEST_CASE("an even-sized sample's median interpolates") {
    const std::vector<Seconds> sample = secondsOf(kFour);
    // h = 3 / 2: halfway between the two middle values, 2 and 3.
    CHECK(sameBits(percentileOf(sample, fraction(0.5)), 2.5));
}

TEST_CASE("a single value is every percentile and the mean") {
    const std::vector<Seconds> sample{Seconds{0.0166}};
    for (const f64 p : {0.0, 0.01, 0.5, 0.95, 0.99, 1.0}) {
        INFO("p = " << p);
        const auto at = Fraction::from(p);
        REQUIRE(at.has_value());
        CHECK(sameBits(percentileOf(sample, *at), 0.0166));
    }
    const auto average = mean(sample);
    REQUIRE(average.has_value());
    CHECK(sameBits(*average, 0.0166));
}

TEST_CASE("p95 is interpolated between ranks, not the nearest rank") {
    // The convention, asserted (decision 366). On 1 to 10, p95 is 9.55 by
    // interpolation, where nearest rank and type 6 both give 10 -- and the
    // median 5.5, where nearest rank gives 5. The 0.95 a double holds is a
    // shade under 0.95, so the value is a shade under 9.55: within two ulp.
    const std::vector<Seconds> sample = secondsOf(kOneToTen);
    const f64 p95 = percentileOf(sample, fraction(0.95)).value();
    CHECK(std::abs(p95 - 9.55) <= 2.0 * std::numeric_limits<f64>::epsilon() * 9.55);
    CHECK(p95 < 10.0);
    CHECK(sameBits(percentileOf(sample, fraction(0.5)), 5.5));
}

TEST_CASE("percentiles and the mean against exact references") {
    const std::vector<Seconds> sample = secondsOf(kSample);
    CHECK_THAT(percentileOf(sample, fraction(0.5)).value(),
               WithinRelTo(kSampleMedian, kReferenceRelative));
    CHECK_THAT(percentileOf(sample, fraction(0.95)).value(),
               WithinRelTo(kSampleP95, kReferenceRelative));
    CHECK_THAT(percentileOf(sample, fraction(0.99)).value(),
               WithinRelTo(kSampleP99, kReferenceRelative));
    CHECK(sameBits(percentileOf(sample, fraction(1.0)), kSampleMax));
    const auto average = mean(sample);
    REQUIRE(average.has_value());
    CHECK_THAT(average->value(), WithinRelTo(kSampleMean, kReferenceRelative));
}

TEST_CASE("the summary is the single functions, from one sort") {
    const std::vector<Seconds> sample = secondsOf(kSample);
    const auto summary = summarise(sample);
    REQUIRE(summary.has_value());
    const auto average = mean(sample);
    REQUIRE(average.has_value());
    CHECK(summary->mean.bitIdentical(*average));
    CHECK(summary->median.bitIdentical(percentileOf(sample, fraction(0.5))));
    CHECK(summary->p95.bitIdentical(percentileOf(sample, fraction(0.95))));
    CHECK(summary->p99.bitIdentical(percentileOf(sample, fraction(0.99))));
    CHECK(sameBits(summary->max, kSampleMax));
}

TEST_CASE("the order of the sample does not matter") {
    std::vector<Seconds> sample = secondsOf(kSample);
    const auto before = summarise(sample);
    REQUIRE(before.has_value());
    std::ranges::reverse(sample);
    const auto after = summarise(sample);
    REQUIRE(after.has_value());
    CHECK(before->mean.bitIdentical(after->mean));
    CHECK(before->median.bitIdentical(after->median));
    CHECK(before->p95.bitIdentical(after->p95));
    CHECK(before->p99.bitIdentical(after->p99));
    CHECK(before->max.bitIdentical(after->max));
}

TEST_CASE("the mean keeps what a plain sum rounds away") {
    // 1 and four halves of an ulp of 1: a plain sum adds each half-ulp to 1
    // and rounds back to 1, every time, and gives 1 / 5. The sum is exactly
    // 1 + 2^-51, which a double holds, and its fifth rounds to
    // 0.2000000000000001 -- worked in exact fractions (decision 375).
    const f64 half = std::ldexp(1.0, -53);
    const std::vector<Seconds> sample =
        secondsOf(std::to_array<f64>({1.0, half, half, half, half}));
    const auto average = mean(sample);
    REQUIRE(average.has_value());
    CHECK(sameBits(*average, 0.2000000000000001));
}

TEST_CASE("an empty sample is reported, not divided by zero") {
    checkRefused({}, StatisticsError::EmptySample);
}

TEST_CASE("a value that is not finite, or is negative, is reported") {
    struct Case {
        f64 bad{};
        StatisticsError error{};
    };
    for (const Case& c : {
             Case{.bad = kNaN, .error = StatisticsError::NotFinite},
             Case{.bad = kInf, .error = StatisticsError::NotFinite},
             Case{.bad = -kInf, .error = StatisticsError::NotFinite},
             Case{.bad = -1e-9, .error = StatisticsError::Negative},
         }) {
        INFO("bad value " << c.bad);
        // First, in the middle and last, so a check that reads only one end
        // of the sample cannot pass.
        for (const std::size_t at : {std::size_t{0}, std::size_t{2}, std::size_t{4}}) {
            INFO("at index " << at);
            std::vector<Seconds> sample = secondsOf(kFour);
            sample.insert(sample.begin() + static_cast<std::ptrdiff_t>(at), Seconds{c.bad});
            checkRefused(sample, c.error);
        }
    }
}

TEST_CASE("negative zero is zero, and accepted") {
    const std::vector<Seconds> sample = secondsOf(std::to_array<f64>({-0.0, 1.0}));
    CHECK(percentile(sample, fraction(0.5)).has_value());
    CHECK(mean(sample).has_value());
}
