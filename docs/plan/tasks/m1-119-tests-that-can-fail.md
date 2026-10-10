# M1-119 — Tests that can fail for what they claim

Phase: B | Status: not started
Prerequisites: M1-118
Decided by: register decisions 442 and 450, ruled 2026-10-10 on the review's
findings 3.2-3.11 ([the findings](../../review/m1-116-findings.md))

## Purpose

The review planted the fault each of these tests names, and the suite still
passed -- or showed by reading that it could. Each fix is shown catching its
fault before it is committed.

## What to do

- **3.2** the back-lit patch drawn smaller than the frame, so a lit edge
  against an unlit border shows it was drawn (decision 442); the comment and
  `87af028`'s claim corrected.
- **3.3** an EXR taller than 16 rows and not a multiple of 16, read back pixel
  for pixel.
- **3.4** the storage pointer taken before the first fill.
- **3.5** NaN starts for right ascension and tilt; the replay case replays
  from a real copy taken half way.
- **3.6** one helper for worst-error sweeps that keeps a NaN as the worst,
  used at every site the review listed.
- **3.7** the determinism case requires every step to succeed, compares
  `slr`, and says "within a run" -- or runs twice in two processes.
- **3.8** every sampler reads its engine directly (approved, decision 442),
  and every sweep carries its worst case's parameters to the assertion
  (VERIFICATION.md rule 12). The cases drawn change: say so in the commit.
- **3.9** a grey grid line sampled in grid-400km, away from the coloured
  ones.
- **3.10** the 60-digit orbit references' generator committed under
  `scripts/`, if it survives on a machine; otherwise rewritten and the
  references re-derived, measured against the committed values.
- **3.11** each vacuous assertion tightened or removed, each name made true,
  `bitsOf` where a double is compared bit for bit, and the copied probe
  helpers in one place.

## Mutants

The review's planted faults, kept: every EXR chunk from row 0; no
`reserve`; `isFiniteStart` without right ascension, and without tilt;
nothing drawn for the back-lit probe -- each now caught. Plus one NaN mutant
per worst-error sweep fixed.

## Done when

- [ ] Every finding above fixed, and each planted fault seen caught.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.
