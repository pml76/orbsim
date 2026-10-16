# M1-124 — STATUS.md's open items (b) to (d)

*(Item (a) moved to M1-129 on 2026-10-12, register decision 495, and on to M1-131 on 2026-10-16, decision 601.)*

Phase: B | Status: not started
Prerequisites: M1-123
Decided by: register decisions 447 and 450, ruled 2026-10-10 on the review's
group 8 ([the findings](../../review/m1-116-findings.md))

## Purpose

Four items `STATUS.md` has carried since the phase A gate as known and not
fixed. The owner ruled each fixed; item (e) is M1-126, on the second machine.

## What to do

- ~~**(a)**~~ the shader files a test read: moved to
  [M1-129](m1-129-tests-by-fingerprint.md), whose tests record every file
  they read (decisions 489 and 495), and on to
  [M1-131](m1-131-coverage-hook.md), which names the test each process
  belongs to (decision 601).
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
