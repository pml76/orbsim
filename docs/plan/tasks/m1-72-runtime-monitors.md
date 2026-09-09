# M1-72 — Runtime invariant monitors

Phase: E | Status: not started
Prerequisites: M1-70
Decided by: [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

`VERIFICATION.md` rule 15, the last of the three the document marks **to
build**, and the one that was waiting for a simulation loop to monitor.

The argument is short: assertions only run where tests reach, and a long flight
visits states no test constructed. A monitor that catches a slow divergence at
minute three is worth a great deal more than a test that would have caught it at
hour three, if anyone had thought to write it.

## What to implement

`src/sim/Monitors.hpp`.

- **Energy drift per orbit**, compared against a stated budget. With J2 on,
  total energy is conserved, so drift is pure integration error and its rate is
  a direct measure of the integrator's health.
- **The z-component of angular momentum**, likewise conserved under J2 and a
  different invariant from energy — two independent quantities, so a bug that
  preserves one is unlikely to preserve both.
- **NaN gates** on the state entering and leaving each step. Cheap, and it turns
  "the vessel vanished" into a report naming the step it happened on.
- **Step-size and iteration bounds**, and the embedded error estimate from
  M1-65's stepper compared against a threshold — the monitor that has an actual
  numerical claim behind it.
- **How they report, which is the design decision**: in Debug builds a violation
  is an assertion, because it means this code is wrong. In release builds the
  NaN gate stays live and **reports** through the existing error path, because a
  NaN reaching the renderer is a user-visible failure and should be named rather
  than drawn. Everything else compiles out. That split follows ADR 0002 exactly.
- The monitors are **off the hot path by construction**: an energy evaluation
  per step is affordable at 10,000 steps per second and is measured to confirm
  it, rather than assumed.

## Out of scope

A telemetry or logging framework. Monitors for attitude, mass or thrust — none
exist yet. Automatic recovery of any kind: a monitor reports, it does not
correct, because a simulation that quietly repairs its own state is one whose
results cannot be trusted.

## Tests

`tests/test_monitors.cpp`.

- **Each monitor fires when it should**, driven by a test hook that injects a
  corrupted state: an energy jump, an angular-momentum jump, a NaN, an
  out-of-range step. Four monitors, four named failures.
- **And does not fire when it should not**: a 24-hour nominal run trips nothing,
  with the margin between the observed drift and the budget recorded — a monitor
  that sits at 90 % of its threshold in normal flight will cry wolf.
- **The budgets are derived, not chosen**: the energy-drift budget comes from
  the measured drift in M1-64 and M1-66 plus a stated margin, and the derivation
  is in the comment.
- **Release behaviour**: under `NDEBUG` the NaN gate still reports and the
  others are gone, asserted by a test compiled both ways.
- **The cost is measured**: steps per second with and without monitors, recorded
  in the commit message. Per.6 — do not claim a cost is negligible, measure it.

## Error budget

Energy drift per orbit and the embedded error threshold, both derived from
measured values with a stated margin, both recorded.

## Verification

The standing rules, plus a long run — the 24-hour simulation at 10000× under the
Debug build, which is roughly nine seconds of wall clock and exercises 86,400
steps of monitoring.

## Done when

- [ ] `check` green in both trees.
- [ ] Every monitor has a test that makes it fire and one that keeps it quiet.
- [ ] The budgets are derived from measurements, with margins stated.
- [ ] `VERIFICATION.md` Part 4's rule 15 row moves off **to build** — the last
      of the three.
