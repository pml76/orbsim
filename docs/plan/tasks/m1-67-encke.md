# M1-67 — Encke's method

Phase: E | Status: not started
Prerequisites: M1-66

## Purpose

Cowell spends its precision re-deriving the Kepler motion this project already
solves exactly. Encke integrates only the **deviation** from an osculating
reference conic: the deviation is small, so the same integrator holds far more
significant digits, and the reference conic is `propagate()` — the most tested
code in the repository.

`realism.md` recommends it, ADR 0011 records the choice, and the owner's ruling
was to build Cowell first precisely so that this one has something independent
to be checked against.

## What to implement

- **`EnckePropagator`**, joining the `Propagator` variant. It holds a reference
  conic, integrates the difference between the true acceleration and the
  reference conic's own, and adds the result to the analytic conic position.
- **Rectification**: when the deviation grows past a threshold, the reference
  conic is replaced by the current osculating conic and the deviation resets to
  zero. The threshold is stated as a **relative** quantity — deviation over
  radius — with the number and its justification in the comment, because an
  absolute threshold in metres is exactly the class of bug this project has
  already shipped once (`VERIFICATION.md` rule 9).
- **Rectification is deterministic**: it happens on a condition of the state,
  never on elapsed wall-clock time or a frame count.
- The cancellation-prone subtraction — the difference of two nearly equal
  accelerations — uses the formulation that avoids it (the standard `f(q)` series
  form), with a comment on why the naive difference is wrong. This is the part
  of Encke that everyone gets wrong once.

## Out of scope

Regularisation for close approaches — a refinement, not a prerequisite, as
`realism.md` records after withdrawing the constraint that briefly made it one.
Variable-step rectification policies.

## Tests

`tests/test_encke.cpp`.

- **Exactness with no perturbation**, which is Encke's signature property: with
  J2 off, the deviation is identically zero and the result equals `propagate()`
  to machine precision — far tighter than Cowell's 1e-9, and a test only Encke
  can pass.
- **Differential against Cowell** with J2 on, at four scales and four conics:
  the two agree within the sum of their error estimates over 24 hours. Two
  formulations sharing no code — rule 14, mechanised, and the reason both are
  kept.
- **Rectification changes nothing**: forcing a rectification at an arbitrary
  point produces a trajectory that agrees with one rectified elsewhere, to well
  inside the budget. If rectification is visible in the answer, it is wrong.
- **The cancellation is actually avoided**: the naive difference formulation is
  implemented in the *test* and shown to lose digits where the shipped one does
  not, with the measured difference recorded. Otherwise the careful formulation
  is a comment nobody can check.
- **The precision advantage is measured**: the step size each method needs for
  1 m of error over 24 h, recorded for Cowell and Encke. That number is the
  justification for keeping two propagators.
- **Determinism**, including across a rectification.

## Error budget

Zero-perturbation agreement with `propagate()` at **machine precision**
(≤ 1e-14 relative). Encke against Cowell within their combined estimates over
24 h. The measured precision advantage recorded as a number.

## Verification

The standing rules, plus both Linux presets — dense floating-point code in which
a cross-compiler disagreement would be meaningful.

## Done when

- [ ] `check` green in both trees, both Linux presets agreeing.
- [ ] The zero-perturbation exactness test passes at machine precision.
- [ ] Encke and Cowell agree, and both are kept.
- [ ] The rectification threshold is dimensionless and says why.
