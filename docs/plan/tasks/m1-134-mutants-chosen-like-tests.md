# M1-134 — Mutation passes chosen the way the tests are

Phase: B | Status: postponed until the owner continues it (decision 735)
Prerequisites: M1-133
Decided by: register decision 633, ruled 2026-10-18 at the owner's request --
point 3 of the order decision 543 set, "mutation testing at function level",
made a task of its own -- and 634, ruled the same day after a spike, which
settles its question 2. Its design questions are put to the owner before it
is built (543); the first of them are below.
Builds on [M1-131](m1-131-coverage-hook.md)'s records of the functions each
test entered, [M1-132](m1-132-function-level-selection.md)'s digests and
map reader, and [M1-133](m1-133-selection-proved.md)'s proof that choosing
tests that way skips no test that would have failed.

## Purpose

Today a whole mutant file is due when anything its tests' programs are built
from has changed (`scripts/mutants-due.py`; VERIFICATION.md rule 19), and each
of its mutants is run against every one of those tests. This task gives
mutation passes the rule M1-132 gives `check`'s tests: a mutant is run again
only when something its verdict rests on has changed, and only against the
tests that can be affected.

The owner was shown on 2026-10-18 what choosing this way risks, and said they
can live with those risks (decision 633). What that covers -- in particular
whether a full pass still runs as a safety net -- is the first question below.

## What it is to do

Point 3 of M1-129's order as it was written on 2026-10-15, and what the
discussion of 2026-10-18 added to it:

1. **Records taken while the mutant is in place.** The harness sets M1-131's
   record folder for each mutant's run, so that each test's record says what
   it entered under that mutant. The normal runs' records are not enough:
   under a mutant a test can take a path it never takes normally -- a flipped
   condition sending it into an error branch -- and a change there can change
   the verdict while the normal record shows nothing.
2. **Due per mutant, not per file.** A surviving mutant runs again when a
   function one of its tests entered under it has changed, or the mutated code
   has; a caught mutant, when a function the test that caught it entered under
   it has. The reasoning is the one that makes skipping a normal test safe: a
   program that passes only through unchanged functions behaves exactly as it
   did before.
3. **Only the tests that can be affected run against a mutant**: those whose
   normal record enters a function the mutant changes. A test that never
   enters the mutated function behaves under the mutant exactly as without it.
