# ADR 0015: Quadtree LOD is skirts plus vertex morphing

Status: accepted (2026-09-08; recorded 2026-09-09)

Decision 18 of [the milestone 1 register](../plan/milestone-1-decisions.md).

## Decision

A spherical quadtree has two artefacts at a level boundary, and they are
separate problems with separate answers. Phase C's acceptance criterion --
descend from 400 km to 10 km with no popping and no seams -- needs both.

- **Cracks are closed by skirts.** A crack appears wherever a tile meets a
  neighbour at a different level: the coarse edge is one chord where the fine
  edge is two, and the background shows through the planet. A skirt is a ring
  of geometry dropped below each tile's edge along the geodetic normal, and it
  covers the gap **without any tile needing to know its neighbours' levels**.
- **The skirt depth is computed, not chosen** -- deep enough to cover the worst
  gap a one-level difference can produce, with the derivation written where the
  constant is. Too shallow leaves a crack; too deep shows as a dark fringe at
  grazing angles. It is revisited when terrain gains relief, and that is said in
  the task that adds relief rather than left to be rediscovered.
- **Popping is removed by vertex morphing.** Each vertex carries the position
  its *parent's* tessellation would have given it; the two are blended so that
  at the moment of subdivision the child is geometrically identical to the
  parent, and there is nothing to pop.
- **The morph factor comes from the same screen-space error that drives
  selection** -- not from distance, and not from a second curve. One metric, so
  the morph is guaranteed to finish exactly when the switch happens, and the
  factor reaching exactly 1 at the subdivision threshold is the assertion the
  whole mechanism rests on. A second curve is how morphing systems end up
  popping anyway.
- **One shared index buffer for every tile.** The grid resolution is the same
  for all of them, so there is exactly one triangulation in the project, and it
  can therefore be wrong only once.
- **The surface is the WGS-84 ellipsoid**, not a sphere, and the threshold, the
  maximum level and the cache budget are `RenderQuality` fields
  ([`0007`](0007-render-quality-is-a-struct.md),
  [`0012`](0012-orbsim-view.md)) rather than constants.
- **Selection is testable headless.** The quadtree and its error metric live in
  `orbsim_view`, so "every selected tile's screen-space error is at or under
  the threshold" is a property test over a seeded sweep of camera poses, not
  something judged by looking at a frame.

## What we considered

**Stitching, or T-junction removal**: generating transition geometry along each
edge where the levels differ. Geometrically exact, no skirt to tune, and no
dark fringe. It requires every tile to know its neighbours' levels, which turns
a per-tile decision into a graph problem, couples selection to rendering, and
adds a case for each edge-and-corner combination -- in the phase the plan
already calls the highest risk in the milestone. Skirts trade a small,
bounded, computable amount of hidden geometry for keeping traversal a pure
function of the camera.

**Geomorphing alone**, without skirts. It removes the pop and does nothing
about the crack, because two adjacent tiles at different levels still
disagree about where the shared edge is.

**Skirts alone.** Removes the crack and does nothing about the pop -- and the
pop is the artefact most visible during exactly the manoeuvre phase C is
accepted on. Neither half is optional; that is why this is one record and not
two.

## Why

Phase C is where planet renderers consume their schedule, and the reason is
almost always the interaction between selection, geometry and streaming rather
than any one of them. Every clause above is chosen to keep those three
independent: a tile is selected by a metric that reads only the camera and the
tile, meshed without reference to its neighbours, and streamed by a cache that
knows nothing about either. The one place they are allowed to meet is the
morph factor, and it is deliberately the *same* number as the selection metric
so that the meeting cannot drift.

Skirts are the less elegant of the two crack fixes and are chosen for that
independence. It is worth being explicit that this is a trade rather than a
free win: a skirt is geometry that is drawn and mostly hidden, and its depth is
a number that has to be right.

## What this record does not decide

- **The grid resolution per tile**, the skirt depth's exact formula, or the
  morph's blend curve beyond its endpoints.
- **The screen-space error formula itself**, which is
  [M1-50](../plan/tasks/m1-50-quadtree-and-sse.md)'s, nor the default
  threshold, which is a `RenderQuality` field.
- **How elevation changes the skirt depth.** Relief arrives in
  [M1-56](../plan/tasks/m1-56-displacement-and-normals.md) and the revisit is
  named there.
- **Eviction policy** for the tile cache, which is a streaming question rather
  than a geometry one.
