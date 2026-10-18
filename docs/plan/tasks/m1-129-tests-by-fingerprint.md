# M1-129 — Tests that run only when what they read has changed

Phase: B | Status: done 2026-10-17, and pushed the same day (`28fd3c5`)
Prerequisites: M1-118
Decided by: register decisions 483-491, 495-506 and 508-510, ruled
2026-10-12, 511-519, ruled 2026-10-13, 520-524 and 525-542, ruled
2026-10-14, 543, 544-568 and 569-581, ruled 2026-10-15, 582-596 and 601-616,
ruled 2026-10-16, and 617-622, ruled 2026-10-17. **Split on 2026-10-16** (decision 596): item 9's steps C, D and E
are [M1-131](m1-131-coverage-hook.md), [M1-132](m1-132-function-level-selection.md)
and [M1-133](m1-133-selection-proved.md), after this task.
Continues [ADR 0024](../../adr/0024-verification-costs-what-changed.md); its
sibling, the code as modules, is [M1-130](m1-130-code-as-modules.md).

## Purpose

M1-118 ran `check` in both trees about eighteen times, and every run reran all
557 tests, whatever had changed. CTest remembers nothing, so every test runs
every time. This task makes a test run only when something it reads has
changed, keeps a full run before every push, and removes two fixed costs every
`check` and every mutation pass carry today.

## Measured before the ruling (2026-10-11/12)

- **Fingerprints, simulated over M1-118's 16 commits** in a worktree under
  `build/`, removed afterwards: **2,284 test runs instead of 8,771 (26%)**, of
  which 1,008 are the 63 GPU tests counted as always running; **16% if they
  are fingerprinted too** (decision 490). A change inside one layer reran
  105-118 of about 540 tests; `Scalar.hpp` 491, `Math.hpp` 312-329; a
  document 63. The simulation saw only files named on a test's command line,
  so it can under-count for tests that read other files -- item 3 closes that.
- **Reproducible builds**: today an unchanged file compiles and links to
  different bytes -- measured on `LeapSeconds.cpp`'s object and on
  `test_math.exe`. With the compiler's `-mno-incremental-linker-compatible` and
  the linker's `/Brepro`, all 48 programs were identical after a clean rebuild.
- **The `mutants_due` script tests**: eight of them, run one after another
  under one lock, 17-33 s each -- about 3 of the Debug tree's 4.3 minutes of
  tests.
- **A mutation pass leaves a full rebuild behind**: `mutate.py` restores files
  with `git checkout`, which gives them a new time; the next `check` took 18
  minutes for both trees with no source change.
- **What the tests open, recorded** with Process Monitor over a full Debug
  run (2026-10-12, after the ruling): six programs read files under `data/`
  and the application reads `tests/golden/`, none named on a command line;
  five read images the probes write in the same run (decision 508); and the
  results also rest on files outside the repository -- the Vulkan SDK's
  validation layer, Windows' time-zone database, the Python, CMake and clang
  installations (decision 509). Two more script tests read whole folders
  (decision 510).

## What to do

1. **Reproducible builds in every tree** (decision 488): the two settings
   above, and a test that rebuilds a program and requires identical bytes.
   Each toolchain measured by two builds from clean (decision 504): Windows
   clang 945 of 963 files different without the settings and 0 with them;
   gcc-14 and clang on Linux 0 of 642 with none; MSVC 0 of 967 once built
   without debug information (decision 511). `reproducible_build` fails
   without the clang settings, naming the object and the program, and
   passes with them.
2. **Fingerprints** (decision 483): before a test runs, a hash of its program,
   its command line, its environment and every file it reads; equal to its
   last passing run, the test is skipped and reported as skipped. Built on
   what `scripts/mutants-due.py` already knows about each test. **Kept in the
   build tree**, never committed (decision 491).
3. **What a test reads besides its command line** (decision 489): each test
   **records the files it opens**, in a record file read after the run, as the
   probes already do for shaders; until a test records, **a conservative rule
   covers it** -- any change under `data/`, `tests/golden/` or the build files
   reruns every test that could read them. M1-124's item (a), the shader files
   a test read, came here (decision 495) and was not built; it moved on to
   [M1-131](m1-131-coverage-hook.md), which names the test each process
   belongs to (decision 601).
