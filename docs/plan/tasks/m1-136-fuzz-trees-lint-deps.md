# M1-136 — The fuzz trees' lint steps re-run on the fuzzers' objects

Phase: B | Status: **proposed 2026-10-18; its questions not yet ruled**
Prerequisites: none of substance -- its place in the queue is question 6
Decided by: **nothing yet.** The eight questions below were put to the owner
on 2026-10-18, before any work, and the owner postponed them the same day, to
be put again by the session working on the queue at a time it chooses. **No
part of this task starts until every one of them is ruled** (working agreement
1), and the rulings go into the register,
[`../milestone-1-decisions.md`](../milestone-1-decisions.md) section 9.

## Purpose

**The CTest test `lint_deps` fails in the two trees that build the fuzzers**,
`windows-fuzz` and `linux-fuzz` (the presets that set `ORBSIM_BUILD_FUZZERS`).
Found on 2026-10-18 by M1-135, which is adding a fifth check to the same script
on a branch of its own, and outside its scope.

`lint_deps` (`scripts/check-lint-deps.py`, M1-89) makes sure that each lint
step -- one clang-tidy run over one file, which `check` re-runs only when one
of its inputs changes -- depends on every *object* (the compiled form of a
source file) that any target makes from its file, because each compile can see
different flags. In a fuzz tree the programs `fuzz_orbit` and `fuzz_time`
compile `tests/fuzz_orbit.cpp` and `tests/fuzz_time.cpp` a second time, with
the fuzzer's flags, beside the object library `orbsim_fuzz_objects` that every
tree compiles them into. The two programs are not in `orbsim_lint_targets`
(`cmake/MemoryPool.cmake`), so the lint steps for those two files do not depend
on their objects:

```
check-lint-deps: lint/tests_fuzz_orbit.cpp.ok: does not depend on the object .../windows-fuzz/cmakefiles/fuzz_orbit.dir/tests/fuzz_orbit.cpp.obj
check-lint-deps: lint/tests_fuzz_time.cpp.ok: does not depend on the object .../windows-fuzz/cmakefiles/fuzz_time.dir/tests/fuzz_time.cpp.obj
check-lint-deps: 2 problem(s) in 73 lint steps
```

No gate runs `check` or `ctest` in a fuzz tree, which is why it had not shown.

## What was measured, read only, 2026-10-18

Nothing was configured or built; each figure below was read from the trees
already in the main checkout's `build/` folder, with this branch's copy of the
script.

- **Both fuzz trees fail, and no other tree does.** `windows-fuzz`, configured
  2026-10-16: 2 problems in 73 lint steps, as above. `linux-fuzz`, configured
  2026-10-06, run under WSL: the same 2, in 66. `asan`, `windows-msvc`,
  `release`, `linux-sanitize` and `linux-gcc` all pass. `memory_pool`
  (`scripts/check-memory-pool.py`) passes in `windows-fuzz`.
- **How the two compiles differ.** From the compile database (the list CMake
  writes of every file compiled, with its exact command): the fuzz programs'
  compile adds `-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
  -fno-omit-frame-pointer`, and nothing else. From Ninja's own record of the
  headers each object read (`.ninja_deps`): one more input, clang's
  `lib/clang/23/share/asan_ignorelist.txt` -- 373 against 374 files for
  `fuzz_orbit.cpp`, 374 against 375 for `fuzz_time.cpp`.
- **The missing dependency is real.** clang-tidy 23.1.2 lints a file once per
  compile command the database holds for it, and reports what either run
  sees. Spike, in a scratch folder: a file holding `return 4242;` inside
  `#ifdef ONLY_IN_FIRST` and `return 5353;` inside `#ifdef ONLY_IN_SECOND`,
  two database entries for it, one with each `-D`, and
  `readability-magic-numbers` on. clang-tidy printed `(1/2)` and `(2/2)` and
  reported both numbers; with only the first entry, it reported 4242 alone. So
  in a fuzz tree the lint step for these two files does lint them under the
  fuzzer's flags too, and would not run again when only the fuzzer's object
  changes -- after an edit to the fuzzer's compile options, say.
- **It has changed no result yet.** Neither fuzz tree's `.ninja_log` holds a
  single lint step, both trees' `Testing/Temporary` folders are empty, and the
  fuzz recipes -- `VERIFICATION.md` rule 13, `PROJECT_STATE.md` section 2 --
  build `all` and run the fuzzers, never `lint` or `ctest`.
