# M1-138 — The script tests that run at each commit

Phase: B | Status: **started 2026-10-24. Its questions 1 to 14 are ruled
(decisions 764-775 and 777-780), questions 15 to 20 about how the code is
done (decisions 781-786), questions 21 to 24 about the rules of decision
778 (decisions 787-790), questions 25 to 35 before step 2 (decisions
791-801), questions 36 to 42 before step 3 (decisions 803-809), question 43
(decision 810), questions 44 to 50 before step 4 (decisions 811-817), and
questions 51 to 61 before the safety proof (decisions 818-828). The spike is
done ([its record](../../measurements/m1-138-spike.md)). Steps 1 to 4 of the
code are committed. The next session starts with "Left for the next session
(2026-10-27)" below**
Prerequisites: [M1-133](m1-133-selection-proved.md), which found the cost. It
comes before M1-137 (decision 763).
Decided by: **decision 763**, its position in the queue, and **decisions
764-775**, its questions, all ruled 2026-10-24. The owner gave the
instruction for this task on 2026-10-24, from a finding of M1-133. The owner
ruled questions 1 to 4 and 6 to 12 by their recommendations on 2026-10-24:
"Pls go ahead with your recommendations". The rulings are in the register,
[`../milestone-1-decisions.md`](../milestone-1-decisions.md) section 9.
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

## The measurements examined again, and new facts, 2026-10-24

The next session examined each measurement above again, before the questions
went to the owner. It used the functions of the driver itself, on the trees
after `check-full` at `54db237`. No tracked file changed for it.

| Item | Then | Examined again |
|---|---|---|
| 1 | as above | The same, in the measurement record of M1-133 |
| 2, the tests | 22 and 28 | The same |
| 2, the times | as above | Not measured again: for this, a change to the working tree is necessary (decision 773) |
| 3 | as above | The same tests. The longest tests are different, because the first `mutants_due` test that runs gets the answers of Ninja for all the others |
| 4 | 132 tests, 62 GPU tests | The same. Summed time in `check-full` at `54db237`: 580.0 s of 781.0 s in Debug, and 385.7 s of 539.3 s in RelWithDebInfo |
| 5 | 106, 70, 27, 9 | The same. Also: 33 of the 106 change `scripts/`, `cmake/`, `CMakeLists.txt` or `CMakePresets.json` |
| 6 | as above | The same |

New facts, measured in scratch folders with Python 3.14.0 and CMake 4.3.1.
For N3 to N5, the 45 Python script tests ran one time in `build/debug`
through CTest, with an audit hook loaded from a `sitecustomize.py` file on
`PYTHONPATH`. All 67 tests passed: CTest added 22 setup tests.

- **N1. 57 of the 102 script tests start through `cmake -P`**: 29 GPU probes
  (`RunProbe.cmake`), 17 tests of the command line (`RunUsage.cmake`), and 11
  others. The audit hook cannot see these processes. A search of their 9
  scripts found no Python and no path in `scripts/`. The 30 tests related by
  `DEPENDS` are all related to these 57. In Debug, the 57 and the 30 used
  225.9 s of the 580.0 s; the 45 Python script tests used 354.1 s.
- **N2. What the hook sees.** It sees each file that opens, each folder that a
  script lists (`os.listdir`, `os.scandir`, `os.walk`, `glob`, and `iterdir()`,
  `glob()` and `rglob()` of `pathlib`), and each process that starts, with its
  command line. It sees no event for `os.stat`, `os.path.exists`, `isfile`,
  `getmtime`, or `exists()` and `is_file()` of `pathlib`. On Windows,
  `os.path.exists`, `isfile` and `isdir`, and `exists()` and `is_file()` of
  `pathlib`, do not call `os.stat`: a replacement of `os.stat` does not see
  them. `getsize`, `getmtime` and `stat()` of `pathlib` call it.
- **N3. A cached script hides the script.** When a `.pyc` file in
  `__pycache__` agrees with its `.py` file, Python opens only the `.pyc` file.
  `scripts/__pycache__/` held the `.pyc` files of 8 scripts, which git
  ignores. In 13 of the 45 tests, a script of `scripts/` was loaded only from
  its `.pyc` file: `run-tests.py` in 9 `mutants_due` tests and in
  `mutate_self_test`, and `check-coverage-recorder.py` in 3 tests of the
  driver. The source file of each module, recorded at exit, closes this.
- **N4. Child Python processes.** With the hook loaded from `sitecustomize.py`
  on `PYTHONPATH`, the hook also sees a child Python process. It does not see
  a Python that starts with `-I` or `-E`. No script and no test definition
  does that. This Python has no `sitecustomize` of its own. 63 processes had
  no test name: they are the tests of a scratch project in the temporary
  folder, which a test of the driver starts. One process had the name of no
  test of the tree, `a different test`, which a self-test of the driver sets.
