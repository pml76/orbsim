# ADR 0024: Verification re-does only what a change can affect

Status: **accepted** (2026-09-27), the owner's rulings on
[the proposals](../plan/verification-cost-proposals.md) that followed
[the measurement of what verification costs](../measurements/verification-cost.md).
Register decisions 207 to 216. Implemented by tasks
[M1-88](../plan/tasks/m1-88-lint-pool.md) to
[M1-94](../plan/tasks/m1-94-split-test-files.md).

## Decision

**The chance that a bug survives a task must not rise. Within that, `check`,
the mutation pass and the fuzzers re-do only what a change can affect, and run
no more at once than the machine can hold.** Seven decisions, each implemented
by its own task:

| # | Decision | Task |
|---|---|---|
| 1 | The lint steps share a Ninja pool of `floor(physical memory / 3 GiB)` jobs, at least one. Compiling keeps Ninja's default. | M1-88 |
| 2 | A file is re-linted exactly when the text clang-tidy reads for it changes. Each lint step writes the file's own dependency list, the headers it includes, as a Ninja depfile. The `.clang-tidy` configurations, the verify step and the clang-tidy executable stay inputs of every step. | M1-89 |
| 3 | `tests/AbortBehaviour.cpp` is compiled once, as an OBJECT library linked into every suite, so it is compiled and linted once. A CTest test requires the listener in every suite. | M1-90 |
| 4 | `check` runs `ctest` in parallel, with half the processor threads, computed at configure time. Every test that needs the GPU holds one shared lock, so no two of them run together. | M1-91 |
| 5 | An older mutant file is re-run when any file its judging programs depend on has changed since it last passed. That is the *strict* rule: a script lists them from git and from Ninja's dependency records. The full pass runs at every gate. | M1-92 |
| 6 | Each fuzz target has a corpus committed under `tests/corpus/<target>/`, and a budget from its measured curve: `fuzz_orbit` 240 s, `fuzz_time` 900 s. A task that changes code a fuzzer reaches runs that fuzzer for its budget before its commit, not only at the gates. | M1-93 |
| 7 | `tests/test_time.cpp` is split into three suites and `tests/test_orbit_scales.cpp` into two, with every case moved unchanged. `tests/test_fixture_file.cpp` stays whole. | M1-94 |

**The rules live in the repository**, where they bind: `CLAUDE.md`,
`docs/VERIFICATION.md`, `.claude/rules/lint-config.md`, the queue's standing
rules and the gate documents. Not in any assistant's memory (decision 215).

## Why

Measured on 2026-09-27, per tree:

- **Lint:** 206–313 s, re-run in full after any header edit. That is more than
  the compile, and over half of what a `check` after a header edit costs.
- **Tests:** 48–113 s, run one at a time.
- **One file linted 23 times:** `tests/AbortBehaviour.cpp`, at 114–171 s the
  slowest lint step in every tree.
- **Fuzzing:** `fuzz_time` was still finding new paths at 240 s and stopped at
  about 900 s. Every run started from an empty corpus.

Each decision above was argued, in the proposals, to keep the chance of a
surviving bug level. Each task proves its claim before anything relies on it:
a check seen to fail first, and a measurement.

## What we considered

- **Selecting tests by what changed.** Not adopted. The tests are the cheap
  part, and a selector that misses one input lets a bug through silently. The
  independent review of 2026-09-26 found seven such inputs.
- **Re-linting by target** (P1a). Rejected for the exact per-file list. It is
  simpler and public CMake, but coarser.
- **Linting a reduced compile database** (P2b). Rejected. It works around the
  linter and leaves 22 redundant compiles in place.
- **A loose mutation rule** (re-run only when the mutated file or its tests
  change). Rejected. A mutant's verdict also depends on the code between the
  two, so the loose rule can hide a weakened test for up to a phase.
- **A fixed lint pool of 10, and a fixed test parallelism of 8.** Rejected for
  rules computed from the machine, so another machine gets a fitting number.

## Consequences

- `check` finishes sooner after most edits. After an edit to `core/Units.hpp`
  or `core/Scalar.hpp` it costs what it did, because nearly everything really
  does depend on them.
- **Three scripts join the tooling**, and each is itself checked by a CTest
  test that has been seen to fail:
  - the lint wrapper;
  - the mutation-rerun list;
  - the lint-pool check.
- **Two tests cannot run from both trees at once**, because
  `test_image_files`' scratch file names are shared across trees. `check` is
  run one tree after the other, as it always has been.
