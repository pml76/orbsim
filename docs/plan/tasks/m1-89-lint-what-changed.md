# M1-89 — Re-lint only what changed

Phase: A | Status: **done, 2026-09-27**
Prerequisites: M1-88
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 209 and 217

## Purpose

**Today any header edit re-lints all 48 files: 206–313 s per tree.** That is
more than the compile, and over half of what a `check` after a header edit
costs
([`../../measurements/verification-cost.md`](../../measurements/verification-cost.md)).
Each lint step depends on every project header, where it only needs the ones
its file includes.

## What to do

*(Amended before any code, 2026-09-27, register decision 217: the wrapper
script and depfile first written here were replaced by a mechanism with the
same precision and no new script.)*

- **Each lint step depends on its own file's compiled object**, in every target
  that compiles the file. CMake gives the object path through
  `$<TARGET_OBJECTS:...>`, filtered to the file.
  - Ninja records every header the compiler reports for each object -- 377 for
    `src/view/Camera.cpp`, system headers included, measured -- and rebuilds
    the object exactly when one changes.
  - A linted file that no target compiles stops the configure step with an
    error, so it cannot quietly lose its dependency.
- **The lint step's inputs** become:
  - its source;
  - its objects;
  - the `.clang-tidy` configurations;
  - the verify stamps;
  - the clang-tidy executable;
  - in place of "every header".
- **`scripts/check-lint-deps.py`**, as the CTest test `lint_deps`, holds all of
  it against the generated Ninja file and the compile database, and refuses
  `__clang_analyzer__` under `src/` and `tests/`, the one way clang-tidy could
  read text the compiler does not. Its self-test is `lint_deps_self_test`.

## Done when

- [x] `lint_deps` failed on the tree before the change, for the right reason:
  - 70 objects missing, 48 steps listing project headers directly, and 48
    without the clang-tidy executable;
  - 166 problems in 48 steps.
- [x] **Planted edits, by real lint runs.** Ninja's dry run stops at
      "Re-running CMake" on this tree, so it cannot answer the question. Each
      set re-linted was compared with the files whose recorded dependencies
      hold the edit, and was **identical**:

  | Edit | Re-linted | Before |
  |---|---|---|
  | `src/view/Camera.hpp` | 11 | 48 |
  | `src/core/Units.hpp` | 32 | 48 |
  | `tests/OrbitTestSupport.hpp` | 9 | 48 |
  | `src/view/Camera.cpp` | 1 | 1 |
  | `.clang-tidy` | 48 | 48 |

- [x] **A lint finding planted in `src/view/Camera.hpp` was caught through each
      of its 11 includers**, and through nothing else.
  - The finding was a local variable never changed but not `const`
    (`misc-const-correctness`).
  - A first attempt used a one-letter name. That check is one of the four
    suppressed in `.clang-tidy`, so it found nothing, which was the wrong
    instrument rather than a hole.
- [x] The self-test reports each of the eleven faults it is given.
- [ ] A mutation pass on the check, `scripts/mutants/m1-89.json`, run after
      this task's commit, since the harness refuses a file with uncommitted
      changes.
- [x] `check` passes in both trees.
