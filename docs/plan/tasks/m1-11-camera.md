# M1-11 — The camera, and the f64 → f32 boundary

Phase: A | Status: **done, 2026-09-22**
Prerequisites: M1-10, M1-87
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md), [ADR 0021](../../adr/0021-transforms-carry-their-frames.md), [ADR 0022](../../adr/0022-a-bounded-scalar-validates-itself.md)

**Corrected 2026-09-20, with M1-09.** This document was written on 2026-09-08,
nine days before [ADR 0019](../../adr/0019-vectors-carry-their-unit.md) made
`Vec3` a template: a world position is a `Position`, and a bare `Vec3` no
longer names a type. The signature below is corrected; the task is unchanged.

**Amended 2026-09-22, when the task ran**, and this one is not a wording
change. Four of its statements did not survive measurement — the geometry its
central test puts its teeth at, two of its three tolerances, and the type its
round trip transforms — and one of its checkboxes was already false on the day
it was written. Each is corrected in place below, with the measurement and the
register decision beside it (115 to 121). **Read this rather than the original
if the two disagree; the original's numbers are in the git history.**

## Purpose

This is the task phase A's acceptance criterion is actually about. A vertex on
Earth's surface is 6.4e6 m from the origin; `f32` has about seven significant
digits, so narrowing that coordinate directly quantises it to roughly half a
metre — **measured at exactly 0.5 m** — and the whole planet visibly crawls as
the camera moves. Subtracting the camera position **in `f64` first** and
narrowing only the small difference is what removes it, and the guidelines
require that subtraction to happen in one named, greppable place.

## What was implemented

`src/view/Camera.hpp` / `.cpp`. `orbsim_view` becomes a STATIC library, which
is the two-line change its CMake comment has anticipated since M1-09.

- **`class Camera`** with an `f64` world position in metres, a `Quat`
  orientation, a vertical field of view and a near plane.

  **A validated class, not the plain struct this document specified**
  (decision 116). Three of the four members have exactly the kind of physical
  bound [ADR 0022](../../adr/0022-a-bounded-scalar-validates-itself.md) is
  about, and the orientation is the one that matters: a quaternion that is not
  unit length yields a matrix that is finite, plausible and not a rotation. So
  the constructor is private, `Camera::from(...)` returns
  `std::expected<Camera, CameraError>`, and `CameraError` names one failure per
  argument. It is **still trivially copyable**, which this document asked for
  and which was measured before the shape was chosen. The orientation runs
  **camera-to-world**, the direction a vessel's attitude runs in.
- `[[nodiscard]] WorldToView viewMatrix(const Camera&)` — **camera-relative**: the
  rotation only, with the translation identically zero, because the translation
  has already been applied by the subtraction below. A comment says that, since
  a view matrix with no translation looks like a bug to anyone who has written
  one before. It is the only caller of `retargetFrame` in `src/`.
- **The boundary, and it is the only one**:
  ```cpp
  // The one place f64 becomes f32. Subtract in f64, then narrow.
  [[nodiscard]] Vec3f toRenderSpace(const Position& worldMetres, const Camera& camera) noexcept;
  ```
  with `struct Vec3f { f32 x, y, z; }` in the same header. Three
  `static_cast<f32>` in one function, exactly as
  `coding-guidelines-example/src/render/PathUpload.cpp` does it. Anything else
  in the project that narrows is a defect, and `-Wconversion` is what finds it.
  It asserts that the offset is inside 32-bit range, because narrowing past it
  is undefined behaviour rather than merely inexact (decision 121).
- **`projectionOf(const Camera&, Aspect)`, which M1-13 deletes** (decision
  117). Not in the original scope; added so that the camera's field of view and
  near plane are read by something before M1-13, and so that this task's suite
  can drive the whole chain through one entry point. The obligation to remove
  it is on [M1-13](m1-13-pipelines.md)'s own checklist.
- A short header comment recording the number this exists to protect: at Earth
  radius, narrowing before subtracting quantises position to about 0.5 m.

Outside its own files, four small additions (decision 121): `isUnitQuaternion`
and `kUnitQuaternionTolerance` in `core/Math.hpp`, a `bitsOf(f32)` overload in
`core/Scalar.hpp`, `bitIdentical` for `Vec3f`, and the narrowability
predicate.

## Out of scope

Camera *controls* — input, orbiting, zooming — which are M1-21. Interpolation
between simulation states, which is M1-71. Any Vulkan.

## Tests

`tests/test_camera.cpp`, thirteen cases.

