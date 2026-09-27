# M1-89 — Re-lint only what changed

Phase: A | Status: planned
Prerequisites: M1-88
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 209

## Purpose

**Today any header edit re-lints all 48 files: 206–313 s per tree.** That is
more than the compile, and over half of what a `check` after a header edit
costs
([`../../measurements/verification-cost.md`](../../measurements/verification-cost.md)).
Each lint step depends on every project header, where it only needs the ones
its file includes.

## What to do

- **A wrapper for each lint step**, `scripts/lint-one.py`:
  - it reads the file's entry in `compile_commands.json`, and refuses if there
    is not exactly one;
  - it runs the compiler in dependency-listing mode (`-M`) with those exact
    flags, which lists every header the file includes, system headers too;
  - it runs clang-tidy;
  - on success it writes the stamp and a Ninja depfile. A *depfile* is the
    list of files a step depends on, which Ninja reads back after the step
    has run.
- **The lint step's inputs** become:
  - its source;
  - the `.clang-tidy` configurations;
  - the verify stamps;
  - the clang-tidy executable;
  - its depfile, in place of "every header".
- **A grep check** in `check` refuses any include guarded by
  `__clang_analyzer__`, the one way clang-tidy could read text the compiler
  does not.
- **A self-test for the wrapper**, as a CTest test: on a synthetic source and
  header, the depfile must name the header, and a failed clang-tidy must leave
  no stamp.

## Done when

- [ ] The self-test was seen failing first, against a wrapper that writes no
      depfile.
- [ ] **Planted edits**, each compared with the files that actually include
      the edit (from the compile database):
  - a leaf header re-lints exactly its includers;
  - a core header re-lints what includes it;
  - a source file re-lints only itself;
  - `.clang-tidy` re-lints everything.
- [ ] A lint finding planted in a header is caught through each of its
      includers.
- [ ] A mutation pass on the wrapper, `scripts/mutants/m1-89.json`.
- [ ] `check` passes in both trees.
