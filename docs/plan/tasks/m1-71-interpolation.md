# M1-71 — Render-side interpolation

Phase: E | Status: not started
Prerequisites: M1-11, M1-69
Decided by: [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

The fixed timestep is what makes the simulation reproducible; interpolation is
what stops it looking like a slideshow. Without it, a 1 Hz physics step and a
60 Hz display show the vessel jumping once a second — and turning the step rate
up to hide that would trade the reproducibility away for nothing.

This is also the mechanism the owner's fluency question turns on: **a heavy
simulation step costs a step, not a frame.**

## What to implement

- The renderer reads the **two most recent snapshots** and the accumulator's
  remainder, and interpolates between them. It never reads the live state.
- **Cubic Hermite interpolation**, not linear: position and velocity are both
  known at both ends, which is exactly the data a Hermite spline needs, and it
  is a great deal closer to the true trajectory than a straight line for no
  meaningful cost. The error difference is measured below rather than asserted.
- **Never extrapolate.** If the renderer is somehow ahead of the simulation, it
  holds the last snapshot rather than predicting past it — extrapolation
  produces a position the simulation never occupied, which then snaps back.
- The interpolated state is a **render-side value**: it is what
  `toRenderSpace` narrows, and it never flows back into the simulation. One
  direction only, and the link graph keeps it that way.

## Out of scope

Interpolating attitude — there is none yet, and when there is it is `slerp`, on
the same snapshots. Client-side prediction. Threading the simulation.

## Tests

`tests/test_interpolation.cpp`, headless.

- **Exact at the ends**: at fraction 0 and fraction 1 the result equals the
  respective snapshot, bit-identically. A blend that is 0.999 at the end is a
  visible stutter once per step.
- **The accuracy gain is measured**: interpolation error against the true
  trajectory, for Hermite and for linear, at several step sizes, recorded as
  numbers. That is the justification for choosing the more complex one, and if
  the gain is small the choice should be revisited rather than defended.
- **Continuity across a step boundary**: sweeping the fraction from 0 to 1 and
  into the next pair produces no jump in position or its first derivative beyond
  a stated bound — the assertion that catches an off-by-one in which snapshot is
  which.
- **No extrapolation**: a fraction above 1 or below 0 clamps, and the test asks
  for that behaviour by name.
- **Determinism**: the interpolated result is a pure function of the two
  snapshots and the fraction, asserted bit-identically across runs.
- **The simulation is unaffected**: with the renderer interpolating, the
  simulation state after an hour is bit-identical to a headless run — a
  restatement of M1-70's claim with the render path now in the process.

## Error budget

Hermite interpolation error against the true trajectory, at the chosen step
size, recorded as a number and compared against linear. Exactness at both ends
is bit-identical, not toleranced.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The ends are exact, bit for bit.
- [ ] The accuracy gain over linear is a recorded number.
- [ ] The renderer never reads live simulation state, and never extrapolates.
