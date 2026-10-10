# M1-122 — The declared survivors: records corrected, the catchable ones caught

Phase: B | Status: not started
Prerequisites: M1-121
Decided by: register decisions 444 and 450, ruled 2026-10-10 on the review's
group 5 ([the findings](../../review/m1-116-findings.md))

## Purpose

Two survivors are already caught by tests written after them, one survivor's
reason is out of date, and four could be caught at some cost. The owner
ruled every record fixed and every catchable survivor caught.

## What to do

- **m1-09's divide**: `test_projection` added as its judge, recorded caught.
- **m1-19's depth write**: the grid-400km cases and its golden added as
  judges, recorded per card -- caught on the RTX A2000; the RX 7900 XTX is
  judged by M1-126.
- **m1-107's staging copy**: its reason corrected -- the lambert probes call
  `uploadBuffer` -- and the lambert judges added.
- **m1-111's refused summary write**: a test that makes the report's write
  fail while the CSV's succeeds. How is put to the owner at the start, since
  decision 420 found no way to make the disk refuse.
- **m1-109's three**: an end-to-end self-test of `mutants-due.py` on
  synthetic records and a scratch tree.
- **m1-19's tint**: a probe with a coloured tint, and its goldens per card.
- **m1-23's bare scene**: the application names the scene it ran, and
  `orbsim_smoke_bare` reads it.
- **The count**: measured from the files, and kept in `STATUS.md` alone.

## Done when

- [ ] Every mutant file above re-run, and each changed verdict recorded.
- [ ] The survivor count in `STATUS.md` measured from the files.
- [ ] `check` passes in both trees.
- [ ] This task's own mutant file has run after the commit and its record is
      committed.
