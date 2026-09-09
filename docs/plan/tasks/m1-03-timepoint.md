# M1-03 — `TimePoint` and the time scales

Phase: A | Status: not started
Prerequisites: M1-01
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md)

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
- **Storage is a two-part Julian date**: `dayNumber` holding an integral-valued
  JD and `dayFraction` in `[0, 1)`, kept normalised by construction. That is
  what buys the resolution: the fraction's ulp is about 1.1e-16 day ≈ **10 ps**,
  where an f64 count of seconds since J2000 is about 0.5 µs at the century mark.
  The normalisation invariant is a postcondition, asserted — it cannot be
  produced by a caller, only by a bug in this file.
- Arithmetic **only on the uniform scales**. `operator+(Seconds)`,
  `operator-(Seconds)` and the difference `TimePoint - TimePoint -> Seconds`
  are constrained to `Tai`, `Tt` and `Tdb`. Adding an SI second to a UTC instant
  is not a well-defined operation — a UTC day may have 86401 of them — and
  making it a compile error is cheaper than making it a rule.
- Calendar conversion both ways, proleptic Gregorian, by Fliegel and Van
  Flandern's algorithm (*Communications of the ACM* 11(10), 1968), with the
  citation in the comment. `fromCalendar` reports rather than asserts: a month of
  13 comes from a scenario file, not from a bug.
- `enum class TimeError : std::uint8_t { NotFinite, InvalidCalendarDate, … }`
  with a `describe()`, in the shape `OrbitError` already sets.
- Epoch constants with their provenance in a comment: `kJ2000` = JD 2451545.0 =
  2000-01-01T12:00:00 TT, `kMjdZero` = JD 2400000.5, `kUnixEpoch` = JD 2440587.5.
- `static_assert`s: the size is two doubles; trivially copyable; no implicit
  conversion in either direction; `TimePoint<Tt>` does not convert to
  `TimePoint<Tdb>`; `TimePoint<Utc> + Seconds` does not compile.

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
  1 µs drift by less than 1 ns in total.
- **Ordering** agrees with the sign of the difference, over the same sweep.
- **Normalisation** holds after every operation in the sweep.
- **Named failures**: month 0, month 13, day 32, 30 February, and a non-finite
  component each report their own `TimeError`, by name.
- **The type system**: `static_assert`s proving the scales do not interconvert
  and that UTC has no duration arithmetic.

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