- **The jitter budget, which is phase A's acceptance criterion**: a fixed world
  point and a camera translating in 0.1 m steps, projected through the full
  pipeline in exact `f64` and through the `f32` render path. Asserted three
  ways rather than one:
  - against **a conditioning law** rather than a number at one field of view —
    `error <= 2 * (screenHeight / 2) * 2^-24 * focalLength`, which contains no
    distance at all. Measured worst **0.683** of that bound over nine
    geometries; the law is what makes "it does not depend on how far the origin
    is" a claim rather than a hope;
  - against **0.05 px at 1920×1080**, which the law beats by about 320 times at
    a 45-degree field of view;
  - and the whole chain computed **in 32 bits, as the graphics card computes
    it**, against the same bound. Measured 1.55 of its coefficient.
- **Flatness across six decades**: the same geometry at Earth radius, lunar
  distance and 1 AU, asserted to agree within 5 %. Measured 0.06 %.
- **The test has teeth, and where they are is a correction** (decision 115).
  This document asked that the naive path — narrowing before subtracting —
  fail the same threshold by a wide margin at 400 km altitude. **Measured, it
  does not: 5.8e-4 px there, 86 times inside the budget**, because half a metre
  at a range of 400 km is 1.25 microradians. It fails at **1 AU** (4.7 px, 95
  times over) and at **short range** (0.43 px at 1 km, 5.6 px at 100 m), and
  that is where the failing assertions are. **The 400 km case is kept and
  asserted to pass**, so the original claim cannot silently return, together
  with an assertion that it is still at least five times worse than doing it
  properly — without which the two paths could be compared and found equal.
- **The view matrix is a pure rotation**: translation **bit-identical** to
  zero, `isAffine`, orthonormal and determinant +1 to **270 ulp** — not the
  1e-15 this document asked for, which is unreachable (decision 118). A
  quaternion of length 1 + e gives a matrix whose orthonormality residual is
  about eight times e, measured across six decades, so the orientation
  tolerance and this budget are the same number twice. The tolerance is
  **16 ulp**, set from what composing rotations costs rather than from what
  a single producer costs.
- **The camera's own axes land on view space's axes.** Not in the original
  list, and the only case here that can tell a view matrix from its inverse: a
  rotation and its inverse are both rotations, both have determinant +1, and
  each round-trips perfectly against the other.
- **Round trip**: a **displacement in metres** — not a dimensionless
  `Direction`, which since [ADR 0020](../../adr/0020-transforms-carry-their-units.md)
  does not compile against a metre-to-metre matrix at all — recovered to
  **270 ulp**, not 1e-14, for the same coupling.
- **The narrowing is exact, and the subtraction is what makes it so**: a camera
  at 6771000.3 m and a point 0.1 m beyond it. Subtracting first gives 0.1 m;
  narrowing first gives **exactly zero**, because both operands round to the
  same 32-bit float. That is the crawl in one assertion.
- **The refusals**, one case per argument, each asking for its error by name,
  and a boundary case that checks the orientation tolerance **from both sides**
  — without which a factory that refused everything would pass.

## Error budget

**≤ 0.05 px at 1920×1080** for a fixed world point at Earth radius, between
consecutive camera positions — asserted, and measured at 5.3e-5 px. The
regression guard is the tighter conditioning law above. The naive path is
asserted to fail at 1 AU and at short range, and **asserted to pass at 400 km**,
which is what this document got wrong.

## Verification

The standing rules. Still headless — the GPU cannot make this claim, only
demonstrate it, and M1-20 does the demonstrating. M1-20's own claim about what
quantises visibly was corrected by this task's measurement (decision 120).

## Done when

- [x] `check` green in both trees. 188 tests, 0 failed, in each.
- [x] `grep static_cast<f32>` over `src/` finds them in exactly one function.
- [x] `orbsim_view` becomes a STATIC library when `Camera.cpp` arrives; M1-09
      left it INTERFACE because a sourceless static library does not configure.
- [x] `grep retargetFrame` over `src/` finds **exactly one call site outside
      `view/Mat4.hpp`**, which is `viewMatrix`. *(This checkbox read "finds it
      in exactly one place" and was already false when it was written: the
      definition, its comment, a compile-time assertion and that assertion's
      message are four lines of `view/Mat4.hpp`. Corrected 2026-09-22,
      decision 121's neighbourhood.)* Since
      [ADR 0021](../../adr/0021-transforms-carry-their-frames.md) a rotation
      and a translation are within a frame, so the camera is what declares
      world-to-view, and it is the only thing that should.
- [x] The naive-path comparison is in the suite and fails as expected — **at
      the geometries where it genuinely fails**, which are not the one this
      document named.
- [x] The budget is in the header, the test and the commit message.
