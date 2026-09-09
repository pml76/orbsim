# M1-04 — UTC, TAI and TT: the leap-second table

Phase: A | Status: not started
Prerequisites: M1-03
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md)

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
  `InvalidCalendarDate` on every other day. That is why UTC is stored as a
  quasi-Julian date whose days are not all the same length, and the header says
  so in one paragraph.

## Out of scope

TDB and UT1, which are M1-05. Predicting future leap seconds. The 1961–1971
rate-offset era, refused above. Any automatic download of the table: it is
committed data with a recorded provenance.

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
  seconds on an ordinary day reports `InvalidCalendarDate`. Each asked for by
  name, not by "it failed".

## Error budget

UTC ↔ TAI ↔ TT round trip **exact to 1e-9 s** over 1972–2035. TT − TAI is exact
by definition, so any deviation is a bug rather than a tolerance.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The table records the IERS bulletin it came from and its validity date.
- [ ] Expiry and the pre-1972 era are reported by name, with a test for each.
- [ ] The leap second at 2016-12-31T23:59:60 round-trips.
