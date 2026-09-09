# ADR 0011: The integrator has three seams, and they are closed sets

Status: accepted (2026-09-08; recorded 2026-09-09)

Decisions 4 and 7 to 13 of
[the milestone 1 register](../plan/milestone-1-decisions.md), with the phase E
error budgets from its section 5.

## Decision

[`0006`](0006-simulation-not-sandbox.md) requires multi-body gravity with
perturbations, growing from J2 through drag and solar radiation pressure to
thrust. The way that goes wrong is not the mathematics -- it is that the
integrator becomes the place each new force is welded in, until adding drag
means editing the stepper. So the shape is fixed before the first line:

- **Three seams, not one.** Force terms, the stepper, and where body positions
  come from. Adding the Moon later is adding a *term* and an *ephemeris*; it is
  not a change to the integrator.
- **Each seam is a closed set, expressed as `std::variant`.** No inheritance,
  no heap, no vtable. Values stay trivially copyable, so a physics snapshot is
  a copy rather than a deep clone, and **an alternative the compiler has not
  seen handled is a compile error** rather than a term that is silently zero.
- **The stepper is stateful, with explicit copyable state**: `reset(state,
  epoch)`, then `advance()`. That is the only shape that admits a multistep
  method such as Gauss-Jackson, or an adaptive controller, without a rewrite --
  both need to carry history, and a stateless `step()` cannot.
- **Each force term declares whether it depends on time, position or
  velocity.** A future symplectic or Nystrom integrator can then refuse a term
  it cannot handle, by name, instead of integrating something wrong but
  plausible.
- **Six integrated components now, with steppers generic over a state
  concept.** Thrust with variable mass, and 6-DOF attitude, join later as
  channels rather than as a new integrator.
- **Cowell first, then Encke, and both are kept and diffed against each
  other.** Fixed-step Cowell with RK4 comes first so that convergence order can
  be measured against theory (4.0 +/- 0.1); then a high-order tableau (8.0 +/-
  0.1); then Encke, integrating only the deviation from an osculating reference
  conic and reusing the two-body propagator that is already written and tested.
- **Time acceleration is more steps, never a bigger one.** 1x and 10000x are
  bit-identical. If a machine cannot keep up, the simulation clock lags real
  time **and says so**; the model never changes.
  [`0007`](0007-render-quality-is-a-struct.md) is the same rule from the other
  side: physics cost is answered by decoupling, never by a quality setting.
- **The external reference is NASA GMAT, not JPL Horizons.** Horizons publishes
  ephemerides of natural bodies and of real spacecraft; it cannot propagate a
  hypothetical satellite under a J2-only force model, which is precisely the
  claim phase E has to check. Horizons keeps what it is good for -- Sun, Moon
  and Earth positions, and the time scales.

The budgets, stated before the code per
[`../VERIFICATION.md`](../VERIFICATION.md) rule 4:

| Claim | Budget | Checked against |
|---|---|---|
| J2 off, one orbit | **1e-9 relative** | `propagate()`, which shares no line with the integrator |
| Convergence order | within **0.1** of theory | A step-halving study |
| Position, J2 only, 400 km circular, i = 51.6 deg | **< 10 m after one orbit, < 1 km after 24 h** | GMAT R2026a, settings recorded beside the fixture |
| Determinism | **bit-identical** after 24 h, run against run and 1x against 10000x | Itself -- the one place `==` on floats is the correct operator |

## What we considered

**Virtual interfaces.** The obvious answer, and what most engines do: a
`ForceTerm` base class, an `Integrator` base class, `unique_ptr` and a vector.
It makes the set *open*, which sounds like the benefit and is the cost here.
An open set means no compiler check that every alternative was handled
anywhere; it means a virtual call in the innermost loop of the simulation; and
it means a physics state that is a graph of owned objects rather than a value,
so a snapshot is a deep copy and bit-identical determinism
([`../VERIFICATION.md`](../VERIFICATION.md) rule 16) becomes something to
engineer rather than something to get for free.

**Templates alone**, resolving every combination at compile time. The fastest
code and genuinely zero-overhead. It also makes the force model a property of
the *binary* rather than of the scenario, so nothing can be chosen at run time,
and it multiplies build time by the number of instantiated combinations. Build
time is a feature ([`../../CODING_GUIDELINES.md`](../../CODING_GUIDELINES.md)
section 17).

**Encke only.** Tempting, and it was the standing recommendation in
[`../plan/realism.md`](../plan/realism.md): it holds far more significant
digits than Cowell for a nearly-Keplerian orbit, and it reuses a propagator
that is written, tested and known correct at every scale. It was rejected for
the reason rule 2 exists -- **a method checked only against itself proves
nothing.** Cowell is the independent second implementation, and the difference
between the two is the check that neither can hide a sign error behind. Cowell
first also makes the convergence-order study meaningful, because its error is
the stepper's rather than the reference conic's, so the measured 4 and 8 are
statements about the tableaux.

## Why

Every seam here exists because of a specific thing that is coming and is not
here yet: a third body, a multistep method, a symplectic integrator, thrust
with variable mass, attitude. None of them is speculative generality -- each is
named in [`0006`](0006-simulation-not-sandbox.md) or in
[`../plan/realism.md`](../plan/realism.md) section 3 as work this project has
committed to. What is being bought is that each of them is an *addition* rather
than an edit to a working integrator, and the `std::variant` is what makes the
compiler say where the additions are needed.

Keeping both propagators is expensive in code and cheap in confidence, and this
project has already been shown the price of the alternative: a two-body
propagator passed 732 assertions while being wrong at 1 AU, because every test
that could have caught it was written against the same assumptions. Two
formulations that share no line are the cheapest defence there is against a
class of error that no amount of self-consistency will find.

## What this record does not decide

- **Which high-order tableau.** Order 8 is the requirement; the choice is
  [M1-65](../plan/tasks/m1-65-high-order-stepper.md)'s.
- **Adaptive stepping.** The stateful interface admits it; nothing in milestone
  1 uses it, and determinism constrains how it could ever be introduced.
- **Equinoctial elements.** The propagated state is Cartesian; equinoctial is
  deferred and named in the milestone's scope fences so the deferral is
  visible.
- **The ephemeris beyond an analytic Sun**, and how far up the
  spherical-harmonic field to go. Both are open in
  [`../plan/realism.md`](../plan/realism.md) section 4.
- **Regularisation for close approaches.** A refinement, not a prerequisite --
  see `realism.md` section 1.2, where a constraint that argued for it turned
  out to be a defect in the solver rather than a property of the method.
