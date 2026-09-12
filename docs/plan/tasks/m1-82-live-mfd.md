# M1-82 — The live MFD, and the clock

Phase: G | Status: not started
Prerequisites: M1-69, M1-71, M1-81
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

Wiring the instrument to the simulation. The milestone plan makes one point
here that is easy to get backwards:

> **The elements are osculating.** With J2 running they drift continuously, and
> the readout should show that rather than look like instrument noise somebody
> will try to smooth away. An MFD whose inclination never moves is now reporting
> a bug.

## What to implement

- Each frame: take the interpolated state from M1-71, convert with
  `elementsFromState`, derive the display quantities with `orbitInfo`, format
  with M1-81, and draw into the M1-80 panel.
- **The radius and the speed come from the state, not from `orbitInfo`.** This
  MFD holds the state vector it just interpolated, where |r| and |v| are exact;
  the same two read back through the elements are worth only what the true
  anomaly is worth, which near the radial limit is not much — up to 6.4e-5 in
  the radius over a sweep of nearly radial states, measured 2026-09-12. The
  contract is written on `orbitInfo` in [`src/orbit/Orbit.hpp`](../../../src/orbit/Orbit.hpp).
  Every other displayed quantity comes from `orbitInfo`.
- **No smoothing, no averaging, no hysteresis on the numbers.** If a digit
  flickers, that is the physics, and hiding it would hide the perturbation this
  milestone exists to demonstrate. Written in the header, because the instinct
  to smooth an unsteady readout is strong and the reason not to is not obvious.
- **The clock**: UTC from the simulation's `TimePoint`, converted through the
  scale chain from M1-04 and M1-05, plus mission elapsed time from the
  scenario's epoch. A simulation clock that lags real time (M1-69) is shown as
  lagging rather than silently drifting.
- Failure has a display: if `elementsFromState` reports — a rectilinear
  trajectory, a degenerate state — the MFD shows the named reason in place of
  the numbers. An instrument that goes blank tells a pilot nothing; one that
  says `RECTILINEAR` tells them everything.

## Out of scope

Other MFD modes — map, docking, surface. Interaction. Predicting a manoeuvre.
Alarms.

## Tests

- **The readout agrees with the tests**, which is the milestone criterion: for a
  scenario whose elements are known analytically, every displayed value matches
  the value computed independently in the test, to the displayed precision.
- **The numbers drift, and by the right amount**: over one orbit, the displayed
  inclination and ascending node change by amounts consistent with the J2 rates
  measured in M1-77, within their budget. This is the assertion that makes "an
  MFD whose inclination never moves is reporting a bug" a test rather than a
  remark.
- **The clock is right**: UTC displayed at a known simulated epoch matches the
  value computed from the time-scale chain, and MET equals the elapsed simulated
  time exactly.
- **A reported failure is displayed by name**, driven by feeding the MFD a
  rectilinear state.
- **The MFD does not touch the simulation**: with the MFD active, the simulation
  state after an hour is bit-identical to a run without it — M1-70's claim, with
  the last consumer added.
- **Probe `mfd-live`**, with a golden, at a fixed epoch and scenario.

## Frames to look at

`mfd-live.png`. Judge: every number is readable at a glance; the units are
present; the panel does not obscure the part of the view a pilot needs; nothing
is clipped. Then, live: fly the scenario at 100× and watch the inclination and
node digits move. **That is the milestone's most satisfying moment and also its
last functional check** — the numbers moving means the perturbation, the
integrator, the time system and the display are all working together.

## Done when

- [ ] `check` green in both trees.
- [ ] Displayed values match independently computed ones to the displayed
      precision.
- [ ] The osculating drift is asserted against the J2 rates.
- [ ] The simulation is provably unaffected by the display.
- [ ] `mfd-live.png` approved, and the owner has watched the digits drift.
