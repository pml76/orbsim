#include "view/GpuClock.hpp" // SF.5: own header, first
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <cstdint>
#include <expected>
#include <limits>

namespace orb::view {

namespace {

// The bits a timestamp holds, and so the most a device may say are valid.
constexpr std::uint32_t kTimestampBits = 64;

// Nanoseconds in a second, for the one division that turns ticks into seconds:
// a whole number a double holds exactly, so the result is the product
// correctly rounded once more.
constexpr f64 kNanosecondsPerSecond = 1e9;

// The low `bits` bits set. A shift by 64 is undefined, so the full counter is
// its own case.
[[nodiscard]] constexpr std::uint64_t maskOf(std::uint32_t bits) noexcept {
    if (bits >= kTimestampBits) return std::numeric_limits<std::uint64_t>::max();
    return (std::uint64_t{1} << bits) - 1U;
}

static_assert(maskOf(36) == 0xF'FFFF'FFFFU);
static_assert(maskOf(64) == 0xFFFF'FFFF'FFFF'FFFFU);
static_assert(maskOf(1) == 1U);

} // namespace

GpuClock::GpuClock(std::uint64_t mask, const GpuClockFacts& facts) noexcept
    : mask_(mask), validBits_(facts.validBits), nanosecondsPerTick_(facts.nanosecondsPerTick) {}

std::expected<GpuClock, GpuClockError> GpuClock::from(const GpuClockFacts& facts) {
    if (facts.validBits == 0) return std::unexpected(GpuClockError::NoTimestamps);
    if (facts.validBits > kTimestampBits) return std::unexpected(GpuClockError::TooManyBits);
    // Written so that a NaN fails it: NaN > 0 is false.
    if (!(facts.nanosecondsPerTick > 0.0) || !isFinite(facts.nanosecondsPerTick)) {
        return std::unexpected(GpuClockError::TickNotPositive);
    }
    return GpuClock{maskOf(facts.validBits), facts};
}

Seconds GpuClock::elapsed(const TimestampPair& ticks) const noexcept {
    // Unsigned subtraction is modulo 2^64, and masking it gives the
    // difference modulo 2^validBits: exact across one wrap of the counter.
    const std::uint64_t counted = (ticks.end - ticks.begin) & mask_;
    return Seconds{(static_cast<f64>(counted) * nanosecondsPerTick_) / kNanosecondsPerSecond};
}

} // namespace orb::view
