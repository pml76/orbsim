# M1-135 — Every compiled file linted, or named as an exception

Phase: B | Status: **in progress from 2026-10-18**, on a branch of its own
Prerequisites: M1-131, for decision 650's line
Decided by: register decisions 650 and 660-671, ruled 2026-10-18 before the
work began -- the task and its place (660), the guard in the lint-dependency
check (661), the files it holds (662), the generated self-checks (663), a
header no linted file includes (664), how an exception is named (665), the
order against M1-131 (666), seeing it fail (667), its mutants (668), its build
trees (669), the register's numbers (670), and four smaller choices (671);
and 672, ruled the same day after the planted file found it -- fetched code
in a folder outside the repository. Decisions 660-679 are this task's (670).

## Purpose

**Nothing makes sure that every file this project compiles is linted.** The
`lint` step lints the files named in `ORBSIM_LINT_SOURCES` in `CMakeLists.txt`,
through the targets in `orbsim_lint_targets` in `cmake/MemoryPool.cmake`. The
configure step stops when a listed file is compiled by none of those targets,
but nothing looks the other way: a file compiled and never listed is simply
never linted, and `check` passes.

It has happened three times. The abort listener and the count-wraparound
probes went unlinted until 2026-09-24 (decision 156); `src/view/` until
2026-09-25 (decision 185); and `tests/ReadRecordListener.cpp`, compiled since
M1-129, was found unlinted by M1-131 on 2026-10-18 (decision 650).

Measured 2026-10-18, read only, in the main checkout's two `check` trees
(decision 662): 1,030 files compiled; 94 under `src/` and `tests/`, 91
linted; the three not linted `src/render/VmaImpl.cpp` and
`tests/float_equal_guard.cpp`, left out on purpose, and
`tests/ReadRecordListener.cpp`.

## What to do

- **`scripts/check-lint-deps.py`, a fifth check** (decision 661), run by the
  CTest test `lint_deps`. Every file the compile database lists inside the
  repository and outside its build folders (662) is linted, or named as an
  exception; and every exception has a reason, names a file this tree
  compiles, names a file that is not linted, and is named once (665). A file
  compiled from outside both the repository and the build tree is refused,
  and an empty compile database too (667). Generated files and fetched code
  in the build tree are not held to it, and the script says why (663).
- **Each failure names the file, the targets compiling it, and both fixes**
  (671): add it to `ORBSIM_LINT_SOURCES`, or name it in
  `ORBSIM_LINT_EXCEPTIONS` beside its target, with its reason.
- **`ORBSIM_LINT_EXCEPTIONS`** in `CMakeLists.txt` (665): entries
  `<path>: <reason>`, each added beside the target that compiles the file and
  under the same condition --
  - `src/render/VmaImpl.cpp`, where the application is built: "VMA's
    implementation, from its one third-party include";
  - `tests/float_equal_guard.cpp`, under clang and gcc: "must not compile --
    check-float-equal-guard.py requires the error".

  `cmake/ScriptTests.cmake` hands each entry to `lint_deps` as
  `--exception <entry>`.
- **The documents** (671): the script's own description;
  `.claude/rules/lint-config.md`, one sentence; the `lint_deps` row of
  `docs/TEST_SUITES.md`; the queue; `docs/STATUS.md`.

## Seeing it fail (decision 667)

1. **Test first.** The self-test's new cases are written before the check
   and seen to fail against the script without it; the failure goes in the
   commit message.
2. **The real omission.** The finished check, run read only against the main
   checkout's Debug tree -- which still compiles
   `tests/ReadRecordListener.cpp` unlinted -- must name that file and nothing
   else.
3. **A planted file** in this task's own Debug tree, `tests/planted.cpp`,
   added by hand to one target and never committed: the check fails naming
   it, passes once it is an exception, and fails again once the exception
   names a file no longer compiled. Then all of it is removed.

The output of each is written below as it is seen.

**(1), 2026-10-18.** The fourteen new cases, run against a check that
accepted everything -- what the script did until then -- gave nine `accepted
(WRONG)`, each a case that must be refused, and five `accepted (ok)`; with
the check written, all 25 cases of the self-test pass. And each of the seven
mutants below, applied to a scratch copy of the script, was caught by its own
case.

**(2), 2026-10-18.** The main checkout's Debug and RelWithDebInfo trees were
configured again at 14:02 and 14:10 that day, with decision 650's line, before
the finished check could run against them: there it found every file linted
but the two exceptions. Run read only against `build/release`, M1-131's tree
without the hook, configured at 12:15 the same day:

