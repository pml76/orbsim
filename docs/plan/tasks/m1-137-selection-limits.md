# M1-137 — The limits of the test selection, measured and closed

Phase: B | Status: not started; the next task after M1-138 (decisions 744 and
763)
Prerequisites: M1-133
Decided by: register decision 744, ruled 2026-10-21 on the owner's word, after
the test selection was compared with published work: Ekstazi
(Gligoric and others, 2015), studies of static and method-level selection,
and Bazel's test cache.
Builds on [M1-129](m1-129-tests-by-fingerprint.md)'s fingerprints and read
records, [M1-132](m1-132-function-level-selection.md)'s selection by
function, and [M1-133](m1-133-selection-proved.md)'s proof and its decision
737.

## Purpose

Published work agrees with the method of ADR 0027 and ADR 0028. It also names
the conditions in which a selection of tests is not safe. Some of these are
the limits that ADR 0028 gives, and at this time the full run before each push
is the only check that finds them. This task measures each limit, closes the
limits that can be closed, and names the others where the selection is called
safe.

## What to do

1. **Coverage that changes between runs** (decision 707). In each
   `check-full`, the driver compares the functions that each test entered with
   the functions of its last pass. A difference is a warning that names the
   test and the functions. A test with a difference can get a rule of its
   own: by program, or always run.
2. **Tests that read the outputs of other tests** (decision 508). A test that
   reads an output of a different test gets, in its fingerprint, the
   fingerprint of the test that wrote that output. If the writer runs again,
   the reader runs too, and CTest's fixtures run the writer first. Decision
   737 corrects one example of this type; this item closes the type.
   *Added 2026-10-27 (decision 826):* the store of the driver,
   `<tree>/run-tests/fingerprints.json`, is an output of the same type,
   which the driver writes. The `mutants_due` tests read it. In each run, some of
   them keep the rules of decisions 500, 510 and 531, and these are not the
   same tests between the trees and between runs (decision 826, corrected).
   The store is not an input of these rules, thus a change of the store
   alone does not make them run. This item examines them too.
3. **The read records compared again.** At each phase gate, the read records
   are compared again with the files that the tests actually open (decision
   612, Process Monitor), with the reads of child processes.
4. **The environment part, measured** (decision 501). Run the suites with
   other variables changed -- `TEMP`, the locale, `TZ`, the proxy variables --
   and record each result that changes. Only then rule which variables go into
   the hash.
5. **The limits named.** One line in `docs/BRIEFING.md` and in `CLAUDE.md`
   gives the limits of ADR 0028 -- start-up code, coverage that changes between
   runs, outputs of other tests -- and the full run that finds them.
6. **The `.pdb` files of the programs** (decision 833, added 2026-10-29).
   In Debug, seven programs read their own `.pdb` file, in 7 script tests,
   because an assertion prints its call chain. The part of the programs
   does not hold the `.pdb` file. Measure if a change can change a `.pdb`
   file and keep the bytes of its program, then close the gap or name it.

## Questions to put to the owner before it is built

Each with its options, costs and a recommendation when the task is taken up:

1. Item 1: a warning, or a failure of `check-full`; and the rule for a test
   with a difference.
2. Item 2: how the driver finds which test wrote a file (the read records,
   the fixtures, or a list), and the mutant files that it makes due.
3. Item 3: the gate checklist that holds it, and its time.
4. Item 4: which variables to measure, and in which trees.

## Done when

- [ ] All the questions above, and each question of the design, ruled before
      its code.
- [ ] Items 1 to 5 done, each check seen to fail first.
- [ ] `check` passes in the two trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed. A commit at the end; the push only on the owner's go-ahead.
