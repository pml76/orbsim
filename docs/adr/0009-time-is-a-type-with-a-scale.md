# ADR 0009: Time is a type with a scale, and the astronomy lives in `src/astro/`

Status: accepted (2026-09-08; recorded 2026-09-09)

Decisions 14 and 15 of
[the milestone 1 register](../plan/milestone-1-decisions.md), with the phase A
error budgets from its section 5.

## Decision

[`0006`](0006-simulation-not-sandbox.md) says it in one line: *a bare `Seconds`
since an unstated epoch is not a time*. This is what replaces it.

- **Five scales, five types.** `UTC`, `TAI`, `TT`, `TDB` and `UT1` are the
  parameter of a `TimePoint<Scale>`, so `TtTime` and `TdbTime` are different
  types and a conversion is a named function -- exactly the arrangement
  [`0001`](0001-units-in-the-type-system.md) makes for `Radians` and `Degrees`.
  Nothing converts implicitly in either direction.
- **Storage is a two-part Julian date**: an integral day number and a fraction
  in `[0, 1)`, normalised by construction, the invariant asserted as a
  postcondition because only a bug in that file can break it. This is what buys
  the resolution: the fraction's ulp is about 1.1e-16 day, near **10 ps**,
  where an `f64` count of seconds since J2000 is about **0.5 us** at the
  century mark. The budgets below need the former.
- **Duration arithmetic exists only on the uniform scales.** `TimePoint +
  Seconds` and the difference of two `TimePoint`s are constrained to `TAI`,
  `TT` and `TDB`. Adding an SI second to a UTC instant is not a well-defined
  operation -- a UTC day may have 86401 of them -- and making it a compile
  error is cheaper than making it a rule.
- **The leap-second table reports rather than extrapolates.** A date past the
  table's expiry is `LeapSecondTableExpired`, by name; a date before
  1972-01-01, when UTC began running on integer seconds, is
  `BeforeLeapSecondEra`. A table that extrapolates is a table that invents the
  future and does not say so.
- **IAU 2006 precession with the Earth rotation angle** gives the body-fixed
  frame, and an analytic solar position gives the Sun.
