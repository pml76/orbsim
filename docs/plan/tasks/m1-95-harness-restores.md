# M1-95 — The mutation harness rebuilds what it restored

Phase: A | Status: **done, 2026-09-28**
Prerequisites: M1-92
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 221 and 223

## Purpose

**A mutant must be judged against programs built from the code, not from the
previous mutant.**

`scripts/mutate.py` restores each mutated file with `git checkout` before the
next mutant. Ninja notices the restored file, but it rebuilds only what it is
asked to build, and the harness asks only for the *next* mutant's targets. A
program the previous mutant rebuilt therefore stays built from the mutated
code until something asks for it.

M1-90's first pass shows the cost. Its first mutant removed the abort listener
from the suites' link and rebuilt `test_math` and `test_orbit`. The next
mutants asked only for `orbsim_shaders`, so their judge, `abort_listener`,
ran the stale `test_math.exe`, and two kills were counted for the wrong
reason.

## What to do

- After restoring the mutated files, `mutate.py` rebuilds the previous
  mutant's targets (its `suites` and `targets`) before the next mutant starts.
  Only the restored file is recompiled, and those programs relinked.
- **The regression test is a mutant file**, `scripts/mutants/m1-95.json`, which
  recreates M1-90's situation:
  - **mutant A** removes the listener from the suites' link, rebuilding
    `test_math`, and must be caught;
  - **mutant B** changes only a comment in `scripts/check-abort-listener.py`,
    is judged by `abort_listener`, and must survive, being declared as a
    survivor.

  With the fault, B is judged against the stale `test_math.exe` and "caught",
  and the run fails. With the fix, B survives as declared.

## Done when

- [x] **`m1-95.json` was run with the unfixed harness and failed for the right
      reason**: "UNEXPECTED CAUGHT (expected survives)" for the comment-only
      mutant, caught by `abort_listener` against a `test_math` still built
      without the listener.
  - A first attempt came out *invalid* instead: CLion reloaded the CMake
    project in `build/relwithdebinfo` because mutant A had edited
    `CMakeLists.txt`, and the two collided ("ninja: failed recompaction:
    Permission denied").
  - That is the hazard `STATUS.md` records as open item 3, seen during a
    mutation pass. The run was repeated once CLion had finished.
- [x] **With the fix, it runs clean**: 1 caught, and the comment-only mutant
      surviving as declared. The pass is recorded once this is committed.
- [x] `check` passes in both trees.
