# M1-97 — The harness rebuilds only what the next mutant does not

Phase: A | Status: **in progress, 2026-09-28**
Prerequisites: M1-95
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 224 and 226

## Purpose

**M1-95 made mutation passes about three times slower.** It rebuilds the
previous mutant's programs after restoring its file, and the next mutant then
builds its own. Consecutive mutants usually share their programs, so most of
them were built twice. Measured on 2026-09-28 across the 17 mutant files, that
was 191 of the 225 rebuilds M1-95 added.

## What to do

- After restoring, rebuild the previous mutant's targets **less those the next
  mutant builds itself**. Its own build rebuilds them from the restored file
  anyway.
- **Put the rule in one function**, `rebuild_plan` in `scripts/mutate.py`,
  with one helper, `built_by`, which both the plan and the mutant's own build
  use, so the two cannot drift apart. The function's docstring gives the
  argument that nothing is left stale.
- **Prove it three ways:**
  - `mutate.py --self-test`, as the CTest test `mutate_self_test`;
  - `scripts/mutants/m1-97.json`, each mutant breaking the plan one way;
  - `m1-95.json` kept as the end-to-end regression test. The self-test cannot
    see the main loop, so a loop that ignores the plan is caught only there.
    It is declared as a survivor in `m1-97.json`, with that reason.
- **Measure it.** Time `m1-09.json` before and after, under the same
  conditions.

## Done when

- [x] `mutate_self_test` passes, and each caught mutant of `m1-97.json` was
      seen failing it before the pass ran. Checked on 2026-09-28 without a
      build: 5 caught, and the declared survivor survives.
- [x] `m1-09.json` timed before and after, back to back on the same tree,
      at full parallelism, 2026-09-28: **801 s before, 332 s after**, 2.4 times
      faster. Every verdict was the same: 14 caught, 1 declared survivor.
      Two kills listed the same failing cases in a different order.
- [ ] `m1-95.json` still passes: 1 caught, 1 declared survivor.
- [x] `check` passes in both trees, 283 of 283, 2026-09-28.
- [ ] One full mutation rerun after M1-97 and M1-98 (decision 226).
