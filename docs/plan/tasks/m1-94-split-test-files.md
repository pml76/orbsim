# M1-94 — Split the two slowest test files

Phase: A | Status: **done, 2026-09-27**
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

- [x] **Suite by suite, the case and assertion counts after the split add up
      exactly to those before, and the case names are identical**, from a
      script that moved the sections by their marker lines rather than by hand:

  | Before | After |
  |---|---|
  | `test_time`: 772,933 assertions in 47 cases | `test_time` 420,790 in 20 + `test_time_leap` 105,793 in 12 + `test_time_ut1` 246,350 in 15 |
  | `test_orbit_scales`: 85,672 in 30 | `test_orbit_scales` 3,600 in 21 + `test_orbit_elements` 82,072 in 9 |

- [x] **What the suites share moved into two headers**, `tests/TimeTestSupport.hpp`
      and `tests/OrbitSweepSupport.hpp`. Each is added to the test header
      self-check. The helpers' anonymous namespaces became named ones, which a
      header needs, and their non-template functions became `inline`. The
      leap-second section's two helper blocks moved too, because the UT1 cases
      use them. The one approved `NOLINTNEXTLINE`, on each sampler, moved with
      its line; no new suppression was written.
- [x] **The seeds are unchanged**, 20260910 and 20260905, now in the two headers,
      and every case still builds its own `Sampler`, so no draw changed.
- [x] **Lint clean.** The 31 findings after the move were all includes a new
      file no longer used, and all were removed.
- [x] **Every reference that pointed at a moved case was corrected**:
  - ten code comments: two in `Orbit.hpp`, one in `Orbit.cpp`, three in
    `LeapSeconds.hpp`, and four across three test files;
  - `VERIFICATION.md` rule 12's seed location;
  - M1-87's mutant file, whose seven mutants judged by `test_orbit_scales` now
    name `test_orbit_elements` too.
- [x] `STATUS.md`'s suite table is updated, in the closing records commit.
- [x] `check` passes in both trees.
- [x] **The full mutation rerun** (decision 219). It ran after this commit,
      because `mutate.py` records a pass only for committed code, and nothing
      was pushed before it had run clean.
  - All 15 files matched their expected results, and every pass is recorded at
    this commit.
  - One M1-87 mutant first reported "hung", because its build straddled the
    machine's overnight sleep. Run again awake, the file caught 12 of 12.
