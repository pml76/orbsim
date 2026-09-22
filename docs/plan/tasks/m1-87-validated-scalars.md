# M1-87 — `Eccentricity` and `GravParam` are validated at construction

Phase: A | Status: **done, 2026-09-22**
Prerequisites: none in the queue. Runs **before M1-11**, because it changes a
public interface of `orbit/` and a type in `core/Units.hpp` that every later
task builds on (decision 109)
Decided by: [ADR 0022](../../adr/0022-a-bounded-scalar-validates-itself.md); register decisions 106–114

## Purpose

**A value that cannot be right should not be constructible.** Today
`Eccentricity{-0.5}` and `GravParam{-1.0}` compile, and what happens next
depends on which function you hand them to. That was found on 2026-09-21 by
checking the source against `CLAUDE.md`'s non-negotiables, and the shape of it
is worth stating before the fix.

`orbit/`'s public interface answers the same class of bad input in **three
different ways**:

| Function | `mu <= 0` | `ecc < 0` |
|---|---|---|
| `elementsFromState` | reports `NonPositiveGravity` | — |
| `propagate` | reports | — |
| `propagateElements` | reports | — |
| `stateFromElements` | **asserts** | — |
| `orbitInfo` | **asserts** | — |
| `meanToEccentricAnomaly` | — | **asserts** |
| `trueToEccentricAnomaly` | — | **nothing** |
| `eccentricToTrueAnomaly` | — | **nothing** |
| `eccentricToMeanAnomaly` | — | **nothing** |

[ADR 0002](../../adr/0002-error-handling-strategy.md) and
[`CODING_GUIDELINES.md`](../../../CODING_GUIDELINES.md) section 7 both say one
strategy per layer. This is three.

**The last row is the one that decided the task.** Measured on 2026-09-21,
with a probe built against the real `orbsim_core` rather than reasoned about:

```
e = -0.5000 | trueToEcc finite  +1.127589 | eccToTrue finite  +0.415419 | eccToMean finite  +1.022109
e = -0.9000 | trueToEcc finite  +2.019387 | eccToTrue finite  +0.167097 | eccToMean finite  +1.279796
e = -1.5000 | trueToEcc NaN     -nan(ind) | eccToTrue NaN     -nan(ind) | eccToMean finite  +1.666327
e = -3.0000 | trueToEcc NaN     -nan(ind) | eccToTrue NaN     -nan(ind) | eccToMean finite  +2.632653
--- for comparison ---
e = +0.5000 | trueToEcc finite  +0.415419 | eccToTrue finite  +1.127589 | eccToMean finite  +0.377891
```

A negative eccentricity between -1 and 0 gives a **finite, plausible, wrong**
number with nothing to say so — and note that at `e = -0.5` the two converters
return each other's answers for `e = +0.5`, because the `sqrt((1-e)/(1+e))`
factor inverts. Below -1 a NaN is returned as a valid `Radians`. That is the
"silently coping" third option [`VERIFICATION.md`](../../VERIFICATION.md)
rule 7 names, in code whose sibling four lines up asserts the same condition.

**The answer is rule 24 rather than rule 7**: prefer the bug you cannot write.
Both scalars become validated types, so the bad value has nowhere to live, and
the runtime reports that existed for `mu` are removed with it.

## What to implement

### `core/Units.hpp`

- **`enum class UnitError : std::uint8_t`** with `NotFinite`,
  `NegativeEccentricity` and `NonPositiveGravity`, a `describe()` with an
  exhaustive `switch` and no `default`, as every other error enum here has
  (decisions 106 and 111). It lives beside the types it belongs to, which is
  the precedent decision 90 set for `EphemerisError`.

- **`Eccentricity` becomes a validated class.** Private constructor;
  `static constexpr std::expected<Eccentricity, UnitError> from(f64)`
  refusing a non-finite value as `NotFinite` and a negative one as
  `NegativeEccentricity`; `value()` for the bare `f64`. **No upper bound**: a
  hyperbolic eccentricity is unbounded in principle, and `Orbit.hpp` records
  convergence measured from 0 to 100 (decision 107).

