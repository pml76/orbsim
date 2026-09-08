# M1-83 — Milestone acceptance

Phase: G | Status: not started
Prerequisites: M1-22, M1-60, M1-82

## Purpose

Every phase criterion has been checked as it was reached. This task checks them
all **again, together, in one build**, because a milestone is not the sum of its
phases: the atmosphere was accepted before the quadtree existed, the frame-time
budget was measured before the MFD, and the determinism claim was made before
the last two consumers of simulation state were added.

## What to do

Run the whole milestone's acceptance in one session, from one build, and record
it.

**The seven phase criteria, in the plan's own words:**

| Phase | Criterion | How it is checked here |
|---|---|---|
| A | A grid at Earth radius with no jitter, lit through the radiometric chain | `probe_grid-jitter` and `probe_lambert`, re-run |
| B | One tile loads from disk and draws | `probe_tile-earth`, and the full pyramid loaded |
| D | The limb, terminator and a sunrise read as Earth, in physical radiance | The four phase D probes, re-run and re-judged |
| C | 400 km to 10 km with no popping and no seams, relief at the terminator | The full `descent` path, re-run |
| E | A vessel orbits under the integrator, inside a stated budget against GMAT | `test_gmat_reference`, and a live one-hour flight |
| F | The track precesses at the J2 rate, within 1 % | `test_precession`, and the two `precession` frames |
| G | Numbers readable while flying, agreeing with the tests, visibly osculating | `probe_mfd-live`, and a flown session |

**The fluency budget**, measured last, with everything in the frame at once:
16.6 ms at 1920×1080, High preset, RTX A2000, Earth from 400 km — planet,
atmosphere, quadtree, orbit track and MFD together. Recorded per stage.

**A flown session**, by the owner, of at least ten minutes: fly the scenario,
accelerate time, descend, watch the MFD. Some defects only appear when a person
does something nobody scripted, and that is the point of this step.

## What to record

A short milestone report in `docs/plan/milestone-1-earth.md` — the criteria, the
measured numbers behind each, the frame-time table, and the model errors
standing at the end: nutation, ΔUT1, the solar formula, the geoid, and the
force model's deliberate exclusions. That last list matters more than the
successes: it is what the next milestone starts from.

## What to do if something fails

**Add a task to the queue, and say so.** Not a quiet fix inside this task: a
milestone-acceptance commit that also changes behaviour is a commit nobody can
review, and the queue is the record of what was actually done.

## Done when

- [ ] All seven phase criteria pass, in one build, on one day.
- [ ] The fluency budget is measured with the complete scene and recorded.
- [ ] The owner has flown it for ten minutes and said what they saw.
- [ ] The milestone report is written, including what is still approximate.
