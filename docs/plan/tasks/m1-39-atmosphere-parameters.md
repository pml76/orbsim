# M1-39 — Parameters, and the CPU reference model

Phase: D | Status: not started
Prerequisites: M1-01, M1-09
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

Four GPU lookup tables are about to be written, and each needs something to be
checked against that did not come out of the same shader. This task writes that
something: a slow, obvious, CPU implementation of atmospheric scattering, plus
Earth's optical parameters with their sources.

The reference is the whole verification strategy for phase D. It is written
**first**, before any shader, so that no LUT is ever verified against itself.

## What to implement

`src/view/Atmosphere.hpp` / `.cpp`, in the Vulkan-free library.

- **`AtmosphereParameters`**, every field with its unit and its source in a
  comment: Rayleigh scattering coefficients at the three sampled wavelengths and
  the Rayleigh scale height; Mie scattering and absorption coefficients, scale
  height and the phase asymmetry `g`; ozone absorption and its tent-shaped
  vertical distribution; ground and atmosphere-top radii; ground albedo. The
  values are the published Earth parameters used by Hillaire (2020) and Bruneton
  (2008) — **copied with the citation**, not tuned.
- **The phase functions**: Rayleigh, and Cornette–Shanks for Mie, each with the
  normalisation that makes it integrate to one over the sphere.
- **Density profiles**: exponential for Rayleigh and Mie, the tent for ozone,
  each a named function of altitude.
- **Ray–sphere intersection** for the ground and the atmosphere top, written for
  numerical stability rather than brevity — the naive quadratic loses
  catastrophically at grazing angles, which is exactly where the limb is.
- **The reference model itself**: optical depth by numerical integration along a
  ray; transmittance as its exponential; single scattering by marching a ray and
  accumulating; and a brute-force multiple-scattering estimate by iterating
  orders. Slow by design — it is a test oracle, not a renderer.

## Out of scope

Anything on the GPU. Any LUT. Ozone chemistry, aerosol models beyond the single
Mie species, and clouds.

## Tests

`tests/test_atmosphere_reference.cpp`, headless, and every check here is against
mathematics rather than against a picture.

- **The phase functions integrate to 1** over the sphere, by numerical
  quadrature, for several values of `g`. This is an analytic property and it
  catches a missing 1/4π immediately.
- **Optical depth against a closed form**: for an exponential atmosphere and a
  vertical ray, the optical depth is `β·H·(1 − e^(−h/H))` exactly. The numerical
  integrator must match it to 1e-6 relative. That single test is what makes the
  integrator trustworthy for the cases with no closed form.
- **Ray–sphere**: against analytic solutions, including the cases that break the
  naive formula — grazing incidence, an origin inside the sphere, an origin
  exactly on the surface, and a ray pointing away.
- **Transmittance is bounded and monotonic**: always in [0,1], decreasing with
  path length, and exactly 1 for a zero-length path.
- **Energy sanity**: with absorption set to zero, scattering conserves energy to
  within the integrator's tolerance over a closed path.
- **The singularities**, per rule 5: the zenith ray, the exact horizon, a ray
  that just grazes the ground, and the atmosphere-top boundary from both sides.

## Error budget

The reference model's own numerical error is **1e-6 relative** against the
closed forms above. It has to be an order of magnitude tighter than the 1 % it
will be used to judge the GPU by, or the budget measures the oracle rather than
the shader — and this task states that explicitly.

## Verification

The standing rules. No GPU.

## Done when

- [ ] `check` green in both trees.
- [ ] Every parameter carries its published source.
- [ ] The reference model is verified against closed forms to 1e-6.
- [ ] Nothing in this task was tuned to make anything look right.
