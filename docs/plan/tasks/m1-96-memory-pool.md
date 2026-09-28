# M1-96 — Compiles and lint share one memory-sized pool

Phase: A | Status: planned
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

- [ ] The check was seen failing first, on compiles outside the pool.
- [ ] A clean rebuild of a tree runs without `CMAKE_BUILD_PARALLEL_LEVEL`, and
      never more than the pool's depth of heavy processes; its time is recorded
      against the unpooled measurement.
- [ ] `check` passes in both trees.
- [ ] One full mutation rerun after this commit (decision 223).
