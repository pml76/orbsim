//
// Tests for core/Time.hpp: instants that know which time scale they are in.
//
// Nothing here is checked against the code it tests. The expected values come
// from four places, each independent of Time.hpp:
//
//   * the published epochs, as the US Naval Observatory states them;
//   * the C++ standard library's proleptic Gregorian calendar, whose
//     days-from-civil algorithm is a different formulation from Fliegel and
//     Van Flandern's, compared on every day of 1900-2100;
//   * exact rational arithmetic -- Python's fractions module -- for the
//     Julian-date conversions, whose right answer is a correctly rounded double;
//   * integer picoseconds, which are exact, for the resolution and drift claims.
//
// The budgets are M1-03's. Beneath each one the suite also asserts what the
// design guarantees, because a budget a thousand times looser than the code
// cannot see a hundredfold regression (decided 2026-09-10).
//
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
#include <expected>
#include <format>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>

using namespace orb;
using namespace orb::test;

// Catch2 prints an unknown type as "{?}". An instant prints as its two stored
// integers, so a failure can be pasted back into a test as an exact case.
namespace Catch {

template <orb::TimeScale Scale> struct StringMaker<orb::TimePoint<Scale>> {
    [[nodiscard]] static std::string convert(const orb::TimePoint<Scale>& value) {
        return std::format(
            "(MJD {:.17g}, {} ps)", value.modifiedJulianDay(), value.picosecondOfDay());
    }
};

} // namespace Catch

// A note on the readability-function-cognitive-complexity suppressions below.
//
// Catch2's REQUIRE and REQUIRE_THAT each expand to a do-while wrapping a
// try/catch, so a case scores roughly three points per assertion whether or
// not it branches at all. The score measures the framework, not the code.
// Ruled by the project owner on 2026-09-09 -- see test_orbit_scales.cpp for
// the alternatives that were measured -- and confirmed for this suite on
// 2026-09-10: one suppression per function, and only where the check fires.

