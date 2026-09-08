# M1-44 — The Sun as a disc

Phase: D | Status: not started
Prerequisites: M1-08, M1-15

## Purpose

A point light with a direction is wrong in a specific, visible way: the Sun
subtends about half a degree from Earth, and that finite size is what gives the
terminator its soft edge and what makes a penumbra exist at all. It is also the
cheapest correctness win in the whole phase — the geometry is a solid angle and
the radiance follows from the irradiance already computed in M1-08.

## What to implement

`src/view/SunDisc.hpp`, plus its use in the sky shader.

- **Angular radius from distance**: `α = asin(R☉ / d)`, with the IAU nominal
  solar radius as a named constant carrying its source. At 1 AU that is 0.2665°,
  a disc of 0.533° — and it changes with the season, which the code gets for
  free by taking `d` from M1-08 rather than hard-coding an angle.
- **Radiance from irradiance**, which is the whole physical content of this
  task: the disc's solid angle is `Ω = 2π(1 − cos α)`, so its mean radiance is
  `L = E / Ω`. At 1 AU that is about 2.0e7 W·m⁻²·sr⁻¹ — a number worth putting
  in a comment, because it is the largest value the HDR pipeline will ever carry
  and it is the reason the pipeline is HDR.
- **Limb darkening** by the standard quadratic law with published coefficients,
  **renormalised so the total flux is unchanged**. Darkening the limb without
  renormalising quietly dims the Sun by several per cent, and that error would
  then propagate into everything the Sun lights.
- A probe, `sun-disc`, viewing the Sun directly from orbit.

## Out of scope

Eclipse geometry, umbra and penumbra on the vessel — shared with the solar
radiation pressure shadow model and deferred with it. Sunspots. Solar limb
reddening as a function of wavelength beyond the single coefficient pair.
Lens flare, which is a camera artefact and not radiometry.

## Tests

`tests/test_sun_disc.cpp`, headless where it can be.

- **Flux is conserved, and this is the test that matters**: integrating the
  disc's radiance — with limb darkening — over its solid angle recovers the
  irradiance from M1-08 to **0.1 %**. A missing renormalisation, a wrong solid
  angle or a factor of two in the half-angle all fail here.
- **Angular size against published values**: 0.5334° mean diameter, 0.5422° at
  perihelion and 0.5243° at aphelion, each within the budget of M1-08's distance
  accuracy — the numbers come from a reference, not from our formula.
- **The small-angle trap**: `2π(1 − cos α)` loses precision catastrophically at
  this angle in `f32`; the implementation uses a stable form and the test
  asserts agreement with the exact value to 1e-9 relative. This is
  `VERIFICATION.md` rule 9 arriving in a shader.
- **The terminator softens**: the width of the penumbra on a sphere matches the
  angular diameter, computed analytically in the test.
- The probe's readback shows the disc's peak radiance within 1 % of `E/Ω`.

## Frames to look at

`sun-disc.png`. Judge: a round disc with a visibly darker limb, not a clipped
white blob and not a square; the surrounding sky is not haloed by a filtering
artefact. Because this is the brightest thing the renderer will ever draw, it is
also the best test of whether exposure and AgX are behaving — a highlight that
goes flat white or shifts hue is a tonemap problem, not a Sun problem.

## Verification

The standing rules, plus `ctest -R probe_sun-disc`.

## Done when

- [ ] `check` green in both trees.
- [ ] Flux conservation holds to 0.1 % with limb darkening enabled.
- [ ] The angular diameter matches published values across the year.
- [ ] The solar radius and the limb-darkening coefficients carry their sources.
