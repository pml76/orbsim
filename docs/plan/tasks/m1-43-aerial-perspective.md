# M1-43 — The aerial-perspective volume

Phase: D | Status: not started
Prerequisites: M1-42

## Purpose

The last of the four tables, and the one that puts atmosphere *between* the
camera and the ground rather than only above it. Without it, distant terrain is
as crisp as near terrain and the planet reads as a painted ball; with it,
mountains fade with distance because the air between is scattering light into
the view.

It matters more than it sounds for phase C: the aerial perspective is a large
part of how a viewer judges scale and distance, and it is what makes a quadtree
seam at range less visible rather than more.

## What to implement

- A 3D compute pass filling a froxel volume, default 32×32×32, covering the
  near range of the view frustum, each cell holding the in-scattered radiance
  and the transmittance from the camera to that depth.
- The depth distribution is stated and justified — the near field needs the
  resolution — and the far limit is documented, along with what happens beyond
  it: the sky-view table takes over, and the join between them must be
  continuous.
- Applied to the surface shading from M1-32: surface radiance is attenuated by
  the transmittance and the in-scattered term is added, in that order, in
  physical units.
- **A probe, `aerial-perspective`**, dumping the volume and rendering a scene
  with a long ground view.

## Out of scope

Volumetric shadows through the atmosphere (godrays). Clouds. Anything below the
surface.

## Tests

- **The limiting cases, which are exact**: at zero distance transmittance is 1
  and in-scatter is 0; at the far limit the values agree with the sky-view table
  in the same direction to within the sum of their budgets — that is the
  continuity check, and a discontinuity there is a visible band in the image.
- **The budget: 5 % against a CPU ray-march** of the same path from M1-39, at
  sample points spread across the volume, including the corners of the frustum
  where the parameterisation is worst.
- **Monotonic**: transmittance decreases with depth along every froxel column,
  asserted over the whole volume rather than at samples.
- **Energy**: in-scatter plus transmitted background never exceeds the incoming
  radiance, within the budget — a bound a factor error breaks.
- **Determinism** across dispatches.

## Error budget

**5 %** against the CPU ray-march. Continuity with the sky-view table at the far
limit within the sum of the two budgets, stated as a number.

## Frames to look at

`aerial-perspective.png`: a low view across a long stretch of ground. Judge:
distant terrain fades toward the sky colour rather than toward grey or toward
black; the fade is smooth with no visible froxel banding or slicing; there is no
bright or dark seam where the volume ends and the sky-view table takes over.
This is the frame where a wrong depth distribution is obvious and a numeric
sample grid can miss it.

## Verification

The standing rules, plus `ctest -R probe_aerial`.

## Done when

- [ ] `check` green in both trees.
- [ ] The 5 % budget and the continuity check both hold.
- [ ] The far-limit join is verified numerically, not just looked at.
- [ ] The volume is bit-identical across runs.
