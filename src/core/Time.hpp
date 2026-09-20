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
#include "core/LeapSeconds.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

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
    InvalidTimeOfDay, // an hour, minute or second outside the day's own length

    // The two the leap-second table reports (M1-04, ADR 0009). Both are
    // refusals to convert, not refusals to *represent*: an instant outside the
    // table is still a perfectly good UtcTime, as kUnixEpoch below is, and it
    // is only the crossing to TAI that has no whole-second answer.
    BeforeLeapSecondEra,    // before 1972-01-01, when UTC ran at its own rate
    LeapSecondTableExpired, // past the last date the committed bulletin covers

    // UT1 (M1-05). A value of UT1 - UTC arrives from outside -- an IERS
    // series, in time -- so a wrong one is reported, not asserted (ADR 0002).
    DeltaUt1OutOfRange,      // |UT1 - UTC| beyond 0.9 s, which UTC is defined never to reach
    InsideRemovedLeapSecond, // UT1 in the second a negative leap second removes from UTC

    // TT - UT1 (M1-86). A DeltaT arrives from outside too -- a scenario, a
    // prediction -- and is reported rather than asserted for the same reason.
    DeltaTOutOfRange, // |TT - UT1| beyond 10^6 s, past what its picoseconds are built to hold
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
        return "the time of day lies outside the day it is in, which for UTC may hold a "
               "leap second";
    case TimeError::BeforeLeapSecondEra:
        return "before 1972-01-01, when UTC ran at its own rate rather than in whole "
               "seconds, so TAI - UTC is not an integer";
    case TimeError::LeapSecondTableExpired:
        return "past the last date the committed IERS bulletin covers, so TAI - UTC is "
               "not yet known";
    case TimeError::DeltaUt1OutOfRange:
        return "UT1 - UTC is beyond 0.9 s, which UTC's definition rules out (ITU-R TF.460-6)";
    case TimeError::InsideRemovedLeapSecond:
        return "that UT1 falls in the second a negative leap second removes from UTC, so no "
               "UTC instant has it";
    case TimeError::DeltaTOutOfRange:
        return "TT - UT1 is beyond 10^6 s, past what its picoseconds are built to hold; no model "
               "of the Earth's rotation reaches it inside years 1 to 9999";
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
    if (!isFinite(value)) return false;
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

// A day's length, its own type rather than a bare count.
//
// It meets an f64 fraction in scaleDayFraction, and clang-tidy's
// bugprone-easily-swappable-parameters counts f64 and std::int64_t as
// convertible -- correctly, since transposing them there compiles and produces
// a wrong instant. This is non-negotiable 1 applied inside the header rather
// than only across its interface.
struct DayLength {
    std::int64_t picoseconds{};
};

// How many picoseconds a day holds on a given scale.
//
// **Only UTC's varies.** TAI, TT and TDB run on SI seconds by definition, so
// their days are 86 400 of them; UT1 follows the Earth's rotation, which this
// type labels rather than measures, and it has no duration arithmetic either.
// A UTC day holds 86 399, 86 400 or 86 401, and the table says which
// (core/LeapSeconds.hpp).
//
// The day is taken as an f64 rather than an integer so the cast happens here,
// once, behind the range check: a uniform scale can hold the NaN day a violated
// arithmetic precondition leaves in a Release build (see operator+), and
// casting that to an integer would be undefined behaviour. inCalendarRange is
// false for a NaN and bounds the cast at the same time.
[[nodiscard]] constexpr DayLength picosecondsInDayOf(TimeScale scale, f64 mjd) noexcept {
    if (scale != TimeScale::Utc) return {.picoseconds = kPicosecondsPerDay};
    ORBSIM_EXPECTS(inCalendarRange(mjd));
    if (!inCalendarRange(mjd)) return {.picoseconds = kPicosecondsPerDay};
    return {.picoseconds = secondsInUtcDay(static_cast<std::int64_t>(mjd)) * kPicosecondsPerSecond};
}

// A validated date's time of day is at most its day's length, and reaches it
// only when the last representable instant rounds up to the next midnight. So
// one step is the whole normalisation: carry()'s general division is for
// durations, and the scale whose days differ in length has none.
[[nodiscard]] constexpr DayAndPicos carryOneDay(DayAndPicos parts, DayLength day) noexcept {
    ORBSIM_EXPECTS(parts.picos >= 0 && parts.picos <= day.picoseconds);
    if (parts.picos >= day.picoseconds) return {.mjd = parts.mjd + 1.0, .picos = 0};
    return parts;
}

// How many seconds the last minute of a day holds: 59, 60 or 61. Its own type
// so it cannot be transposed with the other counts around it, and so that a
// call site says which number it is passing (I.24).
struct FinalMinuteLength {
    std::int32_t seconds{60};
};

// The same question as picosecondsInDayOf, asked of the final minute, and
// answered the same way: only UTC's varies. A function rather than a branch at
// the call site so that fromCalendar's local can be const -- an `if constexpr`
// that assigns is a variable that cannot be, and misc-const-correctness is
// right to say so.
[[nodiscard]] constexpr FinalMinuteLength finalMinuteOf(TimeScale scale,
                                                        std::int64_t mjd) noexcept {
    if (scale != TimeScale::Utc) return {};
    return {.seconds = secondsInFinalMinuteOfUtcDay(mjd)};
}

// NotFinite first (ADR 0002: a NaN says the input is corrupt, not merely
// wrong), then each field in turn, so a date wrong in one way reports that
// way.
//
// **Split from the time of day on 2026-09-18, with M1-04.** How long the last
// minute of a UTC day is depends on which day it is, and that is not known
// until the year, the month and the day have been checked. The order the two
// halves run in is the order the errors were reported in before the split.
[[nodiscard]] constexpr std::expected<void, TimeError>
validateDate(const CalendarDate& date) noexcept {
    if (!isFinite(date.second.value())) return std::unexpected(TimeError::NotFinite);
    if (date.year < kFirstYear || date.year > kLastYear) {
        return std::unexpected(TimeError::YearOutOfRange);
    }
    if (date.month < 1 || date.month > 12) return std::unexpected(TimeError::InvalidMonth);
    const CivilDay civil{.year = date.year, .month = date.month, .day = date.day};
    if (date.day < 1 || date.day > daysInMonth(civil)) {
        return std::unexpected(TimeError::InvalidDay);
    }
    return {};
}

// The time of day, against the length of the minute that ends the day.
//
// **A leap second is only ever inserted at 23:59:60**, so the allowance belongs
// to the final minute and not to the day. 12:30:60 is an invalid time on every
// day there has ever been, and a check written against the day's total length
// alone would wave it through -- 45 060 s is comfortably inside 86 401. A
// negative leap second shortens the same minute to 59 seconds, and the same
// comparison then refuses 23:59:59 on such a day.
[[nodiscard]] constexpr std::expected<void, TimeError>
validateTimeOfDay(const CalendarDate& date, FinalMinuteLength finalMinute) noexcept {
    const bool fieldsInRange =
        date.hour >= 0 && date.hour < 24 && date.minute >= 0 && date.minute < 60;
    const bool endsTheDay = date.hour == 23 && date.minute == 59;
    const f64 secondsAllowed = endsTheDay ? static_cast<f64>(finalMinute.seconds) : 60.0;
    const bool withinTheMinute = date.second.value() >= 0.0 && date.second.value() < secondsAllowed;
    if (!fieldsInRange || !withinTheMinute) return std::unexpected(TimeError::InvalidTimeOfDay);
    return {};
}

