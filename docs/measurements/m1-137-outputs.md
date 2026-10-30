# M1-137 item 2 — tests that read the outputs of other tests, measured

Status: measured 2026-10-29 and 2026-10-30 on the first machine, in
`build/debug` and `build/relwithdebinfo`, with clang 23.1.2. Register
decisions 826, 846, 847, 857-863 and 872-881. The scripts are
[`m1-137/outputs-lines.py`](m1-137/outputs-lines.py) and
[`m1-137/judges-read-files.py`](m1-137/judges-read-files.py); they use
the functions of `scripts/run-tests.py` and `scripts/mutants-due.py` that
do the work, thus they measure what these scripts do.

## The question

A test can read a file that a different test writes, with no fixture and
no `DEPENDS` on the writer (decision 508). When the writer runs and changes
the file, and the selection skips the reader, the reader keeps a result
that it got from the earlier file. Decision 826 found one example: three
tests of `scripts/mutants-due.py` read the store of the driver,
`<tree>/run-tests/fingerprints.json`, and keep the earlier rules, thus the
store was not an input of them.

## What was measured

On 2026-10-29 (decisions 846 and 857):

- 44 tests in each tree had read lines for 1,392 files of the build tree.
  The configure and the build write most of these files. The outputs of
  tests are in `probes/`, which the probe tests write, and in
  `golden-broken/`, which `broken_goldens` writes.
- After the last run, only the store was not as six tests read it, because
  the driver writes the store after the run.
- Seven `mutants_due` tests read `probes/clear.png` or the three files of
  `golden-broken/` with no fixture and no `DEPENDS` on their writer. With
  one byte added to each of the four files, their commands gave the same
  exit codes and the same output, in the two trees.

On 2026-10-30, with `outputs-lines.py` (decisions 872-875):

| What | Debug | RelWithDebInfo |
|---|---|---|
| Tests with read or list lines of the build tree | 45 | 45 |
| Lines, and the paths that they name | 2,821, 1,407 paths | 2,821, 1,407 paths |
| Lines with the mark `unreadable` | 331 | 331 |
| Lines with the mark `missing` | 917 | 917 |
| Lines that the new hash counts (decision 872) | 2,803 | 2,803 |
| Time of a new hash of all lines | 1.1 s to 1.3 s | 0.8 s to 0.9 s |
| Tests with no complete record | 17 | 17 |
| Of these, tests that name `scripts/mutants-due.py` | 3 | 3 |

- **The lines with `unreadable`** are not files. They are words of command
  lines that the hook examined as paths, added to the path of the tree, for
  example `--scene|earth`. Seven tests have them: six `mutants_due` tests
  and `accept_golden`. Thus decision 873 compares the lines as text.
- **The list of `test-reads/`**, which six `mutants_due` tests make,
  changes in each run, because each test writes its records into that
  folder during the run, and the names of the records of the Python
  processes hold the number of the process. Thus decision 872 does not
  count the folders that the driver, CTest and the tools write in each run.
- **The list of the top folder of the tree** held `.ninja_lock` when these
  tests made it, because Ninja keeps that file while `check` runs. In a
  run of `check`, the list does not change.
- **After the last `check`**, with the filter of decision 872 and the rule
  of decision 873, six tests in each tree had lines that were not as
  stored: the store, which the driver writes after the run, and the list of
  the top folder, made while Ninja ran. In a run, the two do not change.
- **The fixtures of the tree** are four, each with setups and with no
  cleanup test. Thus a test of a second call finds the outputs of the
  setups of the first call (decision 874).
- **Ten tests name `scripts/mutants-due.py`** on their command line, the
  only program that reads the store. Three of them had no complete record
  in each tree: `mutants_due`, `mutants_due_fingerprints` and
  `mutants_due_gpu_identity` in Debug, and `mutants_due_fingerprints`,
  `mutants_due_gpu_identity` and `mutants_due_probe_shaders` in
  RelWithDebInfo (decision 875). Which of them keep the earlier rules
  changes between the trees and between runs (decision 826).

## The rules (decisions 846, 847 and 872-875)

- After the run, and before it writes the store, the driver hashes again
  each counted line of each test with complete records, if the test ran or
  not, with no value that it calculated before. A counted line is a read
  line or a list line of the build tree, but not of `run-tests/`,
  `test-reads/`, `coverage-records/` or `Testing/`, and not of the three
  files `mutants-due-*.json`, `.ninja_deps` or `.ninja_log`. The store
  counts all the same.
