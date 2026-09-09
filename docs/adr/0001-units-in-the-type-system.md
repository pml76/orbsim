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

## Update, 2026-09-08: counts get an integral base beside `Quantity`

`Quantity<Derived>` holds an `f64`, which is right for every unit this record
names and wrong for the ones that arrive with the renderer. A texel count, a
mip level and a cache budget in mebibytes are **counts**: integral, with no
meaningful fractional part. An `f64` base invites a division that silently
truncates, or a value that is 2047.9999 texels wide.

So `core/Scalar.hpp` gains **`Count<Derived>`**, the integral sibling: one
`std::uint32_t`, explicit construction, no implicit conversion in either
direction, comparison, addition and subtraction of the same type, and
multiplication by an unsigned scale. No division that could truncate -- a ratio
of counts is a named function returning `f64`. The same CRTP shape and the same
`friend Derived` trick, so `struct Other : Count<Texels>` does not compile.

`Texels` and `Mebibytes` are `Count`s. **`Pixels` stays on the `f64`
`Quantity`**, because a screen-space error threshold of 2.5 px is a real
quantity rather than a count, and rounding it to 2 or 3 would change what the
quadtree does.

This closes the question [`0007`](0007-render-quality-is-a-struct.md) left
open. Decision 19 of [the register](../plan/milestone-1-decisions.md); built by
[M1-12](../plan/tasks/m1-12-render-quality.md).
