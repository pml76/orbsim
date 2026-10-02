# M1-105 — Give the mutated parts of the build their own files

Phase: A | Status: **done, 2026-10-01**
Prerequisites: M1-103
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 254

## Purpose

**An edit anywhere in `CMakeLists.txt` made seven mutant files due**, about 30
minutes, whatever the edit was. Since M1-103 a build-definition change makes a
file due only through what it changed in that file's judges -- but a mutant
file whose mutants *mutate* `CMakeLists.txt` itself is due whenever that file
changes, by the rule that a changed mutated file is always a reason (decision
212). Ten mutants in seven files mutate it: `m1-88`, `m1-90`, `m1-95`,
`m1-96`, `m1-98`, `m1-100` and `m1-102`. Measured on the real tree on
2026-09-30, a comment added to `CMakeLists.txt` made exactly those seven due.

The owner's idea, approved on 2026-10-01: move the parts those mutants aim at
into files of their own under `cmake/`.

## What to do

As decided (decision 254):

- **Three blocks move, their text unchanged**, each included where it stood,
  in the same directory scope -- `include()` makes no new scope, so every
  variable and target is the top-level file's as before:
  - `cmake/TestSuites.cmake`: the abort-listener library and the loop that
    makes each suite (`m1-90`, `m1-95`);
  - `cmake/MemoryPool.cmake`: the pool's size and the lint steps that run in
    it (`m1-88`), and the loop that puts this project's compiles into it
    (`m1-96`) -- which has to run once every target exists, so it becomes a
    function there that `CMakeLists.txt` calls last, where the loop stood;
  - `cmake/ScriptTests.cmake`: the CTest tests of this project's own scripts
    and build, from `mutate_self_test` to `lint_deps` (`m1-100`, `m1-102`).
- **`m1-98`'s three mutants stay**: the configure guard has to fingerprint
  `CMakeLists.txt` from inside it, and record at its end, and `check`'s own
  dependency list is there. A `CMakeLists.txt` edit should then make `m1-98`
  alone due.
- **Each new file is registered with the configure guard** (M1-98) before it
  is included, as `cmake/GccWarnings.cmake` is.
- **The seven moved mutants are re-pointed** to their new files -- the `file`
  field only; their search and replacement text is untouched, one changed line
  each.
- **The proof that nothing changed**: each tree's generated `build.ninja`,
  `rules.ninja` and CTest's test definitions, before and after, in both Windows
  trees and the core-only `linux-sanitize` tree, so that both sides of every
  `if(ORBSIM_BUILD_APP)` are covered; and every judge's fingerprint, which
  `mutants-due.py` compares by itself.

## Done when

- [x] Before and after, in all three trees, 2026-10-01: CTest's test
      definitions (320, 320 and 299, without the file and line each was
      declared at) and `rules.ninja` identical, and `build.ninja` differing on
      exactly two lines -- the two that make CMake run again, which gained the
      three new files, lost none, and kept everything else in order.
- [x] The configure guard records the three new files in every tree, 5 files
      in all.
- [x] `check` passes in both Windows trees, 320 of 320, 2026-10-01.
- [x] **Found on the way**: CLion configured both Windows trees on its own at
      20:35-20:36, while this task's configure ran, and all three runs failed
      -- this one in a Ninja log compaction that collided. Run again alone it
      succeeded, and the comparison above is from that run. CLion's automatic
      reload is still partly on on this machine; STATUS asks the owner to
      switch it off.
- [x] `mutants-due.py` lists only the files the move should make due, and they
      pass, 2026-10-02 at d0d06c8: `m1-88` 7 of 7, `m1-90` 5 of 5, `m1-95` 1
      and its declared survivor, `m1-96` 4 of 4, `m1-98` 8 of 8, `m1-100` 4 of
      4, `m1-102` 4 of 4 -- once M1-106 had each file judged in its own tree;
      before it, one tree's view of the other's files over-reported. CLion was
      closed for the pass. Afterwards 0 of 26 due, asked about either tree.
- [x] Afterwards, a change to `CMakeLists.txt` alone makes `m1-98` due and no
      other file: 1 of 26, where it was 7 before this task and 25 before
      M1-103. Measured with `--assume-changed CMakeLists.txt`, the generated
      files unchanged -- which is what a comment leaves them, as the
      before-and-after comparison above showed.
