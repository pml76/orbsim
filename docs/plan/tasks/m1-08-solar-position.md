# M1-08 — Solar position

Phase: A | Status: not started
Prerequisites: M1-05, M1-06, M1-07
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md)

## Purpose

Everything about the image depends on where the Sun is: the terminator, the
limb, the length of the shadows, and — through the inverse-square law — how much
light there is to expose for. This is the one piece of ephemeris milestone 1
needs, and it is deliberately the cheapest possible one.

**It lights the scene; it does not pull on anything.** The force model in phase E
is Earth point mass and J2 only.

## What to implement

`src/astro/Sun.hpp` / `.cpp`.

- The **low-precision solar coordinates** from the *Astronomical Almanac*
  (mean longitude, mean anomaly, ecliptic longitude with two equation-of-centre
  terms, the obliquity of date, and the radius vector in AU), with the formula
  and its stated validity — about 0.01° over 1950–2050 — quoted in the comment.
- **Referred to the equinox of date, then rotated into ICRF** using M1-07's
  precession. This is why M1-07 comes first, and the header says so, because a
  reader who assumes the formula already gives J2000 is wrong by 0.36° at a 2026
  epoch — which is 36 times the formula's own error.
- `[[nodiscard]] Vec3 geocentricSunPosition(TdbTime)` in **metres**, ICRF, and
  `[[nodiscard]] Metres sunDistance(TdbTime)`.
- `Irradiance` (W·m⁻²) joins `core/Units.hpp`, and
  `[[nodiscard]] Irradiance solarIrradianceAt(Metres distance)` implements the
  inverse-square law from the total solar irradiance at 1 AU. The constant is
  **1361 W·m⁻²** and the comment cites the measurement it comes from rather than
  calling it well known.

## Out of scope

The Moon. Planets. DE440, VSOP87, and any series with more than a handful of
terms. Light-time correction and aberration — both below the 0.01° budget.
Anything gravitational.

## Tests

`tests/test_sun.cpp`, against the M1-06 Horizons fixture.

- **Direction within 0.01°** and **distance within 2e-4 AU** at all ~40 fixture
  epochs across 2000–2050. This is the budget, and Horizons is data this project
  did not produce.
- **Perihelion and aphelion**: the annual minimum and maximum distances land at
  0.9833 AU and 1.0167 AU within budget, and near the right dates — a physical
  check the fixture rows alone would not force.
- **The March equinox**: the Sun's declination crosses zero within 0.01° of the
  published instant. A sign error in the obliquity rotation fails this and
  passes several other tests, which is why it is here.
- **Irradiance**: 1361 W·m⁻² at exactly 1 AU, and the 1/r² falloff checked at
  0.5 AU and 2 AU against hand-computed values.
- **Continuity**: no jump larger than the per-step motion across a year, swept —
  a wrapped angle handled wrongly shows up here as a discontinuity.

## Error budget

**Direction 0.01°, distance 2e-4 AU** against JPL Horizons over 2000–2050,
asserted. The distance budget is 0.04 % of irradiance, which is well below the
0.5 % radiometric budget in M1-18, so this cannot be what makes that one fail.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] Both budgets asserted against the Horizons fixture, not against a claim.
- [ ] The frame the result is expressed in is stated in the header, and the
      of-date-to-ICRF rotation is applied and tested.
- [ ] The solar constant carries its source.
