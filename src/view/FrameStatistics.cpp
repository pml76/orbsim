#include "view/FrameStatistics.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

namespace orb::view {

namespace {

// What is wrong with the sample, if anything: emptiness first, then each
// value in order, so a sample with both a NaN and a negative reports the one
// that comes first.
[[nodiscard]] std::optional<StatisticsError> faultIn(std::span<const Seconds> sample) noexcept {
    if (sample.empty()) return StatisticsError::EmptySample;
    for (const Seconds value : sample) {
        if (!isFinite(value.value())) return StatisticsError::NotFinite;
        // Negative zero passes: -0.0 < 0.0 is false, and what it holds is zero.
        if (value.value() < 0.0) return StatisticsError::Negative;
    }
    return std::nullopt;
}

// The sample's values, sorted. Only after faultIn has passed, so there is no
// NaN to leave the order undefined.
[[nodiscard]] std::vector<f64> sortedValues(std::span<const Seconds> sample) {
    std::vector<f64> values;
    values.reserve(sample.size());
    std::ranges::transform(sample, std::back_inserter(values), [](Seconds s) { return s.value(); });
    std::ranges::sort(values);
    return values;
}

// The type-7 percentile of a sorted, valid, non-empty sample (the header has
// the definition). The interpolation is written x[k] + (h - k)(x[k+1] - x[k])
// rather than as a weighted average of the two, so that between two equal
// values it is that value bit for bit, and at h = k exactly it is x[k].
[[nodiscard]] f64 percentileOfSorted(const std::vector<f64>& sorted, Fraction p) {
    ORBSIM_EXPECTS(!sorted.empty());
    // n - 1 is exact as a double for any sample that fits in memory, and p
    // is at most 1, so h is at most n - 1 and k is a valid index.
    const f64 h = static_cast<f64>(sorted.size() - 1) * p.value();
    const f64 below = std::floor(h);
    const auto k = static_cast<std::size_t>(below);
    ORBSIM_ENSURES(k < sorted.size());
    const f64 lower = sorted.at(k);
    const f64 upper = sorted.at(std::min(k + 1, sorted.size() - 1));
    return lower + ((h - below) * (upper - lower));
}

// Neumaier's compensated sum: the error of each addition is recovered exactly
// -- by whichever of the two operands is the larger, which is the difference
// from Kahan's -- and added back once, at the end.
[[nodiscard]] f64 compensatedSum(std::span<const Seconds> sample) noexcept {
    f64 sum = 0.0;
    f64 compensation = 0.0;
    for (const Seconds value : sample) {
        const f64 x = value.value();
        const f64 next = sum + x;
        compensation += std::abs(sum) >= std::abs(x) ? (sum - next) + x : (x - next) + sum;
        sum = next;
    }
    return sum + compensation;
}

} // namespace

std::expected<Seconds, StatisticsError> percentile(std::span<const Seconds> sample, Fraction p) {
    if (const auto fault = faultIn(sample)) return std::unexpected(*fault);
    return Seconds{percentileOfSorted(sortedValues(sample), p)};
}

std::expected<Seconds, StatisticsError> mean(std::span<const Seconds> sample) {
    if (const auto fault = faultIn(sample)) return std::unexpected(*fault);
    return Seconds{compensatedSum(sample) / static_cast<f64>(sample.size())};
}

std::expected<FrameTimeSummary, StatisticsError> summarise(std::span<const Seconds> sample) {
    if (const auto fault = faultIn(sample)) return std::unexpected(*fault);
    const std::vector<f64> sorted = sortedValues(sample);
    return FrameTimeSummary{
        .mean = Seconds{compensatedSum(sample) / static_cast<f64>(sample.size())},
        .median = Seconds{percentileOfSorted(sorted, fraction(0.5))},
        .p95 = Seconds{percentileOfSorted(sorted, fraction(0.95))},
        .p99 = Seconds{percentileOfSorted(sorted, fraction(0.99))},
        .max = Seconds{sorted.back()},
    };
}

} // namespace orb::view
