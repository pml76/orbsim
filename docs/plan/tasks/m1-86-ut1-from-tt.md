# M1-86 — UT1 from TT

Phase: A | Status: **done, 2026-09-20** *(corrected 2026-09-21: this said
2026-09-19, which is the day decisions 72-74 were ruled and this document was
written. The code landed the next day -- commit `2a82e37`, and
[`HISTORY.md`](../../HISTORY.md) and [`STATUS.md`](../../STATUS.md) both say
2026-09-20.)*
Prerequisites: M1-05. Runs **before M1-07**, which needs it (decision 73)
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md); register decisions 72–74

**Split off from [M1-07](m1-07-earth-orientation.md) on 2026-09-19**, before
either had code, on the owner's rulings of that day. M1-05 left M1-07 one
question (decision 66): the simulation's clock runs on TT, TT reached UT1 only
through UTC, and UTC only through the leap-second table -- so the Earth's
orientation, as planned, would have been refused past 2027-01-01. The answer
(decision 72) is a conversion of its own, in `core/Time.hpp`, and it is a
task of its own because it is one commit that can be verified on its own
(decision 73). Numbered 86 rather than renumbering the queue: the numbers are
identifiers, as decision 30 ruled for M1-85.

## Purpose

UT1 is the angle the Earth has turned through, continuous in TT. The
leap-second table is a labelling convention for UTC. Routing the one through
the other made the Earth's orientation inherit the table's two edges -- no
Earth before 1972-01-01 or from 2027-01-01, 104 days from the day this was
written and fifteen minutes of simulated time at 10,000x -- and, with DeltaUT1
unmodelled, a step back of one second of rotation, 15" or about 465 m at the
equator, at the midnight after every leap second while TT ran on. None of that
is physics.

The table refuses because a UTC label is an integer a committee has not yet
chosen. UT1 is a quantity whose prediction is a model with a size, which is
exactly how DeltaUT1 = 0 is already treated. So the conversion is
UT1 = TT - DeltaT, with DeltaT = TT - UT1 a model the caller chooses and names.

## What to implement

In `src/core/Time.hpp`, beside UT1 -- it needs no ERFA.

- **`DeltaT`**, TT - UT1, shaped as `DeltaUt1` is: validated factories,
  integer picoseconds, no default constructor. `fromSeconds` to the nearest
  picosecond, splitting the whole seconds off before scaling so that a DeltaT
  of days keeps its picoseconds; `fromPicoseconds`, exact. Both report
  `NotFinite` and a new **`DeltaTOutOfRange`** beyond **10^6 s**, inclusive.
  That limit is of the representation, not of the Earth: DeltaT has no defined
  bound, and 10^18 ps keeps an instant's picoseconds less DeltaT's nine times
  inside an int64. The largest DeltaT any model gives inside the calendar's
  years is Morrison and Stephenson's (2004) long-term parabola at 9999, about
  2.1e5 s.
- **`ut1FromTt(TtTime, DeltaT)`** and **`ttFromUt1(Ut1Time, DeltaT)`**: exact
  integer arithmetic on the stored picoseconds, carried over days of 86 400 s.
  Neither can fail, and neither returns `std::expected`.
- **`deltaTFromLeapSecondTable(TtTime, DeltaUt1)`**: 32.184 s + DeltaAT -
  DeltaUT1, with DeltaAT the value in force during the UTC day the instant falls
  in, so that inside a leap second it is still the old one. Refused by name,
  `BeforeLeapSecondEra` and `LeapSecondTableExpired`, where the table has no
  DeltaAT to give.
- **`kDeltaTHeldAtTableExpiry`**: the same at the table's last step with DeltaUT1
  unmodelled, 32.184 + 37 = 69.184 s, derived from the table so that renewing
  it with a new step moves it too. A caller past the expiry takes it by name.
- **The model error, in the header, as numbers.** From the table with DeltaUT1
  unmodelled: at most 0.9 s, as before. Held from some date: that, plus the
  drift since -- from the IERS EOP 20 C04 series, 1962 to 2026-08-20, at worst
  **1.15 s in one year** (1972) and 10.4 s in ten; since 2000, 0.54 s and 3.5 s;
  about +0.1 s a year in 2026.
- *(Decision 74.)* **`tests/fuzz_time.cpp`** gains a DeltaT from arbitrary bytes,
  both round trips, and the table's DeltaT against the UTC road.

## Out of scope

A DeltaT model for all dates -- the IERS series for 1962 on, Morrison et al.
(2021) before it, an extrapolation after -- which would give historical
scenarios UT1 to about 0.1 s. Considered in decision 72 and not taken: it is a
body of reference data to carry and verify, and milestone 1's fences defer
"DeltaUT1 from IERS data". The signature admits it later unchanged, since
DeltaT is a parameter. Anything that *uses* UT1: M1-07.

## Tests

In `tests/test_time.cpp`, every expected value an integer number of
picoseconds worked out from the definitions, with DeltaAT from the suite's own
transcription of the published steps rather than from `core/LeapSeconds.hpp`.

- **The table's DeltaT is 32.184 s + DeltaAT - DeltaUT1 either side of every
  step**: half a second after each step's UTC midnight, half a second before --
  inside the leap second -- and a second and a half before, for three DeltaUT1.
- **UT1 from the table's DeltaT is the UTC road's UT1 to the picosecond**, over
  a seeded sweep of 1972-2026 with DeltaUT1 drawn over its range, and through
  every leap second a quarter of a second at a time.
- **UT1 = TT - DeltaT**, carried across midnight both ways and across days, for
  a negative DeltaT -- as around 1900 -- and at the limit.
- **Both round trips exact**, bit for bit, with DeltaT drawn to the picosecond
  over its whole range.
- **Through the leap second at the end of 2016, a held DeltaT turns UT1 by
  exactly each TT step**, while the UTC road steps back once, at UTC midnight.
- **Every refusal by name**, and the edges that are accepted: 10^6 s from either
  factory, a picosecond past it refused, and 2^17 + 2^-35 s kept to the
  picosecond, which scaling the whole value would lose.
- **The table's DeltaT refused by name** a picosecond either side of both of
  the table's edges, and at the Apollo 11 landing and in 2050.
- **The held DeltaT is the table's last step**, 69.184 s.

## Error budget

**Exact.** Both round trips are the identity, and UT1 from the table's DeltaT
is the UTC road's UT1 to the picosecond, both asserted. **The model error is
the caller's choice of DeltaT**, stated in the header and not asserted: at most
0.9 s from the table with DeltaUT1 unmodelled, and past that the drift of a
held value, measured from the IERS series.

## Done when

- [x] `check` green in both trees.
- [x] Every conversion exact, and the table's DeltaT agreeing with the UTC road
      to the picosecond, asserted.
- [x] The model error in the header as numbers, with their source.
- [x] `fuzz_time` carries the new claims, and was run.
