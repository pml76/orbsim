# M1-133 — the selection proved, and measured

Kind: measurement record
Binding: no — this file records what was measured, on the dates given
Task: [M1-133](../plan/tasks/m1-133-selection-proved.md), register decisions
552, 561, 592 and 720-735.

This record has two parts. Part 1 is the mutant check of decision 552, which
is finished. Part 2 is the replay of M1-118's 16 commits of decision 592,
which comes next.

## Part 1. The mutant check, 2026-10-21

**The question.** A skipped test is safe only if it could not fail. For each
mutant of `scripts/mutants/m1-118.json`, the check gets the selection of the
driver, runs all tests, and makes sure that each test that fails is a
selected test (decision 552).

**The method** (decisions 722-724). The script is
[`m1-133/mutant-check.py`](m1-133/mutant-check.py). For each mutant, it:

1. writes the mutant into its file, as `scripts/mutate.py` does;
2. builds the default target of the tree, which holds each program that a
   test starts;
3. runs `scripts/run-tests.py` with the command line of `check`, from the
   tree's `build.ninja`. The driver writes its selection and runs the
   selected tests;
4. runs with `ctest` each of the other tests, in the environment that the
   driver gives CTest;
5. puts the tree back with the snapshot functions of `mutate.py`, and makes
   sure that the store of the driver has the same bytes as before.

A fault is a skipped test that does not pass. A skipped test with no result,
or with a skip of its own, is also a fault. All 590 CTest tests of each tree
run for each mutant, with the 65 tests that have the label `gpu`. The start is
the store after `check` at `5ac4b2a`.

**The instrument, seen to fail first** (decision 733). With `--plant`, the
script moves a failed selected test to the skipped tests. It ran on mutant 3
in Debug: the driver selected 41 tests and skipped 549, and two selected tests
failed. The script moved one of them, and it reported that test as the one
fault, and no other ([the log](m1-133/mutant-check-plant-debug.log),
[the results](m1-133/mutant-check-plant-debug.json)).

**The result: no fault in the two trees.** No mutant made a test fail that the
driver skipped.

| | Debug | RelWithDebInfo |
|---|---|---|
| Mutants caught by a selected test | 17 | 16 |
| Mutants caught by the build, at a `static_assert` | 3 | 3 |
| Mutants with no failed test | 0 | 1 |
| Faults: a skipped test that did not pass | **0** | **0** |
| Invalid, or with a problem | 0 | 0 |
| Tests selected for a mutant that got to the tests, of 590 | 27 to 90, mean 39.3 | 30 to 58, mean 41.4 |
| Time of the run | 1 h 51 min | 55 min |
| Records | [log](m1-133/mutant-check-debug.log), [results](m1-133/mutant-check-debug.json) | [log](m1-133/mutant-check-relwithdebinfo.log), [results](m1-133/mutant-check-relwithdebinfo.json) |

For each mutant ("failed" is the number of selected tests that failed):

| # | Mutant | Debug | selected / skipped | failed | RelWithDebInfo | selected / skipped | failed |
|---|---|---|---|---|---|---|---|
| 1 | wrapTau without its fix: a tiny negative angle wraps to 2 pi | build, `static_assert` | -- | -- | build, `static_assert` | -- | -- |
| 2 | wrapPi folds every negative remainder through 2 pi again | build, `static_assert` | -- | -- | build, `static_assert` | -- | -- |
| 3 | the cube back in orbitInfo's mean motion | caught, selected | 41 / 549 | 2 | caught, selected | 47 / 543 | 2 |
| 4 | the task's first form, sqrt(mu / a) / a | caught, selected | 41 / 549 | 2 | caught, selected | 47 / 543 | 2 |
| 5 | orbitInfo's period back to 2 pi / n | caught, selected | 41 / 549 | 1 | caught, selected | 47 / 543 | 1 |
| 6 | propagateElements' period back to its old form | caught, selected | 37 / 553 | 1 | caught, selected | 43 / 547 | 1 |
| 7 | propagateElements without its unreachable-anomaly test | caught, selected | 37 / 553 | 1 | caught, selected | 43 / 547 | 1 |
| 8 | double-double never scales, as before M1-118 | build, `static_assert` | -- | -- | build, `static_assert` | -- | -- |
| 9 | double-double scales its error term back by half the factor | caught, selected | 53 / 537 | 2 | caught, selected | 58 / 532 | 2 |
| 10 | the backward-rotation comparison: the signed angle again | caught, selected | 27 / 563 | 3 | caught, selected | 33 / 557 | 3 |
| 11 | the square roots swapped in trueToEccentricAnomaly | caught, selected | 33 / 557 | 4 | caught, selected | 39 / 551 | 4 |
| 12 | the square roots swapped in eccentricToTrueAnomaly | caught, selected | 32 / 558 | 4 | caught, selected | 38 / 552 | 4 |
| 13 | trueToEccentricAnomaly answers a parabola | caught, selected | 33 / 557 | 1 | caught, selected | 39 / 551 | 1 |
| 14 | meanToEccentricAnomaly answers a parabola | caught, selected | 29 / 561 | 1 | caught, selected | 35 / 555 | 1 |
| 15 | trueToEccentricAnomaly answers past the asymptote | caught, selected | 33 / 557 | 1 | caught, selected | 39 / 551 | 1 |
| 16 | trueToEccentricAnomaly takes a NaN | caught, selected | 33 / 557 | 1 | caught, selected | 39 / 551 | 1 |
| 17 | eccentricToTrueAnomaly back to sinh and cosh, which overflow | caught, selected | 32 / 558 | 1 | caught, selected | 38 / 552 | 1 |
| 18 | eccentricToMeanAnomaly returns an infinite mean anomaly | caught, selected | 31 / 559 | 1 | caught, selected | 37 / 553 | 1 |
| 19 | angleBetween without its assertion | caught, selected | 90 / 500 | 2 | caught, selected | 30 / 560 | 1 |
| 20 | stateFromElements asserts the shape again instead of reporting it | caught, selected | 45 / 545 | 1 | no test failed | 51 / 539 | 0 |

