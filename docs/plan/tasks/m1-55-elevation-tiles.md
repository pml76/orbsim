# M1-55 — Elevation tiles and sampling

Phase: C | Status: not started
Prerequisites: M1-27, M1-54

## Purpose

The elevation grid becomes a pyramid, in the same tile scheme as the imagery, so
that the quadtree can stream height the same way it streams colour.

## What to implement

- **The tile format, and its numbers.** 16-bit unsigned with a **global** scale
  and offset covering −11,000 m to +9,000 m: 20,000 m over 65,536 steps is
  **0.31 m** per step, comfortably inside the 1 m budget. Global rather than
  per-tile, deliberately — a per-tile range would quantise two neighbouring
  tiles differently and put a visible step along every shared edge.
- **259 × 259 samples per tile**, adopting Orbiter's convention: columns 1 to
  257 are the tile's own edges inclusive, and columns 0 and 258 are a one-cell
  border shared with the neighbours, existing so that normals and slopes can be
  computed at the edge without needing the neighbouring tile to be resident.
  Adopting their layout also makes M1-57 a copy rather than a resample.
- **Border values must agree between neighbours**, exactly. The generator writes
  them from the same source samples, so agreement is structural rather than
  hoped for, and the test asserts it.
- Written through M1-27's KTX2 writer as a single-channel 16-bit format, and
  loaded through the same `TileSource`, cache and loader as imagery — with the
  `TileLayer` enum from M1-35 selecting it.
- **Bilinear sampling** on the CPU in `orbsim_view`, matching what the shader
  will do in M1-56, so a test can assert what the GPU should produce.

## Out of scope

Displacement and normals (M1-56). Orbiter's ELEV import (M1-57). Compression:
BC formats are for colour, and quantising terrain through a block compressor is
how terracing appears.

## Tests

`tests/test_elevation_tiles.cpp`, headless.

- **The budget: a sampled height is within 1 m of the source grid's bilinear
  value**, over a seeded sweep of positions, including inside every ocean and on
  every continent. The quantisation floor is 0.31 m, so the budget measures the
  pipeline rather than the format — and the test says so.
- **Border agreement**: for every pair of adjacent tiles at several levels, the
  shared border columns and rows are **bit-identical**. A mismatch here is a
  visible ridge along every tile edge later, and it is far cheaper to catch now.
- **The pole and the seam**, per rule 5: tiles at ±90° latitude and the pair
  straddling ±180° longitude have consistent borders too, which the general case
  does not cover because their neighbours are found differently.
- **Range**: no sample exceeds the global scale's range, and the extremes of the
  dataset survive the round trip through quantisation within 0.31 m.
- **Determinism**: two generations produce byte-identical tiles.

## Error budget

**1 m** between a sampled height and the source grid's bilinear value, with the
0.31 m quantisation floor stated. Border columns **exactly** equal between
neighbours — no tolerance, because the values come from the same source samples
and anything else is a bug.

## Verification

The standing rules, plus a generation over the full ETOPO grid with the level
depth, tile count and total size recorded.

## Done when

- [ ] `check` green in both trees.
- [ ] The 1 m budget holds across the sweep.
- [ ] Borders are bit-identical, poles and the date line included.
- [ ] The scale and offset are global constants with their derivation in a
      comment.
