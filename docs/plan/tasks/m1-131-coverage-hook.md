# M1-131 — The coverage hook and the recorder

Phase: B | Status: not started
Prerequisites: M1-129
Decided by: register decisions 544, 551, 559, 564-566, 569 and 570, ruled
2026-10-15, 582-595 and 601, ruled 2026-10-16, 625-632, ruled 2026-10-17
before the task began -- every question it raised, item 5's among them -- and
634, ruled 2026-10-18 after a spike, the record written however a program
ends, and 635, shaders judged alike by both scripts; and 636-644, ruled
2026-10-18 before the work began -- the self-test's plain program, the `end`
line, the writers taking turns, the 51 programs, Windows only, the `check`
trees for the measurements, the names, a supposed shader change, and four
smaller choices; and 645-651, ruled the same day while items 1-3 were built --
the two NOLINTs and the warning of the self-test's program, the turn-taking
tested in a suite of its own, the refusals checked by hand, an unlinted file,
and the smaller choices; and 652-654 the same day -- the document-link check
reading only what git does not ignore, `mutant-anchors` waiting for the
programs whose tests it reads, each in a commit of its own, and the recorder's
end on a stuck writer declared a surviving mutant; and 696-699, ruled
2026-10-19 before the mutant file was written -- two more ways of stopping in
the self-test, three more declared survivors, the pass in `build/debug`, and
about 45 mutants; split out of M1-129 as its item 9's step C by decision 596.
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
   clang (625) or on a system other than Windows (640). Loops at the end of
   `CMakeLists.txt` over the top directory's targets give them what follows,
   as `orbsim_pool_top_level_compiles` gives them their pool (644). With it on:
   - the hook, with `-ffunction-sections` and `-fdata-sections` (545), on the
     project's own targets only (569) and on none that compiles someone
     else's code (588): not `orbsim_erfa` or ERFA's validation programs, not
     `orbsim_lodepng`, `orbsim_stb_image` or `vma_impl` -- nor the recorder,
     `coverage_recorder_probe_plain` (636) or the four header self-check
     targets, compiled from generated files (644);
   - every program linked with the linker's map, lld-link's `/map` (582).
2. **The recorder**, CoverageRecorder.cpp under tests/, in the OBJECT library
   `orbsim_coverage_recorder`, so that it is linked into every program
   carrying the hook, the application included -- 51 in each `check` tree,
   not ERFA's two validation programs (639) -- as `orbsim_abort_behaviour`
   is into the suites (566, 594); compiled without the hook. It:
   - supplies the four section markers lld-link does not, and checks that the
     flags line up with the address table, and that the pointers the hook
     hands over match its own markers, before writing anything (559, 627);
   - writes only when `ORBSIM_COVERAGE_RECORDS` names a folder, which the
     driver sets for its run and empties before it (570, 594);
   - writes one file per process, named after its process number and start
     time (564) -- the number from the C runtime, the time from the C++ clock
     at the hook's first start-up call (627) -- ending `.functions.txt` (632):
     the program, its command line, the value of `ORBSIM_TEST_NAME`, how the
     program ended, the flags' count, and the functions entered, each as its
     address less the program's base address, in hexadecimal (644); and last
     a line `end` with the number of functions, without which, or with
     another number, the record counts as none (637);
   - writes after every static destructor has run, so that code running at
     exit is recorded too: the hook's first start-up call registers the
     writer with `atexit` (626);
   - **writes too when the program stops abnormally** (634): on `abort`, which
     a failed assertion and `std::terminate` call, through a `SIGABRT`
     handler; on a crash, the debug library's checks among them, through a
     vectored exception handler acting only on the kinds that end a program;
     on a bad argument to the C runtime, through its invalid-parameter
     handler, which then ends the program with the runtime's own
     `__fastfail` -- each leaving the exit code and the output as they were,
     and no record written twice at once: the writers take turns, one on
     another thread waiting, boundedly, and one on the same thread giving up
     (638);
   - carries the suppressions of decision 565 as decision 627 narrows it, and
     627's and 634's, and no others: any other finding comes back to the
     owner.
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
     which now holds the maps (631);
   - how often, over a full run of each tree, the crash handler acts in a
     program that goes on to end normally -- an exception some code catches,
     which it sees first (634);
   - the longest a record's write takes, which sets the bound of decision
     638's wait.

   They are measured in the two `check` trees, the option set by hand when
   they are configured and the figures without the hook taken first (641).