4. **The 63 GPU tests fingerprinted too** (decision 490): their programs, the
   shaders, the driver version and the card.
5. **A full, unskipped run before every push** (decision 484), and a
   self-test that changes one file and requires exactly the expected tests to
   run, and one that requires the full run to skip nothing (VERIFICATION rule
   23).
6. **`mutate.py` restores the tree from a snapshot** (decision 506): the
   build outputs and Ninja's record files copied at the start of a pass, and
   after each mutant the outputs, then the file's text, then its time put
   back, with everything the tests wrote; so a pass leaves no rebuild behind.
   Restoring the text and the time alone was measured to leave outputs built
   from mutated code. **Done second, 2026-10-13** (decisions 512-519): the
   copy is the whole tree but the dependencies' downloaded sources; after
   each mutant the build must report nothing to do; a stopped pass's
   snapshot blocks the next until `mutate.py --restore` has used it; and
   `rebuild_plan` and `m1-97.json` are gone.
7. **The `mutants_due` tests read the build graph once** rather than eight
   times; and M1-121's findings **4.5** -- `--no-tests=error` on every CTest
   judge, `--verify` refusing a judge that does not exist and an empty mutant
   list, `mutants-due.py` counting an unknown judge as due -- and **4.8** -- a
   mutant file an input of its own due-list entry -- are done here, since they
   change the same scripts (decision 495). Their mutants come with them: no
   `--no-tests=error`, and the spec not an input, each caught by its
   self-test. **Done third, 2026-10-14** (decisions 520-524): Ninja's answers
   stored in the tree, `mutants-due-graph.json`, under a key of what they rest
   on and the script's own text; the fingerprints check compares a fresh
   reading with them; a missing judge is refused by a pass and by `--verify`
   and makes its file due; a file's own mutants, not its note, are its input.
8. **The working loop** written into `CLAUDE.md`'s definition of done: the
   quick targets -- `lint`, `format-check`, `doc-links` -- and the affected
   suites before a full `check`. **Amended by decision 593** (2026-10-16):
   `check` now runs only the affected tests itself, so the loop is the quick
   targets while working, then `check`; and `check-full` and the pre-push
   hook join the definition of done.

**Items 2-5 as one step** (decisions 525-537, ruled 2026-10-14): a program
is fingerprinted by its own bytes and those of every library it loads (525);
every file is hashed in full on every run (526); a read record holds each
file's hash beside its path, taken as the test case ends (527), and is named
after its test with forbidden characters escaped (528); tests name the files
they read through a recording helper (529), and a check in `check` keeps every
other way out of `tests/` (530); script tests are fingerprinted on all of
`scripts/` and `cmake/` (531); the card is read from a clear probe the driver
runs before CTest (532); the files outside the repository are one "machine"
part of every fingerprint (533); `check` skips in every tree and the gate
trees use `check-full` (534); the push order is commit, mutation pass, record,
`check-full` in both trees, push (535); only a passed test is remembered
(536); and the self-tests are those of decision 537. Then, as the work
measured: `tests/TestReads.cpp` optimised where the compiler allows (538),
only the libraries a program loads at start (539), the end-to-end test kept
(540), `ldd` and two zoneinfo files on Linux (541), and a full run refused
after an edit newer than the build's last step (542).

## Questions put at the start

Whatever the design raises that decisions 483-491 do not settle, put to the
owner before the work starts: how the record files are named and read, what a
"skipped" result looks like in CTest's output, and how the full run before a
push is enforced.

## Mutants

At least: a fingerprint that ignores a file it should include; a skipped test
that should have run; the full run before a push skipped; `mutate.py`
restoring the text and the time but not the outputs; a recorded read left out
-- each caught by its self-test.

