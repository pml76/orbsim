# M1-36 — The Orbiter `.tree` reader, and its fuzz target

Phase: B | Status: not started
Prerequisites: M1-24, M1-28
Decided by: [ADR 0010](../../adr/0010-tiles-are-ktx2.md)

## Purpose

The owner's condition on adopting KTX2 was that Orbiter's `.tree` archives can
still be converted. This is the half that reads them. It is the second parser of
untrusted bytes in this milestone, and the more dangerous one: the file format
is somebody else's, the archives are downloaded from add-on sites, and the data
block is compressed, which means a length field decides how much memory gets
written.

## What to implement

`src/view/TreeArchive.hpp` / `.cpp`. The format is documented in
`PLANETS.tex` section `sssec:compress_archive_layer`, and implemented in the
MIT-licensed `Utils/tileedit/qt/src/ZTreeMgr.{h,cpp}` in the reference clone.
**Read the specification first and the source second** — the document is the
contract, the source is the clarification.

- The 48-byte header: magic `'T','X',1,0`, header size, flags, data offset,
  data length, node count, and the five root indices (levels 1, 2, 3 and the two
  level-4 quadtree roots), with `(uint32)-1` meaning absent.
- The table of contents: `nodeCount` entries of `{int64 pos; uint32 size;
  uint32 child[4]}`, where `pos` is relative to the data block and `size` is the
  **inflated** size.
- Lookup by `TileId`: start at the level-4 root for the hemisphere and descend,
  choosing the child by one bit of `ilat` and one of `ilng` per level, exactly
  as the specification's child ordering states. A missing child ends the descent
  and reports `NotPresent`.
- Inflate with `stbi_zlib_decode_buffer` from the already-pinned `stb` — one
  fewer dependency than adding zlib, and the same code path PNG decoding
  already trusts.
- **Refuse by name**, and this list is the task: bad magic; a header size that
  is not 48; a data offset that is not header plus TOC; a node count that
  contradicts the data offset; a node position or size reaching outside the data
  block; a child index at or beyond `nodeCount`; an inflated size that does not
  match the node's declared size; an inflate failure; and **a cycle in the
  child graph**, which a hand-edited archive can contain and which would
  otherwise be bounded only by the level limit.
- `TreeArchiveSource` joins the `TileSource` variant.
- **`tests/fuzz_ztree.cpp`**, the project's third fuzz target: arbitrary bytes
  in, and the invariant that every returned blob lies inside its own allocation,
  no read goes outside the input, and no inflate is asked for a size the header
  did not declare.

## Out of scope

Converting anything (M1-37). The `Mask`, `Label`, `Cloud` and `Elev_mod`
layers. Writing archives. Orbiter's directory-tree ("cache") layout, which is
the same tiles as loose files and is not needed once the archive reads.

## Tests

`tests/test_tree_archive.cpp`.

- **Synthetic archives built in the test**, since the format is fully specified:
  a two-node archive, a three-level tree, one with gaps, one with only the
  global levels. Descent, lookup and inflation all checked against what the test
  itself constructed.
- **Every refusal by name**, one malformed archive each, including the cycle.
- **The prefix sweep** from M1-26: parse every truncation of a valid archive;
  each must report or succeed, never read out of bounds.
- **A real archive, if one is present.** If an Orbiter installation with
  `Textures/<planet>/Archive/Surf.tree` can be found, the test reads it and
  checks that the level-4 roots exist and inflate to 512×512 DXT1 payloads. If
  not, the test is **skipped loudly**, naming what was not verified — synthetic
  archives verify our reading of the specification, and only a real one verifies
  the specification against reality.

## Verification

The standing rules, plus `fuzz_ztree -max_total_time=240` clean, and the target
added to the phase gate list.

## Done when

- [ ] `check` green in both trees.
- [ ] `fuzz_ztree` runs four minutes clean; any crash found becomes a committed
      regression case.
- [ ] Every refusal, cycles included, has a test that asks for it by name.
- [ ] A real Orbiter archive has been read at least once, or the skip is
      recorded in the commit message as an explicit gap.