- **The omissions are recorded with their sizes**, which is the part that keeps
  them from being mistaken for defects later:

  | Claim | Budget | Kind |
  |---|---|---|
  | UTC/TAI/TT round trip, 1972-2035 | exact to **1e-9 s** | code, asserted |
  | TDB - TT | **100 us** -- 3 m of Earth's orbital motion | code, asserted |
  | Precession + ERA, as implemented | **0.1"** against an IERS/ERFA value | code, asserted |
  | Precession + ERA, as modelled | **<= 40"**, about 1.2 km on the ground: nutation omitted (<= 25") and dUT1 = 0 (<= 15") | model, recorded |
  | Solar direction, distance | **0.01 deg**, **2e-4 AU** -- 0.04 % of irradiance | code, asserted against JPL Horizons |

  A model error is written down and **not** asserted, because it is a decision
  with a size rather than something the code got wrong. Adding nutation later
  is then a measurable improvement instead of a surprise.
- **The astronomy lives in a new `src/astro/`**, inside the same `orbsim_core`
  target. `src/orbit/` stays about trajectories; `astro/` takes where bodies
  are and how frames rotate. No new link dependency, and no renderer anywhere
  near either.

## What we considered

**Seconds since J2000 as a bare `f64`.** One number, no types, and what most
hobby simulators do. Three things are wrong with it here, and only the first is
obvious: it cannot say which scale it is in, so TT and TDB -- which differ by
under 2 ms -- mix silently and produce a plausible answer; it loses about half
a microsecond of resolution at a century's distance, which is inside the TDB
budget above; and it makes UTC arithmetic quietly wrong rather than impossible,
because subtracting two UTC instants across a leap second is off by a second
and nothing says so.

**Deferring the whole thing to phase E**, where the integrator first needs it.
This is the tempting one, because nothing in phases A, B, D or C strictly
requires a time scale. It is rejected for exactly the reason
[`../plan/realism.md`](../plan/realism.md) ranks the time system second out of
fourteen: it is cheapest today, at **zero call sites**, and B, D and C all come
before E. Waiting means four phases of code get written against a bare
duration, and then the property that earned it the rank has been spent.

**Leaving the astronomy in `src/orbit/`.** One fewer directory. `orbit/` is
about the shape of a trajectory around a body; where that body is, and how its
frame turns, is a different subject with different reference data and different
failure modes. Keeping them apart is the same instinct as keeping the renderer
out of the physics, one level down.

## Why

The Mars Climate Orbiter argument in [`0001`](0001-units-in-the-type-system.md)
applies to time with one difference that makes it worse: the numbers are close
enough to look right. Metres and feet differ by a factor of three and a wrong
answer looks wrong. TAI and UTC differ by 37 seconds, TT and TDB by under two
milliseconds, and UT1 from UTC by under a second -- so a scale confusion
produces a trajectory that is plausible, self-consistent, and out by a few
kilometres. That is the failure mode
[`../VERIFICATION.md`](../VERIFICATION.md) exists for, and the type system is
the only thing that catches it before a test does.

The two-part Julian date is the same argument about representation rather than
naming. A budget of 100 us on TDB - TT cannot be checked with a representation
whose resolution at the same epoch is 0.5 us, because then a third of the
budget is spent on the clock.

## What this record does not decide

- **Whether nutation and dUT1 are ever modelled.** They are deferred with their
  budgets stated, so the decision to add them can be made against a number.
- **The ephemeris.** Milestone 1 lights the scene with an analytic Sun and
  nothing pulls on anything; DE440 against VSOP87/ELP2000 is still open in
  [`../plan/realism.md`](../plan/realism.md) section 4.
- **Display formatting**, which arrives with the MFD clock in phase G.
- **Whether `Vec3` acquires a frame.** Several frames now exist as a concept;
  whether the type carries one is
  [`../PROJECT_STATE.md`](../PROJECT_STATE.md) section 7.6, still open.

## Update, 2026-09-10: midnight days, and integer picoseconds

Written with [M1-03](../plan/tasks/m1-03-timepoint.md), before its code, on the
owner's rulings of the same day. **The decision stands** -- five scales as five
types, a two-part date with an integral day number, resolution at 10 ps or
better -- and two of its particulars change.

- **The day is a Modified Julian Day, and it begins at midnight**, where the
  record above implied the integral Julian date, whose day begins at noon.
  UTC's leap second comes at the end of a civil day, so with noon-based days
  23:59:60 would fall in the middle of a stored day. ERFA divides its UTC
  quasi-Julian dates at midnight for the same reason -- "the quasi-JD day
  represents UTC days whether the length is 86399, 86400 or 86401 SI seconds"
  (`dtf2d.c`) -- and the IERS tables M1-04 and M1-05 read are keyed by MJD.
- **The time within the day is an integer count of picoseconds**, not an f64
  fraction of a day. Measured before any code was written: a million additions
  of 1 us to an f64 fraction drift **82.7 ns**, against the 1 ns M1-03 asked
  for, because 1 us has no exact binary representation and every addition
  rounds it the same way. From fraction 0 -- which is J2000 in a noon-based day
  -- the same run drifts 6 ps, so the obvious test would have passed and proved
  nothing. In integer picoseconds the run is exact, as is every decimal step a
  simulation clock takes, and the resolution is 1 ps everywhere in the day
  rather than up to 9.6 ps. A day of 86 401 s is 8.64e16 ps, which an int64
  holds 106 times over. Orekit made the same move in its version 13, to integer
  seconds and attoseconds, and gives the same reasons: robustness, no IEEE-754
  edge cases, and being "decimal-friendly".

So "the fraction's ulp is about 1.1e-16 day, near 10 ps" above now describes
the Julian-date *interface* rather than the storage: `julianDate()` still
returns an f64 fraction, correctly rounded, whose ulp near the end of a day is
9.6 ps. The day number stays an f64, whole-valued, so that no finite duration
can overflow it.

## Partly superseded by 0016, 2026-09-11

[`0016`](0016-the-astronomy-is-erfa.md) makes ERFA compute the astronomy, and
in doing so replaces three things above. `src/astro/` gains a link dependency,
where this record said none. **"IAU 2006 precession with the Earth rotation
angle" does not compose as written**: the Fukushima-Williams matrix is
equinox-based and ERA is measured from the celestial intermediate origin, and
the pairing was measured at 1231" -- 0.342°, about 38 km at the equator -- out
on 2026-09-11; the body-fixed frame is ERFA's CIO-based `eraC2t06a`. And
nutation is modelled, so the "as modelled" budget above falls from <= 40" to
<= 14.1". The other budgets in the table move with it, in 0016 and the register.
The five scales, their storage, and the leap-second table that reports rather
than extrapolates all stand.