namespace {

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();

// One day in picoseconds, typed here rather than taken from Time.hpp, so that
// the header's constant is checked against a number written independently.
constexpr std::int64_t kDay = 86'400'000'000'000'000;
static_assert(kPicosecondsPerDay == kDay);

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

// The published epochs, as the US Naval Observatory states them: J2000.0 from
// its page on Terrestrial Time ("Julian date 2451545.0 TT, or 2000 January 1,
// 12h TT"), the other two from its Julian-date service, queried 2026-09-10.
constexpr f64 kJ2000JulianDate = 2451545.0;
constexpr f64 kMjdZeroJulianDate = 2400000.5;
constexpr f64 kUnixEpochJulianDate = 2440587.5;

[[nodiscard]] std::int64_t mjdByTheStandardLibrary(std::chrono::year_month_day date) {
    const auto daysSince1970 = std::chrono::sys_days{date}.time_since_epoch().count();
    return static_cast<std::int64_t>(daysSince1970) + kMjdOfSysDaysZero;
}

// Normalisation, checked from outside the class: a whole-numbered day, and a
// time of day that lies inside it.
template <TimeScale Scale> [[nodiscard]] bool isNormalised(const TimePoint<Scale>& t) {
    const f64 mjd = t.modifiedJulianDay();
    return std::isfinite(mjd) && nearlyEqual(mjd, std::trunc(mjd), Tolerance{0.0}) &&
           t.picosecondOfDay() >= 0 && t.picosecondOfDay() < kDay;
}

// The two ends of a span, as one parameter: two adjacent instants transpose in
// silence, and a transposed pair here would assert the negated claim. The same
// reasoning as Step in test_orbit_scales.cpp.
struct Span {
    TaiTime from;
    TaiTime to;
};

// to - from in picoseconds, exactly, for spans short enough for an int64.
[[nodiscard]] std::int64_t picosecondsIn(const Span& span) {
    const f64 days = span.to.modifiedJulianDay() - span.from.modifiedJulianDay();
    return (static_cast<std::int64_t>(days) * kDay) +
           (span.to.picosecondOfDay() - span.from.picosecondOfDay());
}

// Unwrap a date the test knows to be valid. A failure here is reported by the
// REQUIRE, with the error's name, rather than read as a value.
template <TimeScale Scale> [[nodiscard]] TimePoint<Scale> instant(const CalendarDate& date) {
    const auto t = TimePoint<Scale>::fromCalendar(date);
    CAPTURE(date.year, date.month, date.day, date.hour, date.minute, date.second.value);
    INFO(errorName(t));
    REQUIRE(t.has_value());
    return *t;
}

// The instant `picos` picoseconds into 2000-01-01 TT, built through the public
// calendar so that nothing but the interface under test is used.
[[nodiscard]] TtTime atPicosecondOfJ2000Day(std::int64_t picos) {
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
constexpr std::size_t kSweepCases = 10'000;

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
[[nodiscard]] CalendarDate drawDate(Sampler& sampler) {
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

} // namespace

// --- the type system: the half of this suite that runs at compile time -----

namespace {

template <typename T>
concept AdvancesBySeconds = requires(const T t, Seconds duration) {
    { t + duration } -> std::same_as<T>;
    { t - duration } -> std::same_as<T>;
};

template <typename T>
concept HasDuration = requires(const T later, const T earlier) {
    { later - earlier } -> std::same_as<Seconds>;
};

static_assert(AdvancesBySeconds<TaiTime> && AdvancesBySeconds<TtTime> &&
              AdvancesBySeconds<TdbTime>);
static_assert(HasDuration<TaiTime> && HasDuration<TtTime> && HasDuration<TdbTime>);
static_assert(!AdvancesBySeconds<UtcTime> && !HasDuration<UtcTime>,
              "a UTC day may hold 86401 SI seconds, so a UTC second is not a fixed length");
static_assert(!AdvancesBySeconds<Ut1Time> && !HasDuration<Ut1Time>,
              "UT1 follows the Earth's rotation, which is not uniform");

// No scale converts to another, implicitly or explicitly: a conversion is a
// named function, and those arrive in M1-04 and M1-05. Written over the type
// aliases rather than over TimeScale values: clang represents an enumerator
// substituted into a template as a cast it made itself, and clang-tidy 23's
// modernize-avoid-c-style-cast reports those as if they had been written.
template <typename From, typename To>
constexpr bool kKeptApart = std::is_same_v<From, To> || (!std::is_convertible_v<From, To> &&
                                                         !std::is_constructible_v<To, From>);

template <typename From, typename... To>
constexpr bool kConvertsToNoOther = (kKeptApart<From, To> && ...);

static_assert(kConvertsToNoOther<UtcTime, UtcTime, TaiTime, TtTime, TdbTime, Ut1Time>);
static_assert(kConvertsToNoOther<TaiTime, UtcTime, TaiTime, TtTime, TdbTime, Ut1Time>);
static_assert(kConvertsToNoOther<TtTime, UtcTime, TaiTime, TtTime, TdbTime, Ut1Time>);
static_assert(kConvertsToNoOther<TdbTime, UtcTime, TaiTime, TtTime, TdbTime, Ut1Time>);
static_assert(kConvertsToNoOther<Ut1Time, UtcTime, TaiTime, TtTime, TdbTime, Ut1Time>);

static_assert(!std::is_convertible_v<f64, TtTime> && !std::is_constructible_v<TtTime, f64>);
static_assert(!std::is_convertible_v<TtTime, f64>, "no silent way back to a bare double");
static_assert(!std::is_constructible_v<TtTime, JulianDate> &&
                  !std::is_constructible_v<TtTime, CalendarDate>,
              "a date becomes an instant only through a factory, which reports");
static_assert(!std::is_default_constructible_v<TtTime>,
              "an instant is always a particular one, so whoever holds one says which");
static_assert(std::is_same_v<decltype(kJ2000), const TtTime>, "J2000.0 is defined in TT");
static_assert(std::is_same_v<decltype(kUnixEpoch), const UtcTime>, "POSIX counts from UTC");

} // namespace

// --- the published epochs ---------------------------------------------------

// Each checked both ways: the calendar date to the published Julian date, and
// the published Julian date back to the calendar date.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the published epochs, both ways", "[time][epochs]") {
    struct Case {
        std::string_view name;
        CalendarDate date;
        f64 julianDate;
    };
    constexpr std::array kCases = std::to_array<Case>({
        {
            .name = "J2000.0",
            .date = {.year = 2000, .month = 1, .day = 1, .hour = 12},
            .julianDate = kJ2000JulianDate,
        },
        {
            .name = "MJD zero",
            .date = {.year = 1858, .month = 11, .day = 17},
            .julianDate = kMjdZeroJulianDate,
        },
        {
            .name = "the Unix epoch",
            .date = {.year = 1970, .month = 1, .day = 1},
            .julianDate = kUnixEpochJulianDate,
        },
    });

    for (const Case& c : kCases) {
        CAPTURE(c.name);
        const TtTime t = instant<TimeScale::Tt>(c.date);
        const JulianDate jd = t.julianDate();
        INFO("calendar -> Julian date");
        REQUIRE_THAT(jd.day + jd.fraction, WithinAbsOf(c.julianDate, Tolerance{0.0}));

        const auto back = TtTime::fromJulianDate({.day = c.julianDate, .fraction = 0.0});
        INFO(errorName(back));
        REQUIRE(back.has_value());
        const auto civil = back->toCalendar();
        INFO(errorName(civil));
        REQUIRE(civil.has_value());
        INFO("Julian date -> calendar");
        REQUIRE(civil->year == c.date.year);
        REQUIRE(civil->month == c.date.month);
        REQUIRE(civil->day == c.date.day);
        REQUIRE(civil->hour == c.date.hour);
        REQUIRE(civil->minute == 0);
        REQUIRE_THAT(civil->second.value, WithinAbsOf(0.0, Tolerance{0.0}));
    }
}

// The named constants are the published instants, in the scales that define them.
TEST_CASE("the epoch constants are the published instants", "[time][epochs]") {
    REQUIRE(kJ2000 == instant<TimeScale::Tt>({.year = 2000, .month = 1, .day = 1, .hour = 12}));
    REQUIRE(kUnixEpoch == instant<TimeScale::Utc>({.year = 1970, .month = 1, .day = 1}));

    const JulianDate unix = kUnixEpoch.julianDate();
    REQUIRE_THAT(unix.day + unix.fraction, WithinAbsOf(kUnixEpochJulianDate, Tolerance{0.0}));

    // kMjdZero is an offset between two day counts rather than an instant, so
    // it is checked as one: MJD 0 begins at JD 2400000.5.
    REQUIRE_THAT(kMjdZero, WithinAbsOf(kMjdZeroJulianDate, Tolerance{0.0}));
    const TtTime mjdZero = instant<TimeScale::Tt>({.year = 1858, .month = 11, .day = 17});
    REQUIRE_THAT(mjdZero.modifiedJulianDay(), WithinAbsOf(0.0, Tolerance{0.0}));
    REQUIRE(mjdZero.picosecondOfDay() == 0);
}

// --- the calendar -----------------------------------------------------------

// Every day of 1900-2100 through both calendars. The standard library's is a
// second implementation sharing no formulation with Fliegel and Van Flandern's,
// so agreement on all 73,414 days is evidence rather than repetition.
TEST_CASE("the calendar agrees with the standard library's on every day of 1900-2100",
          "[time][calendar]") {
    const std::chrono::sys_days first{std::chrono::year{1900} / std::chrono::January / 1};
    const std::chrono::sys_days last{std::chrono::year{2100} / std::chrono::December / 31};

    std::int64_t visited = 0;
    for (std::chrono::sys_days day = first; day <= last; day += std::chrono::days{1}) {
        const std::chrono::year_month_day civil{day};
        const CalendarDate date{
            .year = static_cast<std::int32_t>(civil.year()),
            .month = static_cast<std::int32_t>(static_cast<unsigned>(civil.month())),
            .day = static_cast<std::int32_t>(static_cast<unsigned>(civil.day())),
        };
        CAPTURE(date.year, date.month, date.day);

        const auto t = TtTime::fromCalendar(date);
        INFO(errorName(t));
        REQUIRE(t.has_value());
        const auto back = t->toCalendar();
        INFO(errorName(back));
        REQUIRE(back.has_value());

        const bool agrees = nearlyEqual(t->modifiedJulianDay(),
                                        static_cast<f64>(mjdByTheStandardLibrary(civil)),
                                        Tolerance{0.0}) &&
                            t->picosecondOfDay() == 0 && back->year == date.year &&
                            back->month == date.month && back->day == date.day;
        REQUIRE(agrees);
        ++visited;
    }
    INFO("every day was visited");
    REQUIRE(visited == 73'414);
}

// Which dates exist is the other half of a calendar, and the century rule is
// where implementations differ: 1900 and 2100 have no 29 February, 2000 does.
// Every candidate day 28-31 of every month of 1900-2100, and 29 February of
// every year the type supports, against the standard library's ok().
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a date exists exactly when the standard library says it does", "[time][calendar]") {
    const auto agreesAbout = [](const CalendarDate& date) {
        const std::chrono::year_month_day civil{
            std::chrono::year{date.year},
            std::chrono::month{static_cast<unsigned>(date.month)},
            std::chrono::day{static_cast<unsigned>(date.day)},
        };
        const auto t = TtTime::fromCalendar(date);
        return civil.ok() ? t.has_value() : (!t.has_value() && t.error() == TimeError::InvalidDay);
    };

    for (std::int32_t y = 1900; y <= 2100; ++y) {
        for (std::int32_t m = 1; m <= 12; ++m) {
            for (std::int32_t d = 28; d <= 31; ++d) {
                CAPTURE(y, m, d);
                REQUIRE(agreesAbout({.year = y, .month = m, .day = d}));
            }
        }
    }
    for (std::int32_t y = 1; y <= 9999; ++y) {
        CAPTURE(y);
        REQUIRE(agreesAbout({.year = y, .month = 2, .day = 29}));
    }
}

// The supported range is years 1-9999 (decided 2026-09-10), and both ends are
// dates the standard library agrees about.
TEST_CASE("the first and last supported days", "[time][calendar]") {
    const TtTime first = instant<TimeScale::Tt>({.year = 1, .month = 1, .day = 1});
    const TtTime last = instant<TimeScale::Tt>(
        {.year = 9999, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{59.0}});
    const f64 firstMjd = static_cast<f64>(
        mjdByTheStandardLibrary(std::chrono::year{1} / std::chrono::January / std::chrono::day{1}));
    const f64 lastMjd = static_cast<f64>(mjdByTheStandardLibrary(
        std::chrono::year{9999} / std::chrono::December / std::chrono::day{31}));
    REQUIRE_THAT(first.modifiedJulianDay(), WithinAbsOf(firstMjd, Tolerance{0.0}));
    REQUIRE_THAT(last.modifiedJulianDay(), WithinAbsOf(lastMjd, Tolerance{0.0}));
}

namespace {

// The date and the date it comes back as, as one parameter, for the same
// reason as Span: transposed, the tolerance checks would still pass and the
// field checks would read the wrong way round.
struct DateTrip {
    CalendarDate sent;
    CalendarDate returned;
};

// The date survives the trip through an instant: every field but the second
// unchanged, and the second within the budget and within what the design
// promises beneath it.
void checkDateSurvives(const DateTrip& trip) {
    const bool sameFields =
        trip.returned.year == trip.sent.year && trip.returned.month == trip.sent.month &&
        trip.returned.day == trip.sent.day && trip.returned.hour == trip.sent.hour &&
        trip.returned.minute == trip.sent.minute;
    INFO("year, month, day, hour and minute come back unchanged");
    REQUIRE(sameFields);
    INFO("the budget");
    REQUIRE_THAT(trip.returned.second.value, WithinAbsOf(trip.sent.second.value, kRoundTripBudget));
    INFO("the design: the nearest picosecond, then the nearest double");
    REQUIRE_THAT(trip.returned.second.value, WithinAbsOf(trip.sent.second.value, kRoundTripDesign));
}

// One case of the calendar sweep: the date survives the instant, and the
// instant survives the date, bit for bit.
void checkCalendarRoundTrip(const CalendarDate& date) {
    const TtTime t = instant<TimeScale::Tt>(date);
    INFO("normalised");
    REQUIRE(isNormalised(t));

    const auto back = t.toCalendar();
    INFO(errorName(back));
    REQUIRE(back.has_value());
    checkDateSurvives({.sent = date, .returned = *back});

    const auto again = TtTime::fromCalendar(*back);
    INFO(errorName(again));
    REQUIRE(again.has_value());
    INFO("an instant survives the trip through its date bit for bit");
    REQUIRE(*again == t);
}

} // namespace

TEST_CASE("calendar round trip over a seeded sweep of 1900-2100", "[time][calendar]") {
    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const CalendarDate date = drawDate(sampler);
        CAPTURE(kSweepSeed, i);
        checkCalendarRoundTrip(date);
    }
}

