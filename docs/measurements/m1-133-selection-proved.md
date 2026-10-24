# M1-133 — the selection proved, and measured

Kind: measurement record
Binding: no — this file records what was measured, on the dates given
Task: [M1-133](../plan/tasks/m1-133-selection-proved.md), register decisions
552, 561, 592, 720-735 and 737-761.

This record has two parts. Part 1 is the mutant check of decision 552: it
examines if the selection skips a test that fails. Part 2 is the replay
of M1-118's 16 commits of decision 592: it counts the test runs that the
selection removes, and their causes.

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

## Part 2. The replay of M1-118's 16 commits, 2026-10-21 and 2026-10-22

**The question.** On the changes of M1-118, how many test runs does the
selection by function remove, against the selection by program of ADR 0027?
And what makes each test run (decisions 561, 592, 730 and 731)?

**The method** (decisions 726-729, 745, 746 and 748-756).

1. The replay ran in a second checkout, build/wt133, on a local branch. Its
   base is `30b114c`, the commit before M1-118. One commit, `8e91346`, adds
   the items of decision 726 to the base. These are steps A and B, the
   coverage hook, the recorder and the linker map. They are also
   `ORBSIM_TEST_NAME`, the options of reproducible builds and the read
   records. Its driver and its recorder are the same, byte for byte, as those
   of `master` on 2026-10-24.
2. `git cherry-pick` copied the 16 commits on top, one at a time. For each
   copy, `git patch-id --stable` of its change is that of its initial commit:
   16 of 16, calculated again with git on 2026-10-24.
3. The replay ran in Debug only (decision 728). At the base, a full run passed
   533 of 533 tests in 226.42 s, and the driver wrote "full check passed" for
   the base.
4. At each commit, the tree was built. Then the measurement script
   [`replay-count.py`](m1-133/replay/replay-count.py) calculated the two
   selections and the causes, from the same inputs as the driver. Then the
   driver ran the tests that it selected.
5. The selection by function of the script must agree with that of the driver
   (decision 729). It agreed at each commit, with 0 differences. The script
   was seen to fail first (decision 733). At the base, it reported a test name
   that was removed from its own selection, with exit code 1.
6. The count by program is that of [`shadow.py`](m1-133/replay/shadow.py)
   (decision 756). Each test keeps the bytes of each of its programs at its
   last run by program, as a driver by program stores them. The count supposes
   that each test that runs only by program passes, because no function that
   it entered changed.

The replay ran the 16 commits with no stop. The driver wrote no problem line
(decision 753). The only test that failed is `mutants_due_probe_shaders`, at 9
commits, because each commit has the `mutants-due.py` of its time (decision
742).

**The result** (decisions 731, 757 and 758).

| Figure | Replay | Earlier figure |
|---|---|---|
| C++ tests: runs by function, against by program | 434 of 1,396 = **31.1 %** | the spike, 840 of 4,646 = 18.1 % (decision 543) |
| C++ tests: time by function, against by program | 468.23 s of 932.53 s = **50.2 %** | the spike, 1,997 s of 6,158 s = 32.4 % |
| All tests by program, of the 8,771 possible runs, with the GPU tests fingerprinted | 2,029 = **23.1 %** | the simulation, 16 % (decision 490) |
| The same, with the 63 GPU tests counted as run at each commit | 2,655 = **30.3 %** | the simulation, 2,284 = 26 % (decision 483) |
| All tests by function, of the 8,771 possible runs | 965 = **11.0 %** | -- |
| All tests: time by function, against by program | 3,047.51 s of 3,796.23 s = **80.3 %** | -- |

For each commit (each count is all tests / C++ tests / GPU tests):

