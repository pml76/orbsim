# M1-104 — The mutation harness stops if a restore fails

Phase: A | Status: **done, 2026-09-30** -- its mutation pass is the full pass that follows
Prerequisites: M1-95
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 248 and 252

## Purpose

**A mutant stayed in the source, and the next mutant files ran on top of it.**
On 2026-09-30 the full pass after M1-103 was stopped by Claude Code for want of
memory. Inside `m1-12.json`:

- a compile was killed, so a mutant was recorded "INVALID, does not compile"
  with no compiler output at all;
- the next rebuild was killed the same way, and the harness stopped with an
  error, as designed;
- its clean-up then ran `git checkout --` to put the file back **without
  looking at the result**, and `src/core/Scalar.hpp` kept its mutant;
- the loop running the pass went on to `m1-13.json` and `m1-14.json`, which
  left their own mutants too when they were stopped.

The three files were restored by hand from the commit, and no record written
after the damage was kept: `mutate.py` refuses to record a pass while code is
uncommitted.

**Measured, 2026-09-30**, in a scratch repository: while a mutated file is held
open -- as a compiler being killed may hold it -- `git checkout --` exits with
status 255, "unable to unlink old 'a.hpp'", and the file keeps its mutant.
Released, the same checkout succeeds.

## What to do

As decided (decision 252):

- **`restore` in `scripts/mutate.py` checks what it did**: git's exit status,
  and `git status` for the files afterwards. If either says the files are not
  back, it raises, and the pass stops with the files named and the command
  that puts them back.
- **Test first**: `restore_problem` judges the two signals, and the self-test
  was seen accepting a failed checkout and a still-modified file before it
  judged them. A second case runs `restore` in a scratch repository on a file
  git does not know -- a checkout that fails the same way on every system --
  and requires it to stop; with the stop removed by hand, that case fails.
- **The loop that runs a pass** (a script outside the repository, used for
  full passes) now stops as soon as any source file is left modified after a
  file's run, and a full pass builds with two jobs rather than four.

**Mutants**: `scripts/mutants/m1-104.json`, three of them: the exit status
ignored, the files still modified ignored, and the stop taken away.

## Done when

- [x] The self-test was seen failing first on each case -- a failed checkout
      and a still-modified file "WRONG" before the judging existed, and the
      stop "WRONG" with the raise removed by hand -- and the held-open file
      was refused, "git checkout exited 255; unable to unlink old 'a.hpp'",
      then restored once released.
- [x] `check` passes in both Windows trees, 320 of 320, 2026-09-30.
- [x] **Its first pass found a defect in the self-test itself**, 2026-09-30:
      "a file still modified after the checkout is accepted" survived. Adding
      the stop case had replaced the loop that evaluates the three
      `restore_problem` cases instead of following it, so they were built and
      never checked. The loop is back; each of the two judging mutants,
      applied to a copy of `mutate.py`, now fails the self-test. The pass was
      stopped between files, since the fix makes every file due again.
- [ ] `scripts/mutants/m1-104.json` passes, in the full pass that follows.
