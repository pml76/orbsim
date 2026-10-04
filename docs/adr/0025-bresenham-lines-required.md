# ADR 0025: Lines are drawn by Bresenham's rule, which every device must offer

Status: **accepted** (2026-10-03, measured; written 2026-10-04), with
[M1-19](../plan/tasks/m1-19-line-renderer.md). Register decision 278.

## Decision

**Every pipeline that draws a line list asks for
`VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR`, and the renderer refuses a device
without it.** `VulkanContext` requires the extension `VK_KHR_line_rasterization`
and its feature `bresenhamLines` when it selects a device, beside the Vulkan
1.3 features it already requires; a device that lacks either is not chosen,
and if none is left the start-up error names Bresenham lines among what was
asked for. `render/Pipeline.cpp` chains the line state onto the rasterization
state of every pipeline whose topology is `LineList`. It is not a field of
`GraphicsPipelineDesc`: there is no line this renderer should draw by another
rule, so a field would be a choice that could only be made wrongly.

## Why

Vulkan lets each device choose which pixels a line one pixel wide covers,
unless the pipeline asks for a defined rule. The project's goldens are
compared after a 2x2 downsample with a cap of 4/255, and a line one pixel
aside moves a block far past that. Measured on 2026-10-03 with the `lines`
probe's scene ([the record](../measurements/m1-19-line-raster.md)):

| Card | `strictLines` | Default rule, against the RX 7900 XTX | Bresenham |
|---|---|---|---|
| AMD RX 7900 XTX (`1002-744c`) | false | -- (889 pixels lit) | the same 889, bit for bit |
| NVIDIA RTX A2000 (`10de-25ba`) | true | 1,003 pixels; 337 differ, by up to 171/255 | the same 889 pixels; 350 differ by 1/255 in value |
| Intel UHD (`8086-4626`) | false | identical | identical |

All three report `bresenhamLines = true`, so the requirement refuses none of
the cards this project runs on. With it, a line lands on the same pixels on
every card, and one card's golden differs from another's only by the rounding
decision 238 already allows.

## Considered

**Requiring Bresenham at once, unmeasured** -- rejected in favour of measuring
first (the owner's rule: measure rather than assume); the measurement then
required it. **Keeping the default rule and dropping the golden** -- departs
from [ADR 0008](0008-renderer-verification.md), which makes a probe's
golden the check of what is drawn. **One golden per card** was taken as well
(decision 287, [M1-110](../plan/tasks/m1-110-golden-per-card.md)), for safety
rather than instead: per-card goldens catch a change on one card, and the
rule makes the cards agree in the first place.

## What it costs, and what it does not cover

**Run time: not measured.** The application draws no lines until M1-21
(decision 282), so there is no frame to measure; the first frame that draws
lines is where the rule's cost, if any, is measured.

**This machine cannot see the rule dropped.** On the RX 7900 XTX the default
rule and Bresenham's light the same pixels, so the mutant that drops the
request from line-list pipelines survives here, declared in
`scripts/mutants/m1-19.json`; the RTX A2000's golden is what catches it.

**Triangles drawn as wireframe are not covered.** The rule is chained onto
line-list pipelines only, so a `TriangleList` pipeline with
`PolygonMode::Line`, whose edges Vulkan rasterizes as lines, is drawn by the
device's default rule today. The terrain wireframe of phase D is where that
matters, and where this decision is to be extended.