namespace {

// A day later, through arithmetic, and the calendar says which date that is.
[[nodiscard]] CalendarDate dayAfter(const CalendarDate& date) {
    const TaiTime next = instant<TimeScale::Tai>(date) + Seconds{86'400.0};
    const auto civil = next.toCalendar();
    INFO(errorName(civil));
    REQUIRE(civil.has_value());
    return *civil;
}

struct Transition {
    CalendarDate from;
    CalendarDate to;
};

} // namespace

// The dates the century rule and the month lengths turn on, crossed by
// arithmetic rather than constructed: 1900 and 2100 are not leap years, 2000
// is, and every month end in a common year and a leap year rolls over into the
// first of the next month.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the century rule, 29 February and every month end", "[time][calendar]") {
    constexpr std::array kTransitions = std::to_array<Transition>({
        {.from = {.year = 1900, .month = 2, .day = 28}, .to = {.year = 1900, .month = 3, .day = 1}},
        {
            .from = {.year = 2000, .month = 2, .day = 28},
            .to = {.year = 2000, .month = 2, .day = 29},
        },
        {.from = {.year = 2000, .month = 2, .day = 29}, .to = {.year = 2000, .month = 3, .day = 1}},
        {.from = {.year = 2100, .month = 2, .day = 28}, .to = {.year = 2100, .month = 3, .day = 1}},
        {
            .from = {.year = 1999, .month = 12, .day = 31},
            .to = {.year = 2000, .month = 1, .day = 1},
        },
    });
    for (const Transition& transition : kTransitions) {
        CAPTURE(transition.from.year, transition.from.month, transition.from.day);
        const CalendarDate next = dayAfter(transition.from);
        REQUIRE(next.year == transition.to.year);
        REQUIRE(next.month == transition.to.month);
        REQUIRE(next.day == transition.to.day);
    }

