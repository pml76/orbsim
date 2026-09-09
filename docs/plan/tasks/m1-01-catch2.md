# M1-01 — Move both suites to Catch2

Phase: preliminaries | Status: **done 2026-09-09**
Prerequisites: none — this is the head of the queue

## Purpose

The hand-rolled harness was the right call for one file and is the wrong call
for ten. `CODING_GUIDELINES.md` section 17 says to switch "at three or four
files… then, not before", and this milestone adds roughly eight more suites.
Moving now means every task after this one is written the same way; moving later
means writing eight suites twice.

This task changes **no assertion, no tolerance and no case**. It is a pure
refactor whose evidence is that the numbers do not move.

## What to implement

- Pin **Catch2 v3** in `CMakeLists.txt` via `FetchContent_Declare(... SYSTEM)`,
  by tag, alongside the four dependencies already pinned there. Licence: Boost
  Software License 1.0 — record it in `THIRD_PARTY.md` when M1-02 writes that
  file.
- `orbsim_test_support` links `Catch2::Catch2WithMain`. Individual suites stop
  declaring `main`.
- `include(Catch)` and `catch_discover_tests(<suite>)` so each `TEST_CASE`
  becomes its own CTest test, and `ctest -R` can select one.
- Convert `tests/test_orbit.cpp` and `tests/test_orbit_scales.cpp`:
  - `section("…")` → `TEST_CASE("…", "[orbit]")`, with `SECTION` for the groups
    inside a case.
  - `checkNear(run, what, got, want, tol)` →
    `REQUIRE_THAT(got, Catch::Matchers::WithinAbs(want, tol.value))`.
  - `checkRel` → `WithinRel`.
  - `checkVecRel` → a small `Catch::Matchers` custom matcher, `WithinRelVec`,
    kept in `tests/OrbitTestSupport.hpp`, because relative comparison of a
    vector by its length is this project's own definition and belongs where the
    other fixtures are.
  - `checkAngle` → `WithinAbs` on `wrapPi(got - want).value`.
  - `expectOk` → `INFO(describe(result.error()))` followed by
    `REQUIRE(result.has_value())`, which prints the named error on failure
    exactly as the old helper did.
  - The seeded sweeps keep seed `20260905` and keep printing their parameters,
    now through `CAPTURE(...)` so a failure names the case.
- Delete `tests/TestHarness.hpp`, its entry in `orbsim_test_header_selfcheck`,
  and its entry in the format and lint file lists.

## Out of scope

Adding a test. Changing a tolerance. Renaming a case for taste. Touching
anything under `src/`. Splitting a suite. All of those hide the one thing this
task is trying to show.

## Tests

The suites are the test. What makes the refactor checkable is the invariant:

- **The assertion count is unchanged: 3,632.** Catch2 counts assertions, so the
  number is directly comparable with the hand-rolled harness's "checks".
- Every case that passed passes, and the same cases exist. `ctest -N` lists them.
- `test_orbit` and `test_orbit_scales` still link **only** `orbsim_core`, and
  still include no Vulkan or SDL header.

## Verification

The standing rules, plus — because the harness is what every later number is
reported through — the Linux presets are run in this task rather than waiting
for the phase gate:

```
wsl -d Ubuntu -u root -- bash -c "cd /mnt/c/Users/U439644/Projects/untitled && \
    cmake --preset linux-sanitize && cmake --build build/linux-sanitize && \
    ctest --test-dir build/linux-sanitize --output-on-failure"
wsl -d Ubuntu -u root -- bash -c "… --preset linux-gcc …"
```

Both must report the same 3,632.

## A decision this task may surface

Catch2's `TEST_CASE` registers a file-scope object, which
`cppcoreguidelines-avoid-non-const-global-variables` may report — and
`WarningsAsErrors: '*'` makes a report an error. If it fires, **stop and ask the
owner**: the choice is a `NOLINT` with a reason at each `TEST_CASE`, or one
entry in `.clang-tidy`, and working agreement 7 makes that the owner's call, not
the implementer's. Do not silence it and carry on.

## Done when

- [ ] `check` is green in `build/relwithdebinfo` and `build/debug`.
- [ ] 3,632 assertions, 0 failures, in both Windows trees and both Linux presets.
- [ ] `tests/TestHarness.hpp` no longer exists, and nothing references it.
- [ ] `ctest -N` shows one entry per `TEST_CASE`.
- [ ] The commit message records the before and after assertion counts.
