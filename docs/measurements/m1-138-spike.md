# M1-138 — the spike: what the script tests read

Kind: measurement record
Binding: no — this file records what was measured, on the dates given
Task: [M1-138](../plan/tasks/m1-138-script-tests-each-commit.md), register
decisions 765-771, 775 and 777.

This record holds the spike of decision 766, with only a measurement as its
output, on 2026-10-24. It answers three questions. Does the audit hook of
decision 765 see each file that a Python process of a script test reads?
What do the child programs of the script tests read, which question 7
(decision 769) needs? And does CMake's trace change a result of a `cmake -P`
test (decision 768)? The scripts and the small results are in
[`m1-138/`](m1-138/). The recordings of Process Monitor stay on the first
machine, in the folder build/m1-138-spike, which git ignores.

## 1. The method

- **The hook** is [`m1-138/hook/sitecustomize.py`](m1-138/hook/sitecustomize.py),
  a measurement tool, not the code of the task. Python loads it at its start
  when its folder is on `PYTHONPATH`. Each Python process writes one log: each
  file that it opened, each folder that it listed with a hash of the names in
  it, each process that it started, each question about a file through
  `os.stat`, `os.lstat`, `os.access` and the functions of `os.path` that
  decision 770 names, and the source file of each module that ran.
- **The run** is [`m1-138/spike_run.py`](m1-138/spike_run.py) `full`: CTest
  runs all 590 tests of a tree with 10 jobs, as `check` gives the driver, and
  with the hook. It changes no tracked file and does not touch the store of
  the driver.
- **Process Monitor** records the run. Its settings are in
  [`m1-138/m1-138.pmc`](m1-138/m1-138.pmc): the default filter; no registry,
  network or profiling events; only the operations CreateFile, the six
  attribute queries, QueryDirectory and Process Start; and the filtered
  events dropped. The assistant starts and stops it from its shell, and the
  owner accepts each start, stop and save at a prompt of Windows.
- **The script that compares** is [`m1-138/procmon_analyse.py`](m1-138/procmon_analyse.py).
  It gives each process to its test: the top process by its command line,
  which it compares with the commands of CTest; a different process by a test
  name in its environment, or else by its parent. For each Python process of
  a script test, it compares Process Monitor with the hook's log. It also
  lists, for each script test and program, what was read, queried and listed
  in the repository, and it gives the events of each minute, so that a part
  of a recording with no events shows.
- **The trace run** is `spike_run.py trace`: each of the 57 `cmake -P` tests
  runs two times, one at a time, first as now and then with
  `--trace-expand --trace-format=json-v1 --trace-redirect=<file>`.
  [`m1-138/trace_analyse.py`](m1-138/trace_analyse.py) reads the traces.

## 2. The instruments, tested before their results were trusted

`VERIFICATION.md` rule 23 and decision 733: each instrument must find a
fault that is put in to make it fail.

- **The hook's self-test**, [`m1-138/hook_selftest.py`](m1-138/hook_selftest.py),
  makes each type of access one time and makes sure that its line is there.
  It found a gap in the hook before the first use: a script that `importlib.util.spec_from_file_location`
  and `exec_module` load does not go into `sys.modules`, thus the list of
  modules at exit did not hold it. The cure: the hook gets the source file
  from the audit event `exec`, and also from each `.pyc` file that opens. Then
  the self-test passed, and it failed when the lines of file questions, of
  modules, or of listings were removed.
- **A short recording** of 7 tests of different types. The first two
  recordings were not usable: a filter that Process Monitor had kept from
  decision 612 kept only CreateFile and Load Image, and the second recording
  had no events during the run. The third was complete. On it, four faults
  of the script that compares were found and corrected: Process Monitor cuts
  the text of a process start at 4,095 characters, thus a test name in the
  environment is frequently cut off; `ORBSIM_PYTHON` is the launcher of
  PyManager, which starts the interpreter itself as a child, and only the
  child runs the hook; a folder that is opened to list it is a listing, not a
  read; and the path of a listing can end with a pattern such as `\*`. Then
  the script found no miss, and it reported a miss when `check-lint-deps.py`, or the
  listing of `src`, was removed from the logs.

## 3. The recordings

| Recording | Tree | The run | Complete | Script tests in it |
|---|---|---|---|---|
| First full run | Debug | 18:06:00-18:22:20 | No: its events end at 18:17:24 | 89 of 102 |
| Second full run, with the operations filter | Debug | 19:32:35-19:40:29 | Yes: events in each minute | 102 of 102 |
| First full run | RelWithDebInfo | 19:42:46-19:47:23 | No: its events end at 19:45:52 | 87 of 102 |
| Second full run | RelWithDebInfo | 19:52:08-19:56:20 | No: its events end at 19:54:51 | 78 of 102 |

All 590 tests passed in each run. The cause of the three stops is not known.
A second export of the first RelWithDebInfo recording gave the same file,
thus the export did not cut it. At the first two stops, the system log of
Windows shows no change of the power state; the third was not examined
(working agreement 10).
Decision 777 lets the complete Debug recording count for the two trees.

