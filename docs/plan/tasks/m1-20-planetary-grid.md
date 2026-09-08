# M1-20 — The planetary grid, and the jitter budget

Phase: A | Status: not started
Prerequisites: M1-17, M1-19

## Purpose

Phase A's stated acceptance criterion, in one task: *"a grid drawn at Earth
radius shows no vertex jitter as the camera moves — which is the thing
camera-relative rendering exists to prevent — and that grid is lit through the
radiometric chain end to end."*

The numeric half of that claim was proved on the CPU in M1-11, and the
radiometric half in M1-18. This is where both become something a person can look
at, which is the other half of how this project verifies rendering.

## What to implement

- A latitude/longitude wireframe at Earth's equatorial radius, generated in
  `orbsim_view` — parallels every 10°, meridians every 15°, with the equator and
  the prime meridian in a distinct colour so orientation is unambiguous. A
  sphere for now; the WGS-84 ellipsoid arrives in M1-49 and this grid is the
  first thing that will show the difference.
- The grid is oriented by **M1-07's body-fixed rotation** at the probe's fixed
  epoch, so the prime meridian is where the epoch says it is rather than
  wherever the mesh generator started. That is what makes this grid a check of
  the frame work and not just of the line renderer.
- Two probes:
  - **`grid-400km`**: camera at 400 km altitude looking at the limb, one frame,
    with a golden.
  - **`grid-jitter`**: five frames written as
    `grid-jitter.0.png` … `grid-jitter.4.png`, with the camera translating by
    0.25 m between frames — a distance that quantises visibly under naive `f32`
    narrowing and must be invisible with the subtraction done first.

## Out of scope

Anything shaded — the grid is lines. Textures, tiles, atmosphere. The ellipsoid.
Camera controls, which are the next task; these probes use fixed poses.

## Tests

- The grid generator is a pure function and is tested headlessly: vertex counts
  for a given spacing, every vertex within 1e-9 m of the sphere's radius, the
  equator lying in the z = 0 plane, and the prime meridian passing through the
  point the body-fixed rotation puts it at.
- `probe_grid-400km` compares against its golden.
- **The jitter claim is asserted numerically, not by eye**: the five
  `grid-jitter` HDR dumps are read by a Catch2 test which locates the rendered
  position of a marked grid vertex by centroid in each frame, and asserts the
  frame-to-frame movement matches the analytic projected motion to **≤ 0.05 px**
  — the same budget as M1-11, now measured through the real GPU path rather
  than a CPU model of it.

## Error budget

**≤ 0.05 px** at 1920×1080 between consecutive frames for a fixed world point at
Earth radius. Same number as M1-11; this is the end-to-end confirmation of it.

## Frames to look at

`grid-400km.png`, and the five `grid-jitter` frames in sequence. Judge: the limb
is a clean curve rather than a polygon at this altitude; the grid does not swim,
crawl or shimmer between the five frames; the equator and prime meridian are
where they should be for the stated epoch. **This is the sign-off that closes
phase A's headline claim**, so it is worth flipping between the five frames
rather than glancing at one.

## Verification

The standing rules, plus `ctest -R probe_grid`.

## Done when

- [ ] `check` green in both trees.
- [ ] The 0.05 px budget is asserted from the rendered frames, not only on the
      CPU.
- [ ] `grid-400km.png` approved and committed as a golden.
- [ ] The owner has flipped through the jitter sequence and confirmed it is
      still.