5. **The shader files a test read** (M1-124's item (a); moved here from
   M1-129 by decision 601). Today every test that starts the application is
   fingerprinted on every compiled shader: 50 tests, about 149 s of test time
   in Debug and 89 s in RelWithDebInfo, rerun by any shader edit, and eleven
   mutant files stay due on any shader change, measured 2026-10-18, where
   decision 394 had counted six -- 51 of 580 tests on 2026-10-17. The
   application records the shaders it loaded for the test `ORBSIM_TEST_NAME`
   names -- the naming item 3 builds -- and
   `scripts/run-tests.py` and `scripts/mutants-due.py` fingerprint each such
   test on those alone. **How, as decision 629 ruled**: in every tree, the
   application writes a record of its own into the folder
   `ORBSIM_COVERAGE_RECORDS` names, ending `.shaders.txt` (632), opened at
   start with the test's name and given each shader file's full path as the
   renderer is asked for it, through a callback handed to
   `VulkanContext::create`, and ended with a line `end` after `run()`
   returns, a record without it counting as none (637); the two variables
   read through `SDL_getenv` (644); the driver fingerprints a test on the shaders of
   every record carrying its name and stores the list, a test that left none
   keeping every shader; `mutants-due.py` takes the stored list while the
   application is byte for byte what it was then (616), otherwise a probe's
   sidecar (394), otherwise every shader. **And both scripts judge those
   shaders alike** (635): by the compiled files' bytes, each read in full, a
   pass recording each one's hash for its judges -- where `mutants-due.py`
   read the shader sources' text through `git diff` -- so a comment-only
   shader edit makes no test run and no file due -- the shader a file's own
   mutants change included, where every other mutated file stays on its
   source text; `--assume-changed`, given a shader's source, treats its
   compiled file as changed, so that the three tests that suppose such a
   change keep their command lines and expectations (643).

**The names** (632): coverage_recorder_probe.cpp under tests/, the self-test's
program, and coverage_recorder_unaligned.cpp under tests/, flags without the
address table; the CTest test `coverage_recorder`, run through
check-coverage-recorder.py under scripts/; check-coverage-hook.py under
scripts/, with the CTest tests `coverage_hook` and `coverage_hook_self_test`
-- the hook on exactly the project's own objects, every program with its map,
every test `CMakeLists.txt` defines carrying its name; and the map's reader
begun in compiled-code.py under scripts/, which M1-132 extends. The
self-test's program holds one global object with a destructor, the two
warnings that refuse one switched off at that declaration alone (628). And
(642): `coverage_recorder_probe_plain`, the self-test's program built again
with neither the hook nor the recorder (636); `<tree>/coverage-records/`, the
folder the driver names; ShaderRecord.hpp and ShaderRecord.cpp under src/app/,
the type `ShaderRecord`, the application's writer; `ShaderRequested`, the
callback's type in `src/render/VulkanContext.hpp`; and the CTest test
`shader_record`, requiring the `clear` probe's record to name exactly
`fullscreen.vert.spv`, `probe_gradient.frag.spv` and `tonemap.frag.spv`, and a
usage run's to name none. And (648): `tests/WriterTurns.hpp`, the writers'
turn-taking, which the recorder calls, and the Catch2 suite
`test_writer_turns`, testing it with two threads held in step.

## Mutants

At least: the recorder writing nothing; writing without the folder variable
set; writing before the static destructors; the flags-and-table check removed;
the hook on a target compiling someone else's code; a test defined in
`CMakeLists.txt` without its name in the environment; no record on an abort,
on a crash, or on a bad argument to the C runtime; a handler that changes the
exit code; a record without its `end` line, or a shader record without it
taken as complete; a writer that gives up where it should wait, caught by
`test_writer_turns` (648) -- each caught by a test. Four are declared
survivors, each with its reason: the recorder's end on a stuck writer (654),
the two comparisons of the hook's pointers with the section markers, and the
recorder giving up on a second write on the same thread (697). The file is
m1-131.json under scripts/mutants/, about 45 mutants, run in `build/debug` (698,
699).

## Done when

- [ ] A self-test runs a small program carrying the hook with the folder
      variable set and requires its record to name exactly the functions it
      entered, a function entered by a static destructor among them; and
      without the variable, no file. **And the same program stopped each way
      decision 634 names** -- a failed assertion, an exception escaping a
      `noexcept` function, a crash, the debug library's check, a bad argument
      to the C runtime, and since decision 696 a division by zero and a
      privileged instruction, the two other kinds of crash the handler acts
      on -- requiring each record to name the functions entered
      up to the stop, and the exit code and the output to be those the
      program has without the handlers: those of
      `coverage_recorder_probe_plain`, the same program built with neither
      the hook nor the recorder (636). The exception escaping a `noexcept`
      function carries a NOLINT for `bugprone-exception-escape` (645), the
      bad argument is `_itoa_s` with no buffer (646), and the configure's two
      refusals are checked by hand and written down (649).
- [ ] The measurements of item 4 are written down, with the owner's word on
      any that contradicted its ruling.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed; every other file listed as due waits for phase B's gate
      (630). A commit at the end; the push only on the owner's go-ahead
      (595).
