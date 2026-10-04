#ifndef ORBSIM_VIEW_GPUCLOCK_HPP
#define ORBSIM_VIEW_GPUCLOCK_HPP
//
// A GPU's timestamp clock, and the time between two of its readings (M1-22).
//
// A Vulkan timestamp query records a counter, in ticks, when the GPU reaches a
// point in a command buffer. Turning two of them into a duration needs two
// facts about the device: **how long a tick is** -- `timestampPeriod`, in
// nanoseconds, 10 on the RX 7900 XTX and 1 on the RTX A2000 -- and **how many
// of the counter's 64 bits are real**, `timestampValidBits`, which the
// specification allows to be as few as 36. A counter that narrow wraps round
// after 2^36 ticks, about 11.5 minutes at 10 ns, so a frame can begin before
// the wrap and end after it. The difference is therefore taken modulo
// 2^validBits, which is exact for any frame shorter than a full wrap.
//
// Vulkan-free, like everything in orbsim_view, so tests/test_gpu_clock.cpp
// can check the arithmetic without a device: the renderer reads the two facts
// from the device and hands them here.
//
// **A clock that cannot be one is refused by name**: a queue with no valid
// bits does not time anything, more than 64 bits is not a counter Vulkan
// describes, and a tick that is not a positive finite length gives no
// duration. Reported rather than asserted, because they are facts about a
// device, which this program does not control.
//
#include "core/Units.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace orb::view {

enum class GpuClockError : std::uint8_t {
    NoTimestamps,    // the queue's counter has no valid bits
    TooManyBits,     // more than the 64 a timestamp holds
    TickNotPositive, // a tick of zero, negative, infinite or no length
};

[[nodiscard]] constexpr std::string_view describe(GpuClockError error) noexcept {
    switch (error) {
    case GpuClockError::NoTimestamps:
        return "the graphics queue cannot record timestamps";
    case GpuClockError::TooManyBits:
        return "a timestamp has at most 64 valid bits";
    case GpuClockError::TickNotPositive:
        return "the timestamp period must be a positive, finite number of nanoseconds";
    }
    return "unknown GPU clock error";
}

static_assert(describe(GpuClockError::NoTimestamps) != describe(GpuClockError::TooManyBits),
              "each error says what is wrong");
static_assert(describe(GpuClockError::TooManyBits) != describe(GpuClockError::TickNotPositive));

// The two facts, as the device states them. By name, so the count of bits
// and the length of a tick cannot be handed over the wrong way round
// (non-negotiable 1).
struct GpuClockFacts {
    std::uint32_t validBits{};
    f64 nanosecondsPerTick{};
};

// Two readings of the counter: where a frame's work began and where it ended.
struct TimestampPair {
    std::uint64_t begin{};
    std::uint64_t end{};
};

class GpuClock {
public:
    [[nodiscard]] static std::expected<GpuClock, GpuClockError> from(const GpuClockFacts& facts);

    // The time from `ticks.begin` to `ticks.end`, modulo the counter's width.
    [[nodiscard]] Seconds elapsed(const TimestampPair& ticks) const noexcept;

    [[nodiscard]] std::uint32_t validBits() const noexcept { return validBits_; }
    [[nodiscard]] f64 nanosecondsPerTick() const noexcept { return nanosecondsPerTick_; }

private:
    GpuClock(std::uint64_t mask, const GpuClockFacts& facts) noexcept;

    std::uint64_t mask_;
    std::uint32_t validBits_;
    f64 nanosecondsPerTick_;
};

} // namespace orb::view

#endif // ORBSIM_VIEW_GPUCLOCK_HPP