    for (const std::int32_t y : {1900, 2000, 2023, 2024}) {
        for (std::int32_t m = 1; m <= 12; ++m) {
            const auto end = std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)} /
                             std::chrono::last;
            const auto length = static_cast<std::int32_t>(static_cast<unsigned>(end.day()));
            CAPTURE(y, m, length);

            const auto beyond = TtTime::fromCalendar({.year = y, .month = m, .day = length + 1});
            const bool beyondRefused =
                !beyond.has_value() && beyond.error() == TimeError::InvalidDay;
            INFO("the day after the month's last does not exist -> " << errorName(beyond));
            REQUIRE(beyondRefused);

            const CalendarDate next = dayAfter({.year = y, .month = m, .day = length});
            REQUIRE(next.day == 1);
            REQUIRE(next.month == (m % 12) + 1);
            REQUIRE(next.year == (m == 12 ? y + 1 : y));
        }
    }
}

// --- resolution and drift ---------------------------------------------------

namespace {

// Instants at the two places a day-fraction representation is weakest: the end
// of a day, where the fraction is nearly one and its ulp is largest, and the
// crossing into the next day, where normalisation has to carry.
constexpr std::array kAwkwardInstants = std::to_array<CalendarDate>({
    {.year = 2000, .month = 1, .day = 1},
    {.year = 2000, .month = 1, .day = 1, .hour = 12},
    {.year = 2026, .month = 9, .day = 10, .hour = 17, .minute = 31, .second = Seconds{12.345}},
    {.year = 2026, .month = 9, .day = 10, .hour = 23, .minute = 59, .second = Seconds{59.999999}},
    {
        .year = 2026,
        .month = 9,
        .day = 10,
        .hour = 23,
        .minute = 59,
        .second = Seconds{59.999999999999},
    },
});

void checkOneNanosecond(const TaiTime& t) {
    constexpr Seconds kNanosecond{1e-9};
    const TaiTime later = t + kNanosecond;
    INFO("normalised after the addition");
    REQUIRE(isNormalised(later));
    INFO("exactly 1000 picoseconds apart");
    REQUIRE(picosecondsIn({.from = t, .to = later}) == 1000);

    const Seconds recovered = later - t;
    INFO("the budget");
    REQUIRE_THAT(recovered.value, WithinAbsOf(kNanosecond.value, kResolutionBudget));
    INFO("the design: the difference is the double nearest 1 ns, exactly");
    REQUIRE_THAT(recovered.value, WithinAbsOf(kNanosecond.value, Tolerance{0.0}));
}

} // namespace

// The claim the representation exists to make.
TEST_CASE("t + 1 ns - t recovers 1 ns, at the end of a day and across it", "[time][resolution]") {
    for (const CalendarDate& date : kAwkwardInstants) {
        CAPTURE(date.hour, date.minute, date.second.value);
        checkOneNanosecond(instant<TimeScale::Tai>(date));
    }
    Sampler sampler;
    for (std::size_t i = 0; i < 1000; ++i) {
        const CalendarDate date = drawDate(sampler);
        CAPTURE(kSweepSeed, i, date.year, date.month, date.day, date.second.value);
        checkOneNanosecond(instant<TimeScale::Tai>(date));
    }
}

// A million additions of 1 us, from a time of day far from zero. An f64
// fraction of a day drifts 83 ns here, systematically, because 1 us has no
// exact binary representation and every addition rounds the same way -- and
// it drifts only 6 ps if the run starts at fraction zero, which is J2000 in a
// noon-based day. Starting there would have made this test decoration
// (VERIFICATION.md rule 23). Measured 2026-09-10; see ADR 0009's update.
TEST_CASE("a million additions of 1 us drift by nothing", "[time][resolution]") {
    const TaiTime start = instant<TimeScale::Tai>(
        {.year = 2026, .month = 9, .day = 10, .hour = 17, .minute = 31, .second = Seconds{12.345}});
    constexpr Seconds kMicrosecond{1e-6};

    TaiTime t = start;
    for (int i = 0; i < 1'000'000; ++i) {
        t = t + kMicrosecond;
    }

    const Seconds elapsed = t - start;
    INFO("the budget");
    REQUIRE_THAT(elapsed.value, WithinAbsOf(1.0, kDriftBudget));
    INFO("the design: exactly one second, to the picosecond");
    REQUIRE(picosecondsIn({.from = start, .to = t}) == 1'000'000'000'000);
    REQUIRE_THAT(elapsed.value, WithinAbsOf(1.0, Tolerance{0.0}));
    REQUIRE(t == start + Seconds{1.0});
}

