# M1-101 — Read the MSVC tree's paths

Phase: A | Status: **done, 2026-09-30**
Prerequisites: M1-96, M1-98
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 243, 244 and 247

## Purpose

**Two CTest tests fail in the MSVC tree**, `build/windows-msvc`:
`configure_current` ("`check` does not wait for `configure-current`") and
`memory_pool` ("no compiles of this project's own targets found"). Found by
M1-17's run of the other compilers on 2026-09-29, and measured then at the
commit before it, 2ab62a8, with the same words
([M1-17](m1-17-golden-images.md), "Other compilers").

**The checks are wrong, not the tree.** Measured on 2026-09-30:

- CMake's Ninja generator writes the MSVC tree's paths with backslashes --
  `build CMakeFiles\check`, `build CMakeFiles\orbsim_core.dir\…` -- and the
  clang trees' with forward slashes: 0 lines of `build/debug/build.ninja` begin
  `build CMakeFiles\`, against 363 that begin `build CMakeFiles/`, and
  `build/windows-msvc/build.ninja` has none of the second kind.
- `scripts/check-configure-current.py` looks for `build CMakeFiles/check`, and
  `scripts/check-memory-pool.py` for outputs starting `CMakeFiles/`.
- Read with either separator, the MSVC tree follows both rules: `check` waits
  for `configure-current`, and all 349 of this project's compiles are in
  `orbsim_memory`, none outside it.

The memory pool check's lint-step pattern already accepted both separators;
only its compile pattern did not. No other script reads a `CMakeFiles/` path
from a Ninja file.

## What to do

As decided (decision 247):

- **Self-test first**: each script's self-test gains the MSVC form of its
  correct tree, which must be accepted, and of a faulty one, which must be
  reported. Seen failing before the change: the correct MSVC form reported.
- **`check-memory-pool.py`** reads every edge's outputs with backslashes turned
  into forward slashes, before any pattern is applied.
- **`check-configure-current.py`** looks for `build CMakeFiles[/\\]check`.
- Both are one line, placed so that no existing mutant anchor moves:
  `m1-96.json` and `m1-98.json` anchor on the lines beside them.

**Mutants**: `scripts/mutants/m1-101.json`, three of them (decision 244): the
memory pool check reading forward slashes only, and `configure-current`
finding `check`'s edge with forward slashes only and with backslashes only.

## Done when

- [x] Both self-tests were seen failing on the MSVC form of a correct tree,
      before the change: "a correct pool, with backslashes: reported" and
      "check waits, with backslashes: reported". Then ten of ten, each.
- [x] `configure_current` and `memory_pool` pass in `build/windows-msvc`,
      set up again on 2026-09-30: "every file the configure read is
      unchanged", and "every lint step and every compile of this project is
      in orbsim_memory, depth 10".
- [x] `check` passes in both Windows trees, 319 of 319, 2026-09-30.
- [x] `scripts/mutants/m1-101.json` passes, 3 of 3, in the full pass of 2026-09-30 to 2026-10-01 at 7655041 -- 25 files, 261 caught, 13 survived, every one declared, none invalid or hung.
- [x] The other compilers' full runs, 2026-10-01: `windows-msvc` 319 of 319, `linux-sanitize` and `linux-gcc` 299 of 299 each, once M1-103's self-test was made right on Linux (decision 253).