- **N5. No Python process of a script test opened a document.** Of the 21
  script tests that a document change runs in Debug, 15 start `git`, Ninja,
  `ctest`, `clang++`, `llvm-readobj` or a program of the tree. The other 6,
  `lint_deps`, `memory_pool`, `test_cases`, `test_reads`, `tonemap_constants`
  and `configure_current`, used about 10 s of the 107 s. Of the 45 Python
  script tests, 22 start no child program, and they used about 20 s in the
  spike. 7 tests read `scripts/mutation-passes.json`: 7 of the 10
  `mutants_due` tests. In the spike, each of the 45 tests listed `scripts/`.
- **N6. CMake's trace works in script mode.** The arguments `--trace-expand
  --trace-format=json-v1 --trace-redirect=<file>` give each command that a
  script runs, with its arguments expanded and the file that it comes from.
  This includes `include()`, `file(READ)`, `if(EXISTS)`, `file(GLOB)` and
  `execute_process`.
- **N7. `STATUS.md` was not current.** The remote `master` was at `54db237`,
  pushed at 07:57, and `STATUS.md` said that the commits after `9253e87`
  waited for the push. `2000109` records the push (decision 774).
- **N8. The first `check` of a session can run all tests.** After `2000109`, a
  change to documents only, `check` ran all 590 tests in each tree: 279.5 s
  in Debug, and 150.1 s in RelWithDebInfo. Before the run in RelWithDebInfo,
  the functions of the driver gave the cause: the environment part was different in
  each of the 583 stored tests. The `PATH` of this session is different from
  the `PATH` at the last full run (decision 501). Thus the "before" figures of
  a document change come from the next change to documents, the commit of the
  rulings.

## What becomes better: the estimate before the work, 2026-10-24

The owner gave the instruction for this estimate before the work started. It
is calculated from the measurements above, and nothing was measured for it.
The figures are for Debug; RelWithDebInfo can change in about the same
proportion. **Level 1** is the rulings of 2026-10-24: a test with a child
program that has no measured rule keeps rules 1 and 2. **Level 2** comes if
question 7 gives a measured rule to each child command. Only `check` becomes
faster; `check-full` becomes slower by a small time, because of the hook. The
measurements after the work are compared with this estimate.

| A change to a record file in `scripts/` | Tests | GPU tests | Summed test time |
|---|---|---|---|
| Before the task | 132 | 62 | 580 s |
| Level 1 | about 23 | 2 | about 335 s |
| Level 2 | about 7 | 0 | about 25 s |

At level 1, the 57 `cmake -P` tests, their 30 dependents and the 22 Python
tests with no child program stop (decision 775). The GPU tests run one at a
time with one lock, and 60 of the 62 stop. Thus the time on the clock can
decrease more than the summed time. The estimate for the test step at level 1
is about 80 to 100 s, because the longest tests that stay used 78 s and 70 s
in the spike.

| A change to documents only | Tests | Summed test time | Test step |
|---|---|---|---|
| Before the task | 22 | about 107 s | 52 s |
| Level 1 | 16 | about 97 s | almost the same |
| Level 2 | 1 to 3 | less than 15 s | much less; no figure |

At level 1, only the 6 tests with no child program stop; the longest tests
start child programs and keep rule 1. The figures of level 2 can have the
largest error, because nobody has measured what `git`, Ninja and `ctest`
read. Over the 106 commits, from these two cases only, the summed test time
in Debug decreases by about 48 minutes at level 1 and about 3 hours at level
2. An edit to one script runs about 25 tests at level 1, and only the tests
that load the script at level 2. A change in `cmake/` or `CMakeLists.txt`
decreases nothing. A file added to `scripts/` or removed from it runs the 45
Python tests again (N5). At level 2, a new version of `git` or Ninja makes
the tests that start them run again; before the task, `git` is in no
fingerprint. Each script test gets a list of the files that can change its
result, which M1-134 can use. The costs: each Python test that runs uses some
more time; the driver gets more code, self-tests and mutants; the first
`check` after the task runs all script tests one time.

## The questions, each with its alternatives, costs and ruling

**1. Which rules does the task change?** *Ruled by decision 764: A.*

- **A (recommended). The two rules: the build tree and the repository
  (decisions 500 and 510), and the scripts (decision 531).** The first costs
  time at each commit. The second costs the most time when it applies. Cost:
  more work than B. N1 and N5 show that A gives its full effect only with
  questions 6 and 7.
- B. Only the rule of the tests that name the build tree or the repository.
  Cost: then a change to a record file in `scripts/` runs 132 tests, as at
  this time.
- C. Nothing: the cost is recorded as a limit in ADR 0028. Cost: about one
  minute of `check` at each commit, in each of the two trees.

**2. How does a script test show what it read?** *Ruled by decision 765: A,
with the three items that N2 to N4 add.*

- **A (recommended). A read record, as the C++ tests have (decisions 489, 498
  and 527).** The audit hook records each file that a Python process of the
  test opens, with its SHA-256. The driver then fingerprints the test on these
  files. A test with a child process that is not Python keeps the current
  rule, until a measurement shows what the child reads (question 3). The
  record also holds each folder that a process listed, with a hash of the
  names in it, the source file of each module that it loaded, and each
  process that it started. Cost: code in the driver, and a module that each
  Python process of a test loads at its start.
- B. The files of each test, written by hand in `CMakeLists.txt`. Cost:
  decisions 500, 510 and 531 refused that, because it goes out of date with no
  report.
- C. `docs/` removed from the files of the first rule, for each test that is
  measured not to read a document. Cost: the measurement is made one time. An
  edit of a script after the measurement can start to read a document with no
  report, which is the fault of B.

**3. What is measured before the work?** *Ruled by decision 766: A.
Amended by decision 777: the complete Debug recording counts for the two
trees.*

- **A (recommended). A spike, with only a measurement as its output.** One
  full test run in each tree with Process Monitor, as decision 612 did, and
  the audit hook in the same run. It gives the files that each of the 102
  script tests opens, child processes included. Cost: about one hour; the
  owner starts Process Monitor, as for decision 612.
- B. No spike: the answer to question 2 from a reading of the scripts. Cost: a
  read that the code does not show, for example in a child process, can be
  missed.

**4. How is the safety proved?** *Ruled by decision 767: A.*

- **A (recommended). As M1-133 did.** A mutant check over the files that these
  tests read. For each mutant, all tests run, and each test that fails must be
  a selected test. The check must find a planted fault first (decision 733).
  The read records are compared with Process Monitor (decision 612). Then the
  task's own mutant file runs after its commit (`VERIFICATION.md` rule 19).
  The mutants include one of each new type of input: a document, a script
  that a different script loads, a new file in a listed folder, a record file
  and a CMake script. Cost: some hours.
- B. The task's own mutant file only. Cost: it proves the code of the task,
  but not that the skipped script tests are safe.

**5. Where in the queue?** *Ruled by decision 763, 2026-10-24: next, before
M1-137.* The alternatives as they were put:

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

**6. The 57 tests that start through `cmake -P` (N1).** *Ruled by decision
768: B.*

- A. Keep rules 1 and 2 for them. Cost: a change to a record file runs 87
  tests, as at this time, 225.9 s in Debug.
- **B (recommended). CMake's trace as their read record (N6).** The 57 test
  definitions get the three trace arguments. The driver reads a known list of
  commands from the trace, and a trace with a different command keeps rules 1
  and 2. Cost: more code in the driver, and each of the 57 tests runs one time
  more. First, the spike measures that the trace changes no result and adds
  only a small time; if not, the question goes to the owner again.
- C. A rule written one time: a `cmake -P` test gets a fingerprint on `cmake/`
  and the build files only. Cost: a rule written by hand, which decision 531
  refused.

**7. The tests with a child that is not Python (N5).** *Ruled by decision
769: C. The question goes to the owner again with the figures of the spike.*

- A. Keep rules 1 and 2 for each such test. Cost: a document change decreases
  the time by about 10 s in Debug, and most of the cost stays.
- B. A rule for each child command, from measurement, and each child program
  in the fingerprint, with its libraries. Cost: more code, and a measurement
  again when a tool changes.
- **C (recommended). Rule it after the spike, with its figures.** The code of
  the driver does not start before that ruling. Cost: one more set of
  questions, after the spike.

**8. The questions about a file with no open (N2).** *Ruled by decision 770:
B.*

- A. Accept it as a recorded limit. Cost: between two pushes, the driver can
  skip a test that it must run, when a file is added or removed.
- **B (recommended). Replace the functions that examine a file**, in the
  module that loads at the start: `os.stat`, `os.lstat`, `os.path.exists`,
  `isfile` and `isdir`, and the same functions of `pathlib`. Each writes a line
  for the file, as `probeFile()` does for the C++ tests, with `missing` for a
  file that is not there. A self-test calls each function and makes sure that
  its line is there. Cost: a list of the functions of Python, and its
  self-test.

**9. How does the hook load (N4)?** *Ruled by decision 771: A.*

- **A (recommended). A file `sitecustomize.py` in a new folder in `scripts/`,
  which the driver puts on `PYTHONPATH` for CTest.** Cost: a plain `ctest`,
  and the `ctest -R` of `mutate.py`, write no records.
- B. A wrapper script on the command line of each of the 45 Python script
  tests. Cost: 45 definitions change, and the hook does not see a child Python
  process.

**10. Python on the second machine.** *Ruled by decision 772: A.*

- **A (recommended). The self-tests of decisions 770 and 771 run there at the
  next `check`, and report a difference.** The second machine has Python
  3.13.5, and the measurements used 3.14.0. Cost: none at this time.
- B. Measure 3.13.5 before the work. Cost: the owner must start the spike on
  that machine.

**11. The "before" figures of a change to a record file.** *Ruled by decision
773: A.*

- **A (recommended). A change to `scripts/mutation-passes.json` in the
  checkout:** one empty line added at the end, which JSON accepts, with a copy
  kept in the scratch folder first. `check` runs in each tree, the copy goes
  back, and `check` runs again. The same change gives the "after" figures.
  Cost: eight runs of `check`.
- B. Only the counts that the functions of the driver give. Cost: no measured
  time.

**12. Record the push of N7 in `STATUS.md`?** *Ruled by decision 774: A.*

- **A (recommended). Yes, in a commit of its own, before the rulings**, as
  `c6e5cd2` did for M1-133. It is `2000109`.
- B. In the commit of the rulings. Cost: two items in one commit.

**A program that a `cmake -P` test names.** *Ruled by decision 775.* A
program that a `cmake -P` test names on its command line counts as a known
child, as the estimate above calculates. The driver hashes it before this task, and
rule 1 and the cautious rule (decisions 489 and 505) stay for what it reads.
Thus for these tests only rule 2 goes, when the trace shows no other child.

## The sequence of the work

1. The rulings as register rows, decisions 764-775, and this document,
   `STATUS.md`, the briefing and the queue page. A commit of documents only;
   `check` before it gives the "before" figures of a document change (N8).
2. *Done 2026-10-24.* The "before" figures of a change to a record file
   (decision 773).
3. *Done 2026-10-24.* The spike (decision 766), with only a measurement as
   its output: [the record](../../measurements/m1-138-spike.md).
4. *Done 2026-10-24.* Question 7 to the owner again, with the figures of the
   spike, and questions 13 and 14 (decisions 778-780).
5. The code, in small commits, each with `check` passing in the two trees and
   its tests first seen to fail (questions 15 to 20, decisions 781-786, and
   questions 25 to 35, decisions 791-801):
   0. *Done 2026-10-25, `9d39113`.* The correction of the hook for a fault
      on a different thread, and its case in `check-read-record.py`
      (decision 791), before step 2;
   1. *Done 2026-10-24, `a56c1f3`.* The hook,
      [`scripts/read-record/sitecustomize.py`](../../../scripts/read-record/sitecustomize.py),
      and its self-test, `read_record_self_test` (decisions 765, 770, 771,
      780-782 and 785);
   2. *Done 2026-10-25, `e95dcef`.* The driver with the records of the
      Python processes, and its self-test cases (decisions 783, 786, 792-796,
      801 and 802);
   3. *Done 2026-10-26, `01c3868`.* The trace of the `cmake -P` tests
      (decisions 768, 775, 784, 797, 798 and 803-810);
   4. *Done 2026-10-26, `863d41d`.* The rules for the child programs
      (decisions 778, 787-790 and 811-817), and the end-to-end test
      (decisions 786, 808 and 816).
6. *Removed by decision 779:* the "after" figures.
7. The safety, as decision 767 says.
8. `check` in the two trees, the commit, the mutant file of the task after
   its commit and the commit of its record and `STATUS.md`, `check-full` in
   the two trees, and the push on the owner's word.

## The work at this time, and the questions after the spike (2026-10-24)

1. *Done 2026-10-24.* **The handover is committed**, after `check` passed in
   the two trees. It changes `CLAUDE.md` (working agreement 10), the register
   (decision 776), this document, `STATUS.md` and the briefing. The commits
   after `54db237` wait for the next push, after `check-full` in the two
   trees.
2. **The `check` in Debug failed, after the copy of decision 773 went back.** It ran
   10,275.5 s. Five tests stopped at their time limit:
   `probe_grid-jitter-1km-3` (2,979.73 s), `run_tests_function_level`
   (3,036.46 s), `run_tests_end_to_end` (3,036.42 s),
   `probe_grid-jitter-1km-2` (4,231.14 s) and `probe_grid-400km`
   (2,798.36 s). The tests that CTest's `DEPENDS` property connects to the
   three probes did not run. The cause was not examined. One possible cause
   is open item (3) of `STATUS.md`: when a machine sleeps during a run, the
   tests go past their time limits. The second `check` in RelWithDebInfo did
   not run. The record file
   is back byte for byte: its SHA-256 is the same as before the change.
   *Later on 2026-10-24*, `check` ran again: it passed in Debug, where 53
   tests ran, and in RelWithDebInfo, where 133 ran. The five time-outs did
   not come again, and their cause stays unknown.
3. **The figures that were measured before agreement 10.** A change to
   documents only, the `check` before `a4e8bad`: in Debug, 22 tests ran and
   568 were skipped, their times summed to 53.24 s, the test step used
   44.3 s and all of `check` 46.5 s. In RelWithDebInfo: 28 ran, 562 were
   skipped, 53.70 s, 36.7 s and 38.9 s. Decision 773, the empty line at the
   end of `scripts/mutation-passes.json`: in Debug, 133 tests ran and 457
   were skipped, their times summed to 547.89 s, CTest used 243.53 s, the
   test step 255.5 s and all of `check` 257.6 s. In RelWithDebInfo: 133
   ran, 457 were skipped, 486.29 s, 120.02 s, 127.7 s and 130.2 s. 133, not
   132: the test that the driver does not store also ran.
4. *Done 2026-10-24.* **The spike**, with its tools first tested on planted
   faults: [the record](../../measurements/m1-138-spike.md), and the scripts
   in [`../../measurements/m1-138/`](../../measurements/m1-138/). In the
   complete Debug recording, the hook missed no file read, no listing and no
   question about a file, in 148 Python processes. Decision 777 lets that
   recording count for the two trees.

The questions below went to the owner all at one time, before the code.

**7 (again, with the figures of the spike). The tests with a child program
that is not Python** (decision 769; the record, sections 5 and 7). *Ruled by
decision 778: B.*

- A. Rules 1 and 2 stay for each such test. Estimate: a change to documents
  runs 16 tests in Debug, about 97 s summed, and the test step stays at
  about 44 s; a change to a record file runs about 23 tests, about 335 s.
- **B (recommended). A measured rule for each type of command in the
  list below; rules 1 and 2 for each other type.**
  1. Each program that a Python script test starts: its bytes, the libraries
     that it loads, and each path on its command line, also a path after
     `=`, as in `-c core.hooksPath=<path>`. A folder gives each file in it
     that git does not ignore. This is decision 775, for the Python tests.
  2. `ctest --show-only=json-v1`: the CTest files of the build tree.
     Measured: it reads only the build tree.
  3. `ninja -t <tool>`, also with `-C <tree>`: the four Ninja files of the
     tree, and that each file that it queries is there, or is not there, as
     before. Measured: it reads only these four files, and queries 900
     object files.
  4. `git ls-files` in the repository: the names that it gives. Measured: it
     reads only `.git`.
  5. A program with its working folder not in the repository or the tree:
     rule 1 only. Measured: of 667 such processes, each one that read or
     queried a file in the repository read only a path on its own
     command line, or its own program file and its folder in the tree.
  6. Each other type of command, for example `clang++`, `cmd`, `lld-link`, and a
     different `git` command in the repository: rules 1 and 2 stay.

  Estimate: a change to documents runs 3 tests in Debug (`float_equal_guard`,
  `reproducible_build`, and the test that the driver does not store), about
  32 s summed; the test step about 30 s, because `float_equal_guard` alone
  uses about 25 s. A change to a record file runs about 9 tests, about 61 s
  summed. Cost: code for six rules, each with a self-test case. A rule fits
  only its measured type of command, thus a new type keeps rules 1 and 2.
- C. B, and a rule for `clang++`: the files of `src/`, `tests/` and the tree
  that it read, and that none of the about 300 header names that it
  queries at the root is there. Estimate: a change to documents
  runs 1 test. Cost: the most code, and the rule depends on the search path
  of the compiler, which a different flag can change.

**13. Working agreement 10 and decisions 766, 768 and 773.** Agreement 10
(decision 776) came after them. They include measurements of time that do
not prevent a bug. The time of the trace is measured (the record, section
6). Not measured: the time that the hook adds (decision 766), and the "after"
figures of decision 773, which "Done when" compares with the estimate.
*Ruled by decision 779: A.*

- **A (recommended). No more of these measurements.** The "after" figures,
  the time of the hook and the comparison with the estimate go out of "Done
  when". Each measurement that prevents a bug stays: the mutant check, and
  the records compared with Process Monitor (decision 767). Cost: the task
  gives no measured decrease of time; only the estimate shows it.
- B. Keep them, because decisions 766 and 773 include them. Cost: about one
  hour of runs of `check` and of the hook.

**14. The replacement of `os.stat` and the sets of `os`** (decision 770; the
record, section 7). A Python function that replaces `os.stat` is not in
`os.supports_fd`, `os.supports_follow_symlinks` and `os.supports_dir_fd`,
which `shutil` examines when it loads: it uses its safe form of `rmtree`
only when `os.stat` is in the last two (`shutil.py` of Python 3.14.0, lines
804 to 807). With Python 3.14.0 on Windows, `os.supports_dir_fd` is empty,
thus nothing changes there. On Linux, the replacement changes how
`shutil.rmtree` works, and `check-full` runs there before a milestone.
*Ruled by decision 780: A.*

- **A (recommended). The module puts each replacement into each of these
  sets that holds the function before the replacement**, and a self-test
  makes sure of it. Thus `shutil` gives the same answers. Cost: some lines
  and one self-test case.
- B. No replacement of `os.stat` and `os.lstat`; only the functions of
  `os.path`. Cost: `getsize`, `getmtime` and `stat()` of `pathlib` are not
  seen, which decision 770 includes.
- C. Accept the change on Linux. Cost: a test can give a different result
  in the Linux trees only.

**15 to 20. How the code is done.** *Ruled by decisions 781-786, each by its
recommendation.* 15: the places and the names of the hook, the records and
the two variables. 16: a record written at the start of a process and again
at its exit, with `end` only at the exit, and a record required from the
interpreter of the test's own script. 17: each record also into the folder
of each outer test, for a driver that runs inside a test. 18: one CMake
function for the trace arguments. 19: the self-test of the hook as a CTest
test of its own. 20: a self-test case for each new part, and the end-to-end
test with a Python script test and a `cmake -P` test.

**21 to 24. The rules of decision 778, for the code.** *Ruled by decisions
787-790, each by its recommendation.* 21: for `ninja -t`, which of the
outputs that `build.ninja` names are there. 22: a path not in the
repository or the tree counts by its bytes, by the names in it, or as
`missing`. 23: the CTest files of the tree are each `CTestTestfile.cmake`
and `DartConfiguration.tcl`, each file of Catch2's test discovery, and each
program of the tree. 24: the driver runs the same `git ls-files` command
again and hashes its output.

**25 to 35. Before step 2, 2026-10-25.** The next session examined the code
of step 1 and the driver before step 2, and measured each finding in a
scratch folder. *Ruled by decisions 791-801, each by its recommendation.*
25: the flag of a fault of the hook is one value for all of the process,
because a value for each thread does not stop the line `end` after a fault on
a different thread. 26: for the records of the Python processes, the driver
makes the line that the hook writes for a path at this time, because
`World.file()` gives `missing` for a folder. 27: the driver uses a record only
if it holds the `read` line of its own hook, because a copy of the hook in a
different repository keeps the paths of that repository. 28: the physical
memory and `os.cpu_count()` go into the machine part, because `memory_pool`
and `parallel_tests` use them and no record can show them. 29:
`check-read-record.py` compares the reading of the driver with the records of
the hook itself. 30: `PYTHONPATH` goes into the environment part. 31: from a
trace, the driver uses only the files that are not in the build tree. 32:
the CMake function of the trace stops the configure for a test name that the
escape changes. 33: the tests that read the store of the driver run at each
`check` after step 4, as before this task. 34: the files of the spike stay
until this task ends. 35: the rules of step 2 that no earlier decision gives.

**36 to 42. Before step 3, 2026-10-26.** The session examined the 57 traces
of the spike, the 9 scripts and the properties of the 57 tests in CTest.
*Ruled by decisions 803-809, each by its recommendation.* 36: the CMake of
the test is a known child. 37: the trace of a test that passed is complete
when its first line is the version line and each other line is an entry. 38:
the commands and the subcommands that the driver uses from a trace, and the
rules for each other command. 39: a child with a path that is not in the
build tree and not on the command line of the test keeps rules 1 and 2. 40:
the driver stores the value of a file from before CTest where it has one. 41:
the function is in cmake/TraceArguments.cmake, and the end-to-end test of
step 4 uses it, also with a test name that the escape changes. 42: the rules
of step 3 that no earlier decision gives. 43, during step 3: the configure
makes `test-reads/` (decision 810).

**44 to 50. Before step 4, 2026-10-26.** The session sorted each child of
each Python test of the spike by the types of decision 778. *Ruled by
decisions 811-817, each by its recommendation.* 44: no rule for `ninja -t` and
`git ls-files` at this time, because only two tests start them, and those run
at each `check` (decision 799). 45: a child with its program by its name only
keeps rules 1 and 2. 46: a path in the repository or the build tree in the
arguments of a child keeps rules 1 and 2. 47: a Python child has no rule of
its own. 48: rule 2 for the code. 49: the end-to-end test of steps 2 to 4,
with its twelve steps. 50: the rules of step 4 that no earlier decision gives.

**51 to 57. Before the safety proof, 2026-10-27.** *Ruled by decisions
818-824: question 55 by alternative B, which the owner selected, and each other question by its
recommendation.* 51: the mutant check is a copy of the script of M1-133, with
two changes. 52: the five mutants, one of each type that decision 767 names.
53: the mutant check runs in the two `check` trees. 54: `prompts.txt` is
committed before the mutant check (`9b832ea`). 55: Process Monitor records
one full run in each `check` tree, and the owner starts and stops each
recording. 56: what counts as a miss when the records are compared with
Process Monitor. 57: the results, and their sequence: the mutant check first,
then the recordings.

**58 to 61. Also before the safety proof, 2026-10-27.** *Ruled by decisions
825-828, each by its recommendation.* 58: the handover of 2026-10-27, and one
commit with the rulings. 59: the three tests that keep the earlier rules and
read the store of the driver go into item 2 of M1-137. 60: `accept_golden`
runs at each `check`, and the driver gets no rule for its path. 61: the
questions about the mutant file go to the owner after the safety proof.

## Left for the next session (2026-10-27)

The owner told the assistant to write this handover before the first file of
the safety proof, and to stop all other work (decision 825). Start here.
Steps 1 to 4 are committed, and the rulings of the safety proof, decisions
818-828, are in the register. No file of the safety proof is written. The
part of 2026-10-25 below gives the earlier work, and its points 1 to 4 are
done.

1. **Where the work is.** The last commit of the code is `863d41d`, step 4.
   After it come `d917dd6`, `9b832ea` (`prompts.txt`, decision 821) and the
   commit of this handover. The commits after `54db237` wait for `check-full`
   in the two trees and for the owner's push. Start with `git status`. The
   working tree must have no change, because the mutant check refuses to
   start with a change (decision 821).
2. **First, the mutant check** (decisions 818-820 and 824). Copy
   `docs/measurements/m1-133/mutant-check.py` to
   docs/measurements/m1-138/mutant-check.py, and make these two changes only:
   - in `driver_env()`, give the skipped tests the environment that the
     driver gives CTest at this time, through `with_hook()` of
     `scripts/run-tests.py` (decisions 771 and 781);
   - let a mutant add a file, and delete that file when the script puts the
     tree back.

   Write the five mutants of decision 819 into a mutant list next to the
   script. Mutants 2 to 5 must make tests fail. If no test fails, the check
   has a problem, and it is not a pass. Run `--plant` first, and continue
   only if the script finds the planted fault (decision 733). Then run the
   check in Debug, and after it in RelWithDebInfo. Before the runs, make the
   app keep the computer awake: if the computer sleeps, the runs stop at
   their time limits.
3. **Then, compare the records with Process Monitor** (decisions 822 and
   823). Write the script with the rules of decision 823. Its planted
   fault is one `read` line removed from a copy of the store, and the script
   must report that path. The spike read its recordings with
   `docs/measurements/m1-138/procmon_analyse.py` and
   `docs/measurements/m1-138/last_events.py`. Then the owner makes the
   recordings: one run of the driver with `--full` in each `check` tree, with
   the configuration `docs/measurements/m1-138/m1-138.pmc`. The owner starts
   and stops Process Monitor. If a recording does not hold events in each
   minute of its run and for each test of the run, the owner makes it again.
   The recordings go into `build/`, which git ignores. Each miss goes to the
   owner before a change.
4. **The record**, docs/measurements/m1-138-safety.md, with the scripts and
   the small results in `docs/measurements/m1-138/`, and no figures of time
   (decisions 779 and 824).
5. **Then the questions about the mutant file**,
   scripts/mutants/m1-138.json, all at one time, before the file is written
   (decision 828). Point 5 of the part of 2026-10-25 names the five faults of
   the hook that step 1 tried, which are mutants of the file. Then the
   commit, the mutant file and its record, `check-full` in the two trees, and
   the push on the owner's word.
6. **Facts of 2026-10-27 for the next work:**
   - Decisions 799 and 811 have corrections. Only a test with the new rules
     runs at each `check` because it reads the store. Some `mutants_due`
     tests keep the earlier rules, and the store is not one of their inputs.
     These are not the same tests between the trees and between runs
     (decision 826, corrected). This goes into item 2 of M1-137.
   - `accept_golden` runs at each `check`, because of an `unreadable` read
     line (decision 827).
   - Run each scratch Python script with `-I -B`. Without `-B`, Python writes
     the folder scripts/__pycache__, and then `mutants_due_self_test` runs
     again.

## Left for the next session (2026-10-25)

The owner ended the session after step 1. Start here: step 2 is next. Each
point below comes from the work of this session; read it before the code.

*Added 2026-10-25:* the owner ruled questions 25 to 35 before step 2
(decisions 791-801). The correction of the hook (decision 791) is committed
(`9d39113`): two child processes of `check-read-record.py` plant a fault on a
second thread, through the audit hook and through `os.stat`, because the
correction sets the flag in these two functions. **Step 2 is committed
(`e95dcef`), thus points 1 and 2 below are done.** The driver sets the hook
for each test that it runs. In each `check` tree, the 22 Python tests with no
child process have complete records, and the store holds their listings.
Four mutants of M1-129 and M1-132 have new anchors (decision 802). Before the
commit, `check` in RelWithDebInfo found a fault of the new case of decision
795: its planted fault went into a record with no line `end` when the
numbers of the processes put such a record first, and the driver does not
examine such a record. **Step 3 is committed (`01c3868`), with decisions 797,
798 and 803-810.** In each `check` tree, each `cmake -P` test that passed has
its trace in the store: 57 in Debug, and 51 in RelWithDebInfo, where 6 tests
skip themselves. During step 3, a measurement found that CMake stops
`cmake -P` when the folder of the trace is not there; thus the configure
makes `test-reads/` (decision 810). **Step 4 is committed (`863d41d`), with
decisions 799, 808 and 811-817.** In each `check` tree, 34 Python tests have
complete records: 22 with no child, and 12 with children of rule 2 or 5 of
decision 778. `run_tests_end_to_end` has the twelve steps of decision 816.
Thus points 3 and 4 below are done. Next, the safety proof of decision 767
(point 5), with Process Monitor, which the owner starts; then the mutant
file of the task. The files of the spike in point 6 stay until this task
ends (decision 800).

1. **Where the work is.** Step 1 is `a56c1f3`: the hook and `read_record_self_test`.
   No test uses the hook at this time, because the driver does not set
   `ORBSIM_READ_RECORDS`. The commits after `54db237` wait for `check-full`
   in the two trees and for the owner's push. The first `check` of a session
   can run all tests, because of a different `PATH` (N8).
2. **Step 2, the driver** (`scripts/run-tests.py`, decisions 765, 781-783 and
   786):
   - the environment that the driver gives CTest gets `ORBSIM_READ_RECORDS`,
     the tree's `test-reads` folder, and `PYTHONPATH` with
     `<root>/scripts/read-record` first. The driver does not remove
     `ORBSIM_READ_RECORD_OWNER`, because a driver in a test must give the
     outer chain to its tests (decision 783), and `environment()` does not
     include it (decision 781). The environment part changes one time, thus the
     first `check` after step 2 runs all tests;
   - before the run, the driver deletes the Python records of each test that
     it runs: `<escaped name>.py-<process>-<start>.txt` in `test-reads`;
   - after the run, the records of a test are complete only if each one has
     the head line and `end`, and if one of them comes from the interpreter
     of the test's own script: its `argv` without the first item agrees with
     the test's command without the first item, because the launcher of
     PyManager starts the interpreter with the same arguments (decision
     782);
   - a `read` line goes into the stored `reads`, which `unchanged()` examines
     as at this time; a `list` line must get a new examination with the hash of the hook
     (SHA-256 of the sorted names, with a line end between them, in UTF-8 with
     `surrogateescape`); `unreadable` counts as changed at each decision;
   - in `parts()`, a Python test with complete records and no child process
     gets no part for the build tree, for a folder of the repository on its
     command line, or for `scripts/`. The cautious rule of decision 505
     stays. Until step 4, a test with a child process keeps rules 1 and 2;
   - a self-test case for each new part (decision 786).
3. **Step 3, the trace** (decisions 768, 775 and 784): one CMake function for
   the three arguments, at the 17 places that add the 57 tests. From the
   trace: the `file` field of each entry (the scripts that ran);
   `file(READ|STRINGS|SHA256|SIZE)` as a read; `file(GLOB)` as a listing of the
   folder of the pattern; `if` and `elseif` with `EXISTS`, and
   `get_filename_component` with `REALPATH`, as a question about a file;
   `execute_process` as a child. The known commands are in section 6 of the
   record of the spike; a different one keeps rules 1 and 2.
4. **Step 4, the rules for the children** (decisions 778 and 787-790), and
   then the end-to-end test (decision 786). Its scratch repository must hold
   a copy of `scripts/read-record/`, because the hook finds its repository from
   the folder of its own file.
5. **Then** the safety proof (decision 767); the mutant file `m1-138.json`,
   with the five faults of the hook that step 1 tried, as mutants;
   `check-full` in the two trees; and the push on the owner's word.
6. **Files of the spike on the first machine**, in build/m1-138-spike, which
   git ignores: the complete Debug recording `procmon-debug2.*` and the short
   one `procmon-probe3.*`, about 4 GB. They can be deleted on the owner's
   word.

## Done when

- [x] Each question above ruled before its code; the rulings in the register
      (decisions 764-775 and 777-780).
- [x] Question 7 ruled again, with the figures of the spike (decision 778).
- [x] The measurements of question 3, recorded: [the record of the
      spike](../../measurements/m1-138-spike.md).
- [ ] A script test runs only when a file that it read changed, or by a rule
      that the task document gives with its cause. *Decision 779: with no
      measurement of time, and no comparison with the estimate.*
- [ ] The safety proved as question 4 rules.
- [ ] `check` passes in the two trees. The task's mutant file has run after
      its commit, and its record is committed. A commit at the end; the push
      only on the owner's go-ahead.
