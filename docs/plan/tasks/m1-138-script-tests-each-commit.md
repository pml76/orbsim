# M1-138 — The script tests that run at each commit

Phase: B | Status: **started 2026-10-24. Its questions 1 to 4 and 6 to 12
are ruled (decisions 764-775). The next session starts with "Left for the
next session" below**
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
2. The "before" figures of a change to a record file (decision 773).
3. The spike (decision 766), with only a measurement as its output, recorded
   in `docs/measurements/`. The owner starts Process Monitor.
4. Question 7 to the owner again, with the figures of the spike (decision
   769). If the figures change a different ruling, that question goes to the
   owner again too.
5. The code: the self-test cases of the driver first, seen to fail; then the
   read record of the Python processes (decisions 765, 770 and 771), the trace
   of the `cmake -P` tests (decisions 768 and 775), and the rule for the
   children (decision 769).
6. The "after" figures, in the two trees: one change to documents only, and
   the change of decision 773.
7. The safety, as decision 767 says.
8. `check` in the two trees, the commit, the mutant file of the task after
   its commit and the commit of its record and `STATUS.md`, `check-full` in
   the two trees, and the push on the owner's word.

## Left for the next session (2026-10-24)

The owner ended the first session of the work. Start here, in this
sequence.

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
3. **Working agreement 10 and this task: a question for the owner.**
   Agreement 10 (decision 776) came after decisions 766, 768 and 773. These
   decisions include measurements of time that do not prevent a bug: the
   time that the hook adds (decision 766), the time that CMake's trace adds
   (decision 768), the "after" figures of decision 773, and the figures
   compared with the estimate in "Done when". Do these measurements stay?
   - **A (recommended). Remove them.** Keep each measurement that prevents a
     bug: Process Monitor compared with the records of the hook, the result
     of each `cmake -P` test with the trace, and the mutant check of decision
     767. Cost: the task gives no measured decrease of time; only the
     estimate above shows it.
   - B. Keep them. Decisions 766, 768 and 773 include them, and agreement 10
     does not stop a measurement that the owner tells you to make. Cost:
     about one hour more of runs of `check` and of the spike.
4. **The figures that were measured before agreement 10.** A change to
   documents only, the `check` before `a4e8bad`: in Debug, 22 tests ran and
   568 were skipped, their times summed to 53.24 s, the test step used
   44.3 s and all of `check` 46.5 s. In RelWithDebInfo: 28 ran, 562 were
   skipped, 53.70 s, 36.7 s and 38.9 s. Decision 773, the empty line at the
   end of `scripts/mutation-passes.json`: in Debug, 133 tests ran and 457
   were skipped, their times summed to 547.89 s, CTest used 243.53 s, the
   test step 255.5 s and all of `check` 257.6 s. In RelWithDebInfo: 133
   ran, 457 were skipped, 486.29 s, 120.02 s, 127.7 s and 130.2 s. 133, not
   132: the test that the driver does not store also ran.
5. **The tools of the spike, not tested at this time.** They are in the folder
   build/m1-138-spike of the checkout on the first machine, which git
   ignores. The hook of decisions 765 and 770 as a measurement tool is
   hook/sitecustomize.py; there are also hook_selftest.py, spike_run.py and
   procmon_analyse.py, the scratch scripts of the measurements, and the logs
   of the failed run. Before the spike, run hook_selftest.py, and then again
   with `--plant open`, which must fail. Then let Process Monitor record a
   short run of some script tests. Examine procmon_analyse.py on it, and
   again with `--plant`, which must report a miss (decision 733). Then the
   full runs, one tree at a time: the owner starts Process Monitor
   (C:\Users\U439644\Procmon64.exe, as administrator, with the default
   filter), and the assistant starts spike_run.py with `full`. Then
   spike_run.py with `trace`, with no Process Monitor.
6. **Found while the hook was written**, to examine before the code of
   decision 770: a replacement of `os.stat` by a Python function changes the
   checks such as `os.stat in os.supports_dir_fd`, which `shutil` makes when
   it loads. With Python 3.14.0 on Windows, `os.supports_dir_fd` is empty,
   thus `shutil.rmtree` does not change there. But `os.stat` is in
   `os.supports_fd` and in `os.supports_follow_symlinks`, and a replacement
   is not. On Linux, the replacement changes how `shutil.rmtree` works.
7. Then question 7 goes to the owner again with the figures of the spike
   (decision 769). No code starts before that ruling.

## Done when

- [x] Each question above ruled before its code; the rulings in the register
      (decisions 764-775).
- [ ] Question 7 ruled again, with the figures of the spike (decision 769).
- [ ] The measurements of question 3, recorded.
- [ ] A script test runs only when a file that it read changed, or by a rule
      that the task document gives with its cause. This is measured before and
      after, in the two `check` trees. One change is to documents only, and
      one is to a record file in `scripts/`. The figures are compared with the
      estimate above.
- [ ] The safety proved as question 4 rules.
- [ ] `check` passes in the two trees. The task's mutant file has run after
      its commit, and its record is committed. A commit at the end; the push
      only on the owner's go-ahead.
