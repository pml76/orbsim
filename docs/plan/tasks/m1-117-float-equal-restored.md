# M1-117 — `-Wfloat-equal` back on, and the five comparisons it hid

Phase: B | Status: not started
Prerequisites: M1-116
Decided by: register decisions 443 and 450, ruled 2026-10-10 on the review's
finding 4.1 ([the findings](../../review/m1-116-findings.md))

## Purpose

**A one-word slip in mp-units v2.5.0 switches `-Wfloat-equal` off for the
rest of every file that includes it**: `operator==` in its
`framework/quantity.h` closes with a second diagnostic `PUSH` where a `POP`
belongs. Almost every file in `src/` and `tests/` includes it through
`core/Units.hpp`, so the warning the project relies on (CODING_GUIDELINES
section 11, ADR 0017) has been checking nothing there -- and five
floating-point `==` comparisons went unseen. **First of the review's tasks**,
because it changes what the compiler checks for every task after it.

## What to do

- **Switch the warning back on after mp-units' headers in
  `core/Units.hpp`**, for clang and gcc, with the reason written beside it
  (decision 443); measure first that a plain push and pop around the include
  cannot do it against an unbalanced header, and say so there.
- **Fix the five comparisons** the review found with a corrected copy of the
  header: the `static_assert` on `eccentricity(0.7306)` in
  `core/Units.hpp`; a `REQUIRE` in `tests/test_earth_orientation.cpp`; a
  `static_assert` and `fixtureMatrixAtEpoch` in
  `tests/test_planetary_grid.cpp`; `fixtureMatrix` in
  `tests/test_probe_grid.cpp` -- each to `bitsOf` or `bitIdentical`, or to
  the comparison it means.
- **Prove the instrument**: a file comparing two doubles with `==` after
  `core/Units.hpp` must now fail to build, in both trees and under gcc.
- **Report the fault to mp-units**, with the two-line reproduction; record
  the report's link in `THIRD_PARTY.md`, and the condition for removing the
  local fix.

## Mutants

- The re-enabling pragma removed: caught at compile time by a guard written
  for it, which compares two doubles where the warning must fire.
- Each of the five sites put back as `==`: caught at compile time.

## Done when

- [ ] The warning fires after `core/Units.hpp`, shown by a file that must not
      build.
- [ ] The five comparisons fixed; `check` passes in both trees, and the
      `linux-gcc` tree builds.
- [ ] The upstream report filed and linked.
- [ ] The task's mutant file has run after the commit and its record is
      committed.
