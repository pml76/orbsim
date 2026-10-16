# M1-133 — The selection proved, and measured

Phase: B | Status: not started
Prerequisites: M1-132
Decided by: register decisions 543, 552, 553 and 561, ruled 2026-10-15, and
592, ruled 2026-10-16; split out of M1-129 as its item 9's step E by decision
596.
Measures [M1-129](m1-129-tests-by-fingerprint.md)'s whole-program
fingerprints and [M1-132](m1-132-function-level-selection.md)'s function-level
selection; the spike it is measured against is in M1-132's last section.

## Purpose

A skipped test is safe only if it could not have failed. This task shows, on
real changes and real faults, that the selection never skips a test that would
have failed, and measures what it saves.

## What to do

1. **`check` in both trees**, with M1-131 and M1-132 in place.
2. **M1-118's 16 commits replayed** in a scratch worktree, with M1-129's steps
   A and B and M1-131's hook and recorder laid over each commit -- the
   assertion message, `tests/TestCase.hpp` with each test file's include
   switched to it -- and the real driver run on the result (592), counting
   both the whole-program selection, against the simulation's 26% (decision
   483), and the function-level one, against the spike's 18% (543), split by
   cause (561). About three to four hours, estimated.
3. **The mutant check** (552), on today's tree: for each mutant of
   `scripts/mutants/m1-118.json`, the selection computed, every test run, and
   every test that fails required to be among those selected. About one
   mutation pass of time.
4. **The figures in `STATUS.md`.**

## Then

Points 3-5 of M1-129's "The order from here": mutation testing at function
level, working agreement 9 applied to the configure step, the tests run in
both trees and the linting, and whether agreement 9 covers the documentation
-- each may become a task of its own, decided when it is taken up.

## Done when

- [ ] No mutant of `m1-118.json` made a test fail that the selection skipped.
- [ ] M1-118's 16 commits measured both ways, split by cause, and written in
      `STATUS.md`.
- [ ] `check` passes in both trees.
- [ ] Any scratch the measurements made is removed. A commit at the end; the
      push only on the owner's go-ahead (595).
