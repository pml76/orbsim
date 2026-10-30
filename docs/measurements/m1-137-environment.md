# M1-137 item 4 — the environment part, measured

Status: measured 2026-10-30 on the first machine, in `build/debug` and
`build/relwithdebinfo`, with clang 23.1.2 and Python 3.14. The first runs are
at commit `e824c73`, and the run of `TEMP` after the fixes is at commit
`0b6e144`. Register decisions 850, 851 and 882-891. The script is
[`m1-137/environment-runs.py`](m1-137/environment-runs.py).

## The question

`check` skips a test when its fingerprint did not change. The environment part
of the fingerprint holds only some variables (decisions 501 and 796). A
variable that is not in the part can change the result of a test, and `check`
then skips that test. Decision 851 gives the rule: a variable goes into the
part when it changes a result.

## The method

- **The run** (decision 882). The script runs CTest directly, for all tests,
  with the environment that the driver gives CTest, and changes the variables
  there and nowhere else. `check-full` also runs the build, the linter and the
  driver with the changed variables, and a failure there is not a result of a
  test: `GIT_DIR` of a different repository breaks the calls of Git of the
  driver.
- **The values** (decision 883). Each value can change a result, and is not
  the value at this time. The list is in the script.
- **A change** (decision 884). Each change between passed, failed and skipped.
- **The sequence** (decisions 851 and 886). One run with no change, one run for
  each of the two groups, and then one run for each of the 23 entries of the
  groups, each with all tests, because both groups changed a result.

| Tree | Passed | Skipped | Failed | Time |
|---|---|---|---|---|
| Debug | 590 | 1 | 0 | 231 s |
| RelWithDebInfo | 584 | 7 | 0 | 127 s |

## The result

Seven entries change a result. These numbers are the same in the two trees,
and the same tests fail in the two trees.

| Entry | Tests that fail | Cause |
|---|---|---|
| `PYTHONHOME` | 46 | Python cannot start |
| `TEMP` | 4 | four faults, see below |
| `GIT_DIR` | 4 | Git in a scratch folder stops, or acts on the other repository |
| `GIT_WORK_TREE` | 4 | the same |
| `PATHEXT` | 2 | the driver does not find `llvm-readobj`, and stops |
| `PYTHONSAFEPATH` | 1 | the folder of a script is not on the module path, thus a test no longer reads `scripts/` |
| `CMAKE_*` | 1 | the launcher that is not there stops a scratch project |

The 16 other entries change no result: `TMP`, `TZ`, `LANG`, `LC_ALL`,
`HTTP_PROXY`, `HTTPS_PROXY`, `NO_PROXY`, `NoDefaultCurrentDirectoryInExePath`,
`__COMPAT_LAYER`, `PYTHONNOUSERSITE`, `PYTHONUTF8`, `PYTHONIOENCODING`,
`PYTHONDONTWRITEBYTECODE`, `PYTHONHASHSEED`, `GIT_CONFIG_*` and `CTEST_*`.

Each change is from passed to failed. No test started to skip itself. A group
changes no test that its entries, each alone, do not change, and no entry alone
changes a test that its group does not change: group 1 changes 4 tests and
group 2 changes 46.

## `TEMP`: four faults (decisions 888 and 889)

The value of `TEMP` is a folder with a space and `ä` in its name. A run of the
four tests with a space alone and with `ä` alone gave the cause of each failure.
None of the causes is in the recorders of the coverage, which write UTF-8.

| Test that fails | Character | Cause | Where |
|---|---|---|---|
| `mutants_due_self_test` | `ä` | `json.dumps` writes `ä`, thus `placeholders` does not find the path, and the fingerprint depends on the folder | `test_fingerprint` of scripts/mutants-due.py |
| `run_tests_function_level` | `ä` | the driver writes its output in the code page of Windows, and the test reads UTF-8 | `main` of scripts/run-tests.py |
| `run_tests_end_to_end` | `ä` | three stand-ins write their read records in the code page, and the real hook writes UTF-8 | the three stand-ins in scripts/run-tests.py |
| `run_tests_self_test` | space | six stand-in command lines have no quotes around the program, and CTest uses quotes | the self-test of scripts/run-tests.py |

A *code page* is the table with which Windows changes letters to bytes. A
*stand-in* is a small program that a test makes in place of a real one. The fixes are 12
lines. No test definition of the two trees has a letter that is not ASCII,
thus the first fix moves no fingerprint.

**The guard** (decision 889). The two self-tests and the two end-to-end runs
make their scratch folders inside one folder with a space and `ä` in its name,
thus each run of `check` tests such a path. **Seen to fail:** on a scratch copy,
with no change of `TEMP`, each fix was taken out alone. Its test failed, and the
three others passed. With all fixes in, the four tests passed.

| Fix taken out | Test that failed | The other three |
|---|---|---|
| `ensure_ascii=False` | `mutants_due_self_test` | passed |
| the UTF-8 output of the driver | `run_tests_function_level` | passed |
| UTF-8 in the three stand-ins | `run_tests_end_to_end`, 30 of 43 steps | passed |
| the quotes in the six command lines | `run_tests_self_test`, 379 of 383 cases | passed |

**The result after the fixes.** With the same value of `TEMP`, at commit
`0b6e144`, all 591 tests ran in each tree, and no result changed: 590 passed
and 1 skipped in Debug, and 584 passed and 7 skipped in RelWithDebInfo. Thus
`TEMP` stays out of the environment part (decisions 851 and 888).

**The limit of the guard.** A letter that the code page of the machine does not
have stops `ninja` in the compiler check of CMake. Measured with `Ω`: the two
end-to-end runs fail, also with the fixes. That is not code of this project. A
machine with no `ä` in its code page fails these tests.

## `PATHEXT` (decision 890)

| Where | Value |
|---|---|
| Windows, and programs that Bash starts | `.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC` |
| Programs that PowerShell 7.6 starts | the same, with `;.CPL` at its end |

With the value as it is, all tests run again at each change between PowerShell
and Bash, about 6 minutes for the two trees. Thus the part holds `PATHEXT`
without its entry `.CPL`.

## An error in the first run

The first value of `TEMP` was a folder beside the usual temporary folder.
Windows sends the files that the desktop app of Claude writes in the folder
AppData/Local, but not in its folder Temp, to the folder of its package, and
the path then becomes about 55 characters longer. That run thus changed two things
at one time. The script now makes the folder inside the usual temporary folder.
Both runs of group 1 made the same 4 tests fail.

## The limits

- **One value for each variable.** Another value can change a result that this
  value did not change. `check-full` runs all tests before each push and finds
  that case.
- **CLion is not measured**, because a session cannot run it. If it sets a name
  of `CMAKE_*`, all tests run again at each change between CLion and a shell.
  No such name is set in Bash or PowerShell.
- **The first machine only**, and only the two `check` trees, because only they
  skip tests.
