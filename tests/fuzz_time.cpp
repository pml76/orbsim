//
// libFuzzer entry point for the time scales.
//
// VERIFICATION.md rule 13, applied to the second surface in this project that
// takes bytes from outside and turns them into numbers. A calendar date is six
// fields a scenario file supplies; a Julian date is two doubles a fixture
// supplies; and the leap-second table turns either into an instant on another
// scale. The interesting inputs are the ones nobody writes down -- a second of
// 59.999999999999996, a Julian date a picosecond either side of a leap second,
// a date on the far side of the table's expiry -- and a fuzzer writes them down
// for you. The same harness found five distinct defects in the orbital core,
// none of them reachable by any test anyone had written.
//
// What is asserted is deliberately narrow, because the fuzzer does not know
// what the right answer is. It knows what must never happen:
//
//   * no crash and no undefined behaviour -- ASan and UBSan are linked into
//     this target and enforce those, and UBSan is the reason a fuzzer is worth
//     running over integer arithmetic at all: a signed overflow in a day count
//     is exactly the defect that would otherwise surface as a plausible date;
//   * a reported failure is a legitimate outcome for any input;
//   * but a *success* must obey what the types promise. A normalised instant, a
//     Julian-date fraction inside [0, 1), a UTC second of 60 only where the
//     table says one exists -- and, the claim this task rests on, **a UTC to
//     TAI round trip that returns the instant it started from, bit for bit.**
//     Not nearly: the arithmetic is whole seconds and whole picoseconds, so
//     anything short of identity is a defect rather than a rounding.
//   * *(M1-05, register decision 64.)* A DeltaUT1 accepted only inside 0.9 s;
//     UT1 -> UTC -> UT1 bit for bit wherever UTC has the instant, for the same
//     reason; and TT -> TDB -> TT within the one picosecond each rounding
//     allows, both ways, at any date the calendar holds -- ERFA underneath,
//     across all of years 1 to 9999.
//   * *(M1-86, register decision 74.)* A DeltaT accepted only inside 10^6 s;
//     TT -> UT1 -> TT and UT1 -> TT -> UT1 bit for bit, under any DeltaT the
//     factory accepts; and, wherever the leap-second table gives a DeltaT, UT1
//     from it exactly where the UTC road puts UT1 -- the claim that lets the
//     simulation's TT clock stop going through UTC at all.
//
// Build and run, on Windows, where the fuzzing happens (VERIFICATION.md rule
// 13):
//
//     cmake --preset windows-fuzz
//     cmake --build build/windows-fuzz
//     build\windows-fuzz\fuzz_time.exe -max_total_time=60
//
// Every tree also compiles this file without libFuzzer, into
// orbsim_fuzz_objects, so that the full warning set and check's lint cover it
// even where the fuzzer itself is not built.
//
#include "astro/Tdb.hpp"
#include "core/LeapSeconds.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace {

using orb::CalendarDate;
using orb::DeltaT;
using orb::DeltaUt1;
using orb::f64;
using orb::JulianDate;
using orb::Seconds;
using orb::TaiTime;
using orb::TdbTime;
using orb::TimePoint;
using orb::TimeScale;
using orb::Tolerance;
using orb::TtTime;
using orb::Ut1Time;
using orb::UtcTime;

// libFuzzer reports a non-zero exit as the finding, so abort is the whole
// vocabulary available for saying "this input broke an invariant".
void require(bool held) {
    if (!held) std::abort();
}

// Normalisation, checked from outside the class: a whole day, and a time of day
// inside it. The bound is a second longer for UTC, whose day may hold 86 401 SI
// seconds -- which is the invariant M1-04 widened, so it is the one most worth
// throwing arbitrary bytes at.
template <TimeScale Scale> void requireNormalised(const TimePoint<Scale>& t) {
    const f64 mjd = t.modifiedJulianDay();
    require(orb::isFinite(mjd));
    require(
        orb::nearlyEqual(mjd, static_cast<f64>(static_cast<std::int64_t>(mjd)), Tolerance{0.0}));
    require(t.picosecondOfDay() >= 0);
    const std::int64_t longest = Scale == TimeScale::Utc
                                     ? orb::kPicosecondsPerDay + orb::kPicosecondsPerSecond
                                     : orb::kPicosecondsPerDay;
    require(t.picosecondOfDay() < longest);
}

// The fraction is of that instant's own day, whatever its length, and never
// reaches one. Before M1-04 it would have reached 1.0000116 inside a leap
// second, which is the regression this guards.
template <TimeScale Scale> void requireJulianDate(const TimePoint<Scale>& t) {
    const JulianDate jd = t.julianDate();
    require(orb::isFinite(jd.day) && orb::isFinite(jd.fraction));
    require(jd.fraction >= 0.0 && jd.fraction < 1.0);
}

