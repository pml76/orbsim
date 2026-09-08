# M1-74 — Phase E gate

Phase: E | Status: not started
Prerequisites: M1-62 … M1-73

## Purpose

Phase E is where this project's central claim now lives: that the simulation is
right for reasons that can be checked, against data it did not produce. This
gate is where that is confirmed with everything the toolbox has.

## What to do

The full sweep from M1-61 — ASan, Linux clang with ASan and UBSan, gcc-14, TSan,
GPU validation layers — plus:

- **Five fuzz targets**: `fuzz_orbit`, `fuzz_ktx2`, `fuzz_ztree`,
  `fuzz_elevation` and `fuzz_integrator`. The last gets ten minutes.
- **A long-arc run**: 30 days of simulated time at high acceleration, with the
  monitors live in a Debug build, watching for a drift the 24-hour tests cannot
  see. Energy drift, angular-momentum drift and the embedded error estimate are
  recorded as curves, not endpoints, because the *shape* of a drift says whether
  it is truncation error or a bug.
- **The GMAT comparison re-run** after everything in the phase, since several
  tasks after M1-68 touch the path.

## What to check, beyond "it passed"

- **gcc-14 and clang agree on the integrator.** This is the densest
  floating-point code in the project, and the second toolchain has already found
  an unstable algorithm here once. If the two disagree beyond their expected
  rounding difference, that is a finding, not a tolerance to widen — the working
  agreement is explicit, and the last time this happened the correct answer was
  a better algorithm.
- **The accuracy budget still holds** for both propagators, and the numbers in
  `PROJECT_STATE.md` match what the suite reports today rather than what it
  reported in M1-68.
- **Coverage** of `src/orbit/` and `src/sim/`. `Orbit.cpp` was at 99.2 % lines
  before this phase; the new force model, steppers and propagators should be
  comparable, and any uncovered line in an integrator is a path no test has
  taken.
- **The monitors are quiet** through the long-arc run, with margins recorded.

## What to record

In `PROJECT_STATE.md`: assertion counts per toolchain; five fuzzing totals; the
GMAT budget as measured for Cowell and Encke; the chosen fixed step and its
justification; the long-arc drift curves; the coverage table; and the
measurements that justified the design — the step size each propagator needs for
1 m, and Encke's precision advantage.

In `VERIFICATION.md` Part 4: rules 3, 4 and 15 all move off **to build**. That
completes the three the document names as remaining, which is worth stating
plainly in the commit message.

## Done when

- [ ] Every toolchain passes, counts matching, and the two compilers agree.
- [ ] Five fuzzers clean.
- [ ] The 30-day arc is clean, with drift curves recorded.
- [ ] The GMAT budget holds for both propagators.
- [ ] `VERIFICATION.md` has no rows left marked **to build**.
