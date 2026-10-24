# M1-133 — The selection proved, and measured

Phase: B | Status: **done 2026-10-24**
Prerequisites: M1-132
Decided by: register decisions 543, 552, 553 and 561, ruled 2026-10-15, and
592, ruled 2026-10-16; split out of M1-129 as its item 9's step E by decision
596. And 720-734, ruled 2026-10-21 before its work started: the push before
it, the sequence, the trees, the code and the tests of the mutant check, a
write that Windows refuses, what the replay adds to each commit and how, its
tree, how it counts and the causes, the figures and where they go, no mutant
file, and the scratch folders. And 737-761, ruled during the work
from 2026-10-21 to 2026-10-24. They rule the correction of open item (h), its
mutant file and M1-137. They also rule the base of the replay, how it counts,
Part 2, the data of the replay, a fact of `STATUS.md` and the hand sort.
Measures [M1-129](m1-129-tests-by-fingerprint.md)'s whole-program
fingerprints and [M1-132](m1-132-function-level-selection.md)'s function-level
selection; the spike it is measured against is in M1-132's last section.

## Purpose

A skipped test is safe only if it could not have failed. This task shows, on
real changes and real faults, that the selection never skips a test that would
have failed, and measures what it saves.

## What to do

In this sequence since decision 721:

1. **`check-full` in the two trees** at `fddeefc`, with M1-131 and M1-132,
   before the owner's push (decision 720).
2. **The mutant check** (552), on today's tree, in the two `check` trees
   (722). For each mutant of `scripts/mutants/m1-118.json`: get the
   selection, run all tests, and make sure that each test that fails is a
   selected test. The script is `docs/measurements/m1-133/mutant-check.py`
   (723); it runs all 590 CTest tests from the store of a `check` at its
   commit (724), and it is seen to find a planted fault before its results
   are trusted (733). A write that Windows refuses runs that mutant again
   (725). Estimated: 2 to 2.5 hours in Debug and 1 to 1.5 hours in
   RelWithDebInfo (722).
3. **M1-118's 16 commits replayed** in `build/wt133`, in Debug only (728):
   one commit on `374745f~1` with the items of decision 726 -- steps A and B,
   the hook, the recorder, the linker map, `ORBSIM_TEST_NAME`, reproducible
   builds and the read records -- and the 16 commits copied on top with
   `git cherry-pick`, each copy compared with its initial commit (727). The
   driver runs at each commit, and a script that uses its code counts the
   whole-program selection and the function-level one, and must agree with
   the driver's (729). The causes are decision 730's eight, and
   the figures are decision 731's: the C++ tests against the spike's 18%
   (543), and all tests against the simulation's 16% and 26% (483, 490).
   About three to four hours, estimated.
4. **The figures** in `docs/measurements/m1-133-selection-proved.md`, in a
   dated section at the end of ADR 0028 (719), and their totals in
   `STATUS.md` with a link (732).

## Where it ended (2026-10-24)

- **The mutant check**, in the two trees: no fault (Part 1 of
  [the measurement record](../../measurements/m1-133-selection-proved.md)).
- **Open item (h)**, the failure of `mutants_due_probe_shaders` for a cause
  that is not the change, corrected in `scripts/run-tests.py` (decisions
  737-742), seen to fail first, the mutant file `m1-133.json` 3 of 3, and
  measured again with mutants 10 and 19 in the two trees.
- **The replay** of the 16 commits of M1-118, in Debug: by function, 434 runs
  of a C++ test against 1,396 by program (31.1 %). The time was 468.23 s
  against 932.53 s (50.2 %). The figures were examined against the files of
  the replay on 2026-10-24 (decisions 757-761). Part 2 of the measurement
  record and the dated section of ADR 0028 hold them.
- **The scratch folders**: build/wt133, its branch, `build/wt129` and the
  scratch folder of the replay were removed on 2026-10-24 (decisions 734 and
  759). Before the removal, the work in `build/wt129` that was not committed
  was compared with `4495ea0`: the same changes, and the same bytes.
- **`check`** passed in the two trees on 2026-10-24, and `check-full` at
  `9253e87`. The owner pushed `9253e87` the same day at 07:03, from a terminal
  of their own.

## Then

Points 3-5 of M1-129's "The order from here": mutation testing at function
level, which is [M1-134](m1-134-mutants-chosen-like-tests.md) since
2026-10-18 (decision 633), written again from the start on 2026-10-21
(decisions 735 and 736), and which comes after
[M1-137](m1-137-selection-limits.md), the limits of the test selection
(decision 744); working agreement 9 applied to the
configure step, the tests run in both trees and the linting; and whether
agreement 9 covers the documentation -- each of the last two may become a
task of its own, decided when it is taken up.

## Done when

- [x] All the questions of the work ruled, and the rulings in the register.
      *Decisions 720-734, 2026-10-21, and 737-761 during the work.*
- [x] No mutant of `m1-118.json` made a test fail that the selection skipped,
      in the two `check` trees (722). *2026-10-21: 0 faults in Debug and in
      RelWithDebInfo, seen to find a planted fault first; Part 1 of
      [the measurement record](../../measurements/m1-133-selection-proved.md).*
- [x] M1-118's 16 commits measured by program and by function, with the
      causes, and written in the measurement record, ADR 0028 and
      `STATUS.md` (732). *2026-10-24: Part 2 of the record, decisions
      757-761.*
- [x] `check` passes in the two trees. *2026-10-24.*
- [x] The scratch folders removed as decision 734 says: `build/wt133` and its
      branch, and `build/wt129`; and the scratch folder of the replay
      (decision 759). A commit at the end; the push only on the owner's
      go-ahead (595). The mutant file `m1-133.json` came with the correction
      of decision 737 (decision 740). *2026-10-24.*
