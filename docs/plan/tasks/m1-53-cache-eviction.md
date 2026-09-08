# M1-53 — Eviction under motion

Phase: C | Status: not started
Prerequisites: M1-34, M1-50

## Purpose

M1-34 built a cache with a budget and an LRU policy; this is where it meets a
moving camera, which is a different problem. A descent from 400 km to 10 km
walks through six or seven levels, and a policy that evicts what it is about to
need again turns the loader into a treadmill — the frame rate survives, and the
image never sharpens.

The milestone plan names this as one of the three places planet renderers
consume their schedule. It is tested here entirely on the CPU, before it can
hide behind a frame rate.

## What to implement

- **Priority, not just recency.** A tile's value is a function of its level and
  its distance from the camera's current selection: an ancestor of a visible
  tile is worth keeping because it is the fallback while its children load; a
  tile the camera has left behind at a level it has passed is worth nothing.
- **Hysteresis.** A tile is not evicted the instant it leaves the selection, and
  not requested the instant it enters. Without it, a camera oscillating at a
  threshold loads and evicts the same tile forever, which costs more than the
  quality difference being managed — the same argument ADR 0007 makes about the
  adaptive quality controller, arriving in a different subsystem.
- **A fallback rule**: while a tile is loading, its nearest resident ancestor is
  drawn, scaled to the child's bounds. That is what makes streaming invisible
  rather than a hole, and it is why keeping ancestors is worth budget.
- The budget stays the `Mebibytes` value from M1-34 and becomes a
  `RenderQuality` field in M1-59.

## Out of scope

Prefetching along a predicted camera path — worth doing, and worth doing after
there is a measurement saying it is needed. Disk-side caching. GPU memory
defragmentation.

## Tests

`tests/test_tile_streaming.cpp`, headless: drive the M1-50 selection along
scripted camera paths and run the real cache and a stub loader with a modelled
latency. No GPU, no files, no threads — which is what makes these assertions
possible at all.

- **The descent**: 400 km to 10 km over 60 seconds of simulated motion. Assert
  **no tile is loaded more than twice**, and the cache hit rate exceeds **90 %**
  after the first two seconds. Both numbers are stated here, before the policy
  is written, and both are properties a thrashing policy fails.
- **The oscillation**: a camera moving back and forth across a subdivision
  threshold for 60 seconds. Assert the total number of loads is bounded by a
  small multiple of the working set — this is the test hysteresis exists for,
  and without hysteresis it fails by orders of magnitude.
- **The orbit**: a full orbit at 400 km, where the visible set is entirely
  replaced twice. Assert the budget is never exceeded and the working set size
  stabilises.
- **The fallback is always available**: at every step of every path, each
  selected tile either is resident or has a resident ancestor. This is the
  assertion that guarantees no hole in the planet, and it is checked at every
  frame of every path rather than at the end.
- **Determinism**: the same path produces the same load and eviction sequence.

## Error budget

Stated before implementation: **≤ 2 loads per tile** and **≥ 90 % hit rate**
after the first two seconds on the descent path; the oscillation path's load
count bounded by 3× the working set. Measured values recorded in the commit
message.

## Verification

The standing rules, plus the streaming suite under `linux-tsan`, since the real
loader threads are in the path.

## Done when

- [ ] `check` green in both trees; TSan clean.
- [ ] All three camera paths meet their stated numbers.
- [ ] The fallback-ancestor invariant holds at every frame of every path.
- [ ] The hysteresis test was seen to fail with hysteresis removed.
