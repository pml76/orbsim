# M1-88 — The lint pool, sized from memory

Phase: A | Status: **done, 2026-09-27**
Prerequisites: none. Runs **before M1-17**, first of M1-88 to M1-94 (decision 207)
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 208

*(Since M1-96, 2026-09-28, the pool is `orbsim_memory` and holds this
project's own compiles too; the script is `scripts/check-memory-pool.py` and
its tests `memory_pool` and `memory_pool_self_test`. What follows is M1-88 as
it was done.)*

## Purpose

**A `check` should not fail because the machine ran out of memory.**

- Each clang-tidy process needs about 1.04 GB on this project.
- Ninja's default, one job per thread plus two, asked for about 22 GB on the
  32 GB development machine.
- On 2026-09-27 that ran the Debug lint out of memory beside an open IDE
  ([`../../measurements/verification-cost.md`](../../measurements/verification-cost.md)).

A lint step killed that way fails loudly, so no bug gets through. But the run
has to be repeated in full, and a `check` that fails for no reason teaches
people to distrust failures.

## What to do

- In `CMakeLists.txt`:
  - read the physical memory at configure time
    (`cmake_host_system_information`);
  - declare a Ninja pool `orbsim_lint` of `floor(MiB / 3072)` jobs, at least
    one;
  - put every lint step in it, both the per-file lint and `--verify-config`;
  - leave compiling at Ninja's default: every from-scratch compile fitted.
- Add `scripts/check-lint-pool.py`, which:
  - reads the generated Ninja files;
  - requires every lint step to be in the pool;
  - computes the depth itself, from the operating system's own figure for
    physical memory, so it is a second implementation of the rule, not a copy
    of it.
- Register the script as the CTest test `lint_pool`, and its `--self-test` as
  `lint_pool_self_test`.

## Done when

- [x] `lint_pool` failed on the tree before the change, for the right reason:
      "no pool named orbsim_lint", and 55 lint steps outside it.
- [x] The self-test reports every broken input it is given: a step outside the
      pool, the wrong depth, no pool, no lint steps.
- [x] CMake and the script agree independently: 32,208 MiB gives 10.
- [x] Observed live: a cold `lint` never ran more than 10 clang-tidy processes
      at once. That run took 521 s; this machine's run-to-run noise is up to
      27 %, and the gap from the 313 s measured earlier is recorded, not
      explained.
- [x] `check` passes in both trees.