4. **The mutated code judged on its source, or on a build of it.** The
   unmutated program being byte for byte the same does not prove the mutated
   one is: an edit the compiler optimised away in the normal build can matter
   once the mutation reaches it. The mutated file is compiled and its
   functions compared through M1-132's digests, without linking or running
   anything; a mutant that leaves every function identical cannot be caught,
   and is declared equivalent without running anything ("trivial compiler
   equivalence").
5. **The Python mutants the same way**, through Python's syntax tree (`ast`)
   for which function changed and `sys.monitoring` for which functions a
   self-test enters, each script's self-test marking where its cases begin and
   end. Measured 2026-10-15 on the last 16 commits of `scripts/mutants-due.py`
   and `scripts/mutate.py`: 169 of 586 function versions changed (29 %). The
   tools stay in Python.

What it rests on, measured: 532 mutants in 41 files on 2026-10-17, when every
file passed (`STATUS.md`); on 2026-10-15, of 467, 323 were in C++, 113 in
Python, 18 in shaders and 13 in CMake (M1-129's order, point 3). In the
final logs of 2026-10-17, one per file, 418 mutants were caught by a test, and
8 of them only by a program stopping on a failed assertion -- which, since
decision 634, leaves a record as well.

## Questions to put to the owner before it is built

Each with its options, costs and a recommendation when the task is taken up;
these were already visible on 2026-10-18:

1. **What the accepted risks cover**: whether a full pass -- every file, every
   mutant -- still runs as the safety net, as `check-full` does for the tests
   before every push; at each phase gate or each milestone gate, about five
   hours a tree at today's size; or none.
2. *Ruled 2026-10-18, before the task, by decision 634:* **mutants caught only
   by an abort** leave a record, since M1-131's recorder writes one however a
   program ends -- an abort, a crash, a bad argument to the C runtime --
   measured on a spike; so they are treated as the rest.
3. **The shader and CMake mutants**: today's rule, or a route of their own.
   The shaders a test loads are already judged by their compiled bytes, in
   mutation passes as in `check`, since decision 635.
4. **Mutants caught at compile time** -- by a `static_assert` or a named
   warning -- judged on the compile of their own file alone.
5. **Files judged on a graphics card** (decision 397), and mutants whose verdict
   differs by card (decisions 410 and 411).
6. **Where the per-mutant records live**, and how a pass writes them.
7. **How the rule is proved safe**, as M1-133 proves the test selection, and
   what is measured before and after: a full pass's time, and what M1-118's 16
   commits would have run, both ways.

## Material from the discussion of 2026-10-21 (not ruled)

The owner postponed this task on 2026-10-21 (decision 735). This section keeps
what the discussion of that day found. It is material for the questions
above, and it rules nothing.

- **What the owner wants:** a mutant runs only if its run can find a fault.
  No run is wasted time.
- **The owner's first rule**, "a mutant runs if and only if the test that it
  tests runs": the "if" part is safe. The "only if" part is not safe, because
  with a mutant a test can go on a path that it does not go on without the
  mutant (an error branch). Thus the records with the mutant are necessary.
- **Errors found in this document's items** (2026-10-21):
  E1 items 2 and 4 look only at functions, but a verdict also changes with the
  data, the definition, the environment, the machine, the card, the shaders,
  the directives and the link command (owner: "go ahead");
  E2 a caught mutant "when the test that caught it" changed -- the owner
  prefers: a changed test runs against all mutants that it is meant to catch,
  thus the unit is the pair of a mutant and a test;
  E3 item 4 compiles only the mutated file, but a header mutant changes
  functions in each object that includes it (`Scalar.hpp`: 119 of 159
  objects);
  E4 compile-time mutants judged by the fingerprint of their compile (owner:
  agreed);
  E5 a changed or new mutant always runs (owner: agreed);
  E6 each test that can enter the code of the mutant is a judge, not only the
  named ones (owner: b);
  E7 a stop at the first test that catches a mutant -- not used, because of
  E2;
  E8 the full pass at the gates is not wasted time (owner: agreed).
- **The proposed rule** (R1-R9): the result of each pair is kept with a hash
  of all its inputs as its key. A pair runs if and only if no result is kept
  with its current hash as its key. The hash: the mutant; the hash of the inputs of the
  mutated compile; the digests of the functions on the test's path with the
  mutant; the test's registration; all other inputs of the test. No test run
  for an equivalent mutant, a compile-time mutant, or a test that cannot
  enter the code of the mutant. A hang, or a record that is not complete,
  keeps no result. A full pass at each gate. Proved by the method of M1-133.
- **No compile before a decision:** the digests of the mutated compile are
  kept with the hash of its inputs as their key: the mutant, and the fingerprint of the
  usual compile of each object that the mutant changes, from Ninja. A compile
  occurs only when that hash is not kept, that is, when one of those objects
  changed.
- **The cost of the hashing:** many pairs (532 mutants, each with 76 to 450
  tests that can enter its code). Proposed: an index from each function and
  each other input to the pairs that use it, so that the work increases with
  the quantity of a change, not with the number of pairs; a full hash only on
  a miss; a look-up of the kept hashes for the touched pairs, so that a revert
  costs no run. Not measured: the length of the index and the time of a
  look-up.
- **Not ruled:** the owner wants to think about the design; question 3 of
  this document (shader and CMake mutants).

## Done when

- [ ] Every question above, and any the design raises, ruled before its code.
- [ ] The rule is shown, on real changes, never to skip a mutant whose verdict
      could have changed, in the way question 7's ruling sets.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed. A commit at the end; the push only on the owner's go-ahead.
