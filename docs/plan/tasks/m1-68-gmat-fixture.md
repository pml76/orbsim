# M1-68 — The GMAT fixture, and the accuracy budget

Phase: E | Status: not started
Prerequisites: M1-06, M1-67

## Purpose

Everything so far shows the integrator is **self-consistent**: it agrees with
the project's own two-body propagator, its two formulations agree with each
other, and its constants of motion behave. `VERIFICATION.md` rule 3 is explicit
that this is not enough — *"a wrong gravitational parameter conserves energy
beautifully."*

This is the task that brings in data this project did not produce. It exists
because the milestone plan's original wording — an error budget against JPL
Horizons — is not achievable: Horizons publishes ephemerides of natural bodies
under their full force models and will not propagate an Earth satellite under a
J2-only model. The owner ruled for **NASA GMAT** instead: Apache-2.0, current
release R2026a, and a genuinely independent implementation.

## What to implement

- **Install GMAT** and record the version. Configure a propagation with, and
  only with: Earth as a point mass, J2 as the only harmonic, no drag, no solar
  radiation pressure, no third bodies, no relativistic terms. Every one of those
  exclusions is checked in the GMAT script, because a default that is quietly on
  makes the comparison meaningless in the direction that looks like our bug.
- **The case**, stated here before the run: a 400 km circular orbit at
  i = 51.6°, at a named epoch, propagated 24 hours, exported at 60-second
  intervals.
- **Check the constants match first.** GMAT's J2, reference radius and `mu` are
  read out of its configuration and compared against ours. If they differ, ours
  are aligned to the published model GMAT uses and the change is recorded — a
  comparison between two different gravity models measures the models, not the
  code.
- **Commit the ephemeris** as an M1-06 fixture, with a header recording the GMAT
  version, the full force-model configuration, GMAT's own integrator and
  tolerance, the epoch, and the date it was generated. Also commit **the GMAT
  script itself**, so the fixture can be regenerated rather than trusted.
- `tests/test_gmat_reference.cpp` reads it and asserts the budget.

## Out of scope

Comparing against a real satellite's real trajectory — that would test the
model, not the code, and the model deliberately excludes drag. Any force model
GMAT and we do not both have.

## Tests

- **The budget: position error < 10 m after one orbit, and < 1 km after 24
  hours.** Asserted for both propagators — Cowell and Encke — since both ship.
- **The error grows the way theory says**: along-track error dominates and grows
  roughly linearly with time, while radial and cross-track stay bounded. If the
  error is dominated by the radial component, something is wrong with the model
  rather than with the accumulation, and the shape of the error is what
  distinguishes the two.
- **The step size that meets the budget is recorded**, for both propagators,
  which is what M1-69 needs to choose the simulation's fixed step.
- **Sensitivity, which is what proves the test can fail**: with J2 deliberately
  switched off, the 24-hour comparison must miss the budget by orders of
  magnitude. A test that would pass with the perturbation disabled is measuring
  nothing.

## Error budget

**< 10 m after one orbit; < 1 km after 24 h**, for a 400 km circular orbit at
i = 51.6°, J2 only, against GMAT R2026a with its configuration recorded. The
numbers go in the test, the task, the commit message and `PROJECT_STATE.md`.

## Verification

The standing rules. The fixture is committed data, so the test needs no network
and no GMAT installation to run.

## Done when

- [ ] `check` green in both trees.
- [ ] Both propagators meet the budget against the fixture.
- [ ] The GMAT script, version and full configuration are committed with the
      data.
- [ ] The J2-off sensitivity check fails as intended.
- [ ] `VERIFICATION.md` Part 4's rule 3 and rule 4 rows can finally be marked
      done for the physics.
