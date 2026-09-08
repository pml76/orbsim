# M1-65 — The high-order stepper

Phase: E | Status: not started
Prerequisites: M1-64

## Purpose

RK4 at a step small enough to meet the milestone's accuracy budget is wasteful:
an eighth-order method reaches the same error with a step tens of times longer,
which matters at 10000× time acceleration where the step count is the whole
cost.

The one that ships. RK4 stays, permanently, as the thing it is checked against.

## What to implement

- **`Dop853Stepper`**: the Dormand-Prince 8(5,3) coefficients, run in
  **fixed-step mode**. The embedded error estimate is computed and **exposed as
  a monitor** — it is what M1-72's runtime invariant checks read — but it does
  **not** adapt the step. Determinism first: an adaptive controller is
  reproducible only if the controller is itself deterministic and never reads a
  clock, and that is a decision to take deliberately later rather than inherit
  by accident now.
- The coefficients are published (Hairer, Nørsett and Wanner), and they are
  copied with the citation and with their full precision. A truncated
  coefficient produces a method of lower order that still converges, which the
  order test below is what catches.
- Joins the `Stepper` variant. Every consumer now handles both, or does not
  compile.

## Out of scope

Adaptive stepping, dense output, and multistep methods. All three are
straightforward to add behind this seam, which is the reason the seam is shaped
the way it is.

## Tests

Extends `tests/test_stepper.cpp` — and the point of this task is that the tests
already exist.

- **The measured convergence order is 8.0 ± 0.1**, by the same step-halving
  study, at all four scales. This is the test that catches a mistyped
  coefficient.
- **Differential against RK4** (rule 14 mechanised): both steppers, the same
  force model, the same initial state, small steps — the trajectories agree to
  within the sum of their truncation errors, which is computed rather than
  guessed.
- **Efficiency, measured rather than claimed**: the step size each method needs
  to reach 1 m of position error over one orbit, recorded as two numbers in the
  commit message. That is the justification for the added complexity, and if it
  is not large the decision should be revisited.
- **The embedded estimate is honest**: the estimated error correlates with the
  actual error against `propagate()` over a sweep of step sizes. A monitor that
  does not track the thing it monitors is worse than none, because it will be
  believed.
- **Determinism** and the state-resume property, as for RK4.

## Error budget

Measured order **8.0 ± 0.1** at four scales. The step-size-for-1 m figures for
both methods recorded, not bounded.

## Verification

The standing rules, plus the Linux presets run here rather than waiting for the
gate: two compilers disagreeing about a high-order tableau is exactly the signal
that found the unstable Kepler solver, and this is dense floating-point code.

## Done when

- [ ] `check` green in both trees, and both Linux presets agree.
- [ ] Order 8 is measured at four scales.
- [ ] Both steppers agree within their combined truncation error.
- [ ] The efficiency gain is a recorded number.
