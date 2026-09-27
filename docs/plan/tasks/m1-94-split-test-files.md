# M1-94 — Split the two slowest test files

Phase: A | Status: planned
Prerequisites: M1-89, M1-91
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 214

## Purpose

`tests/test_time.cpp` (2,563 lines) and `tests/test_orbit_scales.cpp` (2,606)
are among the longest compiles (25–67 s) and lint steps (83–164 s). With many
steps in parallel, the slowest single step sets the minimum time.

`tests/test_fixture_file.cpp` stays whole. It is 867 lines and slow because of
the headers it includes, so splitting it would add work, not remove it.

## What to do

- Split `test_time` into three suites:
  - core time;
  - the leap-second table (M1-04);
  - UT1 and DeltaT (M1-05, M1-86).
- Split `test_orbit_scales` into two:
  - scales and properties;
  - the element-accuracy half, with its 60-digit tables.
- Move helpers they share into a test header. Every case moves unchanged.
- Every case builds its own random generator (checked 2026-09-27), so no
  sweep's draws change.

## Done when

- [ ] Suite by suite, the case and assertion counts after the split add up
      exactly to those before.
- [ ] The seeds are unchanged.
- [ ] `STATUS.md`'s suite table is updated.
- [ ] `check` passes in both trees.
