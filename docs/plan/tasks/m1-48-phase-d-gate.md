# M1-48 — Phase D gate

Phase: D | Status: not started
Prerequisites: M1-39 … M1-47
Decided by: [ADR 0005](../../adr/0005-correctness-is-enforced-by-tools.md)

## Purpose

Phase D added the project's first compute shaders, a CPU reference model that
several budgets depend on, and six golden images. The compute work in particular
is a new class of risk: a race in a compute shader produces a table that is
*nearly* right, and nearly right is the failure mode this project cares most
about.

## What to do

The full sweep from M1-38 — ASan, Linux clang with ASan and UBSan, gcc-14, TSan,
and the three fuzzers — plus what phase D introduces:

- **GPU-assisted validation**, enabled through `vkconfig` for one manual run.
  The base validation layers do not check descriptor indexing bounds at runtime;
  the compute passes index tables from shader code, and this is the layer that
  catches a read past the end of one.
- **Synchronization validation** for one manual run, which by now covers four
  compute dispatches feeding a graphics pass — the project's densest set of
  barriers.
- **Determinism across all six probes**, re-run: byte-identical HDR dumps.
- **The CPU reference model's coverage** examined specifically. It is the oracle
  behind five budgets, and an untested branch in an oracle is worse than an
  untested branch anywhere else.

## What to check, beyond "it passed"

- **gcc-14 and clang agree on the reference model.** It is dense floating-point
  code of exactly the kind that has already produced a cross-compiler
  disagreement once in this project, and the answer that time was a real
  instability, not a tolerance.
- **The budgets are all still met at High** after any change made during the
  phase, and the recorded numbers in each commit message match what the suite
  reports now.
- **Every golden has an approval date** recorded, and none was re-baselined
  without one.

## What to record

In `PROJECT_STATE.md`: assertion counts per toolchain; the atmosphere budgets as
measured, per table, maximum and p99; the frame-time table from M1-47; the
coverage table; and the six goldens with their approval dates.

In `VERIFICATION.md` Part 4: rule 14, differential testing, can now cite a
second pair — GPU tables against the CPU reference — alongside the two
propagators.

## Done when

- [ ] Every toolchain passes, counts matching.
- [ ] Three fuzzers clean.
- [ ] GPU-assisted and synchronization validation both run clean by hand.
- [ ] All six probes are deterministic.
- [ ] `PROJECT_STATE.md` is current, and phase C starts with a known frame-time
      headroom rather than a hope.
