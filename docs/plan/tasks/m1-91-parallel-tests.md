# M1-91 — Tests in parallel

Phase: A | Status: **done, 2026-09-27**
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

- [x] **`parallel_tests` was seen failing first**, for the right reasons: all 7
      GPU tests without the lock, and "the check target's ctest command runs
      the tests one at a time (no -j)". Its self-test reports each of five
      broken inputs. The three `test_probe_clear` cases take their lock only
      once the program is rebuilt, because Catch2's test discovery bakes the
      properties in.
- [x] **Twenty parallel runs per tree, each case's result the same as a serial
      run's**, at `-j 10`:

  | Tree | Serial | Parallel, 20 runs | Runs differing |
  |---|---|---|---|
  | release | 26.8 s | median 15.5 s, 14.5–17.2 | 0 of 20 |
  | Debug | 65.9 s | median 36.5 s, 33.7–41.0 | 0 of 20 |

- [x] **The unexplained stalls.** No small case stalled for about 10 s in any
      of the 40 parallel runs. The slowest case in each run is now a GPU probe
      under its lock: 3.8–4.7 s in release and 9.2–11.5 s in Debug.
- [x] `check` passes in both trees, 274 of 274. The whole target took 52 s in
      release and 100 s in Debug after this change.
