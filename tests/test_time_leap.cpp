//
// Tests for core/LeapSeconds.hpp and the UTC, TAI and TT conversions (M1-04).
// Part of what was tests/test_time.cpp until 2026-09-27 (M1-94); what the time
// suites share is tests/TimeTestSupport.hpp.
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
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

// --- the leap-second table (M1-04) ------------------------------------------
//
// Nothing below is checked against the table it tests. The expected values come
// from three places, each independent of core/LeapSeconds.hpp:
//
//   * the IERS/IANA `leap-seconds.list`, transcribed a second time and in a
//     different form -- a calendar date where the header holds an MJD, so the
//     conversion between them is checked rather than repeated;
//   * **std::chrono's leap seconds**, which come from the platform's own
//     tzdata -- a separate transcription of the same IERS bulletins, by people
//     who have never seen this project. Measured on 2026-09-18 before being
//     relied on: usable on all four toolchains, agreeing on all 28 rows;
//   * integer arithmetic, which is exact, for the claim that the conversions
//     lose nothing at all.
//
// The budget is M1-04's: the UTC <-> TAI <-> TT round trip exact to 1e-9 s,
// wherever the table is valid. What the design guarantees beneath that budget
// is **bit identity**, because every step is a whole number of seconds and of
// picoseconds, and that is what the sweeps assert -- a budget a thousand times
// looser than the arithmetic cannot see a regression in it.

// The MJD in the header and the date in the IERS file name the same day, on a
// calendar that is in neither of them.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the leap-second table is the published one", "[time][leap]") {
    REQUIRE(kIersLeapSeconds.size() == kPublishedSteps.size());
    for (std::size_t i = 0; i < kPublishedSteps.size(); ++i) {
        const PublishedStep& published = kPublishedSteps.at(i);
        CAPTURE(i, published.year, published.month, published.day);
        REQUIRE(kIersLeapSeconds.at(i).utcMjd == mjdOf(published));
        REQUIRE(kIersLeapSeconds.at(i).deltaAtSeconds == published.deltaAtSeconds);
    }

    // The two boundaries, as dates rather than as the MJDs the header holds.
    REQUIRE(kLeapSecondEraFirstMjd ==
            mjdByTheStandardLibrary(civilOf({.year = 1972, .month = 1, .day = 1})));
    REQUIRE(kLeapSecondTableExpiryMjd ==
            mjdByTheStandardLibrary(civilOf({.year = 2027, .month = 1, .day = 1})));

    // IERS Bulletin C 72, 2026-07-06: UTC-TAI = -37 s until further notice.
    REQUIRE(kIersLeapSeconds.back().deltaAtSeconds == 37);
}

