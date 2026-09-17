# ADR 0019: Vectors carry their unit

Status: **proposed** (2026-09-17) — the owner has asked for units on `Vec3`;
this record is the shape of that change and the decision it forces, and nothing
moves until it is accepted.

Supersedes one paragraph of [`0001`](0001-units-in-the-type-system.md), which
ruled the other way: *"Vectors stay `Vec3` of `f64`. A vector's unit is a
property of what it represents, and that is carried by the struct holding it."*

## What is being decided

Whether `Vec3` carries its unit in its type, and — because the two cannot be
separated — whether this project adopts **compile-time dimensional analysis**.

They cannot be separated because of one operation. `h = r × v` is the specific
angular momentum, and its unit is **m²/s**. There is no `MetresSquaredPerSecond`
in `core/Units.hpp` and there should not be, because the next such product is
`mu/r²` and the one after that is `r v²/mu`. A vector that knows it holds metres
immediately demands a rule for what `cross` of metres and metres-per-second
gives back, and that rule is a dimension system or it is nothing.

This is [`../VERIFICATION.md`](../VERIFICATION.md) rule 17 arriving from the
other side. Rule 17 is listed there as *undecided*; this record is where it gets
decided, because `Vec3<Metres>` cannot be built without answering it.

## Why now, and why this is the cheapest it will ever be

Measured 2026-09-17, `Vec3` appears **104 times in `src/` and `tests/`**: 47 in
`core`, 36 in `orbit`, 21 in the suites. **Zero in `src/render/` and
`src/app/`** — the renderer has never drawn a position, so the `f64` → `f32`
boundary that will narrow vectors does not exist yet.

Every phase makes that worse and the early ones make it worse fastest. Phase A
adds `orbsim_view`, a camera and a projection. Phase C adds a quadtree whose
whole job is positions. Phase E adds a force model, a stepper and a state
larger than six doubles. Phase F draws the track, which is where positions
first cross into `f32`.

ADR 0006 also sharpened the argument after 0001 was written: with a real epoch
and real reference frames, **the same three doubles can be barycentric,
geocentric or perifocal**, and the type system currently cannot tell those
apart at all. A wrong frame is a much likelier bug than a wrong unit, and it is
invisible to everything this project currently does.

## The options

### A. Status quo — `Vec3` of `f64`, unit on the holder

What 0001 decided. `StateVector::pos` is metres because its comment says so.

Costs nothing, catches nothing. The defect it cannot see is a velocity passed
where a position belongs, which compiles and flies.

### B. `Vec3<Unit>` over the existing named types

`Vec3<Metres>`, `Vec3<MetresPerSecond>`. Addition, subtraction, scaling and
rotation all work and read well.

**It breaks on `dot`, `cross` and `lengthSq`**, which is not a corner: those
three are how the orbital elements are computed. `cross(Vec3<Metres>,
Vec3<MetresPerSecond>)` has no type to return. The options within B are to
return a bare `f64`/`Vec3` there — which loses the unit at exactly the step
where the 1 AU bug lived, a tolerance in √metres against an absolute number —
or to hand-write a named type per product, which is a dimension system with the
inference done by a person.

### C. `Vec3<Dim>` over a compile-time dimension system

Dimensions as exponents of mass, length and time carried as template
parameters, so `Metres` is `Quantity<Dim<0,1,0>>` and `cross` returns
`Vec3<Dim<0,2,-1>>` without anyone naming it. Products and quotients compose;
`mu / (r*r)` produces an acceleration *type*; adding it to a velocity does not
compile.

This is the only option that answers the `cross` question rather than routing
around it. It is also the largest: every unit type in `core/Units.hpp` becomes
an alias, `nearlyEqual` and the matchers become templates, and the error
messages get worse — which is a real cost in a codebase whose suites are read
as a specification.

### D. Distinct vector types, hand-written

`Position`, `Velocity`, `AngularMomentum` as separate structs with only the
operations that make sense between them. No templates, excellent error
messages, and the conversions are named functions.

It is the option that scales worst: every new quantity is a new type and a new
set of operators, and milestone 1 alone adds accelerations, angular velocities
and torques.

## What is not decided here

**Frames.** A `Vec3<Metres>` still does not know whether it is barycentric or
geocentric. That is a second axis — a tag alongside the dimension — and folding
it in now would make this record about two changes rather than one. It is worth
saying out loud because it is the *more valuable* of the two and the argument
above leans on it: whichever option is chosen should leave room for a frame tag
rather than close it off.

**`Quat`.** A rotation is dimensionless and mostly unaffected. `rotate` must
preserve its argument's unit, which every option above allows.

**The `f32` boundary.** Non-negotiable 8 says the narrowing happens in one
named function after a camera-relative subtraction. Units make that function's
signature honest; they do not change where it is.

## Recommendation

**C, in two steps, and not before M1-04 lands.**

B is the tempting one and it is a trap: it looks smaller, and it pays for that
by dropping the unit at `cross` and `dot` — the exact operations the elements
conversion is built on, and the exact place a bare number went wrong once
already at 1 AU. A units system that switches itself off in the hardest
arithmetic is worse than none, because it reads as protection.

Two steps, because the second is where the risk is:

1. **The dimension system and the scalars**, with `core/Units.hpp`'s nine types
   becoming aliases. No `Vec3` change. The suites and the static_asserts prove
   the arithmetic is unchanged; the assertion count must not move.
2. **`Vec3<Dim>`**, then the 104 sites, `orbit/` last because it is the file
   with the measured error budgets. The cross-toolchain checksum in
   `test_orbit_scales.cpp` is the instrument: it pins the bits of an
   `elementsFromState` conversion, so if step 2 changes any number it will say
   so rather than leaving it to be noticed.

**Not before M1-04** because M1-04 and M1-06 are `core/Time.hpp` and a fixture
reader, neither of which touches `Vec3`, and because sequencing a large
refactor behind two small tasks costs nothing and de-risks both.

## What it would cost, honestly

- Roughly **104 sites** in `src/` and `tests/`, plus the worked example's 58,
  which must keep following every guideline or it stops being the reference.
- **Template error messages**, in a codebase that reads its suites as a
  specification. Concepts (`I.9`) are the mitigation and should be part of
  step 1, not retrofitted.
- **A real chance of changing a number.** Nothing about dimensions should alter
  an arithmetic result, and the checksum is there to prove it did not — but
  `orbit/` carries budgets measured to the ulp, and this record should not
  pretend the risk is zero.
- ADR 0001 needs a dated amendment pointing here, since its Vec3 paragraph
  stops being true.

## Consequences

`VERIFICATION.md` rule 17 stops being *undecided* and becomes either **done**
or **rejected with a reason** — it cannot stay open once this is answered.
`PROJECT_STATE.md` section 7 question 6 closes with it. And the frame question
above becomes the obvious next record, which is the point: the reason to do
this at all is that a frame tag is where the bugs that cannot currently be seen
are hiding.
