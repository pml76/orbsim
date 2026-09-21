# ADR 0020: A transform carries the units of both spaces it maps between

Status: **accepted** (2026-09-20), implemented the same day in
[`src/view/Mat4.hpp`](../../src/view/Mat4.hpp) with
[M1-09](../plan/tasks/m1-09-orbsim-view.md).

Extends [`0019`](0019-vectors-carry-their-unit.md) from vectors to the
transforms that act on them. It supersedes nothing: [`0012`](0012-orbsim-view.md)
decides *where* this maths lives, and this decides what its principal type is.

Decision 92 of [the milestone 1 register](../plan/milestone-1-decisions.md).

## What is being decided

Whether `Mat4` is sixteen bare `f64` with typed edges, or a type that carries
the units of the spaces it maps between.

[M1-09](../plan/tasks/m1-09-orbsim-view.md) was written on 2026-09-08 and says
"a 4×4 `f64` matrix". That was **nine days before** 0019 made `Vec3` carry its
unit, and non-negotiable 1 says never a bare `f64` across an interface. The
question had to be re-asked.

## The fact that shapes the answer

**A homogeneous 4×4 has no single unit.** Write a projective point as a `Vec4`
whose xyz carry one reference and whose w carries another; the affine point it
denotes is `xyz / w`, so the *ratio* is what is geometrically meaningful and
the *pair* is what fixes the matrix entries. A matrix mapping
`Vec4<a, b>` to `Vec4<c, d>` then has four blocks in four different references:

| block | rows, columns | reference |
|---|---|---|
| linear | 0–2, 0–2 | `c / a` |
| translation | 0–2, 3 | `c / b` |
| bottom row | 3, 0–2 | `d / a` |
| corner | 3, 3 | `d / b` |

That assignment is **closed under multiplication**: each block of a product
comes out in its own reference from both of its two terms, which is what makes
a chain of transforms typeable at all.

It is also why the obvious simplifications do not work, and both were tried on
paper before the spike:

- **One reference for the whole matrix** cannot hold. The near-plane entry of
  a perspective matrix is in metres while its linear block is dimensionless.
- **Fixing w dimensionless** and templating on the affine unit alone cannot
  hold either: with w dimensionless, clip z would have to be dimensionless
  too, so the near-plane entry would need dividing by an arbitrary reference
  length. Under this parameterisation clip space is honestly
  `Vec4<metre, metre>`, and the divide lands in dimensionless normalised
  device coordinates without inventing anything.

## Decision

`Mat4<kInXyz, kInW, kOutXyz, kOutW>`, with the four block references derived
rather than declared, storage a private column-major `std::array<f64, 16>`,
and typed accessors named after the blocks: `linear(Row, Column)`,
`translation(Row)`, `bottomRow(Column)`, `corner()`.

Two aliases carry the milestone:

```cpp
using Transform  = Mat4<metre, one, metre, one>;    // world or view, affine
using Projection = Mat4<metre, one, metre, metre>;  // world -> clip
```

**Element access is by named block, not by one `at(row, column)`.** Under this
parameterisation a single two-index accessor cannot have one return type, and
neither can `column(i)` — columns 0–2 and column 3 differ. The indices are
strong types (`Row`, `Column`) so a transposition is a compile error, which
matters because **clang-tidy cannot catch it**: measured 2026-09-20,
`bugprone-easily-swappable-parameters` silences any pair used together in one
expression, and `elements_.at((column * 4) + row)` is one expression. That
blind spot was closed project-wide in its own commit the same day; the strong
types stay regardless, because they are the narrower instrument.

## What the spike measured

Built before this record was written, and the reason it is accepted rather
than proposed.

**It compiles, and agrees to the digit, on all three front ends** — clang
23.1.0 under `-Weverything -Werror` with zero clang-tidy findings in the
header, gcc-14, and MSVC 19.51 under `/W4 /WX /permissive-`.

