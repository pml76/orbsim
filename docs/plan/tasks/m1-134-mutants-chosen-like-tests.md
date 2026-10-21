# M1-134 — Mutation passes chosen the way the tests are

Phase: B | Status: not started; the next task after M1-133 (decision 736)
Prerequisites: M1-133
Decided by: register decision 633, ruled 2026-10-18, and 634, ruled the same
day after a spike. Postponed on 2026-10-21 (decision 735), and written again
from the start the same day, on the owner's rules R1 to R13 below, with
SQLite for the store (decision 736). The text before that date is in git.
Builds on [M1-131](m1-131-coverage-hook.md)'s records of the functions each
test entered, [M1-132](m1-132-function-level-selection.md)'s digests and
map reader, and [M1-133](m1-133-selection-proved.md)'s method of proof.

## Purpose

Today a mutant file runs in full when an input of its judges has changed
(decision 276). Most of those runs cannot change a verdict. This task makes a
mutant run only when its run can find a fault. **It is not necessary to
compile a mutant to find if it must run.**

## The rules

**R1. The unit.** A *triple*: a set of three items, one mutant, one test and
one tree. Its kept verdict is "failed" or "passed". A hang, or a record that
is not complete, keeps no verdict.

**R2. The tests that can enter a mutant.** From the line tables and the map
of the usual build (the build with no mutant), find the code that the mutated
line compiles to: its function, each function into which it is inlined, and
each function that uses mutated data. A test can enter the mutant only if it
entered one of these at its last pass. A test that cannot enter it cannot
fail: it is not run, and its verdict is "passed".

**R2a. R2 is proved before it is trusted.** The line tables of an optimised
build can give code to an incorrect line. Thus, for each mutant of the mutant
files, the functions with compiled bytes that change when the mutant is
compiled must be functions that R2 found. A miss fails the proof,
and that type of mutant uses the fallback of R4. The proof runs one time as a
measurement, and again at each gate (R13).

**R3. The cache of mutated digests.** At each run of a mutant, keep the
mutated digests of the mutated function and of each function into which it is
inlined.

**R4. When the cache stays correct.** While these are unchanged: the mutant's
text; the usual digest of the mutated function; and the usual digests of the
names that the replacement adds -- the functions, data, constants and types
that it uses and the initial line does not use; and the usual digests of each
function into which the mutated function is inlined (R3). If such a name cannot
be found with no compile (a macro, an overload set), the compile fingerprint
of the mutated object is also a part of the condition, for that mutant only.

**R5. The key of a triple.** The mutant's cached digests, while R4 holds; the
usual digests of the functions that the test entered with the mutant at the
triple's last run; all the other parts of the test's fingerprint --
definition, environment, reads, machine, card, shaders, linker directives,
link command, other libraries; the tree; and the version of the harness.

**R6. Skip.** A triple with a key that is equal to its kept key does not run.
Its verdict stays.

**R7. A caught mutant costs nothing.** If one triple of a mutant is skipped by
R6 with the verdict "failed", the mutant stays caught, and no other test of
it runs. The kept verdicts of those other tests are marked "not run since"
the commit of their last run, and the report names only the tests that ran.

**R8. A survivor is proved again.** A mutant with no failing triple is
skipped only if R6 skips each of its triples with a test that can enter it
(R2).

**R9. Compile-time mutants.** The key of a mutant that the compiler catches is
only its text and the compile fingerprint of the mutated objects.

**R10. A judgement is not a run.** A change to `expect`, `diagnostic` or the
named judges is judged again from the kept verdicts, with no compile and no
run.

**R11. When a run occurs.** Only when R6 to R9 cannot skip: a new mutant, a
new test that can enter it, or a changed key. The run compiles the mutant one
time, runs the tests of the triples that must run, and updates the cache of R3
and the paths of R5.

**R12. Other types of mutant.** The same rules, with the inputs of their type:
Python through `sys.monitoring`, shaders by their compiled bytes, CMake by
the configure inputs.

**R13. The safety net.** A full pass at each phase gate, and also when more
than N commits came after the last full pass, finds what the rules cannot
see: a test that enters different functions in a different run (decision
707); start-up functions (decision 624); `constexpr` and `static_assert`
effects in headers; code that the linker folds into one copy; and the changes
that add up over a long interval, where kept verdicts are less accurate.

## The store

All look-ups and writes go through SQLite, with Python's own `sqlite3`
module: no new dependency. One table for each type of record: the triples and
their keys and verdicts (R1, R5); the cached mutated digests and their
conditions (R3, R4); and an index from each function and each other input to
the triples that use it, so that the work of a pass increases with the
quantity of a change, not with the number of triples.

## What to do

1. The questions below ruled, and the rulings in the register.
2. The tests that can enter each mutant (R2), and the names that a
   replacement adds (R4), from the usual build only, with self-tests on
   samples compiled with the tree's own clang; and the proof of R2a.
3. The store and its index, in SQLite.
4. The decision rules R5 to R10 in `scripts/mutate.py` or a script next to it,
   with self-tests on made-up inputs.
5. The runs (R11), the other types (R12), and the full pass at the gates
   (R13).
6. The proof, by the method of M1-133: for each change, each mutant that the
   rules skip must have its kept verdict when it runs in full.

## Questions to put to the owner before it is built

Each with its options, costs and a recommendation when the task is taken up:

1. Where the SQLite file is: in each build tree, not committed, as the
   driver's store is (decision 491), or in a different folder.
2. How R4 finds the names that a replacement adds, and which ones fall back
   to the compile fingerprint.
3. The method for shader and CMake mutants (R12).
4. Files judged on a graphics card, and verdicts that are different for
   each card (decisions 397, 410 and 411).
5. What is measured: the length of the index, the time of a look-up, the
   number of mutants that must use the fallback of R4, and the time of a full
   pass before and after.
6. The proof of R2a: on which mutants, and what a miss changes.
7. A triple with a changed test: does it run, also when R7 skips it?
   This runs a changed test against the mutants that it must catch, at a
   cost of more runs.
8. The value of N in R13.
9. How R5 records the functions that a test entered with the mutant: each
   mutant run then has the coverage hook, at a cost of build time.

Published work agrees with this method: Regression Mutation Testing (Zhang and
others, 2012) also keeps the result of each pair of a mutant and a test, and
uses it again when no change is on the test's path to the mutant; and trivial
compiler equivalence (Papadakis and others, 2015) judges a mutant by its
compiled bytes. R2a and the new parts of R4, R7 and R13 come from that work,
compared with these rules on 2026-10-21.

## Done when

- [ ] All the questions above, and each question of the design, ruled before
      its code.
- [ ] The proof of step 6 shows that no skipped mutant has a changed verdict.
- [ ] `check` passes in the two trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed. A commit at the end; the push only on the owner's go-ahead.
