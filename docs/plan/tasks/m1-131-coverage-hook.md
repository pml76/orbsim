# M1-131 — The coverage hook and the recorder

Phase: B | Status: not started
Prerequisites: M1-129
Decided by: register decisions 544, 551, 559, 564-566, 569 and 570, ruled
2026-10-15, and 582-595 and 601, ruled 2026-10-16; split out of M1-129 as its
item 9's step C by decision 596.
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
   fingerprints (548). With it on:
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
     flags line up with the address table before writing anything (559);
   - writes only when `ORBSIM_COVERAGE_RECORDS` names a folder, which the
     driver sets for its run and empties before it (570, 594);
   - writes one file per process, named after its process number and start
     time (564): the program, its command line, the value of
     `ORBSIM_TEST_NAME`, and the functions entered;
   - writes after every static destructor has run, so that code running at
     exit is recorded too;
   - carries the suppressions of decision 565 and no others: any other finding
     comes back to the owner.
3. **The test's name, `ORBSIM_TEST_NAME`, in the environment of every test
   `CMakeLists.txt` defines** (564, 594), appended in one place after the last
   test is defined, so that a test added later cannot be missed.
4. **Measured before the option is switched on in the presets**, each brought
   back to the owner if it contradicts its ruling:
   - the build time in both trees, and the size of the linker's maps
     (estimated 220-260 MB a tree, decision 582);
   - `reproducible_build` still passing with the hook and the maps;
   - no two functions at one address (551);
   - the optimised tree's functions without a flag (586; the Debug figure is
     2.2%, all standard-library code);
   - the benchmark with and without the hook on this machine (589), written
     down; M1-47 takes the same both ways on the RX 7900 XTX.
5. **The shader files a test read** (M1-124's item (a); moved here from
   M1-129 by decision 601). Today every test that starts the application is
   fingerprinted on every compiled shader: 50 tests, about 149 s of test time
   in Debug and 89 s in RelWithDebInfo, rerun by any shader edit, and six
   mutant files stay due on any shader change (decision 394). The application
   records the shaders it loaded for the test `ORBSIM_TEST_NAME` names -- the
   naming item 3 builds -- and `scripts/run-tests.py` and
   `scripts/mutants-due.py` fingerprint each such test on those alone. How
   the record is written and read is put to the owner before it is built.

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
      committed. A commit at the end; the push only on the owner's go-ahead
      (595).
