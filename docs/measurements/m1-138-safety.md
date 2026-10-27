# M1-138 — the safety of the selection of the script tests

Kind: measurement record
Binding: no — this file records what was measured, on the dates given
Task: [M1-138](../plan/tasks/m1-138-script-tests-each-commit.md), register
decisions 767 and 818-828.

This record has two parts (decision 767). Part 1 is the mutant check: it
examines if the driver skips a script test that fails. Part 2 compares the
read records with Process Monitor. Part 2 is not done at this time. This
record gives no figures of time (decisions 779 and 824).

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

## Part 2. The read records compared with Process Monitor

Not done at this time. Decisions 822 and 823 give the method: one recording
in each `check` tree, which the owner makes, and a script that compares the
recordings with the read records, after it finds a planted fault.
