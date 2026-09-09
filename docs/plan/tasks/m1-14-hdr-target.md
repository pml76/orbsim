# M1-14 — The HDR render target

Phase: A | Status: not started
Prerequisites: M1-13
Decided by: [ADR 0014](../../adr/0014-radiometric-chain.md)

## Purpose

Item 1 in `realism.md`'s priority list, and the highest-ranked item in the whole
document: *"every shader written before it would have to be rewritten after
it."* Today the swapchain is `B8G8R8A8_UNORM` with the comment "the shaders
write display-ready colours directly". That inverts here.

Nothing about the image changes visibly in this task — that is the point.
Rendering goes to a floating-point target and comes back through a pass that
does nothing but encode. Exposure and the tonemap are M1-15, so that if the
image *does* change, there is exactly one candidate for why.

## What to implement

In `src/render/VulkanContext.cpp` / `.hpp`.

- An offscreen colour target, **`VK_FORMAT_R16G16B16A16_SFLOAT`**, at swapchain
  resolution, recreated with the swapchain and owned by the same RAII handles.
  Format support is queried through `vkGetPhysicalDeviceFormatProperties` and
  **reported** if absent rather than assumed — it is universally supported for
  colour attachment and sampling, and "universally" is not a thing to rely on
  without checking once.
- `beginFrame` binds the HDR image and the existing depth image; the swapchain
  image is no longer a colour attachment for scene drawing.
- A **fullscreen resolve pass** at the end of the frame: `fullscreen.vert` (the
  three-vertex trick, no vertex buffer) and `tonemap.frag`, which in this task
  does **only** the linear-to-sRGB encode, one place, at the very end.
- The swapchain **stays UNORM** and the encode stays explicit in the shader,
  rather than switching to an `_SRGB` swapchain format and letting the hardware
  do it. Both work; explicit is chosen so the encode is visible in code and
  cannot be applied twice by accident. The header says so.
- The layout transitions and the barrier between "scene written to HDR image"
  and "HDR image sampled by the resolve pass" go through the existing
  `transitionImage`, and the validation layers' **synchronization validation**
  is switched on for at least one manual run — the base layers do not catch a
  read-after-write hazard, and this task creates the project's first one.

## Out of scope

Exposure, AgX, and any actual tone mapping — M1-15. Bloom. Auto-exposure.
Changing what the scene shaders compute; they still write what they wrote.

## Tests

- `orbsim_smoke` still exits 0 with the validation layers on, which now also
  covers the new attachment, the new pass and the new barrier.
- One manual run with **synchronization validation enabled** through `vkconfig`,
  and the log read. This is a rule-20-style act: the tool exists, and a tool
  nobody runs is a tool nobody has.
- The pure-function part — the sRGB transfer function — is tested in
  `orbsim_view` against its published piecewise definition at 0, at the
  0.0031308 knee from both sides, and at 1, plus a round trip through the
  inverse to 1e-6.

Numeric proof that the *chain* is right is M1-18, which reads a pixel back and
compares it against an analytic radiance. That is deliberate: this task moves
the plumbing, the next two give it physical meaning, and the third measures it.

## Verification

The standing rules, plus the synchronization-validation run above.

## Done when

- [ ] `check` green in both trees.
- [ ] The scene renders to RGBA16F, and the swapchain is written by exactly one
      fullscreen pass.
- [ ] Synchronization validation reports nothing on a manual run.
- [ ] The comment in `VulkanContext.cpp` about shaders writing display-ready
      colour is gone, because it is no longer true.
