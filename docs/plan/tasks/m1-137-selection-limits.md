# M1-137 — The limits of the test selection, measured and closed

Phase: B | Status: **started 2026-10-29.** Its questions 1 to 15 are ruled
(decisions 842-856), question 16 by decision 857, questions 17 to 22
about the design of decision 857 by decisions 858-863, questions 23 to
26 about the result of item 6 by decisions 864-867, questions 27 to 30
about the code of item 1 by decisions 868-871, questions 31 to 40 about
the code of item 2 by decisions 872-881, questions 41 to 50 about item 4
by decisions 882-891, questions 51 to 62 about item 3 by decisions 892-903,
and questions 63 and 64 about item 5 by decisions 904 and 905. **All six
items are done** (2026-10-29 to 2026-10-31). The mutant file of decision 856
is next. The next task after M1-138 (decisions 744 and 763)
Prerequisites: M1-133
Decided by: register decision 744, ruled 2026-10-21 on the owner's word, after
the test selection was compared with published work: Ekstazi
(Gligoric and others, 2015), studies of static and method-level selection,
and Bazel's test cache.
Builds on [M1-129](m1-129-tests-by-fingerprint.md)'s fingerprints and read
records, [M1-132](m1-132-function-level-selection.md)'s selection by
function, and [M1-133](m1-133-selection-proved.md)'s proof and its decision
737.

## Purpose

Published work agrees with the method of ADR 0027 and ADR 0028. It also names
the conditions in which a selection of tests is not safe. Some of these are
the limits that ADR 0028 gives, and at this time the full run before each push
is the only check that finds them. This task measures each limit, closes the
limits that can be closed, and names the others where the selection is called
safe.

## What to do

1. **Coverage that changes between runs** (decision 707). In each
   `check-full`, the driver compares the functions that each test entered with
   the functions of its last pass. A difference is a warning that names the
   test and the functions. A test with a difference can get a rule of its
   own: by program, or always run.
   *Amended 2026-10-29 (decisions 842-845):* a difference is a fault only
   when a function is new and the selection skips the test. It fails
   `check-full`, and the test then gets the rule "by program". A measurement
   comes first.
   *Done 2026-10-30 (decisions 868-871):* the measurement found no test with
   a difference. From that date, a full run fails for each test that the
   selection skips and that entered a new function, and the test keeps the
   entry of its last pass. The record is
   [`m1-137-coverage.md`](../../measurements/m1-137-coverage.md).
2. **Tests that read the outputs of other tests** (decision 508). A test that
   reads an output of a different test gets, in its fingerprint, the
   fingerprint of the test that wrote that output. If the writer runs again,
   the reader runs too, and CTest's fixtures run the writer first. Decision
   737 corrects one example of this type; this item closes the type.
   *Added 2026-10-27 (decision 826):* the store of the driver,
   `<tree>/run-tests/fingerprints.json`, is an output of the same type,
   which the driver writes. The `mutants_due` tests read it. In each run, some of
   them keep the rules of decisions 500, 510 and 531, and these are not the
   same tests between the trees and between runs (decision 826, corrected).
   The store is not an input of these rules, thus a change of the store
   alone does not make them run. This item examines them too.
   *Amended 2026-10-29 (decisions 846 and 847):* the driver does not use the
   fingerprint of the writer. After the run, it calculates again the hash of
   each file of the build tree that a test read, and a test with a file that
   is not as it read it runs again. The store becomes an input of the tests of
   decision 826.
   *Done 2026-10-30 (decisions 872-881):* the rules of decisions 846 and 847
   are in the driver, and decision 857 is in scripts/mutants-due.py and
   scripts/mutate.py, as a commit of its own (decision 863). The record is
   [`m1-137-outputs.md`](../../measurements/m1-137-outputs.md).
3. **The read records compared again.** At each phase gate, the read records
   are compared again with the files that the tests actually open (decision
   612, Process Monitor), with the reads of child processes.
   *Done 2026-10-31 (decisions 892-903):* `scripts/compare-reads.py` compares
   each test of the store, and its steps are in its description. The six
   gates that stay have a line for it. In its first real run, no test of the
   two trees used a path that its fingerprint does not hold. The record is
   [`m1-137-reads.md`](../../measurements/m1-137-reads.md).
