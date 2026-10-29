# M1-138 — the safety of the selection of the script tests

Kind: measurement record
Binding: no — this file records what was measured, on the dates given
Task: [M1-138](../plan/tasks/m1-138-script-tests-each-commit.md), register
decisions 767 and 818-834.

This record has two parts (decision 767). Part 1 is the mutant check: it
examines if the driver skips a script test that fails. Part 2 compares the
read records with Process Monitor: it examines if the fingerprint of each
script test holds each file that the test read. This record gives no figures
of time (decisions 779 and 824); the times of day in Part 2 show where a
recording stopped.

## Part 1. The mutant check, 2026-10-27

**The question.** Since M1-138, the driver skips a script test when no file
that the test read, listed or ran has changed. A skipped test is safe only if
it cannot fail. For each mutant, the check gets the selection of the driver,
runs all tests, and makes sure that each test that fails is a selected test
(decision 767).

**The method** (decisions 818-820). The script is
[`m1-138/mutant-check.py`](m1-138/mutant-check.py), a copy of the script of
M1-133 with two changes. The skipped tests run with the hook of the records of
the Python processes, as the driver runs them. A mutant can add a file, which
the script deletes before it puts the tree back. For each mutant, the script:

1. writes the mutant, or the new file of a mutant that adds a file;
2. builds the default target of the tree;
3. runs `scripts/run-tests.py` with the command line of `check`, from the
   tree's `build.ninja`. The driver writes its selection and runs the
   selected tests;
4. runs with `ctest` each of the other tests, in the environment that the
   driver gives CTest;
5. deletes the new file, puts the tree back with the snapshot functions of
   `scripts/mutate.py`, and makes sure that the store of the driver has the
   same bytes as before.

A fault is a skipped test that does not pass. A skipped test with no result,
or with a skip of its own, is also a fault. All 591 CTest tests of each tree
run for each mutant. The start is the store after `check` at `8df5bcf`.

**The mutants** (decision 819), in
[`m1-138/mutant-check-mutants.json`](m1-138/mutant-check-mutants.json): one of
each new type of input that decision 767 names. Before the run, each mutant was
measured on a scratch copy: the test of mutant 2 and the test of mutant 3
passed without the mutant and failed with it.

| # | Mutant | Type of input | The test that must fail | What must select it |
|---|---|---|---|---|
| 1 | one more line at the end of `docs/STATUS.md` | a document | none | -- |
| 2 | `encode_power()` of `scripts/srgb-reference.py` adds `OFFSET` | a script that a different script loads | `downsample_reference_self_test` | its `read` line of the script (step 2) |
| 3 | tests/zz_reads_a_file.cpp, a test file that opens a file itself | a new file in a listed folder | `test_reads` | its `list` line of `tests/` (step 2) |
| 4 | the last `}` of `scripts/mutation-passes.json` removed | a record file | the `mutants_due` tests that read it | their `read` lines of the file; for a test with the earlier rules, its part `scripts` (decision 531) |
| 5 | `cmake/RunUsage.cmake` accepts only the exit code 3 | a CMake script | the 17 `usage_*` tests | their traces (step 3) |

**The instrument, seen to fail first** (decision 733). With `--plant`, the
script ran mutant 2 in Debug. The driver selected 21 tests and skipped 570,
and one selected test failed. The script moved that test,
`downsample_reference_self_test`, to the skipped tests. Then it reported that
test as the one fault, and no other
([the log](m1-138/mutant-check-plant-debug.log),
[the results](m1-138/mutant-check-plant-debug.json)).

**The result: no fault in the two trees.** No mutant made a test fail that the
driver skipped.

| | Debug | RelWithDebInfo |
|---|---|---|
| Mutants caught by a selected test | 4 | 4 |
| Mutants with no failed test | 1, mutant 1, as decision 819 says | 1, mutant 1, as decision 819 says |
| Faults: a skipped test that did not pass | **0** | **0** |
| Invalid, or with a problem | 0 | 0 |
| Records | [log](m1-138/mutant-check-debug.log), [results](m1-138/mutant-check-debug.json) | [log](m1-138/mutant-check-relwithdebinfo.log), [results](m1-138/mutant-check-relwithdebinfo.json) |

For each mutant ("failed" is the number of selected tests that failed):