// The claim M1-04 rests on, checked on whatever instant the fuzzer reached.
void requireRoundTrip(UtcTime utc) {
    const auto tai = orb::taiFromUtc(utc);
    if (!tai) return; // outside the table is a legitimate outcome, by name
    requireNormalised(*tai);

    const auto back = orb::utcFromTai(*tai);
    require(back.has_value());
    require(*back == utc);

    // And through TT, which adds an exact 32.184 s in the middle.
    const auto viaTt = orb::utcFromTt(orb::ttFromTai(*tai));
    require(viaTt.has_value());
    require(*viaTt == utc);
}

// A second of 60 is admissible only in the final minute of a UTC day the table
// gives a positive leap second to. Anywhere else it must have been refused, and
// this is where a table consulted at the wrong moment would show itself.
void requireLeapSecondIsWhereTheTableSaysItIs(Seconds second, UtcTime utc) {
    if (second.value() < 60.0) return;
    const auto mjd = static_cast<std::int64_t>(utc.modifiedJulianDay());
    require(orb::leapSecondsAtEndOfUtcDay(mjd) == 1);
    require(utc.picosecondOfDay() >= orb::kPicosecondsPerDay);
}

// M1-05's three claims, on whatever the fuzzer reached (register decision 64).
//
// A DeltaUT1 the factory accepts is inside 0.9 s, to the picosecond.
void requireDeltaUt1InRange(DeltaUt1 delta) {
    require(delta.picoseconds() <= orb::kDeltaUt1LimitPicoseconds);
    require(delta.picoseconds() >= -orb::kDeltaUt1LimitPicoseconds);
}

// UT1 -> UTC -> UT1 is exact wherever UTC has the instant -- which is the
// direction that is a claim everywhere; the other is two-to-one inside a leap
// second (decision 59). A refusal is a legitimate outcome, by name.
void requireUt1RoundTrip(Ut1Time ut1, DeltaUt1 delta) {
    const auto utc = orb::utcFromUt1(ut1, delta);
    if (!utc) return;
    requireNormalised(*utc);
    const Ut1Time back = orb::ut1FromUtc(*utc, delta);
    requireNormalised(back);
    require(back == ut1);
}

// |a - b| in picoseconds, for two instants a picosecond or so apart.
template <TimeScale Scale>
// The two are interchangeable: this returns |a - b|, whose sign is taken off
// at the end, so transposing them cannot produce a wrong answer.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::int64_t picosecondsApart(const TimePoint<Scale>& a, const TimePoint<Scale>& b) {
    const f64 days = a.modifiedJulianDay() - b.modifiedJulianDay();
    const std::int64_t apart = (static_cast<std::int64_t>(days) * orb::kPicosecondsPerDay) +
                               (a.picosecondOfDay() - b.picosecondOfDay());
    return apart < 0 ? -apart : apart;
}

// TT -> TDB -> TT, and the other way, within the one picosecond each rounding
// allows (decision 57), at any instant the calendar holds.
void requireTdbRoundTrip(TtTime tt) {
    const TdbTime tdb = orb::tdbFromTt(tt);
    requireNormalised(tdb);
    require(picosecondsApart(orb::ttFromTdb(tdb), tt) <= 1);
}

void requireTtRoundTrip(TdbTime tdb) {
    const TtTime tt = orb::ttFromTdb(tdb);
    requireNormalised(tt);
    require(picosecondsApart(orb::tdbFromTt(tt), tdb) <= 1);
}

// All of M1-05's, on one date: TDB from it on the two dynamical scales, and
// UT1 from a DeltaUT1 the fuzzer chose -- accepted only inside 0.9 s, and then
// applied to the date read as UT1 and as UTC. A function of its own, so that
// the entry point stays inside readability-function-size.
void requireTdbAndUt1(const CalendarDate& date, Seconds deltaUt1) {
    if (const auto tt = TtTime::fromCalendar(date); tt) requireTdbRoundTrip(*tt);
    if (const auto tdb = TdbTime::fromCalendar(date); tdb) requireTtRoundTrip(*tdb);

    const auto delta = DeltaUt1::fromSeconds(deltaUt1);
    if (!delta) return; // refused by name, which is a legitimate outcome
    requireDeltaUt1InRange(*delta);
    if (const auto ut1 = Ut1Time::fromCalendar(date); ut1) requireUt1RoundTrip(*ut1, *delta);
    if (const auto utc = UtcTime::fromCalendar(date); utc) {
        const Ut1Time ut1 = orb::ut1FromUtc(*utc, *delta);
        requireNormalised(ut1);
        requireUt1RoundTrip(ut1, *delta);
    }
}

// The two offsets the fuzzer chooses, as one parameter: both are Seconds, and
// transposed they would test a different claim in silence.
struct Offsets {
    Seconds deltaT;
    Seconds deltaUt1;
};