// The time of day of a validated date, to the nearest picosecond. The second
// is split before it is scaled: the whole seconds are exact integers, and the
// part below one second, times 10^12, stays under 2^40, where an f64 resolves
// 1.2e-4 ps. The result can be a whole day -- 23:59:59.9999999999996 is the
// next midnight to the nearest picosecond -- and carryOneDay() takes it from
// there. On a UTC day with a leap second the whole second reaches 60 and the
// result reaches 86 401 s, which is that day's own length.
[[nodiscard]] constexpr std::int64_t picosecondsIntoDay(const CalendarDate& date) noexcept {
    const auto wholeSecond = static_cast<std::int64_t>(date.second.value());   // [0, 60]
    const f64 subSecond = date.second.value() - static_cast<f64>(wholeSecond); // exact
    const std::int64_t secondOfDay =
        (((static_cast<std::int64_t>(date.hour) * 60) + date.minute) * 60) + wholeSecond;
    return (secondOfDay * kPicosecondsPerSecond) +
           roundHalfAwayFromZero(subSecond * kPicosecondsPerSecondF);
}

// How many bits of a product's significand a whole number consumes: its value
// with the trailing zeros taken off.
[[nodiscard]] constexpr int significantBits(std::int64_t value) noexcept {
    ORBSIM_EXPECTS(value > 0);
    std::int64_t odd = value;
    while (odd % 2 == 0) {
        odd /= 2;
    }
    int bits = 0;
    while (odd > 0) {
        ++bits;
        odd /= 2;
    }
    return bits;
}

// The power of two a day fraction is split at, so that the high part times the
// day's picoseconds is exact: the two significands together must fit in 53
// bits. An ordinary day is 2^19 x 164 794 921 875 and spends 38 of them,
// leaving 15; a day with a leap second is 2^12 times an odd number of 45 bits
// and leaves 8.
[[nodiscard]] constexpr f64 fractionSplitFor(DayLength day) noexcept {
    constexpr int kDoubleSignificandBits = 53;
    const int highBits = kDoubleSignificandBits - significantBits(day.picoseconds);
    ORBSIM_EXPECTS(highBits > 0 && highBits < 63);
    // Unsigned on both sides: bugprone-signed-bitwise objects to a signed
    // operand of a shift, and is right that a signed shift is a trap worth
    // never getting used to.
    return static_cast<f64>(std::uint64_t{1} << static_cast<unsigned>(highBits));
}

// fraction * picosecondsInDay without losing a picosecond, for a fraction in
// [0, 1]: the whole part exactly, and the rest to a fraction of a picosecond.
//
// The fraction is split at fractionSplitFor(). Its leading bits times the day's
// make 53, so that product is exact, and -- because every day length here is a
// multiple of 2^12 and the split never exceeds 2^15 -- a whole number of
// picoseconds. What is left is below the split, and times the day it is small
// enough to resolve far finer than a picosecond: 2.4e-4 ps for an ordinary day,
// 3.8e-2 ps for one with a leap second, against an ulp of the fraction itself
// of up to 9.6 ps. Both splits are exact: scaling by a power of two, flooring,
// and subtracting the part that was kept.
//
// **The ordinary day is bit-for-bit what M1-03 proved**, because its split is
// still 2^15; only a leap-second day takes the wider one.
struct Scaled {
    std::int64_t whole{};
    f64 rest{};
};

[[nodiscard]] inline Scaled scaleDayFraction(f64 fraction, DayLength day) noexcept {
    ORBSIM_EXPECTS(fraction >= 0.0 && fraction <= 1.0);
    const f64 split = fractionSplitFor(day);
    const f64 dayF = static_cast<f64>(day.picoseconds); // exact: 45 bits at most
    const f64 high = std::floor(fraction * split) / split;
    const f64 low = fraction - high;
    return {
        .whole = static_cast<std::int64_t>(high * dayF),
        .rest = low * dayF,
    };
}

// A ratio of whole numbers, as one parameter: two adjacent std::int64_t
// transpose in silence, and a transposed ratio here is a wrong instant.
struct Ratio {
    std::int64_t numerator{};
    std::int64_t denominator{};
};

// The nearest whole number to a ratio, halves away from zero -- the rule
// roundHalfAwayFromZero uses, in integers, where it is exact rather than nearly
// so.
[[nodiscard]] constexpr std::int64_t roundedQuotient(Ratio ratio) noexcept {
    ORBSIM_EXPECTS(ratio.denominator > 0);
    const std::int64_t half = ratio.denominator / 2;
    if (ratio.numerator >= 0) return (ratio.numerator + half) / ratio.denominator;
    return -((-ratio.numerator + half) / ratio.denominator);
}

// One part of a Julian date as whole days plus picoseconds, exactly. Truncating
// rather than flooring keeps value - days exact for a negative value too.
//
// **Always on the ordinary day**, even for UTC: a Julian date counts days of
// 86 400 seconds, and UTC's quasi-Julian date differs from it only in what the
// *fraction* of the day it lands on means. fromJulianDate rescales that
// fraction afterwards, once it knows which day it is.
struct DaysAndScaled {
    f64 days{};
    Scaled picos;
};

[[nodiscard]] inline DaysAndScaled splitDays(f64 value) noexcept {
    const f64 days = std::trunc(value);
    const f64 fraction = value - days; // exact; (-1, 1), with the sign of value
    const Scaled scaled =
        scaleDayFraction(std::fabs(fraction), DayLength{.picoseconds = kPicosecondsPerDay});
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
    const f64 remainder = std::fmod(duration.value(), kSecondsPerDayF);
    const f64 days = std::round((duration.value() - remainder) / kSecondsPerDayF);
    const f64 wholeSeconds = std::trunc(remainder);
    const f64 subSecond = remainder - wholeSeconds;
    const std::int64_t picos = (static_cast<std::int64_t>(wholeSeconds) * kPicosecondsPerSecond) +
                               roundHalfAwayFromZero(subSecond * kPicosecondsPerSecondF);
    return carry({.mjd = start.mjd + days, .picos = start.picos + picos});
}

// A quasi-Julian date's fraction belongs to the UTC day it lands in, and
// splitDays measured it against an ordinary one; this is the correction, and
// the identity on every other scale.
//
// Whole-number arithmetic: picos * inserted is at most 8.65e16, a hundredth of
// what an int64 holds, and the single rounded division is exact to half a
// picosecond -- against the 9.6 ps an ulp of the fraction is worth at the far
// end of a day.
//
// Only fromJulianDate needs it; julianDate() goes the other way by handing
// scaleDayFraction the day's real length, which is exact rather than rounded.
// The two directions differ because a Julian date arrives split in two parts
// and does not say which day it is in until they are summed.
[[nodiscard]] inline DayAndPicos rescaleIntoDay(TimeScale scale, DayAndPicos parts) noexcept {
    if (scale != TimeScale::Utc) return parts;
    ORBSIM_EXPECTS(inCalendarRange(parts.mjd));
    const std::int64_t inserted = leapSecondsAtEndOfUtcDay(static_cast<std::int64_t>(parts.mjd));
    if (inserted == 0) return parts;
    const DayAndPicos scaled{
        .mjd = parts.mjd,
        .picos = parts.picos + roundedQuotient({
                                   .numerator = parts.picos * inserted,
                                   .denominator = kSecondsPerDay,
                               }),
    };
    return carryOneDay(scaled, picosecondsInDayOf(scale, parts.mjd));
}

// Used to prove, below, that duration arithmetic exists only where it means
// something.
template <typename T>
concept AddsSeconds = requires(const T t, Seconds duration) { t + duration; };

