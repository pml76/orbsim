# M1-76 — Drawing the track, and apsis markers

Phase: F | Status: not started
Prerequisites: M1-19, M1-71, M1-75
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

The orbit track: the first thing in this project that draws the *simulation*
rather than the world. It is also the debugging instrument the rest of the
milestone would have benefited from — an orbit that is wrong is much easier to
see as a curve than as six numbers.

## What to implement

- Each frame: take the interpolated simulation state from M1-71, convert it to
  osculating elements with `elementsFromState`, sample it with M1-75, and push
  the points into a `LineBatch` through `toRenderSpace`. The elements are
  recomputed **every frame**, which is what makes the track precess in phase
  F's acceptance criterion rather than sit still.
- **Apsis markers**: periapsis and apoapsis drawn as small screen-facing crosses
  at the radii `orbitInfo` reports, in a distinct colour, with apoapsis omitted
  — not drawn at infinity — for an unbound orbit.
- Depth handling stated: the track is drawn with depth testing on so the far
  half is occluded by the planet, which is what makes it read as a
  three-dimensional curve rather than an overlay. Reverse-Z, so the comparison
  is `GREATER`.
- The sample count is a constant now; if it ever becomes a quality setting it
  belongs in `RenderQuality`, and the comment says so.

## Out of scope

The precession measurement (M1-77) — this task draws, the next measures. Ground
tracks. Predicted future tracks under a manoeuvre. Colour-coding by time.

## Tests

- **The projected track matches the analytic conic**: the rendered polyline's
  points, projected on the CPU from the elements and the camera, land within
  **1 px** of where the frame draws them, sampled at 32 points around the orbit.
  That is the numeric form of "the ellipse is right", and it catches a
  camera-relative error a golden image would show only as a slightly wrong
  curve.
- **Apsis markers are at the right radii**, checked against `orbitInfo` rather
  than against the drawn geometry.
- **A hyperbolic state draws an open curve** with no apoapsis marker and no NaN
  — the case that would otherwise put a vertex at infinity and take the whole
  batch with it.
- **Occlusion is correct**: the far half of the track is hidden behind the
  planet, asserted by sampling the frame at points the CPU says should be
  occluded. A reversed depth comparison passes every other test here and fails
  this one.
- **Probe `orbit-track`**, with a golden: a 400 km orbit seen from outside,
  planet lit, track drawn, markers visible.

## Frames to look at

`orbit-track.png`. Judge: the ellipse is smooth rather than faceted; it passes
behind the planet and reappears; the periapsis marker is at the low point and
the apoapsis at the high one; the track sits *on* the orbit rather than floating
beside it, which is the artefact a camera-relative error produces and which is
much easier to see than to measure.

## Verification

The standing rules, plus `ctest -R probe_orbit-track`.

## Done when

- [ ] `check` green in both trees.
- [ ] The 1 px projection agreement holds around the whole orbit.
- [ ] Occlusion is asserted, not just observed.
- [ ] `orbit-track.png` approved and committed.
