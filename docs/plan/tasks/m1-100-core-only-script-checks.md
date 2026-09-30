# M1-100 — The script checks in a tree that builds the core only

Phase: A | Status: not started
Prerequisites: M1-91, M1-92
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 243, 244 and 246

## Purpose

**Four CTest tests fail in both Linux trees**, `linux-sanitize` and
`linux-gcc`, which build the core only (`ORBSIM_BUILD_APP` off). Found by
M1-17's run of the other compilers on 2026-09-29 and older than it
([M1-17](m1-17-golden-images.md), "Other compilers").

**Reproduced first, on 2026-09-30**, in `build/linux-sanitize`:

- `mutants_due`, `mutants_due_shaders` and `mutants_due_scripts` stop with a
  Python traceback: `ninja -t inputs orbsim` returned exit status 1. Several
  mutant files are judged by the application and its tests, and a tree that
  does not build the application cannot say what those depend on.
- **Found on the way**: `mutants_due_unmet_expectation_fails` is marked
  "expected to fail", so in these trees it *passes* -- because the script
  crashes, not because the expectation failed. A false pass.
- `parallel_tests` refuses: "no test is labelled gpu -- the check would be
  checking nothing". That refusal exists on purpose, but in a core-only tree
  no GPU test is what is right.

The Linux trees have a `check` target too, running `ctest -j 10`, so the rule
`parallel_tests` holds -- tests run in parallel -- matters there as well.

## What to do

As decided (decision 246):

- **The four `mutants_due` tests that read the tree are registered only where
  the application is built**, as M1-17 already does for `accept_golden`.
  Mutation passes run in a tree that builds everything, so the core-only trees
  lose nothing they could use. `mutants_due_self_test` reads no tree and stays
  in every tree.
- **`parallel_tests` is told what the tree builds**:
  `check-parallel-tests.py … --gpu-tests expected|none`, from
  `ORBSIM_BUILD_APP`, with no default.
  - `expected`: as before -- at least one GPU test, each holding the `gpu` lock.
  - `none`: a test labelled `gpu` is the fault, since nothing in the tree
    could run it.
  - Both: the `check` target's `ctest -j N` is checked as before.
- **Self-test first**: four new cases for a core-only tree -- all correct, a
  GPU test, no `-j`, the wrong N -- seen failing before the change for the
  right reason (the correct core-only listing reported, the GPU test accepted).

Considered and not taken:

- Teaching `mutants-due.py` to skip mutant files whose judges the tree does
  not build. A filter that drops findings looks exactly like a clean result.
- Registering `parallel_tests` only where the application is built. The Linux
  trees would stop checking that their tests run in parallel.

**Mutants**: `scripts/mutants/m1-100.json`, four of them (decision 244). The
opposite of the registration change -- the `mutants_due` tests registered in
every tree -- changes nothing in a tree that builds the application, where the
harness runs, so it is judged instead by the core-only trees' own runs below.

## Done when

- [ ] `parallel_tests_self_test` was seen failing on the two core-only cases
      that describe the fault, before the change.
- [ ] In `build/linux-sanitize` and `build/linux-gcc`: the four `mutants_due`
      tests are not registered, `mutants_due_self_test` and `parallel_tests`
      pass.
- [ ] `check` passes in both Windows trees, where the `mutants_due` tests and
      `parallel_tests` run as before.
- [ ] `scripts/mutants/m1-100.json` passes.
