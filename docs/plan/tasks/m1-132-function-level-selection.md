# M1-132 — Tests chosen function by function

Phase: B | Status: started 2026-10-20
Prerequisites: M1-131
Decided by: register decisions 543-553, 556-561, 564, 567, 569 and 570, ruled
2026-10-15, 582-595, ruled 2026-10-16, and 624, ruled 2026-10-17; split out of
M1-129 as its item 9's step D by decision 596. And 695, ruled 2026-10-19; and
701-709, ruled 2026-10-20 before the work started: the method of work, the
start-up check, the tests that run through a script, records that match no
test, the paths of the libraries, the rule that skips a test, a test that
enters other functions in a different run, ADR 0028, and the smaller choices.
And 710 and 711, ruled 2026-10-20 during step 1: the five points of the
readers, and the eight points of the digests; and 712, ruled the same day
during step 2: the hook's table counted from two sources, and a thunk's
record in the debug information counted as a procedure.
Reads what [M1-131](m1-131-coverage-hook.md) records; proved by
[M1-133](m1-133-selection-proved.md).

## Purpose

`scripts/run-tests.py` skips a test only when its whole program is unchanged
(decision 525). This task makes it rerun a test only when a function the test
entered at its last passing run has changed in its compiled code -- in the
trees carrying the hook; the others keep whole-program fingerprints (548).

A function's **digest** is a hash of its compiled bytes, the names of the
functions it points at, and the contents of the data it uses, followed through
data that points at other data (545).

## What to do