// **The independent oracle.** std::chrono's leap seconds come from the
// platform's tzdata, transcribed from the same IERS bulletins by people with no
// connection to this project. Its `elapsed` counts insertions from 1970, so
// DeltaAT is 10 + elapsed -- the 10 being the offset UTC started the era with
// rather than a leap second, which is why the counts differ by one.
//
// It reports itself skipped, loudly, if the platform carries no leap-second
// list. A machine without one must not turn this into a silent pass, which is
// ADR 0005's "a step that has silently been doing nothing".
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the leap-second table agrees with the standard library's", "[time][leap]") {
    std::size_t listed = 0;
    try {
        listed = std::chrono::get_tzdb().leap_seconds.size();
    } catch (const std::exception& e) {
        SKIP("this platform carries no tzdata leap-second list, so the independent oracle "
             "for core/LeapSeconds.hpp cannot run here: "
             << e.what());
    }
    INFO("tzdata lists " << listed << " leap-second insertions");
    REQUIRE(listed + 1 == kIersLeapSeconds.size());

    // Every day of the era, not only the steps: a table wrong between two
    // boundaries is a table wrong for years at a time.
    std::int64_t checked = 0;
    for (std::int64_t mjd = kLeapSecondEraFirstMjd; mjd < kLeapSecondTableExpiryMjd; ++mjd) {
        const std::chrono::sys_days day{std::chrono::days{mjd - kMjdOfSysDaysZero}};
        const auto info = std::chrono::get_leap_second_info(std::chrono::utc_clock::from_sys(day));
        const auto expected = static_cast<std::int32_t>(10 + info.elapsed.count());
        if (deltaAtSecondsForUtcDay(mjd) != expected) {
            CAPTURE(mjd, expected, deltaAtSecondsForUtcDay(mjd));
            FAIL("DeltaAT disagrees with the standard library");
        }
        ++checked;
    }
    REQUIRE(checked == kLeapSecondTableExpiryMjd - kLeapSecondEraFirstMjd);

    // And the insertions themselves: the last second of a day this table gives
    // a leap second to is a second std::chrono calls a leap second.
    for (std::size_t i = 1; i < kIersLeapSeconds.size(); ++i) {
        const std::int64_t boundary = kIersLeapSeconds.at(i).utcMjd;
        CAPTURE(i, boundary);
        const auto justBefore = std::chrono::utc_clock::from_sys(std::chrono::sys_days{
                                    std::chrono::days{boundary - kMjdOfSysDaysZero}}) -
                                std::chrono::seconds{1};
        REQUIRE(std::chrono::get_leap_second_info(justBefore).is_leap_second);
        REQUIRE(leapSecondsAtEndOfUtcDay(boundary - 1) == 1);
    }
}

// The worked example M1-04 states, end to end.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("2017-01-01T00:00:00 UTC is 00:00:37 TAI and 00:01:09.184 TT", "[time][leap]") {
    const UtcTime utc = instant<TimeScale::Utc>({.year = 2017, .month = 1, .day = 1});
    const TaiTime tai = converted(taiFromUtc(utc));
    const TtTime tt = converted(ttFromUtc(utc));

    const auto taiDate = tai.toCalendar();
    REQUIRE(taiDate.has_value());
    REQUIRE(taiDate->year == 2017);
    REQUIRE(taiDate->month == 1);
    REQUIRE(taiDate->day == 1);
    REQUIRE(taiDate->hour == 0);
    REQUIRE(taiDate->minute == 0);
    REQUIRE(tai.picosecondOfDay() == 37 * kSecond);

    const auto ttDate = tt.toCalendar();
    REQUIRE(ttDate.has_value());
    REQUIRE(ttDate->hour == 0);
    REQUIRE(ttDate->minute == 1);
    // 69.184 s into the day: 37 from DeltaAT and 32.184 from TT - TAI. Exact in
    // picoseconds, and not exact in an f64 count of seconds.
    REQUIRE(tt.picosecondOfDay() == (69 * kSecond) + 184'000'000'000);

    // And back, bit for bit.
    REQUIRE(converted(utcFromTai(tai)) == utc);
    REQUIRE(converted(utcFromTt(tt)) == utc);
    REQUIRE(taiFromTt(tt) == tai);
}

// TT - TAI is exactly 32.184 s by definition (IAU 1991 Resolution A4,
// Recommendation IV), so any deviation is a bug and not a tolerance.
TEST_CASE("TT - TAI is exactly 32.184 s at every epoch", "[time][leap]") {
    Sampler sampler;
    for (std::size_t i = 0; i < 1'000; ++i) {
        const TaiTime tai = instant<TimeScale::Tai>(drawDate(sampler));
        CAPTURE(kSweepSeed, i, tai);
        const TtTime tt = ttFromTai(tai);
        // Compared in picoseconds, where the claim is exact, rather than as a
        // difference of Seconds -- which the two types rightly refuse anyway.
        const auto days =
            static_cast<std::int64_t>(tt.modifiedJulianDay() - tai.modifiedJulianDay());
        const std::int64_t offset = (days * kDay) + tt.picosecondOfDay() - tai.picosecondOfDay();
        REQUIRE(offset == 32'184'000'000'000);
        REQUIRE(taiFromTt(tt) == tai);
    }
}