**What it rejects at compile time**, each inverted and watched failing before
being believed:

- `view * projection` — the classic bug — does not compile; `projection * view`
  does, and yields a world-to-clip `Projection`;
- `transformPoint(projection, position)` does not compile: a projection must
  go through `transform()` and the perspective divide;
- a clip point cannot be projected a second time;
- a projection has no `inverseRigid`;
- a transform of velocities neither composes with one of positions nor moves a
  `Position` — 0019's guarantee, one level up.

**What it costs**: +0.27 s per translation unit, 3.68 s to 3.95 s, about 7%.

**Two things the compiler taught the design.** A deduced `auto` return type
defeats NRVO and `-Wnrvo` is an error here, so the transpose's type is spelled
as an alias template. And `Eccentricity` is *not* `Scalar<one>` — it is its own
kind and deliberately converts to nothing — so the homogeneous coordinate of an
ordinary point needed a name of its own, `Dimensionless`, local to the header.

## What we considered

**Sixteen bare `f64` with typed edges.** `transformPoint(const Mat4&, const
Position&) → Position`, and the elements a documented exception. Simplest, and
it satisfies the letter of non-negotiable 1, because a `Position` crosses the
interface and not three doubles. What it does not buy is any check on the
*chain*: `view * projection` compiles, and so does applying a projection with
`transformPoint`, which is the bug that actually happens.

**Templating on the translation column's reference alone.** Correct for affine
transforms and broken by the first projection, which would then need a second,
different matrix type.

**No general `Mat4` at all** — a `RigidTransform{Quat, Position}` and a
separate `Projection`, composed explicitly, with a 4×4 only at the GPU
boundary. The most typed and the cheapest, and genuinely tempting under
`CODING_GUIDELINES` section 17. Rejected because 0012 and four later tasks name
`Mat4`, and the quadtree and the MFD want a general matrix before this
milestone ends.

## What this record does not decide

- **Frames.** This types the *units* of the projective chain, not the spaces:
  world and view are both metres, so a model matrix and a view matrix are one
  type and `V * M` compiles either way. 0019 left frames open deliberately and
  milestone 1's scope fence (register section 6) keeps them out. The signature
  admits two more parameters whenever that changes, and nothing here has to be
  unpicked first.
- **Whether the storage stays private.** `columnMajor()` hands out a copy
  today because a reference would need `[[clang::lifetimebound]]`, whose macro
  lives in `render/VulkanHandle.hpp`. M1-11's narrowing is the first real
  caller and the place to revisit it.
- **The projection matrix itself**, which is [M1-10](../plan/tasks/m1-10-reverse-z-projection.md),
  and the camera, which is [M1-11](../plan/tasks/m1-11-camera.md). This record
  fixes the container they live in.

## The frames clause is superseded by 0021, 2026-09-20

The first bullet above did not survive the day it was written.
**[`0021`](0021-transforms-carry-their-frames.md) supersedes it**: the owner
asked for frames, and for points to carry them too, so `Mat4` took the two
further template parameters this record said its signature admitted, and
nothing here had to be unpicked. Everything else in this record stands — the
four unit parameters, the derived block references, and the compositions they
accept and refuse.

0021 also replaced this project's formulation of the transpose with the
owner's, as a **dual** map, which is what makes
`transpose(AB) = transpose(B)transpose(A)` typecheck for a chain containing a
projection rather than only for affine matrices. Runtime cost was measured at
nil before the design was chosen: 35 instructions against 35, identical at
`-O2`.

*(Appended 2026-09-21. 0021 named this record from the day it was accepted;
this record did not name 0021 back, and the index recorded it as plain
"accepted" — against the rule in [`README.md`](README.md) that a superseding
record and the one it supersedes both link to each other. 0009 and 0016 are
the worked example of that rule: 0009 carries its own "Partly superseded by
0016" section **and** the index says so. Nothing decided here has changed.)*
