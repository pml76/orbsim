# M1-93 — Fuzz corpora and per-target budgets

Phase: A | Status: planned
Prerequisites: none
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 213

## Purpose

**Every fuzz run starts from nothing**, so each gate spends its budget
re-finding what the last one found. And one budget does not fit both targets:

- **`fuzz_orbit`** found everything within 4 minutes, and nothing in the next
  26;
- **`fuzz_time`** was still finding new paths at 240 s, and stopped at about
  900 s.

See [`../../measurements/verification-cost.md`](../../measurements/verification-cost.md).

## What to do

- Commit a corpus per target under `tests/corpus/<target>/`: seeded by a run
  at the target's budget, then trimmed with libFuzzer's `-merge=1`, which
  keeps only inputs that add coverage.
- The budgets:
  - `fuzz_orbit` 240 s;
  - `fuzz_time` 900 s;
  - each re-measured when its target changes.
- The command becomes
  `fuzz_<target>.exe tests/corpus/<target> -max_total_time=<budget>`: in every
  gate document, in `VERIFICATION.md` rule 13, and in the rule that a task
  which changes code a fuzzer reaches runs it before its commit.

## Done when

- [ ] Both corpora committed and trimmed.
- [ ] With the same budget, several runs from the corpus reach at least the
      coverage of runs from empty.
- [ ] Every gate document and rule 13 name the corpus and the budgets.
- [ ] `check` passes in both trees.
