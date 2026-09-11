# M1-03 — `TimePoint` and the time scales

Phase: A | Status: **done 2026-09-10**
Prerequisites: M1-01
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), and its update of 2026-09-10

**Amended 2026-09-10, before the code, on the owner's rulings.** Measured
first: the f64 day fraction this document asked for drifts 82.7 ns over the
million 1 us additions whose budget below is 1 ns, and a noon-based day puts
UTC's leap second in the middle of a stored day. The storage, the errors, the
epoch constants and the tests were amended to match; each changed bullet says
so. ADR 0009's update has the reasoning.

## Purpose

ADR 0006: *"a bare `Seconds` since an unstated epoch is not a time."*
`realism.md` ranks the time system second in the whole project precisely because
it is cheapest today — there are zero call sites to change — and expensive once
phases B, D and C have been written against a bare duration.

This task builds the representation only. The conversions between scales are
M1-04 and M1-05.

## What to implement

`src/core/Time.hpp` (and `Time.cpp` if the calendar conversion wants one).

- `enum class TimeScale : std::uint8_t { Utc, Tai, Tt, Tdb, Ut1 };`
- `template <TimeScale Scale> struct TimePoint`, so **TT and TDB are different
  types** and a conversion is a named function, exactly as `Radians` and
  `Degrees` are. Aliases `UtcTime`, `TaiTime`, `TtTime`, `TdbTime`, `Ut1Time`.
- **Storage is a day and the time within it** *(amended)*: a whole-valued
  Modified Julian Day in an f64, the day beginning at midnight, and the SI
  picoseconds since that midnight in an int64, `[0, 86 400 × 10¹²)`, kept
  normalised by construction. Resolution is **1 ps** everywhere in the day,
  where an f64 count of seconds since J2000 is about 0.5 µs at the century mark
  and an f64 fraction of a day up to 9.6 ps. The normalisation invariant is a
  postcondition, asserted — it cannot be produced by a caller, only by a bug in
  this file. *(Was: an integral JD and an f64 fraction in `[0, 1)`.)*
- Arithmetic **only on the uniform scales**. `operator+(Seconds)`,
  `operator-(Seconds)` and the difference `TimePoint - TimePoint -> Seconds`
  are constrained to `Tai`, `Tt` and `Tdb`. Adding an SI second to a UTC instant
  is not a well-defined operation — a UTC day may have 86401 of them — and
  making it a compile error is cheaper than making it a rule.
- Calendar conversion both ways, proleptic Gregorian, by Fliegel and Van
  Flandern's algorithm (*Communications of the ACM* 11(10), 1968), with the
  citation in the comment. `fromCalendar` reports rather than asserts: a month of
  13 comes from a scenario file, not from a bug. *(Amended:)* the supported years
  are **1–9999**, ISO 8601's four-digit years, and `toCalendar` reports an
  instant that arithmetic has carried outside them.
- `enum class TimeError : std::uint8_t { NotFinite, YearOutOfRange,
  InvalidMonth, InvalidDay, InvalidTimeOfDay }` with a `describe()`, in the
  shape `OrbitError` already sets. *(Amended: one error per field, so a test
  asking for one by name proves the check for that field fired. Was a single
  `InvalidCalendarDate`.)*
- *(Added:)* **A Julian-date interface**, which the published-epoch tests, the
  M1-06 fixtures and the ERFA-derived references all need: `fromJulianDate`
  takes a two-part date split any way and reports `NotFinite` and
  `YearOutOfRange`; `julianDate()` returns the midnight that begins the day and
  the correctly rounded fraction since.
- Epoch constants with their provenance in a comment: `kJ2000` = JD 2451545.0 =
  2000-01-01T12:00:00 TT, `kMjdZero` = JD 2400000.5, `kUnixEpoch` = JD 2440587.5.
  *(Amended: `kJ2000` is a `TtTime`, because J2000.0 is defined in TT;
  `kUnixEpoch` a `UtcTime`; `kMjdZero` a number of days, because it is an offset
  between two day counts rather than an instant.)*
- `static_assert`s: the size is 16 bytes *(amended; was two doubles)*;
  trivially copyable; no implicit conversion in either direction;
  `TimePoint<Tt>` does not convert to `TimePoint<Tdb>`;
  `TimePoint<Utc> + Seconds` does not compile; *(added)* there is no default
  constructor, because an instant is always a particular one.
- *(Added:)* adding a non-finite `Seconds` is a **precondition**, asserted, and
  a Release build that violates it returns a NaN day rather than undefined
  behaviour. Every duration added in this project is a validated step or a
  difference of two instants; reporting belongs where durations enter.

## Out of scope

Any conversion **between** scales — that is M1-04 and M1-05. Leap seconds.
Formatting for display, which arrives in phase G with the MFD clock. Durations
longer than an f64 second count can hold; the difference of two `TimePoint`s
returns `Seconds` and the resolution argument for that is written in the header.

## Tests

`tests/test_time.cpp`, a new Catch2 suite linking only `orbsim_core`.

- **Published epochs**, which are the independent reference: J2000.0 is
  2000-01-01T12:00:00 and JD 2451545.0; MJD zero is 1858-11-17T00:00:00; the
  Unix epoch is JD 2440587.5. Each checked both ways.
- **Calendar round trip** over a seeded sweep of 10,000 dates from 1900 to 2100,
  seed written down, failing case printed — and specifically 1900 (not a leap
  year), 2000 (a leap year, the century rule that catches people), 29 February,
  31 December, and every month end.
- **Resolution**, which is the claim the two-part representation exists to make:
  `t + 1 ns - t` recovers 1 ns to within 1e-11 s, and one million additions of
  1 µs drift by less than 1 ns in total. *(Amended:)* the additions start from a
  time of day far from zero, 17:31:12.345 — from J2000 in a noon-based day they
  would have passed under the representation that fails them.
- **Ordering** agrees with the sign of the difference, over the same sweep.
- **Normalisation** holds after every operation in the sweep.
- **Named failures**: month 0, month 13, day 32, 30 February, and a non-finite
  component each report their own `TimeError`, by name.
- **The type system**: `static_assert`s proving the scales do not interconvert
  and that UTC has no duration arithmetic.
- *(Added:)* the calendar against **`std::chrono`'s**, a second implementation
  with a different formulation, on every day of 1900–2100, and which dates
  exist against its `ok()`; the Julian-date conversions against exact values
  computed with rational arithmetic outside this project; and, beneath each
  budget, what the design guarantees — a budget a thousand times looser than
  the code cannot see a hundredfold regression.

## Error budget

Representation resolution **≤ 1e-11 s**; calendar round trip exact to
**1e-9 s** across 1900–2100. Both asserted.

## Verification

The standing rules. No GPU, no frames.

## Done when

- [ ] `check` green in both trees; `test_time` in the CTest list.
- [ ] The suite was seen to fail first — the commit message quotes the failure.
- [ ] `core/Time.hpp` is in the header self-check list.
- [ ] Every constant in the file says where its number came from.
