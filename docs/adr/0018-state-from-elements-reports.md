# ADR 0018: `stateFromElements` reports, like every other conversion

Status: accepted (2026-09-13)

`stateFromElements` was the last conversion in `orbit/` returning a value
rather than a `std::expected`. It now returns
`std::expected<StateVector, OrbitError>`, and `OrbitError` gains one
enumerator, `UnreachableAnomaly`.

## Why it changed

Two defects, found while measuring whether the remaining conversions would
gain from the double-double arithmetic added in
[`core/DoubleDouble.hpp`](../../src/core/DoubleDouble.hpp). Neither needed
double-double. Both came from the same thing: the function computed
`1 + e cos v` and `e + cos v` straight, while `orbitInfo` and
`propagateElements` both called the cancellation-free forms sitting a few
hundred lines above it in the same file.

- **A NaN position for 169 of 10,000 nearly radial element sets** -- element
  sets `elementsFromState` itself had produced. `ecc` stores 1 on both sides of
  the radial limit, so `1 + e cos v` came out exactly zero, the radius
  infinite, and the quaternion rotation turned the infinities into a NaN. The
  signature had no way to say so.
- **A radius 2.2 times too small, with nothing to mark it.** For an element set
  whose `ecc` rounds to `1 - 1.11e-16` while `p` and `a` say `e - 1` is
  `-5e-17`, the two disagree by that factor, and at `v = pi` the radius *is*
  their difference: 1.26101e23 m where the elements describe 2.8e23. A
  plausible number, wrong by more than half. This is the worse of the two,
  because a NaN at least propagates.

Substituting the two helpers removes every NaN and brings three of the five
measured families from outside the conditioning law `orbitInfo` already carries
to inside it. That part needed no decision.

What needed one is the third case, which the helpers cannot fix because it is
not a rounding accident: **an anomaly the conic never reaches.** A hyperbola's
asymptote is at `acos(-1/e)`; past it there is no trajectory, `1 + e cos v` is
negative, and the function returned the position mirrored through the focus.
`elementsFromState` never produces such an element set -- but a scenario file
can write one down, and rule 3 of [`../../CLAUDE.md`](../../CLAUDE.md) puts
that on the `std::expected` side of the line: *"`std::expected` for failures a
caller can cause; assertions for what only a bug can cause."*

## What was considered

- **The helpers alone, no signature change.** Zero call-site churn and no
  record like this one. Rejected because it leaves the mirrored position
  shipping silently, which is the failure mode this codebase has spent four
  commits removing from its neighbours.
- **The helpers plus an `ORBSIM_EXPECTS` on the factor**, consistent with the
  two assertions the function already carries for `mu` and `p`. Rejected on
  rule 3: assertions are compiled out in Release, so the mirrored position
  still ships, and an element set is caller input rather than an internal
  invariant.
- **Giving `orbitInfo` an error channel too**, since it shows the same state as
  a *negative radius* and is equally undocumented. Rejected as scope: a second
  public signature and a second set of call sites, in a commit that is already
  a defect fix. Its contract now states what a negative radius means, which is
  information a caller can act on and previously had to discover.

## What it cost

Thirty-five call sites, every one of them in `tests/`. There are no callers in
`src/` yet, which made this the cheapest moment this change will ever have:
the first production caller is the Orbit MFD, still unwritten. The tests reach
it through one helper, `stateOf` in
[`../../tests/OrbitTestSupport.hpp`](../../tests/OrbitTestSupport.hpp), which
asserts success and unwraps -- because a test that builds its own element set
and is then refused has a bug in the test. The three tests that mean to check a
refusal call `stateFromElements` directly.

`stateFromElements` also gained the postcondition `elementsFromState` has
carried since fuzzing found the need for it: a success is never a NaN. That
also covers a non-positive `p` in a Release build, where the assertion above it
is compiled out.

## Consequences

- Every conversion in `orbit/` now reports. There is no longer one that a
  caller has to treat differently.
- `OrbitError` has an enumerator that code actually returns. The last change to
  that enum removed one that nothing could return, on the grounds that *"an
  enumerator no code can return is a lie in a header"*; this is the same
  argument pointing the other way.
- The seeded sweep in
  [`../../tests/test_orbit_scales.cpp`](../../tests/test_orbit_scales.cpp) now
  asserts the whole state through `stateFromElements`, not only the radius and
  the speed through `orbitInfo`. That asymmetry is exactly why a NaN in one of
  them survived three commits whose measurements were all aimed at this region.
  The law is unchanged; its worst case over the wider assertion is between 14
  and 16 u where it was between 8.5 and 9.3, so the budget of 40 u is about two
  and a half times the measurement rather than four.
