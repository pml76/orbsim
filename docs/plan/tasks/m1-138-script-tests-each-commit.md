# M1-138 — The script tests that run at each commit

Phase: B | Status: **proposed 2026-10-24; its questions not ruled**
Prerequisites: [M1-133](m1-133-selection-proved.md), which found the cost;
question 5 gives its position in the queue
Decided by: **nothing at this time.** The owner gave the instruction for this
task on 2026-10-24, from a finding of M1-133. The questions below go to the
owner before the work starts, all at one time (working agreement 1). No part
of this task starts until each of them is ruled, and the rulings go into the
register, [`../milestone-1-decisions.md`](../milestone-1-decisions.md) section
9.
Builds on: [M1-129](m1-129-tests-by-fingerprint.md)'s fingerprints and read
records (decisions 489, 498, 500, 510, 527 and 531),
[M1-132](m1-132-function-level-selection.md) and M1-133.

## Purpose

`check` runs only the tests with changed inputs
([ADR 0027](../../adr/0027-tests-run-when-their-inputs-change.md)). Two rules
of the fingerprint make many script tests run when nothing that they read
changed:

- Some tests name the build tree or the repository on their command line. Such
  a test is fingerprinted on each file that git does not ignore (decisions 500
  and 510). Thus an edit to a document runs 21 script tests again.
- A test that runs through `cmake` or `python` is fingerprinted on each file
  in `scripts/` and `cmake/`, and on `CMakeLists.txt` and `CMakePresets.json`
  (decision 531). Thus a change to a record file, for example
  `scripts/mutation-passes.json`, runs 132 tests again.

Working agreement 9 says: no test step that cannot change its result. This
task measures what these tests read, and makes them run only when a file that
they read changed. A skipped test must not be a test that could fail.

## What was measured, read only, 2026-10-24

1. **In M1-133's replay** (Part 2 of
   [its measurement record](../../measurements/m1-133-selection-proved.md)):
   16 tests ran at each of M1-118's 16 commits. They used 121.56 s at each
   commit, which is 64 % of the time of the selection by function. At each of
   the three commits that change files in `scripts/`, 114 tests ran, 359.77 s.
2. **Today's trees**, `check` after a change to documents only, before the
   commit `0582f1e`:

   | | Debug | RelWithDebInfo |
   |---|---|---|
   | Tests that ran, of 590 | 22 | 28 |
   | Their test time, summed | 80.49 s | 70.67 s |
   | The test step of `check` | 52.20 s | 42.44 s |
   | The full `check` | 65 s | 54 s |

3. **Why each test ran.** In Debug, 21 of the 22 are script tests. Each names
   the build tree, the root of the repository, or the two, on its command
   line. The other one is the C++ test that skips itself on Windows, which the
   driver does not store (decision 752). RelWithDebInfo also runs 6 assertion
   probes that CTest reports as not run in that tree, and that are not stored.
   The longest in Debug: `mutants_due` 14.74 s, `float_equal_guard` 12.90 s,
   `mutants_due_fingerprints` 12.23 s, `coverage_hook` 7.58 s and
   `mutants_due_gpu_identity` 6.32 s.
4. **A change to a file in `scripts/`.** The 102 script tests run, 33 of them
   GPU tests. The 30 tests that are related to them by `DEPENDS` also run
   (decision 508). That is 132 tests, 62 of them GPU tests. Their summed time
   in `check-full` at `9253e87`: 593.8 s of 768.2 s in Debug, and 422.8 s of
   562.2 s in RelWithDebInfo.
5. **How frequently.** The commits after `848fe5c` up to `0582f1e` are 106;
   `848fe5c` is the commit before M1-129's checkpoint `f8f3cd1`. Of them, 70
   change only documents, and 27 change code. 9 change only mutant files or
   the record of the passes, with documents.
6. **Python's audit hook** (PEP 578, `sys.addaudithook`), measured in a
   scratch folder with Python 3.14.0. It sees a file that the process opens
   with `open()`, with `pathlib` and with `os.open`. It does not see a file
   that a child process opens: a child Python process, or `git`.
