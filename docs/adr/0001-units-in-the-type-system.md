# ADR 0001: Physical quantities are types, not doubles

Status: accepted (2026-09-05; supersedes the example's ADR of the same name)

## Decision

Every physical quantity that crosses an interface in `src/` is a distinct
type: `Radians`, `Degrees`, `Metres`, `Seconds`, `MetresPerSecond`,
`RadiansPerSecond`, `SpecificEnergy`, `Eccentricity`, `GravParam`, and the
test-side `Tolerance`. Each is a struct deriving from `Quantity<Derived>` in
`core/Scalar.hpp`, which holds one `f64`, an `explicit` constructor,
comparison, and the arithmetic that preserves the unit: same-type sum and
difference, negation, scaling by a plain number. Conversions between units are
named functions (`toRadians`, `toDegrees`). Nothing converts implicitly in
either direction, and nothing produces a different unit from two others.

Vectors stay `Vec3` of `f64`. A vector's unit is a property of what it
represents, and that is carried by the struct holding it (`StateVector::pos`
is metres, `StateVector::vel` is metres per second).

## What we considered

**Bare `f64` with naming conventions.** What most simulation code does: call
the parameter `meanAnomalyRad` and rely on discipline. It fails in exactly the
case that matters, because the compiler cannot read a name.
`propagate(f64 mu, f64 dt)` accepted its arguments transposed and said
nothing. This is what the project started with.

**Nine hand-written structs.** Where the project went next, and what this
record replaces. Correct, but each type was eight identical lines, and adding
a unit meant copying them and hoping the copy was right. At nine types the
copy-paste was already the largest block of code in the header.

**A units library** -- `strong_type`, `NamedType`, `type_safe`. Any of these
generates what `Quantity` generates. Against them: a dependency for sixty
lines, and a vocabulary of operators and mixins to learn before reading a
`Radians`. Revisit if unit-changing arithmetic (`Metres / Seconds` yielding
`MetresPerSecond`) becomes wanted, because that is where a library's
dimensional analysis starts paying.

**Full dimensional analysis** (`mp-units`). Genuinely powerful and a much
larger commitment: it changes how every expression is written, not just how
interfaces are declared. Out of proportion to what the simulator needs today.

## Why

In 1999 the Mars Climate Orbiter was destroyed because one team's software
produced impulse in pound-force seconds while another's expected
newton-seconds. Nobody was careless. The types simply did not carry the
information, so nothing could catch it. That is this project's problem domain.

The runtime price is zero: `sizeof(Radians) == sizeof(f64)` is a
`static_assert`, and at `-O2` the strong-typed and bare-double forms compile
to the same instruction. The price is paid in typing, once, and `Quantity`
made it four lines per unit.

The benefit is not only the units. Making the types distinct also solves
I.24 -- adjacent parameters that transpose silently -- as a side effect, which
is what `bugprone-easily-swappable-parameters` was flagging before the types
existed. And it made a real defect visible: the universal-variable
propagator's convergence tolerance was an absolute number in square-root
metres, and its conic thresholds were in reciprocal metres. Neither had a
type, and both were wrong at heliocentric scale (see the commit that made
`propagate()` scale-free). A quantity with a unit invites the question "in
what?"; a bare `1e-12` does not.