# M1-96 — Compiles and lint share one memory-sized pool

Phase: A | Status: **done, 2026-09-28**
Prerequisites: M1-88, M1-95
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 222 and 223

## Purpose

**A full rebuild ran `check` out of memory on 2026-09-27.** M1-88 limited the
lint steps to one job per 3 GiB of physical memory, 10 here. During a near-full
rebuild, though, about 20 of this project's compiles (0.6–1 GB each, mp-units
being most of it) run beside those 10 lint jobs. With an IDE open, that is
more than the machine holds.

## What to do

- Rename M1-88's pool from `orbsim_lint` to `orbsim_memory`, keeping its depth,
  `floor(physical memory / 3 GiB)` and at least one.
- Put this project's own compiles in the same pool: every library and program
  target defined in the top-level `CMakeLists.txt`, through the
  `JOB_POOL_COMPILE` property. Together with the lint steps, at most about
  10 GB of heavy processes then run at once.
- Third-party libraries built in their own directories keep Ninja's default:
  they are small, and pooling them would slow fresh builds for no memory
  gain.
- Extend the pool's check. It requires every lint step, and every compile of a
  top-level target, to be in the pool. It is renamed with the pool:
  `check-memory-pool.py`, as the CTest tests `memory_pool` and
  `memory_pool_self_test`.

## Done when

- [x] **The check was seen failing first**, for the right reasons: "no pool
      named orbsim_memory", the lint steps still in the old pool, and all 345
      compiles of this project outside it. Its self-test found a flaw in the
      new check before it was trusted: `notlint/a.ok` was read as a lint step,
      because `lint` was not matched as a whole directory name. Fixed; the
      self-test now judges seven cases right.
- [x] **A clean rebuild of the release tree through `check`, at full
      parallelism, with no `CMAKE_BUILD_PARALLEL_LEVEL`**: 807 s, 279 of 279.
      Sampled every second, **never more than 10 heavy processes at once**,
      the pool's depth: 10 of this project's compiles, with 5 dependency
      compiles beside them at that moment.
  - The comparison is not like for like. The unpooled from-scratch compile
    measured 190-232 s on 2026-09-27 and excluded lint and tests; this run is
    compile, lint and tests together.
  - **Lowest free memory: 572 MiB.** A VMware virtual machine was running and
    holding 14.7 GB, leaving 2.8 GB free at rest. The pool is sized from total
    physical memory, so it cannot know that half of it is taken. Within what
    was left, 10 heavy processes fitted, barely. This is a limit of the rule,
    recorded as one. The owner closed the VM for the remaining runs.
- [x] `check` passes in both trees, 279 of 279: release after the clean
      rebuild, Debug in 180 s with the pool alone and free memory never below
      6.9 GB.
- [ ] One full mutation rerun after this commit (decision 223).
