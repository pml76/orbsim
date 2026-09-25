# M1-14 — The HDR render target

Phase: A | Status: **done, 2026-09-24**
Prerequisites: M1-13
Decided by: [ADR 0014](../../adr/0014-radiometric-chain.md)

> **KNOWN GAPS, declared in `scripts/mutants/m1-14.json`: nothing reads a pixel back before M1-16, so a resolve pass that draws the wrong thing is a valid program.** Accepted by the owner in advance on 2026-09-24: the shader's encode is not checked numerically until M1-18 compares it with `view/Srgb.hpp`, and the two refusals -- a device without the HDR format's features, a surface with no UNORM sRGB format -- cannot be reached on this machine. **Found by the mutation pass and accepted by the owner on 2026-09-25** (register decision 171): scene pipelines built for the swapchain's format, a resolve pass never drawn, and a full-screen triangle a quarter the size all survive every test. **M1-16's probe frames and M1-19's first draw are what kill them**, and whoever closes those tasks re-runs the mutants and removes the declarations.

**Twelve questions went up before any code was written and were ruled the same
day**: decisions 157-168 of the [register](../milestone-1-decisions.md). The
owner asked first what each recommendation would cost against its
alternative, and that the renderer be flexible about what different hardware
supports; the costs were then measured rather than estimated, and on the
owner's request the measurement was made repeatable on any machine (decision
169, [`../../measurements/m1-14-frame-cost.md`](../../measurements/m1-14-frame-cost.md)).
Three rulings change what this document says, and are marked where they do.

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

In `src/render/VulkanContext.cpp` / `.hpp`. *(Amended 2026-09-24, register
decision 157: the resolve pass is its own class, `render/ResolvePass.hpp`,
created by the application and handed to `endFrame`; the HDR target stays in
`VulkanContext`.)*

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
  *(Amended 2026-09-24, register decision 160: it is on in code wherever the
  validation layers are, so `orbsim_smoke` runs it on every `check`.)*

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
  inverse to 1e-6. *(Amended 2026-09-24, register decision 165: 1e-6 would
  accept almost anything in double precision. The budgets are 8 ulp against
  50-digit references and 1e-15 for the round trip, each twice a
  measurement -- with the standard's own exception at the knee asserted
  rather than assumed away; see below.)*

Numeric proof that the *chain* is right is M1-18, which reads a pixel back and
compares it against an analytic radiance. That is deliberate: this task moves
the plumbing, the next two give it physical meaning, and the third measures it.

## Verification

The standing rules, plus the synchronization-validation run above.

## Done when

- [x] `check` green in both trees.
- [x] The scene renders to RGBA16F, and the swapchain is written by exactly one
      fullscreen pass.
- [x] Synchronization validation reports nothing on a manual run -- and
      since decision 160 on every `orbsim_smoke` run.
- [x] The comment in `VulkanContext.cpp` about shaders writing display-ready
      colour is gone, because it is no longer true.

## What was built

- **`src/view/Srgb.hpp`** -- IEC 61966-2-1's encode and decode for the CPU,
  in double precision, with `LinearValue` and `EncodedValue` so that encoding
  twice does not compile; clamped to [0, 1] as the GPU clamps, a NaN asserted.
- **`src/view/SceneClear.hpp`** -- the scene's clear colour as linear light:
  the old display colour decoded, so nothing visible changed (decision 163).
- **`src/render/ResolvePass.hpp` / `.cpp`** -- the one draw that writes the
  display: `shaders/fullscreen.vert` (one triangle, no vertex buffer) and
  `shaders/tonemap.frag` (`texelFetch` at the pixel, the sRGB encode), one
  descriptor set per frame in flight. Created after the scene pipelines and
  handed to `endFrame`, so a frame cannot be presented without it.
- **`src/render/VulkanContext`** -- the HDR target (`kHdrFormat`,
  `R16G16B16A16_SFLOAT`), created and rebuilt with the swapchain; its format
  support asked of the device and a missing feature reported by name; the
  swapchain taken from a list of four UNORM formats in the sRGB colour space
  and anything else refused by name (decisions 161 and 162); synchronization
  validation on wherever the validation layers are; `colorFormat()` renamed
  `swapchainFormat()`.
- **`src/render/Pipeline`** -- descriptor-set layouts in the description, a
  pipeline with no vertex buffer when it has no attributes, and the scene
  pipelines built for `kHdrFormat`.
