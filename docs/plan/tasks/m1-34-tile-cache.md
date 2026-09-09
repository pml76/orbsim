# M1-34 — The tile cache and its budget

Phase: B | Status: not started
Prerequisites: M1-33
Decided by: [ADR 0007](../../adr/0007-render-quality-is-a-struct.md)

## Purpose

Tiles arrive faster than they leave. Without a bound, a descent from orbit to
10 km fills memory with every tile it passed through; with a bad bound, the
cache thrashes and the loader never catches up. The cache is where phase C's
frame time is won or lost, so it is built and tested now, headless, while it is
still simple enough to reason about.

## What to implement

`src/view/TileCache.hpp` / `.cpp`.

- Keyed by `TileId`, holding `TileData` and a GPU descriptor index from M1-31.
- **A budget in `Mebibytes`** — a `Count` type, so it cannot be confused with a
  tile count — enforced on insertion, evicting least-recently-used entries until
  the new tile fits.
- **Pinned tiles are never evicted.** A tile the current frame is drawing is
  pinned; eviction that removed it would produce a hole in the planet, which is
  the one failure the cache must not have.
- **In-flight tracking**: a tile already requested is not requested again, and
  the loader's result is matched back to the request. Without this, a camera
  sweeping across a level requests the same tiles repeatedly and the queue never
  drains.
- Statistics — hits, misses, evictions, resident bytes, in-flight count —
  exposed as a value, for the benchmark and for a debug overlay later. A cache
  whose behaviour cannot be observed cannot be tuned.
- A tile larger than the whole budget is **reported**, not accommodated by
  evicting everything.

## Out of scope

The eviction *policy* under camera motion (M1-53) — this is the mechanism, and
that task tunes and tests it against a real camera path. The `RenderQuality`
budget field (M1-59). Any GPU memory management beyond the descriptor index.

## Tests

`tests/test_tile_cache.cpp`, headless — every one of these is arithmetic and
bookkeeping, which is exactly why it is worth testing without a GPU in the way.

- **The budget is respected exactly**: after any sequence of inserts, resident
  bytes never exceed the budget, over a seeded sweep of random tile sizes and
  access patterns.
- **LRU order is correct**: the least recently *used* entry is evicted, not the
  least recently inserted — the distinction that makes a cache work, and the one
  a naive implementation gets wrong.
- **Pinned tiles survive** pressure that evicts everything else, and unpinning
  makes them evictable again.
- **In-flight**: requesting a tile twice yields one load; a tile that arrives
  after being unpinned and evicted is still accounted for correctly rather than
  leaking an index.
- **Statistics are right**: hits plus misses equals lookups, and evictions plus
  residents equals inserts, over the sweep.
- **Named failures**: a tile bigger than the budget, and a budget of zero.
- **Determinism**: the same access sequence produces the same eviction sequence.

## Verification

The standing rules, plus the loader suite still passing under `linux-tsan` with
the cache in the path.

## Done when

- [ ] `check` green in both trees, and `linux-tsan` clean.
- [ ] The budget invariant holds over the randomised sweep, with the seed
      written down.
- [ ] Pinned tiles are provably never evicted.
- [ ] Cache statistics are available to the benchmark.
