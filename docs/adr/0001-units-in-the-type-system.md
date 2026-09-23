# ADR 0001: Physical quantities are types, not doubles

Status: accepted (2026-09-05; supersedes the example's ADR of the same name),
**mechanism superseded by [`0019`](0019-vectors-carry-their-unit.md) on
2026-09-17**

The decision below — that every physical quantity crossing an interface is a
distinct type — stands, and is if anything stronger now. What changed is how.
The nine types no longer derive from a hand-rolled `Quantity<Derived>` holding
one `f64`; they derive from an mp-units quantity, so a dimension the code can
compute with sits underneath them. Two claims in this record are now false and
are left in place because the reasoning that replaced them is worth reading
against them:

- *"nothing produces a different unit from two others"* — `Metres / Seconds`
  now produces a `MetresPerSecond`, and that is the point of the change.
- *"Vectors stay `Vec3` of `f64`"* — reversed. `Vec3<R>` carries its unit in
  its type, and `cross(r, v)` is m²/s without anything having to name that
  unit.

`Tolerance` is the one type still built on `Quantity<Derived>`, because it is a
parameter of a comparison rather than a measurement of anything.

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

## Update, 2026-09-23: `Count` is built, and one sentence above is no longer true

[M1-12](../plan/tasks/m1-12-render-quality.md) built the paragraph above.
`Count<Derived>` is in `core/Scalar.hpp` with `Texels` and `Mebibytes` beside
it (decision 123), and everything that paragraph asks for is there. Two things
about it are worth recording here, where the type system lives.

**"`Pixels` stays on the `f64` `Quantity`" is wrong, and has been since
2026-09-17.** The *reason* stands — a screen-space threshold of 2.5 px is a
real quantity and rounding it to 2 or 3 would change what the quadtree does —
but [`0019`](0019-vectors-carry-their-unit.md) moved every type in
`core/Units.hpp` onto mp-units that day, and `Quantity<Derived>` now carries
only `Tolerance`, which takes no part in dimensional analysis. A `Pixels` on
that base would be the one dimensional unit in the header standing outside the
dimension system. The register carried this correction as a note on decision 19
from 2026-09-21; this is the record catching up with it.

**`Pixels` has a dimension of its own** — `units::kPixelDimension`,
`kScreenLength` and `kPixel` — which is the mechanism mp-units itself uses for
the **angle**, formally dimensionless in SI and given `dim_angle` regardless.
This project already depends on that: `Radians` is built on
`angular::radian`, and it is why an angle cannot be built from a ratio.

*(A **kind** was tried first, for one day, and did not do the job — decision
137. A kind restricts implicit conversion and, by design, permits explicit
construction, so `Pixels{someRatio}` was legitimate mp-units on every front
end. It looked refused under clang and MSVC, and that was an accident of
`Scalar<>`'s deleted conversion operator resolving differently per front end
rather than anything a kind promised. The second toolchain is what found it,
and the numbers are in the register row and beside the type.)*

*(A consequence worth writing down, because the obvious assertion is
vacuous: `Eccentricity` was this header's other dimensionless quantity, so
`!is_constructible_v<Pixels, Eccentricity>` passes whether or not a pixel is
separate from anything — a class is not constructible from a quantity either
way. The assertions that carry the claim compare `Pixels` against
`Scalar<one>` and against the ratio of two lengths. Found while preparing
M1-12's mutation pass, which is what that pass is for.)*

**A count refuses to wrap rather than wrapping** (decision 124). The paragraph
above says "no division that could truncate" and stops there; addition,
subtraction and multiplication can all wrap round, and `Texels{1} - Texels{2}`
would otherwise be 4,294,967,295. The mechanism is a call, on the wrapping
branch, to a function that is deliberately not `constexpr`, so the expression
stops being a constant expression and the build fails — in every tree, and at
no run-time cost, both measured on three front ends before the shape was
chosen. The register row has the numbers.

## Update, 2026-09-17: the Vec3 paragraph is under review

The paragraph above -- *"Vectors stay `Vec3` of `f64`. A vector's unit is a
property of what it represents, and that is carried by the struct holding it"* --
is the one part of this record the owner has asked to revisit.
[ADR 0019](0019-vectors-carry-their-unit.md) is the proposal, and it is
*proposed* rather than accepted: nothing has changed yet.

The argument that reopened it is not that the paragraph was wrong when written.
It is that [ADR 0006](0006-simulation-not-sandbox.md) changed what a vector
means here. With a real epoch and real reference frames, the same three doubles
can be barycentric, geocentric or perifocal, and the holder carries the unit but
not the frame. 0019 also records why the change cannot be small: `cross` of
metres and metres-per-second is m^2/s, so a vector that knows its unit forces a
dimension system, which is `VERIFICATION.md` rule 17.
