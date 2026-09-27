# M1-92 — When an older mutant file runs again

Phase: A | Status: planned
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

- Each mutant file records the commit at which it last passed, in a new field
  `passed_at` that `scripts/mutate.py` writes after a clean run.
- `scripts/mutants-due.py <tree>` lists every mutant file that is *due*. A file
  is due when any file its judging programs depend on has changed since
  `passed_at`:
  - the judging programs are the `suites` and the programs behind the `ctest`
    entries it names;
  - their inputs come from Ninja's own records (`ninja -t inputs` and
    `ninja -t deps`);
  - the changes come from `git diff --name-only <passed_at>`.
- **The rule**: a task re-runs every file the script lists before its commit.
  The full pass runs at every gate.

## Done when

- [ ] Planted changes, each listing exactly the files expected:
  - to a mutated file;
  - to a test;
  - to a test helper (`tests/OrbitTestSupport.cpp`);
  - to production code between the mutant and its test.
- [ ] With no change, nothing is due.
- [ ] A self-test as a CTest test, seen failing first.
- [ ] A mutation pass on the script.
- [ ] `check` passes in both trees.
