# M1-137 item 1 — coverage that changes between runs, measured

Status: measured 2026-10-29 on the first machine, in `build/debug` and
`build/relwithdebinfo`, at commit `c16a8f6`, with clang 23.1.2. Register
decisions 842-845, 858 and 868-870. The scripts are
[`m1-137/coverage-snapshot.py`](m1-137/coverage-snapshot.py) and
[`m1-137/coverage-compare.py`](m1-137/coverage-compare.py); they only read
the tree.

## The questions

1. **Decision 845.** Does a test enter a different set of functions when it
   runs two times at one commit? The selection by function (ADR 0028) skips a
   test when no function that it entered at its last pass changed. A test
   that enters other functions in a different run can go into code that the
   selection did not examine (decision 707). Each such test gets the rule of
   decision 844: the driver takes its program by its bytes.
2. **Decision 858.** Does a counted file of the build tree change between two
   runs at one commit? Decision 857 adds the hashes of these files to the
   parts of the judges of the mutant files. A file that changes at each run
   makes mutant files due at random. A counted file is each file of the build
   tree in a read line of the store, but not the files of the tools:
   `run-tests/`, `test-reads/`, `Testing/`, the three files of
   `scripts/mutants-due.py`, `.ninja_deps` and `.ninja_log`.

## The method (decision 845)

`check-full` ran four times, one run after the other: two times in
`build/debug`, then two times in `build/relwithdebinfo`, not two trees at
the same time. After each run, `coverage-snapshot.py` kept, from the store of
the tree, the functions of each test that takes a program function by
function, and the hash of each counted file. Then `coverage-compare.py`
compared the two snapshots of each tree:

```
cmake --build build/debug --target check-full
python docs/measurements/m1-137/coverage-snapshot.py build/debug <scratch>/debug-1.json
cmake --build build/debug --target check-full
python docs/measurements/m1-137/coverage-snapshot.py build/debug <scratch>/debug-2.json
python docs/measurements/m1-137/coverage-compare.py <scratch>/debug-1.json <scratch>/debug-2.json
```

and the same for `build/relwithdebinfo`. **The script that compares can
find a difference:** a copy of a snapshot with one function and one hash changed by
hand gave one test with a difference and one file with a different hash.

## The results

All four runs passed.

| Tree | Time of the two runs | Tests by function | Tests with a different set of functions | Counted files with a different hash |
|---|---|---|---|---|
| Debug | 238 s and 243 s | 539 | 0 | 0 of 1,391 |
| RelWithDebInfo | 115 s and 114 s | 535 | 0 | 0 of 1,391 |

- **Decision 845:** no test entered a different set of functions in the
  second run. Thus no test goes into the list of decision 844 from this
  measurement.
- **Decision 858:** no counted file changed between two runs at one commit.
  Thus the rule of decision 857 does not make mutant files due at random.

The two scripts were put into this folder after the measurement, with one
change: the scratch copy left out each file with a name that starts with
`mutants-due-`, and the committed copy names the three files. On
2026-10-30, the committed `coverage-compare.py` gave the same output on the
four snapshots, and a snapshot of each tree at `5bd06c7`, made with the
committed `coverage-snapshot.py`, held the same 1,391 counted paths, with no
difference of functions or of hashes.

## The check that follows (decisions 842, 843, 868 and 869)

The measurement finds only the differences of two runs. From item 1 on, each
full run of `scripts/run-tests.py` examines them:

- Before CTest starts, a full run calculates the selection as a run without
  `--full` calculates it (decision 868). The tests that it skips are the
  tests that the check examines; the run then runs each test.
- After the run, a test that the selection skips and that entered a function
  that its stored entry does not hold is a fault. A program that the entry
  does not hold counts with all its functions new. Fewer functions are not a
  fault.
- Each fault fails the run, and the problem names the test, the program, the
  number of new functions and the first three of them (decision 843).
- The test keeps the entry of its last pass (decision 869). Thus each full
  run fails again while the difference comes again, until the test is in
  `TAKEN_BY_BYTES` with a decision of the owner (decision 844).

A full run thus also calculates, before CTest starts, the values of the files
that the stored tests read. Decision 807 stores these values, as a run
without `--full` does.

**The first runs of the check**, on 2026-10-30: one full run of each tree,
one after the other, from a working tree with the changes of item 1.

| Tree | Time | Tests passed | Faults | "The fingerprints took" | Before item 1 |
|---|---|---|---|---|---|
| Debug | 248 s | 591 of 591 | 0 | 2.5 s | 1.5 s |
| RelWithDebInfo | 122 s | 591 of 591 | 0 | 2.0 s | 1.4 s to 1.5 s |

Each run failed only because the working tree had changes, as it must.
From the store that these runs wrote, the selection skips 583 tests in
Debug and 577 in RelWithDebInfo. These include each test that takes a
program function by function, 539 and 535, thus the next full run examines
all of them.

**Seen to fail** (decision 855): `run_tests_function_level` takes one
function out of the stored entry of `a_beta` after its full run. The next
full run must fail and name the test, the program and the function, and a
second full run must fail again, because the test kept its entry.
