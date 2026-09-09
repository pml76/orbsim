# M1-57 — Orbiter ELEV extraction

Phase: C | Status: not started
Prerequisites: M1-36, M1-55
Decided by: [ADR 0010](../../adr/0010-tiles-are-ktx2.md)

## Purpose

Completing the converter: Orbiter's `Elev.tree` archives become elevation tiles
in our format. It lands here rather than in phase B because the target format
had to exist first.

It is a small task with a large payoff: it is what lets an existing Orbiter
installation's terrain — including third-party high-resolution add-ons for
specific regions — be flown in this simulator.

## What to implement

- The `.elv` format, specified in `PLANETS.tex` section `sssec:elev_tile_format`
  and implemented in the MIT-licensed `elv_io.cpp` in the reference clone. Read
  the specification first: a header followed by a **259 × 259 grid**, where
  columns 1 to 257 are the tile edges inclusive and columns 0 and 258 are
  padding shared with the neighbours.
- **The layout matches ours exactly** — that is why M1-55 adopted it — so the
  conversion is a scale-and-offset requantisation into our global range, not a
  resample. Where Orbiter's per-tile scale and offset differ from ours, the
  conversion is one multiply and one add per sample, and the test asserts the
  round trip stays inside the quantisation floor.
- `treeconv --layer elev`, writing through M1-55's tile writer.
- **Refusals by name**: a header that is not the documented size, a grid that is
  not 259 × 259, a data length that contradicts the header, and an elevation
  value outside our global range — the last reported rather than clamped, since
  a clamp would silently flatten a mountain.
- The `.elv` parser sits behind `fuzz_ztree`, like the DDS parser, because it is
  reached through the same archive.

## Out of scope

`Elev_mod`, Orbiter's elevation-modification layer, which exists to flatten
runways at surface bases and has no meaning until this project has bases.
Merging Orbiter elevation with ETOPO — one source at a time, chosen explicitly.

## Tests

`tests/test_elev_convert.cpp`.

- **Synthetic `.elv` files** built in the test from the specification: a
  constant grid, a ramp, extremes at both ends of the range, and the padding
  distinct from the interior so a border mix-up is visible.
- **Requantisation stays inside the floor**: converting a value into our global
  scale and reading it back differs by no more than 0.31 m.
- **Padding maps to padding**: Orbiter's columns 0 and 258 become ours, checked
  cell by cell. An off-by-one here puts a one-cell shift into every tile, which
  looks like a mysterious seam rather than an index error.
- **Out-of-range values are reported**, not clamped.
- **A real `Elev.tree`, if one is available**: convert it, and compare a tile
  against ETOPO at the same location — they will differ, since they are
  different datasets, but the correlation should be high and the coastlines
  should coincide. Skipped **loudly** if no archive is present.

## Verification

The standing rules, plus `fuzz_ztree` re-run with the `.elv` parser behind it.

## Done when

- [ ] `check` green in both trees.
- [ ] The 259 × 259 padding convention is preserved exactly.
- [ ] Out-of-range elevations are reported by name.
- [ ] Either a real archive was converted, or the gap is recorded in the commit
      message.
