# M1-129 — What `check` and a mutation pass cost, before and after

Kind: reference
Binding: no — this file records a measurement and how to repeat it
Read when: you want the numbers behind M1-129's "before and after", or you
want to repeat them after a change to `scripts/run-tests.py` or `mutate.py`.

Written 2026-10-16 with the method below, before any figure was taken
(register decision 607); the results follow when they are measured.

## Method

**Before** is `build/wt129`, a worktree at `f0eeb24` -- the commit before
M1-129's first -- whose two trees were built on 2026-10-12 and 2026-10-13.
It also holds step 1's two CMake edits, uncommitted and identical to those of
`4495ea0`, so its programs are built reproducibly; that changes no test
selection, since before M1-129 `check` ran every test. **After** is the main
trees at the finished code.

Four scenarios, in each tree, one run each, nothing else running and CLion
closed:

1. **`check` with nothing changed**, right after a `check` that passed.
2. **A one-file change**: a one-line comment added at the top of
   `src/orbit/Orbit.cpp`, then `check`.
3. **A header change**: the same at the top of `src/core/Units.hpp`, which
   nearly every file includes, then `check`.
4. **A mutation pass**: `m1-118.json`'s twenty mutants in `build/debug`, and
   the `check` after it. Its before-figures were measured on 2026-10-13 with
   the harness of the day (`HISTORY.md` section 7): the pass 10 min 1 s with
   its final build, and the Debug `check` after it with 79 lint steps due
   again -- that run was stopped for memory and, on the owner's word, not
   repeated; and 18 minutes for both trees' `check` after a pass with no
   source change (the task document's "Measured before the ruling").

Each `check` is timed whole, and split where its output allows: the build
and lint steps, and the tests -- how many ran and how many were skipped, as
`scripts/run-tests.py` prints them. The edits are taken out again after each
scenario.

## Results

**Before: measured 2026-10-16** in `build/wt129`, one run each, the machine
otherwise idle. Each tree was first built and its `clear` probe run once,
since its `doc-links` reads `build/debug/probes/clear.png`, which a fresh
worktree has not written yet (register decision 507); the warm runs below
include that tree's catching up.

| Tree | Scenario | Wall | Tests | Tests run | Compiles | Links | Lint steps |
|---|---|---|---|---|---|---|---|
| Debug | warm | 435.5 s | 192.9 s | 559 | 0 | 0 | 90 |
| Debug | nothing changed | 203.8 s | 201.2 s | 559 | 0 | 0 | 0 |
| Debug | one file, `Orbit.cpp` | 253.2 s | 225.5 s | 559 | 1 | 49 | 1 |
| Debug | its revert | 230.0 s | 205.2 s | 559 | 1 | 49 | 1 |
| Debug | header, `Units.hpp` | 526.3 s | 204.0 s | 559 | 92 | 51 | 57 |
| Debug | its revert | 514.2 s | 199.5 s | 559 | 92 | 51 | 57 |
| RelWithDebInfo | warm | 645.0 s | 125.8 s | 559 | 884 | 64 | 90 |
| RelWithDebInfo | nothing changed | 111.5 s | 108.7 s | 559 | 0 | 0 | 0 |
| RelWithDebInfo | one file, `Orbit.cpp` | 132.9 s | 111.1 s | 559 | 1 | 49 | 1 |
| RelWithDebInfo | its revert | 140.7 s | 119.2 s | 559 | 1 | 49 | 1 |
| RelWithDebInfo | header, `Units.hpp` | 473.1 s | 117.6 s | 559 | 91 | 51 | 56 |
| RelWithDebInfo | its revert | 447.0 s | 109.9 s | 559 | 91 | 51 | 56 |

**After: measured 2026-10-17** in `build/debug` and `build/relwithdebinfo`
at `aa11bc1`, with the same script, one run each, the machine otherwise idle.
Every run passed. 580 tests in each tree, where the before tree had 559.

| Tree | Scenario | Wall | Tests | Tests run | Compiles | Links | Lint steps |
|---|---|---|---|---|---|---|---|
| Debug | warm | 30.0 s | 23.5 s | 21 | 0 | 0 | 0 |
| Debug | nothing changed | 6.3 s | 0.1 s | 1 | 0 | 0 | 0 |
| Debug | one file, `Orbit.cpp` | 58.2 s | 32.6 s | 76 | 1 | 52 | 1 |
| Debug | its revert | 58.0 s | 31.9 s | 76 | 1 | 52 | 1 |
| Debug | header, `Units.hpp` | 488.2 s | 174.7 s | 454 | 92 | 54 | 57 |
| Debug | its revert | 467.4 s | 171.4 s | 454 | 92 | 54 | 57 |
| RelWithDebInfo | warm | 341.2 s | 30.9 s | 27 | 91 | 54 | 56 |
| RelWithDebInfo | nothing changed | 6.1 s | 0.6 s | 7 | 0 | 0 | 0 |
| RelWithDebInfo | one file, `Orbit.cpp` | 58.8 s | 34.1 s | 82 | 1 | 52 | 1 |
| RelWithDebInfo | its revert | 56.9 s | 31.3 s | 82 | 1 | 52 | 1 |
| RelWithDebInfo | header, `Units.hpp` | 402.5 s | 77.7 s | 459 | 91 | 54 | 56 |
| RelWithDebInfo | its revert | 417.1 s | 90.8 s | 459 | 91 | 54 | 56 |

What the figures say:

- **Nothing changed: 203.8 s to 6.3 s in Debug, 111.5 s to 6.1 s in
  RelWithDebInfo.** The tests that still run are those CTest reports as
  skipped, which are never remembered (decision 536): the Linux-only case in
  both trees, and in RelWithDebInfo also the six assertion probes, which skip
  themselves where assertions are not live.
- **One file: 253.2 s to 58.2 s in Debug, 132.9 s to 58.8 s in
  RelWithDebInfo**, running the 76 tests `Orbit.cpp` reaches, and the same
  six more in RelWithDebInfo. The revert runs them again, since a test is
  remembered by its last passing run, which was the edited one.
- **A header nearly every file includes: 526.3 s to 488.2 s in Debug, 473.1 s
  to 402.5 s in RelWithDebInfo.** The tests fall from 204.0 s to 174.7 s and
  from 117.6 s to 77.7 s, but 454 and 459 of 580 still run, and the compiles
  and lint steps, which no test selection touches, take the rest. A comment
  changes no function, so this is the case function-level selection, M1-131
  to M1-133, is aimed at.
- **The warm runs are not a scenario.** Both ran the 20 tests that name the
  build tree on their command line, which are fingerprinted on every file of
  the repository (decision 500), so `aa11bc1`'s change to two documents
  reran them; and beside them those that always run. RelWithDebInfo's also
  rebuilt 91 objects: the Debug run's reverts put `Units.hpp`'s bytes back
  with a new time, and the two trees share the source. The programs it
  relinked came out byte for byte as before, so no test of theirs ran.

**The first attempt, 2026-10-16** in `build/debug` at `a6da837`, gave a warm
`check` of 7.7 s and one with nothing changed of 7.1 s -- 1 test run, 579
skipped -- and then **found a defect**: the one-file change failed `check`
after 62.0 s (76 run, 504 skipped; 1 compile, 52 links, 1 lint step),
`mutants_due_probe_shaders` failing because the application, relinked to the
same bytes, was newer than every probe's sidecar. Fixed by decision 616. Its
three lines stay in `timings.jsonl`, before the second attempt's, under the
same label (decision 619).

**The mutation pass (scenario 4)** is timed during decision 580's passes:
`m1-118.json` in `build/debug`, and the `check` after it.

The script and the raw figures are in [`m1-129/`](m1-129/): `timings.py`
(run as `timings.py <label> <source root> <build tree>`), `timings.jsonl`.
Beside them, `procmon-compare.py`, decision 612's comparison of the read
records with a Process Monitor CSV, kept for the audit each phase gate
repeats (decision 509).
