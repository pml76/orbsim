# M1-05 — TDB and UT1

Phase: A | Status: not started
Prerequisites: M1-04

## Purpose

The last two scales, and the two that are not offsets. TDB is what solar-system
ephemerides are tabulated in; it differs from TT by a periodic term of about
1.7 ms amplitude, driven by Earth's motion in the Sun's gravity well. UT1
measures the Earth's actual rotation and wanders against UTC by up to 0.9 s,
which is what makes it the scale the body-fixed frame in M1-08 needs.

## What to implement

In `src/core/Time.hpp` / `.cpp`.

- **TDB − TT**, by the standard periodic approximation from the *Explanatory
  Supplement to the Astronomical Almanac*, with the formula, its source and its
  claimed accuracy written in the comment above it. The argument is Earth's mean
  anomaly `g = 357.53° + 0.9856003° × (JD_TT − 2451545.0)`, and the leading terms
  are `0.001658 s × sin g` and a small second harmonic. Both directions:
  `tdbFromTt`, `ttFromTdb`. The inverse is the same series evaluated at TT,
  which is correct to far better than the budget because the term is tiny — and
  the comment says that rather than leaving a reader to wonder.
- **UT1** as `ut1FromUtc(UtcTime, DeltaUt1)` and back, with a `DeltaUt1` strong
  type in seconds. **The default is zero**, and the header states the resulting
  model error plainly: ΔUT1 stays inside ±0.9 s by construction, which is
  ≤ 13.5″ of Earth rotation, about 420 m at the equator. The parameter exists so
  that an IERS series can be supplied later without changing a signature.
- `describe()` gains any new `TimeError` values.

## Out of scope

The full Fairhead–Bretagnon series — the approximation is inside budget by more
than an order of magnitude and the budget says so. Reading IERS EOP files.
Relativistic scale factors between TDB and TCB. Nutation, which is M1-08's
concern and is deferred there too.

## Tests

Extends `tests/test_time.cpp`.

- **TDB − TT against reference values** from an implementation this project did
  not write, committed through the M1-06 fixture mechanism with its provenance
  recorded. Assert **≤ 100 µs**. If no independent reference can be obtained,
  **stop and raise it** rather than falling back on the paper's claimed
  accuracy: `VERIFICATION.md` rule 3 exists for exactly this substitution.
- **Amplitude and period**: the term's peak magnitude is about 1.7 ms and its
  dominant period is one year. Sampled across two years, the extrema and the
  zero crossings land where the physics says, which is a check the fixture
  cannot give and a wrong sign would fail.
- **Round trip** TT → TDB → TT to 1e-9 s over a seeded sweep, 1990–2050.
- **UT1**: with ΔUT1 = 0, UT1 equals UTC exactly; with ΔUT1 = 0.3 s it differs
  by exactly that; the sign convention is stated in a comment and asserted
  against it (UT1 = UTC + ΔUT1).
- **The type system**: TDB and TT still do not interconvert implicitly.

## Error budget

TDB − TT within **100 µs** of the reference series — justified because 100 µs is
3 m of Earth's orbital motion, far below anything this milestone claims.
ΔUT1 = 0 is a **model error of ≤ 0.9 s**, recorded, not asserted.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The 100 µs budget is asserted against an external reference, not a claim.
- [ ] The ΔUT1 = 0 model error appears in the header, in the ADR, and in the
      commit message.
- [ ] All five scales exist, and none converts to another by accident.
