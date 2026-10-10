# M1-124 — STATUS.md's open items (a) to (d)

Phase: B | Status: not started
Prerequisites: M1-123
Decided by: register decisions 447 and 450, ruled 2026-10-10 on the review's
group 8 ([the findings](../../review/m1-116-findings.md))

## Purpose

Four items `STATUS.md` has carried since the phase A gate as known and not
fixed. The owner ruled each fixed; item (e) is M1-126, on the second machine.

## What to do

- **(a)** a test that starts the application without writing a sidecar --
  the smoke tests, the benchmark, golden acceptance -- records the shader
  files it read, so `mutants-due.py` stops counting every shader as read
  (decision 394's gap).
- **(b)** the 400 km jitter sequence kept, and its name and comment say what
  it checks: that five frames from 400 km stay put. The narrowing defect is
  the 1 km sequence's to catch (decision 321), and physics keeps the 400 km
  one from seeing it (decision 447).
- **(c)** every fuzzer run with a fixed `-seed`, and every input that adds
  coverage kept in the committed corpus, so each run starts from the best
  coverage so far and repeats exactly. Coverage measured before and after,
  several runs each.
- **(d)** `check`'s other steps wait for `configure-current`, so a refused
  configure stops the build at once; the cost, measured as about half a
  second a run, measured again.

## Done when

- [ ] Each item fixed, and its row in `STATUS.md` closed with the
      measurement.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.
