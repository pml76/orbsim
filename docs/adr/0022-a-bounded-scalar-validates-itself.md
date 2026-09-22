# ADR 0022: A scalar with a physical bound validates itself

Status: **accepted** (2026-09-22), implemented the same day in
[M1-87](../plan/tasks/m1-87-validated-scalars.md). Register decisions 106–114.

**Supersedes the clause of [`0019`](0019-vectors-carry-their-unit.md) that makes
`Eccentricity` an mp-units *kind***, and extends
[`0018`](0018-state-from-elements-reports.md)'s argument — that a conversion
reports rather than guesses — one level down, to the values the conversions take.

## Decision

**A scalar whose physical meaning bounds its value holds that bound itself.**
Two do: an eccentricity is not negative, and a gravitational parameter is
greater than zero. Both are now classes with a private constructor and a
factory returning `std::expected`, so a value outside the bound cannot be
constructed and no function downstream has to decide what to do about one.

Three consequences follow, and they are the decision as much as the rule is.

**`OrbitError::NonPositiveGravity` is gone**, with the five checks that returned
it. An error a caller cannot receive is dead defensive code, which
[`0002`](0002-error-handling-strategy.md) argues against, and an enumerator no
code can return is a lie in a header — the reason `ParabolicElements` was
removed on 2026-09-12.

**`orbitInfo` keeps its plain return.** `mu > 0` was its only guard; everything
else in it is computed to stay finite deliberately or returns infinity on
purpose. It was ruled earlier the same day that it should return
`std::expected`, and that was reversed once the value became unrepresentable,
because the error channel would carry nothing (decision 114).

**`GravParam` keeps its quantity.** It holds a `Scalar<m³/s⁻²>` and exposes it
through `quantity()`, so `mu.quantity() / (r * r)` is still an m·s⁻² quantity.
`Eccentricity` holds a bare `f64`, having no arithmetic role in any code or
task document.

## What we considered

**Reporting from every entry point, as three of five already did.** This is the
smaller change and it keeps both types in the dimension system. It was rejected
because it answers the symptom: the same condition would still be checked in
five places, and the three converters that checked it nowhere would need a
fourth answer invented for them. `VERIFICATION.md` rule 24 — prefer the bug you
cannot write — is the project's stated order of preference over rule 7's
report-or-assert, and this is the case it describes.

**Asserting in the three converters, matching their sibling.** Two lines, and it
removes the ambiguity. Rejected because a `Debug` build would then abort on
scenario data, which is the wrong half of 0002's split and the defect the fuzzer
found in `propagate`'s postcondition on 2026-09-07.

**Validating `GravParam` as a plain class**, as `DeltaUt1` and `DeltaT` are. This
was the ruling for several hours, with its cost accepted in writing: it
supersedes 0019's `mu / (r*r)` clause and M1-62's planned named failure for a
non-positive `mu`. It was then found to be avoidable — `Scalar<R>` *derives
from* `mp_units::quantity<R, f64>`, so a class that holds the `Scalar` keeps the
algebra — and the cheaper design was put up rather than absorbed (decision 110).

**Leaving `Eccentricity` a kind and validating at the orbit boundary.** Rejected
for the same reason as the first option, and because the kind's only job was
non-convertibility, which a class does more strongly.

## Why

**The finding was measured, not argued.** A probe built against the real
`orbsim_core` on 2026-09-21:

```
e = -0.5000 | trueToEcc finite  +1.127589 | eccToTrue finite  +0.415419
e = -1.5000 | trueToEcc NaN     -nan(ind) | eccToTrue NaN     -nan(ind)
e = +0.5000 | trueToEcc finite  +0.415419 | eccToTrue finite  +1.127589
```

A negative eccentricity between -1 and 0 returned a finite, plausible, wrong
number — and each converter returned the *other's* answer for `+0.5`, because
the `sqrt((1-e)/(1+e))` factor inverts. Below -1 a NaN came back as a valid
`Radians`. Nothing reported, nothing asserted: the "silently coping" third
option `VERIFICATION.md` rule 7 names, four lines from a sibling that asserted
the same condition.

**What it cost to fix is small and was measured first.** 56 construction sites,
3 of them in `src/`. Neither type was used in the unit algebra in `src/` — and
one site in `tests/` was, `specificEnergy`'s `mu / length(sv.pos)`, whose own
comment records that dropping mu's unit had once left the expression
"numerically right ... and unprovable". That site is why `quantity()` exists
rather than being a concession to a future task.

**What it bought is that the assertion count says so.** 1,369,392 before,
1,369,438 after: minus the four assertions in `test_orbit.cpp` and the one in
`test_orbit_scales.cpp` that can no longer be written, plus the 51 in the new
`tests/test_units_validated.cpp`. Every assertion accounted for, and nothing
else moved — which is what says a refactor of this size changed no behaviour.
Decision 96's commit is the precedent for reading the count that way.

## What this record does not decide

- **The other seven units.** `Metres`, `Seconds` and the rest have no physical
  bound to check. This is not a pattern to apply by default; it is the answer
  for a value whose meaning makes some numbers impossible.
- **`stateFromElements`'s `p > 0`.** It derives from `el.slr`, `el.sma` and
  `el.ecc`, so validating the eccentricity does not close it, and `slr` and
  `sma` are `Metres`. It remains an assertion, and M1-87's document names it as
  the next question in this family if a scenario loader ever supplies elements
  directly.
- **How M1-62 types its accelerations.** `quantity()` gives it what 0019
  promised; whether the force model wants a named `Acceleration` alias as well
  is that task's to settle.