**As written** (decisions 602-606): `scripts/mutants/m1-129.json` holds
items 1-7 and step B, judged in `build/relwithdebinfo`, and
`scripts/mutants/m1-129-debug.json` step A, judged in `build/debug`, where
assertions are live. Before they were written, a spike on a scratch copy of
the driver found eight of its parts that no test could see broken -- a
program's libraries, the card, the listener, the machine part, the
environment, the definition, the shaders, a folder named on a command line --
because the self-test checked which parts a fingerprint has, never that each
moves with its input (decision 602). Each now has a self-test case that
changes its input, `run_tests_in_tree` asks the real tools again, and every
one of the eight was seen caught. The enforcement of the full run -- `--full`
on `check-full`, the pre-push hook -- is tested by `parallel_tests` and by
`run_tests_end_to_end` pushing through the real hook (decision 603); and a
read before the first test case, by a listener in `test_read_records`
(decision 604).

## Done when

- [x] Every question the design raised ruled, and the rulings in the register.
- [x] Before and after for a clean `check`, a one-file change, a header change
      and a mutation pass, written in
      [`docs/measurements/m1-129-before-after.md`](../../measurements/m1-129-before-after.md)
      and linked from `STATUS.md` (decision 607).
- [x] `check` passes in both trees.
- [x] The task's mutant file has run after the commit and its record is
      committed, and so has every mutant file `scripts/mutants-due.py` lists
      (decision 580); then `check-full` in both trees, and the push on the
      owner's go-ahead (decisions 535 and 595). *The passes are done,
      2026-10-17, and their record committed with this document; `check-full`
      in both trees and the push followed the same day, up to `28fd3c5`,
      confirmed by a fetch.*

*Moved by decision 596 (2026-10-16):* item 9's steps C to E, and M1-118's 16
commits measured with them against the spike's 18% (decision 543), are
M1-131 to M1-133; and M1-118's commits measured with this task's
whole-program fingerprints, against the simulation's 26%, are measured in the
same replay, in M1-133.

## Where it stands (2026-10-17, the finish)

**Steps 1-3 of the list below are done** (decisions 617-623):

- **The "after" timings** (607, 619) and **scenario 4**, the mutation pass and
  the `check` after it, are in
  [the record](../../measurements/m1-129-before-after.md): with nothing
  changed, `check` takes about 6 s in each tree.
- **Every mutant file passed after the task's commits** (580, 617, 618): 41
  files, 532 mutants, 504 caught, the 28 survivors all declared, in 4 h
  33 min. `m1-129-debug.json`'s first pass found its check,
  `cmake/VerifyAssertionMessage.cmake`, blind to three of its mutants; the
  check now looks for each part where it belongs, measured on Windows and
  Linux before it was committed (622). Windows refused four writes during
  the passes, and each such file ran again (623; `STATUS.md`'s open item
  (g)).
- **The records**: this document, `HISTORY.md` section 7, `STATUS.md` and
  `BRIEFING.md` section 7.

**Left**: step 4, `check-full` in both trees from a clean working tree, then
the push on the owner's go-ahead and a fetch to confirm it; and step 5,
after the push.

## Where it stood (2026-10-17, the second session's end)

**Every question the finish raised ruled, decisions 601-616**, and built:

- **The gaps closed, each test seen failing first.** A spike on a scratch copy
  of the driver found eight parts of a fingerprint no test could see broken
  (decision 602); each now has a self-test case that changes its input --
  91 cases where there were 69 -- and `run_tests_in_tree` asks the real tools
  again. `check-parallel-tests.py` requires `--full` on `check-full` alone,
  and `run_tests_end_to_end` pushes through the real pre-push hook, 15 steps
  where there were 12 (603). A listener in `test_read_records` reads a file
  before the first test case, and `read_record_written` requires it (604).
  `mutate.py --verify` accepts a git hook's mutant that builds nothing (613).
- **The mutants**: 55 new -- `m1-129.json` 83, items 1-7 and step B, and the
  new `m1-129-debug.json` 7, step A, judged where assertions are live
  (605, 606).
- **ADR 0027** (608), and the documents of decision 609; item (a) moved to
  M1-131 (601).
