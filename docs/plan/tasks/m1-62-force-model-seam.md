# M1-62 — The force-model seam, and point-mass gravity

Phase: E | Status: not started
Prerequisites: M1-01, M1-03
Decided by: [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

ADR 0011, and the owner's requirement that a different propagator can be plugged
in later. The seam is **three seams**, because swapping the integrator alone
would still leave the Moon unaddable: force terms, the stepper, and where body
positions come from.

This task builds the first and the third, plus the one term milestone 1 starts
from.

## What to implement

`src/orbit/ForceModel.hpp` / `.cpp`.

- **The term signature, which is the load-bearing decision.** Every term is
  asked for an acceleration given the epoch, the state, and an **ephemeris
  accessor** — even though the only ephemeris in this milestone is the analytic
  Sun and nothing gravitational reads it. That parameter is why adding a
  third-body term later changes no signature anywhere.
- **`enum class Dependency : std::uint8_t { TimeOnly, Position, PositionVelocity }`**,
  declared by every term. A future symplectic or Nyström integrator requires
  velocity-independent acceleration — true of gravity and J2, false of drag and
  thrust — so it can **refuse by name** instead of integrating something that is
  wrong and plausible.
- **`using ForceTerm = std::variant<PointMassGravity>;`** — one alternative
  today, `Zonal` joining in M1-63. A `ForceModel` holds an ordered
  `std::vector<ForceTerm>` and sums them **in order**, because summation order
  is part of the bit-identical determinism claim in M1-70.
- **`Acceleration`** joins `core/Units.hpp` as a strong type (m·s⁻²), and the
  model returns a `Vec3` of them.
- **`PointMassGravity`**: `-mu r / |r|^3`, with the same care the existing
  propagator already takes — reporting a non-finite state or a zero radius by
  name rather than returning a NaN that shows up three hours later.

## Out of scope

Any integrator (M1-64). J2 (M1-63). Multi-body, drag, SRP, thrust — all deferred
by the milestone's scope fence, and all of them are *terms*, which is the point
of building the seam this way.

## Tests

`tests/test_force_model.cpp`, headless, in `orbsim_core`.

- **Point-mass acceleration against analytic values** at several radii and at
  every scale the project flies — Moon, Earth, Jupiter, Sun — because a suite of
  only Earth cases passed 732 checks over a broken propagator once already.
- **Inverse-square**: doubling the radius quarters the acceleration, exactly,
  over a sweep.
- **Direction**: the acceleration is antiparallel to the position vector to
  1e-15, including at very large and very small radii where cancellation bites.
- **Summation is ordered and deterministic**: a model with several copies of the
  same term produces bit-identical results across runs, and reordering the terms
  is asserted to be a *different* answer — which documents that order matters
  rather than leaving it as a surprise.
- **Dependency traits are right**: `PointMassGravity` reports `Position`, and a
  consumer asking for velocity-independence accepts it.
- **Named failures**: a zero radius, a non-finite state, a non-positive `mu`,
  each by name, matching the vocabulary `OrbitError` already uses.
- **The variant dispatches**, and adding an alternative that a visitor does not
  handle fails to compile — demonstrated in a comment with the actual error,
  since it cannot be a runtime test.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The term signature takes the epoch and an ephemeris accessor, unused.
- [ ] Every term declares its dependency, and the trait is tested.
- [ ] Summation order is fixed, documented and asserted.
