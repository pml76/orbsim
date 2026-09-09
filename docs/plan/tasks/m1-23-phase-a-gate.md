# M1-23 — Phase A gate

Phase: A | Status: not started
Prerequisites: M1-03 … M1-22
Decided by: [ADR 0005](../../adr/0005-correctness-is-enforced-by-tools.md)

## Purpose

Phase A added a time system, an astronomy module, a second library, a
radiometric pipeline and the project's first drawing. `check` has been green
throughout, but `check` runs one compiler on one platform with no sanitizer.
This is where the tools that find what `check` cannot get run.

There is no CI here by decision (ADR 0005), so this is not a formality that a
machine performs — it is the deliberate act that replaces one.

## What to do

Run all of it, and write the numbers down.

```
cmake --preset asan && cmake --build build/asan && ctest --test-dir build/asan --output-on-failure

wsl -d Ubuntu -u root -- bash -c "cd /mnt/c/Users/U439644/Projects/untitled && \
    cmake --preset linux-sanitize && cmake --build build/linux-sanitize && \
    ctest --test-dir build/linux-sanitize --output-on-failure"

wsl -d Ubuntu -u root -- bash -c "… --preset linux-gcc … && ctest --test-dir build/linux-gcc …"

wsl -d Ubuntu -u root -- bash -c "… --preset linux-fuzz … && ./build/linux-fuzz/fuzz_orbit -max_total_time=240"
```

Then the coverage run from `VERIFICATION.md` rule 18, and **read the uncovered
lines** — that list is the map of what no test has executed, and phase A added a
great deal of new code to it.

## What to check, beyond "it passed"

- **The assertion counts match across all four toolchains.** They did before
  phase A (3,632 everywhere); they must again, at the new total. A mismatch
  means a test is compiled out somewhere, which is worse than a failure because
  it looks like success.
- **The sanitizers are actually linked**, not merely configured — the check
  `PROJECT_STATE.md` section 6.3 describes, because a green run under a
  sanitizer that was never enabled is the exact failure mode the clang-tidy
  header filter had for sixteen commits.
- **gcc-14 sees the new code.** It has already found two defects Windows clang
  could not — an unstable solver and a missing `<tuple>` — and phase A adds a
  time system full of arithmetic and a new library.
- **Coverage of `src/core/Time.hpp`, `src/astro/` and `src/view/`** is examined
  line by line for anything never executed.

## What to record

In `PROJECT_STATE.md`:

- assertion counts per toolchain, and the total;
- the coverage table, extended with the new files;
- the phase A benchmark baseline from M1-22;
- the model errors now standing: nutation, ΔUT1, and the solar formula;
- every golden image now committed, and the date each was approved.

And in `VERIFICATION.md` Part 4: rule 3 moves off **to build** — external truth
now reaches both the physics and the renderer.

## Done when

- [ ] All four toolchains pass, with matching assertion counts.
- [ ] The fuzzer runs four minutes clean.
- [ ] Coverage is measured and the uncovered lines have been read, not just
      counted.
- [ ] `PROJECT_STATE.md` describes the tree as it now is.
- [ ] Nothing is carried into phase B on the understanding that it will be
      fixed later. If something is, it is a task, and it goes in the queue.
