# M1-33 — Asynchronous tile loading

Phase: B | Status: not started
Prerequisites: M1-30, M1-31
Decided by: [ADR 0010](../../adr/0010-tiles-are-ktx2.md)

## Purpose

This is where the frame rate is usually lost. Loading a tile on the render
thread means a disk read and a BC7 upload inside the frame, and the result is
precisely the intermittent freeze that makes a planet renderer feel like a slide
show — the thing the owner asked about directly.

It is also **the first thread in this project**, which makes it the moment
`CODING_GUIDELINES.md` section 13 and ThreadSanitizer stop being hypothetical.

## What to implement

`src/view/TileLoader.hpp` / `.cpp`, in the Vulkan-free library so it can be
tested — and sanitized — without a GPU.

- One or more `std::jthread` workers, never a raw `std::thread` (CP.50), taking
  a `stop_token` and exiting on it.
- A **request queue** ordered by need — coarser levels first, since a coarse
  tile can stand in for a missing fine one — with duplicate requests collapsed.
- **Finished tiles are published as immutable values**, moved across, and the
  render thread takes ownership. No shared mutable state, per section 13: the
  worker does not touch a cache the renderer is reading.
- The synchronisation is a mutex and a condition variable, named lock guards
  (CP.44), and nothing clever. A lock-free queue here would be optimisation
  before measurement.
- **An upload budget per frame** — at most N tiles or M bytes uploaded to the
  GPU in one frame, with the rest deferred. This is the mechanism that turns a
  burst of newly visible tiles into a few frames of gradual sharpening instead
  of one long stall.
- Cancellation: a request that is no longer wanted is dropped rather than
  loaded, and shutdown joins cleanly with work in flight.

## Out of scope

Eviction (M1-34). Decompression on the worker — BC7 goes to the GPU as bytes.
Threading the physics, which is phase E and is a separate argument.

## Tests

`tests/test_tile_loader.cpp`, headless, against `SyntheticTileSource`.

- Request 500 tiles, drain, and assert every one arrives **exactly once** and
  with the right contents.
- **Duplicate requests** for the same tile produce one load.
- **Cancellation**: requests dropped before they run are not loaded, and the
  loader shuts down cleanly with a full queue.
- **Determinism of the result, not the order**: the *set* of tiles delivered is
  identical across runs, and each tile's bytes are identical. Delivery order is
  explicitly not a promise, and the test says so, because a test that asserted
  order would be asserting the scheduler.
- **The budget holds**: with a budget of N per frame, no frame receives more.
- Failures propagate: a source that reports `IoFailure` results in a reported
  failure on the render thread, not a silently missing tile.

## The new preset

This task adds **`linux-tsan`** — clang with `-fsanitize=thread`, core and view
only — and the loader suite runs under it. The guidelines call ThreadSanitizer
mandatory *"the day you thread the physics off the render loop"*; the honest
reading is the day anything is threaded, which is today. A data race here would
present as a corrupt tile, which looks exactly like a decoder bug.

## Verification

The standing rules, plus:

```
wsl -d Ubuntu -u root -- bash -c "… cmake --preset linux-tsan && cmake --build build/linux-tsan && \
    ctest --test-dir build/linux-tsan --output-on-failure"
```

clean, and TSan confirmed **actually linked** rather than merely configured —
the same check `PROJECT_STATE.md` section 6.3 describes for ASan, because a
green run under a sanitizer that was not enabled is worse than no run.

## Done when

- [ ] `check` green in both trees.
- [ ] `linux-tsan` passes, with the sanitizer confirmed live.
- [ ] No raw `std::thread` anywhere; no unnamed lock guard anywhere.
- [ ] The frame path never blocks on a load, and the upload budget is enforced.
