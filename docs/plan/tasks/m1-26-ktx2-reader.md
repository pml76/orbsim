# M1-26 — The KTX2 reader, and its fuzz target

Phase: B | Status: not started
Prerequisites: M1-01, M1-25

## Purpose

The first of this milestone's three parsers of untrusted bytes.
`VERIFICATION.md` rule 13 names exactly this as the highest-value fuzzing target
the project does not yet have: *"a file parser… because those consume untrusted
bytes and are the classic memory-safety surface."*

The reader comes **before** the writer deliberately. A writer verified only by
our own reader proves the two agree; a reader verified against files produced by
the Khronos tools proves it reads KTX2.

## What to implement

`src/view/Ktx2.hpp` / `.cpp`, in the Vulkan-free library — a KTX2 file carries a
Vulkan format number, but a number is not a header.

- Parse the 12-byte identifier, the header (format, type size, dimensions, layer
  and face counts, level count, supercompression scheme, and the index offsets),
  and the level index array. Return a **view**: for each mip level, an offset, a
  length and the uncompressed size, so that a caller can upload without copying.
- **Refuse, by name, everything not supported**, rather than half-reading it:
  a supercompression scheme other than none; more than one layer or face; a 3D
  texture; zero dimensions; a level count exceeding what the dimensions permit;
  any offset or length outside the file; overlapping levels; a level size
  inconsistent with the format's block size and the level's dimensions.
- `enum class Ktx2Error` with one value per refusal above and a `describe()`.
  Nine named errors is not verbose — it is the difference between "the tile is
  corrupt" and knowing which field.
- **`tests/fuzz_ktx2.cpp`**, built by the existing `ORBSIM_BUILD_FUZZERS` option
  alongside `fuzz_orbit`, asserting what a fuzzer can know: the parser either
  reports or returns a view **entirely inside the input buffer**, never reads
  outside it, and never leaves an offset or length uninitialised.

## Out of scope

Writing (M1-27). Decoding block-compressed data — Vulkan consumes BC formats
natively, so this is header work and an upload. Basis supercompression. Cube
maps and arrays, refused above.

## Tests

`tests/test_ktx2.cpp`.

- **Files produced by the Khronos `ktx` tool**, committed small: an 8×8 RGBA8
  with a full mip chain, and an 8×8 BC7. Dimensions, format, level count and
  every level's offset and length are asserted against what the tool reports —
  data this project did not produce.
- **Every refusal by name**, each from a hand-built malformed file: bad
  identifier, truncated header, supercompression set, two faces, zero width, an
  offset past end of file, a length that overflows when added to its offset,
  overlapping levels, and a level size that contradicts the block size.
- **Truncation sweep, which is the test worth writing carefully**: take a valid
  file and parse **every prefix of it**, from zero bytes to full length. Each
  must either report an error or succeed with an in-bounds view. Under ASan this
  is a very cheap way to find the read that walks off the end, and it covers the
  case a fuzzer reaches slowly.
- Integer overflow: offsets and lengths near `UINT64_MAX` are rejected before
  they are added together, not after.

## Verification

The standing rules, plus the fuzzer, run deliberately:

```
wsl -d Ubuntu -u root -- bash -c "… cmake --preset linux-fuzz && \
    cmake --build build/linux-fuzz && ./build/linux-fuzz/fuzz_ktx2 -max_total_time=240"
```

Four minutes clean before this task is done, and it joins the phase gate list
from here on.

## Done when

- [ ] `check` green in both trees.
- [ ] `fuzz_ktx2` runs four minutes with zero findings, and the corpus of any
      crashes found on the way is committed as regression cases.
- [ ] Every refusal has a test that asks for it by name.
- [ ] The prefix sweep passes under ASan.
