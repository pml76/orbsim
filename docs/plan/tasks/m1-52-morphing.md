# M1-52 — Morphing between levels

Phase: C | Status: not started
Prerequisites: M1-51
Decided by: [ADR 0015](../../adr/0015-skirts-and-morphing.md)

## Purpose

Skirts fix cracks; they do nothing about **popping**. When a tile subdivides,
its geometry changes shape in a single frame, and the eye is extremely good at
noticing that. Phase C's acceptance criterion says "no popping", so this is the
half of it that skirts cannot deliver.

The fix is geometric morphing: a tile's vertices start at the positions its
*parent's* tessellation would have given them, and slide to their own positions
as the camera approaches. At the moment of subdivision the child is
geometrically identical to the parent, so there is nothing to pop.

## What to implement

- Each vertex carries a **morph target**: the position it would occupy on the
  parent tile's coarser grid. For a vertex that exists in both grids the target
  is itself; for one that does not, it is the interpolation of its two parent
  neighbours. Computed in `orbsim_view`, stored in the vertex, applied in the
  vertex shader.
- **The morph factor comes from the same screen-space error** that drives
  selection — not from distance, and not from a separate curve. One metric, so
  the morph is guaranteed to finish exactly when the switch happens. A second
  curve here is how morphing systems end up popping anyway.
- The factor is clamped to [0,1] and reaches **exactly 1 at the subdivision
  threshold**, which is the assertion the whole mechanism rests on.
- Texture coordinates morph with the positions, or the imagery slides against
  the geometry.

## Out of scope

Morphing elevation, which arrives with elevation in M1-56 and must use the same
factor. Temporal blending or dithered transitions.

## Tests

Headless, and these are strong tests because the property is exact:

- **At factor 1 the child mesh equals the parent's tessellation**, vertex by
  vertex, to 1e-9 m — the identity that makes the transition invisible.
- **At factor 0 the child mesh equals its own tessellation**, bit-identical to
  M1-51's output.
- **Continuity across the switch**: sweep the camera through the subdivision
  distance in small steps and assert no vertex moves more than a stated bound
  between consecutive steps, including the step that crosses the threshold. A
  pop is exactly a violation of this, and the bound is derived from the step
  size rather than chosen.
- **The factor is monotonic** in screen-space error, and is exactly 1 at the
  threshold, asserted at the threshold value itself rather than near it.
- **Shared edges still match** between morphing neighbours at the same level —
  the property from M1-51, re-checked under morphing, because a morph factor
  that differs across an edge reopens the crack that skirts were hiding.

On the GPU:

- **Probe `terrain-morph`**, five frames across a subdivision boundary, written
  as a sequence.

## Frames to look at

The five `terrain-morph` frames, flipped through in order. Judge: the
tessellation refines smoothly with no frame where the silhouette or the shading
jumps. This is the one artefact that is far easier to see in a sequence than in
a single image, which is why the probe writes five.

## Done when

- [ ] `check` green in both trees.
- [ ] The factor-1 identity with the parent mesh is asserted exactly.
- [ ] The continuity sweep passes through the switch.
- [ ] The owner has flipped through the sequence and confirmed no pop.
