# M1-08 — Solar position

Phase: A | Status: not started
Prerequisites: M1-05, M1-06, M1-07
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

**Amended 2026-09-11** (decisions 27–29): the Sun comes from ERFA's
`eraEpv00` rather than the *Astronomical Almanac*'s low-precision formula, and
the budgets tighten from 0.01° and 2e-4 AU to **0.1″ and 1e-6 AU**. One
question — what the wrapper does outside 1900–2100 — is the owner's, and is
put before this task starts.

## Purpose

Everything about the image depends on where the Sun is: the terminator, the
limb, the length of the shadows, and — through the inverse-square law — how much
light there is to expose for. This is the one piece of ephemeris milestone 1
needs.

**It lights the scene; it does not pull on anything.** The force model in phase E
is Earth point mass and J2 only.

## What to implement

`src/astro/Sun.hpp` / `.cpp`.

- **The Earth's heliocentric position from ERFA's `eraEpv00`**, negated to give
  the geocentric Sun *(amended)*. It is oriented to the BCRS, which is aligned
  with ICRS, so no rotation follows. Its notes compare it with DE405 over
  1900–2100 at 3.7 km RMS and 11.2 km at worst — 0.016″ of solar direction —
  and the comment quotes them. *(Was: the low-precision formula, about 0.01°,
  referred to the equinox of date and rotated into ICRF through M1-07's
  precession — the step that is no longer needed.)*
- **Geometric, and the header says so** *(added)*: where the Sun is at the
  instant, with neither light-time nor aberration applied. Aberration alone
  moves the apparent Sun by 20.5″ — two hundred times this task's budget, and
  invisible on screen, where the Sun's disc is 0.53° across. The lighting uses
  the geometric Sun.
- `[[nodiscard]] Vec3 geocentricSunPosition(TdbTime)` in **metres**, ICRF, and
  `[[nodiscard]] Metres sunDistance(TdbTime)`.
- **Outside 1900–2100** `eraEpv00` returns a warning status: its stated
  accuracy lapses, and its errors roughly double by 1800 and 2200. What the
  wrapper does there — report it by name, so both functions return a
  `std::expected`, or accept the documented loss of accuracy and say so — is
  **the owner's decision, put before this task starts**. The status is never
  ignored silently either way.
- `Irradiance` (W·m⁻²) joins `core/Units.hpp`, and
  `[[nodiscard]] Irradiance solarIrradianceAt(Metres distance)` implements the
  inverse-square law from the total solar irradiance at 1 AU. The constant is
  **1361 W·m⁻²** and the comment cites the measurement it comes from rather than
  calling it well known.

## Out of scope

The Moon. Planets — ERFA's `eraPlan94` exists, and DE440 against VSOP87 is
still open. Light-time and aberration, deliberately, as above. Anything
gravitational.

## Tests

`tests/test_sun.cpp`, against the M1-06 Horizons fixture — geometric vectors,
with no light-time or aberration correction, or the comparison measures the
correction instead of the code.

- **Direction within 0.1″** and **distance within 1e-6 AU** *(amended)* at all
  ~40 fixture epochs across 2000–2050. This is the budget, and Horizons is data
  this project did not produce.
- **Perihelion and aphelion**: the annual minimum and maximum distances land at
  0.9833 AU and 1.0167 AU to within 1e-4 AU — the four decimals those published
  figures carry — and near the right dates, a physical check the fixture rows
  alone would not force. *(Amended: "within budget" would now be tighter than
  the figures themselves.)*
- **The March equinox**: at the published instant, the Sun's declination,
  referred to the true equator of date through M1-07's rotation, is within
  0.01° of zero. Aberration, which this geometric position omits, accounts for
  0.002° of it. A sign error in the rotation fails this and passes several
  other tests, which is why it is here.
- **Irradiance**: 1361 W·m⁻² at exactly 1 AU, and the 1/r² falloff checked at
  0.5 AU and 2 AU against hand-computed values.
- **Continuity**: no jump larger than the per-step motion across a year, swept —
  a wrapped angle handled wrongly shows up here as a discontinuity.
- *(Added:)* `eraEpv00`'s status is handled as the owner decided, and the test
  asks for the outcome by name.

## Error budget

**Direction 0.1″, distance 1e-6 AU** against JPL Horizons geometric vectors
over 2000–2050, asserted. `eraEpv00`'s stated worst case over 1900–2100 is
11.2 km, which is 0.016″ of direction and 7.5e-8 AU of distance, so the budgets
keep sixfold and thirteenfold headroom over the implementation's own claim. The
distance budget is 0.0002 % of irradiance, so it cannot be what makes the
0.5 % radiometric budget in M1-18 fail. *(Was 0.01° and 2e-4 AU.)*

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] Both budgets asserted against the Horizons fixture, not against a claim.
- [ ] The frame the result is expressed in, and that it is geometric, are stated
      in the header.
- [ ] The solar constant carries its source.
- [ ] The out-of-range question was answered by the owner, and the answer is
      tested by name.