4. **The environment part, measured** (decision 501). Run the suites with
   other variables changed -- `TEMP`, the locale, `TZ`, the proxy variables --
   and record each result that changes. Only then rule which variables go into
   the hash.
   *Done 2026-10-30 (decisions 882-891):* the part now holds `PYTHONHOME`,
   `PYTHONSAFEPATH`, `GIT_DIR`, `GIT_WORK_TREE`, each `CMAKE_*` variable, and
   `PATHEXT` without its entry `.CPL`. `TEMP` stays out: after the fixes of
   decision 888 it changes no result. The record is
   [`m1-137-environment.md`](../../measurements/m1-137-environment.md).
5. **The limits named.** One line in `docs/BRIEFING.md` and in `CLAUDE.md`
   gives the limits of ADR 0028 -- start-up code, coverage that changes between
   runs, outputs of other tests -- and the full run that finds them.
   *Added 2026-10-29 (decision 840):* the line also names the two limits of
   decision 834: a file that comes into the top folder of the tree and that
   the program does not import, and a file that comes into
   `build/SystemResources`.
   *Added 2026-10-29 (decision 864):* the line also names that a new test
   that examines the chain of calls must go into the list of decision 844.
   *Done 2026-10-31 (decisions 904 and 905):* the line is in `CLAUDE.md`, after
   the bullet of M1-132, and in `docs/BRIEFING.md`.
6. **The `.pdb` files of the programs** (decision 833, added 2026-10-29).
   In Debug, seven programs read their own `.pdb` file, in 7 script tests,
   because an assertion prints its call chain. The part of the programs
   does not hold the `.pdb` file. Measure if a change can change a `.pdb`
   file and keep the bytes of its program, then close the gap or name it.
   *Done 2026-10-29 (decisions 864-866):* each change of a `.pdb` file also
   changes the bytes of its program, but six of the seven tests take their
   program function by function. The two that examine the line of the chain
   of calls take it by its bytes from that date. The record is
   [`m1-137-pdb.md`](../../measurements/m1-137-pdb.md).

## The questions, each with its alternatives, costs and ruling

*Added 2026-10-29.* These questions replace the four that this
document gave until the task started: question 1 of the four is questions 1
to 4 here, question 2 is questions 5, 6 and 16, question 3 is questions 7
and 8, and question 4 is questions 9 and 10. The owner ruled questions 1 to
15 on 2026-10-29 with the words "Accept all". The register rows give the
facts and the costs in full.

**Item 1: coverage that changes between runs**

**1. When is a difference a fault?** *Ruled by decision 842: A.*

- **A (recommended).** Only a new function, in a program where the function
  part of the functions of the last pass did not change. For this,
  `check-full` calculates the function parts: about 6 s in Debug and 17 s in
  RelWithDebInfo (ADR 0028).
- B. Each difference. Cost: each change of the code gives differences, also
  when no test is skipped.

**2. A failure, or a warning?** *Ruled by decision 843: A.*

- **A (recommended).** `check-full` fails, and names the test, the program
  and the new functions. Cost: a difference that causes no fault also stops a
  push.
- B. A warning, as item 1 gave. Cost: it is easy to miss, and the fault
  stays.

**3. The rule for a test with a difference.** *Ruled by decision 844: A.*

- **A (recommended).** The driver uses the bytes of that program for that
  test, and a list in the driver names the test with its decision. Cost: the
  test runs at each change of those bytes.
- B. The test runs at each run. Cost: more time.
- C. The functions of the last two passes together (decision 707). Cost: a
  store about two times larger, and a third path is not examined.

**4. Measure before the code?** *Ruled by decision 845: A.*

- **A (recommended).** `check-full` two times at one commit in each `check`
  tree, and a script compares the functions of each test. Cost: about 13
  minutes (the question gave 7, the time of one run in each tree) and a
  short script.
- B. No measurement. Cost: the check of question 1 finds such tests over
  time, each one as a push that it stops.

**Item 2: tests that read the outputs of other tests**

**5. How does the driver find such a test?** *Ruled by decision 846: A.*

