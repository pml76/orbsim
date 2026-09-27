//
// Tests for UT1: from UTC with DeltaUT1 (M1-05), and from TT with DeltaT
// (M1-86). Part of what was tests/test_time.cpp until 2026-09-27 (M1-94); what
// the time suites share is tests/TimeTestSupport.hpp.
//
#include "core/LeapSeconds.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>

using namespace orb;
using namespace orb::test;
#include "tests/TimeTestSupport.hpp"

using namespace orb::test::timesuite;

// A note on the readability-function-cognitive-complexity suppressions below.
//
// Catch2's REQUIRE and REQUIRE_THAT each expand to a do-while wrapping a
// try/catch, so a case scores roughly three points per assertion whether or
// not it branches at all. The score measures the framework, not the code.
// Ruled by the project owner on 2026-09-09 -- see test_orbit_scales.cpp for
// the alternatives that were measured -- and confirmed for this suite on
// 2026-09-10: one suppression per function, and only where the check fires.

// --- UT1 (M1-05) -------------------------------------------------------------
//
// UT1 = UTC + DeltaUT1. The sign is ITU-R TF.460-6 (2002), Annex 1 section D:
// DUT1 is approximately UT1 - UTC, "a correction to be added to UTC to obtain a
// better approximation to UT1". Every expected value below is an integer
// number of picoseconds worked out from that definition, not read from the
// code; the convention across a leap second is ERFA's eraUtcut1 (register
// decision 59), and the size limit is the same recommendation's 0.9 s
// (decision 60).

namespace {

// 0.9 s, from ITU-R TF.460-6 Annex 1 section D.1.2 -- "The departure of UTC
// from UT1 should not exceed +-0.9 s" -- written here rather than taken from
// the header, so that the header's number is checked against the source's.
constexpr std::int64_t kItuUt1Limit = 900'000'000'000;
static_assert(kDeltaUt1LimitPicoseconds == kItuUt1Limit);

// A tenth of a second in picoseconds, for the cases below.
constexpr std::int64_t kTenthOfASecond = 100'000'000'000;

// A UT1 instant at an exact stamp, through the public calendar, as taiAt does
// for TAI.
[[nodiscard]] Ut1Time ut1At(Stamp stamp) {
    CalendarDate date = dateOfMjd(stamp.mjd);
    const std::int64_t secondOfDay = stamp.picosecondOfDay / kSecond;
    const std::int64_t rest = stamp.picosecondOfDay % kSecond;
    date.hour = static_cast<std::int32_t>(secondOfDay / 3'600);
    date.minute = static_cast<std::int32_t>((secondOfDay / 60) % 60);
    date.second = Seconds{static_cast<f64>(secondOfDay % 60) +
                          (static_cast<f64>(rest) / detail::kPicosecondsPerSecondF)};
    const Ut1Time t = instant<TimeScale::Ut1>(date);
    INFO("the calendar built the UT1 instant intended");
    REQUIRE(t.picosecondOfDay() == stamp.picosecondOfDay);
    return t;
}

// A DeltaUT1 the test knows to be valid; a refusal is reported by name.
[[nodiscard]] DeltaUt1 validDeltaUt1(Seconds value) {
    const auto delta = DeltaUt1::fromSeconds(value);
    CAPTURE(value.value());
    INFO(errorName(delta));
    REQUIRE(delta.has_value());
    return *delta;
}

// An instant's two stored numbers against a stamp, exactly.
template <TimeScale Scale> void requireAt(const TimePoint<Scale>& t, Stamp stamp) {
    CAPTURE(t, stamp.mjd, stamp.picosecondOfDay);
    REQUIRE_THAT(t.modifiedJulianDay(), WithinAbsOf(static_cast<f64>(stamp.mjd), Tolerance{0.0}));
    REQUIRE(t.picosecondOfDay() == stamp.picosecondOfDay);
}

} // namespace

// With DeltaUT1 unmodelled, UT1 carries UTC's day and time of day unchanged, and
// the way back is exact. Over the same seeded dates as the calendar sweep; none
// lies inside a leap second, which UT1 has no label for (the case below).
TEST_CASE("with DeltaUT1 unmodelled, UT1 is UTC's day and time of day", "[time][ut1]") {
    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const UtcTime utc = instant<TimeScale::Utc>(drawDate(sampler));
        const Ut1Time ut1 = ut1FromUtc(utc, kDeltaUt1Unmodelled);
        CAPTURE(kSweepSeed, i, utc);
        requireAt(ut1,
                  {
                      .mjd = static_cast<std::int64_t>(utc.modifiedJulianDay()),
                      .picosecondOfDay = utc.picosecondOfDay(),
                  });
        REQUIRE(converted(utcFromUt1(ut1, kDeltaUt1Unmodelled)) == utc);
    }
}

