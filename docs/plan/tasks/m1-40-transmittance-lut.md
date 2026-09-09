# M1-40 — The transmittance LUT

Phase: D | Status: not started
Prerequisites: M1-18, M1-39
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md)

## Purpose

The first of Hillaire's four tables, and the one everything else reads: how much
light survives a path from an altitude in a direction to the top of the
atmosphere. Computed once into a small 2D texture, sampled everywhere.

This is also where the probe machinery earns its keep for the second time — the
table is a texture full of numbers, so it is verified as numbers.

## What to implement

- **A compute pipeline** — the project's first — and the compute shader that
  fills a 2D RGBA16F image, default 256×64.
- **The parameterisation** from Hillaire 2020: the mapping between texture
  coordinates and (radius, view zenith cosine), which is deliberately non-linear
  so that the horizon gets the resolution it needs. The mapping and its inverse
  are implemented **in `orbsim_view` as well**, because the test needs to know
  which (r, μ) each texel represents, and because a parameterisation bug looks
  exactly like a physics bug.
- The LUT is rebuilt only when the atmosphere parameters change, not per frame,
  and the code says so.
- **A probe, `lut-transmittance`**, that dispatches the compute pass and dumps
  the raw table as `f32`, plus a false-colour PNG so the shape can be seen.

## Out of scope

Every other LUT. Anything applied to a rendered scene — the tables are built and
checked before anything uses them, so a wrong image later has one less
candidate.

## Tests

- **The parameterisation round trip** in `orbsim_view`: uv → (r, μ) → uv is the
  identity to 1e-9 across the whole domain, including both edges, where the
  mapping's derivative is worst.
- **The budget: the GPU table agrees with the M1-39 CPU reference to 1 %** at a
  grid of sample points covering low and high altitude, zenith, horizon and
  below-horizon directions. The test reads the dumped table and computes the
  reference itself.
- **Boundary behaviour**: transmittance is exactly 1 at the atmosphere top
  looking up, near zero along a long horizontal path at sea level, and in [0,1]
  everywhere — asserted over the whole table, not at samples.
- **Monotonic** in both parameters where physics says it must be.
- **Determinism**: two dispatches produce byte-identical tables. A compute
  shader with a race in it produces a table that is *nearly* right, which is the
  worst kind.

## Error budget

**1 %** relative against the CPU reference, whose own error is 1e-6 — so the
budget measures the shader. Reported as the maximum and the 99th percentile
across the sample grid, because a single bad texel and a uniformly biased table
are different bugs and one number hides that.

## Frames to look at

`lut-transmittance.png`, the false-colour table. Judge: a smooth gradient with
the horizon band clearly resolved, no banding, no discontinuity across the
middle of the image where the parameterisation switches behaviour. A stripe or a
step here is a parameterisation bug, and it is far easier to see than to derive.

## Verification

The standing rules, plus `ctest -R probe_lut-transmittance`.

## Done when

- [ ] `check` green in both trees.
- [ ] The 1 % budget holds, with max and p99 recorded in the commit message.
- [ ] The table is bit-identical across runs.
- [ ] The parameterisation exists in exactly two places — the shader and the
      test's own copy in `orbsim_view` — and they are asserted to agree.