- **`src/app/main.cpp`** -- creates the resolve pass, and a `DeviceIdleGuard`
  after it: the validation layers' first run found the resolve pass destroyed
  while the last frame still used it. The frame loop moved into its own
  function, which `readability-function-size` asked for.
- **Tests**: `tests/test_srgb.cpp`, 61 assertions in 8 cases, against values
  from `scripts/srgb-reference.py` (50-digit decimal arithmetic).

**Instruments seen to fail before they were trusted.** Synchronization
validation: a barrier planted to wait for nothing fails the run with exit 3
and "WRITE_AFTER_WRITE hazard detected" -- and the same fault with it switched
off passes silently, exit 0, which is the task's claim about the base layers
measured rather than repeated. The reference budget: set to zero, it reported
every point's distance, worst 4 ulp, before 8 was written down.

**What the standard does at its knee** (decision 165). IEC 61966-2-1's rounded
constants make the encode step *down* by 2.85e-8 at 0.0031308, so a value in a
window 7.28e-9 wide just above it decodes on the other branch and the round
trip is out by up to 2.33e-9. The test asserts the 1e-15 budget outside the
window, the window's position and width, and its error bounded by the
standard's decode jump. A first sweep had been too coarse to see it.

## What it costs

Measured with Vulkan timestamps, and repeatable on any machine with
`scripts/measure-frame-cost.py scripts/measurements/m1-14.json` (decision 169).
[`../../measurements/m1-14-frame-cost.md`](../../measurements/m1-14-frame-cost.md)
describes each test and holds the results; on this machine, medians of three:

| | |
|---|---|
| GPU frame before M1-14 | 22.83 us |
| GPU frame with M1-14 | 65.21 us -- **+42.4 us**, 0.25 % of a 60 Hz frame |
| of which the resolve pass | 39.78 us, 27.6 us per megapixel at 1600x900 |
| clearing the display first (decision 167) | 0.29 us |
| rewriting the descriptor set each frame (decision 158) | 0.25 us of CPU |
| synchronization validation on a 2 s validated run | not measurable |

## The mutation pass

**Eighteen mutants in `build/debug`, run twice.** All six sRGB mutants die in
`test_srgb`; the render mutants -- a missing layout transition, a barrier
that waits for nothing, the idle guard removed, the wrong descriptor layout,
and the two checks made to refuse what the device supports -- die in
`orbsim_smoke`, the barrier one only because synchronization validation is
on. Six survive, as declared: the known gaps at the top of this document.

1. **2026-09-24: 10 caught, 6 survived, 0 invalid, 2 hung.** The two hung
   mutants made renderer start-up fail, and a failed start-up opened
   `SDL_ShowSimpleMessageBox`, which waits for a click -- so `orbsim_smoke`
   hung until CTest's timeout on *any* start-up failure instead of failing.
   Older than M1-14; nothing had made start-up fail before.
2. **2026-09-25, after the owner's ruling (decision 170) -- a run with
   `--seconds` shows no dialog: 12 caught, 6 survived as declared, 0 invalid,
   0 hung.**

## Other compilers

Run now rather than at M1-23's gate (decision 168), and both found something
-- neither of it in M1-14's code.

- **gcc-14 found missing braces in four array initialisers in
  `tests/test_srgb.cpp`**, the same fault it found in `test_camera.cpp` on
  2026-09-22; fixed with the brace pair all three compilers accept. Linked by
  hand past the blocked header self-check, the suite passes under gcc: 61 of
  61, so the 8-ulp budget holds on a second maths library.
- **Both gcc-14 and MSVC rejected M1-13's `view/VertexLayout.hpp`** (missing
  braces, and a lambda gcc wants `noexcept`), and under MSVC every renderer
  file includes it, so the application did not build there at all. The fix
  was measured before it was proposed -- applied temporarily and reverted --
  and granted by the owner on 2026-09-25 (decision 172): with it, MSVC builds
  the whole tree with no warning and passes 225 of 225, and gcc 223 of 223.
- **A trap in the measuring, recorded so it is not walked into again**: the
  first MSVC run "passed 210 of 210" because the configure had failed and the
  tests ran the stale binaries of an earlier build. In one `cmd` line,
  `%PATH%` is expanded before any command on the line runs, so a `set PATH`
  after `vcvars64.bat` on the same line throws away what it added. A batch
  file, one command per line, does not have the problem.

