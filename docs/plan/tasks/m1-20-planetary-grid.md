# M1-20 — The planetary grid, and the jitter budget

Phase: A | Status: **done, 2026-10-04** -- code, tests, the RX 7900 XTX's golden and the documents; the RTX A2000's golden waits for the owner on the first machine (decision 331)
Prerequisites: M1-17, M1-19
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md)

**Amended 2026-10-04, when the task ran** (register decisions 319-336 and
338). Six of this document's statements did not survive measurement or were
changed by the owner's rulings, and each is corrected in place below with its
decision beside it. **Read this rather than the original if the two
disagree; the original is in the git history.** In short:

- **The budget is 0.0333 px in a probe's 1280x720 frame** -- the task's
  0.05 px at 1920x1080 as the same angle, since probes are 1280x720 by ADR
  0008 (decision 319).
- **The marked vertex is located by fitting lines, not by a centroid**
  (decision 320). Measured in simulation, the centroid of a small marker drawn
  in one-pixel lines is off by 0.5 to 1.2 px, because one pixel more or less
  moves it by about a sixth of a pixel; straight lines fitted to the equator
  and the prime meridian near their crossing and intersected reach 0.005 to
  0.02 px -- **if neither line is near a row, a column or a diagonal**, where
  the same pattern repeats every pixel (0.2 px near the axes, 0.84 px at 45
  degrees). On the card, before the test was written: within 0.013 px over 76
  steps, under half the budget, the condition the owner set.
- **"Every vertex within 1e-9 m of the sphere" cannot hold in doubles**: two
  neighbouring doubles at the Earth's radius are 9.3e-10 m apart. Measured
  worst 1.454 spacings in the body's frame and 2.570 once turned; the budgets
  are twice those (decision 328).
- **A second jitter sequence, 1 km from the crossing**, because at 400 km a
  0.25 m step moves a point 0.0003 px and the five frames are identical: the
  check passes there but cannot fail for the defect it is named after
  (decision 321, from decision 120). The jitter frames are probes of their own,
  `grid-jitter-0` to `-4` and `grid-jitter-1km-0` to `-4`, since a probe name
  cannot hold a dot (decision 322).
- **The epoch is a row of the committed Skyfield fixture, 2025-07-30 near
  06:29 TT, with that row's own UT1** (decisions 323 and 336), so the frame is
  checked against a rotation ERFA did not compute. The row first chosen,
  2026-09-29, put the prime meridian within 0.3 degrees of the celestial x
  axis, and there narrowing before subtracting jitters by 0.0016 px -- a
  defect planted that way passed. On this row it jitters by 0.43 px.
- **The far side of the grid is not drawn, and the horizon is** (decisions 325
  and 338): a wireframe is see-through, so lines are cut where they pass behind
  the planet, and the limb is drawn in blue so that where they end can be
  judged -- added at the owner's request on seeing the frame.

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
    with a golden. *(As built, decisions 324 and 335: 400 km above where the
    epoch puts 6 degrees south on the prime meridian, heading 35 degrees east
    of north, 27.5 degrees down, so the limb crosses the upper third. Fixed by
    right ascension and declination, not through the Earth's rotation, so that
    a wrong rotation moves the grid and not the camera.)*
  - **`grid-jitter`**: five frames written as
    `grid-jitter.0.png` … `grid-jitter.4.png`, with the camera translating by
    0.25 m between frames. *(As built: five probes, `grid-jitter-0` to `-4`,
    the camera stepping along its own right (decisions 322 and 332); and five
    more, `grid-jitter-1km-0` to `-4`, 1 km straight above latitude 0,
    longitude 0, turned 33 degrees (decision 321).)*

    **Amended 2026-09-22 with M1-11** ([register decision 120](../milestone-1-decisions.md)).
    This bullet said 0.25 m was "a distance that quantises visibly under naive
    `f32` narrowing". It is not, at this altitude: measured, the naive path
    lands at **5.8e-4 px** with the camera 400 km above the point and **7.9e-5
    px** at the limb, where the budget is 0.05 px — inside it by 86 times, and
    at the limb indistinguishable from doing it properly. Half a metre at a
    range of 400 km is 1.25 microradians. The probe and the budget are
    unchanged, because this is still the end-to-end confirmation of M1-11's
    number through the real GPU path; what was wrong was the claim that the
    wrong method would visibly fail here. **Where it does fail is at 1 AU (95
    times over) and at short range (8.6 times over at 1 km),** which is where
    `tests/test_camera.cpp` puts its teeth.

## Out of scope

Anything shaded — the grid is lines. Textures, tiles, atmosphere. The ellipsoid.
Camera controls, which are the next task; these probes use fixed poses.

## Tests

- The grid generator is a pure function and is tested headlessly: vertex counts
  for a given spacing, every vertex within 1e-9 m of the sphere's radius, the
  equator lying in the z = 0 plane, and the prime meridian passing through the
  point the body-fixed rotation puts it at. *(As built, `tests/test_planetary_grid.cpp`:
  the radius to measured budgets, decision 328; the prime meridian bit for bit
  to the rotation it was handed and within 0.1 mas of Skyfield -- 14 uas
  measured; the parallels and meridians at their spacing; the horizon cut and
  the horizon circle against the plane and circle worked out on paper.)*
- `probe_grid-400km` compares against its golden.
- **The jitter claim is asserted numerically, not by eye**: the five
  `grid-jitter` HDR dumps are read by a Catch2 test which locates the rendered
  position of a marked grid vertex by centroid in each frame, and asserts the
  frame-to-frame movement matches the analytic projected motion to **≤ 0.05 px**
  — the same budget as M1-11, now measured through the real GPU path rather
  than a CPU model of it. *(As built, `tests/test_probe_grid.cpp`: by line fit,
  to 0.0333 px, decisions 319 and 320; measured 0.0003 px at 400 km and at
  most 0.008 px at 1 km, where each step is 0.217 px. **Seen failing**: with
  `toRenderSpace` changed to narrow before subtracting, the 1 km steps are off
  by up to 0.428 px. The same suite holds `grid-400km`'s crossing, the equator
  at 15 degrees east, the prime meridian at 10 degrees north and three points
  of the horizon to the pixels the pinhole camera and Skyfield put them on,
  decision 330.)*

## Error budget

**≤ 0.05 px** at 1920×1080 between consecutive frames for a fixed world point at
Earth radius. Same number as M1-11; this is the end-to-end confirmation of it.
*(As built: 0.0333 px at 1280x720, the same angle, decision 319.)*

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

- [x] `check` green in both trees.
- [x] The 0.05 px budget is asserted from the rendered frames, not only on the
      CPU. *(As 0.0333 px at 1280x720, decision 319.)*
- [x] `grid-400km.png` approved and committed as a golden. *(The RX 7900
      XTX's, by the owner on 2026-10-04; the RTX A2000's at the first
      machine's next `check`, decision 331.)*
- [x] The owner has flipped through the jitter sequence and confirmed it is
      still. *(2026-10-04, decision 339: "each pic of a group looks the same";
      the movement itself -- 0.0003 px a frame at 400 km, 0.217 px at 1 km --
      is far below what an eye can judge, which is why the test measures it.)*