// The sign, asserted against the definition: +0.3 s puts UT1 ahead of UTC by
// exactly 300 000 000 000 ps, -0.3 s behind it, and either carries across
// midnight rather than out of the day.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 = UTC + DeltaUT1, to the picosecond", "[time][ut1]") {
    constexpr std::int64_t kDayOne = 57'754; // 2017-01-01, which ends in no leap second
    constexpr std::int64_t kNoon = 43'200 * kSecond;
    const DeltaUt1 ahead = validDeltaUt1(Seconds{0.3});
    const DeltaUt1 behind = validDeltaUt1(Seconds{-0.3});
    REQUIRE(ahead.picoseconds() == 3 * kTenthOfASecond);
    REQUIRE(behind.picoseconds() == -3 * kTenthOfASecond);

    const UtcTime noon = utcAt({.mjd = kDayOne, .picosecondOfDay = kNoon});
    requireAt(ut1FromUtc(noon, ahead),
              {.mjd = kDayOne, .picosecondOfDay = kNoon + (3 * kTenthOfASecond)});
    requireAt(ut1FromUtc(noon, behind),
              {.mjd = kDayOne, .picosecondOfDay = kNoon - (3 * kTenthOfASecond)});

    // 23:59:59.9 UTC plus 0.3 s is 00:00:00.2 UT1 of the next day, and
    // 00:00:00.1 UTC less 0.3 s is 23:59:59.8 UT1 of the day before.
    const UtcTime late = utcAt({.mjd = kDayOne, .picosecondOfDay = kDay - kTenthOfASecond});
    requireAt(ut1FromUtc(late, ahead),
              {.mjd = kDayOne + 1, .picosecondOfDay = 2 * kTenthOfASecond});
    const UtcTime early = utcAt({.mjd = kDayOne + 1, .picosecondOfDay = kTenthOfASecond});
    requireAt(ut1FromUtc(early, behind),
              {.mjd = kDayOne, .picosecondOfDay = kDay - (2 * kTenthOfASecond)});

    // And every one of them back, exactly.
    REQUIRE(converted(utcFromUt1(ut1FromUtc(noon, ahead), ahead)) == noon);
    REQUIRE(converted(utcFromUt1(ut1FromUtc(noon, behind), behind)) == noon);
    REQUIRE(converted(utcFromUt1(ut1FromUtc(late, ahead), ahead)) == late);
    REQUIRE(converted(utcFromUt1(ut1FromUtc(early, behind), behind)) == early);
}

// Across a positive leap second (register decision 59). UT1 has no 23:59:60:
// 86 400.5 s into the UTC day is 0.5 s into the next UT1 day, and the way back
// lands on 00:00:00.5, a second later -- the forward map is two-to-one there.
// With a DeltaUT1 that steps by +1 s at the leap second, as the published
// series does, UT1 runs on through it; with DeltaUT1 unmodelled it steps back
// instead, which is the model error the header states.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 across a leap second runs on, and the way back lands a second later",
          "[time][ut1][leap]") {
    constexpr std::int64_t kLeapDay = 57'753; // 2016-12-31, which ends in 23:59:60
    constexpr std::int64_t kNextDay = kLeapDay + 1;
    constexpr std::int64_t kIntoLeapSecond = 86'400 * kSecond;

    const UtcTime inside =
        utcAt({.mjd = kLeapDay, .picosecondOfDay = kIntoLeapSecond + (5 * kTenthOfASecond)});
    const Ut1Time ut1 = ut1FromUtc(inside, kDeltaUt1Unmodelled);
    requireAt(ut1, {.mjd = kNextDay, .picosecondOfDay = 5 * kTenthOfASecond});
    const UtcTime back = converted(utcFromUt1(ut1, kDeltaUt1Unmodelled));
    REQUIRE(back == utcAt({.mjd = kNextDay, .picosecondOfDay = 5 * kTenthOfASecond}));

    // 23:59:60.9 with the leap day's own DeltaUT1, -0.4 s, and 00:00:00.0 with
    // the next day's, +0.6 s: a tenth of an SI second apart, and a tenth of a
    // second of UT1 apart.
    const UtcTime last =
        utcAt({.mjd = kLeapDay, .picosecondOfDay = kIntoLeapSecond + (9 * kTenthOfASecond)});
    const UtcTime midnight = utcAt({.mjd = kNextDay, .picosecondOfDay = 0});
    requireAt(ut1FromUtc(last, validDeltaUt1(Seconds{-0.4})),
              {.mjd = kNextDay, .picosecondOfDay = 5 * kTenthOfASecond});
    requireAt(ut1FromUtc(midnight, validDeltaUt1(Seconds{0.6})),
              {.mjd = kNextDay, .picosecondOfDay = 6 * kTenthOfASecond});

    // Unmodelled, the later instant has the earlier UT1.
    REQUIRE(ut1FromUtc(midnight, kDeltaUt1Unmodelled) < ut1FromUtc(last, kDeltaUt1Unmodelled));
}

