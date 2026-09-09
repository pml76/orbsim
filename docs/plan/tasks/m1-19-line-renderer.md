# M1-19 — The line renderer

Phase: A | Status: not started
Prerequisites: M1-11, M1-13
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

The orbit track in phase F needs it, and it is worth far more than it costs as a
debugging tool long before then: a quadtree bug is much easier to see as
wireframe than as shading. `shaders/line.vert` and `line.frag` already exist and
have never been loaded.

## What to implement

**`src/view/LineBatch.hpp`** — the CPU half, in the Vulkan-free library so it
can be tested without a GPU:

- A vertex is a `Vec3f` position **already in render space** plus an RGBA
  colour. Positions go through `toRenderSpace` from M1-11 and nowhere else,
  which is what keeps the narrowing in one place.
- `addSegment`, `addPolyline`, `addAxes`, `clear`, and a `std::span` accessor.
- Capacity is reserved once at construction. Exceeding it is **reported**, not
  grown mid-frame: an allocation in the hot path is the thing JPL's rule 3
  exists to prevent, and silently reallocating hides it.

**`src/render/LineRenderer.hpp` / `.cpp`** — the GPU half:

- A per-frame-in-flight host-visible vertex buffer, sized once from the batch
  capacity, written directly.
- One pipeline from M1-13: line list topology, depth test on, depth write off
  (lines over geometry read better and it avoids z-fighting with the surface),
  the reverse-Z comparison, and a push constant carrying the view-projection
  matrix and a tint.
- Draw takes a `RenderQuality` by value, like everything else, and ignores it.

## Out of scope

Thick lines, screen-space width, dashes, anti-aliasing. Depth-sorted
transparency. The orbit track itself (M1-76). Text (M1-79).

## Tests

`tests/test_line_batch.cpp`, headless:

- Vertex count and ordering for a polyline of n points is exactly 2(n−1)
  vertices, and `addAxes` produces three segments with the expected colours.
- **Positions match `toRenderSpace` exactly**, bit-for-bit, for a sweep of world
  points and camera positions — the batch must not do its own arithmetic.
- Overflow past the reserved capacity is reported by name and leaves the batch
  unchanged, so a caller that ignores the error does not draw garbage.
- Empty batch draws nothing and is not an error.

Then the GPU side, on the M1-16/17 machinery:

- **Probe `lines`**: three axes and a unit square at a fixed camera pose, with a
  golden image. Simple enough that a wrong sign or a transposed matrix is
  obvious rather than subtle.

## Frames to look at

`lines.png`. Judge: the axes point where the frame convention says (X toward the
vernal equinox, Z up), the square is square rather than sheared, and the colours
are not swapped. A transposed matrix in `Mat4` survives every arithmetic test in
M1-09 and fails visibly here.

## Verification

The standing rules, plus `ctest -R probe_lines`.

## Done when

- [ ] `check` green in both trees.
- [ ] `lines.png` approved and committed as a golden.
- [ ] No allocation happens inside the frame path — checked by reading the code
      and by the capacity test.
- [ ] `line.vert` and `line.frag` are loaded by something for the first time.