- **Both fuzzers clean on the new assertion handler**, which the fuzz tree
  had not yet been built with: on an otherwise idle machine `fuzz_orbit`
  14,223,942 runs in 241 s, `fuzz_time` 18,320,819 in 901 s, no finding;
  `fuzz_time` found 11 new inputs, of which `-merge=1` kept 9 (52 features),
  committed. A first pair of runs made while the machine was building gave
  4,924,932 and 13,163,555 runs, also clean; on an idle machine
  `fuzz_orbit` runs about 60,000 inputs a second from either the present
  corpus or M1-118's, so the low counts were the load.
- **`check` passes in both trees**, 580 tests, 147 run and 433 skipped as
  unchanged in each; the suites hold 3,941,219 assertions in 479 test cases.
- **The records compared with what the tests open** (612): Process Monitor
  over one full Debug run, each of the 480 test processes that wrote a
  record compared path for path. One read was left out -- `test_hdr_dump`'s
  missing file, a literal path -- now named through `probeFile()` (614).
  And `test_time_leap` opened no ICU file: Microsoft's library reads the
  leap seconds from the registry, whose value now joins the machine part
  (615). The comparison was seen to report a planted fault, after its first
  version had silently matched nothing.
- **A defect the timings found** (616): a one-line comment in
  `src/orbit/Orbit.cpp` relinked the application to the same bytes, the
  probes were rightly skipped, and `mutants_due_probe_shaders` failed,
  since `mutants-due.py` trusted a probe's sidecar only if it was newer
  than the application. A sidecar the time rule trusted now stays trusted
  while it and the application are byte for byte the same.

**Committed, not pushed**: `485c7ff` (fuzz_time's corpus), `97b7e13` (the
finish's code, tests, mutants and documents), `a6da837` (the records held to
what the tests open), and the commit that writes this section (decision
616). `check` passes in both trees on it. The "before" timings are in
[`docs/measurements/m1-129-before-after.md`](../../measurements/m1-129-before-after.md).

**Next, in this order:**

1. *Done 2026-10-17, in the record.* **The "after" timings** (607), both trees, the machine otherwise idle and
   the repository untouched while they run: copy
   `docs/measurements/m1-129/timings.py` to a scratch folder first -- it
   writes its logs and `timings.jsonl` beside itself, which inside the
   repository would change tracked files during "nothing changed" -- and run
   it as `timings.py after-debug <repository> <repository>/build/debug`, then
   `after-relwithdebinfo`. Write the table into the record, and add the raw
   lines to `docs/measurements/m1-129/timings.jsonl`, after the first
   attempt's three (619).
2. *Done 2026-10-17.* **The mutation passes** (580, 610, 617, 618): `m1-129.json`
   (RelWithDebInfo, 83 mutants) and `m1-129-debug.json` (Debug, 7), then
   `m1-118.json` with the Debug `check` after it, timed for the record's
   scenario 4, then the other files
   `python scripts/mutants-due.py build/relwithdebinfo` lists -- 41 of 41
   due on 2026-10-17 -- each in its own tree, one at a time in the
   background, the machine kept awake and CLion closed. About six hours, so
   the sequence stops between two files once the session's context is 80 to
   90 per cent used, and continues in a new session. A mutant that newly
   survives or does not compile goes to the owner before anything changes.
   Commit the records and `STATUS.md`, each commit's changes listed for the
   owner (621).
3. *Done 2026-10-17.* **The records of the finish**: M1-129's entry under `HISTORY.md` section
   7, `STATUS.md`'s last-task and next-task rows, `BRIEFING.md` section 7,
   this document's status.
4. *Done 2026-10-17, up to `28fd3c5`.* **`check-full` in both trees**, from a clean working tree, then the push
   on the owner's go-ahead, then a fetch to confirm it landed.
5. *Done 2026-10-17.* **After the push**: list and delete `build/item9-reshape/` (decision
   611); keep `build/wt129` for M1-133; delete Process Monitor's two files,
   having first taken their names out of the documents, where `doc-links`
   would report them missing (620).

**Worth knowing for whoever continues.** A tree with no
`mutants-due-sidecars.json`, or whose application was relinked after its
probes last ran, needs its probes to run once before
`mutants_due_probe_shaders` can pass: `ctest -R "^mutants_due_probe_shaders$"`
runs them as its setup tests (decision 616). Both trees have the memory
since 2026-10-17. Decision 612's Process Monitor files in
`build/`, `procmon-m1-129.pml` (1.3 GB) and its `.csv` (0.9 GB), were deleted
after the push (decision 620).

