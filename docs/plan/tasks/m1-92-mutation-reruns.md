# M1-92 — When an older mutant file runs again

Phase: A | Status: **done, 2026-09-27**
Prerequisites: M1-88
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 212

## Purpose

**Whether a test catches a mutant depends on three things**: the mutated file,
the test, and all the code between them. A task that changes any of them can
turn a caught mutant into a survivor, and today nothing says which older
mutant files a task should re-run. That is decided by judgement, and judgement
can forget.

The full pass costs 54 minutes, and a single file 2–9
([`../../measurements/verification-cost.md`](../../measurements/verification-cost.md)).

## What to do

- **Record each clean pass.** `scripts/mutate.py` writes the commit a mutant
  file last passed at into `scripts/mutation-passes.json`: after a clean run,
  and only when the code folders match the commit, since otherwise the record
  would vouch for code that is not committed. The record was seeded with the
  passes that had already run, each at its true commit.
- **List what is due.** `scripts/mutants-due.py <tree>` lists every mutant file
  that is *due*: one where anything its judges depend on has changed since
  its recorded pass. That means:
  - its mutated files;
  - everything its judging programs are built from, from Ninja's `-t inputs`
    and dependency log;
  - `shaders/` for a judge that runs the application;
  - `data/`;
  - every file on a judging CTest entry's command line;
  - the build definition and the harness itself.
- **The rule**, in `docs/VERIFICATION.md` rule 19: a task runs every file the
  script lists before its commit. The full pass runs at every gate.

## Done when

- [x] **Planted changes**, judged with `--only-assumed`, each listing a sensible
      set and never missing one it should:

  | Assumed change | Due |
  |---|---|
  | `src/view/Mat4.hpp`, a mutated file | M1-09 to M1-16, M1-90 |
  | `tests/test_camera.cpp`, a test | M1-11, M1-90 |
  | `tests/OrbitTestSupport.cpp`, a test helper | all but M1-88 and M1-89 |
  | `src/core/Scalar.hpp`, code between mutant and test | the same 12 |
  | `shaders/line.vert`, read at run time | M1-13 to M1-16, M1-88 to M1-90 |
  | `README.md` | none |

  M1-90 is due for any source because its check runs every suite. M1-88 and
  M1-89 are due for a shader only because their no-op build target is
  `orbsim_shaders`, which is harmless and on the safe side.
- [x] With no change, nothing is due. Against the seeded record everything was
      due, because `CMakeLists.txt` and the harness changed since.
- [x] Tests in `check`:
  - the self-test judges eight cases;
  - three real runs against the tree, each seen failing when its expectation
    was wrong:
    - a header between (`mutants_due`);
    - a shader (`mutants_due_shaders`);
    - a script on a command line (`mutants_due_scripts`).
- [x] A mutation pass on the script, `scripts/mutants/m1-92.json`, run after
      this task's commit.
  - The first run: 5 caught, 2 survived. One survivor was a real hole, closed
    by a test that must fail; the other is equivalent and was declared on the
    owner's ruling (decision 218).
  - The second run: 6 caught, plus the 1 declared survivor.
- [x] **The rule applied to itself.** Every file due after this commit was run,
      and its pass recorded; then again after M1-94 (decision 219), ending with
      0 of 15 due.
- [x] `check` passes in both trees.
