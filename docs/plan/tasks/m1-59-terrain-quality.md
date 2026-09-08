# M1-59 — Terrain quality settings

Phase: C | Status: not started
Prerequisites: M1-12, M1-50, M1-53

## Purpose

Terrain is the CPU- and IO-bound half of the renderer, which is exactly the
asymmetry ADR 0007 rejected a single global quality tier over: *"an RTX A2000
beside a laptop CPU… one tier cannot say 'high atmosphere, low terrain'."* This
task adds the fields that make that sentence true.

## What to implement

Fields on `RenderQuality`, with the types ADR 0007 requires:

- `screenSpaceError` as **`Pixels`** — a real quantity, on the f64 base, because
  2.5 px is a meaningful threshold and 2 is not more correct for being an
  integer.
- `maxQuadtreeLevel` as a `Count`, hard-capped at the format's 21 (17 for
  elevation) so a preset cannot ask for a level that cannot exist.
- `tileCacheBudget` as **`Mebibytes`**, feeding M1-34's budget directly.
- `loadsPerFrame` as a `Count`, the upload budget from M1-33 — the field that
  most directly trades sharpening speed against frame-time smoothness.

The four presets are given values that differ meaningfully, each with a comment
saying what machine it is for. The descriptor array size from M1-31 derives from
the cache budget rather than being a second, independent constant that can
silently disagree with it.

## Out of scope

The adaptive controller. A configuration file. Any setting that changes the
*data* — a lower preset draws fewer tiles, never different terrain.

## Tests

- **Each field has an effect**, asserted rather than assumed: a coarser
  `screenSpaceError` selects strictly fewer tiles for the same camera; a lower
  `maxQuadtreeLevel` caps the deepest selected level exactly; a smaller
  `tileCacheBudget` produces more evictions on the same camera path; a smaller
  `loadsPerFrame` spreads the same loads over more frames. A field nothing reads
  is a field that will be wrong when something finally does.
- **Bounds are enforced**: a preset asking for level 25, or a zero cache budget,
  is refused at construction — the presets are `constexpr` and a `static_assert`
  proves each is valid.
- **The geometry is unchanged**: at every preset, a tile that *is* drawn has the
  same vertices. Quality changes which tiles are drawn, never where the surface
  is. That distinction is the whole architecture, and it is one assertion.
- **The streaming budgets from M1-53 hold at every preset**, not only at High —
  a preset that thrashes is a preset that is worse than the one below it.

## The rule, again

`RenderQuality` still lives in `orbsim_view`, `orbsim_core` still does not link
it, and `-DORBSIM_BUILD_APP=OFF` still builds the physics and its tests. Phase E
adds the simulation, and M1-70 adds the assertion that closes the loop: the same
scenario at two presets puts the vessel in bit-identical places.

## Done when

- [ ] `check` green in both trees.
- [ ] Every field is demonstrably read by something.
- [ ] Presets are valid by `static_assert`, and out-of-range values are refused.
- [ ] ADR 0007's field list is updated to what actually exists.