- **`GravParam` becomes a validated class that keeps its quantity**
  (decision 110). Private constructor;
  `static constexpr std::expected<GravParam, UnitError> from(f64)` refusing
  non-finite as `NotFinite` and `<= 0` as `NonPositiveGravity`; `value()` for
  the bare `f64` as today; and **`quantity()` returning the
  `Scalar<m³/s⁻²>`**, so `mu.quantity() / (r * r)` is still an m·s⁻² quantity
  for [M1-62](m1-62-force-model-seam.md)'s `Acceleration`. This works because
  `Scalar<R>` *derives from* `mp_units::quantity<R, f64>`, so the unit algebra
  is the base's and survives being held rather than inherited.

- **`consteval` literal helpers** beside the factories — `eccentricity(f64)`
  and `gravParam(f64)` — which are a **compile error** on a bad value rather
  than a runtime unwrap (decision 113). `DeltaUt1`'s
  `kDeltaUt1Unmodelled` shows the plain form; these exist because this task has
  56 construction sites and a test table should stay readable.

- **The four kind assertions are rewritten, not dropped.** `Eccentricity` and
  `GravParam` leave the mp-units kind system, so `!is_constructible_v<Radians,
  Eccentricity>`, `!is_convertible_v<Eccentricity, f64>` and
  `!addable<Radians, Eccentricity>` must be re-stated against a class rather
  than a `Scalar`. All three still hold, and more strongly.

### `orbit/`

- **`OrbitError::NonPositiveGravity` is removed**, with its `describe()` arm
  and the five checks that returned it in `elementsFromState`, `propagate`
  and `propagateElements`. A non-positive `mu` can no longer reach them, and
  an error a caller cannot receive is the dead defensive code ADR 0002 argues
  against (decision 108).
- **`orbitInfo` keeps its plain `OrbitInfo` return** (decision 114, reversing
  a ruling taken earlier the same day). `mu > 0` was its only guard, so with a
  validated `GravParam` there is nothing left for a `std::expected` to carry.
- **Three assertions go, because nothing can reach them**:
  `stateFromElements`'s `mu > 0`, `orbitInfo`'s `mu > 0`, and
  `meanToEccentricAnomaly`'s `e >= 0`.
- **The three unguarded converters need no guard.** They are safe by
  construction now, and a line in the header says why rather than leaving the
  silence to be rediscovered.

### The call sites

56 of them: 3 in `src/`, 53 in `tests/`. Literals take the `consteval`
helpers; `tests/fuzz_orbit.cpp` takes the factories, and gains the claim
`test_time.cpp` already makes for `DeltaUt1` — that an accepted value is
inside the stated range.

## Out of scope

- **`stateFromElements`'s `ORBSIM_EXPECTS(p > 0.0)` stays an assertion.** `p`
  comes from `el.slr`, `el.sma` and `el.ecc`; validating the eccentricity does
  not close it, because `slr` and `sma` are `Metres` and validating every
  length is not on the table. Named here so the omission is visible rather
  than looking like an oversight, and it is the next question in this family
  if a scenario loader ever supplies elements directly.
- **No other unit is validated.** `Metres`, `Seconds` and the rest have no
  physical bound to check; these two do.
- **M1-62 is not written here.** It gains a line saying its planned "named
  failure: a non-positive `mu`" is no longer reachable and why.

## Tests

- **`GravParam::from` and `Eccentricity::from` get their own cases**, in the
  shape `test_time.cpp` uses for `DeltaUt1`: every refusal by name, both
  edges of the accepted range, and a value accepted at the boundary.
- **The `consteval` helpers are shown failing**, by the inversion habit the
  project already uses on `static_assert`s: a bad literal must not compile,
  and that is checked by writing it once and watching the build fail.
