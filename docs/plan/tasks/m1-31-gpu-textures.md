# M1-31 — GPU textures, samplers, descriptors

Phase: B | Status: not started
Prerequisites: M1-13, M1-26

## Purpose

The renderer has never bound a texture. This adds image upload, sampling and the
descriptor machinery — and it makes one forward-looking choice deliberately,
because the quadtree six tasks later will need hundreds of tiles bound at once
and retrofitting that is a rewrite.

## What to implement

`src/render/TextureCache.hpp` / `.cpp`.

- Image creation from a `TileData`: a `UniqueImage` with the full mip chain, an
  image view, and an upload through a staging buffer with one layout transition
  in and one out, batched per upload rather than per mip.
- A **sampler** with trilinear filtering, anisotropy at the device maximum
  (queried, not assumed), and `CLAMP_TO_EDGE` — a tile must never wrap, or the
  seam at 180° smears the far side of the planet across it.
- **Descriptor indexing**: one descriptor set holding a fixed-size array of
  combined image samplers, with tiles referenced by index through a push
  constant. Descriptor indexing is core in Vulkan 1.2 and this project targets
  1.3, so it costs nothing; the alternative — one descriptor set per tile — is
  what would have to be undone in M1-51.
- The array size is a named constant now and becomes a `RenderQuality` field in
  M1-59, when the cache budget does.
- Every `VkResult` through `vkCheck`, every handle owned by a `Unique*`, and the
  device's format support for BC7 **queried and reported** rather than assumed —
  it is universal on desktop, and "universal" is worth one check.

## Out of scope

Asynchronous upload (M1-33) — this path is synchronous, like the existing
`uploadBuffer`. Eviction (M1-34). Streaming budgets. Compressed formats other
than BC7 and the 16-bit elevation format, which arrives in M1-55.

## Tests

Vulkan objects cannot be inspected from `tests/`, so verification is split:

- The pure arithmetic goes to `orbsim_view` and is tested there: mip level count
  from dimensions, the byte size of a BC7 level (block count × 16), the staging
  buffer size for a full chain, and the descriptor index allocator — allocate,
  free, reallocate, and assert an index is never handed out twice.
- `orbsim_smoke` covers creation and destruction under the validation layers.
- The visual and numeric proof is M1-32, one task later, which draws a tile
  whose contents are known.

## Frames to look at

None yet — the next task produces the first textured frame. If it is useful to
see something now, the `clear` probe still runs and should be unchanged, which
is itself worth confirming: adding a descriptor set should not alter a frame
that does not use one.

## Verification

The standing rules, plus a manual run with synchronization validation on, since
this task adds the project's second class of image barrier.

## Done when

- [ ] `check` green in both trees.
- [ ] Anisotropy and BC7 support are queried, and their absence reported.
- [ ] The descriptor index allocator has a test that proves it never
      double-allocates.
- [ ] `clear.png` is unchanged, byte for byte.
