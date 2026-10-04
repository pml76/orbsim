//
// Tests for view/GpuClock.hpp: GPU timestamp ticks into seconds (M1-22).
//
// The expected durations are worked by hand -- a whole number of ticks times
// a tick length, divided by 1e9 -- and chosen so that the answer is the
// double nearest a short decimal, which is then asserted bit for bit. The
// wrap-round cases put the two readings either side of the counter's top,
// where a plain subtraction gives a number near 2^64 instead of a few ticks.
//
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/GpuClock.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

using namespace orb;
using namespace orb::view;

namespace {

[[nodiscard]] GpuClock clockOf(const GpuClockFacts& facts) {
    const auto clock = GpuClock::from(facts);
    REQUIRE(clock.has_value());
    return *clock;
}

[[nodiscard]] bool sameBits(Seconds got, f64 want) { return got.bitIdentical(Seconds{want}); }

constexpr std::uint64_t kTop64 = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kTop36 = (std::uint64_t{1} << 36U) - 1U;

} // namespace

TEST_CASE("ticks become seconds") {
    // The RX 7900 XTX's clock: 64 bits, 10 ns a tick. 1,000 ticks is 10 us.
    const GpuClock amd = clockOf({.validBits = 64, .nanosecondsPerTick = 10.0});
    CHECK(sameBits(amd.elapsed({.begin = 1'000, .end = 2'000}), 1e-5));
    CHECK(sameBits(amd.elapsed({.begin = 5, .end = 5}), 0.0));
    // The RTX A2000's: 1 ns a tick. 16,666,667 ticks is a 60 Hz frame.
    const GpuClock nvidia = clockOf({.validBits = 64, .nanosecondsPerTick = 1.0});
    CHECK(sameBits(nvidia.elapsed({.begin = 7, .end = 16'666'674}), 0.016666667));
}

TEST_CASE("a tick that is not a whole number of nanoseconds") {
    // Intel's integrated GPUs have reported 52.083333 ns (a 19.2 MHz clock),
    // as the nearest float, which is what the device hands over. 96 ticks
    // of exactly 1/19.2 MHz are 5,000 ns; of the float, 4,999.99988 ns,
    // 2.4e-8 short -- so within 1e-7 of 5 us, and the float's own product.
    const auto period = static_cast<f64>(52.083333F);
    const GpuClock intel = clockOf({.validBits = 36, .nanosecondsPerTick = period});
    const f64 got = intel.elapsed({.begin = 1'000, .end = 1'096}).value();
    CHECK(std::abs(got - 5e-6) <= 1e-7 * 5e-6);
    CHECK(sameBits(Seconds{got}, (96.0 * period) / 1e9));
}

TEST_CASE("a counter that wraps round between the two readings") {
    // Ten ticks before the top of a 36-bit counter, and five after it: 15
    // ticks, where a plain 64-bit subtraction would give about 1.8e19.
    const GpuClock narrow = clockOf({.validBits = 36, .nanosecondsPerTick = 10.0});
    CHECK(sameBits(narrow.elapsed({.begin = kTop36 - 9U, .end = 5}), 1.5e-7));
    // The same at the top of a full 64-bit counter.
    const GpuClock wide = clockOf({.validBits = 64, .nanosecondsPerTick = 10.0});
    CHECK(sameBits(wide.elapsed({.begin = kTop64 - 9U, .end = 5}), 1.5e-7));
    // And not when there is no wrap: the narrow clock's mask must not cut a
    // difference that fits.
    CHECK(sameBits(narrow.elapsed({.begin = 0, .end = kTop36}), 687.19476735));
}

TEST_CASE("a clock that cannot be one is reported") {
    struct Case {
        GpuClockFacts facts{};
        GpuClockError error{};
    };
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    const f64 inf = std::numeric_limits<f64>::infinity();
    for (const Case& c : {
             Case{
                 .facts = {.validBits = 0, .nanosecondsPerTick = 10.0},
                 .error = GpuClockError::NoTimestamps,
             },
             Case{
                 .facts = {.validBits = 65, .nanosecondsPerTick = 10.0},
                 .error = GpuClockError::TooManyBits,
             },
             Case{
                 .facts = {.validBits = 64, .nanosecondsPerTick = 0.0},
                 .error = GpuClockError::TickNotPositive,
             },
             Case{
                 .facts = {.validBits = 64, .nanosecondsPerTick = -1.0},
                 .error = GpuClockError::TickNotPositive,
             },
             Case{
                 .facts = {.validBits = 64, .nanosecondsPerTick = nan},
                 .error = GpuClockError::TickNotPositive,
             },
             Case{
                 .facts = {.validBits = 64, .nanosecondsPerTick = inf},
                 .error = GpuClockError::TickNotPositive,
             },
         }) {
        INFO("valid bits " << c.facts.validBits << ", " << c.facts.nanosecondsPerTick
                           << " ns a tick");
        const auto clock = GpuClock::from(c.facts);
        // REQUIRE(!x), for the reason tests/test_projection.cpp gives.
        REQUIRE(!clock.has_value());
        CHECK(clock.error() == c.error);
    }
}

TEST_CASE("the narrowest and widest counters are accepted") {
    CHECK(GpuClock::from({.validBits = 1, .nanosecondsPerTick = 10.0}).has_value());
    CHECK(GpuClock::from({.validBits = 64, .nanosecondsPerTick = 10.0}).has_value());
}
