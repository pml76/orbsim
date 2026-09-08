# M1-66 — The Cowell propagator

Phase: E | Status: not started
Prerequisites: M1-63, M1-65

## Purpose

The third seam: a propagator, which is what a caller actually asks to move a
vessel. Cowell is the direct one — integrate the total acceleration — and it is
built first for the reason the owner chose: it becomes the independent
implementation that Encke is checked against.

## What to implement

`src/orbit/Propagator.hpp` / `.cpp`.

- **`CowellPropagator`**, holding a `ForceModel` and a `Stepper`, exposing
  `propagateTo(TdbTime)` and `advance(Seconds)`, both reporting.
- `using Propagator = std::variant<CowellPropagator>;` — Encke joins in M1-67,
  and the closed-form Kepler propagator can join later as a third alternative
  for the cases that want it.
- **The model description**, which costs almost nothing and is worth a great
  deal: a propagator can describe itself — which terms, which stepper, which
  step — as a short string, written into the log, into probe sidecars and
  eventually into a scenario. ADR 0006's whole argument is that a model error
  and a code bug must stay distinguishable, and that starts with every
  trajectory knowing which model produced it.
- Failure reported by name from any layer beneath, never swallowed.

## Out of scope

Encke (M1-67). The simulation clock and time acceleration (M1-69). Anything that
knows about frames or rendering.

## Tests

`tests/test_cowell.cpp`.

- **The budget: with J2 disabled, agreement with `propagate()` to 1e-9 relative
  over one orbit.** The two-body propagator is closed-form, tested by 3,632
  assertions, and shares no code with the integrator — so this is the strongest
  available check that the integration machinery is right before any
  perturbation is added.
- **Every scale and every conic**: circular, eccentric, near-parabolic and
  hyperbolic, at the Moon, Earth, Jupiter and the Sun. The same list the
  existing suites use, for the same reason.
- **With J2 enabled**, the constants of motion behave as the physics says rather
  than as two-body motion says: energy is still conserved (J2 is conservative),
  the z-component of angular momentum is conserved, and the total angular
  momentum is **not** — that last one is the assertion that proves J2 is
  actually being applied, and it fails if the term is silently zero.
- **Composition**: propagating a + b agrees with propagating a then b, to the
  integrator's accumulated error.
- **Time reversal** over 100 orbits, within the same bound.
- **Determinism**: bit-identical across runs, and across a save and resume of
  the propagator's state.
- **Named failures** propagate from the force model and the stepper unchanged.

## Error budget

**1e-9 relative** against `propagate()` with J2 off, over one orbit, at four
scales. Energy drift and angular-momentum drift with J2 on recorded as measured
numbers.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The 1e-9 agreement holds at every scale and every conic.
- [ ] Total angular momentum is shown *not* to be conserved under J2, which is
      how we know J2 is on.
- [ ] Every propagated trajectory can state the model that produced it.
