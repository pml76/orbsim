# orbsim

A space flight simulator in the spirit of [Orbiter](https://github.com/orbitersim/orbiter):
real orbital mechanics, 6-DOF vessels, and MFD-style instrumentation. Written
from scratch in C++23 with Vulkan.

**Status: early.** The two-body core is complete and tested; the renderer opens
a window, creates a device and paces frames. Nothing is drawn yet. See
[the milestone 1 plan](docs/plan/milestone-1-earth.md) for what comes next.

---

## What works today

- **Two-body orbital mechanics** — state vectors, classical elements, and
  universal-variable propagation covering elliptic, parabolic and hyperbolic
  trajectories through one code path. Escape trajectories are ordinary here, not
  a special case.
- **732 test assertions**, the useful ones crossing two independent
  implementations against each other: universal-variable propagation against
  Kepler-element propagation, state→elements against elements→state. A sign
  error in one cannot hide behind the other.
- **Vulkan 1.3 renderer foundation** — dynamic rendering, synchronization2, VMA
  allocation, reverse-Z depth. Runs clean under the validation layers through
  startup, frame loop and teardown.

## Design

Three decisions that are expensive to reverse, so they were made early:

**The simulation core knows nothing about rendering.** `orbsim_core` builds and
runs headless — no Vulkan, no SDL. That is what makes the physics testable
without a GPU, and it is enforced as a link dependency rather than a convention.

**`f64` everywhere in the simulation; `f32` only at the GPU boundary.** The
narrowing happens in one named function, after a camera-relative transform has
brought the magnitudes down. `-ffp-contract=off` is set, and `-ffast-math` never
will be: `orbitInfo()` returns infinity on purpose, for hyperbolic orbits.

**Reverse-Z depth with an infinite far plane.** A conventional depth buffer
z-fights badly long before it reaches from a cockpit panel to a planet a hundred
million kilometres away.

## Building

Needs a C++23 compiler with `<expected>`, `<print>` and `<ranges>` — clang 17+
or MSVC 19.36+ — plus CMake 3.25, Ninja, and a Vulkan loader. Dependencies
(SDL3, vk-bootstrap, VMA, Vulkan-Headers) are fetched and pinned by CMake.

```
cmake --preset relwithdebinfo
cmake --build build/relwithdebinfo
ctest --test-dir build/relwithdebinfo --output-on-failure
```

`orbsim --validate` enables the Vulkan validation layers from a release build.

## Code standards

The project has an opinionated, enforced house style:

- [`CODING_GUIDELINES.md`](CODING_GUIDELINES.md) — the rules and the arguments,
  cross-referenced against the C++ Core Guidelines
- [`coding-guidelines-example/`](coding-guidelines-example/) — a small,
  standalone program in which every one of those rules is followed and none is
  violated, with a coverage map
- [`CLAUDE.md`](CLAUDE.md) — the short version

The build treats the full warning set as errors, and clang-tidy runs at
`WarningsAsErrors: '*'`. Units live in the type system: `Radians`, `Metres`,
`Seconds` and `GravParam` are distinct types, so `propagate(state, dt, mu)` does
not compile.

## Licence

MIT — see [LICENSE](LICENSE).

### Acknowledgements

**Orbiter**, by Dr Martin Schweiger, is the inspiration and the reference.
[Its source](https://github.com/orbitersim/orbiter) is MIT-licensed, and its
`Doc/Orbiter Developer Manual/PLANETS.tex` documents the planetary tile format
this project will eventually read. Note that `OVP/D3D9Client/` within that
repository is LGPL, and the standalone `mschweiger/orbiter-tileedit` repository
is GPL v3 — no code from either is used here.

Planetary imagery comes from NASA's public-domain
[Blue Marble Next Generation](https://visibleearth.nasa.gov/collection/1484/blue-marble);
see [`data/textures/README.md`](data/textures/README.md).

Dependencies: [SDL3](https://github.com/libsdl-org/SDL) (zlib),
[Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) (Apache-2.0/MIT),
[vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) (MIT),
[VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT).
