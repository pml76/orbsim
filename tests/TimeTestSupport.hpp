//
// What the three time suites share (M1-94): test_time, test_time_leap and
// test_time_ut1 were one file, tests/test_time.cpp, until 2026-09-27, when it was
// split so that each compiles and is linted on its own (ADR 0024, register
// decision 214). This is that file's opening, moved unchanged except that its
// anonymous namespace is named -- a header cannot hold one -- and its
// non-template functions are inline. Every sweep builds its own Sampler, so
// splitting the file changed no draw. Since M1-99, the three published Julian
// dates, which only test_time uses, are in that file, because gcc reports them
// unused in the other two suites.
//
#ifndef ORBSIM_TESTS_TIMETESTSUPPORT_HPP
#define ORBSIM_TESTS_TIMETESTSUPPORT_HPP

#include "core/LeapSeconds.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// Catch2 prints an unknown type as "{?}". An instant prints as its two stored
// integers, so a failure can be pasted back into a test as an exact case.
// Exempt from gcc's -Wabi-tag, for the reason given on WithinAbsOf::describe()
// in tests/OrbitTestSupport.hpp: the std::string is Catch2's.
namespace Catch {

template <orb::TimeScale Scale> struct StringMaker<orb::TimePoint<Scale>> {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
    [[nodiscard]] static std::string convert(const orb::TimePoint<Scale>& value) {
        return std::format(
            "(MJD {:.17g}, {} ps)", value.modifiedJulianDay(), value.picosecondOfDay());
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

} // namespace Catch

namespace orb::test::timesuite {

// Inline, so that the program holds one of each rather than one per suite:
// gcc's -Wunused-const-variable=2 reports a plain constexpr variable in a
// header that a suite including it does not use, and test_time_leap uses
// neither. A constant only one suite uses lives in that suite instead, where
// an unused one is still reported (M1-99).
inline constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
inline constexpr f64 kInf = std::numeric_limits<f64>::infinity();

// One day in picoseconds, typed here rather than taken from Time.hpp, so that
// the header's constant is checked against a number written independently.
constexpr std::int64_t kDay = 86'400'000'000'000'000;
constexpr std::int64_t kSecond = 1'000'000'000'000;
static_assert(kPicosecondsPerDay == kDay);
static_assert(kPicosecondsPerSecond == kSecond);

// M1-03's budgets, in seconds.
constexpr Tolerance kResolutionBudget{1e-11}; // representation resolution
constexpr Tolerance kRoundTripBudget{1e-9};   // calendar round trip, 1900-2100
constexpr Tolerance kDriftBudget{1e-9};       // 10^6 additions of 1 us

// What the design guarantees for a calendar round trip, beneath the budget. The
// seconds field is rounded to the nearest picosecond on the way in, 0.5e-12 s,
// and to the nearest double on the way out, at most half an ulp of a value
// below 60 s, which is 2^-48 s. 2^-47 covers that and the two smaller
// roundings inside it, each below 1e-16 s.
constexpr Tolerance kRoundTripDesign{0.5e-12 + 0x1p-47};

// The Modified Julian Day of 1970-01-01, which is day zero of
// std::chrono::sys_days: JD 2440587.5 (US Naval Observatory) less 2400000.5.
constexpr std::int64_t kMjdOfSysDaysZero = 40'587;

[[nodiscard]] inline std::int64_t mjdByTheStandardLibrary(std::chrono::year_month_day date) {
    const auto daysSince1970 = std::chrono::sys_days{date}.time_since_epoch().count();
    return static_cast<std::int64_t>(daysSince1970) + kMjdOfSysDaysZero;
}

// Normalisation, checked from outside the class: a whole-numbered day, and a
// time of day that lies inside it.
//
// **The bound is a second longer for UTC** since M1-04, whose day may hold
// 86 401 SI seconds. *Which* UTC days actually do is a sharper claim, and it
// belongs somewhere that can be independent of the table -- "the leap-second
// table agrees with the standard library's" asks std::chrono. This helper is
// called by most of the suite and deliberately consults nothing.
template <TimeScale Scale> [[nodiscard]] bool isNormalised(const TimePoint<Scale>& t) {
    const f64 mjd = t.modifiedJulianDay();
    const std::int64_t longestDay = Scale == TimeScale::Utc ? kDay + kSecond : kDay;
    return std::isfinite(mjd) && nearlyEqual(mjd, std::trunc(mjd), Tolerance{0.0}) &&
           t.picosecondOfDay() >= 0 && t.picosecondOfDay() < longestDay;
}

// The two ends of a span, as one parameter: two adjacent instants transpose in
// silence, and a transposed pair here would assert the negated claim. The same
// reasoning as Step in tests/OrbitSweepSupport.hpp.
struct Span {
    TaiTime from;
    TaiTime to;
};

// to - from in picoseconds, exactly, for spans short enough for an int64.
[[nodiscard]] inline std::int64_t picosecondsIn(const Span& span) {
    const f64 days = span.to.modifiedJulianDay() - span.from.modifiedJulianDay();
    return (static_cast<std::int64_t>(days) * kDay) +
           (span.to.picosecondOfDay() - span.from.picosecondOfDay());
}

// Unwrap a date the test knows to be valid. A failure here is reported by the
// REQUIRE, with the error's name, rather than read as a value.
template <TimeScale Scale> [[nodiscard]] TimePoint<Scale> instant(const CalendarDate& date) {
    const auto t = TimePoint<Scale>::fromCalendar(date);
    CAPTURE(date.year, date.month, date.day, date.hour, date.minute, date.second.value());
    INFO(errorName(t));
    REQUIRE(t.has_value());
    return *t;
}

// The instant `picos` picoseconds into 2000-01-01 TT, built through the public
// calendar so that nothing but the interface under test is used.
[[nodiscard]] inline TtTime atPicosecondOfJ2000Day(std::int64_t picos) {
    constexpr std::int64_t kPicosPerSecond = 1'000'000'000'000;
    const std::int64_t secondOfDay = picos / kPicosPerSecond;
    const std::int64_t rest = picos % kPicosPerSecond;
    const TtTime t = instant<TimeScale::Tt>({
        .year = 2000,
        .month = 1,
        .day = 1,
        .hour = static_cast<std::int32_t>(secondOfDay / 3600),
        .minute = static_cast<std::int32_t>((secondOfDay / 60) % 60),
        .second = Seconds{static_cast<f64>(secondOfDay % 60) + (static_cast<f64>(rest) / 1e12)},
    });
    INFO("the calendar built the instant intended");
    REQUIRE(t.picosecondOfDay() == picos);
    return t;
}

// A seeded sweep, with the seed written down. VERIFICATION.md rule 12.
constexpr std::uint64_t kSweepSeed = 20260910ULL; // the date this suite was written
// Inline, as kNaN above: all three suites use it, and the header's own
// self-check compile, which uses none, is where gcc reported it (M1-99).
inline constexpr std::size_t kSweepCases = 10'000;

// The two ends of a draw, as one parameter, for the same reason as Span.
struct Range {
    std::int64_t lo;
    std::int64_t hi;
};

// The one random source for the sweeps.
//
// It reads the engine directly rather than through a std:: distribution:
// mt19937_64's output sequence is fixed by the standard, while the
// distributions' algorithms are not, and MSVC's library and libstdc++ differ.
// A sweep built on them would test different dates under the two toolchains,
// and a failure found under gcc could not be reproduced from the seed here.
class Sampler {
public:
    // An integer in [lo, hi]. The modulo bias is below 1e-15 for these ranges,
    // which nothing a sweep of 10,000 cases does can see.
    [[nodiscard]] std::int64_t between(Range range) {
        const auto width = static_cast<std::uint64_t>(range.hi - range.lo + 1);
        return range.lo + static_cast<std::int64_t>(rng_() % width);
    }
    // A double in [0, 1), from the engine's top 53 bits: every such double is
    // equally likely, and none is rounded.
    [[nodiscard]] f64 unit() { return static_cast<f64>(rng_() >> 11U) * 0x1p-53; }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSweepSeed};
};

// A date in 1900-2100 and a time of day, every field drawn. The day comes from
// the month's length as the standard library states it, so every draw is valid.
[[nodiscard]] inline CalendarDate drawDate(Sampler& sampler) {
    const auto y = static_cast<std::int32_t>(sampler.between({.lo = 1900, .hi = 2100}));
    const auto m = static_cast<std::int32_t>(sampler.between({.lo = 1, .hi = 12}));
    const auto end =
        std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)} / std::chrono::last;
    const auto length = static_cast<std::int64_t>(static_cast<unsigned>(end.day()));
    return CalendarDate{
        .year = y,
        .month = m,
        .day = static_cast<std::int32_t>(sampler.between({.lo = 1, .hi = length})),
        .hour = static_cast<std::int32_t>(sampler.between({.lo = 0, .hi = 23})),
        .minute = static_cast<std::int32_t>(sampler.between({.lo = 0, .hi = 59})),
        .second = Seconds{sampler.unit() * 60.0},
    };
}