1. **compiled-code.py under scripts/** (594), loaded by `scripts/run-tests.py`
   as `scripts/mutate.py` loads `scripts/mutants-due.py`: a reader of Windows
   object files (COFF) and of the libraries that hold them, read directly
   (546), and of the linker's map, whose reader M1-131 begins (632); and the
   digests (545), each object's summary kept in the build tree under the
   file's hash if reading them all takes more than a few seconds (567) --
   more than 3 s for one tree, in `<tree>/run-tests/objects/`, and the driver
   deletes the summaries that the last run did not use (709) -- kept in the
   two trees, and only the digests that the stored records use computed
   (711). And a reader of the module list in a program's .pdb file, which
   gives the path of each library that the program links (705); each member
   that the map names must be a module of its library there (710). The key
   of an object is its path relative to the tree --
   `orbsim_core_d.lib(Orbit.cpp.obj)` for a member of a library -- and its
   full path when it is not in the tree (711). A weak external that no
   object defines is followed to its default (711).
2. **Each recorded address turned into a function through the linker's map**
   (582, amending 560), never through the debug information's names; checked
   against the procedure ranges over a full run before it is relied on (560)
   -- a measurement done one time in each tree, read with llvm-pdbutil and
   written as a measurement record, not part of `check` (709). One name at an
   address is the function; two names that start with `??_E` and `??_G` and
   are the same after that are one function, with the `??_G` name; each
   other set of names fails the check and names the program and the address
   (695). *Measured over a full run in each tree, and written in
   [the measurement record](../../measurements/m1-132-procedure-ranges.md):
   each recorded address starts one procedure record, and only one, an
   `S_THUNK32` record counted as a procedure (712).*
3. **A program carrying the hook is fingerprinted as decision 583 lays out:
   our own code by the hook, everyone else's code by its binary.**
   - Function by function: the digests of the functions the test entered,
     stored per test, a removed function counting as changed (547), and no
     start-up function among them, a test's own registration included (624,
     amending 550 and 587).
   - A module is known to carry the hook by its content: it holds the hook's
     address table (584).
   - A function in such a module that carries no flag counts as entered by
     every test of every program that links it (586) -- judged by code
     section, so that a funclet in a section with an entry in the hook's
     table does not count alone (711). The hook's own start-up function,
     `sancov.module_ctor_bool_flag`, is left out with the other start-up
     functions (711).
   - A member of a library with the hook that the reader cannot read makes
     `check` fail (710); the digests get the keys of the objects without the
     hook, and a name that the map gives to an object in none of the two
     sets is a problem (711).
   - **No start-up function in an object compiled from `src/`** (624):
     `check` fails, naming the object, where one carries a start-up entry, so
     that the rule above stays safe; an exception to `-Wglobal-constructors`
     there comes back to the owner with this ruling. Since decision 702, the
     check examines only the objects from `src/` that carry the hook, and each
     section with a name that starts with `.CRT$` fails it, but for the entry
     of the hook, `.CRT$XCA00002`, when its one relocation points at
     `sancov.module_ctor_bool_flag`. The check is in
     check-coverage-hook.py in scripts/. *Done in step 3: it examines 31
     objects in Debug and 30 in RelWithDebInfo, and finds no start-up entry
     but the hook's own.*
   - Everyone else's code, per program: the bytes of each library or object
     file, and the list of modules the program took from it, from the map
     (585) -- the code compiled in the tree without the hook, the libraries
     built in their own folders, and those from outside the tree, Visual
     Studio's and the Windows SDK's among them.
     The path of each library comes from the program's .pdb file; each
     library that the map names must have one path there, and only one; and
     a library or a program with objects that carry the hook must not hold
     two objects with one file name (705).
   - The program's link command, from Ninja, and lld-link.exe in the machine
     part (583): the last command of Ninja for the program,
     `ninja -n -t commands -s`, read in one call for all programs (709).
   - The libraries the program loads at start (525, 539), and for a GPU test
     the card (532), as now.
   - **The program's bytes stay in the fingerprint with the function part**,
     and a test is skipped when each other part is unchanged and one of these
     two is unchanged: the program's bytes, or its function part (706). The
     digests are thus computed only for the programs with changed bytes.
   - The store goes to version 3; for each program, one table of function
     names, and each test holds numbers into it; an external function is
     known by its decorated name, an internal one by its object's path and
     its name (709).
   - **The linker directives** (`.drectve`) of the program's objects with the
     hook, all of them, `/INCLUDE` included, are a part of the program's
     fingerprint (711): a changed `/alternatename` changes the symbol that a
     name resolves to. What it costs: a test added to a program makes each
     test of that program run again.
4. **Which test a record belongs to** (564): a test starting a program of the
   tree directly, by its command line; a test defined in `CMakeLists.txt`, by
   `ORBSIM_TEST_NAME`, every record carrying it combined. **The records give
   the function part of a program only for a test that starts the program
   directly, or that runs through one of seven CMake scripts that only start
   a program and read its output** -- `RunProbe.cmake`, `RunUsage.cmake`,
   `VerifyAssertionProbe.cmake`, `VerifyAssertionMessage.cmake`,
   `RunBench.cmake`, `VerifyMissingShader.cmake` and `VerifyReadRecord.cmake`,
   a list in `scripts/run-tests.py`. Each other test keeps the bytes of each
   program on its command line, and also gets the function parts of each
   program that its records name (703).
5. **A wrong or missing record fails `check`, naming the program** (590): the
   recorder finding its flags and table out of line, a test that starts a
   hooked program directly passing without a record, a record naming an
   address the map gives no function. The probes that pass by aborting leave
   a record too since decision 634, which amends 559, and are judged like any
   other test. And since decision 704: a record that matches no test, or more
   than one; and a record with a count of flags that is different from the
   number of entries in the address table of the program it names -- the
   number from the map's markers and from the length of the section
   `.SCOVP`, which must agree (712).
6. **Self-tests** (591): the decision rules on made-up inputs; the reader on
   small samples compiled with the tree's own clang as the test runs, among
   them that a test inserted before others leaves their digests unchanged;
   and `compiled_code_self_test` and `run_tests_function_level`, the second
   on a small scratch project carrying the hook, beside
   `run_tests_end_to_end` -- each about 20-30 s, estimated. The samples are
   compiled at `-O0` and at `-O2`, so that each tree's run tests the two
   levels of optimisation; and three cases more than decision 545's list: a
   changed `catch` clause, which changes only the exception tables, the jump
   table of a `switch` statement, and a changed `noexcept` (709). The reader
   of the .pdb file is tested on a program that the test links (705).
7. **ADR 0028** (708), written at the end of the task: a test with a program
   that carries the hook is fingerprinted on the functions it entered; it
   supersedes ADR 0027's rule that uses only the bytes of such a program, and
   it gives the limit of decision 707 -- in a run after its last pass, a test
   can enter a function that it did not enter at that pass; the driver skips
   it, and the full run before each push finds a fault there. M1-133 adds its
   measured figures.

## Mutants

At least: a digest ignoring a relocation; data not followed; a removed function
not counted; a start-up function left in a record; an address mapped to the
wrong function; a function without a flag left out; a start-up entry in an
object from `src/` passed by the check of decision 624; a library from outside
the tree left out of the binary part; a missing record passed over -- each
caught by its self-test. And for the rulings of 2026-10-20: a `.CRT$` section
other than the hook's entry passed (702); a test run through a script that is
not on the list given the function part (703); a record that matches no test,
or with a count of flags that is not correct, passed (704); a library path
that does not come from the .pdb file, or two objects with one name, passed
(705); a test skipped when
a part other than the program changed (706). The file is m1-132.json in
scripts/mutants/, run in `build/debug`; a mutant that only RelWithDebInfo
catches goes into a second file (709). And for decisions 710 and 711: a
member that the .pdb file does not hold passed; a weak default not followed;
an object in none of the two sets passed; a funclet counted alone; a start-up
function counted as without a flag; `/INCLUDE` left out of the directives;
the digests on request computing each section.

## Done when

- [ ] Every question the design raised ruled, and the rulings in the register.
      *The questions known before the work started: decisions 701-709,
      2026-10-20.*
- [ ] `check` passes in both trees, the digest cost measured (567).
- [ ] ADR 0028 written (708).
- [ ] The task's mutant file has run after the commit and its record is
      committed. A commit at the end; the push only on the owner's go-ahead
      (595).

## Background, moved from M1-129 on 2026-10-16 (decision 596)

The section below is M1-129's item 9 as it stood, word for word: the spike, and
the rulings of 2026-10-15 the three tasks rest on.

## 9. Function-level test selection (decision 543)

Added 2026-10-15, to be done **before this task's commit**. Whole-program
fingerprints (decision 525) rerun every test of a program whose bytes changed;
nearly every program links the core library, so a one-function fix to
`Orbit.cpp` reran 507 tests. Function level reruns a test only when a function
it entered, at its last run, changed in its compiled code.

**The spike, 2026-10-15** (`build/wtfn`, machine-local, removed with the
task's scratch): a clang Debug tree with `-ffunction-sections` and
`-fsanitize-coverage=func,trace-pc-guard`, a recorder linked into every
program, replayed over M1-118's 16 commits; C++ tests only, the rest being
judged alike either way.

| | Whole program | Function level | Function level, strict |
|---|---|---|---|
| test runs | 4,646 | 840 (18%) | 1,988 (43%) |
| test time, summed | 6,158 s | 1,997 s (32%) | -- |

Per commit, whole program against function level: `wrapTau` 505/121; mean
motion 507/30; `propagateElements` 508/183; angular velocity 511/22; anomaly
converters 521/209; period 522/38; double-double 523/35; `angleBetween`
524/166; `stateFromElements` 525/36; the docs-only commits and the fuzz
commit 0/0. "Strict" counts added and removed functions as changed too; a
function that did not exist cannot have been entered, and its caller changed.

How a function is compared, as the spike settled it on four known edits: its
raw instruction bytes and the names its relocations point at, labels starting
with `$` inside it; a data item -- a data section, named by its symbols --
whose contents changed makes every function referring to it changed. A comment
changed nothing; a `constexpr` table value changed exactly the function
reading it.

**The 18% is probably too low** (found 2026-10-15, decision 561): the spike
turned each recorded address into a name through the debug information, which
gave 2,813 of the 15,962 names in one record in their readable form, and
those never matched a digest, filed under the object file's names. A change
to such a function could select no test. Step 1c measures the real figure.

What the remaining 18% is: tests whose own file was edited (Catch2 registers
every case at start-up, so the whole program reruns -- 147 runs); the GPU
chain, one probe entering a changed function rerunning the probe suites
through decision 508; line numbers in assertion messages shifting functions
below an edit; and probably an artefact of the spike's keying of unnamed data
sections by position, which makes 18% an upper bound.

**The design questions were ruled on 2026-10-15** (1a, decisions 544-553):
the hook in both `check` trees if it measures cheap (544); a function's digest
its code, the names it points at and the contents of the data it uses,
followed through data, with no data named by position (545); the object files
read by a COFF reader in Python (546); a removed function that was entered
counts as changed (547); trees without the hook keep whole-program
fingerprints (548); the project's assertions print a stack trace from the
debug information instead of carrying a line number (549); Catch2's line
information fixed in one project header, and a test counting only its own
registrar (550); folding checked before addresses are named (551); and the
selection proved safe by `m1-118.json`'s mutants (552). The order (553): first
`check` passing in both trees with items 2-5, then the measurements those
rulings call for, then the build (1b) and M1-118's commits measured with the
real implementation (1c).

**Measured and ruled further on 2026-10-15** (decisions 554-563): `check`
passed in both trees with items 2-5 after the fixes of 554 and 555; the
assertion message names file, function and condition and adds a stack trace
taken inside the macro (556), `ORBSIM_ENSURES` alike (557); two same-named
sections in a test case are refused (558); the hook is `inline-bool-flag`
with `pc-table`, no measurable cost in either tree, where a call at every
entry cost about 30% of the Debug tests (559); an address becomes a function
through its object-file section, never through the debug information's names
(560); and the split by cause is measured in step 1c (561).