| Commit | What it changes | Tests | By function: all / C++ / GPU | By program: all / C++ / GPU | C++ time, s: by function / by program | All time, s: by function / by program | Failed |
|---|---|---|---|---|---|---|---|
| `374745f` | the owner's rulings (documents) | 533 | 16 / 1 / 1 | 16 / 1 / 1 | 0.17 / 0.17 | 121.56 / 121.56 | -- |
| `53f929b` | the fuzzers before the push (register) | 533 | 16 / 1 / 1 | 16 / 1 / 1 | 0.17 / 0.17 | 121.56 / 121.56 | -- |
| `4963695` | finding 1.1, `wrapTau` and `wrapPi` (`Scalar.hpp`) | 537 | 74 / 55 / 1 | 506 / 436 / 63 | 24.71 / 232.86 | 152.03 / 502.40 | `mutants_due_probe_shaders` |
| `0eb2c93` | finding 1.2, the mean motion (`Orbit.cpp`) | 539 | 46 / 30 / 1 | 58 / 42 / 1 | 9.37 / 12.87 | 133.84 / 137.34 | `mutants_due_probe_shaders` |
| `e334905` | finding 1.4, `propagateElements` (`Orbit.cpp`, `Orbit.hpp`) | 540 | 36 / 20 / 1 | 59 / 43 / 1 | 5.41 / 12.95 | 129.87 / 137.42 | `mutants_due_probe_shaders` |
| `1399abf` | finding 1.5, the angular velocity (`Math.hpp`) | 543 | 38 / 22 / 1 | 328 / 261 / 63 | 13.20 / 164.40 | 137.68 / 431.09 | `mutants_due_probe_shaders` |
| `adb1074` | decisions 475 and 476 (register) | 543 | 16 / 1 / 1 | 16 / 1 / 1 | 0.17 / 0.17 | 121.56 / 121.56 | `mutants_due_probe_shaders` |
| `937e49f` | findings 1.6 and 3.1, the anomaly converters (`Orbit.cpp`, `Orbit.hpp`) | 553 | 55 / 39 / 1 | 69 / 53 / 1 | 10.73 / 16.03 | 135.20 / 140.49 | `mutants_due_probe_shaders` |
| `8bc7be7` | the period (`Orbit.cpp`) | 554 | 49 / 33 / 1 | 70 / 54 / 1 | 9.79 / 16.13 | 134.26 / 140.59 | `mutants_due_probe_shaders` |
| `73dc795` | finding 1.3, double-double (`DoubleDouble.hpp`) | 555 | 51 / 35 / 1 | 102 / 86 / 1 | 16.72 / 30.03 | 141.19 / 154.50 | `mutants_due_probe_shaders` |
| `5047abb` | finding 1.7, `angleBetween` (`Math.hpp`, `CMakeLists.txt`, `cmake/`) | 556 | 158 / 70 / 63 | 360 / 272 / 63 | 102.97 / 167.57 | 381.57 / 446.17 | -- |
| `2c5f1e8` | `stateFromElements` reports (`Orbit.cpp`, `Orbit.hpp`) | 557 | 52 / 36 / 1 | 71 / 55 / 1 | 11.85 / 16.21 | 136.32 / 140.68 | `mutants_due_probe_shaders` |
| `27cd31b` | finding 3.12, the fuzzers (their sources, corpus, a mutant file) | 557 | 114 / 30 / 61 | 114 / 30 / 61 | 87.60 / 87.60 | 359.77 / 359.77 | -- |
| `3479028` | two mutants of M1-118 (a mutant file) | 557 | 114 / 30 / 61 | 114 / 30 / 61 | 87.60 / 87.60 | 359.77 / 359.77 | -- |
| `b554d5d` | the record of the mutation pass (`scripts/mutation-passes.json`) | 557 | 114 / 30 / 61 | 114 / 30 / 61 | 87.60 / 87.60 | 359.77 / 359.77 | -- |
| `1edfbc4` | the push recorded (`STATUS.md`) | 557 | 16 / 1 / 1 | 16 / 1 / 1 | 0.17 / 0.17 | 121.56 / 121.56 | -- |
| **Total** | | **8,771** (7,373 C++, 1,008 GPU) | **965 / 434 / 258** | **2,029 / 1,396 / 382** | **468.23 / 932.53** | **3,047.51 / 3,796.23** | |

The time of a test is its time in the full run of the base. The 24 tests that
M1-118 adds use the time of their first run in the replay, 7.92 s together
(decision 750). The time of the C++ tests comes from `shadow.py`, and the
other times from the table of the helper. The two scripts give the time of
each commit to 0.01 s with a different sequence of steps. Thus their results
are different by 0.01 s at three commits. The totals are 3,047.50 s and
3,047.51 s.