## 4. Result 1: the hook against Process Monitor, in the complete Debug run

- **148 Python processes compared: no file read, no folder listing and no
  question about a file that the hook did not record.** The full figures are
  in [`m1-138/results/debug-analysis.txt`](m1-138/results/debug-analysis.txt).
- Not misses: 47 launchers of the interpreter, which do not run the hook;
  and 213 questions about a folder, the working folder of the test and the
  folder of the hook's logs, which Python queries at its start.
- In the first, incomplete Debug recording, the only reads that the tool
  reported were the program files of processes that a Python process started.
  Windows opens a program file to start it, and the hook records the start.
  The tool now accepts them.

## 5. Result 2: what the script tests read

From the complete Debug recording,
[`m1-138/results/debug-children.txt`](m1-138/results/debug-children.txt) and
[`m1-138/results/debug-child-commands.txt`](m1-138/results/debug-child-commands.txt).

- **No program of a script test reads a document of the repository.** The
  only `.md` files that it reads or queries are `data/horizons/README.md` and
  `data/skyfield/README.md`, which `read_record_written` reads and which are
  under `data/`, and `readme.md` in the build tree, which the `mutants_due`
  tests query.
- **Only the 7 `mutants_due` tests that read the record of the passes read
  `scripts/mutation-passes.json`.**
- **What the child programs read:**

  | Program | Tests | What it read or queried |
  |---|---|---|
  | `cmake.exe` | 60 | The build tree, `cmake/`, `data/`, `tests/` |
  | `orbsim.exe` | 50 | The build tree and `tests/` (the goldens) |
  | `ctest.exe` | 15 | The build tree only |
  | `ninja.exe` | 7 | The build tree only |
  | `git.exe` | 5 | `.git` (config, `HEAD`, index, objects, refs) and `scripts/git-hooks/pre-push`; the others in scratch folders |
  | `clang++.exe` | 4 | The build tree, `src/`, `tests/`, and about 300 header names that it queries at the root of the repository, which are not there |
  | The programs of the tree | 1 or 2 each | The build tree |

- **The 21 script tests that a change to documents runs in Debug**, by their
  child programs, with their summed time in `check-full` at `54db237`:

  | Child programs | Tests | Summed time |
  |---|---|---|
  | None | 6: `lint_deps`, `memory_pool`, `test_cases`, `test_reads`, `tonemap_constants`, `configure_current` | 9.6 s |
  | `ctest --show-only=json-v1` only | 9: `accept_golden`, `coverage_hook`, `parallel_tests`, and 6 `mutants_due` tests | 37.7 s |
  | `ctest`, with `ninja -t` or `git ls-files` | 2: `mutants_due_unmet_current_fails`, `mutants_due_fingerprints` | 20.1 s |
  | `ctest`, `llvm-readobj` and programs of the tree | 1: `run_tests_in_tree` | 4.1 s |
  | `orbsim.exe` | 1: `mutants_due_gpu_identity` | 3.1 s |
  | `clang++.exe` | 2: `float_equal_guard`, `reproducible_build` | 32.3 s |

## 6. Result 3: CMake's trace (decision 768)

| Tree | Tests | Verdicts, without and with the trace | Time without | Time with |
|---|---|---|---|---|
| Debug | 57 | 57 passed, 57 passed | 101.5 s | 101.3 s |
| RelWithDebInfo | 57 | 51 passed and 6 skipped, the same | 44.2 s | 45.1 s |

- No script starts Python, and no script names a path in `scripts/`. The
  scripts start `orbsim.exe`, the probe programs of the tree, and `cmake.exe`
  itself: `configure_guard` configures two scratch projects in the build
  tree, and `probe_clear_determinism` compares two images with
  `cmake -E compare_files`.
- The CMake commands that the scripts use: `if`, `elseif`, `else`, `endif`,
  `string`, `list`, `math`, `set`, `file`, `foreach`, `endforeach`,
  `continue`, `return`, `message`, `execute_process`,
  `get_filename_component`, `function`, `endfunction`, and the function
  `scratch_configure` of `cmake/TestConfigureGuard.cmake`. No `include()`.
- The subcommands of `file()`: SIZE, READ, SHA256, STRINGS and GLOB read or
  query a file; REMOVE, REMOVE_RECURSE, WRITE, MAKE_DIRECTORY and
  TO_CMAKE_PATH do not read. `get_filename_component` with NAME and REALPATH.
  `if(EXISTS)` 179 times.

## 7. Findings for the code

- The source file of a module comes from the audit event `exec` and from
  each `.pyc` file that opens; `sys.modules` at exit is not sufficient
  (section 2). This is how decision 765 is built.
- The record of a test comes from the interpreter itself, the child of the
  launcher of PyManager; the launcher writes nothing.
- A replacement of `os.stat` by a Python function changes the checks such as
  `os.stat in os.supports_fd`, which `shutil` makes when it loads. With
  Python 3.14.0 on Windows, `os.supports_dir_fd` is empty, but `os.stat` is in
  `os.supports_fd` and in `os.supports_follow_symlinks`.
- The time figures in this record are by-products of the runs, not
  measurements of their own (working agreement 10).
