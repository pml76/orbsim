# M1-10 — Reverse-Z with an infinite far plane

Phase: A | Status: **done, 2026-09-21**
Prerequisites: M1-09
Decided by: [ADR 0003](../../adr/0003-reverse-z-depth.md), [ADR 0012](../../adr/0012-orbsim-view.md)

**Corrected 2026-09-20, with M1-09**, which built the type this task returns.
Nothing about the decision changes; two things about the wording do.

## Purpose

ADR 0003 has been accepted since 2026-09-05 and exists so far only as a depth
format, a clear value and a comparison operator. The projection matrix that
makes it true is this task, and ADR 0003 names it: *"will be checked by the
phase A grid test"*.

The mechanism is easy to break by accident — a pipeline built with
`VK_COMPARE_OP_LESS` out of habit draws nothing — so the properties get asserted
rather than assumed.

## What to implement

`src/view/Projection.hpp`.

- `[[nodiscard]] constexpr Projection infiniteReverseZPerspective(Radians
  verticalFov, f64 aspect, Metres nearPlane)`, mapping the near plane to depth
  **1.0** and infinity to **0.0**, with no far plane at all. **`Projection`,
  not `Mat4`**: since [ADR 0020](../../adr/0020-transforms-carry-their-units.md)
  a `Mat4` carries the units of both spaces, and this one maps
  `Vec4<kView, metre, one>` to `Vec4<kClip, metre, metre>` -- which is why the
  divide lands in dimensionless normalised device coordinates, why
  `transformPoint` will not accept it, and why it composes only with a
  transform that *ends* in view space
  ([ADR 0021](../../adr/0021-transforms-carry-their-frames.md)).
- **The clip-space convention stated in the header**: right-handed view space
  looking down −z, Vulkan clip space with y downward and depth in [0, 1]. The
  y flip is in the matrix, once, where it can be seen — not scattered through
  the shaders as a sign somebody has to remember.
- Preconditions reported, not asserted, because a scenario or a configuration
  can produce them: a non-positive near plane, a non-positive or non-finite
  aspect, a field of view outside (0, π).
- A comment saying **why**, briefly, with the pointer to ADR 0003 — because the
  next reader's instinct will be to "fix" the reversed depth.

## Out of scope

The view matrix and the camera (M1-11). Pipeline depth state (M1-13). Anything
that reads the matrix.

## Tests

`tests/test_projection.cpp`.

- **The near plane maps to exactly 1.0**, and a point at ten times the near
  distance maps to 0.1 — analytic values, computed by hand in the test. The
  depth comes out through `transform()` and `perspectiveDivide()`, both of
  which M1-09 built for this.
- **Infinity maps to 0**: a point at 1e13 m gives a depth under 1e-12.
- **Monotonic**: depth strictly decreases with distance across a sweep from
  0.1 m to 1e13 m. A reversed comparison or a sign slip fails here.
- **The precision claim ADR 0003 is built on**, which is the test worth writing
  carefully: two points one metre apart at 1000 km from the camera produce
  **distinct `f32` depth values**, with margin. The same check under a
  conventional 0→1 projection with a far plane at 1e9 m does *not* — assert
  that too, in the same test, so the comparison is on the record and the test
  demonstrably has teeth.
- **Field of view and aspect**: a point on the edge of the frustum lands on
  ±1 in normalised device coordinates for several aspect ratios, checked
  against `tan(fov/2)` computed independently.
- **Named failures**: zero near plane, negative aspect, a field of view of π.

## Verification

The standing rules. No GPU yet — every claim here is arithmetic and is checked
on the CPU.

## What the numbers turned out to be

**Amended 2026-09-21, after the work**, by register decisions 99-105. Four of
this document's statements did not survive measurement, and they are left
above as written with the corrections here, because what a plan assumed is
worth keeping beside what turned out to be true.

- **`constexpr` is not achievable** (decision 99). clang and MSVC both refuse
  `std::tan` in a constant expression; gcc-14 accepts it. The entry point is
  `inline`, and a `constexpr` helper takes the already-computed focal length so
  the *layout* carries seven `static_assert`s. Six of fourteen mutants then die
  at compile time.
- **`f64 aspect` became `Aspect`** (decision 100), a dimensionless *kind* of
  its own, because a bare `f64` across an interface is non-negotiable 1 and the
  document predates ADR 0019.
- **"ten times the near plane maps to 0.1" is not exact** (decision 103):
  measured worst 1 ulp over 200,000 near planes, so the budget is 2 ulp. The
  frustum edge is 4 ulp, from a measured 2.0.
- **"a point at 1e13 m gives a depth under 1e-12" holds only below a 10 m near
  plane** (decision 102) -- the figure is 10/1e13. What is asserted is the rule
  the projection implements, `depth == nearPlane / distance` bit for bit, with
  the document's instance kept beside it at a stated near plane.
- **The near-plane case must not run at 1 m** (decision 101), where `n/n` and
  `n*n` agree, so it would accept the very mutant M1-09 handed this task.

## Done when

- [x] `check` green in both trees. 160 CTest entries, up from 150;
      **1,369,392 assertions in 155 cases**, of which `test_projection`
      contributes 21,643 in 10.
- [x] The depth-precision comparison against conventional depth is in the
      suite, with both sides measured: 9-14 f32 ulp of separation for
      reverse-Z against **bit-identical** for the conventional control.
- [x] The clip convention is stated once, in the header, and the y flip lives
      only in this matrix -- asserted **signed**, since a magnitude check
      cannot tell a missing flip from a doubled one.
- [x] ADR 0003 is referenced from the code and carries a dated update saying
      the arithmetic half is checked and the GPU grid test is not.
- [x] The mutation pass ran: **fourteen mutants, fourteen caught, none
      surviving, none invalid**, six at compile time. It killed M1-09's
      declared survivor and corrected two claims that had been written down
      without being measured -- one in M1-09's record and one in this task's
      own test.
