# ADR 0001: Angles, lengths and durations are types, not doubles

Status: accepted

## Decision

Every physical quantity crossing a public interface is a distinct type --
`Radians`, `Degrees`, `Metres`, `Seconds`, `Eccentricity`, `GravParam` -- each a
struct wrapping one `f64` with an `explicit` constructor. Conversions between
them are named functions (`toRadians`, `toDegrees`). Nothing converts
implicitly, in either direction.

## What we considered

**Bare `f64` with naming conventions.** Cheapest, and what most simulation code
does: call the parameter `meanAnomalyRad` and rely on discipline. It fails in
exactly the case that matters, because the compiler cannot read a name.
`solveKepler(f64 meanAnomaly, f64 ecc)` accepts its arguments transposed and
says nothing.

**A units library** -- `strong_type`, `NamedType`, or `type_safe`. Any of these
would generate the boilerplate, including arithmetic operators, and would be the
right answer at scale. Six hand-written types is under the threshold where the
dependency pays for itself; sixteen would not be. Revisit this when the count
grows.

**Full dimensional analysis** (`mp-units` or similar), where `Metres / Seconds`
yields a velocity type automatically. Genuinely powerful and a much larger
commitment: it changes how every expression is written, not just how interfaces
are declared. Out of proportion to what this example needs.

## Why

In 1999 the Mars Climate Orbiter was destroyed because one team's software
produced impulse in pound-force seconds while another's expected newton-seconds.
Nobody was careless. The types simply did not carry the information, so nothing
could catch it. That is the same problem domain as this code.

The cost is about eighty lines in one header and it is entirely `constexpr`.
The check in `tests/test_units.cpp` confirms the strong-typed and bare-double
forms produce bit-identical results, and at `-O2` the abstraction compiles to
the same single `mulsd` instruction. The runtime price is zero; the price is
paid in typing, once.

The benefit is not only the units. Making the types distinct also solves I.24 --
adjacent parameters that transpose silently -- for free, which is what
`bugprone-easily-swappable-parameters` was flagging in `nearlyEqual` before the
`Tolerance` type existed.
