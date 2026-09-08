# M1-41 — The multiple-scattering LUT

Phase: D | Status: not started
Prerequisites: M1-40

## Purpose

Single scattering alone gives a sky that is too dark and a terminator that is
too abrupt: most of the blue overhead at noon has bounced more than once.
Hillaire's contribution is an approximation that captures the higher orders in a
small table rather than a high-dimensional one, and it is what makes the
technique cheap enough to run every frame.

## What to implement

- A second compute pass filling a small 2D table, default 32×32, parameterised
  by altitude and sun zenith angle.
- The isotropic multiple-scattering approximation from Hillaire (2020) —
  implemented from the paper, with the section referenced in the shader comment,
  because this is the part of the technique that is least obvious from the code.
- It reads the transmittance table from M1-40, so the two are now a chain: an
  error in the first shows up here amplified, which is an argument for checking
  them in this order rather than together.
- **A probe, `lut-multiscatter`**, dumping raw values and a false-colour image.

## Out of scope

Sky-view (M1-42) and aerial perspective (M1-43). Anisotropic multiple
scattering, which is the accuracy this approximation trades away deliberately —
and the trade is written down here so nobody later reports it as a bug.

## Tests

- **The budget: 5 % against the CPU brute-force reference** from M1-39, which
  computes multiple scattering by iterating orders until convergence. Five per
  cent rather than one, because the approximation is *deliberately* an
  approximation — the number bounds the modelling error, and the commit message
  says which part of the discrepancy is method and which is implementation.
- **Convergence of the oracle itself is checked first**: the reference's order
  iteration is run to two different depths and the difference is shown to be
  well under the budget, or the budget is measuring the oracle.
- **Physical bounds**: the result is non-negative everywhere, larger at low
  altitude than at high, and larger for a high sun than a low one.
- **The limiting case**: with scattering coefficients set to zero, the table is
  identically zero. With absorption zero and a fully scattering atmosphere, the
  energy in the table is bounded by the energy entering — a conservation check
  that a factor-of-two error fails and a 5 % error does not.
- **Determinism** across dispatches, byte for byte.

## Error budget

**5 %** against the brute-force reference, reported as maximum and p99. The
reference's own convergence is verified to be at least an order of magnitude
tighter.

## Frames to look at

`lut-multiscatter.png`. Judge: smooth in both directions, brightest where the
physics says — low altitude, high sun — and no visible seam or clamp at the
edges. The table is small enough that a single wrong texel is visible, which is
the point of looking at it as well as measuring it.

## Verification

The standing rules, plus `ctest -R probe_lut-multiscatter`.

## Done when

- [ ] `check` green in both trees.
- [ ] The 5 % budget holds against a reference that was itself shown to have
      converged.
- [ ] The method's own approximation is documented as a modelling choice, with
      its size.
- [ ] The table is bit-identical across runs.