- **A (recommended).** After the run, and before it writes its store, the
  driver calculates again the hash of each file of the build tree in the
  read lines of each test with complete records. A test with a file that is
  not as it read it runs again in a second call of CTest; a second change fails
  `check`. Cost: about 1,400 hashes, and the code with its mutants.
- B. A list of the writer of each output in `CMakeLists.txt`. Cost: a person
  keeps it, and it does not see an output that nobody adds.
- C. Write records from the C++ listener and the hook. Cost: a new record,
  and the probes write their images through libraries that no hook sees.

**6. The tests of decision 826, which read the store and keep the earlier
rules.** *Ruled by decision 847: A.*

- **A (recommended).** The store becomes their input, thus they run at nearly
  each `check`: 21.9 s summed in Debug and 22.9 s in RelWithDebInfo, in
  parallel with other tests.
- B. Keep it as a limit, which item 5 names.

**16. Which mutant files does the rule of question 5 make due?** *Ruled by
decision 857: B. The questions of its design are questions 17 to 22.*

Facts, measured 2026-10-29:

- scripts/mutants-due.py follows the fixtures and the `DEPENDS` of each
  judge (M1-109). The rule of question 5 adds no fixture and no `DEPENDS`.
  Thus it does not change which mutant files are due.
- Seven tests read an output of a different test with no fixture and no
  `DEPENDS` on its writer: `mutants_due`, `mutants_due_mutant_file`,
  `mutants_due_probe_shaders`, `mutants_due_scripts`, `mutants_due_shaders`,
  `mutants_due_unmet_current_fails` and `mutants_due_unmet_expectation_fails`.
  They read `probes/clear.png`, which `probe_clear` writes, or the three
  files of `golden-broken/`, which `broken_goldens` writes. They are judges
  of m1-92, m1-100, m1-109, m1-129 and m1-131. They read these files because
  mutants-due calculates the hash of each file that a judge names on its
  command line.
- Their seven commands, run with one byte added to each of the four files,
  gave the same exit codes and the same output, in each of the two trees.
  Then the bytes were put back as they were, and their hashes examined.
- The probe writes `clear.png` in place, with no rename. A reader at the
  same time can read a part of the file. With the rule of question 5, the
  reader then runs again.

Alternatives:

- **A (recommended).** No change to scripts/mutants-due.py. The record of
  item 2 gives the facts above. It also names the limit that stays: a new
  judge with a result that changes with the bytes of an output of a
  different test does not make its mutant file due. Cost: that limit stays.
- B. scripts/mutants-due.py adds, to the fingerprint of each judge, the
  hash of each file of the build tree in the read lines of its last pass,
  as it does for the shaders since M1-131 (decisions 629 and 635). Cost:
  code in a script with its self-test and mutants, and the five mutant
  files become due when `clear.png` or a file of `golden-broken/` changes.
- C. The seven tests get the fixtures of `probe_clear` and
  `broken_goldens` in cmake/ScriptTests.cmake. Cost: seven test definitions
  change, a selected one also runs a GPU probe, and the five mutant files
  become due at nearly each change of the application.

**The design of decision 857.** *Added 2026-10-29.* Questions 17 to 22
were put to the owner after decision 857, with measurements of the two
trees, and the owner ruled them the same day with the words "Accept all".
At that time 44 of the 46 mutant files were due in each tree, thus a rule
that counts more files costs little in practice.

**17. Which files of the build tree count?** *Ruled by decision 858: A.*

- **A (recommended).** Each file, but not the files that the tools write at
  each run (`run-tests/`, `test-reads/`, `Testing/`, the three files
  `mutants-due-*.json`, `.ninja_deps` and `.ninja_log`). It applies to 23
  mutant files, and also closes two more problems of the same type: the
  tests that read the files of the configure, and `coverage_hook`, which
  reads the `.map` files and the objects. Before the code, the counted files
  are compared between the two runs of decision 845.
- B. Only the files that the build and the configure do not write: 12
  mutant files. Cost: the problem of the configure stays.
- C. Only `probes/` and `golden-broken/`: 11 or 12 mutant files. Cost: a new
  folder of outputs is not seen.

**18. Where does the list of the counted files come from?** *Ruled by
decision 859: A.*