```
check-lint-deps: tests/ReadRecordListener.cpp is compiled, by orbsim_abort_behaviour, and neither linted nor named as an exception: add it to ORBSIM_LINT_SOURCES in CMakeLists.txt, or to ORBSIM_LINT_EXCEPTIONS beside its target, with its reason
check-lint-deps: 1 problem(s) in 88 lint steps
```

And against `build/windows-fuzz`, configured 2026-10-16, which builds no
application, given VmaImpl's exception as well: the same line, and
`src/render/VmaImpl.cpp is named as an exception, and this tree does not
compile it`. That run also showed two problems the check's first part reports
there, older than this task: the fuzz programs compile `tests/fuzz_orbit.cpp`
and `tests/fuzz_time.cpp` again, with the fuzzer's flags, and are not among
`orbsim_lint_targets`, so their lint steps do not depend on those objects. No
fuzz tree runs `check`, so it had not shown.

**(3), 2026-10-18**, in this task's Debug tree, configured at the branch's
first commits -- before decision 650's line, so the real omission shows too.
Each step changed `CMakeLists.txt` from a copy, configured again, and ran the
CTest test `lint_deps`, which reads only what a configure writes; nothing was
compiled. The lines the dependency cache drew are left out here (see below):

```
baseline:          tests/ReadRecordListener.cpp is compiled, by orbsim_abort_behaviour, and neither linted nor named as an exception: ...
planted:           tests/planted.cpp is compiled, by orbsim_abort_behaviour, and neither linted nor named as an exception: ...
                   tests/ReadRecordListener.cpp is compiled, ... (as above)
as an exception:   tests/ReadRecordListener.cpp is compiled, ... (as above)
exception, stale:  tests/planted.cpp is named as an exception, and this tree does not compile it: ...
                   tests/ReadRecordListener.cpp is compiled, ... (as above)
restored:          CMakeLists.txt as committed, planted.cpp gone
```

**Found by (3), not foreseen by the rulings.** This tree's five cached
dependencies sit in the main checkout's `build/_deps-cache/`, as decision 669
has it, which is outside this worktree's repository and its build tree; so the
check refused all 376 files compiled from there -- Catch2's 108, SDL3's 267
and vk-bootstrap's one -- as decision 662 has it refuse a file from outside.
Measured on a scratch copy: skipping every folder the tree's `CMakeCache.txt`
names as fetched code -- each `FETCHCONTENT_SOURCE_DIR_<NAME>` set, and
`FETCHCONTENT_BASE_DIR` -- removes exactly those 376 and nothing else here,
and changes nothing in the main checkout's Debug tree, which uses no cache.
**Ruled so, decision 672**, and built test-first: its two self-test cases
were seen to fail against a reader naming no folder -- the named folder's
code `reported (WRONG)` -- and pass once it was written. In this tree the
check then named `tests/ReadRecordListener.cpp` alone.

## The order against M1-131 (decision 666)

The guard is built on this task's branch, which starts at `9061ab0`, before
decision 650's line. There it fails, rightly, on
`tests/ReadRecordListener.cpp`. Once M1-131's commit carrying that line is on
`master`, the branch is moved onto it, and `check` runs after that, in two
trees of the task's own (669), started only on the owner's word.

## Mutants

`scripts/mutants/m1-135.json` (668): one Python mutant for each of the
check's refusals -- an unlinted file accepted, an exception without a reason
accepted, an exception naming a file not compiled accepted, an exception for a
linted file accepted, the same file named twice accepted, a file from outside
the repository and the build tree accepted, an empty compile database
accepted -- and an eighth, code in a folder `CMakeCache.txt` names as
fetched refused (672), judged by `lint_deps_self_test` and `lint_deps`. Its pass runs
after the commit, in this task's Debug tree, and its record is committed on
the branch. `m1-89.json` becomes due and runs at phase B's gate.

## Done when

- [x] The questions are ruled and recorded (660-671).
- [x] The self-test's new cases are seen to fail before the check exists.
- [x] The check names `tests/ReadRecordListener.cpp`, and nothing else, in a
      tree that compiles it unlinted -- `build/release`, which the owner
      accepted for the main checkout's Debug tree, reconfigured first.
- [x] The planted file is caught, accepted as an exception, and caught again
      as a stale one.
- [x] All eight mutants caught in a dry run on a scratch copy of the script.
- [ ] `check` passes in both of this task's trees, after the move onto
      M1-131's commit.
- [ ] The mutation pass has run and its record is committed.
- [ ] Nothing is pushed: the branch is merged by the owner.
