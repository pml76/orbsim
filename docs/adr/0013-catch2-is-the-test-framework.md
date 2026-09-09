# ADR 0013: Catch2 is the test framework

Status: accepted (2026-09-08; recorded 2026-09-09)

Decision 6 of [the milestone 1 register](../plan/milestone-1-decisions.md),
carried out by [M1-01](../plan/tasks/m1-01-catch2.md) on 2026-09-09.

## Decision

- **Catch2 v3**, pinned by tag through `FetchContent_Declare(... SYSTEM)`
  exactly as every other dependency is -- so the build is reproducible and the
  dependency is exempt from this project's warning set, which is the whole
  reason `SYSTEM` is there.
- **`catch_discover_tests`**, so each `TEST_CASE` becomes its own CTest test
  and `ctest -R` selects one. `ctest -N` therefore lists the cases rather than
  the binaries.
- **Both existing suites moved in the first task of the milestone**, before any
  of the roughly ten new ones are written, so there is never more than one
  harness in the tree.
- **The matchers take this project's strong types.** Catch2's own
  `WithinAbs(target, margin)` takes two bare doubles and accepts them
  transposed -- `WithinAbs(5.0, 5554.0)` passes for 5554 exactly as
  `WithinAbs(5554.0, 5.0)` does -- which is the silent transposition
  non-negotiable 1 exists to prevent. `WithinAbsOf`, `WithinRelTo` and
  `WithinRelVec` in `tests/OrbitTestSupport.hpp` take a `Tolerance`, so the
  transposed call does not compile, and they reproduce the old harness's
  predicates exactly rather than approximately: Catch2's `WithinRel` divides by
  `max(|got|, |want|)` where this project divides by `|want|`.
- **The evidence that the move changed nothing is the assertion count**, equal
  before and after in both Windows trees, under ASan, and under both Linux
  presets. A refactor of the harness is exactly the change that can quietly
  stop running a test, and the count is the only thing that would show it.

## What we considered

**Keeping the hand-rolled harness.** It was the right call for one file, and
[`../../CODING_GUIDELINES.md`](../../CODING_GUIDELINES.md) section 17 says so
in as many words: switch at three or four suites, "then, not before".
Milestone 1 adds roughly ten. Against keeping it: no filtering, no tagging, no
useful failure output, and no per-case CTest entry -- so one failing case means
rerunning a whole binary and reading its output. Every new suite would pay for
those absences again.

**doctest.** Substantially cheaper to compile, close in shape, and a reasonable
answer to the same problem. Catch2 was taken for the matcher vocabulary, which
is what this project's strong types plug into: `WithinAbs` and `WithinRel` are
the shape `checkNear` and `checkRel` already had, so the move was a rename plus
a wrapper rather than a redesign of every assertion in the suite.

## Why

One harness in the tree at a time is most of it. Two would mean two ways to
write a test, two failure formats, and a decision to make every time a suite is
added -- and the cost of moving grows with the number of suites, so the
cheapest moment to move was before the milestone rather than during it.

Two things the move turned up are worth keeping in the record, because both are
the kind of defect that reads as correct:

- **`INFO(describe(result.error()))` is undefined behaviour.**
  `std::expected::error()` has the precondition that the expected holds no
  value, and `INFO` evaluates its argument eagerly on every call rather than
  only on failure. The Debug tree asserts. A guarded `errorName()` that returns
  "(succeeded)" is the fix.
- **A test framework's own helpers are not exempt from non-negotiable 1.** The
  transposable `WithinAbs` above would have accepted a wrong value silently, in
  the tests, which is the one place a defect has nothing behind it to catch it.

## What this record does not decide

- **Whether Catch2's benchmarking facility is used.** The worked example has
  its own benchmark; nothing in `src/` is benchmarked through Catch2 yet.
- **Whether `GENERATE` and Catch2's generators are adopted** for the seeded
  sweeps. They are not today: the sweeps carry their own seeded generator so
  that the seed is written down in the file
  ([`../VERIFICATION.md`](../VERIFICATION.md) rule 12), and Catch2's own
  `Randomness seeded to:` line seeds neither.
- **How GPU probes are structured**, beyond a CTest fixture feeding an ordinary
  Catch2 test that links no Vulkan -- that is
  [`0008`](0008-renderer-verification.md).
