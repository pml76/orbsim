# M1-37 — The `.tree` to KTX2 converter

Phase: B | Status: not started
Prerequisites: M1-27, M1-30, M1-36
Decided by: [ADR 0010](../../adr/0010-tiles-are-ktx2.md)

## Purpose

The other half of the owner's condition: an Orbiter archive becomes a pyramid
this project can load. The pleasant part is that **no pixels are recompressed**.
Orbiter's `Surf` tiles are 512×512 DXT1, DXT1 is BC1, and Vulkan reads BC1
natively — so the conversion is a repack of blocks that are already in the right
form. No transcode, no quality loss, and no encoder in the path.

## What to implement

- **`tools/treeconv/`**, a second tool beside `tilegen`, same bar.
  `treeconv --input <Surf.tree> --output <dir> [--levels a..b]`.
- For each node: inflate through M1-36, parse the **DDS header** to confirm
  DXT1, 512×512, and locate the block data, then write the blocks unchanged into
  a KTX2 file as `BC1_RGB_SRGB` through M1-27.
- **The DDS header parser is a third untrusted parser**, so it goes into
  `fuzz_ztree`'s reach rather than getting a target of its own: the fuzzer feeds
  an archive, the archive yields a blob, the blob is parsed as DDS. It refuses
  by name — bad magic, wrong header size, an unexpected pixel format, a
  `DDPF_FOURCC` that is not `DXT1`, dimensions that are not 512×512, and a block
  count that contradicts the dimensions.
- Tile identity maps **straight across**: Orbiter's `[level, ilat, ilng]` is our
  `TileId`, which is the whole reason M1-24 adopted their convention.
- **The texture cache accepts BC1 as well as BC7** — one more format in the
  upload path and in the format-support query, not a second code path.
- The tool reports what it converted: node count, levels present, gaps, bytes in
  and out.

## Out of scope

The `Elev` layer, which is M1-57 and needs an elevation tile format to exist
first. `Mask`, `Cloud`, `Label`, `Elev_mod`. Re-encoding BC1 to BC7, which would
cost quality for nothing.

## Tests

`tests/test_tree_convert.cpp`.

- **The repack is byte-identical**, which is the claim: the BC1 blocks in the
  output KTX2 equal the blocks inflated from the archive, byte for byte, for
  every node in a synthetic archive. Anything else means a transcode happened.
- **Every DDS refusal by name.**
- **Tile identity**: a node found at `[8,5,7]` in the archive is written to the
  path our layout puts `[8,5,7]` at, and reads back through `KtxPyramidSource`
  as the same tile.
- **Gaps survive**: an archive with missing nodes produces a pyramid with the
  same gaps, and no invented tiles.
- **Determinism**: two conversions produce byte-identical output.
- If a real archive is available, one converted tile is loaded and drawn by the
  `tile-earth` probe with `--source ktx-from-tree`, and the frame is compared
  against the Blue Marble golden **by eye, not by tolerance** — they are
  different imagery and will differ; what is being judged is that the geometry,
  orientation and indexing match.

## Frames to look at

If a real archive exists: `tile-earth-from-tree.png` beside `tile-earth.png`.
Judge: the same part of Earth, the same way up, the same terminator. Different
pixels are expected; a different *place* is a bug in the index mapping.

## Verification

The standing rules, plus `fuzz_ztree` re-run now that DDS parsing sits behind
it.

## Done when

- [ ] `check` green in both trees.
- [ ] The block-identical repack is asserted, not assumed.
- [ ] The DDS parser refuses everything unexpected by name.
- [ ] Orbiter's surface archives convert, and the owner's condition on KTX2 is
      satisfied in fact rather than in principle.
