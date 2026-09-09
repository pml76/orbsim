# M1-09 — The `orbsim_view` library, and `Mat4`

Phase: A | Status: not started
Prerequisites: M1-01
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md)

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
- **`src/view/Mat4.hpp`** — a 4×4 `f64` matrix, column-major to match what
  Vulkan and GLSL expect, so the narrowing at the boundary is a copy rather than
  a transpose:
  - multiplication, `transformPoint`, `transformDirection`, `transpose`,
    `identity`, and `inverseRigid` for the view matrix's own inverse (rotation
    transposed, translation negated — cheaper and exact, where a general inverse
    is neither);
  - everything `constexpr` that can be, each with a `static_assert` under it,
    per guideline 3;
  - no operator that silently mixes a point with a direction: `transformPoint`
    applies the translation, `transformDirection` does not, and they are
    different names for that reason.

## Out of scope

The projection matrix (M1-10) and the camera (M1-11) — this is the container
they live in. Anything Vulkan. Any narrowing to `f32`; that happens in exactly
one place and it is M1-11's.

## Tests

`tests/test_view_math.cpp`, linking `orbsim_view`, including no graphics header.

- **Associativity** `(AB)C == A(BC)` to 1e-12 relative over a seeded sweep of
  random matrices, seed written down.
- **Identity** is the multiplicative identity, exactly, and
  `transpose(transpose(M)) == M` bit-for-bit.
- **`transpose(AB) == transpose(B)transpose(A)`** — the check that catches a
  row/column-major mix-up, which is the defect this file exists to avoid.
- **`inverseRigid`**: for a rotation-plus-translation matrix, `M · inverse(M)`
  is the identity to 1e-14, over a sweep of random rotations from `Quat`.
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

- [ ] `check` green in both trees; `test_view_math` in the CTest list.
- [ ] `-DORBSIM_BUILD_APP=OFF` still builds core and tests.
- [ ] `orbsim_view` links `orbsim_core`, and nothing links back the other way.
- [ ] `view/Mat4.hpp` is in the header self-check list.
