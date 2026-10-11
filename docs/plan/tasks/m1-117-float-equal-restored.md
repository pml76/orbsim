# M1-117 — `-Wfloat-equal` back on, and the eight comparisons it hid

Phase: B | Status: **done, 2026-10-11**
Prerequisites: M1-116
Decided by: register decisions 443 and 450, ruled 2026-10-10 on the review's
finding 4.1 ([the findings](../../review/m1-116-findings.md)); refined by
decisions 451-454 the same day, after the measurements below

## Purpose

**A one-word slip in mp-units v2.5.0 switches `-Wfloat-equal` off for the
rest of every file that includes it**: `operator==` in its
`framework/quantity.h` closes with a second diagnostic `PUSH` where a `POP`
belongs. Almost every file in `src/` and `tests/` includes it through
`core/Units.hpp`, so the warning the project relies on (CODING_GUIDELINES
section 11, ADR 0017) has been checking nothing there -- and eight
floating-point `==` comparisons went unseen. **First of the review's tasks**,
because it changes what the compiler checks for every task after it.

## What was measured before the rulings

*(2026-10-10, with the Debug tree's flags and `linux-gcc`'s, on two-line
files. The task as first written said five comparisons and proposed
`#pragma ... warning`; both were wrong, and decisions 451-453 say what
replaced them.)*

- **The slip leaves three warnings off, not one.** mp-units' "ignore" macro
  also switches off `-Wunknown-pragmas` and `-Wunknown-warning-option`
  (gcc: `-Wunknown-pragmas` and `-Wpragmas`), so after `core/Units.hpp` a
  misspelt warning name in a pragma of ours was ignored in silence.
- **`#pragma clang diagnostic warning "-Wfloat-equal"` does not fail the
  build**, under either compiler: `-Werror` does not promote a warning a
  pragma switched on. A plain `push`/`pop` around the include changes
  nothing; one `pop` after it is not enough; **two restore all three
  warnings, as errors**. Under clang a third `pop` fails the build ("no
  matching push"); gcc accepts it silently.
- **Eight comparisons, not five.** With a corrected copy of the header, clang
  reports five and gcc three more -- `static_assert`s against a literal clang
  holds exactly and so exempts, which gcc does not.
- **mp-units fixed it upstream on 2026-03-08**, commit `0eea272e`, and has
  not released since v2.5.0.
- **Microsoft's compiler is not affected**: its "ignore" macro for this
  warning is empty, so the stray pushes change nothing there.

## What to do

- **Two diagnostic `pop`s straight after mp-units' headers in
  `core/Units.hpp`**, clang's and gcc's each under its own compiler test,
  with the reason and the measurements above written beside them
  (decision 451).
- **Fix the eight comparisons**, each to the comparison it means
  (decision 452):
  - `core/Units.hpp`: the `static_assert`s on `Eccentricity{}`,
    `eccentricity(0.7306)`, `gravParam(3.986004418e14)` and
    `fraction(0.25)` -- to `bitsOf`;
  - `tests/test_earth_orientation.cpp`, the pole of date's matrix rows,
    claimed bit-for-bit -- to `bitsOf`;
  - `tests/test_planetary_grid.cpp`, the `static_assert` on
    `kSpacingAtRadius` -- to `bitsOf`;
  - `fixtureMatrixAtEpoch` in `tests/test_planetary_grid.cpp` and
    `fixtureMatrix` in `tests/test_probe_grid.cpp`, which compare a day held
    as a double -- to the time type's own `==`, since the claim is "the same
    instant".
- **Prove the instrument** (decision 454): a CTest test compiles a guard file
  that compares two doubles after `core/Units.hpp`, and passes only on the
  `-Wfloat-equal` error by name, in both trees. The `linux-gcc` tree builds.
- **Record the upstream fix in `THIRD_PARTY.md`** (decision 453): commit
  `0eea272e`, and the condition for removing the local fix -- the first
  mp-units release containing it, which clang then enforces.

## Mutants

[`m1-117.json`](../../../scripts/mutants/m1-117.json):

- One `pop` removed, and both: caught by the guard test.
- Each of the eight sites put back as `==`, judged by the warning itself --
  a kind of kill `mutate.py` learnt for this task (decision 462). Five are
  caught in the clang trees; the three against a literal clang holds exactly
  are declared survivors, caught under gcc and measured there once
  (decision 463).
- The guard's verdict, and the harness's new kill, by their self-tests.

**Raised during the work, and ruled the same day**: the harness counted a
build that fails on a warning as INVALID, so it learnt to count one by name
(decision 462); and three of the eight cannot fail under clang (decision
463).

## Done when

- [x] The warning fires after `core/Units.hpp`, shown by a file that must not
      build.
- [x] The eight comparisons fixed; `check` passes in both trees, and the
      `linux-gcc` tree builds.
- [x] The upstream fix and the removal condition recorded in
      `THIRD_PARTY.md`.
- [ ] The task's mutant file has run after the commit and its record is
      committed.