// What the leap-second suite and the UT1 suite both use: the published table, as a
// second transcription, and instants built at exact stamps. Moved here from the leap
// section when the file was split (M1-94).

// The 28 published steps, as the IERS file gives them: a date and a value.
// core/LeapSeconds.hpp holds the date as an MJD, and the two are compared
// through a calendar that is in neither of them.
struct PublishedStep {
    int year;
    unsigned month;
    unsigned day;
    std::int32_t deltaAtSeconds;
};

constexpr auto kPublishedSteps = std::to_array<PublishedStep>({
    {.year = 1972, .month = 1, .day = 1, .deltaAtSeconds = 10},
    {.year = 1972, .month = 7, .day = 1, .deltaAtSeconds = 11},
    {.year = 1973, .month = 1, .day = 1, .deltaAtSeconds = 12},
    {.year = 1974, .month = 1, .day = 1, .deltaAtSeconds = 13},
    {.year = 1975, .month = 1, .day = 1, .deltaAtSeconds = 14},
    {.year = 1976, .month = 1, .day = 1, .deltaAtSeconds = 15},
    {.year = 1977, .month = 1, .day = 1, .deltaAtSeconds = 16},
    {.year = 1978, .month = 1, .day = 1, .deltaAtSeconds = 17},
    {.year = 1979, .month = 1, .day = 1, .deltaAtSeconds = 18},
    {.year = 1980, .month = 1, .day = 1, .deltaAtSeconds = 19},
    {.year = 1981, .month = 7, .day = 1, .deltaAtSeconds = 20},
    {.year = 1982, .month = 7, .day = 1, .deltaAtSeconds = 21},
    {.year = 1983, .month = 7, .day = 1, .deltaAtSeconds = 22},
    {.year = 1985, .month = 7, .day = 1, .deltaAtSeconds = 23},
    {.year = 1988, .month = 1, .day = 1, .deltaAtSeconds = 24},
    {.year = 1990, .month = 1, .day = 1, .deltaAtSeconds = 25},
    {.year = 1991, .month = 1, .day = 1, .deltaAtSeconds = 26},
    {.year = 1992, .month = 7, .day = 1, .deltaAtSeconds = 27},
    {.year = 1993, .month = 7, .day = 1, .deltaAtSeconds = 28},
    {.year = 1994, .month = 7, .day = 1, .deltaAtSeconds = 29},
    {.year = 1996, .month = 1, .day = 1, .deltaAtSeconds = 30},
    {.year = 1997, .month = 7, .day = 1, .deltaAtSeconds = 31},
    {.year = 1999, .month = 1, .day = 1, .deltaAtSeconds = 32},
    {.year = 2006, .month = 1, .day = 1, .deltaAtSeconds = 33},
    {.year = 2009, .month = 1, .day = 1, .deltaAtSeconds = 34},
    {.year = 2012, .month = 7, .day = 1, .deltaAtSeconds = 35},
    {.year = 2015, .month = 7, .day = 1, .deltaAtSeconds = 36},
    {.year = 2017, .month = 1, .day = 1, .deltaAtSeconds = 37},
});

