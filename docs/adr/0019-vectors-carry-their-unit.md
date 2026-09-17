# ADR 0019: Vectors carry their unit

Status: **accepted** (2026-09-17), both steps done, in two commits on the same
day. `core/Units.hpp`'s nine types are built on mp-units `v2.5.0`, and `Vec3<R>`
is templated on an mp-units **reference** so that `cross(r, v)` is m²/s with no
named type needed.

Step 2 was briefly re-opened between the two commits, and the reason is the most
useful thing in this record: the precondition spike found that **mp-units does
not provide `vector_product` on quantities at all** — not in `v2.5.0`, not on
master 102 commits later — which appeared to remove most of what step 2 was to
buy. It does not, because templating `Vec3` on the *reference* rather than on a
quantity leaves the unit algebra (`R1 * R2`) to the library and keeps only the
vector operations here. That was worth finding out by building it rather than by
reasoning about it. See
[What the precondition spike measured](#what-the-precondition-spike-measured),
which is the decisive section and was written after the rest.

Supersedes the whole of [`0001`](0001-units-in-the-type-system.md) on the
mechanism, not only the one paragraph noted below: the nine types no longer
derive from a hand-rolled `Quantity<Derived>`.

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

## What the field has already built, and the runtime question

**Surveyed 2026-09-17**, after the owner asked whether this can be done without
affecting the runtime and pointed at Boost.Units.

**The runtime question does not discriminate, and that is the useful answer.**
Every serious option here -- A through D above, and every library below -- is
compile-time only. Empty tag types carrying exponents, erased at instantiation.
This project has already proved it for its own scalars:
[`0001`](0001-units-in-the-type-system.md) and `CODING_GUIDELINES.md` section 2
both say *"at -O2 it compiles to exactly the same machine code as the bare
`double`"*, and section 21 tells you to go and look at the disassembly rather
than believe it. So "no runtime cost" is satisfied by all of them and chooses
between none of them.

**What actually differs is compile time, diagnostics, and whether the code is
ours to maintain.** Those are the axes this record should be decided on.

| | State, measured or read 2026-09-17 | Fit here |
|---|---|---|
| **[mp-units](https://github.com/mpusz/mp-units)** v2.5.0 | **MIT**. C++20 minimum, C++23/26 supported. *"No external dependencies, macro-free API, C++20 modules-ready, freestanding-capable."* Conan, vcpkg and a CMake package. ISO standardisation candidate — P1935 (2020) → P2980 (2023) → **P3045R8 (2026)**, targeting C++29 | The closest fit by far, and the only one that answers the `cross` question completely |
| **[Boost.Units](https://github.com/boostorg/units)** | **Dormant.** Measured from the remote: `boost-1.91.0`, `1.91.0.beta1`, `1.92.0` and `1.92.0.beta1` all point at the *same commit*, `f39b667d`. It has not changed across four Boost releases | C++03-era template metaprogramming, MPL-based. mp-units' own lineage describes it as the ancestor. Adopting it would mean taking on the error messages the modern libraries exist to fix |
| **[au](https://github.com/aurora-opensource/au)** 0.6.0 | Apache-2.0, C++14, single-header option, from Aurora. Publishes its own [comparison of alternatives](https://aurora-opensource.github.io/au/main/alternatives/) | Deliberately smaller and simpler than mp-units; dimension-level, not kind-level |
| **nholthaus/units** | Header-only, C++14, widely used | The least rigorous of the four; no kind system |

**The thing mp-units has that a hand-rolled dimension system would not.** It
models *kinds*, not merely dimensions: it can tell **torque from energy**, which
share `kg·m²/s²`, and Hz from Bq. That matters here more than it looks, because
this domain is full of same-dimension-different-meaning pairs — specific angular
momentum and kinematic viscosity are both m²/s. Option C as drafted above is a
dimension vector, and a dimension vector cannot see that distinction. It also
carries **point origins**, the affine distinction between an absolute position
and a displacement, which is adjacent to the frames problem this record defers.

**And the honest costs, from the same reading.** Compile time is the real one:
seconds per translation unit in header mode, in a project whose guidelines say
*"build time is a feature"* and which has nine translation units and rising.
The conceptual surface is large — quantity spec, kind, dimension, unit,
reference, point origin is a five-layer ontology, and the documentation is
excellent and long. And its zero-overhead claim is **structural plus Compiler
Explorer links rather than an in-repo benchmark suite**, which for this project
means the claim is to be *measured here* before it is repeated — working
agreement 4, and exactly what was done for `Quantity` already.

### E. Adopt mp-units

The option the survey adds, and it largely supersedes C and D. `core/Units.hpp`'s
nine types become aliases over `mp-units` quantities; `Vec3<Q>` is a small
wrapper this project still owns; `cross` and `dot` compose units without anyone
naming `m²/s`.

It follows the precedent the owner has already set twice: ERFA computes the
astronomy ([`0016`](0016-the-astronomy-is-erfa.md)) and Vulkan-Utility-Libraries
names `VkResult` values, both on the reasoning that a maintained implementation
beats a local one where the domain is well served. Dimensional analysis is such
a domain, and this one is heading into the standard.

Against it: it is by far the largest dependency this project would take —
compare it with the four surgical ones in `THIRD_PARTY.md` — and the first whose
types appear in *our* public interfaces rather than behind them. That is a real
difference in kind from SDL3 or Catch2, and it is the thing to weigh.

## Recommendation

**E — adopt mp-units — in two steps, and not before M1-04 lands.** Revised
2026-09-17 after the survey above; the first draft recommended C, a hand-rolled
dimension system, because it had not looked at what exists.

Three things moved it. The precedent is already set: this project takes the
maintained implementation where the domain is well served, which is why ERFA
computes the astronomy and Khronos names `VkResult` values. The **kind** system
answers something a hand-rolled dimension vector structurally cannot — telling
specific angular momentum from kinematic viscosity, both m²/s. And a library
heading for C++29 is one whose shape this code would eventually be rewritten
into anyway.

**But there is one measurement to take before accepting this**, and it is
cheap: build one translation unit of `orbit/` against mp-units and record what
it does to compile time, against the "build time is a feature" rule. Its own
zero-overhead claim should be checked the same way `Quantity`'s was — read the
disassembly. If the compile time is unacceptable, **au** is the fallback and C
is the floor; the two-step plan below is unchanged either way.

B is the tempting one and it is a trap: it looks smaller, and it pays for that
by dropping the unit at `cross` and `dot` — the exact operations the elements
conversion is built on, and the exact place a bare number went wrong once
already at 1 AU. A units system that switches itself off in the hardest
arithmetic is worse than none, because it reads as protection.

Two steps, because the second is where the risk is:

1. **The dimension system and the scalars**, with `core/Units.hpp`'s nine types
   becoming aliases — over mp-units under E, over our own exponents under C. No
   `Vec3` change. The suites and the static_asserts prove the arithmetic is
   unchanged; the assertion count must not move. Under E this is also where the
   compile-time measurement lands, and where the dependency gets its
   `THIRD_PARTY.md` row.

   **Done 2026-09-17, and it was not aliases.** Aliases would have dropped two
   invariants the old types asserted, so each type derives from an mp-units
   quantity instead and puts the house rules back on top — the section above
   says which and why. The count of edits was **446 `.value` sites**, every one
   of them becoming `.value()` because the number now lives in the base class:
   97 in `src/`, 349 in `tests/`. That is four times what this record implied
   step 1 would touch, and more than the 104 sites it estimated for step 2.
   The gate held: **642,205 assertions in 66 test cases, unchanged to the
   assertion**, and `check` green in all six configurations.

   `coding-guidelines-example/` was deliberately left alone. It is a separate
   CMake project with **its own** `core/Units.hpp` and no external dependency at
   all, and migrating it would give the worked example a `FetchContent` call it
   has never had. Whether the reference implementation should still demonstrate
   hand-rolled units once `src/` has stopped using them is an open question, not
   an oversight.
2. **`Vec3<Dim>`**, then the 104 sites, `orbit/` last because it is the file
   with the measured error budgets. The cross-toolchain checksum in
   `test_orbit_scales.cpp` is the instrument: it pins the bits of an
   `elementsFromState` conversion, so if step 2 changes any number it will say
   so rather than leaving it to be noticed.

   **Done 2026-09-17.** `Vec3<kReference>` carries `Scalar<kReference>`
   components, so `pos.x` is a `Metres` and not a bare double. The instrument
   held: **642,205 assertions in 66 test cases, unchanged**, the checksum
   included, and the same count under clang 23.1.0, MSVC 14.51 and gcc-14.

   Four things the change bought that were not on the list:

   - **`Degrees + Radians` compiled** after step 1 and does not now. mp-units'
     own `operator+` reached across the two types because both are the angle
     dimension; the deleted overloads in `core/Units.hpp` are what stops it.
     That hole shipped, and was found by asking the question rather than by
     anything failing.
   - **`==` on an unnamed result.** `length()` used to return an `f64` where
     `-Wfloat-equal` caught `==`; a raw mp-units quantity accepts it and
     silences the warning internally. Making the nine names aliases of one
     `Scalar<R>` template gives m²/s the same house rules `Metres` has.
   - **The Lagrange coefficients are typed.** `pos * f + vel * g` only balances
     if `g` is in seconds, and nothing said so; `g` is a `Seconds` and `fdot` a
     `PerSecond` now, and the compiler checks the four against each other.
   - **`specificEnergy` in the test support dropped `mu`'s unit** through a
     `.value()`, leaving the expression subtracting a reciprocal length from an
     energy. Numerically right, because the unit was restored by hand at the
     call site, and unprovable until the compiler could see it.

**Not before M1-04** because M1-04 and M1-06 are `core/Time.hpp` and a fixture
reader, neither of which touches `Vec3`, and because sequencing a large
refactor behind two small tasks costs nothing and de-risks both.

**Reversed for step 1 on 2026-09-17, on the owner's decision.** That reasoning
holds for step 2 and not for step 1: the argument is that M1-04 does not touch
`Vec3`, which is true, but M1-04 is the leap-second table and `Seconds` is one
of the nine types. Sequencing step 1 behind it would have meant writing
`core/Time.hpp` against the old `Quantity` and migrating it weeks later. Step 1
went first so M1-04 is written once, in the system it will live in. Step 2, if
it happens at all, still comes after.

## What the precondition spike measured

Run 2026-09-17, because this record made accepting it conditional on one
measurement. It found more than it went looking for, and two of the findings
changed the plan.

**The central claim holds.** Specific angular momentum and kinematic viscosity
are both m²/s, and mp-units keeps them apart: distinct types, neither
convertible to the other, proven by `static_assert` under all three compilers.
`au` has no equivalent — its `quantity.hh` contains no notion of kind at all.
This was the argument for mp-units over `au` and it survived contact.

**Zero overhead holds, read rather than believed.** vis-viva through mp-units
and through bare `f64` emit the same five instructions; only the `divsd`
schedules differently.

**But `vector_product` on quantities does not exist.** Not in `v2.5.0`, not on
`master` 102 commits later. The blog post that made the case for mp-units shows
`vector_product(position_vector, force)` on quantities; in the shipped code that
overload is a commented-out `TODO` in the `Vector` concept, and the operation
exists only on the bare `cartesian_vector`. A unit-carrying cross product is
therefore still ours to write. It is twelve lines and it is `constexpr`, but it
is the hand-rolling this record proposed to stop, so **step 2 goes back to
undecided.** `cartesian_vector` is also new in `v2.5.0` and has already moved
from `src/core` to `src/utility` on master and grown a second template
parameter — the type step 2 would build on is still churning.

**Compile time, the number this section was demanded for.** clang 23.1.0,
Windows, warm, best of three:

| Translation unit | |
|---|---|
| empty (the floor) | 0.20 s |
| `src/orbit/Orbit.cpp` before this change, 1,462 lines | 1.20 s |
| `au`, the five unit headers it would need | 1.68 s |
| mp-units `core.h` alone | 1.99 s |
| **mp-units `si.h` + `isq.h` + `cartesian_vector.h`** | **5.61 s** |

clang-tidy roughly doubles per translation unit, 4.03 s to 8.11 s. All ten of
this project's translation units reach `core/Units.hpp`, so all ten pay. In
practice `check` in `build/relwithdebinfo` went from **124 s for `lint` alone**
to **188 s for the whole target**, which is the honest figure to quote against
"build time is a feature". `au` costs about a third of mp-units here, because
it ships one header per unit; it was not chosen because it cannot express kinds.

**What it cost in suppressions: one compiler flag and nothing else.** `/utf-8`
is genuinely required — MSVC reads mp-units' UTF-8 unit symbols as the ANSI code
page and dies with 47 instances of C3872. Two other suppressions were approved
in advance and turned out to have no cause: `/wd4686` fires only on
`quantity_spec` arithmetic the nine types do not use, and the NOLINT for
`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` has no site
until `cartesian_vector` arrives with step 2. Neither was written.

**What the linter caught that review would not have.** mp-units declares its
storage without an initialiser and defaults its default constructor, so
`Metres m;` holds whatever was on the stack where the old hand-rolled base
zero-initialised. `cppcoreguidelines-pro-type-member-init` reported it against
the seven uninitialised members of `Elements`. `Unit`'s default constructor
zeroes explicitly, and the comment there says why.

**Two invariants had to be rebuilt rather than inherited.** mp-units provides
`==` on floating-point quantities — and silences `-Wfloat-equal` inside it —
and converts implicitly between units of one dimension. Both are reasonable in
a general-purpose library; neither is allowed here. `core/Units.hpp` derives
rather than aliases in order to delete `==` and to make `Degrees`-to-`Radians`
need two user-defined conversions, which the language refuses. This is why
step 1 is a facade and not the alias swap the plan below describes.

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

Under E, `THIRD_PARTY.md` gains a **decided, not yet pinned** row: mp-units,
**MIT**, arriving with step 1. MIT is the same licence as this project and the
same as vk-bootstrap and VMA, so it adds no new obligation — unlike
Vulkan-Utility-Libraries' Apache-2.0, which did.

`VERIFICATION.md` rule 17 stops being *undecided* and becomes either **done**
or **rejected with a reason** — it cannot stay open once this is answered.
`PROJECT_STATE.md` section 7 question 6 closes with it. And the frame question
above becomes the obvious next record, which is the point: the reason to do
this at all is that a frame tag is where the bugs that cannot currently be seen
are hiding.