- It compares each line as text with the stored line: `unreadable` before
  and after is no change, and the same for `missing`.
- A test with a changed line runs again in a second call of CTest, with
  each test that has `DEPENDS` on one of them, and with no setups. The data
  and the result of the second call replace those of the first call.
- After the second call, the driver examines all counted lines again. A
  difference fails the run, names the test and the file, and the test is not
  stored, thus it runs at the next `check`. There is no third call.
- A test with no complete record that names `scripts/mutants-due.py` on its
  command line has the hash of the store as a part. It runs when the store
  changed since its last pass, thus at nearly each `check`.

**Seen to fail** (decision 855): `run_tests_end_to_end` has six steps for
these rules, with a writer, a reader that has no fixture and no `DEPENDS` on
it, a test that has `DEPENDS` on the reader, and after that a test that changes
its own input after it reads it. With the second call taken out of the
driver, the four steps that are related to it failed (39 of 43 steps passed). The
self-test has six cases for the filter, the rule of decision 873 and the
part of the store.

**The first runs**, on 2026-10-30, through the driver in each tree: 22 of 22
tests passed in Debug and 28 of 28 in RelWithDebInfo, and no test ran in a
second call. The part of the store is on the three tests of each tree above.

## Decision 857: the files that a judge read (decisions 858-863 and 879-881)

The rule of decision 846 adds no fixture and no `DEPENDS`, thus alone it
does not change which mutant files are due. Decision 857 adds, to the
fingerprint of each ctest judge, a part for each counted file in the read
lines of its last pass, as the compiled shaders have since M1-131.

- **The counted files** are the read lines of the build tree (decision 876),
  but not the paths that are written in each run. This is the one list of
  the driver, `written_in_each_run` (decision 881), and for decision 857
  the store of the driver does not count, because it changes at each run.
- **The value of a part** is the line that the hook of the driver writes for
  the file at that time, as text, with all 64 digits of the SHA-256
  (decisions 873 and 880): `read` and the hash, `read missing` or
  `read unreadable`. The name of the part is `read file` and the path in
  the tree.
- **A pass** of scripts/mutate.py takes the files from the store of the
  driver, records them with their parts, and marks the record with
  `read_lines` (decisions 859 and 879). It prints each judge with no
  complete record, which gets no part (decision 861).
- **A reading** of scripts/mutants-due.py takes the files from the record
  where it has the mark, and from the store where not (decision 879). The
  report names each part that moved.

Measured 2026-10-30 with
[`m1-137/judges-read-files.py`](m1-137/judges-read-files.py), which uses
the code of scripts/mutants-due.py, on the records of the passes at that
time, none of which has the mark:

| What | Debug | RelWithDebInfo |
|---|---|---|
| Mutant files recorded in the tree | 25 | 21 |
| Parts of decision 857 of their judges | 1,962 | 1,518 |
| Files that move by these parts | 12 | 11 |
| Files with a judge with no complete record | 13 | 6 |
| Judges with no complete record | 10 | 6 |

The 12 and 11 files are the 23 files of decision 862, each in the tree of
its pass, and the 3,480 parts are the number of decision 859. Each of the 23
files is thus due one time; all 46 were due before this change, for other changes.
After the next pass of each file, its record gives its list.

**The limit that stays** (decision 861): a judge with no complete record at
its pass gets no part, thus a change of a file of the build tree that it
reads does not make its file due. On 2026-10-30 these were, in Debug,
`broken_goldens`, `doc_links_self_test`, `float_equal_guard`,
`mutants_due_gpu_identity`, `mutate_self_test`, `orbsim_smoke`,
`orbsim_smoke_bare`, `read_record_self_test`, `run_tests_end_to_end` and
`run_tests_function_level`, and in RelWithDebInfo `abort_listener`,
`mutate_self_test`, `orbsim_smoke`, `reproducible_build`,
`run_tests_end_to_end` and `run_tests_in_tree`. Which tests have no complete
record changes between the trees and between runs (decision 826).

**Seen to fail** (decision 863): the self-test of scripts/mutants-due.py
changes a counted file after a stand-in pass; the file must be due, and the
report must name the file. With the parts of decision 857 taken out of the
code, that case and two more failed. The self-test of scripts/mutate.py
holds the mark of a pass to the name that scripts/mutants-due.py reads.