| # | Debug: selected / skipped | failed | RelWithDebInfo: selected / skipped | failed | The tests that failed |
|---|---|---|---|---|---|
| 1 | 14 / 577 | 0 | 20 / 571 | 0 | none |
| 2 | 21 / 570 | 1 | 27 / 564 | 1 | `downsample_reference_self_test` |
| 3 | 21 / 570 | 1 | 27 / 564 | 1 | `test_reads` |
| 4 | 20 / 571 | 7 | 26 / 565 | 7 | `mutants_due` and six `mutants_due_*` tests |
| 5 | 131 / 460 | 17 | 131 / 460 | 17 | the 17 `usage_*` tests |

**What the figures say.**

- **Each test that decision 819 names failed, and the driver selected it, in
  the two trees.** Mutants 2 to 5 each made a test fail, thus none of them
  shows a problem of the check (decision 819).
- **A change to a document selects 6 more tests in each tree, and each of
  them keeps the earlier rules.** With no mutant, the driver selected 8 tests
  in Debug and 14 in RelWithDebInfo: the tests with the new rules that read
  the store of the driver (decision 799, corrected), `accept_golden`
  (decision 827), and the tests that skip themselves. With mutant 1, it
  selected 14 and 20. The 6 more tests are `float_equal_guard`,
  `reproducible_build`, `run_tests_in_tree` and the three `mutants_due` tests
  that keep the earlier rules in that tree (decision 826, corrected). Each
  skipped test passed when the script ran it.
- **Mutant 4 selected `mutants_due` by a different rule in each tree.** In
  Debug, `mutants_due` keeps the earlier rules, and its part `scripts`
  selected it. In RelWithDebInfo, it has the new rules and reads the store,
  thus it runs at each `check` (decision 826, corrected).
- **The build of mutant 3 ran the configure again.** `CMakeLists.txt` lists
  `tests/*.cpp` with `CONFIGURE_DEPENDS` for the targets `format` and
  `format-check`, thus the new file changed the result of the glob. No program
  compiles the file. After the mutant, the script put the tree back, and the
  build after that had nothing to do.
- **The test file of mutant 3 is not a C++ test.** `test_reads` runs
  `scripts/check-test-reads.py`, which lists `tests/`. The `list` line of
  its record holds a hash of the names in the folder, thus a new name changes
  the hash.

## Part 2. The read records compared with Process Monitor, 2026-10-29

**The question** (decision 823). Process Monitor records each operation on a
file by each process. For each script test with the new rules, does its
fingerprint hold each path in the repository, the build tree included, that a
process of the test read, examined or listed?

**The recordings** (decisions 822, 829 and 831). Process Monitor ran with the
settings of the spike, [`m1-138/m1-138.pmc`](m1-138/m1-138.pmc). The
assistant started and stopped it from its shell, and the owner accepted each
prompt of Windows. Each full run is the driver with `--full`, with the command
line of `check-full`. A recording includes a script test when all of its read
records were written before the last event of the recording (decision 831).
[`m1-138/completeness.py`](m1-138/completeness.py) found the events of each
minute and of each test.

| Recording | The run | Its last event | All tests passed | It includes |
|---|---|---|---|---|
| Debug, 1 | full, at `75a7344`, 09:00:25 to 09:29:15 | 09:18:03 | no: `shader_record` stopped at its time limit | not used (decision 829) |
| Debug, 2 | full, at `08ac49b`, 10:00:29 to 10:14:41 | 10:10:51 | yes | 83 of the 91 tests with the new rules |
| Debug, 3 | the 10 other script tests and their fixtures, with [`m1-138/short_run.py`](m1-138/short_run.py), 10:30:34 to 10:34:39 | 10:35:22 | yes | the other 8 tests with the new rules |
| RelWithDebInfo, 1 | full, at `08ac49b` with decision 831 not committed, 10:37:52 to 10:47:40 | 10:47:57 | yes | all 85 tests with the new rules |

The cause of the two stops in Debug is not known, as for the three stops of
the spike. The two stops came at 2.82 and 2.10 million events, and in the two,
the tests with no events are mostly the tests of the GPU at the end of the
run: 34 of the 35 in Debug 2 have the label `gpu`. The
event records of Windows show no event that is the same in the two. Alone, with no
Process Monitor, `shader_record` passed two times. The backing files of
Process Monitor of the recordings Debug 1 and Debug 2 are not on the disk at this time, and the cause is not known. Their CSV files, which the script reads,
are kept in `build/`.