// Declared here and defined below TimePoint, which it needs complete. It is
// the one thing TimePoint befriends; the argument is on the definition.
struct Builder;

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
    //
    // **A UTC second of 60 is accepted on exactly the days the table gives a
    // positive leap second to, and refused on every other day** (M1-04). The
    // date has to be checked before that question can be asked -- which day it
    // is decides how long its last minute is -- so the validation runs in two
    // halves, in the order it reported in before it was split.
    [[nodiscard]] static constexpr std::expected<TimePoint, TimeError>
    fromCalendar(const CalendarDate& date) noexcept {
        if (const auto valid = detail::validateDate(date); !valid) {
            return std::unexpected(valid.error());
        }
        const std::int64_t mjd =
            detail::julianDayNumber({.year = date.year, .month = date.month, .day = date.day}) -
            detail::kJdnOfMjdZero;
        const detail::FinalMinuteLength finalMinute = detail::finalMinuteOf(Scale, mjd);
        if (const auto valid = detail::validateTimeOfDay(date, finalMinute); !valid) {
            return std::unexpected(valid.error());
        }
        const f64 mjdAsDouble = static_cast<f64>(mjd);
        const detail::DayAndPicos parts =
            detail::carryOneDay({.mjd = mjdAsDouble, .picos = detail::picosecondsIntoDay(date)},
                                detail::picosecondsInDayOf(Scale, mjdAsDouble));
        if (!detail::inCalendarRange(parts.mjd)) return std::unexpected(TimeError::YearOutOfRange);
        const TimePoint instant{parts};
        ORBSIM_ENSURES(instant.isNormalised());
        return instant;
    }

    // The nearest picosecond to a Julian date split any way. Reports one that
    // is not finite, or that falls outside the supported years.
    //
    // **For UTC the input is a quasi-Julian date**, ERFA's convention: its
    // fraction is a fraction of that UTC day, "whether the length is 86399,
    // 86400 or 86401 SI seconds" (`dtf2d.c`), which is what keeps the fraction
    // inside [0, 1) across a leap second. See julianDate() for the other
    // direction.
    [[nodiscard]] static std::expected<TimePoint, TimeError>
    fromJulianDate(JulianDate date) noexcept {
        if (!isFinite(date.day) || !isFinite(date.fraction)) {
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
        const TimePoint instant{detail::rescaleIntoDay(Scale, parts)};
        ORBSIM_ENSURES(instant.isNormalised());
        return instant;
    }

    // The calendar date and time of day. Arithmetic may carry an instant past
    // the supported years without error; this is where that is reported.
    [[nodiscard]] constexpr std::expected<CalendarDate, TimeError> toCalendar() const noexcept {
        // Reachable only from a Release build whose arithmetic precondition was
        // violated; without it, the cast below would be undefined behaviour.
        if (!isFinite(mjd_)) return std::unexpected(TimeError::NotFinite);
        if (!detail::inCalendarRange(mjd_)) return std::unexpected(TimeError::YearOutOfRange);
        const detail::CivilDay civil =
            detail::civilDay(static_cast<std::int64_t>(mjd_) + detail::kJdnOfMjdZero);
        const std::int64_t secondOfDay = picos_ / kPicosecondsPerSecond;
        const std::int64_t rest = picos_ % kPicosecondsPerSecond;
        std::int64_t hour = secondOfDay / 3'600;
        std::int64_t minute = (secondOfDay / 60) % 60;
        std::int64_t second = secondOfDay % 60;
        if constexpr (Scale == TimeScale::Utc) {
            // An inserted leap second is 23:59:60, which is 86 400 s into the
            // day: the plain division above would call it hour 24 of a day that
            // has no such hour, and 00:00:00 of a day that has not begun. Every
            // second at or past 86 400 is in the final minute, which starts
            // 86 340 s in.
            constexpr std::int64_t kFinalMinuteBegins = 86'340;
            if (secondOfDay >= kSecondsPerDay) {
                hour = 23;
                minute = 59;
                second = secondOfDay - kFinalMinuteBegins;
            }
        }
        return CalendarDate{
            .year = static_cast<std::int32_t>(civil.year),
            .month = static_cast<std::int32_t>(civil.month),
            .day = static_cast<std::int32_t>(civil.day),
            .hour = static_cast<std::int32_t>(hour),
            .minute = static_cast<std::int32_t>(minute),
            .second = Seconds{static_cast<f64>(second) +
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
    //
    // **For UTC this is a quasi-Julian date**, and the fraction is measured
    // against that day's own length rather than against 86 400 s (ERFA's
    // convention, ADR 0009's update; decided 2026-09-18). Without it the
    // fraction would reach 1.0000116 inside a leap second and break the [0, 1)
    // the type promises -- and the number would not be what anything consuming
    // a UTC Julian date, ERFA included, means by one. On a day with a leap
    // second the residual is known to 3.8e-2 ps instead of 2.4e-4 ps, because
    // 86 401e12 spends more of the significand than 86 400e12 does; both are
    // far inside the ulp above.
    [[nodiscard]] JulianDate julianDate() const noexcept {
        const detail::DayLength dayLength = detail::picosecondsInDayOf(Scale, mjd_);
        const f64 dayPicosF = static_cast<f64>(dayLength.picoseconds);
        const f64 approximate = static_cast<f64>(picos_) / dayPicosF;
        const detail::Scaled back = detail::scaleDayFraction(approximate, dayLength);
        const f64 residual = static_cast<f64>(picos_ - back.whole) - back.rest;
        const f64 fraction = approximate + (residual / dayPicosF);
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
        ORBSIM_EXPECTS(isFinite(duration.value()));
        if (!isFinite(duration.value())) return TimePoint{detail::kNotAnInstant};
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
    //
    // The day is compared with std::strong_order rather than with `==`, which
    // on a double is the comparison CODING_GUIDELINES section 11 forbids (ADR
    // 0017). Over every day an instant can hold that is the numerical order:
    // each is whole and finite, and none is -0.0, because every one is built
    // by carry(), which adds a converted integer to it, and a sum is -0.0 only
    // when both of its terms are. The one day outside that is the NaN a
    // violated precondition leaves in a Release build (operator+), which
    // compares equal to itself and after every instant.
    [[nodiscard]] constexpr std::strong_ordering
    operator<=>(const TimePoint& other) const noexcept {
        if (const std::strong_ordering day = std::strong_order(mjd_, other.mjd_);
            std::is_neq(day)) {
            return day;
        }
        return picos_ <=> other.picos_;
    }
    [[nodiscard]] constexpr bool operator==(const TimePoint& other) const noexcept {
        return std::is_eq(*this <=> other);
    }

private:
    // The one door for a scale conversion, and the reason it is one friend
    // rather than six: see detail::Builder below.
    friend struct detail::Builder;

    explicit constexpr TimePoint(detail::DayAndPicos parts) noexcept
        : mjd_{parts.mjd}, picos_{parts.picos} {}

    // A whole day, and a time of day inside *that day*.
    //
    // The last clause used to read `picos_ < kPicosecondsPerDay`. It became the
    // day's own length with M1-04: a UTC day with a leap second simply runs to
    // 86 401 s, so 23:59:60.5 is 86 400.5 s into it, exactly, and the invariant
    // is "within that day's length" rather than "within 86 400 s". Nothing else
    // changes -- the other four scales still get kPicosecondsPerDay, from the
    // same function.
    [[nodiscard]] constexpr bool isNormalised() const noexcept {
        if (!detail::isWhole(mjd_)) return false;
        if (picos_ < 0) return false;
        return picos_ < detail::picosecondsInDayOf(Scale, mjd_).picoseconds;
    }

    // Initialised although the one constructor always sets both, and although
    // TimePoint has no default constructor at all (CODING_GUIDELINES section 6).
    // Without the initialisers, cppcoreguidelines-pro-type-member-init cannot
    // see that an instant held inside an aggregate is ever set, and reports the
    // aggregate -- found by M1-06's StateAtEpoch, the first struct to hold one,
    // and fixed here, where the cause is, on the owner's ruling of 2026-09-19.
    // Measured before and after: every static_assert below still holds,
    // trivially copyable included.
    f64 mjd_{};
    std::int64_t picos_{};
};

namespace detail {

// **The one way to build a TimePoint from stored parts outside the class.**
//
// An instant is otherwise reachable only through a factory that validates,
// which is what makes its normalisation a postcondition rather than a hope. A
// scale conversion has to build one from another instant's stored parts --
// there is no calendar date in between, and going through one would throw away
// picoseconds -- so it needs a door, and this is it. One friend rather than one
// per conversion, so the surface does not grow as M1-05 adds TDB and UT1.
struct Builder {
    template <TimeScale Scale>
    [[nodiscard]] static constexpr TimePoint<Scale> make(DayAndPicos parts) noexcept {
        const TimePoint<Scale> instant{parts};
        ORBSIM_ENSURES(instant.isNormalised());
        return instant;
    }
};

} // namespace detail

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

// --- the conversions between scales -----------------------------------------
//
// **Exact, not merely accurate.** DeltaAT is a whole number of seconds and
// TT - TAI is a whole number of picoseconds, so every step below is integer
// arithmetic on the stored picosecond count: the error of a UTC -> TAI -> TT
// round trip is zero, against a budget of 1e-9 s (M1-04, ADR 0009).
//
// The day part is carried in *seconds* rather than picoseconds wherever a
// multiplication by the day is involved. mjd * 86 400 is about 5e9 for a date
// the table covers; mjd * 86 400 * 10^12 would be 5e21, and an int64 stops at
// 9.2e18.

// TT - TAI, exactly 32.184 s by definition.
//
// IAU 1991 Resolution A4, Recommendation IV defines TT with its origin chosen
// so that TT - TAI = 32.184 s at 1977 January 1, 0h TAI; both run on the SI
// second, so that offset holds for all time. The number itself is inherited
// from Ephemeris Time -- ET - TAI = 32.184 s, the 1976 IAU value -- so that TT
// continues ET without a step. IERS Conventions (2010), section 10.1.
//
// **In picoseconds, not in Seconds.** 32.184 is not a binary fraction: the
// nearest double is 32.18400000000000034..., so adding it as an f64 would be
// exact only by accident of rounding. As a whole number of picoseconds it is
// exact by construction, which is what makes the chain above exact.
inline constexpr std::int64_t kTtMinusTaiPicoseconds = 32'184'000'000'000;
static_assert(kTtMinusTaiPicoseconds == (32 * kPicosecondsPerSecond) + 184'000'000'000,
              "32.184 s, as 32 s and 184 ms");

// TAI -> TT and back. No table, no era, no expiry: a fixed offset between two
// scales that both run on the SI second, so neither can fail and neither
// returns std::expected (decided 2026-09-18). The type system already says
// which conversions can refuse -- those are the four below.
[[nodiscard]] constexpr TtTime ttFromTai(TaiTime tai) noexcept {
    return detail::Builder::make<TimeScale::Tt>(detail::carry({
        .mjd = tai.modifiedJulianDay(),
        .picos = tai.picosecondOfDay() + kTtMinusTaiPicoseconds,
    }));
}

[[nodiscard]] constexpr TaiTime taiFromTt(TtTime tt) noexcept {
    return detail::Builder::make<TimeScale::Tai>(detail::carry({
        .mjd = tt.modifiedJulianDay(),
        .picos = tt.picosecondOfDay() - kTtMinusTaiPicoseconds,
    }));
}

// UTC -> TAI: add the DeltaAT in force during that UTC day.
//
// The leap second needs no special case in this direction. 2016-12-31T23:59:60
// is stored as 86 400 s into a day whose DeltaAT is 36, and 57 753 * 86 400 +
// 36 + 86 400 is 57 754 * 86 400 + 36 -- 2017-01-01T00:00:36 TAI, one second
// before the 00:00:37 that 2017-01-01T00:00:00 UTC maps to. The arithmetic
// carries it.
[[nodiscard]] constexpr std::expected<TaiTime, TimeError> taiFromUtc(UtcTime utc) noexcept {
    ORBSIM_EXPECTS(detail::inCalendarRange(utc.modifiedJulianDay()));
    const auto utcMjd = static_cast<std::int64_t>(utc.modifiedJulianDay());
    if (utcMjd < kLeapSecondEraFirstMjd) return std::unexpected(TimeError::BeforeLeapSecondEra);
    if (utcMjd >= kLeapSecondTableExpiryMjd) {
        return std::unexpected(TimeError::LeapSecondTableExpired);
    }
    const std::int64_t wholeSeconds = utc.picosecondOfDay() / kPicosecondsPerSecond;
    const std::int64_t rest = utc.picosecondOfDay() % kPicosecondsPerSecond;
    const std::int64_t taiSeconds =
        (utcMjd * kSecondsPerDay) + deltaAtSecondsForUtcDay(utcMjd) + wholeSeconds;
    // Named rather than written inside the braces: the division is meant to be
    // a whole number of days, and bugprone-integer-division is right to ask
    // about an integer quotient converted straight to a double.
    const std::int64_t taiMjd = taiSeconds / kSecondsPerDay;
    const std::int64_t taiSecondOfDay = taiSeconds % kSecondsPerDay;
    return detail::Builder::make<TimeScale::Tai>({
        .mjd = static_cast<f64>(taiMjd),
        .picos = (taiSecondOfDay * kPicosecondsPerSecond) + rest,
    });
}

// TAI -> UTC: subtract the DeltaAT in force at that TAI instant.
//
// This direction *does* need the leap second named, because subtracting 36 from
// 2017-01-01T00:00:36 TAI lands on 57 754 * 86 400 exactly -- the midnight that
// begins 2017-01-01 -- when the answer is the second before it, 23:59:60 of
// 2016-12-31. Inside an inserted second the label is always exactly that: the
// table's step is one second, so the subtraction always lands on the midnight,
// and the instant is the day before it with 86 400 s on the clock.
//
// A *negative* leap second needs nothing here. The second it removes is skipped
// because the next step takes over exactly where that second would have begun,
// and the subtraction never produces it.
// **A TAI instant outside the calendar is reported, not asserted** (found by
// `tests/fuzz_time.cpp` on 2026-09-19, fixed 2026-09-20). It is not a bug for
// one to exist: arithmetic may carry an instant past 9999 and `toCalendar` is
// where that is reported, and `taiFromTt` puts the first 32.184 s of year 1 in
// year 0. Both are conditions a caller can produce, so ADR 0002 reports them --
// and the table has no DeltaAT at either end anyway, so the names already
// existed. This used to assert instead, which aborted a build with assertions
// live on an instant a scenario file can name, while a Release build reported
// correctly.
//
// The two bounds are read off the day as a double, before the cast: a day past
// what an int64 holds -- `tai + Seconds{1e300}` reaches one -- would make that
// cast undefined. They are a day wide on purpose. The era begins at 00:00:10
// TAI on its first day and the expiry falls at 00:00:37 TAI on its last, so a
// day at either edge is passed on to the exact comparisons below, which know
// the second.
[[nodiscard]] inline std::expected<UtcTime, TimeError> utcFromTai(TaiTime tai) noexcept {
    // Only a violated precondition upstream leaves a day that is not finite
    // (see operator+); the report behind the assertion keeps a Release build
    // from casting a NaN, which would be undefined.
    ORBSIM_EXPECTS(isFinite(tai.modifiedJulianDay()));
    if (!isFinite(tai.modifiedJulianDay())) return std::unexpected(TimeError::NotFinite);
    if (tai.modifiedJulianDay() < static_cast<f64>(kLeapSecondEraFirstMjd)) {
        return std::unexpected(TimeError::BeforeLeapSecondEra);
    }
    if (tai.modifiedJulianDay() > static_cast<f64>(kLeapSecondTableExpiryMjd)) {
        return std::unexpected(TimeError::LeapSecondTableExpired);
    }
    const auto taiMjd = static_cast<std::int64_t>(tai.modifiedJulianDay());
    const std::int64_t wholeSeconds = tai.picosecondOfDay() / kPicosecondsPerSecond;
    const std::int64_t rest = tai.picosecondOfDay() % kPicosecondsPerSecond;

    const TaiDayAndSecond instant{.mjd = taiMjd, .secondOfDay = wholeSeconds};
    if (instant < kLeapSecondEraFirstTai) return std::unexpected(TimeError::BeforeLeapSecondEra);
    if (instant >= kLeapSecondTableExpiryTai) {
        return std::unexpected(TimeError::LeapSecondTableExpired);
    }

    const TaiLookup lookup = leapSecondLookupForTai(instant);
    const std::int64_t utcSeconds =
        (taiMjd * kSecondsPerDay) + wholeSeconds - lookup.deltaAtSeconds;
    std::int64_t utcMjd = utcSeconds / kSecondsPerDay;
    std::int64_t secondOfDay = utcSeconds % kSecondsPerDay;
    if (lookup.insideInsertedLeapSecond) {
        ORBSIM_EXPECTS(secondOfDay == 0);
        utcMjd -= 1;
        secondOfDay += kSecondsPerDay;
    }
    return detail::Builder::make<TimeScale::Utc>({
        .mjd = static_cast<f64>(utcMjd),
        .picos = (secondOfDay * kPicosecondsPerSecond) + rest,
    });
}

// The composed pair. Written out rather than through and_then so that the
// failure that can occur -- the table's, on the UTC side -- is visible at the
// one place it happens.
[[nodiscard]] constexpr std::expected<TtTime, TimeError> ttFromUtc(UtcTime utc) noexcept {
    const auto tai = taiFromUtc(utc);
    if (!tai) return std::unexpected(tai.error());
    return ttFromTai(*tai);
}

[[nodiscard]] inline std::expected<UtcTime, TimeError> utcFromTt(TtTime tt) noexcept {
    return utcFromTai(taiFromTt(tt));
}

// --- UT1 ----------------------------------------------------------------------
//
// UT1 is the Earth's rotation read as a clock, and DeltaUT1 = UT1 - UTC is how
// far UTC's atomic seconds have drifted from it since the last leap second.
// **The sign** is ITU-R TF.460-6 (2002), Annex 1 section D: DUT1 is
// approximately UT1 - UTC, "a correction to be added to UTC to obtain a better
// approximation to UT1". So UT1 = UTC + DeltaUT1.
//
// **The convention is ERFA's eraUtcut1** (register decision 59): UT1 is the SI
// seconds elapsed in the UTC day plus DeltaUT1, on days of 86 400 s. ERFA gets
// there through TAI, taking DeltaAT at 0h of the UTC day, and that DeltaAT
// cancels -- so this needs no leap-second table, cannot fail, and is exact
// integer arithmetic on the stored picoseconds. DeltaUT1 on a day that ends in a
// leap second is the IERS value for that day.
//
// **The model error, while no IERS series is supplied** (M1-05, ADR 0009): a
// caller with nothing better passes kDeltaUt1Unmodelled, and UT1 is then UTC.
// That is wrong by DeltaUT1 itself, which UTC is defined never to let past
// 0.9 s: **at most 0.9 s of UT1, 13.5" of Earth rotation, about 420 m at the
// equator.** Recorded, not asserted -- it is a decision with a size, not a
// defect. One consequence is worth knowing before it is met: across a leap
// second the published DeltaUT1 steps by +1 s, and without it UT1 steps *back*
// by a second at the UTC midnight that follows -- each side of the step still
// inside the 0.9 s.
//
// **The way back is not quite a bijection**, and cannot be, under one DeltaUT1
// for the day. Inside a positive leap second the forward map is two-to-one --
// 23:59:60.5 and 00:00:00.5 of the next day have the same UT1 -- and the way
// back returns the later, never writing 23:59:60. Across a *negative* leap
// second, which has never happened, the forward map skips a second of UT1, and
// the way back reports an instant in it as InsideRemovedLeapSecond (decision
// 67). So UT1 -> UTC -> UT1 is exact wherever UTC has the instant, and
// UTC -> UT1 -> UTC exact everywhere but inside a positive leap second.

// The largest DeltaUT1 there can be: ITU-R TF.460-6 (2002), Annex 1 section
// D.1.2, "The departure of UTC from UT1 should not exceed +-0.9 s". The IERS
// schedules leap seconds to keep it so. In picoseconds, as DeltaUt1 holds it.
inline constexpr std::int64_t kDeltaUt1LimitPicoseconds = 900'000'000'000;

// UT1 - UTC, validated, to the nearest picosecond.
//
// A factory rather than a constructor, because the value arrives from outside
// -- an IERS series, when one is read -- and a wrong one is reported, not
// asserted (ADR 0002, register decision 60). Held in integer picoseconds, so
// that the conversions below are integer arithmetic and 0.3 s is exactly
// 300 000 000 000 ps rather than the double nearest 0.3; the bound is checked
// on that held value, which is the nearest picosecond to the one given. No
// default constructor: a DeltaUT1 of zero is a modelling decision, and it is
// spelled kDeltaUt1Unmodelled at the call site that takes it (decision 61).
class DeltaUt1 {
public:
    [[nodiscard]] static constexpr std::expected<DeltaUt1, TimeError>
    fromSeconds(Seconds value) noexcept {
        if (!isFinite(value.value())) return std::unexpected(TimeError::NotFinite);
        // Refused before it is scaled, so that what is scaled is below a
        // second and roundHalfAwayFromZero's range is never approached: 1e12 ps
        // is far inside 2^52.
        if (absOf(value.value()) > 1.0) return std::unexpected(TimeError::DeltaUt1OutOfRange);
        // The nearest picosecond, as picosecondsIntoDay rounds one: the product
        // is below 2^40, where a double resolves 1.2e-4 ps.
        const std::int64_t picoseconds =
            detail::roundHalfAwayFromZero(value.value() * detail::kPicosecondsPerSecondF);
        if (picoseconds > kDeltaUt1LimitPicoseconds || picoseconds < -kDeltaUt1LimitPicoseconds) {
            return std::unexpected(TimeError::DeltaUt1OutOfRange);
        }
        return DeltaUt1{picoseconds};
    }

    [[nodiscard]] constexpr std::int64_t picoseconds() const noexcept { return picoseconds_; }

private:
    explicit constexpr DeltaUt1(std::int64_t picoseconds) noexcept : picoseconds_{picoseconds} {}

    std::int64_t picoseconds_{};
};

// DeltaUT1 = 0: UT1 taken as UTC, with the model error stated above. The name
// is the point -- it says, at every call site, that UT1 is not being modelled.
inline constexpr DeltaUt1 kDeltaUt1Unmodelled = DeltaUt1::fromSeconds(Seconds{0.0}).value();

// UTC -> UT1. The UTC day and the SI picoseconds into it, plus DeltaUT1,
// carried over days of 86 400 s: 86 400.5 s into a day with a leap second is
// 0.5 s into the next UT1 day. No table, no failure.
[[nodiscard]] constexpr Ut1Time ut1FromUtc(UtcTime utc, DeltaUt1 deltaUt1) noexcept {
    return detail::Builder::make<TimeScale::Ut1>(detail::carry({
        .mjd = utc.modifiedJulianDay(),
        .picos = utc.picosecondOfDay() + deltaUt1.picoseconds(),
    }));
}

// UT1 -> UTC, over any leap-second table.
//
// Takes the table as a span, as the queries in core/LeapSeconds.hpp do, so the
// suite can drive the negative-leap-second refusal with a synthetic table: the
// published one has never had a negative step and cannot reach it (register
// decisions 33 and 67). The table decides only which UTC days are short. The
// instant built is inside its day on the published table too, which is what
// UtcTime is normalised against: carry() leaves it below 86 400 s, and every
// day the published table describes holds at least that many --
// everyPublishedStepIsPositive, in core/LeapSeconds.hpp, fails the build the
// day that stops being true, and this is one of the places it points at.
[[nodiscard]] constexpr std::expected<UtcTime, TimeError>
utcFromUt1(std::span<const LeapSecondStep> table, Ut1Time ut1, DeltaUt1 deltaUt1) noexcept {
    // A Ut1Time has no arithmetic, so it comes from a factory or from
    // ut1FromUtc, and its day is finite.
    ORBSIM_EXPECTS(isFinite(ut1.modifiedJulianDay()));
    const detail::DayAndPicos parts = detail::carry({
        .mjd = ut1.modifiedJulianDay(),
        .picos = ut1.picosecondOfDay() - deltaUt1.picoseconds(),
    });
    // Before anything asks how long that day is: the table's answer, and
    // UtcTime's own normalisation, are for days inside the calendar.
    if (!detail::inCalendarRange(parts.mjd)) return std::unexpected(TimeError::YearOutOfRange);
    const auto mjd = static_cast<std::int64_t>(parts.mjd);
    if (parts.picos >= secondsInUtcDay(table, mjd) * kPicosecondsPerSecond) {
        return std::unexpected(TimeError::InsideRemovedLeapSecond);
    }
    return detail::Builder::make<TimeScale::Utc>(parts);
}

// The same, over the published table.
[[nodiscard]] constexpr std::expected<UtcTime, TimeError> utcFromUt1(Ut1Time ut1,
                                                                     DeltaUt1 deltaUt1) noexcept {
    return utcFromUt1(kIersLeapSeconds, ut1, deltaUt1);
}

// --- UT1 from TT (M1-86) --------------------------------------------------------
//
// **The Earth turns on UT1, and the simulation's clock runs on TT** (register
// decision 72). Before M1-86 the only road between the two went through UTC --
// TT to TAI to UTC by the leap-second table, then UT1 = UTC + DeltaUT1 -- so the
// Earth's orientation inherited the table's two edges: refused before
// 1972-01-01 and from 2027-01-01, and, with DeltaUT1 unmodelled, stepping back
// a second at the midnight after every leap second while TT ran on. None of that
// is physics. UT1 is the angle the Earth has turned through, continuous in TT,
// and the table is a labelling convention for UTC. The table refuses because a
// UTC label is an integer a committee has not yet chosen; UT1 is a quantity
// whose prediction is a model with a size, like DeltaUT1 = 0 already is.
//
// So UT1 = TT - DeltaT, where DeltaT = TT - UT1 is a model the caller chooses
// and names, as kDeltaUt1Unmodelled is named: exact integer picoseconds, no
// table, and no failure. Two sources of a DeltaT are provided:
//
//   * deltaTFromLeapSecondTable(tt, deltaUt1): 32.184 s + DeltaAT - DeltaUT1,
//     exact wherever the table holds and refused, by name, where it does not.
//     UT1 from it is the UTC road's UT1 to the picosecond, leap seconds and
//     all -- the suite asserts exactly that;
//   * kDeltaTHeldAtTableExpiry: the same at the table's last step with DeltaUT1
//     unmodelled -- 32.184 + 37 = 69.184 s -- for instants past the expiry.
//
// **The model error is the caller's to take, and to name.** From the table with
// DeltaUT1 unmodelled: at most 0.9 s, as the UTC road always was. Held constant
// from some date: that, plus however far DeltaT drifts after it. The IERS EOP
// 20 C04 series, 1962 to 2026-08-20 (retrieved 2026-09-19), puts the worst
// drift at **1.15 s in one year** (1972) and 10.4 s in ten; since 2000, 0.54 s
// and 3.5 s; and about +0.1 s a year in 2026. A second of DeltaT is 15.04" of
// Earth rotation, about 465 m at the equator. Recorded, not asserted: it is a
// decision with a size, not a defect (ADR 0009).

// The largest DeltaT this type holds: 10^6 s, in picoseconds.
//
// **A limit of the representation, not of the Earth.** DeltaUT1 has a bound by
// definition; DeltaT has none. What bounds it here is that an instant's
// picoseconds less DeltaT's must stay far inside an int64, 9.2e18, and 10^18
// leaves a factor of nine. No model reaches it inside the calendar's years:
// the largest DeltaT one gives there is Morrison and Stephenson's (2004)
// long-term parabola, -20 + 32 u^2 s with u = (year - 1820) / 100 -- as
// Skyfield 1.55 carries it -- at 9999: about 2.1e5 s, two and a half days.
inline constexpr std::int64_t kDeltaTLimitPicoseconds = 1'000'000 * kPicosecondsPerSecond;

// TT - UT1, validated, to the nearest picosecond.
//
// Shaped as DeltaUt1 is, for the same reasons: a factory, because a DeltaT
// arrives from outside -- a scenario, a prediction, one day an IERS series --
// and a wrong one is reported, not asserted (ADR 0002); integer picoseconds, so
// that UT1 from TT is integer arithmetic; and no default constructor, because
// every DeltaT is a modelling decision somebody takes by name.
class DeltaT {
public:
    // The nearest picosecond to a DeltaT given in seconds. Reports one that is
    // not finite, and one past the limit -- the limit itself is inside it.
    //
    // **The whole seconds are split off before anything is scaled**, as
    // picosecondsIntoDay does, so that what is scaled stays below a second and
    // the nearest picosecond comes out right whatever the size. Scaled whole, a
    // DeltaT of days would not: 10^12 times a value near 1.3e5 s lands where a
    // double resolves only 16 ps. The limit is checked first, because it is
    // also what keeps the cast to an integer defined.
    [[nodiscard]] static constexpr std::expected<DeltaT, TimeError>
    fromSeconds(Seconds value) noexcept {
        // Named before it becomes a double: the quotient is meant to be whole,
        // 10^6, and bugprone-integer-division is right to ask about one that
        // goes straight into a floating-point context.
        constexpr std::int64_t kLimitWholeSeconds = kDeltaTLimitPicoseconds / kPicosecondsPerSecond;
        constexpr auto kLimitSeconds = static_cast<f64>(kLimitWholeSeconds); // exact
        if (!isFinite(value.value())) return std::unexpected(TimeError::NotFinite);
        if (absOf(value.value()) > kLimitSeconds) {
            return std::unexpected(TimeError::DeltaTOutOfRange);
        }
        const auto whole = static_cast<std::int64_t>(value.value()); // truncates toward zero
        const f64 part = value.value() - static_cast<f64>(whole);    // exact, in (-1, 1)
        return fromPicoseconds(
            (whole * kPicosecondsPerSecond) +
            detail::roundHalfAwayFromZero(part * detail::kPicosecondsPerSecondF));
    }

    // A DeltaT given exactly, in picoseconds. Reports one past the limit,
    // which is the only thing that can be wrong with an integer here.
    [[nodiscard]] static constexpr std::expected<DeltaT, TimeError>
    fromPicoseconds(std::int64_t picoseconds) noexcept {
        if (picoseconds > kDeltaTLimitPicoseconds || picoseconds < -kDeltaTLimitPicoseconds) {
            return std::unexpected(TimeError::DeltaTOutOfRange);
        }
        return DeltaT{picoseconds};
    }

    [[nodiscard]] constexpr std::int64_t picoseconds() const noexcept { return picoseconds_; }

private:
    explicit constexpr DeltaT(std::int64_t picoseconds) noexcept : picoseconds_{picoseconds} {}

    std::int64_t picoseconds_{};
};

// DeltaT as the committed table leaves it: 32.184 s + the last published
// DeltaAT, with DeltaUT1 unmodelled -- 69.184 s since 2017-01-01. For an instant
// past the table's expiry, where deltaTFromLeapSecondTable refuses, a caller
// that takes this is holding DeltaT at the last value anyone published, and the
// model error stated above -- 0.9 s, plus the drift since -- is the one it
// takes. Derived from the table rather than written out, so that renewing the
// table with a new step moves it too.
inline constexpr DeltaT kDeltaTHeldAtTableExpiry =
    DeltaT::fromPicoseconds(
        kTtMinusTaiPicoseconds +
        (std::int64_t{kIersLeapSeconds.back().deltaAtSeconds} * kPicosecondsPerSecond))
        .value();

// TT -> UT1: UT1 = TT - DeltaT, carried over days of 86 400 s. Integer
// arithmetic on the stored picoseconds, so it cannot fail and is exact; the
// limit keeps the difference far inside an int64, and carry() takes whole days
// out of it, however many a DeltaT of days makes.
[[nodiscard]] constexpr Ut1Time ut1FromTt(TtTime tt, DeltaT deltaT) noexcept {
    return detail::Builder::make<TimeScale::Ut1>(detail::carry({
        .mjd = tt.modifiedJulianDay(),
        .picos = tt.picosecondOfDay() - deltaT.picoseconds(),
    }));
}

// UT1 -> TT, the same arithmetic backwards, and exactly its inverse: both round
// trips are the identity, bit for bit.
[[nodiscard]] constexpr TtTime ttFromUt1(Ut1Time ut1, DeltaT deltaT) noexcept {
    return detail::Builder::make<TimeScale::Tt>(detail::carry({
        .mjd = ut1.modifiedJulianDay(),
        .picos = ut1.picosecondOfDay() + deltaT.picoseconds(),
    }));
}

// DeltaT from the leap-second table: TT - UT1 = (TT - TAI) + (TAI - UTC) -
// (UT1 - UTC) = 32.184 s + DeltaAT - DeltaUT1, exact.
//
// **DeltaAT is the value in force during the UTC day the instant falls in** --
// inside a leap second, still the day's old value. That is what makes UT1 from
// this DeltaT land exactly where the UTC road lands, leap seconds included:
// 23:59:60.5 UTC is 86 400.5 s into its day under the old DeltaAT, and
// ut1FromUtc carries that into the next UT1 day by the same amount this
// subtracts. Refused by name where the table has no DeltaAT to give, before
// 1972-01-01 and from its expiry.
[[nodiscard]] inline std::expected<DeltaT, TimeError>
deltaTFromLeapSecondTable(TtTime tt, DeltaUt1 deltaUt1) noexcept {
    const auto utc = utcFromTt(tt);
    if (!utc) return std::unexpected(utc.error());
    const std::int64_t deltaAt =
        deltaAtSecondsForUtcDay(static_cast<std::int64_t>(utc->modifiedJulianDay()));
    return DeltaT::fromPicoseconds(kTtMinusTaiPicoseconds + (deltaAt * kPicosecondsPerSecond) -
                                   deltaUt1.picoseconds());
}

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
static_assert(std::three_way_comparable<TtTime, std::strong_ordering>,
              "instants are totally ordered, so they sort, and they key a map");

// The published epochs, at compile time: J2000.0 is noon of MJD 51544 (JD
// 2451545.0 - 2400000.5 = 51544.5), and the Unix epoch is the midnight that
// begins MJD 40587 (2440587.5 - 2400000.5).
static_assert(nearlyEqual(kJ2000.modifiedJulianDay(), 51'544.0, Tolerance{0.0}) &&
              kJ2000.picosecondOfDay() == kPicosecondsPerDay / 2);
static_assert(nearlyEqual(kUnixEpoch.modifiedJulianDay(), 40'587.0, Tolerance{0.0}) &&
              kUnixEpoch.picosecondOfDay() == 0);

// --- the scales, at compile time ---------------------------------------------

// The day lengths the fraction split was chosen for. If a fourth ever appears,
// fractionSplitFor is what has to be checked, and this is where it says so.
static_assert(detail::significantBits(kPicosecondsPerDay) == 38,
              "an ordinary day is 2^19 x 164 794 921 875");
static_assert(detail::significantBits(86'401'000'000'000'000) == 45,
              "a day with a leap second is 2^12 times an odd number");
static_assert(detail::significantBits(86'399'000'000'000'000) == 45, "and so is a shortened one");
static_assert(nearlyEqual(detail::fractionSplitFor({.picoseconds = kPicosecondsPerDay}),
                          32'768.0,
                          Tolerance{0.0}),
              "2^15 for an ordinary day: exactly the split M1-03 measured, so its arithmetic "
              "is unchanged");
static_assert(nearlyEqual(detail::fractionSplitFor({.picoseconds = 86'401'000'000'000'000}),
                          256.0,
                          Tolerance{0.0}),
              "2^8 where the day spends seven more bits of the significand");

static_assert(detail::roundedQuotient({.numerator = 7, .denominator = 2}) == 4 &&
                  detail::roundedQuotient({.numerator = -7, .denominator = 2}) == -4 &&
                  detail::roundedQuotient({.numerator = 5, .denominator = 2}) == 3 &&
                  detail::roundedQuotient({.numerator = 0, .denominator = 2}) == 0,
              "halves away from zero, symmetrically");

// **The worked example M1-04 states**, at compile time rather than only in the
// suite: 2017-01-01T00:00:00 UTC is 2017-01-01T00:00:37 TAI, and
// 2017-01-01T00:01:09.184 TT. MJD 57 754 is 2017-01-01.
consteval bool theWorkedExampleHolds() {
    const UtcTime utc = UtcTime::fromCalendar({.year = 2017, .month = 1, .day = 1}).value();
    const TaiTime tai = taiFromUtc(utc).value();
    const TtTime tt = ttFromTai(tai);
    return nearlyEqual(tai.modifiedJulianDay(), 57'754.0, Tolerance{0.0}) &&
           tai.picosecondOfDay() == 37 * kPicosecondsPerSecond &&
           nearlyEqual(tt.modifiedJulianDay(), 57'754.0, Tolerance{0.0}) &&
           tt.picosecondOfDay() == (69 * kPicosecondsPerSecond) + 184'000'000'000;
}
static_assert(theWorkedExampleHolds(), "UTC + 37 s is TAI, and TAI + 32.184 s is TT");

// **The leap second is representable, and it is one second before that.**
consteval bool theLeapSecondHolds() {
    const CalendarDate date{
        .year = 2016,
        .month = 12,
        .day = 31,
        .hour = 23,
        .minute = 59,
        .second = Seconds{60.0},
    };
    const UtcTime utc = UtcTime::fromCalendar(date).value();
    const TaiTime tai = taiFromUtc(utc).value();
    return nearlyEqual(utc.modifiedJulianDay(), 57'753.0, Tolerance{0.0}) &&
           utc.picosecondOfDay() == 86'400 * kPicosecondsPerSecond &&
           nearlyEqual(tai.modifiedJulianDay(), 57'754.0, Tolerance{0.0}) &&
           tai.picosecondOfDay() == 36 * kPicosecondsPerSecond &&
           // TT has no leap seconds, whatever the date: only UTC does.
           !TtTime::fromCalendar(date).has_value();
}
static_assert(theLeapSecondHolds(),
              "23:59:60 UTC is 86 400 s into 2016-12-31 and 00:00:36 TAI the next day");

// A second of 60 on a day the table gives no leap second to is an invalid time
// of day, on UTC as on every other scale.
static_assert(
    !UtcTime::fromCalendar(
         {.year = 2017, .month = 1, .day = 1, .hour = 23, .minute = 59, .second = Seconds{60.0}})
         .has_value(),
    "2017-01-01 has no leap second at its end");

// The two refusals, by name. kUnixEpoch is a perfectly good UtcTime and only
// its conversion has no answer, which is the distinction the two errors draw.
static_assert(!taiFromUtc(kUnixEpoch).has_value() &&
                  taiFromUtc(kUnixEpoch).error() == TimeError::BeforeLeapSecondEra,
              "1970 is before UTC ran on whole seconds");
static_assert(
    !taiFromUtc(UtcTime::fromCalendar({.year = 2027, .month = 1, .day = 1}).value()).has_value() &&
        taiFromUtc(UtcTime::fromCalendar({.year = 2027, .month = 1, .day = 1}).value()).error() ==
            TimeError::LeapSecondTableExpired,
    "2027-01-01T00:00:00 is the first instant IERS Bulletin C 72 does not cover");
static_assert(
    taiFromUtc(UtcTime::fromCalendar({.year = 2026, .month = 12, .day = 31}).value()).has_value(),
    "and the day before it is covered");

// The conversions that cannot fail do not pretend they can, and the ones that
// can say so in their type.
static_assert(std::is_same_v<decltype(ttFromTai(std::declval<TaiTime>())), TtTime>,
              "TAI to TT is a fixed offset and has no failure to report");
static_assert(std::is_same_v<decltype(taiFromTt(std::declval<TtTime>())), TaiTime>);
static_assert(std::is_same_v<decltype(taiFromUtc(std::declval<UtcTime>())),
                             std::expected<TaiTime, TimeError>>,
              "UTC to TAI needs the table, which has two edges, so it reports");
static_assert(std::is_same_v<decltype(utcFromTai(std::declval<TaiTime>())),
                             std::expected<UtcTime, TimeError>>);

// --- UT1, at compile time ------------------------------------------------------

static_assert(
    std::is_same_v<decltype(ut1FromUtc(std::declval<UtcTime>(), kDeltaUt1Unmodelled)), Ut1Time>,
    "UTC to UT1 needs no table and cannot fail");
static_assert(std::is_same_v<decltype(utcFromUt1(std::declval<Ut1Time>(), kDeltaUt1Unmodelled)),
                             std::expected<UtcTime, TimeError>>,
              "UT1 to UTC reports the calendar's ends and a removed leap second");
static_assert(!std::is_default_constructible_v<DeltaUt1> &&
                  !std::is_constructible_v<DeltaUt1, Seconds> &&
                  !std::is_constructible_v<DeltaUt1, std::int64_t>,
              "a DeltaUT1 comes from the factory, which validates, or is kDeltaUt1Unmodelled "
              "by name");
static_assert(std::is_trivially_copyable_v<DeltaUt1>);
static_assert(DeltaUt1::fromSeconds(Seconds{0.9}).has_value() &&
                  !DeltaUt1::fromSeconds(Seconds{0.900000000001}).has_value(),
              "0.9 s is inside the limit, and a picosecond past it is not");

// The sign, as ITU-R TF.460-6 states it: UT1 = UTC + DeltaUT1. Noon of
// 2017-01-01 (MJD 57 754) with DeltaUT1 = +0.3 s is 0.3 s past noon in UT1.
consteval bool theUt1SignHolds() {
    const UtcTime noon =
        UtcTime::fromCalendar({.year = 2017, .month = 1, .day = 1, .hour = 12}).value();
    const Ut1Time ut1 = ut1FromUtc(noon, DeltaUt1::fromSeconds(Seconds{0.3}).value());
    return nearlyEqual(ut1.modifiedJulianDay(), 57'754.0, Tolerance{0.0}) &&
           ut1.picosecondOfDay() == (43'200 * kPicosecondsPerSecond) + 300'000'000'000;
}
static_assert(theUt1SignHolds(), "UT1 = UTC + DeltaUT1");

// 23:59:60.5 UTC on 2016-12-31 is 0.5 s into 2017-01-01 UT1, with DeltaUT1
// unmodelled: UT1 has no leap seconds, only UTC does.
consteval bool theLeapSecondRunsIntoTheNextUt1Day() {
    const UtcTime inside = UtcTime::fromCalendar({
                                                     .year = 2016,
                                                     .month = 12,
                                                     .day = 31,
                                                     .hour = 23,
                                                     .minute = 59,
                                                     .second = Seconds{60.5},
                                                 })
                               .value();
    const Ut1Time ut1 = ut1FromUtc(inside, kDeltaUt1Unmodelled);
    return nearlyEqual(ut1.modifiedJulianDay(), 57'754.0, Tolerance{0.0}) &&
           ut1.picosecondOfDay() == kPicosecondsPerSecond / 2;
}
static_assert(theLeapSecondRunsIntoTheNextUt1Day(),
              "86 400.5 s into a day with a leap second is 0.5 s into the next UT1 day");

// --- UT1 from TT, at compile time (M1-86) ---------------------------------------

static_assert(
    std::is_same_v<decltype(ut1FromTt(std::declval<TtTime>(), kDeltaTHeldAtTableExpiry)), Ut1Time>,
    "TT to UT1 under a given DeltaT needs no table and cannot fail");
static_assert(
    std::is_same_v<decltype(ttFromUt1(std::declval<Ut1Time>(), kDeltaTHeldAtTableExpiry)), TtTime>);
static_assert(
    std::is_same_v<decltype(deltaTFromLeapSecondTable(std::declval<TtTime>(), kDeltaUt1Unmodelled)),
                   std::expected<DeltaT, TimeError>>,
    "the table's DeltaT has the table's two edges, so it reports");
static_assert(!std::is_default_constructible_v<DeltaT> &&
                  !std::is_constructible_v<DeltaT, Seconds> &&
                  !std::is_constructible_v<DeltaT, std::int64_t>,
              "a DeltaT comes from a factory, which validates, or is a constant with a name");
static_assert(std::is_trivially_copyable_v<DeltaT>);
static_assert(kDeltaTHeldAtTableExpiry.picoseconds() == 69'184'000'000'000,
              "32.184 s + 37 s, the last published DeltaAT");
static_assert(DeltaT::fromPicoseconds(kDeltaTLimitPicoseconds).has_value() &&
                  !DeltaT::fromPicoseconds(kDeltaTLimitPicoseconds + 1).has_value() &&
                  DeltaT::fromSeconds(Seconds{-1e6}).has_value(),
              "10^6 s is inside the limit, and a picosecond past it is not");

// The sign: UT1 = TT - DeltaT. 2017-01-01T12:00:00 TT less 69.184 s is
// 11:58:50.816 UT1, and the way back is exact.
consteval bool theDeltaTSignHolds() {
    const TtTime noon =
        TtTime::fromCalendar({.year = 2017, .month = 1, .day = 1, .hour = 12}).value();
    const Ut1Time ut1 = ut1FromTt(noon, kDeltaTHeldAtTableExpiry);
    return nearlyEqual(ut1.modifiedJulianDay(), 57'754.0, Tolerance{0.0}) &&
           ut1.picosecondOfDay() == (43'200 * kPicosecondsPerSecond) - 69'184'000'000'000 &&
           ttFromUt1(ut1, kDeltaTHeldAtTableExpiry) == noon;
}
static_assert(theDeltaTSignHolds(), "UT1 = TT - DeltaT");

} // namespace orb

#endif // ORBSIM_CORE_TIME_HPP