**What the figures say.**

- **Three mutants stop at the build** in the two trees, at a `static_assert`:
  1, 2 and 8. Thus 17 mutants in Debug and 17 in RelWithDebInfo get to the
  tests.
- **Mutant 20 has no failed test in RelWithDebInfo.** It puts back an
  assertion where the code reports since M1-118, and the assertion is empty with
  `NDEBUG`. The `note` field of `m1-118.json` says that this mutant must run
  in the Debug tree. In RelWithDebInfo it has nothing to catch, thus the
  driver could not skip a test that fails.
- **Mutant 19 in RelWithDebInfo** changes no compiled code of `angleBetween`,
  because the assertion is empty with `NDEBUG`. Its one failed test is
  `mutants_due_probe_shaders`; see the finding below.
- **A change to a header that most files include selects a small number of
  tests.** Mutant
  10 changes `Math.hpp`, which 74 of the 159 objects of the project's own code
  include in Debug. The driver selected 27 tests, because only those entered
  the changed function.
- **The estimates were too high.** The task estimated 2 to 2.5 hours in Debug
  and 1 to 1.5 hours in RelWithDebInfo (decision 722). The estimate counted
  four mutants of headers that almost all files include, but two of them stop
  at the build.

### A finding: `mutants_due_probe_shaders` fails for a cause that is not the mutant

**What was seen.** With mutants 10 and 19 in Debug, and 10 and 19 in
RelWithDebInfo, the selected script test `mutants_due_probe_shaders` failed:
`mutants-due.py` reported `m1-16.json` due, where the test must find it
current. With the other mutants, the same test was selected and passed. It
is not a fault of the selection, because the driver selected the test. But
the test failed for a cause that is not the mutant.

**The cause, read from the code and the logs.** Mutants 10 and 19 change
`Math.hpp`, which the application links, and the application's bytes change.
The probe tests entered no changed function, thus the driver skipped them and
their setup tests. `mutants-due.py` uses the list of shaders that the driver
stored for a probe test only while the application is byte for byte what it
was when the list was stored (decisions 616 and 629). It is not, thus the
script counts each shader, and `m1-16.json` is due. Before M1-132, a change to
the application's bytes ran all probe tests again, and their lists were new.

**What is not proved.** Mutant 19 in RelWithDebInfo changes no compiled code,
but the failure shows that the application's bytes changed. A possible cause is that the lines of
`Math.hpp` moved, and with them the debug information. This was not measured.
The effect on an edit with no mutant comes from the same mechanism, but no
such edit was measured.

**The effect.** In `check`, a change that changes the application's bytes,
but no function that the probe tests entered, makes `mutants_due_probe_shaders`
fail, until the probes run again (`check-full`, or
`ctest -R "^mutants_due_probe_shaders$"`, which runs them as its setup tests).
The correction is for the owner to rule, and that ruling is not given at this
time. Four alternatives went to the owner on 2026-10-21.

**The correction, measured** (decisions 737-739). The owner ruled the
correction on 2026-10-21: when the driver skips a test as unchanged, it gives
the stored list of shaders of that test the hash of the current application,
before CTest starts. After the correction (`75d2a9f`), the mutant check of
mutants 10 and 19 ran again in the two trees, at `4444de7`:

| | Debug | RelWithDebInfo |
|---|---|---|
| Mutant 10 | 27 selected, 2 failed, each selected | 33 selected, 2 failed, each selected |
| Mutant 19 | 90 selected, 1 failed (`angle_between_zero_aborts`), selected | 30 selected, none failed |
| `mutants_due_probe_shaders` | passed | passed |
| Faults | 0 | 0 |
| Records | [log](m1-133/recheck-debug.log), [results](m1-133/recheck-debug.json) | [log](m1-133/recheck-relwithdebinfo.log), [results](m1-133/recheck-relwithdebinfo.json) |

The selections have the same number of tests as before the correction,
thus the correction changed no selection. The driver's self-test has four new cases,
and each of the three mutants of `scripts/mutants/m1-133.json` made one of
them fail first; the pass of that file caught 3 of 3.

### Two more observations

- **A new environment runs each test again.** At the start of the
  RelWithDebInfo run, the driver ran all 590 tests, not the 7 that always run
  there. The environment part of each fingerprint is a hash of `PATH`, of each
  `VK_*` variable and of each `ORBSIM_*` variable (decision 501). In the
  Debug tree, the 589 stored tests had the environment part `91514d993e46`,
  and the new session of the assistant gave `47cc3fd11920`. The owner had
  changed the settings of the proxies. The proxy variables are not part of the
  hash, thus `PATH` probably changed with them. All 590 tests passed, and the
  results of the run stay correct.
- **A stopped run, and its restore.** The first RelWithDebInfo run stopped at
  approximately 17:14, during the build of mutant 3, when the session of the
  assistant ended. It left mutant 3 in `src/orbit/Orbit.cpp` and its snapshot
  in `build/mutate-snapshot-relwithdebinfo`. The change was the mutant and
  nothing else. `python scripts/mutate.py --restore --tree build/relwithdebinfo`
  put the tree back, and then a build had no work to do. The run then ran
  again from its first mutant; the table above is that second run.

## Part 2. The replay of M1-118's 16 commits

Comes next (decisions 726-731).
