# M1-69 — The simulation clock

Phase: E | Status: not started
Prerequisites: M1-03, M1-68
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

Where the propagator becomes a simulation. Guideline section 19 argues the fixed
timestep at length: hand the integrator whatever the last frame took, and the
trajectory becomes a function of frame rate — *"a slow machine flies a
measurably different orbit than a fast one, and neither can reproduce the
other. That is not a bug you find, it is a bug you argue about."*

## What to implement

**`src/sim/`**, a new directory in `orbsim_core` — `orbit/` is trajectory
mathematics and `core/` is below both; the loop that owns world state, and which
will grow vessels and scenarios in milestone 2, is neither. Same target, same
dependency rules, so no new link boundary.

- **The fixed step**, chosen from M1-68's measurement rather than by feel: the
  largest step at which the shipped propagator meets the 24-hour budget, with
  margin, and the number recorded with its derivation. A step chosen because it
  is a round number is a step nobody can defend.
- **An accumulator**: real elapsed time multiplied by the acceleration factor
  goes in, whole steps come out, the remainder carries to the next frame. The
  simulation clock is a `TdbTime` advanced by whole steps only — never by a
  fraction, and never from a frame duration.
- **Time acceleration is a step count, not a step size** (decision 13). At
  10000× with a one-second step that is 10,000 steps per second of wall clock,
  a few milliseconds of CPU — far inside budget. The trajectory is therefore
  identical at every acceleration, which is what makes M1-70's assertion exact
  rather than approximate.
- **When the machine cannot keep up, the simulation clock lags and says so.**
  There is a maximum number of steps per frame; beyond it the simulation falls
  behind real time, the lag is reported, and the model is not changed. The
  alternative — a bigger step under load — is a different simulation, and
  `realism.md` section 6.1 rules it out.
- **Snapshots**: after each step the loop publishes an immutable state — epoch,
  position, velocity — and keeps the last two. The renderer reads those and
  never touches the live state (section 13).

## Out of scope

Threading the simulation off the render loop — worth doing, and worth doing when
a profile says so; the snapshot design is what makes it a small change later.
Vessels, mass, thrust. Scenario files: the initial state is a hard-coded named
scenario, per the milestone's scope fence.

## Tests

`tests/test_simulation_clock.cpp`, headless.

- **The accumulator is exact**: over 100,000 frames of varying, adversarial
  frame durations — including zero, and including one enormous one — the total
  simulated time equals whole steps times the step size exactly, and the
  remainder never exceeds one step.
- **Acceleration does not change the trajectory**: 1×, 100× and 10000× produce
  bit-identical states at the same simulated epoch. The full statement of this
  is M1-70; the mechanism is asserted here.
- **A varying frame rate changes nothing**: a run with random frame durations
  and one with constant durations reach bit-identical states at the same
  simulated time. This is the property section 19 exists for.
- **The lag path**: with the step cap set low, the clock falls behind, the lag
  is reported, and the states produced are still bit-identical to a run that had
  no cap — the model did not change, only the pace.
- **Zero and negative acceleration** are handled by name: zero pauses, negative
  is refused.
- **A zero-length step is never handed to the propagator**, which is the case
  that used to make `propagate()` report non-convergence on a hyperbola and is
  now the case a paused frame produces.

## Error budget

The fixed step is chosen so the propagator meets **< 1 km after 24 h** with
margin; the chosen step and its measured error are recorded.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The step size is derived from a measurement and written down.
- [ ] Frame rate and acceleration provably do not affect the trajectory.
- [ ] Falling behind is visible and honest, and never changes the model.