**The method.** The script is [`m1-138/compare.py`](m1-138/compare.py). It
gives each process to its test as the spike did: by its command line, else by
the test in its environment, else by its parent. For each path in the
repository that a process of the test read, examined or listed, it examines
the three rules of decision 823: a `read` or `list` line of the test, a part
of the test, or an output of the same recording in the build tree. For the
full recording the lines come from the store after the run; for the short
recording, from the read records of the run, read with the functions of the
driver (decisions 830 and 831). Three faults of the first form of the script
were measured and corrected before its results were used:

- a folder that a process opened with no "Directory File" option counted as
  a read; the script counts it as a question about the folder;
- a read of a file that was not there counted as a read; the script counts
  it as a question;
- CMake writes a file with a temporary name and renames it, and the settings
  record no rename: `CMakeCache.txt.tmp9f9bd` and `CMakeSystem.cmake.tmp`,
  written with OverwriteIf and then opened with Delete. A write of
  `<name>.tmp<hex>` makes `<name>` an output too.

**The instrument, seen to fail first** (decision 823). With `--plant`, the
script removed one `read` line from a copy of the store: the line of
`scripts/read-record/sitecustomize.py` of `abort_listener_self_test`, a file
that only that line makes known. In each tree, that path came back as the only
new miss, as a read and as a question
([Debug](m1-138/compare-debug.log),
[RelWithDebInfo](m1-138/compare-relwithdebinfo.log)).

**The result.**

| | Debug | RelWithDebInfo |
|---|---|---|
| Script tests with the new rules, compared | 91 of 91 | 85 of 85 |
| Reads of a file of the repository, not in the build tree, not known | **0** | **0** |
| Reads of a file of the build tree, not known | 8, the `.pdb` files (decision 833) | 0 |
| Listings not known | 46 | 46 |
| Questions with no read, not known | 2,281 | 1,948 |
| Questions known only by the `list` line of their folder | 30 | 30 |
| Results | [all of them](m1-138/compare-debug.json) | [all of them](m1-138/compare-relwithdebinfo.json) |

The misses by group, and the rulings on them:

| Group | Debug | RelWithDebInfo | Ruling |
|---|---|---|---|
| Not there, in the top folder of the tree: the search of Windows, of the graphics driver, of the security software and of the launcher of PyManager for libraries and programs, for example `vcruntime140.dll`, `cfgmgr32.dll`, `eelog64.dll` and `python.exe` | 1,560 | 1,255 | not inputs; a file that comes there and that the program does not import is a limit (decision 834) |
| Not there, in the build tree | 223 | 223 | not inputs (decision 834) |
| Folders of the build tree, mostly from `ctest --show-only`, and 10 listings of folders that `configure_guard` and `orbsim_bench_smoke` remove and write again in the same run | 222 | 222 | not inputs (decision 834) |
| Folders of the repository: `cmake/`, `build/` and the root, and 2 listings by `get_filename_component(REALPATH)` | 187 | 175 | not inputs (decision 834) |
| The folder of the hook and its `__pycache__`, which Python examines to find the hook before the hook is active | 68 | 68 | corrected: the hook writes a `list` line for each of the two (decision 832) |
| `build/SystemResources/<program>.mun`, the search of Windows for the resources of a program | 57 | 49 | not inputs; a file that comes there is a limit (decision 834) |
| Files of the build tree: in Debug, 8 reads of the `.pdb` files of 7 programs in 7 tests, which an assertion reads to print its call chain | 17 | 1 | the `.pdb` files go into M1-137, item 6 (decision 833) |
| Not there, in the repository: data/skyfield/no-such-file.txt, which a test asks for to make sure that it is not there | 1 | 1 | not an input: the cautious set sees the file if it comes (decision 834) |

**What the figures say.**

- **No test with the new rules read a file of the repository, not in the build
  tree, that its fingerprint does not hold**, in the two trees.
- **One gap was found and corrected** (decision 832). Python examines the
  folder of the hook before the hook is active. Since decision 832, each record has a `list`
  line for the folder and for its `__pycache__`, and `read_record_self_test`
  examines the two lines, with a planted fault that removes only them: 7 of 7
  planted faults found.
- **One gap goes into M1-137** (decision 833): a `.pdb` file is not in the
  part of the programs.

**The limits of this part.** The script sees only what the recorded runs
did: a test that reads a different file in different conditions is not
examined here, as for the spike. The recordings were made before decision 832,
thus the two new `list` lines are examined by the self-test of the hook, not
by a recording.
