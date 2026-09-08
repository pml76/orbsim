# M1-45 — Atmosphere over the planet

Phase: D | Status: not started
Prerequisites: M1-17, M1-32, M1-43, M1-44

## Purpose

Phase D's stated acceptance criterion: *"the limb, the terminator and a sunrise
from orbit look right, with the scattering computed in physical units and the
exposure doing the work that hand-tuned constants used to do."*

Everything needed exists; this assembles it and puts the frames in front of the
owner.

## What to implement

- The frame's composition order, written down in one place because getting it
  wrong is subtle: surface radiance → attenuated by aerial perspective and
  added to its in-scatter → sky radiance from the sky-view table where no
  surface was hit → the sun disc where the view ray meets it → exposure →
  AgX → sRGB. Each stage is in radiance until the exposure stage, and the code
  is structured so that is checkable rather than asserted.
- **The night-side blend from M1-35 is revisited**, as that task promised: real
  twilight now comes out of the scattering, so the hand-written terminator
  ramp is removed and what remains is the emissive layer only. This is the
  provisional thing being made unprovisional.
- Four probes, each a golden:
  - `earth-limb-400km` — the limb against black, the case that fails if the
    atmosphere's apparent thickness is wrong;
  - `earth-sunrise-400km` — the terminator with the sun just below it, which is
    the hardest case for multiple scattering;
  - `ground-noon` — from the surface, sun high;
  - `ground-sunset` — from the surface, sun on the horizon, where the Mie phase
    function and ozone do the visible work.

## Out of scope

Clouds. Stars. The quadtree — the surface is still one tile on a sphere, and
that is deliberate: this phase gives phase C a correct reference image to debug
tile seams against.

## Tests

- **The limb's radiance profile** against the CPU reference along a line crossing
  it, within **5 %**, computed from the dumped HDR frame. This is the numeric
  form of "the limb looks right".
- **The atmosphere's apparent thickness** in the rendered frame matches the
  geometric prediction from the atmosphere-top radius and the camera altitude,
  within one pixel.
- **Ground-level sky radiance at the zenith at noon** agrees with the reference
  to 5 %.
- **Sunset reddening is real, not tuned**: the ratio of red to blue radiance at
  the horizon at sunset matches the reference's ratio to 5 %. A hand-tuned
  colour would pass a golden image and fail this.
- **The four probes are deterministic** — byte-identical HDR dumps across runs,
  which by now also covers four compute passes and a camera-dependent update
  rule.

## Frames to look at

All four, and this is the sign-off the phase exists for. Judge:

- **`earth-limb-400km`** — a thin blue band that fades smoothly to black, no
  hard edge at the atmosphere top, no banding in the gradient.
- **`earth-sunrise-400km`** — an orange-to-blue gradient along the terminator,
  the day side not blown out, the night side not crushed to pure black.
- **`ground-noon`** — a blue sky, brighter toward the horizon, with the sun's
  aureole visible.
- **`ground-sunset`** — red near the sun, blue overhead, and a smooth transition
  rather than a rainbow band.

The question to ask of each is not "is it pretty" but **"does anything here look
like a constant somebody chose?"** — because by construction there are none.

## Verification

The standing rules, plus `ctest -R probe_earth` and `ctest -R probe_ground`.

## Done when

- [ ] `check` green in both trees.
- [ ] All four goldens approved and committed.
- [ ] The limb profile, the apparent thickness and the sunset ratio all pass
      numerically.
- [ ] The provisional terminator blend from M1-35 is gone.
- [ ] Phase D's stated criterion is met, in physical units.