// UT1 -> UTC -> UT1 is exact wherever UTC has the instant, which on the
// published table is everywhere: no negative leap second has occurred. DeltaUT1
// is drawn over its whole range, to the picosecond, and read back to the
// picosecond it was drawn as.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 to UTC and back is exact over a seeded sweep", "[time][ut1]") {
    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const Ut1Time ut1 = instant<TimeScale::Ut1>(drawDate(sampler));
        const std::int64_t drawn = sampler.between({.lo = -kItuUt1Limit, .hi = kItuUt1Limit});
        CAPTURE(kSweepSeed, i, ut1, drawn);
        const DeltaUt1 delta =
            validDeltaUt1(Seconds{static_cast<f64>(drawn) / detail::kPicosecondsPerSecondF});
        REQUIRE(delta.picoseconds() == drawn);
        const UtcTime utc = converted(utcFromUt1(ut1, delta));
        REQUIRE(ut1FromUtc(utc, delta) == ut1);
    }
}

// Every way a DeltaUT1 can be wrong, by name, and the edges that are right.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("DeltaUT1 is refused by name beyond 0.9 s and when not finite", "[time][ut1][errors]") {
    struct Refused {
        f64 seconds;
        TimeError error;
    };
    constexpr std::array kRefused = std::to_array<Refused>({
        {.seconds = kNaN, .error = TimeError::NotFinite},
        {.seconds = kInf, .error = TimeError::NotFinite},
        {.seconds = -kInf, .error = TimeError::NotFinite},
        {.seconds = 0.900000000001, .error = TimeError::DeltaUt1OutOfRange}, // 0.9 s + 1 ps
        {.seconds = -0.900000000001, .error = TimeError::DeltaUt1OutOfRange},
        {.seconds = 1.0, .error = TimeError::DeltaUt1OutOfRange},
        {.seconds = -1.0, .error = TimeError::DeltaUt1OutOfRange},
        {.seconds = 1e300, .error = TimeError::DeltaUt1OutOfRange},
        {.seconds = -1e300, .error = TimeError::DeltaUt1OutOfRange},
    });
    for (const Refused& r : kRefused) {
        const auto delta = DeltaUt1::fromSeconds(Seconds{r.seconds});
        CAPTURE(r.seconds);
        INFO(errorName(delta));
        REQUIRE(!delta.has_value());
        REQUIRE(delta.error() == r.error);
    }

    // The limit itself is inside it, both ways.
    REQUIRE(validDeltaUt1(Seconds{0.9}).picoseconds() == kItuUt1Limit);
    REQUIRE(validDeltaUt1(Seconds{-0.9}).picoseconds() == -kItuUt1Limit);
    // The bound is on the value held, which is the nearest picosecond: 0.4 ps
    // past the limit is the limit.
    REQUIRE(validDeltaUt1(Seconds{0.9000000000004}).picoseconds() == kItuUt1Limit);
    // And zero is zero, whichever zero.
    REQUIRE(validDeltaUt1(Seconds{-0.0}).picoseconds() == 0);
    REQUIRE(kDeltaUt1Unmodelled.picoseconds() == 0);
}

// A UTC instant stays inside the calendar that says how long its day is, so UT1
// within a second of either end can have no UTC, and says so by name.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 to UTC past either end of the calendar is YearOutOfRange", "[time][ut1][errors]") {
    const Ut1Time first =
        instant<TimeScale::Ut1>({.year = 1, .month = 1, .day = 1, .second = Seconds{0.1}});
    const auto before = utcFromUt1(first, validDeltaUt1(Seconds{0.3}));
    INFO("0001-01-01T00:00:00.1 UT1 less 0.3 s -> " << errorName(before));
    REQUIRE(!before.has_value());
    REQUIRE(before.error() == TimeError::YearOutOfRange);
    // 0.1 s less is the first instant there is.
    REQUIRE(converted(utcFromUt1(first, validDeltaUt1(Seconds{0.1}))) ==
            instant<TimeScale::Utc>({.year = 1, .month = 1, .day = 1}));

    const Ut1Time last = instant<TimeScale::Ut1>(
        {.year = 9999, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{59.9}});
    const auto after = utcFromUt1(last, validDeltaUt1(Seconds{-0.3}));
    INFO("9999-12-31T23:59:59.9 UT1 plus 0.3 s -> " << errorName(after));
    REQUIRE(!after.has_value());
    REQUIRE(after.error() == TimeError::YearOutOfRange);
    REQUIRE(utcFromUt1(last, validDeltaUt1(Seconds{-0.05})).has_value());
}