// Each step, checked one second before and one second after the boundary, in
// both directions. The published table is the reference; this code is the
// lookup and the arithmetic around it.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("every step in the table, one second either side", "[time][leap]") {
    for (std::size_t i = 1; i < kIersLeapSeconds.size(); ++i) {
        const std::int64_t boundaryMjd = kIersLeapSeconds.at(i).utcMjd;
        const std::int32_t before = kIersLeapSeconds.at(i - 1).deltaAtSeconds;
        const std::int32_t after = kIersLeapSeconds.at(i).deltaAtSeconds;
        CAPTURE(i, boundaryMjd, before, after);
        REQUIRE(after - before == 1); // every published step is a positive one

        const TaiTime atBoundary = taiAtStep(i);
        const TaiTime oneBefore = atBoundary - Seconds{1.0};
        const TaiTime oneAfter = atBoundary + Seconds{1.0};

        const UtcTime utcAtBoundary = converted(utcFromTai(atBoundary));
        const UtcTime utcBefore = converted(utcFromTai(oneBefore));
        const UtcTime utcAfter = converted(utcFromTai(oneAfter));

        // At the boundary: midnight begins the new day.
        REQUIRE_THAT(utcAtBoundary.modifiedJulianDay(),
                     WithinAbsOf(static_cast<f64>(boundaryMjd), Tolerance{0.0}));
        REQUIRE(utcAtBoundary.picosecondOfDay() == 0);

        // One second before: the inserted leap second itself, 86 400 s into the
        // day before -- which is 23:59:60, a label no other scale has.
        REQUIRE_THAT(utcBefore.modifiedJulianDay(),
                     WithinAbsOf(static_cast<f64>(boundaryMjd - 1), Tolerance{0.0}));
        REQUIRE(utcBefore.picosecondOfDay() == kDay);
        const auto beforeDate = utcBefore.toCalendar();
        REQUIRE(beforeDate.has_value());
        REQUIRE(beforeDate->hour == 23);
        REQUIRE(beforeDate->minute == 59);
        REQUIRE_THAT(beforeDate->second.value(), WithinAbsOf(60.0, Tolerance{0.0}));

        // One second after: 00:00:01 of the new day.
        REQUIRE_THAT(utcAfter.modifiedJulianDay(),
                     WithinAbsOf(static_cast<f64>(boundaryMjd), Tolerance{0.0}));
        REQUIRE(utcAfter.picosecondOfDay() == kSecond);

        // DeltaAT on each side of the boundary, and the day that grew.
        REQUIRE(deltaAtSecondsForUtcDay(boundaryMjd - 1) == before);
        REQUIRE(deltaAtSecondsForUtcDay(boundaryMjd) == after);
        REQUIRE(secondsInUtcDay(boundaryMjd - 1) == 86'401);
        REQUIRE(secondsInUtcDay(boundaryMjd) == 86'400);

        // And the other direction, bit for bit.
        REQUIRE(converted(taiFromUtc(utcBefore)) == oneBefore);
        REQUIRE(converted(taiFromUtc(utcAtBoundary)) == atBoundary);
        REQUIRE(converted(taiFromUtc(utcAfter)) == oneAfter);
    }
}

// The leap second is an instant like any other: it has a calendar form, a
// storage form, and a TAI counterpart, and it survives all three.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the leap second at 2016-12-31T23:59:60 round-trips", "[time][leap]") {
    const CalendarDate date{
        .year = 2016,
        .month = 12,
        .day = 31,
        .hour = 23,
        .minute = 59,
        .second = Seconds{60.0},
    };
    const UtcTime utc = instant<TimeScale::Utc>(date);

    // 23:59:60 is 86 400 s into the day, exactly -- M1-03's integer
    // picoseconds are what make that a representation rather than a fudge.
    REQUIRE_THAT(utc.modifiedJulianDay(), WithinAbsOf(57'753.0, Tolerance{0.0}));
    REQUIRE(utc.picosecondOfDay() == kDay);

    const auto back = utc.toCalendar();
    REQUIRE(back.has_value());
    REQUIRE(back->year == 2016);
    REQUIRE(back->month == 12);
    REQUIRE(back->day == 31);
    REQUIRE(back->hour == 23);
    REQUIRE(back->minute == 59);
    REQUIRE_THAT(back->second.value(), WithinAbsOf(60.0, Tolerance{0.0}));

    // It is 2017-01-01T00:00:36 TAI -- one second before the 00:00:37 that
    // 2017-01-01T00:00:00 UTC maps to.
    const TaiTime tai = converted(taiFromUtc(utc));
    REQUIRE_THAT(tai.modifiedJulianDay(), WithinAbsOf(57'754.0, Tolerance{0.0}));
    REQUIRE(tai.picosecondOfDay() == 36 * kSecond);
    REQUIRE(converted(utcFromTai(tai)) == utc);

    const UtcTime nextMidnight = instant<TimeScale::Utc>({.year = 2017, .month = 1, .day = 1});
    REQUIRE(utc < nextMidnight);
    REQUIRE_THAT((converted(taiFromUtc(nextMidnight)) - tai).value(),
                 WithinAbsOf(1.0, Tolerance{0.0}));

    // Half a second into it, which only exists because the day runs to 86 401 s.
    CalendarDate half = date;
    half.second = Seconds{60.5};
    const UtcTime halfway = instant<TimeScale::Utc>(half);
    REQUIRE(halfway.picosecondOfDay() == kDay + (kSecond / 2));
    REQUIRE(utc < halfway);
    REQUIRE(halfway < nextMidnight);
    REQUIRE(converted(utcFromTai(converted(taiFromUtc(halfway)))) == halfway);

    // 60 is not a second on any other scale, whatever the date.
    REQUIRE(!TtTime::fromCalendar(date).has_value());
    REQUIRE(!TaiTime::fromCalendar(date).has_value());
    REQUIRE(!Ut1Time::fromCalendar(date).has_value());
}

// The round trip, over a seeded sweep that lands on every boundary as well as
// at random. The budget is 1e-9 s; the design guarantees bit identity, and that
// is what is asserted -- a budget a thousand times looser than the arithmetic
// cannot see a regression in it.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("UTC and TAI round-trip exactly over a seeded sweep", "[time][leap]") {
    Sampler sampler;

    // Every boundary, to the picosecond either side of the inserted second.
    for (std::size_t i = 1; i < kIersLeapSeconds.size(); ++i) {
        const TaiTime atBoundary = taiAtStep(i);
        CAPTURE(i);
        for (const std::int64_t offset : {
                 -2'000'000'000'000LL,
                 -1'000'000'000'001LL,
                 -1'000'000'000'000LL,
                 -999'999'999'999LL,
                 -1LL,
                 0LL,
                 1LL,
                 1'000'000'000'000LL,
             }) {
            CAPTURE(offset);
            const TaiTime tai =
                atBoundary + Seconds{static_cast<f64>(offset) / detail::kPicosecondsPerSecondF};
            const UtcTime utc = converted(utcFromTai(tai));
            REQUIRE(isNormalised(utc));
            REQUIRE(converted(taiFromUtc(utc)) == tai);
        }
    }

    // And anywhere in the era the table covers.
    const std::int64_t firstDay = kLeapSecondEraFirstMjd;
    const std::int64_t lastDay = kLeapSecondTableExpiryMjd - 1;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const Stamp stamp{
            .mjd = sampler.between({.lo = firstDay, .hi = lastDay}),
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kDay - 1}),
        };
        CAPTURE(kSweepSeed, i, stamp.mjd, stamp.picosecondOfDay);
        const TaiTime tai = taiAt(stamp);
        const UtcTime utc = converted(utcFromTai(tai));
        REQUIRE(isNormalised(utc));
        REQUIRE(converted(taiFromUtc(utc)) == tai);
        // And through TT, which adds an exact 32.184 s in the middle.
        REQUIRE(converted(utcFromTt(ttFromTai(tai))) == utc);
        REQUIRE(converted(ttFromUtc(utc)) == ttFromTai(tai));
    }
}

