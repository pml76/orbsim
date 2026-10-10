# M1-123 — The worked example brought up to the project's rulings

Phase: B | Status: not started
Prerequisites: M1-122
Decided by: register decisions 445 and 450, ruled 2026-10-10 on the review's
group 6 ([the findings](../../review/m1-116-findings.md))

## Purpose

`.claude/rules/cpp-style.md` tells a reader to copy the shape of
`coding-guidelines-example/`, and the example has fallen behind the project:
a test that reads `error()` without a guard that stops it, unit types that
accept any value, and documents describing code that no longer exists.

## What to do

- **6.1** the parabolic refusal checked in one condition, as the example's
  `test_orbit_path.cpp` already does.
- **6.2** `Eccentricity` and `GravParam` validate themselves (ADR 0022); the
  deleted conversion narrowed to arithmetic targets (decision 138); its
  ADR 0002 and README describe the project as it is; `wrapToPi` keeps to
  (-π, π]; its suppression count is true; and the instruction to copy the
  example says what to copy for a test.
- The example's own build, lint and checks run clean, as its README claims.

## Done when

- [ ] Every finding above fixed in the example.
- [ ] The example builds with zero warnings and zero lint findings, and its
      checks pass; `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.