- Nothing under `src/` or `tests/` tests `__SANITIZE_ADDRESS__`,
  `address_sanitizer` or `__has_feature`, so our own code reads the same text
  under both commands; the standard library's headers do not.
- All 41 mutant files under `scripts/mutants/` name `build/debug` or
  `build/relwithdebinfo` as their tree; none names a fuzz tree. `m1-88.json`
  and `m1-96.json` are aimed at `cmake/MemoryPool.cmake`, which M1-131's commit
  `639059e` changed on 2026-10-18.
- M1-136 was the next free number: M1-135 the last, on its own branch, and no
  branch or worktree used 136.
- M1-135's branch changes `scripts/check-lint-deps.py`, `CMakeLists.txt`,
  `cmake/ScriptTests.cmake`, `.claude/rules/lint-config.md`, the queue, the
  register and `docs/TEST_SUITES.md` -- not `cmake/MemoryPool.cmake`. The fix
  recommended below touches none of its code files.

## The questions, each with its options, costs and a recommendation

Put to the owner on 2026-10-18 and postponed. Re-check each measurement above
before putting them again: the trees and the branches move.

**1. Must `lint_deps` pass in the fuzz trees at all, given that no gate runs `check` there?**

- **A (recommended). Yes: fix it.** A few lines; a scratch fuzz tree to see
  the test fail and then pass (question 4); `check` in both check trees. It
  keeps M1-89's claim -- each lint step re-runs when any compile of its file
  changes -- true in every preset the project ships.
- B. No: the fuzz trees named as trees where `lint_deps` does not apply, the
  test not registered when `ORBSIM_BUILD_FUZZERS` is on, with the reason
  written. Cost: a hole, and M1-135's new check -- every compiled file linted
  -- would not run there either; silencing a test is the owner's decision
  anyway.
- C. Leave it, recorded as a known gap in `STATUS.md`. Costs nothing now; it
  leaves a test that fails in two of the project's own presets.

**2. If A: how?**

- **A1 (recommended). `fuzz_orbit` and `fuzz_time` named in
  `orbsim_lint_targets` under `if(ORBSIM_BUILD_FUZZERS)`**, in
  `cmake/MemoryPool.cmake`, as the application's programs (under
  `ORBSIM_BUILD_APP`) and the coverage hook's (under `ORBSIM_COVERAGE_HOOK`)
  already are: about four lines and their comment. It changes nothing the
  fuzzers are built from, so neither fuzzer's budget run (240 s and 900 s,
  rule 13) falls due. Cost: the list stays written by hand, and an entry can
  be forgotten again in a tree nothing checks (question 3).
- A2. `orbsim_lint_targets` worked out from every library and program the top
  directory has defined at that point, as `orbsim_pool_top_level_compiles`
  already does for the memory pool, so that no target can be left off again
  ("prefer the bug you cannot write", VERIFICATION.md rule 24). About ten
  lines in place of the list. Cost: it changes every tree's lint wiring
  (`lint_deps` passing in the other seven trees says the result there is the
  same), it removes the lines M1-131 added to the list this week while M1-131
  is still working in that block, and it is beyond the scope asked. It could
  be a task of its own after M1-131.
- A3. Each fuzz source compiled once in a fuzz tree: the object library given
  the fuzzer's flags there, and the two programs built from its objects. Cost:
  it changes how the fuzzers are built, so both budget runs fall due, and it is
  more CMake than A1.

**3. Keeping it from breaking again unseen: nothing runs `lint_deps` in a fuzz tree.**

- **A (recommended). The two fuzz recipes gain one line after the build**:
  `ctest --test-dir build/windows-fuzz -R "^lint_deps$"` in `VERIFICATION.md`
  rule 13, and the same for `build/linux-fuzz` in `PROJECT_STATE.md` section 2.
  About a second each time, and it carries M1-135's new check into the fuzz
  trees too. Cost: someone fuzzing can be stopped by a lint-wiring fault that
  has nothing to do with their change -- which is the point of it.
- B. Nothing more: the fix seen to work once, in this task.
- (A2 of question 2 would also answer this one.)

**4. How the fix is proved (test first: VERIFICATION.md rules 1 and 23)**

