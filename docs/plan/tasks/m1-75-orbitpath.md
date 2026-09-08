# M1-75 — `OrbitPath` moves into `src/orbit/`

Phase: F | Status: not started
Prerequisites: M1-01

## Purpose

The algorithm already exists, written and tested, in
`coding-guidelines-example/src/orbit/OrbitPath.{hpp,cpp}` — it was chosen as the
worked example precisely because *"it is the next thing orbsim genuinely needs;
there is no Orbit MFD without it."* This task moves it into the project.

**The example keeps its copy, unchanged.** It is the reference implementation of
the house style, not a staging area, and the two are allowed to diverge as this
one adapts to the project's types.

## What to implement

`src/orbit/OrbitPath.hpp` / `.cpp`.

- The sampler itself, ported: `PathOptions` with `SampleCount`, `Spacing`
  (uniform in angle or in time) and `PathClosure` (open or closed loop) — three
  enums and a count rather than three booleans, which is why the example exists.
- **Adapted to this project's types**: `orb` rather than `orbex`, the project's
  `Elements` — which carries `slr` explicitly, so a parabolic set stays
  representable — `GravParam`, `Seconds`, and `Vec3` from `core/Math.hpp`.
- **One error representation per layer** (ADR 0002): the example's `PathError`
  folds into `OrbitError`, gaining `NotAClosedOrbit` and `TooFewSamples`, each
  with a `describe()` string. The orbit layer reports one enum, not two.
- Rule of Zero with a resource, as the example demonstrates: the class owns a
  `std::vector` and declares no destructor, no copy, no move — asserted by
  `static_assert`, not claimed in a comment.

## Out of scope

Drawing (M1-76). Sampling a *propagated* path under J2 — the track this
milestone draws is the osculating conic, which is what makes it precess. Ground
tracks.

## Tests

`tests/test_orbit_path.cpp`, the example's suite ported to Catch2 and extended.

- **The conic equation**, which is the independent check: every sampled point
  satisfies `r = p / (1 + e·cos ν)` to 1e-12 relative, computed in the test from
  the elements rather than from anything the sampler did.
- **Closure**: with `ClosedLoop` the last point equals the first bit-identically;
  with `OpenEnded` it stops exactly one step short.
- **Uniform in time really is**: propagating between consecutive samples with
  `propagate()` takes equal time intervals to 1e-9 relative — checked against
  the propagator, which shares no code with the sampler.
- **Uniform in angle really is**: consecutive true anomalies differ by exactly
  the same step.
- **The singularities**, per rule 5: `e = 0`, `e = 0.99`, `e = 0.9999`, `i = 0`,
  `i = π`, retrograde. Each produces a path whose points still satisfy the conic
  equation.
- **Named failures**: a hyperbolic element set, fewer than three samples, a
  non-positive `mu`, a non-positive semi-major axis.
- **Determinism**: bit-identical output across runs, which the example already
  asserts and which this project needs for the golden images downstream.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The example's copy is untouched.
- [ ] Path errors live in `OrbitError`, with `describe()` covering them.
- [ ] Every sampled point satisfies the conic equation independently.
