# M1-131 — The coverage hook and the recorder

Phase: B | Status: not started
Prerequisites: M1-129
Decided by: register decisions 544, 551, 559, 564-566, 569 and 570, ruled
2026-10-15, 582-595 and 601, ruled 2026-10-16, and 625-632, ruled 2026-10-17
before the task began -- every question it raised, item 5's among them; split
out of M1-129 as its item 9's step C by decision 596.
The selection that reads what this records is [M1-132](m1-132-function-level-selection.md),
and [M1-133](m1-133-selection-proved.md) proves it. The spike and the design
behind all three are in M1-132's last section.

## Purpose

`check` reruns a test whenever its program's bytes change, and nearly every
program links the core library, so a one-function fix to `Orbit.cpp` reran 507
tests. Choosing tests function by function needs to know which functions each
test entered at its last run. This task records that, and nothing else: the
driver does not read it until M1-132.

A **coverage hook** is a compiler option that makes every function do
something of its own when it is entered. The one chosen,
`-fsanitize-coverage=func,inline-bool-flag,pc-table`, sets a flag per function
with no call, and keeps a table of every function's address beside the flags;
it measured no cost in either tree's test time, where a call at every entry
cost about 30% of the Debug tests (decision 559).

## What to do

1. **The option**, `ORBSIM_COVERAGE_HOOK`, on in the `debug` and
   `relwithdebinfo` presets only (566); every other tree keeps whole-program
   fingerprints (548). The six presets that inherit those two today inherit
   two hidden presets holding today's settings instead, `debug-base` and
   `relwithdebinfo-base`, each checked to resolve to the settings it has now,
   and the configure step refuses the option with a compiler other than
   clang (625). With it on:
   - the hook, with `-ffunction-sections` and `-fdata-sections` (545), on the
     project's own targets only (569) and on none that compiles someone
     else's code (588): not `orbsim_erfa` or ERFA's validation programs, not
     `orbsim_lodepng`, `orbsim_stb_image` or `vma_impl`;
   - every program linked with the linker's map, lld-link's `/map` (582).
2. **The recorder**, CoverageRecorder.cpp under tests/, in the OBJECT library
   `orbsim_coverage_recorder`, so that it is linked into every program, the
   application included, as `orbsim_abort_behaviour` is (566, 594); compiled
   without the hook. It:
   - supplies the four section markers lld-link does not, and checks that the
     flags line up with the address table, and that the pointers the hook
     hands over match its own markers, before writing anything (559, 627);
   - writes only when `ORBSIM_COVERAGE_RECORDS` names a folder, which the
     driver sets for its run and empties before it (570, 594);
   - writes one file per process, named after its process number and start
     time (564) -- the number from the C runtime, the time from the C++ clock
     at the hook's first start-up call (627) -- ending `.functions.txt` (632):
     the program, its command line, the value of `ORBSIM_TEST_NAME`, and the
     functions entered;
   - writes after every static destructor has run, so that code running at
     exit is recorded too: the hook's first start-up call registers the
     writer with `atexit` (626);
   - carries the suppressions of decision 565 as decision 627 narrows it, and
     627's, and no others: any other finding comes back to the owner.
3. **The test's name, `ORBSIM_TEST_NAME`, in the environment of every test
   `CMakeLists.txt` defines** (564, 594), appended in one place after the last
   test is defined, so that a test added later cannot be missed.
4. **Measured before the option is switched on in the presets**, and every
   figure brought to the owner before it is, whatever it is (631):
   - the build time in both trees, and the size of the linker's maps
     (estimated 220-260 MB a tree, decision 582);
   - `reproducible_build` still passing with the hook and the maps;
   - no two functions at one address (551);
   - the optimised tree's functions without a flag (586; the Debug figure is
     2.2%, all standard-library code);
   - the benchmark with and without the hook on this machine (589), written
     down; M1-47 takes the same both ways on the RX 7900 XTX;
   - the size and time of the mutation harness's copy of the build tree,
     which now holds the maps (631).
5. **The shader files a test read** (M1-124's item (a); moved here from
   M1-129 by decision 601). Today every test that starts the application is
   fingerprinted on every compiled shader: 50 tests, about 149 s of test time
   in Debug and 89 s in RelWithDebInfo, rerun by any shader edit, and six
   mutant files stay due on any shader change (decision 394) -- 51 of 580
   tests on 2026-10-17. The application records the shaders it loaded for the
   test `ORBSIM_TEST_NAME` names -- the naming item 3 builds -- and
   `scripts/run-tests.py` and `scripts/mutants-due.py` fingerprint each such
   test on those alone. **How, as decision 629 ruled**: in every tree, the
   application writes a record of its own into the folder
   `ORBSIM_COVERAGE_RECORDS` names, ending `.shaders.txt` (632), opened at
   start with the test's name and given each shader file's full path as the
   renderer is asked for it, through a callback handed to
   `VulkanContext::create`; the driver fingerprints a test on the shaders of
   every record carrying its name and stores the list, a test that left none
   keeping every shader; `mutants-due.py` takes the stored list while the
   application is byte for byte what it was then (616), otherwise a probe's
   sidecar (394), otherwise every shader.

**The names** (632): coverage_recorder_probe.cpp under tests/, the self-test's
program, and coverage_recorder_unaligned.cpp under tests/, flags without the
address table; the CTest test `coverage_recorder`, run through
check-coverage-recorder.py under scripts/; check-coverage-hook.py under
scripts/, with the CTest tests `coverage_hook` and `coverage_hook_self_test`
-- the hook on exactly the project's own objects, every program with its map,
every test `CMakeLists.txt` defines carrying its name; and the map's reader
begun in compiled-code.py under scripts/, which M1-132 extends. The
self-test's program holds one global object with a destructor, the two
warnings that refuse one switched off at that declaration alone (628).

## Mutants

At least: the recorder writing nothing; writing without the folder variable
set; writing before the static destructors; the flags-and-table check removed;
the hook on a target compiling someone else's code; a test defined in
`CMakeLists.txt` without its name in the environment -- each caught by a test.

## Done when

- [ ] A self-test runs a small program carrying the hook with the folder
      variable set and requires its record to name exactly the functions it
      entered, a function entered by a static destructor among them; and
      without the variable, no file.
- [ ] The measurements of item 4 are written down, with the owner's word on
      any that contradicted its ruling.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed; every other file listed as due waits for phase B's gate
      (630). A commit at the end; the push only on the owner's go-ahead
      (595).
