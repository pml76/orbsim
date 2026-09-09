# M1-11 — The camera, and the f64 → f32 boundary

Phase: A | Status: not started
Prerequisites: M1-10
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

This is the task phase A's acceptance criterion is actually about. A vertex on
Earth's surface is 6.4e6 m from the origin; `f32` has about seven significant
digits, so narrowing that coordinate directly quantises it to roughly half a
metre and the whole planet visibly crawls as the camera moves. Subtracting the
camera position **in `f64` first** and narrowing only the small difference is
what removes it, and the guidelines require that subtraction to happen in one
named, greppable place.

## What to implement

`src/view/Camera.hpp` / `.cpp`.

- `struct Camera` with an `f64` world position in metres, a `Quat` orientation,
  a vertical field of view and a near plane. Rule of Zero, trivially copyable,
  every member default-initialised.
- `[[nodiscard]] Mat4 viewMatrix(const Camera&)` — **camera-relative**: the
  rotation only, with the translation identically zero, because the translation
  has already been applied by the subtraction below. A comment says that, since
  a view matrix with no translation looks like a bug to anyone who has written
  one before.
- **The boundary, and it is the only one**:
  ```cpp
  // The one place f64 becomes f32. Subtract in f64, then narrow.
  [[nodiscard]] Vec3f toRenderSpace(const Vec3& worldMetres, const Camera& camera) noexcept;
  ```
  with `struct Vec3f { f32 x, y, z; }` in the same header. Three
  `static_cast<f32>` in one function, exactly as
  `coding-guidelines-example/src/render/PathUpload.cpp` does it. Anything else
  in the project that narrows is a defect, and `-Wconversion` is what finds it.
- A short header comment recording the number this exists to protect: at Earth
  radius, narrowing before subtracting quantises position to about 0.5 m.

## Out of scope

Camera *controls* — input, orbiting, zooming — which are M1-21. Interpolation
between simulation states, which is M1-71. Any Vulkan.

## Tests

`tests/test_camera.cpp`.

- **The jitter budget, which is phase A's acceptance criterion**: take a fixed
  world point on Earth's surface and a camera at 400 km altitude translating in
  0.1 m steps along its orbit; project the point through the full pipeline in
  exact `f64`, and through the `f32` render path; assert the screen-space
  difference stays **≤ 0.05 px at 1920×1080**, and that the frame-to-frame
  movement of the static point does too.
- **The test has teeth, and that is asserted too**: the same measurement with
  the narrowing done *before* the subtraction — the naive path — must **fail**
  the same threshold by a wide margin. Without this, the budget could be met by
  a test that cannot fail (`VERIFICATION.md` rule 23, and rule 19 by hand).
- **The view matrix is a pure rotation**: orthonormal to 1e-15, determinant +1,
  translation exactly zero.
- **Round trip**: a direction transformed into view space and back is recovered
  to 1e-14 over a seeded sweep of orientations.
- **Scale sweep**: the jitter budget holds at lunar distance and at 1 AU too,
  because a boundary that only works at Earth radius is the same defect this
  project already shipped once in the propagator.

## Error budget

**≤ 0.05 px at 1920×1080** for a fixed world point at Earth radius, between
consecutive camera positions. Asserted, with the naive path asserted to fail it.

## Verification

The standing rules. Still headless — the GPU cannot make this claim, only
demonstrate it, and M1-20 does the demonstrating.

## Done when

- [ ] `check` green in both trees.
- [ ] `grep static_cast<f32>` over `src/` finds them in exactly one function.
- [ ] The naive-path comparison is in the suite and fails as expected.
- [ ] The budget is in the header, the test and the commit message.