// M1-86's claims (register decision 74), on the same date read as TT and as
// UT1. A function of its own, for readability-function-size's sake, as M1-05's
// are.
void requireUt1FromTt(const CalendarDate& date, Offsets offsets) {
    if (const auto held = DeltaT::fromSeconds(offsets.deltaT); held) {
        require(held->picoseconds() <= orb::kDeltaTLimitPicoseconds);
        require(held->picoseconds() >= -orb::kDeltaTLimitPicoseconds);
        if (const auto tt = TtTime::fromCalendar(date); tt) {
            const Ut1Time ut1 = orb::ut1FromTt(*tt, *held);
            requireNormalised(ut1);
            require(orb::ttFromUt1(ut1, *held) == *tt);
        }
        if (const auto ut1 = Ut1Time::fromCalendar(date); ut1) {
            const TtTime tt = orb::ttFromUt1(*ut1, *held);
            requireNormalised(tt);
            require(orb::ut1FromTt(tt, *held) == *ut1);
        }
    }

    // The table's DeltaT, wherever it has one, against the UTC road.
    const auto delta = DeltaUt1::fromSeconds(offsets.deltaUt1);
    const auto tt = TtTime::fromCalendar(date);
    if (!delta || !tt) return;
    const auto fromTable = orb::deltaTFromLeapSecondTable(*tt, *delta);
    if (!fromTable) return; // outside the table, by name
    const auto utc = orb::utcFromTt(*tt);
    require(utc.has_value());
    require(orb::ut1FromTt(*tt, *fromTable) == orb::ut1FromUtc(*utc, *delta));
}

// The whole input, as one trivially copyable layout: a calendar date, a
// Julian date in two parts, a DeltaUT1 and a DeltaT in seconds. One memcpy from libFuzzer's buffer
// fills it, which is how this harness reads its bytes without doing arithmetic on a raw pointer --
// cppcoreguidelines-pro-bounds-pointer-arithmetic and clang's -Wunsafe-buffer-usage both object to
// that, and both are right that a pointer walked by hand is how a fuzz harness gets its own buffer
// overrun.
struct RawInput {
    std::int32_t year;
    std::int32_t month;
    std::int32_t day;
    std::int32_t hour;
    std::int32_t minute;
    double second;
    double julianDay;
    double julianFraction;
    double deltaUt1;
    double deltaT;
};
static_assert(std::is_trivially_copyable_v<RawInput>,
              "the input is filled by one memcpy, so it must be copyable that way");

} // namespace

// The entry point, declared as libFuzzer's own driver declares it. libFuzzer
// ships no header for it, and a definition with no declaration before it is
// what clang's -Wmissing-prototypes and gcc's -Wmissing-declarations report;
// declaring it answers both rather than silencing either. The name and
// signature are libFuzzer's, not this codebase's, so the naming check cannot
// apply to them, and clang-tidy reports the name at its first declaration.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < sizeof(RawInput)) return 0;

    // memcpy rather than a cast: the bytes are not aligned for a double, and
    // type-punning through a reinterpret_cast is undefined -- which UBSan would
    // rightly report as a bug in this harness rather than in the code under it.
    // libFuzzer hands the input over as a pointer and a size, and the size is
    // checked above; a C library copy from a bare pointer is what clang's
    // -Wunsafe-buffer-usage-in-libc-call reports, and it is off for this one
    // call (ADR 0017) -- for clang alone, since gcc has no such warning and
    // would report a pragma it does not recognise.
    RawInput raw{};
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
    std::memcpy(&raw, data, sizeof(RawInput));
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    const CalendarDate date{
        .year = raw.year,
        .month = raw.month,
        .day = raw.day,
        .hour = raw.hour,
        .minute = raw.minute,
        .second = Seconds{raw.second},
    };

    if (const auto utc = UtcTime::fromCalendar(date); utc) {
        requireNormalised(*utc);
        requireJulianDate(*utc);
        requireLeapSecondIsWhereTheTableSaysItIs(date.second, *utc);
        requireRoundTrip(*utc);
    }

    // The same date on a uniform scale, where 60 is never a second and the day
    // always holds 86 400 of them.
    if (const auto tai = TaiTime::fromCalendar(date); tai) {
        requireNormalised(*tai);
        requireJulianDate(*tai);
        require(tai->picosecondOfDay() < orb::kPicosecondsPerDay);
        require(orb::taiFromTt(orb::ttFromTai(*tai)) == *tai);
        requireNormalised(orb::ttFromTai(*tai));
        if (const auto utc = orb::utcFromTai(*tai); utc) {
            requireNormalised(*utc);
            requireRoundTrip(*utc);
        }
    }

    // A Julian date split any way at all -- ERFA's convention, and the one
    // place a fixture's two doubles arrive unchecked.
    const JulianDate julian{.day = raw.julianDay, .fraction = raw.julianFraction};
    if (const auto utc = UtcTime::fromJulianDate(julian); utc) {
        requireNormalised(*utc);
        requireJulianDate(*utc);
        requireRoundTrip(*utc);
    }
    if (const auto tai = TaiTime::fromJulianDate(julian); tai) {
        requireNormalised(*tai);
        requireJulianDate(*tai);
    }

    requireTdbAndUt1(date, Seconds{raw.deltaUt1});
    requireUt1FromTt(date, {.deltaT = Seconds{raw.deltaT}, .deltaUt1 = Seconds{raw.deltaUt1}});
    return 0;
}
