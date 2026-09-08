# M1-30 — The pyramid

Phase: B | Status: not started
Prerequisites: M1-24, M1-29

## Purpose

One level of tiles is not a pyramid. This generates every level from the
quadtree root down to the deepest the source resolution supports, plus the mip
chain inside each tile, and makes the result loadable.

## What to implement

- `tilegen --levels 4..N`, generating every tile at every level. **Each level is
  resampled from the source image directly**, not downsampled from the level
  below: it costs more time in a tool that runs offline, and it avoids
  accumulating filter error down eight levels.
- The **maximum useful level** is computed from the source resolution and
  printed, rather than being a number the operator guesses. The 21600×10800 Blue
  Marble image supports a specific deepest level, and the tool says which.
- **Mip generation uses the same linear-light filtering** M1-28 established for
  resampling: decode to linear, filter, re-encode to sRGB. Every level and every
  mip goes through one filter path, not two.
- A mip chain per tile, down to the 4×4 block floor, filtered the same way.
- **`KtxPyramidSource`** joins the `TileSource` variant: it maps a `TileId` to a
  path, reads through M1-26, and reports `NotPresent` for a gap rather than
  inventing a tile. Orbiter's own format permits gaps, and so does ours.

## Out of scope

Interpolating a missing tile from an ancestor — Orbiter does this and we may
later; for now a gap is a gap and is reported. Elevation (M1-54). Threads
(M1-33).

## Tests

`tests/test_pyramid.cpp`, on a small synthetic source so the whole pyramid fits
in a test.

- **Completeness**: every tile at every generated level exists and reads back.
- **Parent–child consistency**, which is the real check: a parent tile's pixels
  agree with the average of its four children to within a stated tolerance.
  Both were resampled independently from the source, so agreement is evidence;
  a mis-indexed child fails this immediately.
- **Determinism**: two full runs produce byte-identical files, across the whole
  pyramid. This is the claim the tool's usefulness rests on.
- **Linear filtering is actually happening**: a source that alternates black and
  white pixels averages to the linear mid-grey (about 188/255 in sRGB), not to
  128. That single assertion is what stops the gamma bug this task exists to
  avoid, and it fails loudly if somebody "simplifies" the conversion away.
- **Mip chain**: each level halves, the smallest is 4×4, and every mip reads
  back through M1-26.
- A gap in the pyramid reports `NotPresent` and does not fall back to an
  ancestor.

## Verification

The standing rules, plus a full generation over the 21600×10800 Blue Marble
image, timed, with the deepest level and total size recorded in the commit
message.

## Done when

- [ ] `check` green in both trees.
- [ ] The parent–child consistency test passes across a whole synthetic pyramid.
- [ ] Two runs are byte-identical.
- [ ] The linear-filtering assertion exists and has been seen to fail with the
      conversion removed.
- [ ] The real pyramid is generated, and its level count and size recorded.
