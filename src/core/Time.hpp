#ifndef ORBSIM_CORE_TIME_HPP
#define ORBSIM_CORE_TIME_HPP
//
// An instant of time, and the time scale it is measured in.
//
// ADR 0009 and its update of 2026-09-10. A bare Seconds since an unstated
// epoch is not a time (ADR 0006): TT and TDB differ by under 2 ms, UTC and TAI
// by 37 s, UT1 and UTC by under a second, and confusing any two gives a
// trajectory that is plausible, self-consistent and wrong. So the scale is the
// type's parameter -- TtTime and TdbTime are different types, exactly as
// Radians and Degrees are -- and converting between scales is a named
// function, which M1-04 and M1-05 supply. This header is the representation.
//
// The storage is the day, and the time within it.
//
//   * The day is a Modified Julian Day number: whole-valued, held in an f64.
//     MJD rather than JD, because a day that begins at midnight is a civil
//     day, and UTC's leap second comes at the end of one; ERFA's convention for
//     UTC divides its days at midnight for the same reason, and the IERS
//     tables M1-04 and M1-05 read are keyed by MJD. An f64 rather than an
//     integer, so that no finite duration -- however absurd -- can overflow it.
//   * The time within the day is a count of SI picoseconds, in an int64, in
//     [0, 86 400 * 10^12). A day of 86 401 s fits 106 times over.
//
// Why integer picoseconds and not an f64 fraction of a day, which is what ADR
// 0009 first said: measured on 2026-09-10, a million additions of 1 us to an
// f64 fraction drift 83 ns, against a budget of 1 ns, because 1 us has no exact
// binary representation and every addition rounds it the same way. In
// integer picoseconds the same run is exact, as is every decimal step a
// simulation clock will take, and the resolution is 1 ps everywhere in the
// day, where an f64 fraction's ulp is 9.6 ps through the whole second half of
// it. The budget is 10 ps.
//
// Duration arithmetic exists only where a second has a fixed length: TAI, TT
// and TDB. A UTC day may hold 86 401 SI seconds, and UT1 follows the Earth's
// rotation, so adding a Seconds to either is not a well-defined operation --
// and making it a compile error is cheaper than making it a rule.
//
// The difference of two instants is a Seconds, one f64, whose resolution is
// its own ulp: 15 ps across a day, 3.7 ns across a year, 0.5 us across a
// century. An instant is exact; a long span between two of them, written as
// one double, cannot be. That is why durations beyond what an f64 second count
// holds precisely are out of scope (M1-03).
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace orb {

// The five scales. The explicit base is interface, not optimisation: it makes
// the representation part of the declaration (see OrbitError).
enum class TimeScale : std::uint8_t { Utc, Tai, Tt, Tdb, Ut1 };

// The scales on which a second has a fixed length, and so the only ones on
// which adding a Seconds means anything.
[[nodiscard]] constexpr bool isUniform(TimeScale scale) noexcept {
    return scale == TimeScale::Tai || scale == TimeScale::Tt || scale == TimeScale::Tdb;
}

// The same, as a constraint. A concept rather than `requires(isUniform(S))`
// because a function call in a requires-clause must be parenthesised, and
// clang-tidy 23's readability-redundant-parentheses reports those parentheses
// as redundant -- its fix would not compile. A named constraint needs none.
template <TimeScale Scale>
concept UniformScale = isUniform(Scale);

// --- errors ----------------------------------------------------------------

// Conditions a caller can produce: a date from a scenario file, a Julian date
// from a fixture. Each field of a calendar date has its own error, so a test
// asking for one by name proves the check for that field is the one that
// fired. Conditions only a bug can produce are asserted; see core/Contract.hpp.
enum class TimeError : std::uint8_t {
    NotFinite,        // NaN or infinity where a number was expected
    YearOutOfRange,   // outside 1-9999, the years this calendar supports
    InvalidMonth,     // outside 1-12
    InvalidDay,       // a day that month does not have, in that year
    InvalidTimeOfDay, // an hour, minute or second outside 00:00:00 to 23:59:59.999...
};

[[nodiscard]] constexpr std::string_view describe(TimeError error) noexcept {
    switch (error) {
    case TimeError::NotFinite:
        return "a component is not finite: NaN or infinity where a number was expected";
    case TimeError::YearOutOfRange:
        return "the year is outside 1 to 9999, the years this calendar supports";
    case TimeError::InvalidMonth:
        return "the month is outside 1 to 12";
    case TimeError::InvalidDay:
        return "that month does not have that day, in that year";
    case TimeError::InvalidTimeOfDay:
        return "the time of day is outside 00:00:00 to 23:59:59.999...";
    }
    return "unknown time error";
}

// --- dates -----------------------------------------------------------------

// A civil date and time of day in the proleptic Gregorian calendar, which is
// ISO 8601's. One struct rather than six parameters, so that a year, a month
// and a day cannot be transposed at a call site. The second is in [0, 60);
// M1-04 admits 60 on the days UTC has a leap second.
//
// Every field has an initializer of its own, so a designated initializer may
// stop at the day or the hour. `second` needs one written out although Seconds
// initialises itself: gcc's -Wmissing-field-initializers reports a field left
// out of a designated initializer unless the field has one, and `{}` there is
// what readability-redundant-member-init removes. `{0.0}` calls a different
// constructor, so neither objects. Found by the linux-gcc preset, 2026-09-10.
struct CalendarDate {
    std::int32_t year{};
    std::int32_t month{};
    std::int32_t day{};
    std::int32_t hour{};
    std::int32_t minute{};
    Seconds second{0.0};
};

// A Julian date in two parts, day + fraction, in days.
//
// Going in, the split may be anything at all -- ERFA's convention -- so a
// Julian date read as one number is {jd, 0.0}. Coming out of julianDate(),
// `day` is the Julian date of the midnight that begins the day, which ends in
// .5, and `fraction` is the part of the day since, in [0, 1): the split that
// leaves the most resolution in the fraction.
struct JulianDate {
    f64 day{};
    f64 fraction{};
};

// The SI prefix pico, 10^-12: exact by definition.
inline constexpr std::int64_t kPicosecondsPerSecond = 1'000'000'000'000;

// The day of the Julian date, 86 400 SI seconds. A UTC day with a leap second
// is a second longer, and that is M1-04's.
inline constexpr std::int64_t kSecondsPerDay = 86'400;

inline constexpr std::int64_t kPicosecondsPerDay = kSecondsPerDay * kPicosecondsPerSecond;

// The Julian date at which the Modified Julian Date is zero, 1858-11-17T00:00:
// MJD = JD - 2 400 000.5. The Smithsonian Astrophysical Observatory's
// convention of 1957, recognised by IAU 1997 Resolution B1. An offset between
// two day counts, in days, rather than an instant -- which is why it is a
// number and not a TimePoint.
inline constexpr f64 kMjdZero = 2400000.5;

// --- the arithmetic beneath TimePoint ---------------------------------------

namespace detail {

// Both exact: 10^12 < 2^53, and 86 400 * 10^12 = 2^19 * 164 794 921 875, which
// has 38 significant bits.
inline constexpr f64 kPicosecondsPerSecondF = 1e12;
inline constexpr f64 kPicosecondsPerDayF = 86'400e12;
inline constexpr f64 kSecondsPerDayF = 86'400.0;

// The Julian Day Number names a civil date by the noon that falls on it, so
// its midnight is JD = JDN - 0.5, and MJD = JD - 2 400 000.5 = JDN - 2 400 001.
inline constexpr std::int64_t kJdnOfMjdZero = 2'400'001;

// The years the calendar supports, decided 2026-09-10: ISO 8601's four-digit
// years, which the MFD prints (M1-81). The Gregorian calendar began in 1582 and
// is extended backwards by ISO 8601's proleptic rule.
inline constexpr std::int64_t kFirstYear = 1;
inline constexpr std::int64_t kLastYear = 9'999;

// An int64 holds 2^63 ps, 106.7 days; a difference of whole days below this
// leaves room for the time-of-day term and is computed exactly.
inline constexpr f64 kExactSpanDays = 100.0;

struct CivilDay {
    std::int64_t year{};
    std::int64_t month{};
    std::int64_t day{};
};

// The Gregorian rule, as the papal bull Inter gravissimas set it in 1582: every
// fourth year, except the century years not divisible by 400.
[[nodiscard]] constexpr bool isLeapYear(std::int64_t year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

[[nodiscard]] constexpr std::int64_t daysInMonth(const CivilDay& date) noexcept {
    if (date.month == 2) return isLeapYear(date.year) ? 29 : 28;
    if (date.month == 4 || date.month == 6 || date.month == 9 || date.month == 11) return 30;
    return 31;
}

// The Julian Day Number of a Gregorian date, and back. Fliegel, H. F. and Van
// Flandern, T. C., "A Machine Algorithm for Processing Calendar Dates",
// Communications of the ACM 11(10), p. 657, 1968. doi:10.1145/364096.364097.
//
// The algorithm relies on integer division truncating toward zero, as the
// Fortran it was written in does and as C++'s does: (month - 14) / 12 is -1 for
// January and February and 0 for the rest. Every other intermediate is
// positive across the supported years. The constants are the paper's, and
// civilDay keeps its working variables L, N, I and J, lower-cased -- its K is
// `day` -- so the code can be checked against it line by line.
[[nodiscard]] constexpr std::int64_t julianDayNumber(const CivilDay& date) noexcept {
    const std::int64_t a = (date.month - 14) / 12;
    return date.day - 32'075 + ((1'461 * (date.year + 4'800 + a)) / 4) +
           ((367 * (date.month - 2 - (a * 12))) / 12) - ((3 * ((date.year + 4'900 + a) / 100)) / 4);
}

[[nodiscard]] constexpr CivilDay civilDay(std::int64_t jdn) noexcept {
    std::int64_t l = jdn + 68'569;
    const std::int64_t n = (4 * l) / 146'097;
    l -= ((146'097 * n) + 3) / 4;
    const std::int64_t i = (4'000 * (l + 1)) / 1'461'001;
    l = l - ((1'461 * i) / 4) + 31;
    const std::int64_t j = (80 * l) / 2'447;
    const std::int64_t day = l - ((2'447 * j) / 80);
    l = j / 11;
    return CivilDay{.year = (100 * (n - 49)) + i + l, .month = j + 2 - (12 * l), .day = day};
}

// The first and last days of the supported range. JDN 1 721 426 and
// 5 373 484 were checked against an independent calendar (Python's
// proleptic Gregorian ordinal) before this was written, and the suite checks
// them against the C++ standard library's.
inline constexpr std::int64_t kFirstMjd =
    julianDayNumber({.year = kFirstYear, .month = 1, .day = 1}) - kJdnOfMjdZero;
inline constexpr std::int64_t kLastMjd =
    julianDayNumber({.year = kLastYear, .month = 12, .day = 31}) - kJdnOfMjdZero;
static_assert(kFirstMjd == 1'721'426 - kJdnOfMjdZero && kLastMjd == 5'373'484 - kJdnOfMjdZero);

// False for NaN, which fails both comparisons.
[[nodiscard]] constexpr bool inCalendarRange(f64 mjd) noexcept {
    return mjd >= static_cast<f64>(kFirstMjd) && mjd <= static_cast<f64>(kLastMjd);
}

// The nearest integer, halves away from zero -- std::llround's rule -- without
// <cmath>, so that it runs at compile time. value - whole is exact below 2^52,
// which every caller is far inside: the largest value rounded here is under
// 6e12.
[[nodiscard]] constexpr std::int64_t roundHalfAwayFromZero(f64 value) noexcept {
    ORBSIM_EXPECTS(value > -0x1p52 && value < 0x1p52);
    const auto whole = static_cast<std::int64_t>(value); // truncates toward zero
    const f64 rest = value - static_cast<f64>(whole);
    if (rest >= 0.5) return whole + 1;
    if (rest <= -0.5) return whole - 1;
    return whole;
}

// No value that is not finite is a whole number, and every f64 of magnitude
// 2^52 or more is one. The finiteness test comes first, because what follows
// it casts to an integer, which a NaN or an infinity would make undefined.
[[nodiscard]] constexpr bool isWhole(f64 value) noexcept {
    constexpr f64 kEveryValueWholeFrom = 0x1p52;
    if (!std::isfinite(value)) return false;
    if (value <= -kEveryValueWholeFrom || value >= kEveryValueWholeFrom) return true;
    return nearlyEqual(static_cast<f64>(static_cast<std::int64_t>(value)), value, Tolerance{0.0});
}

// A day number and picoseconds, before or after normalisation.
struct DayAndPicos {
    f64 mjd{};
    std::int64_t picos{};
};

// What a Release build returns from arithmetic whose precondition was violated
// (see operator+): a day that is NaN, so the failure reaches every consumer as
// a NaN rather than as a plausible wrong instant.
inline constexpr DayAndPicos kNotAnInstant{
    .mjd = std::numeric_limits<f64>::quiet_NaN(),
    .picos = 0,
};

// Moves whole days out of the picoseconds until they lie within one day, by
// floor division, so that a negative count borrows from the day before.
[[nodiscard]] constexpr DayAndPicos carry(DayAndPicos parts) noexcept {
    std::int64_t days = parts.picos / kPicosecondsPerDay;
    std::int64_t picos = parts.picos % kPicosecondsPerDay;
    if (picos < 0) {
        picos += kPicosecondsPerDay;
        --days;
    }
    return {.mjd = parts.mjd + static_cast<f64>(days), .picos = picos};
}

// NotFinite first (ADR 0002: a NaN says the input is corrupt, not merely
// wrong), then each field in turn, so a date wrong in one way reports that
// way.
[[nodiscard]] constexpr std::expected<void, TimeError> validate(const CalendarDate& date) noexcept {
    if (!std::isfinite(date.second.value)) return std::unexpected(TimeError::NotFinite);
    if (date.year < kFirstYear || date.year > kLastYear) {
        return std::unexpected(TimeError::YearOutOfRange);
    }
    if (date.month < 1 || date.month > 12) return std::unexpected(TimeError::InvalidMonth);
    const CivilDay civil{.year = date.year, .month = date.month, .day = date.day};
    if (date.day < 1 || date.day > daysInMonth(civil)) {
        return std::unexpected(TimeError::InvalidDay);
    }
    const bool withinTheDay = date.hour >= 0 && date.hour < 24 && date.minute >= 0 &&
                              date.minute < 60 && date.second.value >= 0.0 &&
                              date.second.value < 60.0;
    if (!withinTheDay) return std::unexpected(TimeError::InvalidTimeOfDay);
    return {};
}

// The time of day of a validated date, to the nearest picosecond. The second
// is split before it is scaled: the whole seconds are exact integers, and the
// part below one second, times 10^12, stays under 2^40, where an f64 resolves
// 1.2e-4 ps. The result can be a whole day -- 23:59:59.9999999999996 is the
// next midnight to the nearest picosecond -- and carry() takes it from there.
[[nodiscard]] constexpr std::int64_t picosecondsIntoDay(const CalendarDate& date) noexcept {
    const auto wholeSecond = static_cast<std::int64_t>(date.second.value);   // [0, 59]
    const f64 subSecond = date.second.value - static_cast<f64>(wholeSecond); // exact
    const std::int64_t secondOfDay =
        (((static_cast<std::int64_t>(date.hour) * 60) + date.minute) * 60) + wholeSecond;
    return (secondOfDay * kPicosecondsPerSecond) +
           roundHalfAwayFromZero(subSecond * kPicosecondsPerSecondF);
}

// fraction * kPicosecondsPerDay without losing a picosecond, for a fraction in
// [0, 1]: the whole part exactly, and the rest to 2.4e-4 ps.
//
// The fraction is split at 2^-15. Its leading 15 bits times the day's 38
// significant bits make 53, so that product is exact, and a whole number of
// picoseconds. What is left is below 2^-15, and times the day it is below
// 2^41.3, where an f64 resolves 2^-11 ps. Both splits are exact: scaling by a
// power of two, flooring, and subtracting the part that was kept.
struct Scaled {
    std::int64_t whole{};
    f64 rest{};
};

[[nodiscard]] inline Scaled scaleDayFraction(f64 fraction) noexcept {
    ORBSIM_EXPECTS(fraction >= 0.0 && fraction <= 1.0);
    constexpr f64 kSplit = 32'768.0; // 2^15
    const f64 high = std::floor(fraction * kSplit) / kSplit;
    const f64 low = fraction - high;
    return {
        .whole = static_cast<std::int64_t>(high * kPicosecondsPerDayF),
        .rest = low * kPicosecondsPerDayF,
    };
}

// One part of a Julian date as whole days plus picoseconds, exactly. Truncating
// rather than flooring keeps value - days exact for a negative value too.
struct DaysAndScaled {
    f64 days{};
    Scaled picos;
};

[[nodiscard]] inline DaysAndScaled splitDays(f64 value) noexcept {
    const f64 days = std::trunc(value);
    const f64 fraction = value - days; // exact; (-1, 1), with the sign of value
    const Scaled scaled = scaleDayFraction(std::fabs(fraction));
    if (fraction < 0.0) {
        return {.days = days, .picos = {.whole = -scaled.whole, .rest = -scaled.rest}};
    }
    return {.days = days, .picos = scaled};
}

// start + duration, to the nearest picosecond of the duration's exact value.
//
// The duration is split into whole days and a remainder by fmod, which is
// exact, and the remainder into whole seconds and a part below one, both exact
// again, before anything is scaled. Below about 10^18 s -- thirty billion
// years -- every step is exact. Beyond that the quotient is rounded back to a
// whole number of days, which stays exact to about 3e20 s and then errs by
// about a day, where the duration's own resolution is already most of one.
[[nodiscard]] inline DayAndPicos advance(DayAndPicos start, Seconds duration) noexcept {
    const f64 remainder = std::fmod(duration.value, kSecondsPerDayF);
    const f64 days = std::round((duration.value - remainder) / kSecondsPerDayF);
    const f64 wholeSeconds = std::trunc(remainder);
    const f64 subSecond = remainder - wholeSeconds;
    const std::int64_t picos = (static_cast<std::int64_t>(wholeSeconds) * kPicosecondsPerSecond) +
                               roundHalfAwayFromZero(subSecond * kPicosecondsPerSecondF);
    return carry({.mjd = start.mjd + days, .picos = start.picos + picos});
}

// Used to prove, below, that duration arithmetic exists only where it means
// something.
template <typename T>
concept AddsSeconds = requires(const T t, Seconds duration) { t + duration; };

} // namespace detail

// --- TimePoint --------------------------------------------------------------

// An instant on one time scale. Normalised by construction -- a whole-valued
// day and a time of day inside it -- and there is no way to build one except
// from a validated date, a validated Julian date, or another instant; so the
// normalisation is a postcondition, asserted, because only a bug in this file
// can break it.
template <TimeScale Scale> class TimePoint {
public:
    // Reports a date a scenario file can get wrong, field by field, and one
    // that rounds to the next midnight past 9999-12-31 as out of range.
    [[nodiscard]] static constexpr std::expected<TimePoint, TimeError>
    fromCalendar(const CalendarDate& date) noexcept {
        if (const auto valid = detail::validate(date); !valid) {
            return std::unexpected(valid.error());
        }
        const std::int64_t jdn =
            detail::julianDayNumber({.year = date.year, .month = date.month, .day = date.day});
        const detail::DayAndPicos parts = detail::carry({
            .mjd = static_cast<f64>(jdn - detail::kJdnOfMjdZero),
            .picos = detail::picosecondsIntoDay(date),
        });
        if (!detail::inCalendarRange(parts.mjd)) return std::unexpected(TimeError::YearOutOfRange);
        const TimePoint instant{parts};
        ORBSIM_ENSURES(instant.isNormalised());
        return instant;
    }

    // The nearest picosecond to a Julian date split any way. Reports one that
    // is not finite, or that falls outside the supported years.
    [[nodiscard]] static std::expected<TimePoint, TimeError>
    fromJulianDate(JulianDate date) noexcept {
        if (!std::isfinite(date.day) || !std::isfinite(date.fraction)) {
            return std::unexpected(TimeError::NotFinite);
        }
        const detail::DaysAndScaled first = detail::splitDays(date.day);
        const detail::DaysAndScaled second = detail::splitDays(date.fraction);
        // JD = MJD + 2 400 000.5: the whole days lose 2 400 000, and the
        // picoseconds half a day. The two rests are summed before the one
        // rounding, so the result is the nearest picosecond to the whole date.
        const std::int64_t picos =
            first.picos.whole + second.picos.whole +
            detail::roundHalfAwayFromZero(first.picos.rest + second.picos.rest) -
            (kPicosecondsPerDay / 2);
        const detail::DayAndPicos parts =
            detail::carry({.mjd = (first.days + second.days) - 2'400'000.0, .picos = picos});
        if (!detail::inCalendarRange(parts.mjd)) return std::unexpected(TimeError::YearOutOfRange);
        const TimePoint instant{parts};
        ORBSIM_ENSURES(instant.isNormalised());
        return instant;
    }

    // The calendar date and time of day. Arithmetic may carry an instant past
    // the supported years without error; this is where that is reported.
    [[nodiscard]] constexpr std::expected<CalendarDate, TimeError> toCalendar() const noexcept {
        // Reachable only from a Release build whose arithmetic precondition was
        // violated; without it, the cast below would be undefined behaviour.
        if (!std::isfinite(mjd_)) return std::unexpected(TimeError::NotFinite);
        if (!detail::inCalendarRange(mjd_)) return std::unexpected(TimeError::YearOutOfRange);
        const detail::CivilDay civil =
            detail::civilDay(static_cast<std::int64_t>(mjd_) + detail::kJdnOfMjdZero);
        const std::int64_t secondOfDay = picos_ / kPicosecondsPerSecond;
        const std::int64_t rest = picos_ % kPicosecondsPerSecond;
        return CalendarDate{
            .year = static_cast<std::int32_t>(civil.year),
            .month = static_cast<std::int32_t>(civil.month),
            .day = static_cast<std::int32_t>(civil.day),
            .hour = static_cast<std::int32_t>(secondOfDay / 3'600),
            .minute = static_cast<std::int32_t>((secondOfDay / 60) % 60),
            .second = Seconds{static_cast<f64>(secondOfDay % 60) +
                              (static_cast<f64>(rest) / detail::kPicosecondsPerSecondF)},
        };
    }

    // The Julian date, split at the midnight that begins the day; the fraction
    // is the nearest double to the time of day.
    //
    // A first quotient is within about an ulp of it. One correction, computed
    // from the exact product in scaleDayFraction, makes it the nearest: the
    // residual is picoseconds the first quotient missed, known to 2.4e-4 ps,
    // against an ulp of the fraction of up to 9.6 ps.
    [[nodiscard]] JulianDate julianDate() const noexcept {
        const f64 approximate = static_cast<f64>(picos_) / detail::kPicosecondsPerDayF;
        const detail::Scaled back = detail::scaleDayFraction(approximate);
        const f64 residual = static_cast<f64>(picos_ - back.whole) - back.rest;
        const f64 fraction = approximate + (residual / detail::kPicosecondsPerDayF);
        const f64 day = kMjdZero + mjd_;
        // The nearest double to the last four picoseconds of a day is 1.0. It
        // is carried, so the fraction stays in [0, 1) and the sum is the same.
        if (fraction >= 1.0) return {.day = day + 1.0, .fraction = 0.0};
        return {.day = day, .fraction = fraction};
    }

    // The two stored numbers, for the astronomy that needs the day and the
    // time of day separately (the Earth rotation angle, M1-07) and for tests.
    [[nodiscard]] constexpr f64 modifiedJulianDay() const noexcept { return mjd_; }
    [[nodiscard]] constexpr std::int64_t picosecondOfDay() const noexcept { return picos_; }

    // To the nearest picosecond of the duration's exact value.
    //
    // A finite duration is a precondition rather than a report: every duration
    // added in this project is a validated simulation step or the difference
    // of two instants, and reporting belongs where durations enter from
    // outside (decided 2026-09-10). The check after the assertion keeps a
    // Release build, where the assertion is compiled out, free of undefined
    // behaviour: a NaN comes back as a NaN day, visible to whatever reads it.
    [[nodiscard]] TimePoint operator+(Seconds duration) const noexcept
        requires UniformScale<Scale>
    {
        ORBSIM_EXPECTS(std::isfinite(duration.value));
        if (!std::isfinite(duration.value)) return TimePoint{detail::kNotAnInstant};
        const TimePoint moved{detail::advance({.mjd = mjd_, .picos = picos_}, duration)};
        ORBSIM_ENSURES(moved.isNormalised());
        return moved;
    }

    [[nodiscard]] TimePoint operator-(Seconds duration) const noexcept
        requires UniformScale<Scale>
    {
        return *this + (-duration);
    }

    // Exact in picoseconds while the span is under 100 days, then rounded once
    // to a double; beyond that, the whole days and the picoseconds are added
    // as doubles, and the result is within an ulp of the span. Antisymmetric
    // bit for bit either way: a - b is exactly -(b - a).
    [[nodiscard]] Seconds operator-(const TimePoint& earlier) const noexcept
        requires UniformScale<Scale>
    {
        const f64 days = mjd_ - earlier.mjd_;
        const std::int64_t picos = picos_ - earlier.picos_;
        if (days > -detail::kExactSpanDays && days < detail::kExactSpanDays) {
            const std::int64_t total =
                (static_cast<std::int64_t>(days) * kPicosecondsPerDay) + picos;
            const std::int64_t whole = total / kPicosecondsPerSecond;
            const std::int64_t rest = total % kPicosecondsPerSecond;
            return Seconds{static_cast<f64>(whole) +
                           (static_cast<f64>(rest) / detail::kPicosecondsPerSecondF)};
        }
        return Seconds{(days * detail::kSecondsPerDayF) +
                       (static_cast<f64>(picos) / detail::kPicosecondsPerSecondF)};
    }

    // Normalised instants compare as their day, then their time of day.
    [[nodiscard]] constexpr auto operator<=>(const TimePoint&) const noexcept = default;

private:
    explicit constexpr TimePoint(detail::DayAndPicos parts) noexcept
        : mjd_{parts.mjd}, picos_{parts.picos} {}

    [[nodiscard]] constexpr bool isNormalised() const noexcept {
        return detail::isWhole(mjd_) && picos_ >= 0 && picos_ < kPicosecondsPerDay;
    }

    f64 mjd_;
    std::int64_t picos_;
};

using UtcTime = TimePoint<TimeScale::Utc>;
using TaiTime = TimePoint<TimeScale::Tai>;
using TtTime = TimePoint<TimeScale::Tt>;
using TdbTime = TimePoint<TimeScale::Tdb>;
using Ut1Time = TimePoint<TimeScale::Ut1>;

// --- epochs -----------------------------------------------------------------

// J2000.0, the fundamental epoch: 2000-01-01T12:00:00 TT, JD 2451545.0 TT (US
// Naval Observatory, "Terrestrial Time"; the IERS Conventions 2010 use the
// same). Defined in TT, so a TtTime; JD 2451545.0 in TDB is a different
// instant.
inline constexpr TtTime kJ2000 =
    TtTime::fromCalendar(CalendarDate{.year = 2000, .month = 1, .day = 1, .hour = 12}).value();

// The Unix epoch: 1970-01-01T00:00:00 UTC (IEEE Std 1003.1-2024, Base
// Definitions 3.125, "Epoch"), JD 2440587.5. Representable as a UtcTime,
// although M1-04 will refuse to convert it: UTC before 1972 ran at a different
// rate from TAI rather than in whole leap seconds.
inline constexpr UtcTime kUnixEpoch =
    UtcTime::fromCalendar(CalendarDate{.year = 1970, .month = 1, .day = 1}).value();

// --- compile-time proofs ----------------------------------------------------

static_assert(sizeof(TtTime) == sizeof(f64) + sizeof(std::int64_t),
              "a day and a time within it, and nothing else");
static_assert(std::is_trivially_copyable_v<TtTime>, "a snapshot of the simulation is a copy");
static_assert(!std::is_convertible_v<f64, TtTime> && !std::is_convertible_v<TtTime, f64>,
              "no implicit conversion in either direction");
static_assert(!std::is_convertible_v<TtTime, TdbTime> && !std::is_constructible_v<TdbTime, TtTime>,
              "TT and TDB are different types; converting between them is M1-05's named function");
static_assert(detail::AddsSeconds<TtTime> && !detail::AddsSeconds<UtcTime>,
              "a UTC day may hold 86 401 SI seconds, so UTC + Seconds does not compile");

// The published epochs, at compile time: J2000.0 is noon of MJD 51544 (JD
// 2451545.0 - 2400000.5 = 51544.5), and the Unix epoch is the midnight that
// begins MJD 40587 (2440587.5 - 2400000.5).
static_assert(nearlyEqual(kJ2000.modifiedJulianDay(), 51'544.0, Tolerance{0.0}) &&
              kJ2000.picosecondOfDay() == kPicosecondsPerDay / 2);
static_assert(nearlyEqual(kUnixEpoch.modifiedJulianDay(), 40'587.0, Tolerance{0.0}) &&
              kUnixEpoch.picosecondOfDay() == 0);

} // namespace orb

#endif // ORBSIM_CORE_TIME_HPP