- **Four existing assertions are removed** (decision 112, with the owner's
  explicit go-ahead, working agreement 1):
  - `tests/test_orbit.cpp`, `TEST_CASE("failures are reported, not
    approximated")` — `propagate(leo, GravParam{0.0})`,
    `elementsFromState(leo, GravParam{-1.0})`, and the `describe()` check on
    `NonPositiveGravity`;
  - `tests/test_orbit_scales.cpp`, `TEST_CASE("non-finite inputs are refused
    by name")` — `propagate(leo, GravParam{kNaN})`.

  **Neither case is deleted**; both keep every other assertion.
  `VERIFICATION.md` rule 6 names the first case by name, and it keeps that
  name and its purpose. What those four assertions checked has not stopped
  being checked — it has moved from a runtime report to a type that cannot
  hold the value, which is rule 24.
- **A mutation pass**, with any survivor taken to the owner rather than closed
  by a test nobody ruled on (`VERIFICATION.md` rule 19), and its mutants
  committed as `scripts/mutants/m1-87.json`.

## Error budget

None. Nothing here is numerical: no formula changes, and the assertion count
must come out **unchanged except for the four removed assertions and the ones
the new cases add**, which is what says the refactor changed no behaviour.
Decision 96's commit is the precedent for using the count that way.

## What the numbers turned out to be

**56 construction sites**, 3 in `src/` and 53 in `tests/`, exactly as measured
before the work started.

**The assertion count is the evidence that nothing else changed.** 1,369,392
before, **1,369,454** after, in 168 cases where there were 155. The +62 is
`test_orbit` -4 and `test_orbit_scales` -1 -- the five assertions that can no
longer be written -- plus 67 in the new `tests/test_units_validated.cpp`.
Nothing else moved.

**All six toolchains, 2026-09-22.** 173 tests under both Windows trees, `asan`
and `windows-msvc`; 172 under `linux-sanitize` and `linux-gcc`, which are core
only and do not build the GPU smoke test. No build error or warning anywhere.

**The mutation pass ran twice, and the first run is the one worth reading.**
Twelve mutants; 11 caught and 1 survived. The code was right and the *test* was
not: every refusal case read `error()` without asserting `!has_value()` first,
which is undefined behaviour on an expected that holds a value and in practice
compares equal to `NotFinite`, the zero enumerator. The case passed while the
factory accepted the NaN it was written to refuse.

The guard had been dropped while splitting the suite to clear a
`readability-function-cognitive-complexity` finding -- a fix for a lint finding
had quietly removed the thing the test was for, which is `VERIFICATION.md`
rule 23 arriving inside this task. Two mutants of the same shape settle the
mechanism rather than leaving it a theory: the `GravParam` NaN mutant was caught
in both runs, because there the NaN is still *refused* and only the name is
wrong, so `error()` is well-defined.

Second run, guard restored: **twelve of twelve, none surviving, none invalid**,
seven of them at compile time.

**Two findings arrived from the tooling and both were fixed rather than
suppressed.** `elementsFromState` reached 88 lines against a threshold of 80, so
the eccentricity construction moved into `assignShape` -- an in/out `Elements&`,
the shape its two neighbours already have for this exact budget. And the new
suite first tripped `clang-analyzer`'s `EnumCastOutOfRange` inside Catch2's own
flag arithmetic; splitting it to one claim per case cleared that and the
complexity finding together, with no `NOLINT`.

## Done when

- [x] `check` green in both trees.
- [x] `Eccentricity{-0.5}` and `GravParam{-1.0}` do not compile, each shown
      failing once rather than assumed -- with a control that does compile, so
      the probe proves something.
- [x] `mu.quantity() / (r * r)` yields an m·s⁻² quantity, proven by a
      `static_assert`, so M1-62 has what ADR 0019 promised it.
- [x] `NonPositiveGravity` appears nowhere in `orbit/`.
- [x] Every refusal has a test that asks for it by name -- and asserts
      `!has_value()` before reading `error()`, which the mutation pass is the
      reason anyone knows to check.
- [x] A mutation pass, with its file committed.
- [x] All six toolchains before the commit: both Windows trees, `asan`,
      `windows-msvc`, `linux-sanitize`, `linux-gcc`.
- [x] ADR 0022 written, superseding ADR 0019's clause that `Eccentricity` is
      an mp-units kind and extending ADR 0018's argument; the ADR index and
      `STATUS.md` updated.
