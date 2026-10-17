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

**After: to be measured again.** The first attempt, 2026-10-16 in
`build/debug` at `a6da837`, gave a warm `check` of 7.7 s and one with
nothing changed of 7.1 s -- 1 test run, 579 skipped -- and then **found a
defect**: the one-file change failed `check` after 62.0 s (76 run, 504
skipped; 1 compile, 52 links, 1 lint step), `mutants_due_probe_shaders`
failing because the application, relinked to the same bytes, was newer than
every probe's sidecar. Fixed by decision 616; the after run is to be taken
again on the committed fix, both trees, with the same script.

**The mutation pass (scenario 4)** is timed during decision 580's passes:
`m1-118.json` in `build/debug`, and the `check` after it.

The script and the raw figures are in [`m1-129/`](m1-129/): `timings.py`
(run as `timings.py <label> <source root> <build tree>`), `timings.jsonl`.
Beside them, `procmon-compare.py`, decision 612's comparison of the read
records with a Process Monitor CSV, kept for the audit each phase gate
repeats (decision 509).
