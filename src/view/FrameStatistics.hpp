#ifndef ORBSIM_VIEW_FRAMESTATISTICS_HPP
#define ORBSIM_VIEW_FRAMESTATISTICS_HPP
//
// The statistics of a benchmark's frame times (M1-22): a percentile, the
// mean, and the summary the benchmark prints. Pure functions of the sample --
// no clock, no state -- so that tests/test_statistics.cpp can hold them to
// values worked out by hand and by scripts/statistics-reference.py.
//
// **A percentile is linear interpolation between the two nearest ranks**
// (register decision 366): Hyndman and Fan's type 7, which is Python's
// `statistics.quantiles(method="inclusive")`, numpy's default and Excel's
// `PERCENTILE.INC`. Stated because "p95" means at least three different
// things in common use -- on the frame times 1 to 10 this convention gives
// 9.55, nearest rank gives 10, and type 6 gives 10. For a sorted sample
// x[0] <= ... <= x[n-1] and a fraction p,
//
//     h = (n - 1) p,   k = floor(h),   value = x[k] + (h - k) (x[k+1] - x[k])
//
// with x[k+1] read as x[k] at the top. So the median of an even-sized sample
// is the mean of the two middle values, p = 0 is the smallest value and
// p = 1 the largest, and a sample of one value is that value at every p.
//
// **What is refused, by name** (register decision 375): an empty sample,
// which has no percentile and whose mean would be 0 / 0; a value that is not
// finite; and a negative one. A clock does not produce the last two, but
// these functions are not only for clocks, and a NaN would otherwise sort
// nowhere in particular and come out as a plausible median.
//
// **The mean is a compensated sum** (Neumaier's), which carries each
// addition's rounding error along rather than dropping it (decision 375). A
// plain sum of a benchmark's few thousand frame times would be wrong by about
// 1e-11 of the total, far below the clock's 100 ns, so this does not change a
// figure the benchmark prints; it is here so that the question never needs
// asking again, at a cost of a few lines.
//
#include "core/Units.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace orb::view {

enum class StatisticsError : std::uint8_t {
    EmptySample, // nothing to take a percentile or a mean of
    NotFinite,   // a value is infinite or not a number
    Negative,    // a value is below zero
};

[[nodiscard]] constexpr std::string_view describe(StatisticsError error) noexcept {
    switch (error) {
    case StatisticsError::EmptySample:
        return "the sample is empty";
    case StatisticsError::NotFinite:
        return "every value in the sample must be finite";
    case StatisticsError::Negative:
        return "no value in the sample may be negative";
    }
    return "unknown statistics error";
}

static_assert(describe(StatisticsError::EmptySample) != describe(StatisticsError::NotFinite),
              "each error says what is wrong");
static_assert(describe(StatisticsError::NotFinite) != describe(StatisticsError::Negative));

// The value a fraction `p` of the way up the sorted sample, by the convention
// above. The sample need not be sorted.
[[nodiscard]] std::expected<Seconds, StatisticsError> percentile(std::span<const Seconds> sample,
                                                                 Fraction p);

// The arithmetic mean, from a compensated sum.
[[nodiscard]] std::expected<Seconds, StatisticsError> mean(std::span<const Seconds> sample);

// What the benchmark prints for each of its clocks (M1-22).
struct FrameTimeSummary {
    Seconds mean;
    Seconds median;
    Seconds p95;
    Seconds p99;
    Seconds max;
};

// All five at once, from one sort.
[[nodiscard]] std::expected<FrameTimeSummary, StatisticsError>
summarise(std::span<const Seconds> sample);

} // namespace orb::view

#endif // ORBSIM_VIEW_FRAMESTATISTICS_HPP