// The three fields as one parameter: three adjacent integers transpose in
// silence, and the whole point of CalendarDate is that they cannot.
struct CivilFields {
    int year;
    unsigned month;
    unsigned day;
};

[[nodiscard]] inline std::chrono::year_month_day civilOf(CivilFields fields) {
    return std::chrono::year{fields.year} / std::chrono::month{fields.month} /
           std::chrono::day{fields.day};
}

[[nodiscard]] inline std::int64_t mjdOf(const PublishedStep& step) {
    return mjdByTheStandardLibrary(
        civilOf({.year = step.year, .month = step.month, .day = step.day}));
}

// A calendar date for an MJD, through std::chrono rather than through the
// calendar in core/Time.hpp: a test that built its dates with the code under
// test would prove nothing about them.
[[nodiscard]] inline CalendarDate dateOfMjd(std::int64_t mjd) {
    const std::chrono::year_month_day ymd{
        std::chrono::sys_days{std::chrono::days{mjd - kMjdOfSysDaysZero}}};
    return CalendarDate{
        .year = static_cast<std::int32_t>(ymd.year()),
        .month = static_cast<std::int32_t>(static_cast<unsigned>(ymd.month())),
        .day = static_cast<std::int32_t>(static_cast<unsigned>(ymd.day())),
    };
}