## Where it stood (2026-10-15, third session's end)

**Committed as a checkpoint on `master`, not pushed** -- at the owner's
request, to continue the next day. A push waits for the whole task:
`check-full` in both trees and the mutation passes of decision 580. **`check`
passes in both trees with all of it** (2026-10-15: Debug 579 of 579,
RelWithDebInfo 579 of 579, the assertion probes skipping themselves where
assertions are not live, and the one Linux-only case skipping itself).

Items 2-5:

- read records: `tests/TestReads.hpp`/`.cpp`, `tests/Sha256.hpp`,
  `tests/ReadRecordListener.cpp`, `tests/test_read_records.cpp`, the call
  sites in the suites, `cmake/VerifyReadRecord.cmake`;
- the structural check, `scripts/check-test-reads.py` (CTest `test_reads`);
- the driver, `scripts/run-tests.py`: self-test 69 cases, end-to-end test
  12 steps (`run_tests_self_test`, `run_tests_end_to_end`);
- `check` and `check-full` calling the driver; `scripts/git-hooks/pre-push`;
- the fixes `check` needed: five lint findings in the code above, a
  recorded file held to CMake's SHA-256 (decision 554), and `parallel_tests`
  reading the driver's `--jobs` (decision 555).

Item 9, **step A done** -- the assertion message (decisions 556, 557, 568):
`src/core/Contract.hpp` and the new `src/core/Contract.cpp`, the probe
`tests/assertion_message_probe.cpp` and its checker
`cmake/VerifyAssertionMessage.cmake` (CTest `assertion_message_precondition`
and `_postcondition`, seen failing against `assert` first), `stdc++exp` linked
on Linux, and `addVisibleGrid` asserting after its loop.

Item 9, **step B done** (decisions 569-581, 2026-10-15):

- `tests/TestCase.hpp`, included by every test file in place of Catch2's
  macro header: Catch2's line fixed at 0, and each test's function and
  registration named after the test's own name and tags, a start-up function
  of its own per test (573, 574, 576, 577). Measured on a scratch file: one
  test inserted before two left every other function's digest unchanged,
  where Catch2's counter had changed 7 and removed 8. Seen failing: four
  faults planted in the header, each caught by its case of the new suite
  `tests/test_test_case.cpp` (decision 574). Clean under clang, MSVC
  `/Wall /WX` and gcc-14.
- `scripts/check-test-cases.py` (CTest `test_cases`, `test_cases_self_test`,
  32 cases): only the header includes Catch2's macros, every file writing a
  `TEST_CASE` includes the header, none of Catch2's other registering macros,
  and no two sections of one test case share a name (558, 572, 575). On the
  tree: 477 test cases and 28 sections -- decision 558's 29 had counted a
  comment.
- **The test bodies reshaped to clang-tidy's size and complexity limits**
  (578, 581): the new `TEST_CASE` made the size check reach test bodies, which
  Catch2's never had, and the owner ruled the bodies be reshaped and the 69
  complexity suppressions of 2026-09-09 removed with them. 76 test cases in
  24 files, each long body split into named helpers or a table with one
  check in a loop; no test renamed, split or removed. **Proved unchanged**
  (579): Catch2's JSON report of every CTest test, each run's sections and
  checks in order with their results, identical before and after in both
  trees -- 479 test cases, 3,941,219 checks -- and the numbers in the code
  compared file by file, the only differences a helper's starting `0.0` or an
  array size, and one check shared by two cases through a helper. A shared
  helper `requireRefused` in `tests/OrbitTestSupport.hpp`. In two helpers of
  `tests/test_double_double.cpp`, `REQUIRE(!x)` rather than `REQUIRE_FALSE`,
  for the reason `tests/test_projection.cpp` gives.
- Ten mutants for step B in `scripts/mutants/m1-129.json`: the header's
  line, registration line, tags and file, and six of the check's rules.
- What decisions 569 and 570 settle for steps C and D: the hook on the
  project's own targets, with one fingerprint per program over the code that
  carries none; and the recorder writing only when the driver names a record
  folder.

