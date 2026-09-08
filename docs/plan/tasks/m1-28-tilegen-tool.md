# M1-28 — The `tilegen` tool

Phase: B | Status: not started
Prerequisites: M1-27

## Purpose

Blue Marble ships as a handful of enormous equirectangular JPEGs; a quadtree
needs tiles. This is the offline tool that turns one into the other, and it is
where `stb_image` earns the pin M1-16 already made for `stb_image_write`.

## What to implement

- **`tools/tilegen/`**, a new executable target carrying the same warning set,
  the same clang-tidy configuration and the same formatting as everything else.
  A tool that is exempt from the project's bar is a tool that will grow bugs
  where nobody is looking.
- Decode the source image with `stb_image` (already pinned; dual MIT /
  Unlicense). A decode failure is reported with the file name and the library's
  own reason.
- **Colour handling, stated once and stated plainly.** Blue Marble is
  sRGB-encoded 8-bit and is stored that way: tiles are written in an sRGB
  format, so the GPU decodes to linear when sampling and the shader receives a
  linear albedo. But **filtering happens in linear light** — decode to linear,
  box filter, re-encode to sRGB — because averaging sRGB values directly
  darkens every edge, visibly along coastlines. That decode/encode pair is the
  only colour transform the tool performs, and the header says so, so that
  nobody later "simplifies" it away.
- Resample to the tiles of a chosen level with a box filter over the exact
  source pixel range each tile covers, computed from `TileId`'s bounds. The
  mapping from tile bounds to source pixels is arithmetic and belongs in
  `orbsim_view` where it can be tested, not in the tool.
- Write each tile through M1-27's writer, as **uncompressed RGBA8 for now** —
  BC7 arrives in M1-29 as its own task, so that "is it the compressor or the
  sampler?" is never a question anybody has to ask.
- `tilegen --input <file> --output <dir> --layer surf --level N`, with the
  output directory laid out by `TileId` exactly as Orbiter lays it out:
  `<dir>/<layer>/<NN>/<NNNNNN>/<NNNNNN>.ktx2`.
- The tool prints what it did — tiles written, source pixels per tile, elapsed
  time — because the first question about a pyramid is always whether it used
  the resolution you thought.

## Out of scope

Compression (M1-29). The full pyramid across levels (M1-30). Elevation (M1-54).
Any download: the source imagery is fetched by hand per
`data/textures/README.md` and is gitignored.

## Tests

`tests/test_tile_resample.cpp`, headless, on the pure arithmetic:

- **The pixel range for a tile** matches the bounds from `TileId`, computed
  independently in the test from the equirectangular projection — for the level-4
  roots, for the specification's worked example [8,5,7], and across a seeded
  sweep.
- **The box filter**: a constant source gives a constant tile; a source with a
  known linear ramp gives the exact average per tile, computed by hand.
- **Seams**: adjacent tiles cover adjacent source pixel ranges with no gap and
  no overlap — asserted by summing the ranges across a whole level and comparing
  against the source width.
- **The wrap at 180°**: the westernmost and easternmost tiles do not share a
  column, and together the level covers exactly the full width once.
- **Determinism**: two runs over the same input produce byte-identical files.

Then, end to end, on a tiny committed synthetic source image (not Blue Marble,
which is gitignored): the tool produces the expected file tree and every file
reads back through M1-26.

## Verification

The standing rules, plus one run over the real 5400×2700 Blue Marble image with
the output inspected — one tile opened in an image viewer and confirmed to be
the part of Earth its index claims.

## Done when

- [ ] `check` green in both trees.
- [ ] `tilegen` builds under the full warning set with zero findings.
- [ ] A tile at a known index visibly contains the right part of Earth.
- [ ] `THIRD_PARTY.md` records `stb_image`'s use alongside `stb_image_write`.