- **A (recommended).** The record of each pass holds the counted files of
  each judge with their hashes, and each reading after it calculates the
  hashes of the same files. Cost: scripts/mutation-passes.json becomes
  larger, from 103 KB to about 500 KB.
- B. The store of the driver at each reading. Cost: m1-92, m1-100 and m1-109
  are due at random, because `mutants_due` and `mutants_due_probe_shaders`
  change between complete records and the earlier rules.

**19. The read lines of paths that were not there.** *Ruled by decision
860: A.*

- **A (recommended).** They count, with the mark "missing", as the driver
  does: 916 lines in each tree, of which 252 are paths of files.
- B. They do not count. Cost: a file that comes after the pass is not seen.

**20. The judges with no complete record.** *Ruled by decision 861: A.*

- **A (recommended).** They get nothing from decision 857. The pass prints
  each such judge, and the record of item 2 names the limit: 15 judges, of
  17 mutant files.
- B. A file with such a judge is due at each gate: 17 files.

**21. The records of the passes before decision 857.** *Ruled by decision
862: A.*

- **A (recommended).** They stay as they are, thus each of the 23 files is
  due one time. 22 of them were due; the other is m1-135, with 8 mutants.
- B. Only the parts that the two sides recorded are compared, until the next
  pass of each file. Cost: more code, and no examination until then.

**22. Where does decision 857 go in the work?** *Ruled by decision 863: A.*

- **A (recommended).** In item 2, after the rule of question 5, as a commit
  of its own, with its mutants and a planted fault.
- B. A different task after M1-137. Cost: the problems stay until then.

**Item 3: the read records compared again at each gate**

**7. The tool.** *Ruled by decision 848: A.*

- **A (recommended).** One committed script in `scripts/`, made from the two
  scratch scripts of M1-129 and M1-138, with a self-test, a planted fault
  and its mutants.
- B. The two scratch scripts, by hand. Cost: two methods with no tests.

**8. What each gate records.** *Ruled by decision 849: A.*

- **A (recommended).** One run with `--full` in each `check` tree with a
  recording of Process Monitor, a line in the checklist of each of the six
  gates that stay, and each miss to the owner. Cost: about 30 to 60 minutes
  for each tree, and two UAC prompts.
- B. Debug only (decision 612). Cost: in M1-138 the two trees gave
  different results.

**Item 4: the environment part**

**9. Which variables?** *Ruled by decision 850: A.*

- **A (recommended).** Two groups: the variables of item 4, and the
  variables that change which program or module runs, or where files go.
  Each with a value that can change a result.
- B. Only the variables of item 4.

**10. How to measure, and the rule after.** *Ruled by decision 851: A.*

- **A (recommended).** `check-full` in the two `check` trees, with all
  variables of a group changed at one time, and one variable at a time only when a result
  changes. A variable that changes a result goes into the part. The list goes
  to the owner before the code. Cost: about 30 minutes when nothing changes.
- B. One variable at a time from the start. Cost: about 2 hours.

**Item 5 and item 6**

**11. The line that names the limits.** *Ruled by decision 852: A.*

- **A (recommended).** At the end, one line in `docs/BRIEFING.md` and in
  `CLAUDE.md`, after the bullet of M1-132, with only the limits that stay.
- B. `docs/BRIEFING.md` only.

**12. How item 6 measures.** *Ruled by decision 853: A.*

- **A (recommended).** A scratch project with the arguments of build/debug:
  a comment line added, and the program and its `.pdb` file compared. If the
  program also changes, there is no gap; if not, the `.pdb` files go into
  the part of the programs.
- B. The `.pdb` files added with no measurement.

**The result of item 6.** *Added 2026-10-29.* The measurement of question
12 found that its premise was not correct for six tests, and questions 23 to 25
went to the owner. The owner ruled them the same day with the words
"Accept all".

**23. How does item 6 close the gap?** *Ruled by decision 864: A.*

- **A (recommended).** The two `assertion_message` tests take their program
  by its bytes, through the list of decision 844. Cost: about 4.4 s at each
  change of the code that the probe links.
- B. The six tests by their bytes. Cost: about 8 s more, with results that
  cannot change.
- C. No change, and the limit named.

**24. When does the list of decision 844 come?** *Ruled by decision 865: A.*

