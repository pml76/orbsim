# ADR 0003: The dimension system is mp-units, not six hand-written structs

Status: accepted (2026-09-17)

Supersedes the *mechanism* of [`0001`](0001-units-in-the-type-system.md). Its
decision — that every physical quantity crossing an interface is a distinct
type — stands and is stronger now. What changed is what those types are built
on.

## Decision

`core/Units.hpp` defines one class template, `Scalar<kReference>`, over an
mp-units quantity. The six names — `Radians`, `Degrees`, `Metres`, `Seconds`,
`Eccentricity`, `GravParam` — are aliases of it. `core/Vec3.hpp` defines
`Vec3<kReference>` whose three components are `Scalar<kReference>`, so a
position and a velocity are different types and do not add.

mp-units is fetched by `CMakeLists.txt` at an exact tag, `v2.5.0`, licence MIT.

## Why

The parent project made the same change on the same day for its own reasons —
[`../../../docs/adr/0019`](../../../docs/adr/0019-vectors-carry-their-unit.md)
has the measurements. This example exists to show how that project writes C++,
and an example demonstrating a mechanism the real code has stopped using is
worse than no example.

There is also a reason that belongs here rather than there. The six structs
were **six copies of the same eight lines**, and section 17 of the guidelines
is about exactly that: the code you have to change in six places is the code
you will change in five. One template and six aliases is the shape that rule
asks for, and adding a seventh unit is now one line.

## What it cost

**The example is no longer dependency-free.** It was: `cmake -S . -B build` and
nothing else, which is what made it easy to copy somewhere and poke at. Now the
first configure fetches mp-units and therefore needs a network. That is a real
loss and it is the reason this record exists rather than a commit message.

It was accepted because the alternative was worse in a way that compounds: an
example whose units are hand-rolled teaches a reader to hand-roll units, and
then they read `src/` and find something else. A dependency is a one-time cost
to whoever builds this; a misleading example is a cost to everyone who reads it.

**Two things in the guidelines had to be re-stated rather than re-proved.**

- `!std::is_constructible_v<Radians, Eccentricity>` in `tests/test_units.cpp`
  became **true** while `Radians{someEccentricity}` still refused to compile.
  mp-units gives every dimensionless quantity an `explicit operator V_()` for
  any `V_` constructible from its representation; the constraints are satisfied,
  so the trait answers yes, and the body then fails. Neither a trait nor a
  `requires`-expression can see an error in a function body. The assertion now
  measures implicit convertibility, which is the dangerous direction and is
  still measurable — and the comment there says why, because *a check that
  silently stops checking looks exactly like one that passes* (rule 23 of the
  parent's VERIFICATION.md).
- `Degrees + Radians` compiles under a plain mp-units alias, because both are
  the angle dimension and mp-units' own `operator+` is found by
  argument-dependent lookup. `Scalar<>` deletes the cross-unit overloads to stop
  it. Not writing an operator is not the same as deleting one.

## What we considered

**Keeping the six structs, and pointing at the parent.** Cheapest, and it keeps
the example buildable on a machine with no network. Rejected: the example's
whole claim is that every section of the guidelines is followed and none
violated, and section 17 is violated by six copies of eight lines the moment a
one-template alternative is known to work.

**au instead of mp-units.** A third the compile-time cost, and its per-unit
headers would suit a small example well. Rejected for the same reason the parent
rejected it: it cannot express a *kind*, and `Eccentricity` — dimensionless, and
still not interchangeable with any other ratio — is precisely a kind. That
property is one of the things this example is here to demonstrate.

**Writing our own dimension system.** Exponents of M, L and T as template
parameters is perhaps eighty lines and would keep the example dependency-free.
Rejected: it would be the largest piece of machinery in an example about
writing small, clear code, and it still could not express kinds.