// The TAI instant at which step `i` takes effect. UTC day D begins at
// 00:00:DeltaAT TAI, and DeltaAT has never reached a minute, so this is an
// ordinary calendar date on the TAI scale -- built without using any
// conversion, so that the conversions can be checked against it.
[[nodiscard]] inline TaiTime taiAtStep(std::size_t i) {
    CalendarDate date = dateOfMjd(kIersLeapSeconds.at(i).utcMjd);
    date.second = Seconds{static_cast<f64>(kIersLeapSeconds.at(i).deltaAtSeconds)};
    return instant<TimeScale::Tai>(date);
}

// Unwrap a conversion the test knows must succeed; a failure is reported with
// the error's name rather than read as a value.
template <typename T> [[nodiscard]] T converted(const std::expected<T, TimeError>& result) {
    INFO(errorName(result));
    REQUIRE(result.has_value());
    return *result;
}

// A day and a picosecond within it, as one parameter: two adjacent std::int64_t
// transpose in silence, and a transposed pair here would build a different
// instant and assert a claim about it.
struct Stamp {
    std::int64_t mjd;
    std::int64_t picosecondOfDay;
};

// A TAI instant at an exact stamp, built through the calendar so that only the
// public interface is used, and checked to be the instant intended.
[[nodiscard]] inline TaiTime taiAt(Stamp stamp) {
    CalendarDate date = dateOfMjd(stamp.mjd);
    const std::int64_t secondOfDay = stamp.picosecondOfDay / kSecond;
    const std::int64_t rest = stamp.picosecondOfDay % kSecond;
    date.hour = static_cast<std::int32_t>(secondOfDay / 3'600);
    date.minute = static_cast<std::int32_t>((secondOfDay / 60) % 60);
    date.second = Seconds{static_cast<f64>(secondOfDay % 60) +
                          (static_cast<f64>(rest) / detail::kPicosecondsPerSecondF)};
    const TaiTime t = instant<TimeScale::Tai>(date);
    INFO("the calendar built the TAI instant intended");
    REQUIRE(t.picosecondOfDay() == stamp.picosecondOfDay);
    return t;
}

// The same for UTC, where a second of day at or past 86 400 is the inserted
// leap second and is spelled 23:59:60. Written out here rather than borrowed
// from toCalendar(): this is the test's own account of what the label means,
// and the REQUIRE below is what makes the two agree.
[[nodiscard]] inline UtcTime utcAt(Stamp stamp) {
    constexpr std::int64_t kFinalMinuteBegins = 86'340;
    CalendarDate date = dateOfMjd(stamp.mjd);
    const std::int64_t secondOfDay = stamp.picosecondOfDay / kSecond;
    const std::int64_t rest = stamp.picosecondOfDay % kSecond;
    const f64 fraction = static_cast<f64>(rest) / detail::kPicosecondsPerSecondF;
    if (secondOfDay >= 86'400) {
        date.hour = 23;
        date.minute = 59;
        date.second = Seconds{static_cast<f64>(secondOfDay - kFinalMinuteBegins) + fraction};
    } else {
        date.hour = static_cast<std::int32_t>(secondOfDay / 3'600);
        date.minute = static_cast<std::int32_t>((secondOfDay / 60) % 60);
        date.second = Seconds{static_cast<f64>(secondOfDay % 60) + fraction};
    }
    const UtcTime t = instant<TimeScale::Utc>(date);
    INFO("the calendar built the UTC instant intended");
    REQUIRE(t.picosecondOfDay() == stamp.picosecondOfDay);
    return t;
}

} // namespace orb::test::timesuite

#endif // ORBSIM_TESTS_TIMETESTSUPPORT_HPP
