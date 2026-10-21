# ADR 0028: A test with a program that has the coverage hook runs again only when a function that it entered changed

Status: **accepted** (2026-10-21), with
[M1-132](../plan/tasks/m1-132-function-level-selection.md). Register decisions
543-553, 556-561, 564, 567, 569 and 570 for the start, 582-595 and 624 before
the work, and 701-719 for the work and its finish. **Replaces one rule of
[ADR 0027](0027-tests-run-when-their-inputs-change.md)**: that the fingerprint
of a test holds each program on its command line by its bytes. That rule stays
for a program without the coverage hook, and for all programs in the trees
without the hook. ADR 0027 has a dated section about this record.
[M1-133](../plan/tasks/m1-133-selection-proved.md) adds its measured figures
to this record, as a dated section at its end (decision 719).

## Decision

**In the two `check` trees, `scripts/run-tests.py` skips a test with a program
that has the coverage hook when each other part of its fingerprint is
unchanged, and the bytes of the program or its function part are unchanged
(decision 706).** The coverage hook is the compiler option
`-fsanitize-coverage=func,inline-bool-flag,pc-table` (decision 559). It gives
each function a flag, which the function sets when it starts, and a recorder
writes the functions that a test entered when the test ends
([M1-131](../plan/tasks/m1-131-coverage-hook.md)). The function part of a
program has four parts (decision 583):

- **Our own code, function by function**: the digest of each function that the
  test entered at its last pass, and of each function without a flag in an
  object with the hook (decisions 586 and 711 (5)). A digest is a hash of the
  compiled bytes of a function, of the names of the functions and data that it
  points at, and of the contents of that data, followed through data that
  points at other data (decisions 545 and 711). A removed function counts as
  changed (decision 547). An object has the hook when it holds the hook's
  address table (decision 584).
- **The code of all others, by its bytes**: each library and each object
  without the hook, and the names of the modules of it that the program links
  (decision 585). The path of each library comes from the program's .pdb file
  (decision 705).
- **The linker directives** of the objects with the hook, `/INCLUDE` included
  (decision 711 (7)).
- **The link command of the program**, from Ninja (decision 709 (4)).

The linker's map turns each recorded address into a function (decision 582):
one name at the address, or one pair of `??_E` and `??_G` names (decision
695). That was compared one time with the procedures of the debug information,
over a full run in each tree, and each recorded address starts one procedure,
and only one ([the measurement record](../measurements/m1-132-procedure-ranges.md),
decision 712 (2)). A start-up function counts for no test (decision 624), thus
`check` refuses a start-up entry in an object from `src/` that has the
hook, other than the hook's own (decisions 702 and 716).

A record belongs to a test by its command line, or by `ORBSIM_TEST_NAME`
(decision 564). The records give the function part only for a test that starts
its program directly, or through one of seven CMake scripts that only start a
program and read its output (decision 703). Each other test keeps the bytes of
the programs on its command line. **`check` fails, and names the program, when
a record is incorrect or missing** (decisions 590, 704, 712 (1) and 713 (2)):
a test that passed without a record, an address with no function, a record
that belongs to no test or to two tests, a record that is not complete, and a count
of flags that is different from the program's table. A problem that the driver
finds before the tests run makes the program count as changed, and `check`
fails at the end (decision 713 (3)).

The code is in `scripts/compiled-code.py` (the readers and the digests),
`scripts/run-tests.py` (the driver) and `scripts/check-coverage-hook.py` (the
start-up check), with the CTest tests `compiled_code_self_test`,
`run_tests_function_level`, `coverage_hook_self_test` and
`run_tests_self_test`.

## Why

With ADR 0027 alone, a change to a program's bytes makes each test of that
program run again, and nearly all programs link `orbsim_core`. A spike over
the 16 commits of M1-118 counted 840 runs of a C++ test by function against
4,646 by program, 18 %, and 1,997 s of test time against 6,158 s, 32 %
(decision 543); that figure was probably too low (decision 561). With the
driver of M1-132, measured on 2026-10-21 in each `check` tree (decision 715):
one comment line added to `src/orbit/Orbit.cpp` or to `tests/test_math.cpp`
ran no Catch2 test, where the selection by program runs approximately 500 for
`Orbit.cpp`. 29 tests ran in RelWithDebInfo and 23 in Debug: the script tests,
which the files of the repository make run, and the tests that were not stored.

## Considered

- **ADR 0027 alone**: safe, but each change to the core library runs nearly all
  tests (decision 543).
- **The names from the debug information**, not the linker's map: 53 % of the
  pieces of code agreed with more than one function, and 2,813 of 15,962 names
  in one record had no digest (decisions 560, 561 and 582).
- **The text of `llvm-objdump`**, not a reader of the object files: the
  text of a different tool, which can change (decision 546).
- **A hook that calls a function at each entry** (`trace-pc-guard`):
  approximately 30 % more test time in Debug (decision 559).
- **The hook on all code that the tree compiles**, and calls followed into code
  without the hook: more build time, and the libraries that are not in the
  tree stay without it (decisions 569 and 583).
- **The function part only**, with the digests of each program calculated at
  each run (decision 706).
- **A start-up function counted as a change for each test** (decision 587),
  replaced by decision 624.
- **The functions of the last two passes**, for a test that enters different
  functions in a different run: two times the data (decision 707).

## What it costs, and what it does not include

**Time.** With the hook, the tests of `check` use 22.4 % more time in Debug and
3.6 % more in RelWithDebInfo, and the compiles of our own code in
RelWithDebInfo approximately 6 % more (decisions 559 and 694,
[the measurement record of M1-131](../measurements/m1-131-hook.md)). The
compiled code costs each run of `check` 0.9 s in RelWithDebInfo and 1.9 s in
Debug for a comment line in `Orbit.cpp`, with the kept table of each program
(decision 715 (1), commit `bb80462`); without the kept tables it was 2.9 to
3.2 s and 7.4 to 7.6 s. With a store of a different version, or in a full run,
it is approximately 6 s and 17 s, one time.

**Space.** The maps hold 293.4 MB in Debug and 119.6 MB in RelWithDebInfo
(decision 694). The summaries of the objects hold approximately 103 MB and
35 MB (decision 714), and the kept tables approximately 47,000 entries in
Debug (decision 715).

**Limits.** The full run before each push finds a fault there (decision 484):

- In a run after its last pass, a test can enter a function that it did not
  enter at that pass, because of the time, the GPU or a different cause. Then
  the driver skips it, where the selection by program runs it (decision 707).
  The 64 GPU tests, run one more time in each tree, entered the same functions.
- A listener added, or a change to the registration of a test -- its tags or
  its file, `[!shouldfail]` and `[!mayfail]` included -- runs no test by
  itself (decision 624).
- A test added to a program makes each test of that program run again,
  because the linker directives change (decision 711 (7)). This is on the safe
  side.
- A test that runs through a script that is not one of the seven of decision
  703 keeps the bytes of its programs, which is on the safe side, until the
  driver also names that script.
