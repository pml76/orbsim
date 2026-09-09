# M1-27 — The KTX2 writer

Phase: B | Status: not started
Prerequisites: M1-26
Decided by: [ADR 0010](../../adr/0010-tiles-are-ktx2.md)

## Purpose

The tile tool has to produce files, and the `.tree` converter has to produce
them too. One writer, used by both.

## What to implement

`src/view/Ktx2Write.hpp` / `.cpp`.

- Write a plain KTX2 file — **no supercompression** — from a format, dimensions
  and a mip chain supplied as spans of bytes. The level index is written
  largest-level-last, as the specification requires, and the byte offsets are
  computed rather than assumed.
- The formats this milestone needs, and no others: BC7 sRGB for imagery, BC7
  UNORM for masks, and 16-bit single-channel for elevation in M1-55. An
  unsupported format is refused by name at the call, not silently written.
- **Deterministic output.** No timestamp, no padding filled from uninitialised
  memory, no map iteration order anywhere in the path. Byte-identical for
  identical input, which M1-30 depends on for its own determinism claim.
- Refuse rather than write nonsense: a mip chain whose dimensions do not halve
  correctly, a level whose byte count contradicts the format's block size, zero
  levels, or a dimension of zero.

## Out of scope

BC7 encoding (M1-29) — the writer takes bytes and does not care how they were
produced. Supercompression. Metadata beyond what the format requires.

## Tests

`tests/test_ktx2.cpp`, extended.

- **Round trip**: write a mip chain, read it back with M1-26's reader, and get
  the same dimensions, format, level count and — byte for byte — the same level
  data.
- **Against the external tool**, which is what stops the round trip being the
  code checking itself: write an 8×8 RGBA8 with a full mip chain and compare the
  file against the equivalent produced by the Khronos `ktx` tool. Fields that
  are legitimately free (writer-specific metadata) are compared structurally;
  everything the specification fixes is compared byte for byte. If `ktx` is not
  installed, the test is skipped **loudly**, with a message naming what was not
  checked — a silently skipped test is worse than a missing one.
- **Determinism**: writing the same input twice gives byte-identical files.
- **Every refusal by name**: a mip chain that does not halve, a wrong level
  size, zero levels, a zero dimension, an unsupported format.
- The written file passes `ktx validate` where the tool is available, and the
  invocation is recorded in the test file's comment so it can be run by hand.

## Verification

The standing rules, plus one run of `ktx validate` over a written file, by hand,
with the output pasted into the commit message.

## Done when

- [ ] `check` green in both trees.
- [ ] Round trip through our own reader is exact.
- [ ] A file we wrote has been compared against one the Khronos tool wrote.
- [ ] The output is byte-identical across runs.
