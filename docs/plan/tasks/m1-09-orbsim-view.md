# M1-09 — The `orbsim_view` library, and `Mat4`

Phase: A | Status: **done, 2026-09-20**
Prerequisites: M1-01
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md), and
[ADR 0020](../../adr/0020-transforms-carry-their-units.md) for what `Mat4` is

**Amended 2026-09-20, before the code**, by register decisions 92-97. Three of
this document's numbers did not survive measurement and are corrected in place
below, each with what was measured; the `f64` matrix it described became a
typed one, because the task was written nine days before ADR 0019.

## Purpose

A great deal of what this milestone builds is render-side maths that never
touches a Vulkan type: the camera, the projection, the quality struct, tile
identity, the screen-space error metric, and the CPU reference the atmosphere
tests compare against. All of it needs testing headlessly, and `CLAUDE.md`'s
rule — *nothing in `tests/` may include a Vulkan or SDL header* — should stay
enforced by the link graph rather than by anyone remembering.

So: a second render-side target that Vulkan never enters.

## What to implement

- **`src/view/`**, a new directory, and **`orbsim_view`**, a new static library.
  It links `orbsim_core` and nothing else. `orbsim_render` and the app link it;
  `orbsim_core` does not, and cannot, which is what makes the rule mechanical.
- Wiring, all of which is easy to forget and each of which is load-bearing:
  the header self-check group, the clang-tidy source list, the `check`
  dependencies, and the directory map in `CLAUDE.md`.
- **`src/view/Mat4.hpp`** — a 4×4 matrix, column-major to match what Vulkan and
  GLSL expect, so the narrowing at the boundary is a copy rather than a
  transpose. **Not `f64`**: `Mat4<kInXyz, kInW, kOutXyz, kOutW>` carries the
  units of both spaces it maps between, with the four block references derived
  from them (ADR 0020, decision 92). A homogeneous 4×4 has no single unit, and
  the parameterisation is what makes `projection * view` compile and
  `view * projection` not:
  - multiplication, `transformPoint`, `transformDirection`, `transpose`,
    `identity`, and `inverseRigid` for the view matrix's own inverse (rotation
    transposed, translation negated — cheaper and exact, where a general inverse
    is neither);
  - everything `constexpr` that can be, each with a `static_assert` under it,
    per guideline 3;
  - no operator that silently mixes a point with a direction: `transformPoint`
    applies the translation, `transformDirection` does not, and they are
    different names for that reason;
  - **`Vec4` and `perspectiveDivide`, added by decision 93**, because a
    projection's bottom row is not `(0, 0, 0, 1)` and `transformPoint` would be
    silently wrong for it. `transformPoint` refuses a projection by type, and
    `isAffine` is public so that its runtime precondition can be tested;
  - **element access by named block**, with strong `Row` and `Column` indices
    (decision 95). A single `at(row, column)` cannot have one return type here,
    and clang-tidy cannot catch a transposed index anyway.

## Out of scope

The projection matrix (M1-10) and the camera (M1-11) — this is the container
they live in. Anything Vulkan. Any narrowing to `f32`; that happens in exactly
one place and it is M1-11's.

## Tests

`tests/test_view_math.cpp`, linking `orbsim_view`, including no graphics header.

- **Associativity** `(AB)C == A(BC)` over a seeded sweep of random matrices,
  seed written down. **Not "1e-12 relative"** (decision 97): read elementwise
  that is unsatisfiable, measured worst 7.5e-10, because an element can cancel
  to near zero. The bound is the conditioning law `2·gamma_8 = 1.78e-15`
  against `|A||B||C|`, where the measured worst is 6.54e-16.
- **Identity** is the multiplicative identity, exactly, and
  `transpose(transpose(M)) == M` bit-for-bit. Both are **bit-identical**
  rather than close, measured 200,000 of 200,000.
- **`transpose(AB) == transpose(B)transpose(A)`** — the check that catches a
  row/column-major mix-up, which is the defect this file exists to avoid. Also
  bit-identical: the same four products summed in the same order.
- **`inverseRigid`**: **not "the identity to 1e-14"** (decision 97). That is
  dimensionally wrong — the translation column of `M · inverse(M)` is in
  metres and its residual scales with `|t|`, reaching 3.1e-4 m at 1 AU, so
  1e-14 holds only below about 5 m. The claim splits: **1e-14 on the
  dimensionless rotation block** (measured 2.22e-15, flat) and **20 ulp of
  |t| on the translation** (measured 8.5 to 9.2 ulp, flat), asserted at 1 m,
  Earth radius, lunar distance and 1 AU — because a boundary that only works
  at Earth radius is the defect this project already shipped once.
- **A point survives the round trip** through `M` and `inverseRigid(M)` to
  24 ulp of the scale, measured worst 11.2.
- **Point versus direction**: a translation moves a point and leaves a direction
  alone. One assertion, and it is the whole reason for the two names.
- **Compile-time**: `static_assert`s proving multiplication and the identity are
  usable in a constant expression.

## Verification

The standing rules, plus one structural check that belongs here because this is
the task that creates the risk:

```
cmake -S . -B build/nogpu -G Ninja -DORBSIM_BUILD_APP=OFF && cmake --build build/nogpu
```

must still build the core and its tests, and `orbsim_core` must not appear to
link `orbsim_view` in any generated build file.

## Done when

- [x] `check` green in both trees; `test_view_math` in the CTest list, which
      is 149 entries now.
- [x] `-DORBSIM_BUILD_APP=OFF` still builds core and tests: 410 targets, 148
      CTest entries, all passing.
- [x] `orbsim_view` links `orbsim_core`, and nothing links back the other way.
      Checked twice: a configure-time assertion in `CMakeLists.txt` reads
      `orbsim_core`'s `LINK_LIBRARIES` and fails if `orbsim_view` appears, and
      every mention of `orbsim_view` in the generated `build.ninja` belongs to
      `test_view_math` or the view self-check.
- [x] `view/Mat4.hpp` is in the header self-check list.
