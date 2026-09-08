# M1-25 — The `TileSource` seam

Phase: B | Status: not started
Prerequisites: M1-24

## Purpose

The milestone plan calls `TileSource` *"the seam that lets the Orbiter reader
arrive later as a second implementation rather than a parallel code path"*. It
takes the same shape as the physics seams decided in ADR 0011: a closed set as a
`std::variant`, so adding a source is adding an alternative, and forgetting to
handle it somewhere is a compile error.

It also delivers the thing every later test needs — **tiles without files**.

## What to implement

`src/view/TileSource.hpp`.

- `struct TileData`: an immutable value. Dimensions, `VkFormat`-equivalent
  format enum (our own, so `orbsim_view` stays free of Vulkan headers), mip
  count, and the bytes in a `std::vector<std::byte>`. Moved, never copied, into
  the cache; that is what makes the async publication in M1-33 a hand-over
  rather than shared state.
- `enum class TileError : std::uint8_t { NotPresent, LevelTooHigh, Corrupt,
  UnsupportedFormat, IoFailure };` with `describe()`.
- The operations every alternative provides: `load(TileId) ->
  std::expected<TileData, TileError>`, `has(TileId)`, `maxLevel()`, and a
  `name()` for logs and probe sidecars.
- `using TileSource = std::variant<SyntheticTileSource>;` — one alternative
  today. `KtxPyramidSource` joins it in M1-30 and `TreeArchiveSource` in M1-36,
  and each addition breaks every `std::visit` that has not been updated, which
  is the property being bought.
- **`SyntheticTileSource`**: generates a tile procedurally and deterministically
  — a checkerboard tinted by latitude and longitude band, with the tile's level
  and indices legible in the pattern. Every later test that needs tiles uses
  this rather than the filesystem, and every quadtree bug in phase C is easier
  to see against a pattern that says which tile it is.

## Out of scope

Any file format (M1-26). Threads (M1-33). Caching (M1-34). Uploading anything
to a GPU (M1-31).

## Tests

`tests/test_tile_source.cpp`, headless.

- The synthetic source is **deterministic**: the same `TileId` produces
  byte-identical data twice, and across a rebuild.
- `has` and `load` agree for every tile at levels 4 to 8, including the ones
  outside `nlat`/`nlng`, which report `NotPresent` rather than generating
  something.
- A level above `maxLevel()` reports `LevelTooHigh` by name.
- The pattern is **positionally correct**: the tint at a tile's centre matches
  the latitude and longitude the `TileId` bounds say, so a mis-indexed tile is
  visible rather than merely different.
- The variant dispatches: a `std::visit` over a `TileSource` compiles and
  returns the same answer as calling the alternative directly.
- `TileData` is move-only in practice — moving it does not copy the bytes,
  asserted by checking the source is emptied.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] `orbsim_view` still includes no Vulkan header — the format enum is ours.
- [ ] Every `TileError` value has a test that asks for it by name.
- [ ] The synthetic source is good enough to debug a quadtree against, which is
      what it is for.
