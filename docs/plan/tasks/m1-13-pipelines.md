# M1-13 — Graphics pipelines and shader modules

Phase: A | Status: **done, 2026-09-24**
Prerequisites: M1-01, M1-09 *(Corrected 2026-09-21: the queue says each task lists its true
prerequisites so that a reordering can be reasoned about, and this task tests its pure
functions -- the vertex input description and the push-constant range -- in
`orbsim_view`, which M1-09 creates.)*

> **KNOWN GAP, accepted by the owner on 2026-09-24: no test can yet tell which way depth is compared.** Changing `kDepthCompareOp` from `GREATER` to `LESS` survives every test in the project, because nothing is drawn until M1-19 and `LESS` is a valid comparison the validation layers rightly accept. A pipeline built that way draws nothing at all (ADR 0003). It is the one declared survivor in `scripts/mutants/m1-13.json`, and **M1-16's probe frames and M1-19's lines probe are what must kill it** -- whoever closes those tasks re-runs this mutant and removes the declaration.

**Twelve questions went up before any code was written and were ruled the same
day**: decisions 142-153 of the [register](../milestone-1-decisions.md). Ten
were in the first round; two more -- what `create` returns, and where the
push-constant sizes come from -- were raised before the code that needed them,
because the first round had missed them. One changes what this document says,
and is marked where it does.

> **Amended 2026-09-25 (register decision 172): gcc-14 and MSVC rejected this task's `view/VertexLayout.hpp`** -- missing brace pairs and a lambda gcc wants `noexcept` -- which neither had seen, because this task left them to M1-23's gate. M1-14 ran them early and found it; under MSVC the application did not build at all. Fixed in its own commit, and both pass everything.

## Purpose

`PROJECT_STATE.md`: *"No pipelines, no drawing."* Four shaders have compiled to
SPIR-V on every build since the first commit and have never been loaded. This is
the task that makes the renderer able to draw anything at all.

## What to implement

`src/render/Pipeline.hpp` / `.cpp`. No new handles are needed.

- `UniquePipeline` and `UniquePipelineLayout` **already exist** in
  `src/render/VulkanHandle.hpp`, beside `UniqueShaderModule`, in the move-only
  shape the fifteen handles there use — they were written ahead of their first
  caller. This task uses them; it does not add them. No hand-written destructor
  anywhere else. *(Until 2026-09-13 this said to add two handles, which would
  have meant writing code that was already in the tree.)*
- **A description struct, not a parameter list**: `GraphicsPipelineDesc` carrying
  the shader modules, vertex input description, topology, polygon mode, depth
  state, colour attachment formats for dynamic rendering, and push-constant
  ranges. I.23 — five parameters is a struct that wants to exist.
- **No booleans.** `enum class DepthTest { Disabled, Enabled }`,
  `enum class DepthWrite { Disabled, Enabled }`,
  `enum class CullMode { None, Back, Front }`. `create(desc)` returning
  `std::expected<UniquePipeline, RenderError>`; a driver failure is the driver's
  to explain. *(Amended 2026-09-24, [register decision 147](../milestone-1-decisions.md):
  `GraphicsPipeline::create(device, desc)` returns
  `std::expected<GraphicsPipeline, RenderError>`, which owns the pipeline
  **and its layout**. The push-constant ranges belong to the layout, and a draw
  needs the layout every frame to send them through, so returning the pipeline
  alone would have destroyed the layout on return.)*
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

## What was built

- **`src/view/VertexLayout.hpp`** -- `AttributeFormat`, `ShaderLocation`,
  `VertexAttribute`, and `VertexLayout<N>::packed`, which computes locations
  and offsets from the formats in order. `LineVertex`, `BodyVertex` and their
  layouts, with `static_assert`s against `sizeof` and `offsetof`.
- **`src/view/PushConstants.hpp`** -- `ShaderStages`, `PushConstantError`,
  `ByteRange`, and `PushConstantRange::from`, which keeps the specification's
  five rules against the guaranteed 128 bytes. `LinePushConstants` and
  `BodyPushConstants`, pinned to the reflected shaders.
- **`src/render/Pipeline.hpp` / `.cpp`** -- the enums, `GraphicsPipelineDesc`,
  `GraphicsPipeline` and `ScenePipelines`. The two scene pipelines are built at
  start-up in `main.cpp`, after the context and before the first frame.
- **`kDepthClear` and `kDepthCompareOp`** now sit in `VulkanContext.hpp` beside
  `kDepthFormat`; `beginFrame` takes the `RenderQuality`; the application takes
  `--shader-dir <path>`.
- **Tests**: `tests/test_pipeline_inputs.cpp` (81 assertions in 7 cases), whose
  expected numbers were read from the compiled shaders with
  `spirv-cross --reflect`; and the CTest entry `shader_missing_is_reported`.

**The smoke test was made to fail before it was trusted.** With the body's
push-constant range declared for the vertex stage only, `orbsim --validate`
exits 3 and the validation layers name the rule
(`VUID-VkGraphicsPipelineCreateInfo-layout-07987`). A clean run of an
instrument never seen to fail proves nothing (`VERIFICATION.md` rule 23).

**What no test can see yet**: which way depth is compared -- the known gap
stated at the top of this document.

**The mutation pass, 2026-09-24**, eighteen mutants in `build/debug`, run three
times:

1. **16 caught, 2 survived.** One survivor was the declared one. The other was
   not expected: describing a four-component attribute to Vulkan as three.
   The validation layers accept it, because Vulkan lets a shader read a vec4
   from a three-component format and supplies the missing alpha as 1.0. Closed
   in the code, not in the mutant file: `render/Pipeline.cpp` now checks each
   translated format against Vulkan-Utility-Libraries' own format table.
2. **16 caught, 1 survived, 1 hung.** The new check fired inside the
   application, and the Windows debug runtime turned the abort into a modal
   dialog, so `orbsim_smoke` waited for a click until the harness gave up. A
   hung mutant is not a kill. The fix -- the same `_set_abort_behavior` call
   every suite makes -- needed a one-line lint suppression, which the owner
   granted (decision 155); finding it also found four test sources that had
   never been linted (decision 156).
3. **17 caught, 1 survived, 0 invalid, 0 hung.** The survivor is the declared
   one, the known gap at the top of this document. M1-12's pass was re-run
   the same day, because decision 156 changed the script that judges its
   probes: 26 of 26 caught, as before.

## Done when

- [x] `check` green in both trees, `orbsim_smoke` included.
- [x] No new hand-written destructor outside `VulkanHandle.hpp`.
- [x] Every `VkResult` in the new code goes through `vkCheck`.
- [x] The depth comparison constant is named, commented and used once.
- [x] **`projectionOf` is deleted from `view/Camera.hpp` and `view/Camera.cpp`**,
      with its test case in `tests/test_camera.cpp`. M1-11 added it as an
      explicitly temporary entry point so that its own suite could drive the
      whole chain and so that the camera's field of view and near plane were
      not carried unread for two tasks; this task owns the projection from
      here on. [Register decision 117](../milestone-1-decisions.md), ruled
      2026-09-22. The obligation is on this list rather than only in a
      comment because a comment nobody opens is not a plan.