7. **The earlier rulings.** Decisions 500, 510 and 531 made these rules. They
   considered the files of each test, written by hand, and refused that,
   because it goes out of date with no report.

## The questions, each with its alternatives, costs and a recommendation

Re-check each measurement above before the questions go to the owner: the
trees move.

**1. Which rules does the task change?**

- **A (recommended). The two rules: the build tree and the repository
  (decisions 500 and 510), and the scripts (decision 531).** The first costs
  time at each commit. The second costs the most time when it applies. Cost:
  more work than B.
- B. Only the rule of the tests that name the build tree or the repository.
  Cost: then a change to a record file in `scripts/` runs 132 tests, as at
  this time.
- C. Nothing: the cost is recorded as a limit in ADR 0028. Cost: about one
  minute of `check` at each commit, in each of the two trees.

**2. How does a script test show what it read?**

- **A (recommended). A read record, as the C++ tests have (decisions 489, 498
  and 527).** The audit hook records each file that a Python process of the
  test opens, with its SHA-256. The driver then fingerprints the test on these
  files. A test with a child process that is not Python keeps the current
  rule, until a measurement shows what the child reads (question 3). Cost:
  code in the driver, and a module that each Python process of a test loads at
  its start. Also, M1-137, item 3, must then compare the read records with the
  files that the tests open.
- B. The files of each test, written by hand in `CMakeLists.txt`. Cost:
  decisions 500, 510 and 531 refused that, because it goes out of date with no
  report.
- C. `docs/` removed from the files of the first rule, for each test that is
  measured not to read a document. Cost: the measurement is made one time. An
  edit of a script after the measurement can start to read a document with no
  report, which is the fault of B.

**3. What is measured before the work?**

- **A (recommended). A spike, with only a measurement as its output.** One
  full test run in each tree with Process Monitor, as decision 612 did, and
  the audit hook in a scratch copy. It gives the files that each of the 102
  script tests opens, child processes included. If the figures change the
  recommendation of question 2, that question goes to the owner again. Cost:
  about one hour; the owner starts Process Monitor, as for decision 612.
- B. No spike: the answer to question 2 from a reading of the scripts. Cost: a
  read that the code does not show, for example in a child process, can be
  missed.

**4. How is the safety proved?**

- **A (recommended). As M1-133 did.** A mutant check over the files that these
  tests read. For each mutant, all tests run, and each test that fails must be
  a selected test. The check must find a planted fault first (decision 733).
  The read records are compared with Process Monitor (decision 612). Then the
  task's own mutant file runs after its commit (`VERIFICATION.md` rule 19).
  Cost: some hours.
- B. The task's own mutant file only. Cost: it proves the code of the task,
  but not that the skipped script tests are safe.

**5. Where in the queue?**

- **A (recommended). After M1-137 and before M1-134.** M1-137, item 3,
  compares the read records with the files that the tests open, child
  processes included. This task must do the same. M1-134 selects the mutation
  passes as the tests are selected, thus this task also makes M1-134 faster.
  Cost: M1-134 starts after this task.
- B. As a part of M1-137. Cost: the scope of M1-137 changes, which decision
  744 ruled.
- C. After M1-134, or with point 4 of M1-129's "The order from here": working
  agreement 9 applied to the tests that run in the two trees. Cost: about one
  minute of `check` at each commit stays until then.

## Done when

- [ ] Each question above ruled, and each question about how the work is done,
      before its code; the rulings in the register.
- [ ] The measurements of question 3, recorded.
- [ ] A script test runs only when a file that it read changed, or by a rule
      that the task document gives with its cause. This is measured before and
      after, in the two `check` trees. One change is to documents only, and
      one is to a record file in `scripts/`.
- [ ] The safety proved as question 4 rules.
- [ ] `check` passes in the two trees. The task's mutant file has run after
      its commit, and its record is committed. A commit at the end; the push
      only on the owner's go-ahead.
