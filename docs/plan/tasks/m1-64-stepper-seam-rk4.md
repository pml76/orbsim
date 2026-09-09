# M1-64 — The stepper seam, and RK4

Phase: E | Status: not started
Prerequisites: M1-62
Decided by: [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

The second seam from ADR 0011, and the first integrator. RK4 is chosen to go
first not because it is what will ship, but because its convergence order is
exactly 4 and that is a property a test can *measure* — which makes it the
instrument that verifies the high-order method in M1-65.

## What to implement

`src/orbit/Stepper.hpp` / `.cpp`.

- **A stateful stepper**, per ADR 0011: `reset(state, epoch)`, `advance()`,
  `state()`, `epoch()`, with the internal state copyable and part of any
  snapshot. A pure `step()` function would be simpler and would permanently
  exclude multistep methods — Gauss-Jackson needs a startup history — and the
  header says that in one sentence so the shape is not "simplified" later.
- **A state concept** rather than a hard dependency on `StateVector`, so that
  mass and attitude can join as channels without any stepper changing
  (decision 12). Milestone 1 instantiates only the six-element case.
- **`Rk4Stepper`**: classical fixed-step Runge-Kutta, step size a `Seconds`
  fixed at construction. Four force evaluations per step, in a fixed order,
  summed in a fixed order — determinism is a property of the arithmetic, not
  only of the inputs.
- `using Stepper = std::variant<Rk4Stepper>;` — `Dop853Stepper` joins in M1-65.
- Reported failures: a non-finite state entering or leaving a step, a
  non-positive step size, and a force model that reports.

## Out of scope

Adaptive step control. Multistep methods. Encke (M1-67) and Cowell (M1-66) —
this task advances a state under a force model and does not know what a conic
is.

## Tests

`tests/test_stepper.cpp`.

- **The convergence order is measured, not assumed**: integrate a circular
  Earth orbit for one period at step sizes h, h/2, h/4 and h/8, and fit the
  error against the step size. The measured order must be **4.0 ± 0.1**. The
  reference is the existing `propagate()`, which shares no code and no
  formulation with the integrator — rule 2 in its strongest form.
- **Exactness on polynomials**: RK4 integrates a cubic exactly; the residual is
  at machine epsilon. A wrong Butcher coefficient fails this and can still show
  order 4 on a smooth problem, which is why both tests exist.
- **Time reversal**: stepping forward n steps and back n steps returns the
  initial state within the accumulated truncation error, which is itself
  bounded by the order test.
- **Conservation**: energy and angular momentum drift stay inside a stated bound
  over 100 orbits — reported as measured numbers, since for a non-symplectic
  method the drift is secular and its size is the useful information.
- **Determinism**: two runs of 10,000 steps are bit-identical, and the stepper's
  saved state resumes to a bit-identical trajectory.
- **Every scale**: the order test is repeated at the Moon, Earth, Jupiter and
  the Sun. An integrator with a hidden absolute tolerance passes at one scale
  and fails at another — the exact defect this project shipped once in
  `propagate()`.
- **Named failures**: zero step, negative step, non-finite state in, and a force
  model that reports.

## Error budget

Measured convergence order **4.0 ± 0.1** at every scale. Energy drift over 100
orbits recorded as a number rather than bounded by a guess.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The order test measures 4 at four scales.
- [ ] The stepper's state is copyable and resumes bit-identically.
- [ ] Nothing in the stepper knows what a Kepler orbit is.
