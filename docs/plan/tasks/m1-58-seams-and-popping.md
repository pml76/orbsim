# M1-58 — Seams and popping: the frames

Phase: C | Status: not started
Prerequisites: M1-17, M1-51, M1-52
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md), [ADR 0015](../../adr/0015-skirts-and-morphing.md)

## Purpose

Phase C's acceptance criterion is *"no popping and no seams"*, and both are
visual claims that this project refuses to leave visual. This task turns each
into a measurement, then puts the frames in front of the owner as well.

The technique for seams is worth stating up front because it is unusually clean:
**render against a background colour that appears nowhere in the scene, and
assert that no background pixel appears inside the planet's silhouette.** A
crack is, by definition, background showing through — so this is not a proxy for
the property, it is the property.

## What to implement

- **Probe `terrain-seam`**: the camera positioned so that the view straddles a
  level boundary — several of them, chosen from the M1-50 selection so the
  frame is guaranteed to contain a transition rather than hoped to. Rendered
  against a magenta background that no surface, sky or night-lights value can
  produce.
- **Probe `terrain-seam-grazing`**: the same boundary at a grazing angle, where
  skirts are most likely to show as dark fringes and where a too-shallow skirt
  is most likely to leak.
- **Probe `terrain-morph`** from M1-52, extended to a longer sequence and a
  second boundary.
- The silhouette test needs the planet's outline: computed on the CPU from the
  camera and the ellipsoid, not detected from the image, so the test knows where
  "inside" is independently of what was drawn.

## Out of scope

Fixing anything these frames reveal — a defect found here becomes a task, and
that task is inserted into the queue rather than absorbed silently into this
one. The queue is the record of what was actually done.

## Tests

- **No cracks**: zero background pixels strictly inside the computed silhouette,
  in both seam probes. Not "few" — zero. A single leaking pixel is a real gap
  and will be a flickering dot in motion.
- **The test has teeth**: with skirts disabled, the same test must **fail**, and
  the failure count is recorded in the commit message. Without that, the assertion
  could be passing because the camera never actually saw a boundary — rule 23,
  and rule 19 by hand.
- **No popping**: across the morph sequence, no pixel changes by more than a
  stated bound between consecutive frames, ignoring the pixels where the sun
  disc or a specular highlight legitimately moves. The bound is derived from the
  camera step and the shading gradient rather than chosen, and the derivation is
  in the test.
- **Skirts are not visible**: in the grazing probe, the shading along a tile
  boundary differs from its neighbourhood by less than a stated amount — the
  dark-fringe failure that a too-deep skirt produces, which the crack test alone
  would happily pass.

## Frames to look at

`terrain-seam.png`, `terrain-seam-grazing.png`, and the `terrain-morph`
sequence. Judge: no magenta anywhere; no visible line where the level changes,
either as a gap or as a shading discontinuity; no frame in the sequence where
the terrain jumps. **This is the sign-off for the riskiest claim in the
milestone**, so it is worth zooming in on the boundary regions rather than
viewing the frames at fit-to-window.

## Done when

- [ ] `check` green in both trees.
- [ ] Zero background pixels inside the silhouette, in both seam probes.
- [ ] The same test fails with skirts disabled, and the number is recorded.
- [ ] The morph sequence passes the per-pixel bound.
- [ ] All frames approved by the owner.
