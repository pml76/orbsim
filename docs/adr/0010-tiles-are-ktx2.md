# ADR 0010: Tiles are KTX2, and Orbiter's `.tree` is converted rather than streamed from

Status: accepted (2026-09-08; recorded 2026-09-09)

Decisions 20 and 21 of
[the milestone 1 register](../plan/milestone-1-decisions.md).

## Decision

- **The on-disk tile container is KTX2**, carrying the mip chain and the Vulkan
  format number directly. One file per tile, read as a header and a set of
  offsets, uploaded without a decode.
- **The renderer streams from a KTX2 pyramid, always.** That pyramid is
  produced offline: by `tilegen` from source imagery, or by `treeconv` from an
  Orbiter archive. Nothing on the frame path inflates a zlib stream or parses
  somebody else's container.
- **Orbiter's `Surf` tiles are repacked, not transcoded.** They are 512x512
  DXT1, DXT1 is BC1, and Vulkan reads BC1 natively -- so the conversion writes
  the same blocks into a KTX2 file as `BC1_RGB_SRGB`. No decode, no re-encode,
  no quality loss, and no encoder in the path at all.
- **`TileSource` is a closed set**, a `std::variant` in the shape
  [`0011`](0011-the-integrator-has-three-seams.md) sets for the physics
  seams: `SyntheticTileSource` first, then `KtxPyramidSource`, then
  `TreeArchiveSource`. Each addition breaks every `std::visit` that has not
  been updated, which is the property being bought.
- **`TreeArchiveSource` exists so the converter reads an archive through the
  same seam every other source uses**, rather than as a parallel code path. It
  is a reader, and what feeds `treeconv`. What this record rejects is making a
  `.tree` archive *the format the shipping renderer streams from*.
- **Both readers are parsers of untrusted bytes, and each arrives with its
  fuzz target in the same task** -- `fuzz_ktx2` with
  [M1-26](../plan/tasks/m1-26-ktx2-reader.md), `fuzz_ztree` with
  [M1-36](../plan/tasks/m1-36-tree-reader.md), the DDS header inside the
  latter's reach. Each refuses by name rather than half-reading: nine named
  KTX2 errors, and for the archive a list that includes a cycle in the child
  graph, which a hand-edited file can contain and which the level limit alone
  would not bound.
- **The reader comes before the writer**, deliberately. A writer verified only
  by our own reader proves the two agree; a reader verified against files
  produced by the Khronos `ktx` tool proves it reads KTX2.

## What we considered

**DDS.** What Orbiter itself uses, which is worth something for the later
reader, and what most of the surrounding tooling emits. Against it: there is no
single blessed specification, the pixel format arrives as a FourCC that does
not map cleanly onto `VkFormat`, and the mip-chain layout is convention rather
than contract. KTX2 carries the Vulkan format number as a number, so the upload
path reads the file instead of inferring from it. DDS does not disappear -- it
is parsed inside the converter, where a wrong guess costs an offline run rather
than a frame.

**Our own container.** Trivially exactly what is needed, and one more format
that nothing outside this repository can validate. KTX2 comes with the Khronos
`ktx` tool, which is how the reader is tested against files this project did
not produce -- [`../VERIFICATION.md`](../VERIFICATION.md) rule 3, for a parser
rather than for physics.

**A runtime `.tree` `TileSource` as the shipping path**, skipping the converter
and reading Orbiter's archives directly. It is genuinely attractive: it brings
the add-on ecosystem in for free and there is no offline step. It was rejected
on three counts. It puts zlib inflation on the streaming path, where the frame
budget is. It makes the pyramid's contents a function of which add-ons are
installed, so "the same scenario" is no longer the same scenario on two
machines. And it makes seeing Earth at all depend on owning an Orbiter
installation, which the Blue Marble route deliberately avoids. The archive
reader is kept and is not wasted -- it is the converter's input side.

## Why

The container question looks like a matter of taste and is really a question
about where a mistake surfaces. A format whose pixel layout must be inferred
puts that inference on the upload path, where being wrong means a corrupt tile
three levels down in a quadtree, at a camera position nobody can reproduce.
KTX2 moves it to a header field, which is checkable, and to a reader that
refuses by name.

The repack is the part most likely to be lost later, so it is written down: BC1
is already what the GPU wants. Any pipeline that decodes Orbiter's tiles and
re-encodes them -- even to a better format like BC7 -- spends quality and time
to arrive somewhere no better than where it started.

And the two parsers are the highest-value fuzzing targets the project does not
yet have. [`../VERIFICATION.md`](../VERIFICATION.md) rule 13 names file parsers
explicitly, after a fuzzer found five defects in the two-body core that no
hand-written test had reached; these consume bytes downloaded from add-on
sites, which is a strictly worse threat model than a scenario file.

## What this record does not decide

- **The elevation tile format.** Orbiter's ELEV layer is extracted in
  [M1-57](../plan/tasks/m1-57-orbiter-elev.md), and it needs an elevation tile
  format to exist first ([M1-55](../plan/tasks/m1-55-elevation-tiles.md)).
- **Basis supercompression, cube maps and texture arrays**, all refused by name
  by the reader.
- **Which Orbiter layers are ever supported.** Only `Surf` and `Elev` are in
  scope; `Mask`, `Label`, `Cloud` and `Elev_mod` are not.
- **Whether `tilegen` ever emits anything but BC7**, or `treeconv` anything but
  BC1.