Decisions 525-581 in the register; working agreement 9 in `CLAUDE.md`.

**Item 9's steps C to E are tasks of their own since 2026-10-16** (decision
596): [M1-131](m1-131-coverage-hook.md), the coverage hook and the recorder;
[M1-132](m1-132-function-level-selection.md), tests chosen function by
function; [M1-133](m1-133-selection-proved.md), the selection proved and
measured -- estimated at 10-12 hours together. Every question they raised was
ruled the same day, before any of their code (decisions 582-595); the spike
and the rulings of 2026-10-15 behind them are in M1-132's last section.

**Next, to finish this task** (point 2 of the order below): its mutants for
items 2-5 and item 9's step A, which have none yet in
`scripts/mutants/m1-129.json` (35 mutants on 2026-10-16: items 6 and 7, and
step B); the before-and-after measurements in `STATUS.md`; the documents,
item 8 among them (decision 593); `check` in both trees; the commit; the
task's own mutant file and every file `scripts/mutants-due.py` lists (decision
580), and their record; `check-full` in both trees; and the push, on the
owner's go-ahead (decisions 535 and 595). The measurements behind decisions
544-581 were taken in scratch trees and folders on the machine they were
taken on; `STATUS.md` names them.

## The order from here (ruled 2026-10-15, decision 543)

1. **Item 9, function-level selection** -- design questions, build, measure.
   **Since 2026-10-16 (decision 596) steps C to E are M1-131 to M1-133, taken
   after point 2.**
2. **Finish this task**: its mutants; the before-and-after measurements in
   `STATUS.md`; the documents (`CLAUDE.md`'s definition of done with
   `check-full` and the pre-push hook, `VERIFICATION.md`, `HISTORY.md`,
   `PROJECT_STATE.md`); item 8; `check` in both trees; then commit, the
   mutation pass and its record, `check-full` in both trees, and push
   (decision 535).
3. **Mutation testing at function level** -- since 2026-10-18 a task of its
   own, [M1-134](m1-134-mutants-chosen-like-tests.md), after M1-133 (decision
   633): which mutants are due, decided per
   mutant from the functions its judging tests enter; only the tests that
   enter the functions a mutant changed run against it; a mutant that leaves
   every compiled function identical is equivalent without running anything
   ("trivial compiler equivalence"). For the Python mutants, the same through
   Python's syntax tree (`ast`) and `sys.monitoring` -- measured on the last
   16 commits of `mutants-due.py` and `mutate.py`: 169 of 586 function
   versions changed (29%), with module-level names tracked one by one -- which
   needs each self-test to mark where its cases begin and end. Keeping the
   tools in Python rather than rewriting them in C++ was recommended.
   Python accounts for 113 of the 467 mutants, CMake 13, shaders 18, C++ 323.
4. **Working agreement 9 applied to the existing process** -- each measured
   before a proposal:
   - the configure step in the definition of done: `cmake --preset` took
     114 s and 106 s with nothing changed (measured with the spike running,
     to be repeated), where Ninja reruns CMake itself and `configure-current`
     refuses a stale tree;
   - tests that cannot depend on the tree -- the scripts' self-tests,
     `doc-links`, `run_tests_end_to_end` -- running in both `check` trees;
   - clang-tidy reading every file once per tree.
5. **Whether agreement 9 covers the documentation too**: `STATUS.md` is
   146 KB, read at the start of every session. **Begun 2026-10-16**
   (decisions 597 and 598): `docs/BRIEFING.md` for a session's start, and
   `STATUS.md` down to about 18 KB, its logs moved word for word into
   `HISTORY.md` and `docs/TEST_SUITES.md`. Whether more follows is still this
   point's question.

Points 3-5 may become tasks of their own; that is decided when each is taken
up. **Settled along the way**: the MSVC exception of decision 538, confirmed
(decision 562), and the proposal to build the Linux trees on WSL's own disk,
to be brought after this task (decision 563). Item 8's text is decision 593:
the quick targets while working, then `check`, with `check-full` and the
pre-push hook in the definition of done.
