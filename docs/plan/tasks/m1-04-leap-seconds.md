# M1-04 — UTC, TAI and TT: the leap-second table

Phase: A | Status: **done, 2026-09-18**
Prerequisites: M1-03
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

## Purpose

UTC is the user's clock, TAI is the uniform one underneath it, and TT is what
ephemerides and dynamics are expressed in. The difference between them is not a
constant and it is not small: ΔAT has stepped from 10 s in 1972 to 37 s, and TT
is TAI plus exactly 32.184 s by definition. Getting this wrong displaces a
ground track by hundreds of metres and does it plausibly.

## What to implement

`src/core/LeapSeconds.hpp` / `.cpp`, and the conversions in `src/core/Time.hpp`.

- **The ΔAT table**, as published by the IERS. At the time of writing the last
  entry is 2017-01-01 with ΔAT = 37 s; **confirm against the current IERS
  Bulletin C before committing** and record the bulletin number and date in the
  file. Each row carries the UTC date it takes effect and the integer seconds.
- **A validity end date**, and `TimeError::LeapSecondTableExpired` reported for
  any instant beyond it. This is the important half: leap seconds are announced
  about six months ahead, so a table is always finite, and quietly extrapolating
  the last value is how a simulator becomes silently wrong a year after release.
- `TimeError::BeforeLeapSecondEra` for anything before 1972-01-01, when UTC ran
  at a different rate rather than in whole seconds. Refused rather than
  approximated.
- Conversions, each `[[nodiscard]]` and reporting:
  `taiFromUtc`, `utcFromTai`, `ttFromTai`, `taiFromTt`, and the composed
  `ttFromUtc` / `utcFromTt`. TT − TAI is `32.184 s` **exactly**, a defined
  constant, with the definition cited in the comment.
- **The leap second itself is representable.** `fromCalendar` accepts a seconds
  field of 60 exactly on the days the table says have one, and reports
  `InvalidTimeOfDay` on every other day. M1-03 stores the time of day as SI
  picoseconds since midnight, so a UTC day with a leap second simply runs to
  86 401 s: 23:59:60.5 is 86 400.5 s into the day, exactly, and the invariant
  for UTC becomes "within that day's length" -- which the header says in one
  paragraph. *(Amended 2026-09-10 with M1-03: the error was
  `InvalidCalendarDate`, and UTC was to be a quasi-Julian date whose days are
  not all the same length. See ADR 0009's update.)*

## Out of scope

TDB and UT1, which are M1-05. Predicting future leap seconds. The 1961–1971
rate-offset era, refused above. Any automatic download of the table: it is
committed data with a recorded provenance.

*(Added 2026-09-11.)* **ERFA's `eraDat`, deliberately** (ADR 0016, decision
28), although ERFA arrives for the astronomy in M1-05. It extrapolates past its
own table — in silence until 2028, with a warning after — which is the
behaviour ADR 0009 rules out, and replacing its table means
`eraSetLeapSeconds`, which changes state for the whole process. This task's
arithmetic stays exact integer picoseconds, and its table comes from the IERS.

## Tests

Extends `tests/test_time.cpp`.

- **Every step in the table**, checked one second before and one second after
  the boundary. The published table is the reference; our code is the lookup and
  the arithmetic around it.
- **A worked example end to end**: 2017-01-01T00:00:00 UTC is
  2017-01-01T00:00:37 TAI and 2017-01-01T00:01:09.184 TT.
- **The leap second exists**: 2016-12-31T23:59:60 UTC is a valid instant, and it
  maps to 2017-01-01T00:00:36 TAI — one second before the instant above.
- **Round trip** UTC → TAI → UTC over a seeded sweep that includes every step
  boundary, exact to 1e-9 s.
- **Monotonicity**: TAI increases across every boundary, including through the
  repeated UTC second.
- **Named failures**: an instant past the validity date reports
  `LeapSecondTableExpired`; one before 1972 reports `BeforeLeapSecondEra`; 60
  seconds on an ordinary day reports `InvalidTimeOfDay`. Each asked for by
  name, not by "it failed".

## Error budget

UTC ↔ TAI ↔ TT round trip **exact to 1e-9 s**. TT − TAI is exact by definition,
so any deviation is a bug rather than a tolerance.

*(Amended 2026-09-18, register decision 38.)* This read "over 1972–2035", which
asserts a round trip over years the table deliberately refuses. **UTC ↔ TAI is
exact wherever the table is valid**, which is 1972-01-01 to 2027-01-01 today;
**TAI ↔ TT is exact over all of 1972–2035**, because it needs no table.

**Measured 2026-09-18: the error is zero.** ΔAT is a whole number of seconds and
TT − TAI is a whole number of picoseconds, so every step is integer arithmetic
on the stored picosecond count and there is nothing to round. The suite asserts
**bit identity** rather than the 1e-9 s budget, over 10,000 seeded instants and
eight offsets at each of the 27 boundaries -- a budget a thousand times looser
than the arithmetic cannot see a regression in it.

## Verification

The standing rules.

## Done when

- [x] `check` green in both trees. **Also** `linux-gcc`, `linux-sanitize` and
      `windows-msvc`: 78 CTest entries on Windows, 77 on the core-only Linux
      presets, all passing, and 747,987 assertions identical across all five.
- [x] The table records the IERS bulletin it came from and its validity date.
      **IERS Bulletin C 72**, Paris, 2026-07-06, confirmed against the bulletin
      itself and against the IERS/IANA `leap-seconds.list` (hash
      `a9bad145 84c31c70 758402aa b37bfd54 5923836a`), which agree; the MJDs
      were converted from the NTP seconds independently rather than copied.
      Validity ends **2027-01-01T00:00:00 UTC** (decision 32), and
      [`../../STATUS.md`](../../STATUS.md) carries that date.
- [x] Expiry and the pre-1972 era are reported by name, with a test for each.
- [x] The leap second at 2016-12-31T23:59:60 round-trips.

**Beyond what was asked:** a second fuzz target,
[`tests/fuzz_time.cpp`](../../../tests/fuzz_time.cpp) (decision 41), 6,252,716
executions clean on the first run; a synthetic negative leap second, which no
published table can reach (decision 33); and `std::chrono::get_leap_second_info`
as an independent oracle over every one of the 20,089 days of the era
(decision 37).
