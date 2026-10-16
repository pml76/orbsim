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

Not yet measured.