- **Fuzzing costs more per gate and in some tasks**: `fuzz_time` goes from 240
  s to 900 s. This is a deliberate rise in cost that buys coverage.

## Update, 2026-09-27: how M1-89 knows what a file includes

Decision 2 above said each lint step writes the file's own dependency list as a
depfile, through a wrapper script. Before any of it was written, a simpler
mechanism with the same precision was found and put to the owner, who chose it
(register decision 217). **Each lint step depends on its own file's compiled
object**, in every target that compiles the file.

Ninja already records every header the compiler reports for each object -- 377
for `src/view/Camera.cpp`, MSVC's `cassert` among them -- and rebuilds the
object exactly when one of them changes. So there is no script, and no
splitting of Windows command strings. The rest of decision 2 is unchanged.

## Update, 2026-09-28: the harness restores, and compiles join the pool

Two findings of the work above became tasks (register decisions 221 to 223).

- **The mutation harness rebuilds what it restored** (M1-95). Ninja notices a
  restored file, but it rebuilds only what it is asked to build, and the
  harness asked only for the next mutant's targets. So a program the previous
  mutant rebuilt was judged still built from the mutated code. M1-90's first
  pass counted two kills that way. `scripts/mutants/m1-95.json` recreates that
  situation as a regression test.
- **Decision 1 is widened** (M1-96). The pool is `orbsim_memory`, and it holds
  this project's own compiles as well as the lint steps: every library and
  program target the top-level `CMakeLists.txt` defines. A full rebuild had
  run `check` out of memory, with about 20 compiles beside the 10 lint jobs.
  Dependencies built in their own directories keep Ninja's default.
  - Measured on a clean rebuild through `check`: never more than 10 heavy
    processes at once.
  - The rule sizes the pool from *total* physical memory. A virtual machine
    holding 14.7 GB left free memory at 572 MiB at its lowest, and the rule
    cannot see that.

## Update, 2026-09-28: a faster restore, and no stale tree

Two more tasks, M1-97 and M1-98 (register decisions 224 to 226).

- **The harness rebuilds only what the next mutant does not build itself**
  (M1-97). M1-95's restore rebuilt the previous mutant's programs, and the
  next mutant usually built the same ones again. `m1-09.json` took 801 s
  before and 332 s after, with every verdict the same. The rule is one
  function, `rebuild_plan` in `scripts/mutate.py`, with a self-test.
- **A build tree set up from text no longer on disk is refused** (M1-98).
  CMake reads `CMakeLists.txt` when a configure starts and writes the
  build plan when it ends. Ninja compares dates, so an edit in between left
  a plan from the old text that nothing replaced. This was reproduced in a
  scratch project first.
  - The configure fingerprints what it reads and fails if any of it
    changes before the end.
  - `check` waits for `configure-current`, which compares the fingerprints
    with the files on disk once more.
  - CLion's automatic reload, which caused it, is switched off on each
    machine (`docs/PROJECT_STATE.md` section 2).

## Update, 2026-09-30: a build-definition change, judged by what it changed

Register decision 250, task M1-103. **The strict rule of M1-92 is narrowed for
the build definition only.** Every task since M1-92 but one had changed
`CMakeLists.txt`, and each change made every mutant file due: on 2026-09-30,
22 files and 118 minutes for a change that altered none of their programs.

- A clean pass records a **fingerprint of each judge** in
  `scripts/mutation-passes.json`: for a program, Ninja's full command list and
  the contents of every file it is built from that git does not track and no
  build step makes -- the fetched dependencies and the toolchain's own
  headers and libraries; for a CTest entry, its command and properties. The
  repository's and the tree's paths are placeholders, so a record travels.
- `CMakeLists.txt`, `cmake/` and `CMakePresets.json` then make a file due only
  through a changed fingerprint. Where none can be compared -- no record of
  them, another tree -- the strict rule stands. `scripts/mutate.py` and
  `data/` still make every file due.
- Measured before it was proposed: 0 of 26 test programs' command lists moved
  between 849b899 and 41001ba, 26 of 26 under a control edit. And after it was
  built, on the real tree: a comment in `CMakeLists.txt` changed no
  fingerprint, and a compile definition on `orbsim_core` made due every file
  whose judges link it, naming each program.
- **What it still costs**: a file whose mutants mutate `CMakeLists.txt` itself
  is due whenever that file changes -- seven of them on 2026-09-30, about 30
  minutes -- and a toolchain update now makes the affected files due, which
  the strict rule never noticed.

