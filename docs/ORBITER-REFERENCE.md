# Reading Orbiter's source, and the licence boundary

Kind: reference
Binding: yes — the licence table is a legal constraint, not a preference
Read when: you are about to read or borrow from the Orbiter reference clone, or
you are working on the tile pipeline (milestone 1 phase B, tasks M1-24 to
M1-37).

Orbiter's source is worth reading and is **not** uniformly licensed. The
reference clone lives at `C:\Reference\orbiter`, outside this repository,
~1.1 GB, shallow. It is not required to build.

| Path in the clone | Licence | Use |
|---|---|---|
| repository root | MIT (Schweiger, 2000–2026) | Read and borrow, with attribution |
| `Utils/tileedit/qt/src/` | MIT — no GPL headers, covered by the root licence | The clearest tile-format reference. Usable |
| `Utils/tileedit/qt/extern/fastdxt/` | **LGPL** (vendored DXT codec, 78 KB) | Do not copy — Vulkan does BC natively |
| `OVP/D3D9Client/` | **LGPL** | Where TileManager2 lives, but Direct3D and LGPL |

The standalone `mschweiger/orbiter-tileedit` repo on GitHub is GPL v3 — the same
code under a different licence, which is entirely the author's prerogative. An
MIT copy exists in the monorepo, so do not clone the GPL one; there is nothing
to gain and a licence to lose. One was cloned early on and then **deleted**
rather than kept and carefully avoided: removing a hazard beats managing one.

**The format specification is `Doc/Orbiter Developer Manual/PLANETS.tex`**,
section `sssec:tile_file_layout`: levels, latitude bands, longitude indices,
`TileFormat = 2`. Read it before any source.

The archive format itself is `Utils/tileedit/qt/src/ZTreeMgr.{h,cpp}` in the
clone, MIT: a magic-tagged header, a table of contents of quadtree nodes each
carrying a file offset, an inflated size and four child indices, then
zlib-deflated per-tile blobs. Surface tiles are DDS/DXT1; elevation is
Orbiter's own ELEV format (`elv_io.cpp`, also MIT). Only the `Surf` and `Elev`
layers are converted here — `Mask`, `Label` and `Cloud` are out of scope for
milestone 1.

Planetary imagery lives in `data/textures/`, gitignored, with
[a README](../data/textures/README.md) saying where to obtain it. Three Blue
Marble files (~29 MB) are already downloaded on this machine.

This project's own licence is MIT. What every third-party dependency is
licensed under, and which files of it are actually compiled, is
[`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 7,
verified 2026-09-08.
