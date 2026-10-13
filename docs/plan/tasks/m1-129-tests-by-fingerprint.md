# M1-129 — Tests that run only when what they read has changed

Phase: B | Status: in progress
Prerequisites: M1-118
Decided by: register decisions 483-491, 495-506 and 508-510, ruled
2026-10-12, and 511, ruled 2026-10-13.
Continues [ADR 0024](../../adr/0024-verification-costs-what-changed.md); its
sibling, the code as modules, is [M1-130](m1-130-code-as-modules.md).

## Purpose

M1-118 ran `check` in both trees about eighteen times, and every run reran all
557 tests, whatever had changed. CTest remembers nothing, so every test runs
every time. This task makes a test run only when something it reads has
changed, keeps a full run before every push, and removes two fixed costs every
`check` and every mutation pass carry today.

## Measured before the ruling (2026-10-11/12)

- **Fingerprints, simulated over M1-118's 16 commits** in a worktree under
  `build/`, removed afterwards: **2,284 test runs instead of 8,771 (26%)**, of
  which 1,008 are the 63 GPU tests counted as always running; **16% if they
  are fingerprinted too** (decision 490). A change inside one layer reran
  105-118 of about 540 tests; `Scalar.hpp` 491, `Math.hpp` 312-329; a
  document 63. The simulation saw only files named on a test's command line,
  so it can under-count for tests that read other files -- item 3 closes that.
- **Reproducible builds**: today an unchanged file compiles and links to
  different bytes -- measured on `LeapSeconds.cpp`'s object and on
  `test_math.exe`. With the compiler's `-mno-incremental-linker-compatible` and
  the linker's `/Brepro`, all 48 programs were identical after a clean rebuild.
- **The `mutants_due` script tests**: eight of them, run one after another
  under one lock, 17-33 s each -- about 3 of the Debug tree's 4.3 minutes of
  tests.
- **A mutation pass leaves a full rebuild behind**: `mutate.py` restores files
  with `git checkout`, which gives them a new time; the next `check` took 18
  minutes for both trees with no source change.
- **What the tests open, recorded** with Process Monitor over a full Debug
  run (2026-10-12, after the ruling): six programs read files under `data/`
  and the application reads `tests/golden/`, none named on a command line;
  five read images the probes write in the same run (decision 508); and the
  results also rest on files outside the repository -- the Vulkan SDK's
  validation layer, Windows' time-zone database, the Python, CMake and clang
  installations (decision 509). Two more script tests read whole folders
  (decision 510).

## What to do

1. **Reproducible builds in every tree** (decision 488): the two settings
   above, and a test that rebuilds a program and requires identical bytes.
   Each toolchain measured by two builds from clean (decision 504): Windows
   clang 945 of 963 files different without the settings and 0 with them;
   gcc-14 and clang on Linux 0 of 642 with none; MSVC 0 of 967 once built
   without debug information (decision 511). `reproducible_build` fails
   without the clang settings, naming the object and the program, and
   passes with them.
2. **Fingerprints** (decision 483): before a test runs, a hash of its program,
   its command line, its environment and every file it reads; equal to its
   last passing run, the test is skipped and reported as skipped. Built on
   what `scripts/mutants-due.py` already knows about each test. **Kept in the
   build tree**, never committed (decision 491).
3. **What a test reads besides its command line** (decision 489): each test
   **records the files it opens**, in a record file read after the run, as the
   probes already do for shaders; until a test records, **a conservative rule
   covers it** -- any change under `data/`, `tests/golden/` or the build files
   reruns every test that could read them. M1-124's item (a), the shader files
   a test read, is done here (decision 495).
4. **The 63 GPU tests fingerprinted too** (decision 490): their programs, the
   shaders, the driver version and the card.
5. **A full, unskipped run before every push** (decision 484), and a
   self-test that changes one file and requires exactly the expected tests to
   run, and one that requires the full run to skip nothing (VERIFICATION rule
   23).
6. **`mutate.py` restores the tree from a snapshot** (decision 506): the
   build outputs and Ninja's record files copied at the start of a pass, and
   after each mutant the outputs, then the file's text, then its time put
   back, with everything the tests wrote; so a pass leaves no rebuild behind.
   Restoring the text and the time alone was measured to leave outputs built
   from mutated code.
7. **The `mutants_due` tests read the build graph once** rather than eight
   times; and M1-121's findings **4.5** -- `--no-tests=error` on every CTest
   judge, `--verify` refusing a judge that does not exist and an empty mutant
   list, `mutants-due.py` counting an unknown judge as due -- and **4.8** -- a
   mutant file an input of its own due-list entry -- are done here, since they
   change the same scripts (decision 495). Their mutants come with them: no
   `--no-tests=error`, and the spec not an input, each caught by its
   self-test.
8. **The working loop** written into `CLAUDE.md`'s definition of done: the
   quick targets -- `lint`, `format-check`, `doc-links` -- and the affected
   suites before a full `check`.

## Questions put at the start

Whatever the design raises that decisions 483-491 do not settle, put to the
owner before the work starts: how the record files are named and read, what a
"skipped" result looks like in CTest's output, and how the full run before a
push is enforced.

## Mutants

At least: a fingerprint that ignores a file it should include; a skipped test
that should have run; the full run before a push skipped; `mutate.py`
restoring the text and the time but not the outputs; a recorded read left out
-- each caught by its self-test.

## Done when

- [ ] Every question the design raised ruled, and the rulings in the register.
- [ ] M1-118's 16 commits measured again with the real implementation,
      against the simulation's 26%.
- [ ] Before and after for a clean `check`, a one-file change, a header change
      and a mutation pass, written in `STATUS.md`.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.
