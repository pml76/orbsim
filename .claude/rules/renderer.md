---
paths:
  - "src/render/**"
  - "src/app/**"
  - "shaders/**"
---

# Working in the renderer

Loaded automatically whenever renderer, application or shader code is touched.

**It must be fluent, and only the visuals scale.** A stuttering view from
orbit does not read as real however correct the scattering is, so frame time is
part of the image. But physics fidelity is not a quality knob: dropping J2 to
gain frames yields a different simulation, not a faster one. Physics cost is
answered by decoupling -- fixed timestep, own clock, render-side interpolation.
Visual cost is answered by quality settings.

**The rule that keeps both true: a quality setting must never reach the
simulation state.** The same scenario at the lowest and highest settings puts
the vessel in the same place, bit for bit; the quality controller may read the
frame clock and the physics may not. The mechanism is a `RenderQuality` struct
of per-feature settings that lives in `src/render/`, so the rule is enforced by
the link graph -- physics code that reaches for a quality setting does not
compile. See [`docs/adr/0007`](../../docs/adr/0007-render-quality-is-a-struct.md)
and [`docs/plan/realism.md`](../../docs/plan/realism.md) section 6.

**The dependency direction is one-way and load-bearing.** `orbsim_core` builds
and runs headless: no Vulkan, no SDL. Never push a renderer concept downward
into the physics — push the dependency the other way instead. The check that
proves it still holds: `-DORBSIM_BUILD_APP=OFF` builds the core and its tests.

**Every `VkResult` is checked**, through `vkCheck`, and every function that can
fail says so in its return type. A dropped result is how a lost device becomes
a hang three frames later.

**`f64` in the simulation; `f32` only at the GPU boundary**, in one named
function, subtracting before narrowing. Never touch the floating-point flags.

**Rule of Zero.** Every hand-written destructor in the renderer lives in
`render/VulkanHandle.hpp` (and `app/SdlHandle.hpp`). If you are adding one
anywhere else, wrap the resource instead. Destruction order is reverse
declaration order, which is a rule the language enforces rather than one a
person has to remember — and `DeviceIdleGuard`, declared last and destroyed
first, is why teardown no longer races the GPU.

`orbsim.exe --validate --seconds 3` runs the app under the Vulkan validation
layers for three seconds. Exit codes: 1 failure, 2 usage, 3 validation errors
reported. That run is a CTest test (`orbsim_smoke`, label `gpu`) and a
validation error fails it.

Reverse-Z depth with an infinite far plane is
[`docs/adr/0003`](../../docs/adr/0003-reverse-z-depth.md); the Vulkan headers
are pinned by CMake rather than taken from the SDK, which is
[`docs/adr/0004`](../../docs/adr/0004-pinned-vulkan-headers.md).