- **A (recommended).** In item 6, with a self-test case and a planted fault.
- B. With item 1. Cost: item 6 stays open until then.

**25. Where does the measurement go?** *Ruled by decision 866: A.*

- **A (recommended).** A record and its script in `docs/measurements/`.
- B. Only this document.

**26. A guard for the list of decision 844.** *Added and ruled 2026-10-29
(decision 867): A.* A test that is renamed would leave its entry in the list
with no failure.

- **A (recommended).** `run_tests_in_tree` examines that each name in the
  list is a test of the tree, with a planted fault.
- B. No guard, and the limit named.

**The result of item 1.** *Added 2026-10-30.* The measurement of decision
845 found no test with a difference, and no counted file that changes
(decision 858). The code of decision 842 gave three questions, which went to
the owner. The owner ruled them on 2026-10-30 with the words "Accept all".
The record is [`m1-137-coverage.md`](../../measurements/m1-137-coverage.md).

**27. When is a new function a fault?** *Ruled by decision 868: A.*

- **A (recommended).** Only when the selection skips the test in this run,
  as a run without `--full` does. Cost: a full run calculates the selection
  before CTest starts: 1.0 s more in Debug and 0.5 s more in RelWithDebInfo
  in its first runs, which the record gives.
- B. The words of decision 842 only. Cost: a correct change of a data file
  can make `check-full` fail when there is no fault.

**28. What does the store keep after a fault?** *Ruled by decision 869: A.*

- **A (recommended).** The test keeps the entry of its last pass, thus the
  next `check-full` fails again.
- B. The store takes the new entry. Cost: the next `check-full` passes, and
  the fault stays.

**29. Where does the measurement go?** *Ruled by decision 870: A.*

- **A (recommended).** A record and its two scripts in `docs/measurements/`,
  as for item 6.
- B. Only this document.

**30. A mutant of M1-129 that lost its anchor.** *Added and ruled 2026-10-30
(decision 871): A.* Item 1 removed the line `forced = set(run.tests)`, which
the mutant "a full run forces nothing, so the run before a push skips tests"
changes, and `check` stopped at `mutant-anchors`.

- **A (recommended).** The mutant gets the anchor `chosen = set(run.tests)`,
  which it changes to `pass`, with its name, its judge and its verdict. Its
  judge caught it: 4 of 37 steps failed.
- B. The mutant removed.
- C. Code that keeps the old line, only for the anchor.

**The code of item 2.** *Added 2026-10-30.* The stores of the two trees
showed that the words of decision 846 make `check` fail at each run.
Questions 31 to 36 went to the owner with the measurements, and question
37 after the first `check`. The owner ruled them the same day with the
words "Accept all" and "Accept A". The record is
[`m1-137-outputs.md`](../../measurements/m1-137-outputs.md).

**31. Which lines does the new hash examine?** *Ruled by decision 872: A.*

- **A (recommended).** Read lines and list lines of the build tree, but
  not the paths that the driver, CTest and the tools write in each run.
  The store counts.
- B. Each line of the build tree. Cost: the list of `test-reads/` changes
  in each run, thus `check` fails at each run.
- C. Read lines only. Cost: a new file in a listed folder is not seen.

**32. How are `unreadable` and `missing` compared?** *Ruled by decision
873: A.*

- **A (recommended).** As text: the same mark before and after is no
  change.
- B. As the selection compares them. Cost: seven tests run again at each
  `check`, and then `check` fails.

**33. The second call of CTest.** *Ruled by decision 874: A.*

- **A (recommended).** The tests with a changed line and their `DEPENDS`,
  with no setups. The data of the second call replace those of the first,
  and a difference after it fails `check`.
- B. No second call. Cost: a stale reader stops `check`.

**34. How does the driver find the tests of decision 847?** *Ruled by
decision 875: A.*

- **A (recommended).** By program: `scripts/mutants-due.py` on the command
  line of a test with no complete record.
- B. A list of the four names. Cost: a new test of that type is not seen.
- C. Each test with no complete record and the part of the build tree.
  Cost: three more tests, about 26 s, run at nearly each `check`.

**35. Do list lines count for decision 857?** *Ruled by decision 876: A.*

- **A (recommended).** No, only read lines.
- B. Yes. Cost: the top folder holds `.ninja_lock` only while Ninja runs,
  and mutant files are due at random.

