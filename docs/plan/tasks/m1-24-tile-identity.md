# M1-24 — `TileId` and the tile scheme

Phase: B | Status: not started
Prerequisites: M1-09
Decided by: [ADR 0010](../../adr/0010-tiles-are-ktx2.md), [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

Everything in phases B and C is addressed by a tile index, so the indexing
convention is worth getting exactly right once. We adopt **Orbiter's**, because
the `.tree` converter in M1-37 then becomes a repack rather than a resampling,
and because it is specified in writing.

The specification is `Doc/Orbiter Developer Manual/PLANETS.tex`, sections
`sssec:tile_file_layout` and `sssec:quadtree_struct`, in the MIT-licensed
reference clone. Read it before this task, not during.

## What to implement

`src/view/TileId.hpp`.

- `TileLevel`, `TileLat`, `TileLng` as `Count` types from M1-12. Three adjacent
  `std::uint32_t` parameters would transpose in silence, and
  `bugprone-easily-swappable-parameters` is enabled precisely to catch that.
- `struct TileId { TileLevel level; TileLat ilat; TileLng ilng; };`
- The scheme, quoted from the specification in the header comment:
  - `nlat = 2^(n-4)` and `nlng = 2^(n-3)` for level `n ≥ 4`;
  - `ilat = 0` is the **northernmost** band, `ilng = 0` is the **westernmost**
    tile, whose left edge is 180° W;
  - level 4 is the quadtree root and has exactly two tiles: the western and
    eastern hemispheres;
  - **levels 1 to 3 are not part of the quadtree** — they are whole-planet
    textures at 128², 256² and 512². They get their own type, `GlobalTile`, so
    that a quadtree function cannot be handed one.
- `parent()`, `children()` — `[n+1, 2·ilat + {0,1}, 2·ilng + {0,1}]` — and the
  latitude/longitude bounds:
  ```
  lng ∈ [-180 + (360/nlng)·ilng,  -180 + (360/nlng)·(ilng+1)]
  lat ∈ [ 90 - (180/nlat)·(ilat+1),  90 - (180/nlat)·ilat]
  ```
- `tileAt(TileLevel, Radians lat, Radians lng)`, the inverse, with the boundary
  convention stated: a tile owns its western and northern edges, so a point
  exactly on a boundary belongs to exactly one tile and no point belongs to two.
- Maximum levels as named constants with their source: **21** for imagery, **17**
  for elevation. An out-of-range level is reported, not asserted — it can come
  from a configuration.

## Out of scope

Loading anything. Any file format. The ellipsoid — these are angles, and the
geometry that turns them into positions is M1-49.

## Tests

`tests/test_tile_id.cpp`, headless.

- **The specification's own worked example**, which is an independent reference
  in the strongest sense — someone else wrote it down: tile [n = 8, ilat = 5,
  ilng = 7] spans 22.5° ≤ lat ≤ 33.75° and −101.25° ≤ lng ≤ −90°.
- **The level-4 roots**: [4,0,0] is the western hemisphere
  (−180° ≤ lng ≤ 0°, −90° ≤ lat ≤ 90°) and [4,0,1] the eastern.
- **Round trip** `tileAt(level, centre(tile)) == tile` for every tile at levels 4
  to 10, exhaustively where that is cheap and by seeded sample above it.
- **Parent and child are inverses**, and the four children exactly partition the
  parent's bounds with no gap and no overlap — checked by area summation.
- **The singularities**, per rule 5: longitude exactly −180° and +180°, latitude
  exactly ±90°, the tile at the pole, and the seam where ilng wraps. Each lands
  in exactly one tile, and the test says which.
- **Named failures**: level 0, level 22, an `ilat` past `nlat`, a non-finite
  angle.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The header quotes the specification and cites the section label.
- [ ] The worked example from the document is in the suite verbatim.
- [ ] Levels 1–3 cannot be passed to a quadtree function.