- **A (recommended). A scratch `windows-fuzz` tree in the task's worktree,
  configured but not built**: `lint_deps` reads only what the configure step
  writes, so none of the project is compiled. `lint_deps` run there before the
  change must fail, naming exactly the two objects; after it, it must pass;
  and `ninja -t inputs` on the two lint steps must list both objects -- a
  second reading of the same fact. Cost: cloning about 300 MB of dependencies
  (mp-units, ERFA, stb, lodepng, OpenEXR, Imath, Catch2 -- measured from the
  main checkout's fuzz tree), and a few minutes of light processor work
  (estimated, not measured). Only on the owner's word, never beside another
  session's measurements, and removed after the merge.
- B. As A, and the same in a scratch `linux-fuzz` tree under WSL: a few
  minutes more. The new lines carry no platform condition, and both trees
  failed the same way for the same reason.
- C. No scratch tree: the "before" taken from the main checkout's old trees
  and the "after" argued. Not a proof.

**5. Mutants (VERIFICATION.md rule 19: a mutant is a copy of the code broken on purpose, run to see that a test catches it)**

- **A (recommended). No mutant file of its own**, with the reason recorded:
  the new lines run only in fuzz trees, and every mutant file and the harness
  work in the two check trees (all 41, measured). The before-and-after of
  question 4 is the failure seen, written here and in the register.
  `m1-88.json` and `m1-96.json`, aimed at `cmake/MemoryPool.cmake`, are due
  at phase B's gate either way, since M1-131's commit changed that file
  (decision 276).
- B. A mutant file of its own naming the fuzz tree, with one mutant dropping a
  program from the list, judged by `lint_deps` there: the first mutant file in
  a tree other than a check tree, untried in `scripts/mutate.py` and
  `scripts/mutants-due.py`, and due at every gate where
  `cmake/MemoryPool.cmake` changes, needing a configured fuzz tree then.
- C. A mutant file in a check tree: its mutant cannot be reached there, so it
  survives, declared.

**6. Its branch, the trees its `check` runs in, and its place**

- **A (recommended). A branch of its own from `master`, and two check trees
  of its own** in its worktree, as decision 669 gave M1-135: about 2 GB each,
  about 1 to 1.5 hours of machine time for both (669's estimate, not
  measured), on the owner's word and not beside other sessions' builds or
  measurements. In the queue right after M1-135 and merged after it: the
  queue's count line, the end of the register and `STATUS.md` will conflict
  where the second branch meets the first, resolved by hand.
- B. On top of M1-135's branch: no document conflicts, and `check` could run
  in M1-135's trees once M1-135's own passes -- minutes rather than an hour.
  Cost: M1-136 waits for M1-135, follows each of its moves (on 2026-10-18 its
  tip was a commit to be amended, and it moves onto `master` after M1-131's
  commit), and M1-135's worktree would have to check out this task's branch.

**7. Register numbers**

On 2026-10-18 decision 670, on M1-135's branch, gave M1-135 decisions 660-679
and left M1-131 652-659, M1-131 to go on after 679 should it need more.

- **A (recommended). 680-689 for M1-136**, the next block after M1-135's, and
  M1-131, should it run past 659, going on after 689 instead -- which amends
  decision 670, so its "Amended by" note goes on that row. The sessions
  working on M1-131 and M1-135 are told, as they were for 670.
- B. 690-699, leaving 680-689 to M1-131 -- which also amends 670, since
  M1-131 must then skip 690-699.

Until this is ruled, nothing should take a number from 680 up. Measured
2026-10-18: no branch or worktree had a row above 671.

**8. Smaller choices**

- (a) This document, its title and file name as they are; phase B; no
  prerequisite under 6A, M1-135 under 6B.
- (b) Two commits after the rulings: the rulings themselves (the register's
  rows, this document, the queue), then the fix with its documents and
  `STATUS.md`.
- (c) The documents the fix changes: the comment in `cmake/MemoryPool.cmake`,
  this document, the queue, `STATUS.md`, and under 3A the two recipes.
  `.claude/rules/lint-config.md`, `docs/TEST_SUITES.md` and `docs/BRIEFING.md`
  stay as they are: no new test, and no rule changed.
- (d) Commits name the model that wrote them, with no session address.

No test, tolerance, `.clang-tidy` entry or `NOLINT` is touched by any of the
recommended options.

## Done when

Every question ruled and recorded as register rows; the fix made test first --
the failure seen before the change, the pass after it; `check` passing in both
check trees; committed on its own branch; nothing pushed without the owner's
word.