// TAI runs on, one second at a time, through the second that UTC labels
// 23:59:60 -- and so does UTC, because the day it is in simply has one more
// second in it rather than a repeated label.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("both scales increase across every boundary", "[time][leap]") {
    for (std::size_t i = 1; i < kIersLeapSeconds.size(); ++i) {
        const TaiTime atBoundary = taiAtStep(i);
        CAPTURE(i);
        TaiTime previousTai = atBoundary - Seconds{4.0};
        UtcTime previousUtc = converted(utcFromTai(previousTai));
        for (int step = -3; step <= 4; ++step) {
            CAPTURE(step);
            const TaiTime tai = atBoundary + Seconds{static_cast<f64>(step)};
            const UtcTime utc = converted(utcFromTai(tai));
            REQUIRE(previousTai < tai);
            REQUIRE(previousUtc < utc);
            REQUIRE_THAT((tai - previousTai).value(), WithinAbsOf(1.0, Tolerance{0.0}));
            previousTai = tai;
            previousUtc = utc;
        }
    }
}

// Each refusal asked for by the name of the check that must produce it, not by
// "it failed".
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the leap-second table refuses by name", "[time][leap][errors]") {
    // Before 1972-01-01, where TAI - UTC is not a whole number of seconds.
    const auto unix = taiFromUtc(kUnixEpoch);
    INFO("the Unix epoch -> " << errorName(unix));
    REQUIRE(!unix.has_value());
    REQUIRE(unix.error() == TimeError::BeforeLeapSecondEra);

    const UtcTime lastSecondBefore = instant<TimeScale::Utc>(
        {.year = 1971, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{59.0}});
    REQUIRE(!taiFromUtc(lastSecondBefore).has_value());
    REQUIRE(taiFromUtc(lastSecondBefore).error() == TimeError::BeforeLeapSecondEra);
    REQUIRE(!ttFromUtc(lastSecondBefore).has_value());
    REQUIRE(ttFromUtc(lastSecondBefore).error() == TimeError::BeforeLeapSecondEra);

    // The first instant of the era converts.
    const UtcTime firstOfEra = instant<TimeScale::Utc>({.year = 1972, .month = 1, .day = 1});
    REQUIRE(converted(taiFromUtc(firstOfEra)).picosecondOfDay() == 10 * kSecond);

    // Past the expiry, where the next bulletin has not been read yet.
    const UtcTime pastExpiry = instant<TimeScale::Utc>({.year = 2027, .month = 1, .day = 1});
    const auto expired = taiFromUtc(pastExpiry);
    INFO("2027-01-01 -> " << errorName(expired));
    REQUIRE(!expired.has_value());
    REQUIRE(expired.error() == TimeError::LeapSecondTableExpired);
    REQUIRE(
        !utcFromTt(ttFromTai(taiAt({.mjd = kLeapSecondTableExpiryMjd + 10, .picosecondOfDay = 0})))
             .has_value());

    // And the last instant the table does cover still converts: the boundary is
    // where it is, not a day early.
    const UtcTime lastCovered = instant<TimeScale::Utc>(
        {.year = 2026, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{59.0}});
    REQUIRE(taiFromUtc(lastCovered).has_value());

    // The same two edges from the TAI side.
    REQUIRE(!utcFromTai(taiAt({.mjd = kLeapSecondEraFirstMjd, .picosecondOfDay = 9 * kSecond}))
                 .has_value());
    REQUIRE(utcFromTai(taiAt({.mjd = kLeapSecondEraFirstMjd, .picosecondOfDay = 10 * kSecond}))
                .has_value());

    // 60 seconds on a day the table gives no leap second to, and 60 seconds
    // anywhere but the final minute of a day that has one -- the allowance
    // belongs to 23:59, not to the day.
    constexpr auto kNotLeapSeconds = std::to_array<CalendarDate>({
        {.year = 2017, .month = 6, .day = 30, .hour = 23, .minute = 59, .second = Seconds{60.0}},
        {.year = 2020, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{60.0}},
        {.year = 2016, .month = 12, .day = 31, .hour = 12, .minute = 30, .second = Seconds{60.0}},
        {.year = 2016, .month = 12, .day = 31, .hour = 23, .minute = 58, .second = Seconds{60.0}},
    });
    for (const CalendarDate& ordinary : kNotLeapSeconds) {
        CAPTURE(ordinary.year, ordinary.month, ordinary.day, ordinary.hour, ordinary.minute);
        const auto refused = UtcTime::fromCalendar(ordinary);
        INFO(errorName(refused));
        REQUIRE(!refused.has_value());
        REQUIRE(refused.error() == TimeError::InvalidTimeOfDay);
    }

    // 61 is never a second, not even on a day that has a leap one.
    REQUIRE(!UtcTime::fromCalendar({.year = 2016,
                                    .month = 12,
                                    .day = 31,
                                    .hour = 23,
                                    .minute = 59,
                                    .second = Seconds{61.0}})
                 .has_value());
}

