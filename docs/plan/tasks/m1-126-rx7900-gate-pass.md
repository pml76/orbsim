# M1-126 — The gate's pass on the RX 7900 XTX

Phase: B | Status: not started -- **on the second machine, whenever the owner
is next at it; it blocks no other task**
Prerequisites: M1-116
Decided by: register decisions 447 (e) and 450, ruled 2026-10-10

## Purpose

`STATUS.md`'s open item (e): every mutant file judged on a graphics card
records the RTX A2000, so each is due on the RX 7900 XTX (decision 397), and
that card has not run the phase A gate's pass. The review added one more
reason: m1-19's depth-write mutant is caught on the RTX A2000 and has not been
judged on the RX 7900 XTX.

## What to do

On the second machine, from a clean `check` in both trees:

- `python scripts/mutants-due.py build/<tree>` lists what is due on that card;
  run every listed file, one at a time (the "for whoever runs a long job"
  notes in `STATUS.md`).
- Record each verdict per card, as the files already do for Bresenham's rule.

## Done when

- [ ] Every file due on the RX 7900 XTX has run, and its record is committed.
- [ ] `STATUS.md`'s item (e) closed.
