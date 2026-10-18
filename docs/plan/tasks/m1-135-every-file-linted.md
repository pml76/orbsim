# M1-135 — Every compiled file linted, or named as an exception

Phase: B | Status: **in progress from 2026-10-18**, on a branch of its own
Prerequisites: M1-131, for decision 650's line
Decided by: register decisions 650 and 660-671, ruled 2026-10-18 before the
work began -- the task and its place (660), the guard in the lint-dependency
check (661), the files it holds (662), the generated self-checks (663), a
header no linted file includes (664), how an exception is named (665), the
order against M1-131 (666), seeing it fail (667), its mutants (668), its build
trees (669), the register's numbers (670), and four smaller choices (671).
Decisions 660-679 are this task's (670).

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
accepted -- judged by `lint_deps_self_test` and `lint_deps`. Its pass runs
after the commit, in this task's Debug tree, and its record is committed on
the branch. `m1-89.json` becomes due and runs at phase B's gate.

## Done when

- [x] The questions are ruled and recorded (660-671).
- [ ] The self-test's new cases are seen to fail before the check exists.
- [ ] The check names `tests/ReadRecordListener.cpp`, and nothing else, in the
      main checkout's Debug tree.
- [ ] The planted file is caught, accepted as an exception, and caught again
      as a stale one.
- [ ] `check` passes in both of this task's trees, after the move onto
      M1-131's commit.
- [ ] The mutation pass has run and its record is committed.
- [ ] Nothing is pushed: the branch is merged by the owner.
