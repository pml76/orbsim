# M1-10 — Reverse-Z with an infinite far plane

Phase: A | Status: not started
Prerequisites: M1-09

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

- `[[nodiscard]] constexpr Mat4 infiniteReverseZPerspective(Radians verticalFov,
  f64 aspect, Metres nearPlane)`, mapping the near plane to depth **1.0** and
  infinity to **0.0**, with no far plane at all.
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
  distance maps to 0.1 — analytic values, computed by hand in the test.
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

## Done when

- [ ] `check` green in both trees.
- [ ] The depth-precision comparison against conventional depth is in the suite.
- [ ] The clip convention is stated once, in the header, and the y flip lives
      only in this matrix.
- [ ] ADR 0003 is referenced from the code and updated to say the check exists.
