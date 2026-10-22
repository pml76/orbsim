# The replay of M1-118's 16 commits: the helper's report (2026-10-22)

Kind: measurement, the report of the helper that ran the replay, word for
word in its figures
Binding: no -- the figures are not yet examined against the files beside this
report; that is the first step of the next session (M1-133's task document)

The replay ran in build/wt133, on the local branch scratch/m1-133-replay:
the base `30b114c`, the infrastructure commit `8e91346`, and the copies of the
16 commits up to `181504a`, the copy of `1edfbc4`. Register decisions 726-731,
742, 745, 746 and 748-756 rule it.

## Result

- At all 16 commits, the script's selection by function agrees with the
  driver's `selected.txt`: 0 differences.
- The driver wrote no "run-tests:" problem line at any commit.
- The only failed test is `mutants_due_probe_shaders` of the old commits
  (decision 742).
- No hand merge was necessary: `git cherry-pick` copied `5047abb` as the other
  15 commits. For each of the 16 copies, `git patch-id --stable` of the diff
  from the copy before is the same as that of the initial commit.
- There are three tests `count_wraparound_*_aborts` (addition,
  multiplication, subtraction), not four as decision 749 said. From `5047abb`
  on, they take their probe program function by function.

**The base:** CTest "100% tests passed, 0 tests failed out of 533", "Total
Test time (real) = 226.42 sec"; the driver "run-tests: 533 ran, 0 skipped as
unchanged" and "full check passed at 8e913466115bbb0501db78117df923a262a08668,
all 533 tests run". **The planted difference:** a test name removed from the
script's set was reported, with exit code 1.

## The table

Each count is all / C++ / GPU tests, then the time estimate, from the base's
times and, for 24 tests, the time of their first run (decision 750). "By
function" is what the driver selected. "(B)" is the count by program with a
record of its own (decision 756); it supposes that each test that runs only
by program passes, because no function that it entered changed. "(A)" is the
same rule against the driver's store, for the data only.

| Commit → copy | Listed | By function | (B) by program | (A) by program | Ran / skipped (driver) |
|---|---|---|---|---|---|
| 374745f → c4f33f7 | 533 | 16/1/1, 121.56 s | 16/1/1, 121.56 s | 16/1/1, 121.56 s | 16 / 517 |
| 53f929b → 8049b16 | 533 | 16/1/1, 121.56 s | 16/1/1, 121.56 s | 16/1/1, 121.56 s | 16 / 517 |
| 4963695 → de97645 | 537 | 74/55/1, 152.03 s | 506/436/63, 502.40 s | 506/436/63, 502.40 s | 74 / 463 |
| 0eb2c93 → 2c991bb | 539 | 46/30/1, 133.84 s | 58/42/1, 137.34 s | 482/415/63, 485.45 s | 46 / 493 |
| e334905 → 3addcd1 | 540 | 36/20/1, 129.87 s | 59/43/1, 137.42 s | 483/416/63, 485.53 s | 36 / 504 |
| 1399abf → fc5657f | 543 | 38/22/1, 137.68 s | 328/261/63, 431.09 s | 509/442/63, 500.13 s | 38 / 505 |
| adb1074 → 74b787b | 543 | 16/1/1, 121.56 s | 16/1/1, 121.56 s | 487/421/63, 484.02 s | 16 / 527 |
| 937e49f → c0143f2 | 553 | 55/39/1, 135.20 s | 69/53/1, 140.49 s | 498/431/63, 490.17 s | 55 / 498 |
| 8bc7be7 → 4422433 | 554 | 49/33/1, 134.26 s | 70/54/1, 140.59 s | 499/432/63, 490.27 s | 49 / 505 |
| 73dc795 → d3cd17a | 555 | 51/35/1, 141.19 s | 102/86/1, 154.50 s | 500/433/63, 490.89 s | 51 / 504 |
| 5047abb → cbe4f22 | 556 | 158/70/63, 381.57 s | 360/272/63, 446.17 s | 533/445/63, 507.58 s | 158 / 398 |
| 2c5f1e8 → 82c259d | 557 | 52/36/1, 136.32 s | 71/55/1, 140.68 s | 399/383/1, 254.10 s | 52 / 505 |
| 27cd31b → 3ce2149 | 557 | 114/30/61, 359.77 s | 114/30/61, 359.77 s | 461/377/61, 477.56 s | 114 / 443 |
| 3479028 → f8a18f8 | 557 | 114/30/61, 359.77 s | 114/30/61, 359.77 s | 461/377/61, 477.56 s | 114 / 443 |
| b554d5d → 3dbe59f | 557 | 114/30/61, 359.77 s | 114/30/61, 359.77 s | 461/377/61, 477.56 s | 114 / 443 |
| 1edfbc4 → 181504a | 557 | 16/1/1, 121.56 s | 16/1/1, 121.56 s | 363/348/1, 239.35 s | 16 / 541 |
| **Total** | **8,771** (7,373 C++, 1,008 GPU) | **965/434/258, 3,047.51 s** | **2,029/1,396/382, 3,796.23 s** | **6,674/5,735/754, 6,605.69 s** | |

The 8,771 listed tests are the same number as the simulation's 8,771 runs
(decision 483), and 1,008 is 63 × 16.

## The causes (decision 730), from the tests that ran by function

| Commit | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | By program only (B) |
|---|---|---|---|---|---|---|---|---|---|
| 374745f | – | – | – | – | – | – | 1 | 15 | 0 |
| 53f929b | – | – | – | – | – | – | 1 | 15 | 0 |
| 4963695 | 34 | – | 16 | – | – | – | 5 | 19 | 432 |
| 0eb2c93 | 15 | – | 12 | – | – | – | 4 | 15 | 12 |
| e334905 | 11 | – | 7 | – | – | – | 3 | 15 | 23 |
| 1399abf | – | – | 18 | – | – | – | 5 | 15 | 290 |
| adb1074 | – | – | – | – | – | – | 2 | 14 | 0 |
| 937e49f | 24 | 9 | – | – | – | – | 12 | 15 | 14 |
| 8bc7be7 | 24 | – | 7 | – | – | – | 3 | 15 | 21 |
| 73dc795 | 29 | 5 | 2 | – | – | – | 3 | 15 | 51 |
| 5047abb | 39 | – | – | 25 | 81 | 99 | 3 | 18 | 202 |
| 2c5f1e8 | 21 | – | 13 | – | – | – | 2 | 16 | 19 |
| 27cd31b | – | – | – | 30 | 82 | – | 2 | 14 | 0 |
| 3479028 | – | – | – | 30 | 83 | – | 1 | 15 | 0 |
| b554d5d | – | – | – | 30 | 83 | – | 1 | 15 | 0 |
| 1edfbc4 | – | – | – | – | – | – | 1 | 15 | 0 |
| **Total** | **197** | **14** | **75** | **115** | **329** | **99** | **49** | **246** | **1,064** |

- Cause 1: 162 tests with a changed source and 46 with no changed source; a
  test can have both.
- Cause 7: 16 runs of the test that skips itself (decision 752), 9 of
  `mutants_due_probe_shaders` after it failed at the commit before, and 24 new
  tests.
- Cause 8: build tree 215, files 51, programs 15, definition 3.
- A test with two causes counts for each cause, and one time in the totals
  965 (by function) and 2,029 (B).
- `mutants_due_probe_shaders` failed at 4963695, 0eb2c93, e334905, 1399abf,
  adb1074, 937e49f, 8bc7be7, 73dc795 and 2c5f1e8, each time with
  "mutants-due: expected m1-16.json to be current, and it is due". It passed at
  5047abb and from 27cd31b on, where the probes ran. *(The helper wrote "10 of
  them" and named these 9 commits; the files give the count.)*

## The changed functions, sorted by hand

A function of a header in `src/` is cause 1, also where the linker took its
copy from the object of a test file. Thus cause 2 fell from 37 automatic runs
to 14.

| Commit | Source changed | Source not changed |
|---|---|---|
| 4963695 | `orb::wrapTau`, `orb::wrapPi` (`src/core/Scalar.hpp`) | – |
| 0eb2c93 | `orb::orbitInfo` | – |
| e334905 | `orb::propagateElements` | – |
| 937e49f | the four anomaly converters; two `safeguardedRoot` lambdas of `meanToEccentricAnomaly`; `orb::describe(OrbitError)` (Orbit.hpp, its copies in 3 test objects) | `solveUniversalAnomaly` and its `safeguardedRoot` lambda |
| 8bc7be7 | `orbitInfo`, `propagateElements` | 3 mp-units templates for rad/s in Orbit.cpp.obj |
| 73dc795 | `orb::split`, `orb::twoProduct` (DoubleDouble.hpp) | – |
| 5047abb | `orb::angleBetween` (Math.hpp, copies in 7 objects) | `std::vector<std::stacktrace_entry>::_Xlength`, a new function without a flag in test_camera_controller.exe (21 tests), which the new assertion brought; `Scalar<one>` operators `+` and `/` in ProbeSidecar.cpp.obj |
| 2c5f1e8 | `orb::stateFromElements` | – |

At 1399abf there was only cause 3: 18 tests of test_math.exe, because its
linker directives changed.

## The percentages, with (B) as the count by program

| Figure | Replay | Earlier figure |
|---|---|---|
| C++ tests by function against by program | 434 of 1,396 = 31.1 % | the spike's 840 of 4,646 = 18.1 % (decision 543) |
| All tests by program against all tests listed | 2,029 of 8,771 = 23.1 % | the simulation's 16 % (decision 490) |
| The same, with the 63 GPU tests counted as run at each commit | 2,655 of 8,771 = 30.3 % | the simulation's 2,284 of 8,771 = 26 % (decision 483) |
| All tests by function against all tests listed | 965 of 8,771 = 11.0 % | – |
| Test time by function against by program | 3,047.51 s of 3,796.23 s = 80.3 % | the spike's 1,997 s of 6,158 s = 32.4 % |

For the data only, (A) gives 7.6 %, 76.1 % and 79.0 %, and a time of
6,605.69 s.

## The files beside this report

The scripts (`replay-count.py`, `shadow.py`, `hand-sort.py`, the two shell
scripts and three viewers), the tables (`final-table.*`, `table.*`,
`shadow.*`, `hand-sort.*`), the base's CTest report `base-results.xml`, and
for each commit `count.json`, `after.json`, `compare.json`, `patch-id.txt`,
`steps.txt` and the driver's `driver-selected.txt`. The function digests of
each commit and the logs stay in the scratch folder of the session of
2026-10-21 and 2026-10-22; the scripts can make them again.
