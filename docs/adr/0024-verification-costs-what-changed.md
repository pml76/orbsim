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