**The C++ tests against the spike, for each commit** (the spike's figures are
in M1-132's task document, section 9):

| Commit | Spike: by program / by function | Replay: by program / by function | Test files that the commit changes |
|---|---|---|---|
| `4963695` | 505 / 121 | 436 / 55 | `tests/test_math.cpp`, `tests/test_orbit.cpp` |
| `0eb2c93` | 507 / 30 | 42 / 30 | `tests/test_orbit_scales.cpp` |
| `e334905` | 508 / 183 | 43 / 20 | `tests/test_orbit.cpp` |
| `1399abf` | 511 / 22 | 261 / 22 | `tests/test_math.cpp` |
| `937e49f` | 521 / 209 | 53 / 39 | `tests/test_orbit.cpp`, `tests/test_orbit_scales.cpp` |
| `8bc7be7` | 522 / 38 | 54 / 33 | `tests/test_orbit_scales.cpp` |
| `73dc795` | 523 / 35 | 86 / 35 | `tests/test_double_double.cpp` |
| `5047abb` | 524 / 166 | 272 / 70 | -- |
| `2c5f1e8` | 525 / 36 | 55 / 36 | `tests/test_orbit.cpp` |
| the other 7 commits | 0 / 0 | 94 / 94 | |
| **Total** | **4,646 / 840** | **1,396 / 434** | |

**What the figures say.**

- **The counts by program are lower than the spike's, for a measured cause.**
  The spike's tree, build/wtfn, has a `build.ninja` of 2026-10-15. It has none
  of the two options of reproducible builds,
  `-mno-incremental-linker-compatible` and `/Brepro`. The `build.ninja` of
  build/wt133 has them 1,022 and 50 times. Without them, a program that links
  again gets new bytes (M1-129, item 1). Thus the spike counted each test of
  each program that links `orbsim_core` at each commit that changes its code:
  505 to 525 C++ test runs. In the replay, a change to `Orbit.cpp` changed the
  bytes of 3 of the 43 programs (decision 756). Then 42 to 55 C++ tests ran by
  program. Thus the replay's 31.1 % is a different ratio, and not a selection
  that removes less. By program, the replay counts 1,396 runs and the spike
  4,646. By function, the replay counts 434 and the spike 840.
- **By function, the replay counts less than the spike at five commits, and
  the same at four.** The five commits change a test file, or a header that
  most files include. Steps A and B removed two causes that the spike counted.
  First, an edit to a test file ran each test of its program: 147 runs in the
  spike. Second, a line number in an assertion message moved the functions
  below an edit. These two causes were not measured one by one (decision 757).
- **At the other 7 commits, the spike counted 0 C++ runs and the replay 94.**
  The C++ test that skips itself on Windows runs at each commit (decision
  752). At each of the three commits that change files in `scripts/`, 29 more
  Catch2 tests run (cause 4, decision 508). They read the images of the
  probes. The spike did not count these.
- **16 tests run at each commit**: 15 script tests and the C++ test that
  skips itself. The script tests are `accept_golden`, `configure_current`,
  `float_equal_guard`, `lint_deps`, `memory_pool`, `parallel_tests`,
  `tonemap_constants` and the eight `mutants_due` tests. They read files that
  each commit changes, in the build tree for 14 of them (cause 8). The 16
  tests use 121.56 s at each commit, which is 1,945 s of the 3,047.51 s by
  function, 64 %. Thus the time of all tests by function is 80.3 % of the
  time by program, where that of the C++ tests is 50.2 %.
- **Against the simulation of M1-129** (decision 758). The task document of
  M1-129 records that the simulation saw only the files on the command line of
  a test. Thus it can count too low. The document gives only ranges for the
  commits. At a commit that changes only a document, the simulation counted
  only the 63 GPU tests. In the replay, the 16 tests above give 256 runs, 2.9
  points of the 8,771. At each of the three commits that change files in
  `scripts/`, 114 tests ran. These are 82 or 83 script tests (cause 5,
  decision 531) and 30 tests related to them (cause 4). The remainder of the
  difference cannot be divided by commit, because the simulation kept no count
  for each commit.
- **`5047abb` changes `CMakeLists.txt` and `cmake/`, and 158 tests ran by
  function.** 99 ran by the cautious rule (cause 6, decisions 489 and 505). A
  test that reads `data/` or `tests/golden/` is fingerprinted on these folders
  and on the build files. 81 are script tests (cause 5). All 63 GPU tests ran.
- **At `1399abf`, only cause 3 ran tests.** A test case added to
  `tests/test_math.cpp` changed the linker directives of `test_math.exe`, and
  its 18 tests ran (decision 711 (7)). The changed function,
  `integrateAngularVelocity`, has no caller in `src/` or in a different test
  file.

**The causes** (decisions 730 and 751). A test with two causes counts for each
cause, and one time in the totals. "By program only" is a test that only the
selection by program runs.

| Commit | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | By program only |
|---|---|---|---|---|---|---|---|---|---|
| `374745f` | -- | -- | -- | -- | -- | -- | 1 | 15 | 0 |
| `53f929b` | -- | -- | -- | -- | -- | -- | 1 | 15 | 0 |
| `4963695` | 34 | -- | 16 | -- | -- | -- | 5 | 19 | 432 |
| `0eb2c93` | 15 | -- | 12 | -- | -- | -- | 4 | 15 | 12 |
| `e334905` | 11 | -- | 7 | -- | -- | -- | 3 | 15 | 23 |
| `1399abf` | -- | -- | 18 | -- | -- | -- | 5 | 15 | 290 |
| `adb1074` | -- | -- | -- | -- | -- | -- | 2 | 14 | 0 |
| `937e49f` | 24 | 9 | -- | -- | -- | -- | 12 | 15 | 14 |
| `8bc7be7` | 24 | -- | 7 | -- | -- | -- | 3 | 15 | 21 |
| `73dc795` | 29 | 5 | 2 | -- | -- | -- | 3 | 15 | 51 |
| `5047abb` | 39 | -- | -- | 25 | 81 | 99 | 3 | 18 | 202 |
| `2c5f1e8` | 21 | -- | 13 | -- | -- | -- | 2 | 16 | 19 |
| `27cd31b` | -- | -- | -- | 30 | 82 | -- | 2 | 14 | 0 |
| `3479028` | -- | -- | -- | 30 | 83 | -- | 1 | 15 | 0 |
| `b554d5d` | -- | -- | -- | 30 | 83 | -- | 1 | 15 | 0 |
| `1edfbc4` | -- | -- | -- | -- | -- | -- | 1 | 15 | 0 |
| **Total** | **197** | **14** | **75** | **115** | **329** | **99** | **49** | **246** | **1,064** |

- Cause 1: 162 tests entered a changed function with a change to its own text.
  47 tests entered one with no change to its own text. A test can have the
  two.
- Cause 7: 16 runs of the test that skips itself, 9 runs of
  `mutants_due_probe_shaders` after it failed at the commit before, and 24
  runs of new tests.
- Cause 8: a file in the build tree 215, a different file 51, a program 15,
  and the definition of the test 3. A test can have more than one.

**The changed functions, sorted by hand** (decisions 730 and 761). A function
of a header in `src/` is cause 1, also where the linker took its copy from the
object of a test file. Cause 2 is a test case, a function of the test's own
file, or a template of a library in the object of that file. It counts only at
a commit that changes that file: at `937e49f` and `73dc795`.
[`hand-sort.py`](m1-133/replay/hand-sort.py) holds the sort, made from
`git show -W` of each commit, and examined again against the diffs on
2026-10-24.

| Commit | The text of the function changed | The text of the function did not change |
|---|---|---|
| `4963695` | `orb::wrapTau`, `orb::wrapPi` (`Scalar.hpp`) | -- |
| `0eb2c93` | `orb::orbitInfo` | -- |
| `e334905` | `orb::propagateElements` | -- |
| `937e49f` | the four anomaly converters; `orb::describe(OrbitError)` (`Orbit.hpp`, with its copies in 3 objects of tests) | `solveUniversalAnomaly`; the three copies of `safeguardedRoot`, for its lambda and for the two lambdas of `meanToEccentricAnomaly` |
| `8bc7be7` | `orbitInfo`, `propagateElements` | three mp-units functions for rad/s, removed from two programs |
| `73dc795` | `orb::split`, `orb::twoProduct` (`DoubleDouble.hpp`) | -- |
| `5047abb` | `orb::angleBetween` (`Math.hpp`, with its copies in 7 objects) | `std::vector<std::stacktrace_entry>::_Xlength`; the operators `+` and `/` of `Scalar<one>` in `ProbeSidecar.cpp.obj` |
| `2c5f1e8` | `orb::stateFromElements` | -- |

Why the compiled code changed where the text did not, measured on 2026-10-24
with the data that the replay kept:

- **`937e49f`.** The commit puts `NoEccentricAnomaly` before
  `SolverDidNotConverge` in `OrbitError`, thus the value of
  `SolverDidNotConverge` goes from 4 to 5. The four functions return it. They
  keep their length, 1,211 bytes for `solveUniversalAnomaly` and 1,226 bytes
  for each copy of `safeguardedRoot`, with new bytes. Until decision 761,
  `hand-sort.py` sorted the two copies for the lambdas of
  `meanToEccentricAnomaly` with the changed text. The cause: the name of a
  lambda holds the name of the function that holds it. This moved 1 test: the
  tests of cause 1 with no change to the text of a function went from 46 to
  47.
- **`8bc7be7`.** The commit removes the call `info.meanMotion.value()` from
  `orbitInfo`, which calculates the period with the new `periodOf`. After the
  commit, the three functions are not in `test_orbit.exe` and
  `test_orbit_elements.exe`; `test_math.exe` and `test_orbit_scales.exe` keep
  them with the same digests. A removed function counts as changed (decision
  547).
- **`5047abb`.** `_Xlength` for `std::stacktrace_entry` is a new function
  without a flag in `test_camera_controller.exe`: 21 tests. The only change in
  `src/` is the new assertion of `angleBetween`, and an assertion keeps the
  call chain in a `std::stacktrace` (`src/core/Contract.hpp`). The operator
  `/` of `Scalar<one>` went from 148 to 121 bytes, and its call to
  `__std_terminate` was removed. The operator `+` went from 118 to 148 bytes,
  with a new call to `__std_terminate`. The commit changes the text of none of
  the two. Why the compiler made this change was not examined.

**What was examined on 2026-10-24.** The helper's
[report](m1-133/replay/helper-report.md) said that its figures were not
examined at that time against the files in its folder. They were examined as
follows:

- the totals, counted again from the files of the 16 commits: they agree;
- the patch-ids, calculated again with git: 16 of 16 agree;
- the driver and the recorder of `8e91346`, against `master`: the same bytes;
- the causes and their parts, counted again: they agree, but for the
  correction of decision 761;
- the 24 new tests and the times of their first runs: they agree;
- the base's full run and the fault put in to make the script fail: as the
  report says;
- the hand sort, against the diff of each commit: two copies of
  `safeguardedRoot` corrected (decision 761);
- the time of the C++ tests, added to `shadow.py` (decision 757): its other
  fields did not change.

**Limits.**

- The replay counts the runs. It does not prove that a skipped test is safe,
  because only the selected tests ran. Part 1, the mutant check, examines
  that.
- The count by program supposes that each test that runs only by program
  passes (decision 756).
- The replay ran in Debug only (decision 728).
- build/wt133, its branch and the scratch folder with the function digests of
  each commit are removed at the end of M1-133 (decision 759). A new replay
  must make the commit of decision 726 again.

**The records**: the helper's [report](m1-133/replay/helper-report.md), word
for word in its figures; [the table](m1-133/replay/final-table.txt);
[the count by program](m1-133/replay/shadow.txt);
[the hand sort](m1-133/replay/hand-sort.txt); the base's report,
`base-results.xml`; and a folder for each commit with `count.json`,
`after.json`, `compare.json`, `patch-id.txt`, `steps.txt` and
`driver-selected.txt`. The scripts are in the same folder.