// --- arithmetic -------------------------------------------------------------

namespace {

// A duration and the exact number of picoseconds it must move an instant by:
// the nearest picosecond to the double, worked out by hand for values whose
// decimal expansion makes it obvious.
struct Step {
    std::string_view name;
    Seconds duration;
    std::int64_t picos;
};

constexpr std::array kExactSteps = std::to_array<Step>({
    {.name = "zero", .duration = Seconds{0.0}, .picos = 0},
    {.name = "minus zero", .duration = Seconds{-0.0}, .picos = 0},
    {.name = "the smallest double", .duration = Seconds{5e-324}, .picos = 0},
    {.name = "1 ps", .duration = Seconds{1e-12}, .picos = 1},
    {.name = "-1 ps", .duration = Seconds{-1e-12}, .picos = -1},
    {.name = "0.4 ps rounds to nothing", .duration = Seconds{0.4e-12}, .picos = 0},
    {.name = "0.6 ps rounds to one", .duration = Seconds{0.6e-12}, .picos = 1},
    {.name = "1 ns", .duration = Seconds{1e-9}, .picos = 1'000},
    {.name = "0.1 s", .duration = Seconds{0.1}, .picos = 100'000'000'000},
    {.name = "-0.1 s", .duration = Seconds{-0.1}, .picos = -100'000'000'000},
    {.name = "half a day", .duration = Seconds{43'200.0}, .picos = 43'200'000'000'000'000},
    {.name = "one day", .duration = Seconds{86'400.0}, .picos = kDay},
    {.name = "minus one day", .duration = Seconds{-86'400.0}, .picos = -kDay},
    // A double this size cannot hold a day and one picosecond; the nearest it
    // can hold above a day is one ulp, 2^-36 s = 14.55 ps, which is 15.
    {.name = "a day and one ulp", .duration = Seconds{86'400.0 + 0x1p-36}, .picos = kDay + 15},
    {.name = "ten days", .duration = Seconds{864'000.0}, .picos = 10 * kDay},
    {.name = "a million seconds", .duration = Seconds{1e6}, .picos = 1'000'000'000'000'000'000},
    {
        .name = "minus a million seconds",
        .duration = Seconds{-1e6},
        .picos = -1'000'000'000'000'000'000,
    },
});

// Where the instant starts: midnight, a picosecond either side of it, and noon.
constexpr std::array kStartingPicos = std::to_array<std::int64_t>({
    0,
    1,
    kDay - 1,
    kDay / 2,
});

} // namespace

// Every step from every starting point: the instant stays normalised, moves by
// exactly the picoseconds the step is worth, and comes back when the step is
// taken away. The starting points put the carry into the next day, and the
// borrow from the previous one, under both signs.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("arithmetic moves by the nearest picosecond and stays normalised", "[time][arithmetic]") {
    for (const std::int64_t startPicos : kStartingPicos) {
        // The arithmetic is exercised on TAI; the calendar that built the
        // instant is the same in every scale.
        const auto civil = atPicosecondOfJ2000Day(startPicos).toCalendar();
        INFO(errorName(civil));
        REQUIRE(civil.has_value());
        const TaiTime start = instant<TimeScale::Tai>(*civil);
        REQUIRE(start.picosecondOfDay() == startPicos);
        for (const Step& step : kExactSteps) {
            CAPTURE(startPicos, step.name, step.duration.value);
            const TaiTime moved = start + step.duration;
            INFO("normalised");
            REQUIRE(isNormalised(moved));
            INFO("moved by exactly the step");
            REQUIRE(picosecondsIn({.from = start, .to = moved}) == step.picos);
            INFO("and back again");
            REQUIRE((moved - step.duration) == start);
        }
    }
}

// Durations far beyond anything the simulator takes: the instant must stay
// normalised and finite, and the difference must still be the duration to
// within the duration's own resolution. Precision is out of scope here; a
// corrupt instant is not.
TEST_CASE("enormous durations keep the instant normalised", "[time][arithmetic]") {
    const TaiTime start = instant<TimeScale::Tai>({.year = 2000, .month = 1, .day = 1});
    for (const f64 seconds : {3.15576e9, -3.15576e9, 1e12, -1e12, 1e18, 1e20, -1e20, 1e300}) {
        CAPTURE(seconds);
        const TaiTime moved = start + Seconds{seconds};
        INFO("normalised");
        REQUIRE(isNormalised(moved));
        INFO("the difference is the duration, to the duration's own resolution");
        REQUIRE_THAT((moved - start).value, WithinRelTo(seconds, Tolerance{0x1p-52}));
    }
}

// Ordering agrees with the sign of the difference, and the difference is
// antisymmetric bit for bit: b - a is exactly -(a - b).
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("ordering agrees with the sign of the difference", "[time][arithmetic]") {
    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const TaiTime a = instant<TimeScale::Tai>(drawDate(sampler));
        // Every other pair is close, where the two instants share a day and
        // the difference is decided by the picoseconds alone.
        const TaiTime b = (i % 2 == 0) ? instant<TimeScale::Tai>(drawDate(sampler))
                                       : a + Seconds{(sampler.unit() - 0.5) * 1e-6};
        CAPTURE(kSweepSeed, i, a, b);
        const f64 difference = (b - a).value;
        REQUIRE((a < b) == (difference > 0.0));
        REQUIRE((a > b) == (difference < 0.0));
        REQUIRE((a == b) == nearlyEqual(difference, 0.0, Tolerance{0.0}));
        REQUIRE_THAT((a - b).value, WithinAbsOf(-difference, Tolerance{0.0}));
    }
}

// --- failures, by name ------------------------------------------------------

namespace {

struct Refusal {
    std::string_view name;
    CalendarDate date;
    TimeError error;
};

constexpr CalendarDate kValid{
    .year = 2024,
    .month = 3,
    .day = 15,
    .hour = 10,
    .minute = 20,
    .second = Seconds{30.0},
};

// kValid with one field replaced. Each refusal below breaks exactly one thing,
// so the error it reports can only have come from the check for that thing.
[[nodiscard]] constexpr CalendarDate withYear(std::int32_t year) {
    CalendarDate date = kValid;
    date.year = year;
    return date;
}
[[nodiscard]] constexpr CalendarDate withMonth(std::int32_t month) {
    CalendarDate date = kValid;
    date.month = month;
    return date;
}
[[nodiscard]] constexpr CalendarDate withDay(std::int32_t day) {
    CalendarDate date = kValid;
    date.day = day;
    return date;
}
[[nodiscard]] constexpr CalendarDate withHour(std::int32_t hour) {
    CalendarDate date = kValid;
    date.hour = hour;
    return date;
}
[[nodiscard]] constexpr CalendarDate withMinute(std::int32_t minute) {
    CalendarDate date = kValid;
    date.minute = minute;
    return date;
}
[[nodiscard]] constexpr CalendarDate withSecond(f64 second) {
    CalendarDate date = kValid;
    date.second = Seconds{second};
    return date;
}

} // namespace

// Every way a scenario file can get a date wrong, each asked for by the name
// of the check that must catch it.
TEST_CASE("calendar input is refused by name", "[time][errors]") {
    const std::array kRefusals = std::to_array<Refusal>({
        {.name = "year 0", .date = withYear(0), .error = TimeError::YearOutOfRange},
        {.name = "year 10000", .date = withYear(10'000), .error = TimeError::YearOutOfRange},
        {.name = "year -4713", .date = withYear(-4713), .error = TimeError::YearOutOfRange},
        {.name = "month 0", .date = withMonth(0), .error = TimeError::InvalidMonth},
        {.name = "month 13", .date = withMonth(13), .error = TimeError::InvalidMonth},
        {.name = "day 0", .date = withDay(0), .error = TimeError::InvalidDay},
        {.name = "day 32", .date = withDay(32), .error = TimeError::InvalidDay},
        {
            .name = "30 February",
            .date = {.year = 2024, .month = 2, .day = 30},
            .error = TimeError::InvalidDay,
        },
        {
            .name = "29 February 1900",
            .date = {.year = 1900, .month = 2, .day = 29},
            .error = TimeError::InvalidDay,
        },
        {
            .name = "31 April",
            .date = {.year = 2024, .month = 4, .day = 31},
            .error = TimeError::InvalidDay,
        },
        {.name = "hour 24", .date = withHour(24), .error = TimeError::InvalidTimeOfDay},
        {.name = "hour -1", .date = withHour(-1), .error = TimeError::InvalidTimeOfDay},
        {.name = "minute 60", .date = withMinute(60), .error = TimeError::InvalidTimeOfDay},
        {.name = "minute -1", .date = withMinute(-1), .error = TimeError::InvalidTimeOfDay},
        {.name = "second 60", .date = withSecond(60.0), .error = TimeError::InvalidTimeOfDay},
        {
            .name = "a negative second",
            .date = withSecond(-1e-9),
            .error = TimeError::InvalidTimeOfDay,
        },
        {.name = "a NaN second", .date = withSecond(kNaN), .error = TimeError::NotFinite},
        {.name = "an infinite second", .date = withSecond(kInf), .error = TimeError::NotFinite},
        {
            .name = "a negative infinite second",
            .date = withSecond(-kInf),
            .error = TimeError::NotFinite,
        },
        // Refused by name, first (ADR 0002): a date wrong in two ways reports
        // the NaN, which says the value is corrupt rather than merely wrong.
        {
            .name = "month 13 and a NaN second",
            .date = {.year = 2024, .month = 13, .day = 1, .second = Seconds{kNaN}},
            .error = TimeError::NotFinite,
        },
    });

    for (const Refusal& refusal : kRefusals) {
        const auto t = TtTime::fromCalendar(refusal.date);
        const bool refused = !t.has_value() && t.error() == refusal.error;
        INFO(refusal.name << " -> " << errorName(t));
        REQUIRE(refused);
    }

    // UTC has no leap seconds in M1-03: the table arrives in M1-04, which will
    // accept 23:59:60 on exactly the days it lists and still refuse this on
    // every other day.
    const auto leapSecond = UtcTime::fromCalendar(
        {.year = 2016, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{60.0}});
    const bool leapSecondRefused =
        !leapSecond.has_value() && leapSecond.error() == TimeError::InvalidTimeOfDay;
    INFO("23:59:60 UTC, before the leap-second table exists -> " << errorName(leapSecond));
    REQUIRE(leapSecondRefused);

    // Minus zero is zero, not a negative second.
    REQUIRE(TtTime::fromCalendar(withSecond(-0.0)).has_value());
}

// Every error has a description of its own.
TEST_CASE("every time error describes itself", "[time][errors]") {
    constexpr std::array kErrors = std::to_array<TimeError>({
        TimeError::NotFinite,
        TimeError::YearOutOfRange,
        TimeError::InvalidMonth,
        TimeError::InvalidDay,
        TimeError::InvalidTimeOfDay,
    });
    for (std::size_t i = 0; i < kErrors.size(); ++i) {
        CAPTURE(i);
        REQUIRE(!describe(kErrors.at(i)).empty());
        for (std::size_t j = 0; j < i; ++j) {
            REQUIRE(describe(kErrors.at(i)) != describe(kErrors.at(j)));
        }
    }
}

// Arithmetic may carry an instant past the calendar's range -- that is not an
// error in itself -- and the calendar then says so by name.
TEST_CASE("an instant past the calendar's range is reported, not converted", "[time][errors]") {
    const TaiTime lastSecond = instant<TimeScale::Tai>(
        {.year = 9999, .month = 12, .day = 31, .hour = 23, .minute = 59, .second = Seconds{59.0}});
    const auto beyond = (lastSecond + Seconds{1.0}).toCalendar();
    const bool beyondRefused = !beyond.has_value() && beyond.error() == TimeError::YearOutOfRange;
    INFO("10000-01-01 -> " << errorName(beyond));
    REQUIRE(beyondRefused);
    REQUIRE(lastSecond.toCalendar().has_value());

    const TaiTime firstDay = instant<TimeScale::Tai>({.year = 1, .month = 1, .day = 1});
    const auto before = (firstDay - Seconds{1e-12}).toCalendar();
    const bool beforeRefused = !before.has_value() && before.error() == TimeError::YearOutOfRange;
    INFO("a picosecond before 0001-01-01 -> " << errorName(before));
    REQUIRE(beforeRefused);
}

// --- Julian dates -----------------------------------------------------------

namespace {

// Picoseconds into a day, and the nearest double to that fraction of the day.
// Every expected value is Python's float(Fraction(picos, 86400 * 10**12)):
// exact rational arithmetic, correctly rounded, and nothing shared with
// Time.hpp. Chosen to include the powers of two where an int64 stops fitting
// exactly in a double, the last picoseconds of a day -- where 1.0 is the
// correctly rounded answer and must be carried into the next day -- and a
// case 0.021 ulp from a rounding tie. Generated 2026-09-10.
struct PicosToFraction {
    std::int64_t picos;
    f64 fraction;
};

constexpr std::array kPicosToFraction = std::to_array<PicosToFraction>({
    {.picos = 0LL, .fraction = 0x0.0p+0},
    {.picos = 1LL, .fraction = 0x1.ab0209f8f9bfap-57},
    {.picos = 999LL, .fraction = 0x1.a0953d3aa5a6bp-47},
    {.picos = 1000000000000LL, .fraction = 0x1.845c8a0ce5129p-17},
    {.picos = 43200000000000000LL, .fraction = 0x1.0000000000000p-1},
    {.picos = 86399999999999999LL, .fraction = 0x1.0000000000000p+0},
    {.picos = 86399999999999996LL, .fraction = 0x1.0000000000000p+0},
    {.picos = 86399999999999995LL, .fraction = 0x1.fffffffffffffp-1},
    {.picos = 9007199254740991LL, .fraction = 0x1.ab0209f8f9bf9p-4},
    {.picos = 9007199254740992LL, .fraction = 0x1.ab0209f8f9bfap-4},
    {.picos = 9007199254740993LL, .fraction = 0x1.ab0209f8f9bfbp-4},
    {.picos = 72057594037927935LL, .fraction = 0x1.ab0209f8f9bfap-1},
    {.picos = 72057594037927936LL, .fraction = 0x1.ab0209f8f9bfap-1},
    {.picos = 72057594037927937LL, .fraction = 0x1.ab0209f8f9bfap-1},
    {.picos = 45296789012345678LL, .fraction = 0x1.0c6ce81651ff0p-1},
    {.picos = 17063028386853610LL, .fraction = 0x1.9474fbee77d7ep-3},
    {.picos = 11225482232425174LL, .fraction = 0x1.0a15e3a3f8c66p-3},
    {.picos = 52723655637741169LL, .fraction = 0x1.386fbd0339453p-1},
    {.picos = 49674370757182787LL, .fraction = 0x1.265ddc380b224p-1},
    {.picos = 36627870082489142LL, .fraction = 0x1.b21babc709cf4p-2},
    {.picos = 2153697719679687LL, .fraction = 0x1.9867a79624cb5p-6},
    {.picos = 25161765650108051LL, .fraction = 0x1.2a36a92b4c35ep-2},
    {.picos = 57362784896550556LL, .fraction = 0x1.53ed781fd3f47p-1},
    {.picos = 77649155232931669LL, .fraction = 0x1.cc24a4f648b43p-1},
    {.picos = 74564022553731208LL, .fraction = 0x1.b9dc6243a1abbp-1},
    {.picos = 70190759626900210LL, .fraction = 0x1.9ff1fb651f9b3p-1},
    {.picos = 84154353329287078LL, .fraction = 0x1.f2b14555030fep-1},
});

// A fraction of a day, and the nearest picosecond to it, ties upward. Every
// expected value is Python's floor(Fraction(f) * 86400 * 10**12 + 1/2), exact.
// 2^-20 of a day is exactly 82397460937.5 ps -- the tie. Generated 2026-09-10.
struct FractionToPicos {
    f64 fraction;
    std::int64_t picos;
};

constexpr std::array kFractionToPicos = std::to_array<FractionToPicos>({
    {.fraction = 0x0.0p+0, .picos = 0LL},
    {.fraction = 0x1.0000000000000p-1, .picos = 43200000000000000LL},
    {.fraction = 0x1.0000000000000p-2, .picos = 21600000000000000LL},
    {.fraction = 0x1.0000000000000p-20, .picos = 82397460938LL},
    {.fraction = 0x1.fffffffffffffp-1, .picos = 86399999999999990LL},
    {.fraction = 0x1.999999999999ap-4, .picos = 8640000000000000LL},
    {.fraction = 0x1.5555555555555p-2, .picos = 28799999999999998LL},
    {.fraction = 0x1.7664609595e63p-1, .picos = 63178666665706664LL},
    {.fraction = 0x1.216744a0a1230p-3, .picos = 12209205554518373LL},
    {.fraction = 0x1.19bd990ebe628p-2, .picos = 23771864536046915LL},
    {.fraction = 0x1.c5f6c25a19f08p-3, .picos = 19151602160999160LL},
    {.fraction = 0x1.bf934f9b2a158p-2, .picos = 37764177196740833LL},
    {.fraction = 0x1.9437245f88c37p-1, .picos = 68211348540866475LL},
    {.fraction = 0x1.588916232ad39p-1, .picos = 58140364619164485LL},
    {.fraction = 0x1.d6b8ce9ec1d84p-1, .picos = 79434321092979819LL},
    {.fraction = 0x1.39a8222060889p-1, .picos = 52929580060460144LL},
    {.fraction = 0x1.223cd128024d4p-1, .picos = 48977589342088537LL},
    {.fraction = 0x1.5b454d719e550p-3, .picos = 14650483202533711LL},
    {.fraction = 0x1.45703a317b5bcp-1, .picos = 54917727968100628LL},
    {.fraction = 0x1.30c56a91eebb6p-1, .picos = 51430132807857826LL},
});

// The Julian date of the midnight that begins 2000-01-01.
constexpr f64 kJ2000Midnight = 2451544.5;

} // namespace

// julianDate() returns the nearest double, and carries into the next day when
// the nearest double is a whole day.
TEST_CASE("an instant becomes the nearest Julian date", "[time][julian]") {
    for (const PicosToFraction& c : kPicosToFraction) {
        CAPTURE(c.picos);
        const JulianDate jd = atPicosecondOfJ2000Day(c.picos).julianDate();
        const bool carried = c.fraction >= 1.0;
        INFO("the day part is the midnight that begins the day");
        REQUIRE_THAT(jd.day, WithinAbsOf(kJ2000Midnight + (carried ? 1.0 : 0.0), Tolerance{0.0}));
        INFO("the fraction is the correctly rounded double");
        REQUIRE_THAT(jd.fraction, WithinAbsOf(carried ? 0.0 : c.fraction, Tolerance{0.0}));
    }
}

// fromJulianDate returns the nearest picosecond.
TEST_CASE("a Julian date becomes the nearest picosecond", "[time][julian]") {
    for (const FractionToPicos& c : kFractionToPicos) {
        CAPTURE(c.fraction);
        const auto t = TtTime::fromJulianDate({.day = kJ2000Midnight, .fraction = c.fraction});
        INFO(errorName(t));
        REQUIRE(t.has_value());
        REQUIRE_THAT(t->modifiedJulianDay(), WithinAbsOf(51'544.0, Tolerance{0.0}));
        REQUIRE(t->picosecondOfDay() == c.picos);
    }
}

// A two-part Julian date may be split any way at all: every split below is
// J2000.0, and every one must come out as exactly kJ2000.
TEST_CASE("a Julian date may be split any way", "[time][julian]") {
    constexpr std::array kSplits = std::to_array<JulianDate>({
        {.day = 2451545.0, .fraction = 0.0},
        {.day = 2451544.5, .fraction = 0.5},
        {.day = 2400000.5, .fraction = 51544.5},
        {.day = 0.0, .fraction = 2451545.0},
        {.day = 2451546.0, .fraction = -1.0},
        {.day = 2451545.25, .fraction = -0.25},
        {.day = -0.5, .fraction = 2451545.5},
    });
    for (const JulianDate& split : kSplits) {
        CAPTURE(split.day, split.fraction);
        const auto t = TtTime::fromJulianDate(split);
        INFO(errorName(t));
        REQUIRE(t.has_value());
        REQUIRE(*t == kJ2000);
    }
}

// Julian dates that are not numbers, or not within the supported years.
TEST_CASE("a Julian date is refused by name", "[time][julian][errors]") {
    struct Case {
        std::string_view name;
        JulianDate date;
        TimeError error;
    };
    constexpr std::array kCases = std::to_array<Case>({
        {.name = "NaN day", .date = {.day = kNaN, .fraction = 0.0}, .error = TimeError::NotFinite},
        {
            .name = "infinite fraction",
            .date = {.day = 2451545.0, .fraction = kInf},
            .error = TimeError::NotFinite,
        },
        {
            .name = "JD 0, in 4714 BC",
            .date = {.day = 0.0, .fraction = 0.0},
            .error = TimeError::YearOutOfRange,
        },
        {
            .name = "JD 1e300",
            .date = {.day = 1e300, .fraction = 0.0},
            .error = TimeError::YearOutOfRange,
        },
        {
            .name = "10000-01-01T00:00",
            .date = {.day = 5373484.5, .fraction = 0.0},
            .error = TimeError::YearOutOfRange,
        },
        {
            .name = "a microsecond before 0001-01-01",
            .date = {.day = 1721425.5, .fraction = -1e-6 / 86'400.0},
            .error = TimeError::YearOutOfRange,
        },
    });
    for (const Case& c : kCases) {
        const auto t = TtTime::fromJulianDate(c.date);
        const bool refused = !t.has_value() && t.error() == c.error;
        INFO(c.name << " -> " << errorName(t));
        REQUIRE(refused);
    }

    // And the two ends that are inside.
    REQUIRE(TtTime::fromJulianDate({.day = 1721425.5, .fraction = 0.0}).has_value());
    REQUIRE(TtTime::fromJulianDate({.day = 5373484.5, .fraction = -1e-9}).has_value());
}

// An instant through its Julian date and back lands within 5 ps. The Julian
// date's fraction is a double, whose ulp near one is 9.6 ps: correctly rounded
// it is within 4.8 ps of the instant, and the nearest picosecond to that is
// within 5 -- which is also inside the 10 ps resolution budget.
TEST_CASE("an instant survives its Julian date to within 5 ps", "[time][julian]") {
    Sampler sampler;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const TaiTime t = instant<TimeScale::Tai>(drawDate(sampler));
        CAPTURE(kSweepSeed, i, t);
        const auto back = TaiTime::fromJulianDate(t.julianDate());
        INFO(errorName(back));
        REQUIRE(back.has_value());
        const std::int64_t error = picosecondsIn({.from = t, .to = *back});
        CAPTURE(error);
        REQUIRE(error >= -5);
        REQUIRE(error <= 5);
    }
}
