#ifndef ORBSIM_CORE_LEAPSECONDS_HPP
#define ORBSIM_CORE_LEAPSECONDS_HPP
//
// DeltaAT = TAI - UTC: the IERS leap-second table, and the queries over it.
//
// ADR 0009: the table reports rather than extrapolates. Leap seconds are
// announced about six months ahead, so a table is always finite, and quietly
// carrying the last value forward is how a simulator becomes silently wrong a
// year after release. Two boundaries are therefore named rather than assumed --
// kLeapSecondEraFirstMjd below it, kLeapSecondTableExpiryMjd above it -- and
// core/Time.hpp turns each into a TimeError.
//
// **Provenance.** Transcribed 2026-09-18 from two sources that agree:
//
//   * IERS **Bulletin C 72**, Paris, 2026-07-06: "NO leap second will be
//     introduced at the end of December 2026", and "from 2017 January 1, 0h
//     UTC, until further notice : UTC-TAI = -37 s".
//     https://datacenter.iers.org/data/latestVersion/bulletinC.txt
//   * The IERS/IANA `leap-seconds.list`, last updated 2026-07-06T07:44:57 UTC
//     -- the same day Bulletin C 72 was issued -- whose own content hash is
//     `a9bad145 84c31c70 758402aa b37bfd54 5923836a`.
//     https://hpiers.obspm.fr/iers/bul/bulc/ntp/leap-seconds.list
//
// That file is keyed by NTP seconds; the MJDs below were converted from it
// independently rather than copied, and tests/test_time.cpp checks every one of
// them again against std::chrono, which shares no line of code with this file.
//
// **Why the table ends where it does.** Bulletin C 72 rules out a leap second
// at the end of December 2026 and says nothing about any later date. The
// `leap-seconds.list` file carries a later expiry of its own, 2027-06-28, which
// additionally assumes the 2027 March opportunity goes unused -- true of every
// year so far, but ITU-R TF.460 lists 31 March as a second-preference date and
// no bulletin has ruled it out. So this table expires at **2027-01-01T00:00:00
// UTC**, which is exactly what Bulletin C 72 guarantees and nothing more
// (decided 2026-09-18). Being too conservative costs a named, loud refusal;
// being too generous costs a wrong answer that looks right, which is the
// failure this whole file exists to prevent.
//
// **Renewing it** is: fetch the current Bulletin C, add any new row, move
// kLeapSecondTableExpiryMjd to the first instant the new bulletin no longer
// covers, and update the provenance block above and the row in docs/STATUS.md.
// The static_asserts below check the shape of the result, not its truth.
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace orb {

// One step of DeltaAT, as the IERS publishes it: the UTC day on whose 00:00:00
// the new value takes effect, and the value, in whole SI seconds.
//
// DeltaAT is an integer by construction -- that is what "leap *second*" means --
// so it is held as one. Every conversion built on this file is therefore exact
// integer arithmetic, and the 1e-9 s round-trip budget is met with an error of
// zero rather than a small one.
struct LeapSecondStep {
    std::int64_t utcMjd{};
    std::int32_t deltaAtSeconds{};
};

// The 28 published steps, 1972-01-01 to 2017-01-01. The MJD is the key; the
// date beside it is what the MJD means, and the suite checks the two against
// each other through a calendar this file does not contain.
inline constexpr auto kIersLeapSeconds = std::to_array<LeapSecondStep>({
    {.utcMjd = 41'317, .deltaAtSeconds = 10}, // 1972-01-01
    {.utcMjd = 41'499, .deltaAtSeconds = 11}, // 1972-07-01
    {.utcMjd = 41'683, .deltaAtSeconds = 12}, // 1973-01-01
    {.utcMjd = 42'048, .deltaAtSeconds = 13}, // 1974-01-01
    {.utcMjd = 42'413, .deltaAtSeconds = 14}, // 1975-01-01
    {.utcMjd = 42'778, .deltaAtSeconds = 15}, // 1976-01-01
    {.utcMjd = 43'144, .deltaAtSeconds = 16}, // 1977-01-01
    {.utcMjd = 43'509, .deltaAtSeconds = 17}, // 1978-01-01
    {.utcMjd = 43'874, .deltaAtSeconds = 18}, // 1979-01-01
    {.utcMjd = 44'239, .deltaAtSeconds = 19}, // 1980-01-01
    {.utcMjd = 44'786, .deltaAtSeconds = 20}, // 1981-07-01
    {.utcMjd = 45'151, .deltaAtSeconds = 21}, // 1982-07-01
    {.utcMjd = 45'516, .deltaAtSeconds = 22}, // 1983-07-01
    {.utcMjd = 46'247, .deltaAtSeconds = 23}, // 1985-07-01
    {.utcMjd = 47'161, .deltaAtSeconds = 24}, // 1988-01-01
    {.utcMjd = 47'892, .deltaAtSeconds = 25}, // 1990-01-01
    {.utcMjd = 48'257, .deltaAtSeconds = 26}, // 1991-01-01
    {.utcMjd = 48'804, .deltaAtSeconds = 27}, // 1992-07-01
    {.utcMjd = 49'169, .deltaAtSeconds = 28}, // 1993-07-01
    {.utcMjd = 49'534, .deltaAtSeconds = 29}, // 1994-07-01
    {.utcMjd = 50'083, .deltaAtSeconds = 30}, // 1996-01-01
    {.utcMjd = 50'630, .deltaAtSeconds = 31}, // 1997-07-01
    {.utcMjd = 51'179, .deltaAtSeconds = 32}, // 1999-01-01
    {.utcMjd = 53'736, .deltaAtSeconds = 33}, // 2006-01-01
    {.utcMjd = 54'832, .deltaAtSeconds = 34}, // 2009-01-01
    {.utcMjd = 56'109, .deltaAtSeconds = 35}, // 2012-07-01
    {.utcMjd = 57'204, .deltaAtSeconds = 36}, // 2015-07-01
    {.utcMjd = 57'754, .deltaAtSeconds = 37}, // 2017-01-01
});

// 1972-01-01. Before this, UTC ran at a different *rate* from TAI rather than
// in whole seconds -- the 1961-1971 rate-offset era -- and is refused rather
// than approximated (ADR 0009).
inline constexpr std::int64_t kLeapSecondEraFirstMjd = 41'317;

// 2027-01-01. The first UTC day this table does not describe; see the header
// note. An instant on or after it is LeapSecondTableExpired.
inline constexpr std::int64_t kLeapSecondTableExpiryMjd = 61'406;

// A TAI instant, as the day and the whole second within it -- which is how
// TimePoint stores one, and all the resolution a table of whole seconds can
// use. A struct rather than two std::int64_t parameters, which would transpose
// in silence (I.24, and bugprone-easily-swappable-parameters).
//
// Ordered, and with `==`, because these are integers: the comparison
// CODING_GUIDELINES section 11 forbids is the one on a double.
// The comparisons are hidden friends rather than members, so this stays a plain
// aggregate: misc-non-private-member-variables-in-classes reports public data
// on anything with member functions, and it is right to -- public data plus
// behaviour is a class pretending to be a struct. Defaulted friends compare
// member by member, which for a day and a second within it is exactly
// chronological order.
struct TaiDayAndSecond {
    std::int64_t mjd{};
    std::int64_t secondOfDay{};

    [[nodiscard]] friend constexpr auto operator<=>(const TaiDayAndSecond&,
                                                    const TaiDayAndSecond&) noexcept = default;
    [[nodiscard]] friend constexpr bool operator==(const TaiDayAndSecond&,
                                                   const TaiDayAndSecond&) noexcept = default;
};

// The TAI instant at which a step takes effect. UTC day `utcMjd` begins at
// 00:00:00 UTC, which is `deltaAtSeconds` into the same TAI day -- DeltaAT has
// never exceeded 37 s, far short of a day, and the static_assert below keeps it
// that way -- so the key is the row itself, read differently. There is one
// table, not two.
[[nodiscard]] constexpr TaiDayAndSecond taiInstantOf(const LeapSecondStep& step) noexcept {
    return {.mjd = step.utcMjd, .secondOfDay = step.deltaAtSeconds};
}

// The two boundaries again, on the TAI scale, for the reverse conversion.
inline constexpr TaiDayAndSecond kLeapSecondEraFirstTai = taiInstantOf(kIersLeapSeconds.front());
inline constexpr TaiDayAndSecond kLeapSecondTableExpiryTai{
    .mjd = kLeapSecondTableExpiryMjd,
    .secondOfDay = kIersLeapSeconds.back().deltaAtSeconds,
};

// --- the queries, over any table ---------------------------------------------
//
// Each takes a span so that a test can drive it with a table this project did
// not publish: a negative leap second has never occurred, so the only way the
// arithmetic for one can be executed at all is a synthetic table (decided
// 2026-09-18). The committed-table overloads below forward to these.

// The last step in force at or before `utcMjd`, or nothing if `utcMjd` precedes
// the first step.
//
// A linear scan over 28 rows, called once per conversion: a binary search would
// be harder to read and impossible to measure a difference in. Written as a
// range-for over the rows rather than over indices -- a subscript is what
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access objects to, and
// an iteration that cannot go out of range is a better answer than a checked
// subscript. The step is returned by value: it is sixteen bytes, and a
// reference into a span would raise a lifetime question worth nothing here.
[[nodiscard]] constexpr std::optional<LeapSecondStep>
stepForUtcDay(std::span<const LeapSecondStep> table, std::int64_t utcMjd) noexcept {
    std::optional<LeapSecondStep> inForce;
    for (const LeapSecondStep& step : table) {
        if (step.utcMjd > utcMjd) break;
        inForce = step;
    }
    return inForce;
}

// TAI - UTC in force during UTC day `utcMjd`, in whole seconds.
//
// A precondition, not a report: the caller establishes that the day is inside
// the era first, because "which error" is core/Time.hpp's to decide and this
// file has no TimeError to return (ADR 0002 -- one error strategy per layer).
[[nodiscard]] constexpr std::int32_t deltaAtSecondsForUtcDay(std::span<const LeapSecondStep> table,
                                                             std::int64_t utcMjd) noexcept {
    const std::optional<LeapSecondStep> inForce = stepForUtcDay(table, utcMjd);
    ORBSIM_EXPECTS(inForce.has_value());
    // Reachable only from a Release build whose caller skipped the era check.
    // The value is not a guess at DeltaAT before 1972 -- there is no such whole
    // number, which is why core/Time.hpp reports BeforeLeapSecondEra rather
    // than asking this function.
    if (!inForce) return table.empty() ? 0 : table.front().deltaAtSeconds;
    return inForce->deltaAtSeconds;
}

// The seconds inserted at the end of UTC day `utcMjd`: +1 for a positive leap
// second, -1 for a negative one, 0 for an ordinary day.
//
// **Zero outside the table, deliberately.** A day before the era or at or past
// the expiry gets an ordinary 86 400 s, because a leap second we do not know
// about is one we cannot represent; 23:59:60 on such a day is then refused as
// an invalid time of day, which is the truthful answer. The conversions refuse
// those days outright, so this only governs whether an *instant* can be built.
//
// The end of day `utcMjd` is the start of day `utcMjd + 1`, so this is simply
// the difference of two lookups.
[[nodiscard]] constexpr std::int32_t leapSecondsAtEndOfUtcDay(std::span<const LeapSecondStep> table,
                                                              std::int64_t utcMjd) noexcept {
    if (table.empty()) return 0;
    if (utcMjd < table.front().utcMjd) return 0;
    // Past the last row the two lookups return the same value and the
    // difference is zero on its own, so the expiry needs no guard here: a day
    // the table does not describe is an ordinary day for the purpose of
    // *building* an instant, and core/Time.hpp refuses to *convert* one.
    const std::int32_t inserted =
        deltaAtSecondsForUtcDay(table, utcMjd + 1) - deltaAtSecondsForUtcDay(table, utcMjd);
    // Every published step is +1 and the mechanism defines +-1; a table with a
    // larger step would silently produce a day of the wrong length.
    ORBSIM_ENSURES(inserted >= -1 && inserted <= 1);
    return inserted;
}

// How many SI seconds UTC day `utcMjd` holds: 86 399, 86 400 or 86 401.
[[nodiscard]] constexpr std::int64_t secondsInUtcDay(std::span<const LeapSecondStep> table,
                                                     std::int64_t utcMjd) noexcept {
    constexpr std::int64_t kOrdinaryDay = 86'400;
    return kOrdinaryDay + leapSecondsAtEndOfUtcDay(table, utcMjd);
}

// How many seconds the *last minute* of UTC day `utcMjd` holds: 59, 60 or 61.
//
// This is the one the calendar validator needs, and it is not the same question
// as the day's length. A leap second is only ever inserted at 23:59:60; a date
// of 12:30:60 is invalid on every day there has ever been, and a check written
// against the day's length alone would wave it through.
[[nodiscard]] constexpr std::int32_t
secondsInFinalMinuteOfUtcDay(std::span<const LeapSecondStep> table, std::int64_t utcMjd) noexcept {
    constexpr std::int32_t kOrdinaryMinute = 60;
    return kOrdinaryMinute + leapSecondsAtEndOfUtcDay(table, utcMjd);
}

// --- the same, over the committed table ---------------------------------------

[[nodiscard]] constexpr std::int32_t deltaAtSecondsForUtcDay(std::int64_t utcMjd) noexcept {
    return deltaAtSecondsForUtcDay(kIersLeapSeconds, utcMjd);
}
[[nodiscard]] constexpr std::int32_t leapSecondsAtEndOfUtcDay(std::int64_t utcMjd) noexcept {
    return leapSecondsAtEndOfUtcDay(kIersLeapSeconds, utcMjd);
}
[[nodiscard]] constexpr std::int64_t secondsInUtcDay(std::int64_t utcMjd) noexcept {
    return secondsInUtcDay(kIersLeapSeconds, utcMjd);
}
[[nodiscard]] constexpr std::int32_t secondsInFinalMinuteOfUtcDay(std::int64_t utcMjd) noexcept {
    return secondsInFinalMinuteOfUtcDay(kIersLeapSeconds, utcMjd);
}

// --- the reverse direction, in LeapSeconds.cpp --------------------------------

// What a TAI instant needs to know about the table: the offset in force, and
// whether the instant lies inside an inserted leap second -- in which case its
// UTC label is 23:59:60 of the *previous* day rather than 00:00:00 of the next.
//
// A negative leap second needs no flag: the second it removes simply never
// occurs, because the next step takes over exactly where that second would
// have begun.
struct TaiLookup {
    std::int32_t deltaAtSeconds{};
    bool insideInsertedLeapSecond{};
};

// Declared here and defined in LeapSeconds.cpp, on purpose. The UTC-day queries
// above are constexpr because core/Time.hpp evaluates them while building
// kUnixEpoch, which is a UtcTime and must stay a constant expression -- the
// trap std::isfinite sprang on MSVC (core/Scalar.hpp). Nothing needs *these* at
// compile time, and out of line keeps a reverse scan of the table out of every
// translation unit that includes core/Time.hpp.
//
// Precondition: the instant lies at or after kLeapSecondEraFirstTai. The caller
// checks, for the reason given on deltaAtSecondsForUtcDay.
[[nodiscard]] TaiLookup leapSecondLookupForTai(std::span<const LeapSecondStep> table,
                                               TaiDayAndSecond instant) noexcept;
[[nodiscard]] TaiLookup leapSecondLookupForTai(TaiDayAndSecond instant) noexcept;

// --- compile-time proofs ------------------------------------------------------
//
// The shape of the table, not its truth: that it is sorted, that it steps by
// whole seconds the mechanism allows, and that its ends are the published ones.
// Whether row 14 really is 1985-07-01 is a question for a calendar this file
// does not contain, and tests/test_time.cpp asks std::chrono.

static_assert(!kIersLeapSeconds.empty(), "a table with no rows has no era to begin");
static_assert(kIersLeapSeconds.front().utcMjd == kLeapSecondEraFirstMjd,
              "the era begins at the first step, 1972-01-01");
static_assert(kIersLeapSeconds.front().deltaAtSeconds == 10,
              "DeltaAT was 10 s when UTC began running on integer seconds");
static_assert(kIersLeapSeconds.back().deltaAtSeconds == 37,
              "IERS Bulletin C 72, 2026-07-06: UTC-TAI = -37 s until further notice");
static_assert(kIersLeapSeconds.back().utcMjd < kLeapSecondTableExpiryMjd,
              "the table must expire after the last step it carries");

// Strictly increasing in the day, and stepping by exactly one second -- which
// is what makes the row its own TAI key, and what the day-length arithmetic
// assumes. Written as a loop in a constant expression rather than 27 asserts.
consteval bool leapSecondTableIsWellFormed() {
    for (std::size_t i = 1; i < kIersLeapSeconds.size(); ++i) {
        const LeapSecondStep& previous = kIersLeapSeconds.at(i - 1);
        const LeapSecondStep& current = kIersLeapSeconds.at(i);
        if (current.utcMjd <= previous.utcMjd) return false;
        const std::int32_t step = current.deltaAtSeconds - previous.deltaAtSeconds;
        if (step != -1 && step != 1) return false;
        // The row doubles as its TAI key only while DeltaAT is shorter than a
        // day, so that the key never spills into the next one.
        if (current.deltaAtSeconds < 0 || current.deltaAtSeconds >= 86'400) return false;
        if (taiInstantOf(current) <= taiInstantOf(previous)) return false;
    }
    return true;
}
static_assert(leapSecondTableIsWellFormed(),
              "the table is sorted, steps by one second, and is its own TAI key in order");

// Every published step is a *positive* leap second. The arithmetic handles a
// negative one -- the mechanism allows it and the Earth's rotation has been
// making one likelier -- but nothing in the committed table exercises that
// path, which is why tests/test_time.cpp drives the span overloads above with a
// synthetic table instead of pretending the case is covered.
consteval bool everyPublishedStepIsPositive() {
    for (std::size_t i = 1; i < kIersLeapSeconds.size(); ++i) {
        if (kIersLeapSeconds.at(i).deltaAtSeconds - kIersLeapSeconds.at(i - 1).deltaAtSeconds !=
            1) {
            return false;
        }
    }
    return true;
}
static_assert(everyPublishedStepIsPositive(),
              "no negative leap second has ever been inserted; if one is, this assert is the "
              "reminder to check the tests that assume it");

// The queries, at compile time, on the days that matter. 57 753 is 2016-12-31,
// the last day with a leap second; 57 754 is 2017-01-01.
static_assert(deltaAtSecondsForUtcDay(41'317) == 10, "the first day of the era");
static_assert(deltaAtSecondsForUtcDay(57'753) == 36, "2016-12-31, before the last leap second");
static_assert(deltaAtSecondsForUtcDay(57'754) == 37, "2017-01-01, after it");
static_assert(deltaAtSecondsForUtcDay(61'405) == 37, "2026-12-31, the last day the table covers");
static_assert(leapSecondsAtEndOfUtcDay(57'753) == 1, "2016-12-31 ends in 23:59:60");
static_assert(leapSecondsAtEndOfUtcDay(57'754) == 0, "2017-01-01 does not");
static_assert(leapSecondsAtEndOfUtcDay(41'316) == 0, "nor does a day before the era");
static_assert(leapSecondsAtEndOfUtcDay(kLeapSecondTableExpiryMjd) == 0, "nor one past the expiry");
static_assert(leapSecondsAtEndOfUtcDay(kLeapSecondTableExpiryMjd - 1) == 0,
              "2026-12-31 is covered, and Bulletin C 72 says it has no leap second");
static_assert(secondsInUtcDay(57'753) == 86'401 && secondsInUtcDay(57'754) == 86'400);
static_assert(secondsInFinalMinuteOfUtcDay(57'753) == 61,
              "the last minute of 2016-12-31 runs 23:59:00 to 23:59:60");
static_assert(secondsInFinalMinuteOfUtcDay(57'754) == 60);

} // namespace orb

#endif // ORBSIM_CORE_LEAPSECONDS_HPP