**36. The record of item 2.** *Ruled by decision 877: A.*

- **A (recommended).** A record and its script in `docs/measurements/`.
- B. Only this document.

**37. Two mutants of M1-138 that lost their anchors.** *Added and ruled
2026-10-30 (decision 878): A.* Their loops went into `remove_records`.

- **A (recommended).** New anchors on the same loops, with their names,
  their judge and their verdicts. Their judge caught each of them.
- B. The two mutants removed.
- C. The old lines kept, with a second copy, only for the anchors.

**38. How a reading uses a record made before decision 857.** *Ruled by
decision 879: A.* The owner's words were "Accept all", and then "Q39: B.
for other questions, pls go with your recommendations".

- **A (recommended).** A record from decision 857 on has a mark. For a
  record without it, the reading takes the files from the store, thus
  only the 23 files of decision 862 are due one time.
- B. One more part for each ctest judge of a record without the mark.
  Cost: 38 files are due one time.

**39. The value of a part.** *Ruled by decision 880: B.*

- A (recommended). The SHA-256 cut to 16 digits, as the compiled
  shaders have it. Cost: about 360 KB more in the record of the passes.
- **B.** All 64 digits. Cost: about 190 KB more than A.

**40. Where the list of the paths that do not count is written.** *Ruled
by decision 881: A.*

- **A (recommended).** One list in the driver for decisions 846 and 857,
  with the store as the one difference.
- B. Two lists, kept the same by hand.

**41. How each run of item 4 changes the variables.** *Ruled by decision
882: A.* The owner's words were "Accept all", for questions 41 to 46.

- **A (recommended).** A measurement script runs CTest directly, for all
  tests, with the environment that the driver gives CTest.
- B. `check-full`, as decision 851 reads. Group 2 then stops the build or
  the driver before most tests run.

**42. The value of each variable.** *Ruled by decision 883: A.*

- **A (recommended).** The values that the decision lists: each can change
  a result, and is not the value at this time.
- B. Other values, or more of them, from the owner.

**43. What counts as a changed result.** *Ruled by decision 884: A.*

- **A (recommended).** Each change between passed, failed and skipped.
- B. Only passed against failed. A test that skips itself is then not seen.

**44. A variable that is a pattern.** *Ruled by decision 885: A.*

- **A (recommended).** All of the pattern goes into the part, as `VK_*` and
  `ORBSIM_*` do.
- B. Only the names that the measurement set.

**45. The runs with one variable at a time.** *Ruled by decision 886: A.*

- **A (recommended).** Each such run runs all tests. Cost: at most about 2.2
  hours.
- B. Only the tests that the run of the group changed, about ten times less
  time. A test that changes with one variable alone is then not seen.

**46. The record of item 4.** *Ruled by decision 887: A.*

- **A (recommended).** `docs/measurements/m1-137-environment.md` and a
  script in `docs/measurements/m1-137/`, as for items 1, 2 and 6.
- B. Only this document.

**47. `TEMP` and its four faults.** *Ruled by decision 888: A.* The owner's
words were "Accept all", for questions 47 to 50.

- **A (recommended).** The four fixes, and then `TEMP` again in the two
  trees. If no result changes, `TEMP` stays out by decision 851.
- B. No fixes, and `TEMP` goes in. Cost: 4 tests fail on each machine with a
  space or `ä` in its `TEMP`.
- C. The fixes, and `TEMP` goes in also.

**48. The guard for these faults.** *Ruled by decision 889: A.*

- **A (recommended).** The four tests make their scratch folders inside a
  folder with a space and `ä` in its name.
- B. One new self-test case for each of the two fixes in the scripts. The
  stand-ins then have no guard.
- C. No guard.

**49. `PATHEXT`.** *Ruled by decision 890: A.*

- **A (recommended).** `PATHEXT` goes in, without the `.CPL` that PowerShell
  adds.
- B. `PATHEXT` as it is. Cost: all tests run again at each change between
  PowerShell and Bash, about 6 minutes for the two trees.

**50. The rest of the list.** *Ruled by decision 891: A.*

