# M1-91 — Tests in parallel

Phase: A | Status: planned
Prerequisites: M1-90
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 211

## Purpose

**`check` runs its tests one at a time**: 48–52 s in release and 103–113 s in
Debug, of which 21 cases over one second account for most. Nearly every case
is independent and CPU-bound.

## What to do

- At configure time, set `ORBSIM_TEST_JOBS` to half the processor threads, at
  least one; `check` runs `ctest -j ${ORBSIM_TEST_JOBS}`.
- Give every test that needs the GPU `RESOURCE_LOCK gpu`, so no two of them
  run together:
  - `orbsim_smoke`;
  - the probe tests;
  - `shader_missing_is_reported`;
  - the `test_probe_clear` cases.
- Check that no two tests share a file.
  - The probe tests write to separate folders, and CTest's fixtures order
    them.
  - `test_image_files` gives each case its own scratch file.
  - Those scratch names are shared across trees, so the two trees' `check`
    runs stay one after the other.

## Done when

- [ ] A test lists the GPU tests and requires each to hold the lock. It was
      seen failing first on a GPU test without it.
- [ ] Twenty parallel runs per tree, each case's result the same as a serial
      run's.
- [ ] The time before and after, recorded, and the unexplained 10-second
      stalls looked at again.
- [ ] `check` passes in both trees.
