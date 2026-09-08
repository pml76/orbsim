# M1-51 — Tile meshes and skirts

Phase: C | Status: not started
Prerequisites: M1-30, M1-31, M1-50

## Purpose

Turning selected tiles into geometry, and closing the cracks between levels.

A crack appears wherever a tile meets a neighbour at a different level: the
coarse edge is a straight chord where the fine edge is two chords, and the gap
between them shows the background through the planet. **Skirts** — a rim of
geometry dropped below each tile's edge — close it without any tile needing to
know what level its neighbours are, which is what keeps the traversal a pure
function.

## What to implement

`src/view/TileMesh.hpp` / `.cpp` for the geometry, `src/render/` for the buffers.

- A grid of vertices over the tile's latitude/longitude rectangle, placed on the
  WGS-84 ellipsoid at the vertex's geodetic position. Grid resolution is a named
  constant and is the same for every tile, which is what makes the next point
  possible.
- **One shared index buffer for every tile**, since the topology is identical.
  It saves memory, and more usefully it removes a whole class of bug: there is
  one triangulation in the project, so it can be wrong only once.
- **Skirts**: an extra ring of vertices around the tile's edge, displaced along
  the geodetic normal by a depth derived from the tile's size — deep enough to
  cover the worst gap a one-level difference can produce, computed rather than
  chosen, with the derivation in a comment. Too shallow leaves a crack; too deep
  shows as a dark fringe at grazing angles.
- **Winding order** consistent and outward-facing, with backface culling on.
- Per-tile draw with the texture index from M1-31 pushed as a constant.

## Out of scope

Elevation displacement (M1-56) — these meshes sit on the ellipsoid, and the
skirt depth will need revisiting when terrain has relief, which M1-56 says.
Morphing (M1-52). Instancing or indirect drawing; measure first.

## Tests

Headless, in `orbsim_view`:

- Every surface vertex lies on the ellipsoid to 1e-9 m, and the corner vertices
  match the tile bounds exactly.
- **Adjacent tiles at the same level share their edge vertices exactly** —
  bit-identical positions, not merely close. Anything else is a crack that no
  skirt should have to hide, and floating-point non-determinism along a shared
  edge is precisely how that happens.
- **The skirt depth covers the worst case**: for a tile at level n abutting one
  at level n−1, the computed depth exceeds the maximum geometric gap, checked
  against the sag formula from M1-50 rather than by eye.
- Winding is consistently outward for every triangle, checked by the sign of the
  dot product between the face normal and the outward radial direction — a
  reversed winding is invisible until culling hides half the planet.
- The shared index buffer indexes every quad exactly twice (two triangles) and
  never out of range.

On the GPU:

- **Probe `terrain-tiles`** with a golden: several tiles at two levels, drawn
  with a wireframe overlay from the M1-19 line renderer so the tessellation is
  visible.

## Frames to look at

`terrain-tiles.png`. Judge: the tile boundaries line up; no background shows
through anywhere; the wireframe shows the level change where the metric says it
should be; the skirts are not visible as dark bands at the seams. This is the
first frame where a crack could appear, so it is worth looking at the boundary
region closely rather than the picture as a whole.

## Done when

- [ ] `check` green in both trees.
- [ ] Shared edges are bit-identical between neighbouring tiles.
- [ ] The skirt depth is derived from the gap it must cover, not chosen.
- [ ] `terrain-tiles.png` approved and committed.
