# M1-84 — Milestone gate and the record

Phase: G | Status: not started
Prerequisites: M1-01 … M1-83, and M1-85, the phase F gate, which runs between
M1-77 and M1-78 despite its number, **and M1-86**, UT1 from TT, which runs in
phase A despite its number *(added 2026-09-21: M1-85 was named here when it
took an out-of-sequence number and M1-86 was not, although this is the last
task in the milestone and its job is everything in it)*
Decided by: [ADR 0005](../../adr/0005-correctness-is-enforced-by-tools.md)

## Purpose

The last task. Everything the toolbox has, one final time, and then the
documents are brought back into agreement with the tree — which is the thing
that makes the next machine, or the next month, cheap.

## What to do

**The full sweep**, as at every gate, now at its largest:

- `check` in both Windows trees.
- The `asan` preset, all suites.
- **`windows-msvc`** (the third implementation, and the only preset that builds
  the renderer against a second compiler). *(Added 2026-09-24: it joined the
  mandated sweep in [the queue](../milestone-1-tasks.md) on 2026-09-14 and was
  never added here — in the one gate whose whole claim is "everything the
  toolbox has, one final time".)*
- `linux-sanitize` (ASan + UBSan), `linux-gcc` (the second compiler),
  `linux-tsan` (the threaded code).
- **Six fuzz targets**, ten minutes each rather than four, since this is the
  last run before the milestone is called done.
- GPU-assisted validation and synchronization validation, by hand.
- Coverage across the whole tree, with the uncovered lines read.
- The thirty-minute soak from M1-61, extended to the complete scene with the
  simulation running at 100×.

## What to bring back into agreement

This is the half that is easy to skip and expensive to have skipped.

- **`docs/PROJECT_STATE.md`** — section 1 rewritten: what is built, what each
  directory now holds, the suite list and assertion counts, the tool versions,
  and the machine specifics. Sections 3 and 5 gain the milestone's history and
  its ADRs. Section 7's open questions are re-read: several were settled during
  the milestone and should say so, and the ones that remain — `Vec3` and its
  frame, the ephemeris source, the harmonic depth — should be sharpened by what
  was learned.
- **`docs/plan/milestone-1-earth.md`** — status becomes complete, with the
  acceptance numbers from M1-83.
- **`docs/plan/realism.md`** — the items this milestone closed are marked, and
  the ordering of what remains is re-read now that the structural items are
  built rather than planned.
- **`docs/VERIFICATION.md`** Part 4 — the enforcement table updated. Rules 3
  and 15 have moved off **to build** and rule 4 off *partial* (that is the
  status it carries, not *to build*); rule 13 has six targets rather than two;
  rule 14 has three pairs. The table is the honest accounting the document
  exists for, and it is only honest if it is current.
- **`CLAUDE.md`** — the directory map gains `astro/`, `view/`, `sim/` and
  `tools/`; the current-work section points at milestone 2; the build section
  gains the presets added along the way.
- **`THIRD_PARTY.md`** — final check that every pin, licence and compiled file
  list is accurate.
- **`docs/plan/milestone-1-tasks.md`** — left as the queue it is. **Which tasks
  are done stays in [`../../STATUS.md`](../../STATUS.md), which the queue's own header
  says in those words**; this task used to ask for the queue to be annotated
  with each task's commit instead, which made three documents claim the record
  and none of them own it. If a per-task commit map is wanted as a milestone
  artefact, it belongs in the record below, not in the queue (settled
  2026-09-13).

## What to record for the next milestone

A short section, written while it is fresh, on what this milestone taught:
which estimates were wrong and in which direction, which tasks turned out to be
two tasks, which tests caught something and which never fired, and which of the
model errors is now the most limiting. That last one is the input to milestone
2's planning.

## Done when

- [ ] Every toolchain and every sanitizer passes.
- [ ] Six fuzzers, ten minutes each, clean.
- [ ] The soak is clean with bounded memory.
- [ ] Every document above describes the tree as it actually is.
- [ ] `VERIFICATION.md` Part 4 has no rows left marked **to build**.
- [ ] Milestone 1 is done, and a person picking this up cold on another machine
      could tell that from the repository alone.