// **A negative leap second, which has never been inserted** (register decision
// 67). Its day holds 86 399 s, so under one DeltaUT1 the forward conversion
// skips a second of UT1: from 86 399 s + DeltaUT1 into the short day to
// DeltaUT1 into the next. No UTC instant has a UT1 in that window, and the way
// back says so by name. Driven through the table-taking overload with a
// synthetic table, as decision 33's queries are, because the published table
// cannot reach it -- and checked against the published table too, where the
// same day is ordinary and the same UT1 has its UTC.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 in the second a negative leap second removes has no UTC", "[time][ut1][leap]") {
    constexpr std::int64_t kShortDay = 60'000;
    constexpr auto kNegative = std::to_array<LeapSecondStep>({
        {.utcMjd = 59'000, .deltaAtSeconds = 37},
        {.utcMjd = kShortDay + 1, .deltaAtSeconds = 36},
    });
    const std::span<const LeapSecondStep> table{kNegative};
    REQUIRE(secondsInUtcDay(table, kShortDay) == 86'399);

    const DeltaUt1 delta = validDeltaUt1(Seconds{0.2});
    constexpr std::int64_t kGapBegins = (86'399 * kSecond) + (2 * kTenthOfASecond);
    constexpr std::int64_t kGapEnds = 2 * kTenthOfASecond; // into the next day

    for (const Stamp inside : {
             Stamp{.mjd = kShortDay, .picosecondOfDay = kGapBegins},
             Stamp{.mjd = kShortDay, .picosecondOfDay = kGapBegins + (5 * kTenthOfASecond)},
             Stamp{.mjd = kShortDay + 1, .picosecondOfDay = kGapEnds - 1},
         }) {
        const auto utc = utcFromUt1(table, ut1At(inside), delta);
        CAPTURE(inside.mjd, inside.picosecondOfDay);
        INFO(errorName(utc));
        REQUIRE(!utc.has_value());
        REQUIRE(utc.error() == TimeError::InsideRemovedLeapSecond);
    }

    // A picosecond either side of the window, each a UTC instant.
    const Ut1Time justBefore = ut1At({.mjd = kShortDay, .picosecondOfDay = kGapBegins - 1});
    requireAt(converted(utcFromUt1(table, justBefore, delta)),
              {.mjd = kShortDay, .picosecondOfDay = (86'399 * kSecond) - 1});
    const Ut1Time justAfter = ut1At({.mjd = kShortDay + 1, .picosecondOfDay = kGapEnds});
    requireAt(converted(utcFromUt1(table, justAfter, delta)),
              {.mjd = kShortDay + 1, .picosecondOfDay = 0});

    // On the published table MJD 60 000 is an ordinary day, and the same UT1
    // has a UTC: 23:59:59 of it.
    const Ut1Time gapStart = ut1At({.mjd = kShortDay, .picosecondOfDay = kGapBegins});
    requireAt(converted(utcFromUt1(gapStart, delta)),
              {.mjd = kShortDay, .picosecondOfDay = 86'399 * kSecond});
}

// --- UT1 from TT (M1-86) -----------------------------------------------------
//
// UT1 = TT - DeltaT, where DeltaT = TT - UT1 (register decision 72). Every
// expected value below is worked out from that definition, from TT - TAI =
// 32.184 s and from UT1 = UTC + DeltaUT1 -- with DeltaAT taken from the suite's
// own transcription of the published steps, kPublishedSteps, rather than from
// core/LeapSeconds.hpp -- and each is a whole number of picoseconds.

namespace {

// 10^6 s in picoseconds, typed here rather than taken from the header, so that
// the header's limit is checked against the number decision 72 states.
constexpr std::int64_t kDeltaTLimit = 1'000'000'000'000'000'000;
static_assert(kDeltaTLimitPicoseconds == kDeltaTLimit);

// TT - TAI, 32.184 s by definition (IAU 1991 Resolution A4), in picoseconds.
constexpr std::int64_t kTtMinusTai = 32'184'000'000'000;

// A TT instant at an exact stamp, through the public calendar, as taiAt does
// for TAI.
[[nodiscard]] TtTime ttAt(Stamp stamp) {
    CalendarDate date = dateOfMjd(stamp.mjd);
    const std::int64_t secondOfDay = stamp.picosecondOfDay / kSecond;
    const std::int64_t rest = stamp.picosecondOfDay % kSecond;
    date.hour = static_cast<std::int32_t>(secondOfDay / 3'600);
    date.minute = static_cast<std::int32_t>((secondOfDay / 60) % 60);
    date.second = Seconds{static_cast<f64>(secondOfDay % 60) +
                          (static_cast<f64>(rest) / detail::kPicosecondsPerSecondF)};
    const TtTime t = instant<TimeScale::Tt>(date);
    INFO("the calendar built the TT instant intended");
    REQUIRE(t.picosecondOfDay() == stamp.picosecondOfDay);
    return t;
}

// A DeltaT the test knows to be valid, from either factory; a refusal is
// reported by name.
[[nodiscard]] DeltaT validDeltaT(Seconds value) {
    const auto delta = DeltaT::fromSeconds(value);
    CAPTURE(value.value());
    INFO(errorName(delta));
    REQUIRE(delta.has_value());
    return *delta;
}

[[nodiscard]] DeltaT validDeltaTPicoseconds(std::int64_t picoseconds) {
    const auto delta = DeltaT::fromPicoseconds(picoseconds);
    CAPTURE(picoseconds);
    INFO(errorName(delta));
    REQUIRE(delta.has_value());
    return *delta;
}

// An instant as picoseconds since the midnight that begins day `originMjd`, for
// instants a few days from it, where the difference of two is the claim.
template <TimeScale Scale>
[[nodiscard]] std::int64_t picosecondsAfter(const TimePoint<Scale>& t, std::int64_t originMjd) {
    const std::int64_t days = static_cast<std::int64_t>(t.modifiedJulianDay()) - originMjd;
    return (days * kDay) + t.picosecondOfDay();
}

// A published step, and picoseconds from the UTC midnight that begins its day,
// as one parameter: an index and an offset are both integers, and transposed
// they would build a different instant in silence.
struct NearStep {
    std::size_t step;
    std::int64_t offset;
};

// The TT instant that far from that midnight. The midnight is DeltaAT seconds
// into the same TAI day, and 32.184 s later again in TT. Built from
// kPublishedSteps and the calendar, not from any conversion.
[[nodiscard]] TtTime ttAtStep(NearStep near) {
    const PublishedStep& step = kPublishedSteps.at(near.step);
    const std::int64_t midnight = (std::int64_t{step.deltaAtSeconds} * kSecond) + kTtMinusTai;
    return ttAt({.mjd = mjdOf(step), .picosecondOfDay = midnight + near.offset});
}

} // namespace

// The definition at every published step. Half a second after the step's UTC
// midnight DeltaAT is the new value; half a second before it the instant is
// inside the leap second, whose DeltaAT is still the old one; and a second and a
// half before it, the old one too. The first step has no "before".
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the table's DeltaT is 32.184 s + DeltaAT - DeltaUT1 either side of every step",
          "[time][ut1][deltat]") {
    constexpr std::int64_t kHalf = kSecond / 2;
    for (const f64 dut1 : {0.0, 0.3, -0.9}) {
        const DeltaUt1 delta = validDeltaUt1(Seconds{dut1});
        for (std::size_t i = 0; i < kPublishedSteps.size(); ++i) {
            const std::int64_t after = kPublishedSteps.at(i).deltaAtSeconds;
            CAPTURE(dut1, i, after);
            const DeltaT justAfter =
                converted(deltaTFromLeapSecondTable(ttAtStep({.step = i, .offset = kHalf}), delta));
            REQUIRE(justAfter.picoseconds() ==
                    kTtMinusTai + (after * kSecond) - delta.picoseconds());
            if (i == 0) continue;
            const std::int64_t before = kPublishedSteps.at(i - 1).deltaAtSeconds;
            CAPTURE(before);
            for (const std::int64_t offset : {-kHalf, -(3 * kHalf)}) {
                CAPTURE(offset);
                const DeltaT earlier = converted(
                    deltaTFromLeapSecondTable(ttAtStep({.step = i, .offset = offset}), delta));
                REQUIRE(earlier.picoseconds() ==
                        kTtMinusTai + (before * kSecond) - delta.picoseconds());
            }
        }
    }
}

// The claim that makes the new road safe to take: wherever the table holds, UT1
// from the table's DeltaT is the UTC road's UT1 to the picosecond. Over a seeded
// sweep of 1972-2026 with DeltaUT1 drawn over its whole range, and through every
// leap second, a quarter of a second at a time.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 from the table's DeltaT is the UTC road's UT1 to the picosecond",
          "[time][ut1][deltat]") {
    const auto requireSameRoad = [](TtTime tt, DeltaUt1 delta) {
        CAPTURE(tt, delta.picoseconds());
        const DeltaT deltaT = converted(deltaTFromLeapSecondTable(tt, delta));
        const Ut1Time viaDeltaT = ut1FromTt(tt, deltaT);
        const Ut1Time viaUtc = ut1FromUtc(converted(utcFromTt(tt)), delta);
        CAPTURE(viaDeltaT, viaUtc);
        REQUIRE(viaDeltaT == viaUtc);
        REQUIRE(isNormalised(viaDeltaT));
    };

    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        // A TT day after the era begins and before the table expires.
        const std::int64_t mjd = sampler.between(
            {.lo = kLeapSecondEraFirstMjd + 1, .hi = kLeapSecondTableExpiryMjd - 2});
        const std::int64_t picos = sampler.between({.lo = 0, .hi = kDay - 1});
        const std::int64_t drawn = sampler.between({.lo = -kItuUt1Limit, .hi = kItuUt1Limit});
        CAPTURE(kSweepSeed, i);
        requireSameRoad(
            ttAt({.mjd = mjd, .picosecondOfDay = picos}),
            validDeltaUt1(Seconds{static_cast<f64>(drawn) / detail::kPicosecondsPerSecondF}));
    }

    constexpr std::int64_t kQuarter = kSecond / 4;
    for (std::size_t i = 1; i < kPublishedSteps.size(); ++i) {
        for (std::int64_t offset = -12 * kQuarter; offset <= 12 * kQuarter; offset += kQuarter) {
            CAPTURE(i, offset);
            requireSameRoad(ttAtStep({.step = i, .offset = offset}), validDeltaUt1(Seconds{-0.4}));
        }
    }
}

// The sign, against the definition: UT1 = TT - DeltaT. Carried across midnight
// both ways, and across days for a DeltaT of days -- which no model gives this
// century, and the long-term parabola does near the calendar's far end. DeltaT
// was negative from about 1873 to 1901 -- -2.0 s at 1900-01-01 by the Morrison
// et al. (2021) splines, as Skyfield 1.55 carries them -- so a negative one is an
// ordinary input rather than an error.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UT1 = TT - DeltaT to the picosecond", "[time][ut1][deltat]") {
    constexpr std::int64_t kDayOne = 57'754; // 2017-01-01
    constexpr std::int64_t kNoon = 43'200 * kSecond;
    const DeltaT modern = validDeltaT(Seconds{69.184});
    REQUIRE(modern.picoseconds() == 69'184'000'000'000);

    struct Case {
        std::string_view name;
        Stamp tt;
        DeltaT deltaT;
        Stamp ut1;
    };
    const auto cases = std::to_array<Case>({
        {
            .name = "noon less 69.184 s",
            .tt = {.mjd = kDayOne, .picosecondOfDay = kNoon},
            .deltaT = modern,
            .ut1 = {.mjd = kDayOne, .picosecondOfDay = kNoon - 69'184'000'000'000},
        },
        {
            .name = "00:00:30 less 69.184 s is the day before",
            .tt = {.mjd = kDayOne, .picosecondOfDay = 30 * kSecond},
            .deltaT = modern,
            .ut1 = {.mjd = kDayOne - 1, .picosecondOfDay = kDay - 39'184'000'000'000},
        },
        {
            .name = "23:59:59 less -2.5 s is the day after",
            .tt = {.mjd = kDayOne, .picosecondOfDay = kDay - kSecond},
            .deltaT = validDeltaT(Seconds{-2.5}),
            .ut1 = {.mjd = kDayOne + 1, .picosecondOfDay = 1'500'000'000'000},
        },
        {
            .name = "noon less two and a half days",
            .tt = {.mjd = kDayOne, .picosecondOfDay = kNoon},
            .deltaT = validDeltaT(Seconds{216'000.0}),
            .ut1 = {.mjd = kDayOne - 2, .picosecondOfDay = 0},
        },
        {
            // 10^6 s is 11 days and 49 600 s, and 43 200 + 49 600 s is a day
            // and 6 400 s.
            .name = "J2000.0 TT less minus the limit",
            .tt = {.mjd = 51'544, .picosecondOfDay = kNoon},
            .deltaT = validDeltaTPicoseconds(-kDeltaTLimit),
            .ut1 = {.mjd = 51'544 + 12, .picosecondOfDay = 6'400 * kSecond},
        },
    });
    for (const Case& c : cases) {
        CAPTURE(c.name);
        const TtTime tt = ttAt(c.tt);
        const Ut1Time ut1 = ut1FromTt(tt, c.deltaT);
        requireAt(ut1, c.ut1);
        REQUIRE(ttFromUt1(ut1, c.deltaT) == tt);
    }
}

// Both round trips are exact, bit for bit: the arithmetic is whole picoseconds,
// so anything short of identity is a defect and not a rounding. DeltaT is drawn
// to the picosecond over its whole range, days of it included.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("TT to UT1 and back is exact both ways with DeltaT anywhere in its range",
          "[time][ut1][deltat]") {
    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const CalendarDate date = drawDate(sampler);
        const DeltaT deltaT =
            validDeltaTPicoseconds(sampler.between({.lo = -kDeltaTLimit, .hi = kDeltaTLimit}));
        const TtTime tt = instant<TimeScale::Tt>(date);
        const Ut1Time ut1 = instant<TimeScale::Ut1>(date);
        CAPTURE(kSweepSeed, i, tt, deltaT.picoseconds());

        const Ut1Time there = ut1FromTt(tt, deltaT);
        REQUIRE(isNormalised(there));
        REQUIRE(ttFromUt1(there, deltaT) == tt);

        const TtTime back = ttFromUt1(ut1, deltaT);
        REQUIRE(isNormalised(back));
        REQUIRE(ut1FromTt(back, deltaT) == ut1);
    }
}

// The reason for the new road, asserted. Through the leap second at the end of
// 2016, a tenth of a second at a time: with DeltaT held, UT1 advances by exactly
// the TT step every time, because the Earth does not know that UTC has a leap
// second. The UTC road with DeltaUT1 unmodelled does not: it runs one second of
// UT1 twice and steps back at the UTC midnight after the leap second, which is
// the model error core/Time.hpp states for it.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a held DeltaT turns UT1 uniformly through a leap second where the UTC road steps back",
          "[time][ut1][deltat][leap]") {
    constexpr std::int64_t kDayOne = 57'754; // 2017-01-01; its UTC midnight is TT 00:01:09.184
    constexpr std::int64_t kTenth = kSecond / 10;
    constexpr std::int64_t kFirst = 66 * kSecond; // TT 00:01:06 is UTC 23:59:57.816 the day before
    constexpr std::int64_t kLast = 72 * kSecond;  // TT 00:01:12 is UTC 00:00:02.816

    const DeltaT held = converted(deltaTFromLeapSecondTable(
        ttAt({.mjd = kDayOne, .picosecondOfDay = kFirst}), kDeltaUt1Unmodelled));
    REQUIRE(held.picoseconds() == 68'184'000'000'000); // 32.184 + 36, before the step

    std::int64_t steppedBack = 0;
    for (std::int64_t at = kFirst; at < kLast; at += kTenth) {
        const TtTime now = ttAt({.mjd = kDayOne, .picosecondOfDay = at});
        const TtTime next = ttAt({.mjd = kDayOne, .picosecondOfDay = at + kTenth});
        CAPTURE(at);
        REQUIRE(picosecondsAfter(ut1FromTt(next, held), kDayOne - 1) -
                    picosecondsAfter(ut1FromTt(now, held), kDayOne - 1) ==
                kTenth);

        const Ut1Time roadNow = ut1FromUtc(converted(utcFromTt(now)), kDeltaUt1Unmodelled);
        const Ut1Time roadNext = ut1FromUtc(converted(utcFromTt(next)), kDeltaUt1Unmodelled);
        if (roadNext < roadNow) ++steppedBack;
    }
    INFO("the UTC road with DeltaUT1 unmodelled steps back once, at UTC midnight");
    REQUIRE(steppedBack == 1);
}

// Every way a DeltaT can be wrong, by name, and the edges that are right.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("DeltaT is refused by name beyond 10^6 s and when not finite",
          "[time][ut1][deltat][errors]") {
    const f64 pastTheLimit = std::nextafter(1e6, 2e6);
    struct Refused {
        f64 seconds;
        TimeError error;
    };
    const auto refused = std::to_array<Refused>({
        {.seconds = kNaN, .error = TimeError::NotFinite},
        {.seconds = kInf, .error = TimeError::NotFinite},
        {.seconds = -kInf, .error = TimeError::NotFinite},
        {.seconds = pastTheLimit, .error = TimeError::DeltaTOutOfRange},
        {.seconds = -pastTheLimit, .error = TimeError::DeltaTOutOfRange},
        {.seconds = 1e300, .error = TimeError::DeltaTOutOfRange},
        {.seconds = -1e300, .error = TimeError::DeltaTOutOfRange},
    });
    for (const Refused& r : refused) {
        const auto delta = DeltaT::fromSeconds(Seconds{r.seconds});
        CAPTURE(r.seconds);
        INFO(errorName(delta));
        REQUIRE(!delta.has_value());
        REQUIRE(delta.error() == r.error);
    }
    for (const std::int64_t picoseconds : {
             kDeltaTLimit + 1,
             -kDeltaTLimit - 1,
             std::numeric_limits<std::int64_t>::max(),
             std::numeric_limits<std::int64_t>::min(),
         }) {
        const auto delta = DeltaT::fromPicoseconds(picoseconds);
        CAPTURE(picoseconds);
        INFO(errorName(delta));
        REQUIRE(!delta.has_value());
        REQUIRE(delta.error() == TimeError::DeltaTOutOfRange);
    }

    // The limit itself is inside it, from either factory and on either side.
    REQUIRE(validDeltaT(Seconds{1e6}).picoseconds() == kDeltaTLimit);
    REQUIRE(validDeltaT(Seconds{-1e6}).picoseconds() == -kDeltaTLimit);
    REQUIRE(validDeltaTPicoseconds(kDeltaTLimit).picoseconds() == kDeltaTLimit);
    REQUIRE(validDeltaTPicoseconds(-kDeltaTLimit).picoseconds() == -kDeltaTLimit);

    // The nearest picosecond, whatever the size. 0.1 s is not a binary
    // fraction; 69.2138671875 s is 420 / 2^19 of a day, exactly; and 2^17 s +
    // 2^-35 s is 29.1 ps past a whole number of seconds -- which scaling the
    // double by 10^12 would lose, since a double near 1.3e17 resolves only 16.
    REQUIRE(validDeltaT(Seconds{0.1}).picoseconds() == 100'000'000'000);
    REQUIRE(validDeltaT(Seconds{69.2138671875}).picoseconds() == 69'213'867'187'500);
    REQUIRE(validDeltaT(Seconds{-2.5}).picoseconds() == -2'500'000'000'000);
    REQUIRE(validDeltaT(Seconds{0x1p17 + 0x1p-35}).picoseconds() == 131'072'000'000'000'029);
    REQUIRE(validDeltaT(Seconds{-0x1p17 - 0x1p-35}).picoseconds() == -131'072'000'000'000'029);
    REQUIRE(validDeltaT(Seconds{-0.0}).picoseconds() == 0);
}

// The table has no DeltaAT before 1972-01-01 or from 2027-01-01, and says so by
// name, a picosecond either side of each edge. UTC's first instant is 00:00:10
// TAI, which is 00:00:42.184 TT; the expiry, UTC 2027-01-01T00:00:00, is
// 00:01:09.184 TT.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the table's DeltaT is refused by name where the table has none",
          "[time][ut1][deltat][errors]") {
    constexpr std::int64_t kEraBegins = 42'184'000'000'000;
    constexpr std::int64_t kExpires = 69'184'000'000'000;
    constexpr std::int64_t kExpiryMjd = 61'406; // 2027-01-01
    struct Refused {
        std::string_view name;
        Stamp tt;
        TimeError error;
    };
    const auto refused = std::to_array<Refused>({
        {
            .name = "the Apollo 11 landing, 1969-07-20T20:17:40 TT",
            .tt = {.mjd = 40'422, .picosecondOfDay = 73'060 * kSecond},
            .error = TimeError::BeforeLeapSecondEra,
        },
        {
            .name = "a picosecond before UTC begins",
            .tt = {.mjd = kLeapSecondEraFirstMjd, .picosecondOfDay = kEraBegins - 1},
            .error = TimeError::BeforeLeapSecondEra,
        },
        {
            .name = "the first instant past Bulletin C 72",
            .tt = {.mjd = kExpiryMjd, .picosecondOfDay = kExpires},
            .error = TimeError::LeapSecondTableExpired,
        },
        {
            .name = "2050-01-01",
            .tt = {.mjd = 69'807, .picosecondOfDay = 0},
            .error = TimeError::LeapSecondTableExpired,
        },
    });
    for (const Refused& r : refused) {
        const auto deltaT = deltaTFromLeapSecondTable(ttAt(r.tt), kDeltaUt1Unmodelled);
        CAPTURE(r.name);
        INFO(errorName(deltaT));
        REQUIRE(!deltaT.has_value());
        REQUIRE(deltaT.error() == r.error);
    }

    // The instant just inside each edge has one: 32.184 + 10 s and 32.184 + 37 s.
    const DeltaT first = converted(deltaTFromLeapSecondTable(
        ttAt({.mjd = kLeapSecondEraFirstMjd, .picosecondOfDay = kEraBegins}), kDeltaUt1Unmodelled));
    REQUIRE(first.picoseconds() == kTtMinusTai + (10 * kSecond));
    const DeltaT last = converted(deltaTFromLeapSecondTable(
        ttAt({.mjd = kExpiryMjd, .picosecondOfDay = kExpires - 1}), kDeltaUt1Unmodelled));
    REQUIRE(last.picoseconds() == kTtMinusTai + (37 * kSecond));
}

// The DeltaT a caller takes past the expiry is the table's last with DeltaUT1
// unmodelled: 32.184 s plus the last published DeltaAT, from the suite's own
// transcription.
TEST_CASE("the held DeltaT is the table's last step with DeltaUT1 unmodelled",
          "[time][ut1][deltat]") {
    const std::int64_t lastDeltaAt = kPublishedSteps.back().deltaAtSeconds;
    REQUIRE(kDeltaTHeldAtTableExpiry.picoseconds() == kTtMinusTai + (lastDeltaAt * kSecond));
    REQUIRE(kDeltaTHeldAtTableExpiry.picoseconds() == 69'184'000'000'000);
}