// **A TAI instant outside the calendar is reported, not asserted.**
//
// Found by `fuzz_time` on 2026-09-19, and named after the symptom: a build with
// assertions live aborted where a Release build had been answering correctly
// all along. TT 0001-01-01T00:00:00 is 0000-12-31T23:59:27.816 TAI, a year the
// calendar does not hold, and `utcFromTai` asserted that the day was inside it.
// Arithmetic reaches the same place from the other end -- an instant carried
// past 9999 is not an error, and `toCalendar` is where that is reported -- so
// the condition is one a caller can produce, and ADR 0002 says those are
// reported. The two edges also decide which name: the era below, the table's
// expiry above.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a TAI instant outside the calendar is reported rather than asserted",
          "[time][leap][errors]") {
    // The first 32.184 s of year 1, in TT: every one of them has a TAI in
    // year 0, and none of them may abort.
    for (const f64 second : {0.0, 10.0, 32.0}) {
        const TtTime tt =
            instant<TimeScale::Tt>({.year = 1, .month = 1, .day = 1, .second = Seconds{second}});
        CAPTURE(second);
        REQUIRE(!detail::inCalendarRange(taiFromTt(tt).modifiedJulianDay()));
        const auto utc = utcFromTt(tt);
        INFO(errorName(utc));
        REQUIRE(!utc.has_value());
        REQUIRE(utc.error() == TimeError::BeforeLeapSecondEra);
    }

    // Arithmetic carries an instant past the calendar's far end, where the
    // table has no DeltaAT either -- including a day far past what an int64
    // holds, which nothing may cast.
    const TaiTime last =
        instant<TimeScale::Tai>({.year = 9999, .month = 12, .day = 31, .hour = 23});
    for (const f64 seconds : {86'400.0 * 400, 1e300}) {
        const auto beyond = utcFromTai(last + Seconds{seconds});
        CAPTURE(seconds);
        INFO(errorName(beyond));
        REQUIRE(!beyond.has_value());
        REQUIRE(beyond.error() == TimeError::LeapSecondTableExpired);
    }

    // And the day the table expires on is not refused wholesale: the seconds of
    // it before 00:00:37 TAI are 2026-12-31 in UTC, and the picosecond after is
    // the first the bulletin does not cover.
    const TaiTime lastCovered =
        taiAt({.mjd = kLeapSecondTableExpiryMjd, .picosecondOfDay = (37 * kSecond) - 1});
    const UtcTime covered = converted(utcFromTai(lastCovered));
    REQUIRE_THAT(covered.modifiedJulianDay(),
                 WithinAbsOf(static_cast<f64>(kLeapSecondTableExpiryMjd - 1), Tolerance{0.0}));
    REQUIRE(covered.picosecondOfDay() == kDay - 1);
    REQUIRE(!utcFromTai(taiAt({.mjd = kLeapSecondTableExpiryMjd, .picosecondOfDay = 37 * kSecond}))
                 .has_value());
}

// **A negative leap second, which has never been inserted.**
//
// The mechanism allows one and the Earth's rotation has been making one likelier,
// so the arithmetic handles it -- and nothing in the committed table can execute
// that path. Driving the table-taking overloads with a table this project did
// not publish is what stops "handled" from meaning "written and never run"
// (VERIFICATION.md rule 23, and decided 2026-09-18).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a synthetic negative leap second shortens its day", "[time][leap]") {
    constexpr std::int64_t kDayBefore = 60'000;
    constexpr auto kNegative = std::to_array<LeapSecondStep>({
        {.utcMjd = 59'000, .deltaAtSeconds = 37},
        {.utcMjd = kDayBefore + 1, .deltaAtSeconds = 36},
    });
    const std::span<const LeapSecondStep> table{kNegative};

    REQUIRE(leapSecondsAtEndOfUtcDay(table, kDayBefore) == -1);
    REQUIRE(secondsInUtcDay(table, kDayBefore) == 86'399);
    REQUIRE(secondsInFinalMinuteOfUtcDay(table, kDayBefore) == 59);
    // The day after is ordinary again, and so is the day before.
    REQUIRE(secondsInUtcDay(table, kDayBefore + 1) == 86'400);
    REQUIRE(secondsInFinalMinuteOfUtcDay(table, kDayBefore - 1) == 60);

    // No second is ever inside an *inserted* leap second here: there is none.
    // The TAI instant one second below the step is 23:59:58 of the short day.
    const TaiLookup atStep =
        leapSecondLookupForTai(table, {.mjd = kDayBefore + 1, .secondOfDay = 36});
    REQUIRE(atStep.deltaAtSeconds == 36);
    REQUIRE(!atStep.insideInsertedLeapSecond);
    const TaiLookup justBefore =
        leapSecondLookupForTai(table, {.mjd = kDayBefore + 1, .secondOfDay = 35});
    REQUIRE(justBefore.deltaAtSeconds == 37);
    REQUIRE(!justBefore.insideInsertedLeapSecond);
    // 35 - 37 = -2, which with the day's 86 400 is 23:59:58 of kDayBefore: the
    // second the negative leap removed, 23:59:59, is simply never produced.

    // And the same table read as a positive step, for the contrast: here the
    // instant one second below the step *is* inside the inserted second.
    constexpr auto kPositive = std::to_array<LeapSecondStep>({
        {.utcMjd = 59'000, .deltaAtSeconds = 37},
        {.utcMjd = kDayBefore + 1, .deltaAtSeconds = 38},
    });
    const std::span<const LeapSecondStep> positive{kPositive};
    REQUIRE(leapSecondsAtEndOfUtcDay(positive, kDayBefore) == 1);
    REQUIRE(secondsInFinalMinuteOfUtcDay(positive, kDayBefore) == 61);
    const TaiLookup inserted =
        leapSecondLookupForTai(positive, {.mjd = kDayBefore + 1, .secondOfDay = 37});
    REQUIRE(inserted.deltaAtSeconds == 37);
    REQUIRE(inserted.insideInsertedLeapSecond);
    REQUIRE(!leapSecondLookupForTai(positive, {.mjd = kDayBefore + 1, .secondOfDay = 36})
                 .insideInsertedLeapSecond);
    REQUIRE(!leapSecondLookupForTai(positive, {.mjd = kDayBefore + 1, .secondOfDay = 38})
                 .insideInsertedLeapSecond);
}

// A UTC Julian date is a *quasi*-Julian date: its fraction is of that UTC day,
// however long the day is (ERFA's convention, ADR 0009's update). Without that
// the fraction would reach 1.0000116 inside a leap second and break the [0, 1)
// the type promises.
//
// **The reconstruction is asserted exactly, not to a tolerance.** The fraction
// is a double, and its ulp is 9.6 ps at the far end of a day, so a round trip
// through it cannot be exact -- but the answer a correct implementation must
// give is completely determined: round(nearest_double(picos / L) * L). Those
// values were computed with exact rational arithmetic (Python's fractions
// module), the same way the rest of this suite's Julian-date expectations were,
// and asserting them pins the *rounding* rather than merely bounding the error.
// A bound of "within 5 ps" would pass for an implementation that was 4 ps out
// in the wrong direction.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a UTC Julian date is a fraction of its own day", "[time][leap][julian]") {
    // 57 753 is 2016-12-31, which has a leap second; 57 754 is 2017-01-01,
    // which does not.
    constexpr std::int64_t kLeapDay = 57'753;
    constexpr std::int64_t kOrdinaryDay = 57'754;

    struct Case {
        std::string_view name;
        std::int64_t mjd;
        std::int64_t picosecondOfDay;
        std::int64_t idealError;
    };
    constexpr std::array kCases = std::to_array<Case>({
        {
            .name = "23:59:60.0",
            .mjd = kLeapDay,
            .picosecondOfDay = 86'400'000'000'000'000,
            .idealError = 2,
        },
        {
            .name = "23:59:60.5",
            .mjd = kLeapDay,
            .picosecondOfDay = 86'400'500'000'000'000,
            .idealError = -4,
        },
        {
            .name = "23:59:60.9",
            .mjd = kLeapDay,
            .picosecondOfDay = 86'400'900'000'000'000,
            .idealError = 1,
        },
        {
            .name = "23:59:59.5",
            .mjd = kLeapDay,
            .picosecondOfDay = 86'399'500'000'000'000,
            .idealError = -2,
        },
        {
            .name = "12:00:00.0",
            .mjd = kLeapDay,
            .picosecondOfDay = 43'200'000'000'000'000,
            .idealError = 1,
        },
        {
            .name = "00:00:00.0",
            .mjd = kLeapDay,
            .picosecondOfDay = 0,
            .idealError = 0,
        },
        {
            .name = "12:00:00, ordinary day",
            .mjd = kOrdinaryDay,
            .picosecondOfDay = 43'200'000'000'000'000,
            .idealError = 0,
        },
        {
            .name = "23:59:59.5, ordinary day",
            .mjd = kOrdinaryDay,
            .picosecondOfDay = 86'399'500'000'000'000,
            .idealError = 2,
        },
    });

    for (const Case& c : kCases) {
        CAPTURE(c.name, c.mjd, c.picosecondOfDay);
        const UtcTime utc = utcAt({.mjd = c.mjd, .picosecondOfDay = c.picosecondOfDay});
        const JulianDate jd = utc.julianDate();

        // The invariant the quasi-Julian convention exists to keep.
        REQUIRE(jd.fraction >= 0.0);
        REQUIRE(jd.fraction < 1.0);
        REQUIRE_THAT(jd.day, WithinAbsOf(kMjdZero + static_cast<f64>(c.mjd), Tolerance{0.0}));

        const UtcTime back = converted(UtcTime::fromJulianDate(jd));
        REQUIRE_THAT(back.modifiedJulianDay(),
                     WithinAbsOf(static_cast<f64>(c.mjd), Tolerance{0.0}));
        const std::int64_t error = back.picosecondOfDay() - c.picosecondOfDay;
        CAPTURE(error, c.idealError);
        REQUIRE(error == c.idealError);
    }

    // The fraction inside the leap second is 86 400.5 of 86 401 seconds, not of
    // 86 400 -- which is the whole point, and would otherwise exceed 1.
    const UtcTime halfway = utcAt({.mjd = kLeapDay, .picosecondOfDay = 86'400'500'000'000'000});
    REQUIRE_THAT(halfway.julianDate().fraction, WithinAbsOf(86'400.5 / 86'401.0, Tolerance{1e-16}));

    // And an ordinary UTC day is untouched: M1-03's arithmetic for it is
    // unchanged, so noon is exactly half a day and the trip is exact.
    const UtcTime noon = utcAt({.mjd = kOrdinaryDay, .picosecondOfDay = 43'200'000'000'000'000});
    REQUIRE_THAT(noon.julianDate().fraction, WithinAbsOf(0.5, Tolerance{0.0}));
    REQUIRE(converted(UtcTime::fromJulianDate(noon.julianDate())) == noon);
}
