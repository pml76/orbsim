# M1-50 — The quadtree and its error metric

Phase: C | Status: not started
Prerequisites: M1-12, M1-24, M1-49

## Purpose

The heart of the riskiest phase, and — deliberately — a pure function. Given a
camera, a screen size and an error threshold, which tiles should be drawn? No
GPU, no files, no threads: a decision that can be tested exhaustively on the
CPU, which is the only reason the rest of phase C is tractable.

The milestone plan is blunt about why this matters: *"screen-space error
metrics, cracks and cache eviction under motion are where planet renderers
consume their schedule."*

## What to implement

`src/view/Quadtree.hpp` / `.cpp`.

- **The geometric error of a tile**: the maximum distance between the tile's
  tessellated mesh and the true surface it approximates. For a grid of a given
  resolution on an ellipsoid, that is the sag of the chord, and it has a closed
  form worth deriving in a comment — it halves with each level, and the code
  should make that visible rather than magical.
- **Screen-space error**: geometric error projected through the camera, in
  `Pixels`. The formula is stated with its derivation, since it is the single
  number that decides everything the phase does.
- **Selection**: descend from the level-4 roots, subdividing while the
  screen-space error exceeds the threshold, bounded by a maximum level. Returns
  a list of tiles that **exactly covers the visible surface, without overlap** —
  a parent is either drawn or replaced by its four children, never both.
- **Culling**, in two forms: the view frustum, and the horizon — a tile entirely
  over the horizon cannot be seen, and on a planet that is most of them. The
  horizon test uses the ellipsoid from M1-49 and is conservative: it may keep a
  tile that is not visible, but it must never cull one that is.
- The threshold and the maximum level arrive as parameters now and become
  `RenderQuality` fields in M1-59.

## Out of scope

Meshes (M1-51), morphing (M1-52), eviction policy (M1-53), and anything drawn.
Occlusion culling by terrain.

## Tests

`tests/test_quadtree.cpp`, headless — and this suite is the one that decides
whether phase C is debuggable.

- **The error metric against hand-computed values**: sag for a known tile at a
  known level, and the projection to pixels for a known camera, both worked out
  independently in the test.
- **Halving**: geometric error halves with each level, to 1e-12, across levels 4
  to 20.
- **Coverage without overlap**: for a sweep of camera positions and altitudes,
  the selected set covers the visible surface exactly — checked by sampling
  points on the ellipsoid and asserting each lands in exactly one selected tile.
  This is the crack-free property at the level of *selection*, before geometry
  exists to have cracks.
- **The threshold is respected**: every selected tile's screen-space error is
  below the threshold, and every selected tile's *parent* would have exceeded
  it — so the selection is neither too coarse nor gratuitously fine.
- **Horizon culling is conservative**: at the limb, a tile that is partly
  visible is kept. Tested by sampling the tile's corners and its centre against
  the horizon plane, including the awkward case where all corners are over the
  horizon and the middle is not.
- **Bounded**: the selected count never exceeds a stated bound for a given
  threshold, at any altitude from 36,000 km to 1 km. An unbounded selection is
  how a quadtree becomes a slide show, and this is the assertion that prevents
  it.
- **Determinism**: the same camera gives the same list, in the same order.

## Error budget

Every selected tile's screen-space error **≤ the configured threshold**,
asserted over the sweep. The bound on selected tile count is stated as a number
per threshold and recorded.

## Verification

The standing rules. Entirely headless.

## Done when

- [ ] `check` green in both trees.
- [ ] Coverage-without-overlap holds across the camera sweep.
- [ ] The tile-count bound is measured and written down.
- [ ] The error metric's derivation is in a comment, not just its result.
