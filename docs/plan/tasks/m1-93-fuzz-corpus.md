# M1-93 — Fuzz corpora and per-target budgets

Phase: A | Status: **done, 2026-09-27**
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

- [x] **Both corpora are committed and trimmed.** Each was seeded by a run from
      empty at its budget, trimmed with `-merge=1`, then grown by a run from
      the corpus and trimmed again:
  - `fuzz_orbit`: 29 inputs, 410 features;
  - `fuzz_time`: 70 inputs, 1,280 features;
  - 147 KB together.

  `.gitattributes` marks them binary, so that no line-ending rule can rewrite
  an input.
- [~] **Runs from the corpus against runs from empty: mixed, recorded as
      measured.**

  | Target, budget | From empty, today | From empty, morning | From the corpus |
  |---|---|---|---|
  | `fuzz_orbit`, 240 s | 404 | 411 | 410 |
  | `fuzz_time`, 900 s | 1,273 | 1,453 | 1,280 |

  - A run from the corpus has its full starting coverage within the first
    second, and ended above today's runs from empty.
  - It did **not** reach this morning's lucky `fuzz_time` run, 1,453
    features, whose inputs were lost because that run had no corpus folder.
  - `fuzz_time`'s coverage varies a great deal from one run to the next, which
    is the case for a corpus that accumulates: from now on every gate adds
    what it finds instead of discarding it.
- [x] Every gate document and rule 13 name the corpus and the budgets, and the
      four tasks that add fuzz targets say how to set theirs.
- [x] `check` passes in both trees.
