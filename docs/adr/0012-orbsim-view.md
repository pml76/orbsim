# ADR 0012: `orbsim_view` holds the render-side maths that Vulkan never touches

Status: accepted (2026-09-08; recorded 2026-09-09)

Decision 17 of [the milestone 1 register](../plan/milestone-1-decisions.md).

## Decision

A great deal of what milestone 1 builds is render-side mathematics that never
touches a Vulkan type. So it gets its own library, and the rule that keeps
`tests/` free of graphics headers stops being a convention.

- **`src/view/`, built as `orbsim_view`.** It links `orbsim_core` and nothing
  else. `orbsim_render` and the application link it; `orbsim_core` does not,
  and **cannot** -- which is what makes the rule mechanical rather than
  remembered.
- **What lives there**: `Mat4` and the projection, the camera, `RenderQuality`,
  tile identity and the `TileSource` seam, the KTX2 reader and writer, the
  screen-space error metric, the WGS-84 ellipsoid, the image downsample and the
  two golden-image metrics, exposure and the CPU tonemap, the atmosphere CPU
  reference model, and the MFD's layout and number formatting.
- **A format enum of our own** where a Vulkan concept is needed as a value: a
  KTX2 file carries a Vulkan format number, but a number is not a header, and
  `orbsim_view` including `vulkan.h` would defeat the whole arrangement.
- **`RenderQuality` lives here, not in `src/render/`.**
  [`0007`](0007-render-quality-is-a-struct.md) put it in `src/render/`, and its
  load-bearing clause is untouched by the move: the claim that matters is that
  **the simulation cannot see it**, and `orbsim_core` links neither render-side
  library. What the move buys is that the quadtree, the atmosphere and the
  quality struct can all be tested together, headless. That record carries a
  dated note pointing here.
- **The structural check belongs to the task that creates the risk**:
  `-DORBSIM_BUILD_APP=OFF` must still build the core and its tests, and
  `orbsim_core` must not appear to link `orbsim_view` in any generated build
  file.

## What we considered

**One render target -- put all of it in `orbsim_render`.** Simplest, one fewer
CMake target, and it makes every test of the camera, the projection, the
quadtree's error metric, the tile scheme or the atmosphere reference require a
Vulkan header, a device, or both. In practice that means one of two things
happens: the tests are not written, or `tests/` acquires a Vulkan dependency
and the rule in [`../../CLAUDE.md`](../../CLAUDE.md) becomes a sentence nobody
can enforce. Both outcomes are worse than a second library.

**Putting it in `core/`.** Also gives headless tests, costs nothing to build,
and pushes rendering concepts *down* into the physics library -- the exact
direction [`../../CODING_GUIDELINES.md`](../../CODING_GUIDELINES.md) section 12
forbids. A `RenderQuality` reachable from `orbsim_core` is a `RenderQuality` a
physics translation unit can read, and then
[`0007`](0007-render-quality-is-a-struct.md) is a promise rather than a
property of the build.

## Why

The pressure to break the layering always arrives disguised as convenience, and
the honest observation is that discipline is not what has been holding this
line -- there simply has not been any render-side code yet. Milestone 1 adds
about a dozen files of it, all of which want tests, and the first one that
needs a device to test is the one that ends the rule.

This is [`0005`](0005-correctness-is-enforced-by-tools.md)'s thesis applied to
architecture: a checklist records intent and cannot notice that a step has been
silently doing nothing. A link graph notices immediately, at compile time, in
every build, without anyone remembering that it is supposed to.

The second benefit is the one that pays during phases C and D. A screen-space
error metric and an atmosphere reference model are numerical code with real
failure modes, and numerical code is tested the way the rest of this project
tests numerical code -- seeded sweeps, property tests, an independent CPU
reference, thousands of assertions per second on a machine with no GPU at all.
None of that is available to code that lives behind a Vulkan device.

## What this record does not decide

- **The file list.** It grows through the milestone; this record fixes where
  such files go, not which ones exist.
- **Whether the atmosphere CPU reference ships** or stays a test-side oracle.
- **Whether `orbsim_view` ever generates shader source**, which is a different
  question from holding the maths that a shader mirrors.
