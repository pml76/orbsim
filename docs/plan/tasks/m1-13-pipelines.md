# M1-13 — Graphics pipelines and shader modules

Phase: A | Status: not started
Prerequisites: M1-01

## Purpose

`PROJECT_STATE.md`: *"No pipelines, no drawing."* Four shaders have compiled to
SPIR-V on every build since the first commit and have never been loaded. This is
the task that makes the renderer able to draw anything at all.

## What to implement

`src/render/Pipeline.hpp` / `.cpp`, plus two handles in
`src/render/VulkanHandle.hpp`.

- `UniquePipeline` and `UniquePipelineLayout`, in the same move-only shape as
  the nineteen handles already there. No hand-written destructor anywhere else.
- **A description struct, not a parameter list**: `GraphicsPipelineDesc` carrying
  the shader modules, vertex input description, topology, polygon mode, depth
  state, colour attachment formats for dynamic rendering, and push-constant
  ranges. I.23 — five parameters is a struct that wants to exist.
- **No booleans.** `enum class DepthTest { Disabled, Enabled }`,
  `enum class DepthWrite { Disabled, Enabled }`,
  `enum class CullMode { None, Back, Front }`. `create(desc)` returning
  `std::expected<UniquePipeline, RenderError>`; a driver failure is the driver's
  to explain.
- **Reverse-Z in exactly one place**: the depth compare op is
  `VK_COMPARE_OP_GREATER`, set from a named constant beside `kDepthClear`, with
  the comment pointing at ADR 0003. A pipeline created with `LESS` out of habit
  draws nothing, and this is where that is prevented.
- Shader modules loaded through the existing `loadShaderModule`, from
  `ORBSIM_SHADER_DIR`, with a missing file reported by name rather than
  asserted — a build tree can be incomplete.
- Pipelines are created **at load**, never during a frame, so shader compilation
  cannot stall one. That is a sentence in the header, because it is the kind of
  rule that erodes.

## Out of scope

Anything drawn — the line renderer is M1-19 and the grid is M1-20. The HDR
target (M1-14). Descriptor sets, which arrive with textures in M1-31. Pipeline
caching to disk.

## Tests

**This task is the one place in phase A where verification is weaker than the
standard, and it is worth being explicit about why.** A `VkPipeline` cannot be
inspected without a device, and `tests/` may not link Vulkan. So:

- `orbsim_smoke` — the app under the validation layers for two seconds — now
  exercises pipeline layout creation, shader module loading and pipeline
  creation at startup, and still exits 0. A validation error makes it exit 3.
- The parts that *are* pure functions are tested in `orbsim_view`: the vertex
  input description built from a vertex type, and the push-constant range
  arithmetic (offset and size alignment), both checked against the values
  computed by hand in the test.
- A missing SPIR-V file reports `RenderError` with the path in the message,
  checked by pointing the loader at a path that does not exist.

The gap closes three tasks later: M1-16 renders a probe frame and M1-17 compares
it, at which point every pipeline in the project is verified by its output. That
sequencing is deliberate, and it is why the probe machinery comes before the
things it verifies rather than after.

## Verification

The standing rules, and `orbsim.exe --validate --seconds 3` exits 0 by hand at
least once, with the log read rather than skimmed.

## Done when

- [ ] `check` green in both trees, `orbsim_smoke` included.
- [ ] No new hand-written destructor outside `VulkanHandle.hpp`.
- [ ] Every `VkResult` in the new code goes through `vkCheck`.
- [ ] The depth comparison constant is named, commented and used once.