- **A (recommended).** `PYTHONHOME`, `PYTHONSAFEPATH`, `GIT_DIR`,
  `GIT_WORK_TREE` and all of `CMAKE_*` go in, and the 16 others stay out.
- B. A different list from the owner.

**51. Which tests the tool compares.** *Ruled by decision 892: A.* The
owner's words were "Accept all", for questions 51 to 57.

- **A (recommended).** Each test of the store. A test with no complete record
  is compared with its other parts.
- B. Only the three kinds of decision 848.

**52. What counts as a miss.** *Ruled by decision 893: A.*

- **A (recommended).** One rule for all tests: the rule of decision 823,
  without the groups of decision 834, which the tool counts apart.
- B. Only reads for C++ tests.
- C. No groups taken out: about 2,300 misses at each gate, for each tree.

**53. How the tool knows what a part holds.** *Ruled by decision 894: A.*

- **A (recommended).** Its own function, and a stop for a part that it does
  not know.
- B. A function shared with `Run.parts` of the driver.

**54. Files outside the repository.** *Ruled by decision 895: A.*

- **A (recommended).** A list of the folders, compared by hand at each gate.
- B. Outside files stay out.

**55. A recording that stops early.** *Ruled by decision 896: A.*

- **A (recommended).** The tool names the tests, and a short recording
  follows.
- B. The full recording again until it is complete.

**56. The steps of a gate.** *Ruled by decision 897: A.*

- **A (recommended).** The tool compares and makes the short run. Its
  description holds the steps.
- B. The tool does all steps.

**57. The first real run.** *Ruled by decision 898: A.*

- **A (recommended).** Before the commit of the tool, in each tree.
- B. First use at gate M1-38.

**58. `.pyc` files.** *Ruled by decision 899: A.* The owner's words were
"Accept all", for questions 58 to 62.

- **A (recommended).** Not an input when the `.py` file is known.
- B. They stay as misses.

**59. A name in a folder that a part hashes.** *Ruled by decision 900: A.*

- **A (recommended).** Known, by the reason that decision 834 gives for the
  cautious set.
- B. They stay as misses.

**60. The build tree.** *Ruled by decision 901: A.*

- **A (recommended).** Each path of the tree is known for a test with the
  part `build tree`.
- B. Each file by name.
- C. Complete records for the six tests.

**61. Three small cases.** *Ruled by decision 902: A.*

- **A (recommended).** Not inputs: `.git` by the part, and the two others by
  name.
- B. They stay as misses.

**62. When to stop a recording.** *Ruled by decision 903: A.*

- **A (recommended).** When the events in Process Monitor are later than the
  end of the run.
- B. A fixed wait of 10 minutes.

**63. The output of a different test in the line of item 5.** *Ruled by
decision 904: A.* The owner's words were "Accept both", for questions 63 and
64.

- **A (recommended).** "a test with no complete record that reads the output
  of a different test".
- B. The text of the proposal of decision 852.

**64. The limits of items 3 and 4 in the line.** *Ruled by decision 905: A.*

- **A (recommended).** The line also names a file of the build tree that
  somebody changes by hand, and a value of an environment variable that is not
  in the fingerprint.
- B. The scope of decision 852.

**The work itself**

**13. The sequence and the commits.** *Ruled by decision 854: A.*

- **A (recommended).** Item 6, then 1, 2, 4 and 3, and item 5 last. One
  commit for each item, with `check` in the two trees before it.
- B. The sequence 1 to 6.

**14. Each check seen to fail first.** *Ruled by decision 855: A.*

- **A (recommended).** A planted fault for each new check of items 1, 2 and
  3, seen to fail before its commit.
- B. The mutant pass only.

**15. The mutant file.** *Ruled by decision 856: A.*

- **A (recommended).** As in M1-138: a pre-measure on a scratch copy, its
  list to the owner before it is written, and build/debug only if no
  mutant changes compiled code.
- B. The two trees.

## Done when

- [ ] All the questions above, and each question of the design, ruled before
      its code.
- [ ] Items 1 to 6 done, each check seen to fail first. *(This said "Items 1
      to 5" until 2026-10-29: decision 833 added item 6, and this line was
      not changed then.)*
- [ ] `check` passes in the two trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed. A commit at the end; the push only on the owner's go-ahead.
