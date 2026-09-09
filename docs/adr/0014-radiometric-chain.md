# ADR 0014: The radiometric chain is manual photographic exposure and the AgX tonemap

Status: accepted (2026-09-08; recorded 2026-09-09)

Decision 16 of [the milestone 1 register](../plan/milestone-1-decisions.md).

## Decision

[`0006`](0006-simulation-not-sandbox.md) makes the image a truth problem, not
only a taste problem. Today `shaders/body.frag` lights the surface with
`0.04 + 0.96*pow(ndl, 0.85)` and the swapchain is `B8G8R8A8_UNORM`, so shaders
write display-ready colour. The chain that replaces it:

- **Shaders write physical radiance** -- W/m^2/sr per channel -- into a linear
  `R16G16B16A16_SFLOAT` target, recreated with the swapchain. Nothing writes
  display-ready colour anywhere except the final pass.
- **One resolve pass at the end**, in this order and only here: exposure
  multiply, then AgX, then the sRGB encode. Once, at the very end.
- **The swapchain stays UNORM and the encode stays explicit in the shader**,
  rather than switching to an `_SRGB` swapchain format and letting the hardware
  do it. Both are correct; explicit is chosen so the encode is visible in the
  code and cannot be applied twice by accident.
- **Exposure is manual and photographic**: `Aperture`, `ShutterTime` and `Iso`
  as strong types -- three adjacent bare doubles here would be I.24 again --
  combined by the standard photographic relations, each formula cited where it
  is written.
- **The radiometric-to-photometric convention is stated, not assumed.**
  Luminance comes from the luminance-weighted channel sum through a **luminous
  efficacy of 179 lm/W**, the convention *Radiance* uses, cited by name. Every
  number downstream of exposure depends on that one line, so it is a paragraph
  in a header rather than a constant in a shader.
- **The tonemap is AgX**, implemented from the MIT-licensed minimal
  implementation (Benjamin Wrensch; the constants derive from Troy Sobotka's
  OCIO configuration), attributed in the shader header and in
  [`../../THIRD_PARTY.md`](../../THIRD_PARTY.md). It is mirrored on the CPU
  with the same constants, for the probe comparison and the golden-image
  tooling.
- **No tuning constant exists anywhere in the chain.** If a value has to be
  tweaked until the image looks right, something above it is wrong. This is the
  clause that makes the rest of the record enforceable.
- **The chain is measured, not admired.** The HDR readback is within **0.5 %**
  of the analytic 129.97 W/m^2/sr for a Lambertian patch of albedo 0.3 normal
  to the Sun at 1 AU, taken *before* exposure, with the RGBA16F quantisation
  floor of 0.05 % stated so it is clear the budget tests the chain rather than
  the format. Exposure must not touch that readback: the dump is identical for
  two exposure settings and only the PNG differs, which is the whole
  architecture asserted in one test.
- **Auto-exposure is deferred**, and the condition on it is recorded now: when
  it lands it must be pinned in probe mode, or every golden image becomes a
  function of what else was on screen.

## What we considered

**ACES.** The industry default, widely understood, and available as the same
kind of small constant-driven transform. AgX was taken for its highlight
behaviour: bright saturated sources desaturate toward white as they clip rather
than skewing hue, and a sunlit limb, a solar disc and specular sea glint are
exactly the inputs that expose the difference. ACES remains a defensible
answer, and the CPU/GPU port check in
[M1-15](../plan/tasks/m1-15-exposure-and-agx.md) is deliberately described as a
port check rather than validation, because a display transform is a choice and
not a physical claim.

**Keeping the LDR pipeline and hand-tuning constants.** What exists today, and
it can be made to look good. It cannot be made to be *right*: correct
scattering rendered through it would be tonemapped by constants somebody chose,
so the atmosphere's 1 % and 5 % budgets in phase D would be measuring the
tuning. [`../plan/realism.md`](../plan/realism.md) ranks this item 1 of 14 for
the harder reason -- every shader written before the change is rewritten after
it.

**Auto-exposure now.** It is what a real camera in space does, it is what makes
a terminator sunrise readable without a manual adjustment, and a simulator will
eventually want it. It also makes every frame a function of the scene's
history, which is incompatible with the deterministic single frames
[`0008`](0008-renderer-verification.md) depends on unless it is pinned. Deferred
with that condition attached rather than argued about later.

## Why

The failure this record prevents is subtle and common: a renderer that looks
plausible and cannot be checked. Once a tuning constant sits anywhere between
the scattering integral and the pixel, no downstream number means anything --
the atmosphere LUT's 1 % budget, the radiometry probe's 0.5 %, and any future
comparison against a photograph all measure the constant instead of the model.

Splitting the chain at the HDR target is what makes the two halves separately
checkable. Before it, everything is physics and is asserted against analytic
values in W/m^2/sr. After it, everything is a display choice and is verified
only to be implemented identically on the CPU and the GPU. Exposure sits on the
display side of that line, which is why the readback must not see it.

The 179 lm/W line is the smallest and most easily lost part of this, and it is
called out because a chain with an unstated radiometric-to-photometric
convention has an arbitrary scale factor in it, and an arbitrary scale factor
is a tuning constant wearing a unit.

## What this record does not decide

- **The default aperture, shutter and ISO**, or whether they are per-scenario.
- **Whether auto-exposure ships in milestone 1.**
- **Bloom, glare and lens effects.** None is in scope; each would sit on the
  display side of the line above.
- **The display transfer function beyond sRGB.** HDR output to an HDR monitor
  is a later question and does not disturb anything before the resolve pass.
