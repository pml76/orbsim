# M1-135 — Every compiled file linted, or named as an exception

Phase: B | Status: **done 2026-10-20, and pushed the same day (`1c4c502`)**
Prerequisites: M1-131, for decision 650's line
Decided by: register decisions 650 and 660-671, ruled 2026-10-18 before the
work began -- the task and its place (660), the guard in the lint-dependency
check (661), the files it holds (662), the generated self-checks (663), a
header no linted file includes (664), how an exception is named (665), the
order against M1-131 (666), seeing it fail (667), its mutants (668), its build
trees (669), the register's numbers (670), and four smaller choices (671);
and 672, ruled the same day after the planted file found it -- fetched code
in a folder outside the repository. And 673-678, ruled 2026-10-20 at the
end: where the task is finished (673), the sequence of its last commit and
its mutation pass (674), smaller decisions (675), how the ASD-STE100
dictionary is applied (676), the spelling (677), and the record in
`HISTORY.md` (678). Decisions 660-679 are this task's (670); 679 is not used.

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

**Amended by decisions 673 and 674, 2026-10-20.** M1-131 was pushed, so the
two commits of the branch were copied onto `master` in the main checkout,
and `check` and the mutation pass ran in the two trees of the main
checkout. The branch was not merged. The guard was committed before the
pass, because the pass records the hash of the commit (674).

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

## The end, 2026-10-20

**Measured before the end, read only** (decision 673). The guard as the
branch had it, run against the two `check` trees of the main checkout with
the two exceptions, passed in the two trees: 96 files of the repository
compiled, 94 linted, and the other 2 the exceptions. Without the
exceptions, it named only those 2 files. It also passed in the gate trees
`windows-msvc` and `asan`, with the exception for VMA, and in `linux-gcc`
and `linux-sanitize`, in WSL. In `linux-gcc` it named
`tests/float_equal_guard.cpp` when that exception was removed. `asan` and
`linux-sanitize` were configured before `float_equal_guard` was added, so
they do not compile it.

**`check`** passed in the two trees before each commit. With the guard,
`lint_deps` gave this result in the two trees:

```
check-lint-deps: all 94 lint steps depend on their objects and not on headers, and every file of the repository compiled here is linted or one of the 2 exceptions
```

**The mutation pass**, 2026-10-20, in `build/debug`, in 63 s: 8 mutants,
8 caught, 0 survived, 0 invalid. `lint_deps_self_test` caught each of
them. The record names the commit `85535a3`. After the pass,
`scripts/mutants-due.py build/debug` gives 42 of 43 mutant files as due;
`m1-135.json` is the file that is not due. `m1-89.json` is due, as decision
661 says, and runs at phase B's gate.

**After the push**, 2026-10-20: a fetch showed `origin/master` at
`1c4c502`. The worktree of this task went to the Recycle Bin,
`git worktree prune` removed its record, and the local branch
`claude/suspicious-mendel-1c1f44` was deleted (decision 675). The worktree
of M1-136 stays.

## Done when

- [x] The questions are ruled and recorded (660-671).
- [x] The self-test's new cases are seen to fail before the check exists.
- [x] The check names `tests/ReadRecordListener.cpp`, and nothing else, in a
      tree that compiles it unlinted -- `build/release`, which the owner
      accepted for the main checkout's Debug tree, reconfigured first.
- [x] The planted file is caught, accepted as an exception, and caught again
      as a stale one.
- [x] All eight mutants caught in a dry run on a scratch copy of the script.
- [x] `check` passes in the two trees of the main checkout, on `master`
      (decision 673).
- [x] The mutation pass has run and its record is committed.
- [x] Pushed on the word of the owner, 2026-10-20, up to `1c4c502`
      (decision 675).
