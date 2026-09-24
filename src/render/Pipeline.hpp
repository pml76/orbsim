#ifndef ORBSIM_RENDER_PIPELINE_HPP
#define ORBSIM_RENDER_PIPELINE_HPP
//
// Graphics pipelines and the shader modules they are built from (M1-13).
//
// **Pipelines are created at load, never during a frame.** Creating one is
// where the driver compiles the shaders to the card's own instructions, which
// takes milliseconds -- a frame at 60 Hz is 16.7 -- so a pipeline created on
// first use is a stutter the first time anything new appears on screen. That
// is the kind of rule that erodes one convenient exception at a time, so it is
// written here rather than remembered.
//
// **What a pipeline is told comes from orbsim_view.** The vertex layout and
// the push-constant ranges are described in view/VertexLayout.hpp and
// view/PushConstants.hpp, without Vulkan, where tests/ can check the
// arithmetic without a graphics card (ADR 0012). This file only copies those
// descriptions into Vulkan's structs, field for field.
//
// **Depth is reverse-Z, and not a choice made here.** The comparison comes
// from kDepthCompareOp in render/VulkanContext.hpp, and the description below
// has no field for it (ADR 0003).
//
// **What checks this.** A pipeline cannot be inspected without a device and
// tests/ may not link Vulkan, so this half is verified by orbsim_smoke -- the
// application under the validation layers, which fails on any report -- until
// M1-16 and M1-17 verify pipelines by the frames they draw.
//
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/PushConstants.hpp"
#include "view/VertexLayout.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace orb::gfx {

// Not booleans, and not Vulkan's own enums either: each lists exactly the
// values this renderer uses, so a switch over one is exhaustive and a new
// value is a deliberate addition (CODING_GUIDELINES section 2).
enum class Topology : std::uint8_t {
    TriangleList,
    LineList,
};

enum class PolygonMode : std::uint8_t {
    Fill,
    Line, // wireframe; needs fillModeNonSolid, which VulkanContext requires
};

enum class CullMode : std::uint8_t {
    None,
    Back,
    Front,
};

enum class DepthTest : std::uint8_t {
    Disabled,
    Enabled,
};

enum class DepthWrite : std::uint8_t {
    Disabled,
    Enabled,
};

// Whether depth is tested and whether it is written. How it is compared is
// not here: see the header comment.
struct DepthState {
    DepthTest test{DepthTest::Enabled};
    DepthWrite write{DepthWrite::Enabled};
};

// The two stages every pipeline here has. A struct rather than two parameters
// because both are VkShaderModule and reversed they build a pipeline that the
// driver may well accept (non-negotiable 1). Not owned: the modules may be
// destroyed as soon as the pipeline exists.
struct ShaderModules {
    VkShaderModule vertex{VK_NULL_HANDLE};
    VkShaderModule fragment{VK_NULL_HANDLE};
};

// What the pipeline renders into, which dynamic rendering asks for when the
// pipeline is created rather than through a render pass object. One colour
// attachment, because there is one; a second is a change to make when there
// is one to describe.
struct AttachmentFormats {
    VkFormat colour{VK_FORMAT_UNDEFINED};
    VkFormat depth{kDepthFormat};
};

// A vertex layout from view/VertexLayout.hpp, seen through a span so the
// description does not depend on how many attributes there are. The span
// refers to storage the caller keeps alive until create() returns.
struct VertexInput {
    Bytes stride;
    std::span<const view::VertexAttribute> attributes;
};

// Everything a graphics pipeline is built from. A struct, not a parameter
// list: nine arguments is a struct that wants to exist (I.23).
struct GraphicsPipelineDesc {
    ShaderModules shaders;
    VertexInput vertexInput;
    Topology topology{Topology::TriangleList};
    PolygonMode polygonMode{PolygonMode::Fill};
    CullMode cullMode{CullMode::None};
    DepthState depth;
    AttachmentFormats attachments;
    std::span<const view::PushConstantRange> pushConstants;
};

// A pipeline and the layout it was built with, owned together.
//
// **Both, not only the pipeline** (register decision 147). The push-constant
// ranges belong to the layout, and a draw needs the layout every frame to send
// its push constants through; a create() that returned only the pipeline would
// destroy the layout on return. Declared layout first, so the pipeline is
// destroyed first.
class GraphicsPipeline {
public:
    [[nodiscard]] static std::expected<GraphicsPipeline, RenderError>
    create(VkDevice device, const GraphicsPipelineDesc& desc);

    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_.get(); }
    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_.get(); }

private:
    GraphicsPipeline(UniquePipelineLayout layout, UniquePipeline pipeline) noexcept;

    UniquePipelineLayout layout_;
    UniquePipeline pipeline_;
};

// The pipelines the scene draws with: one per pair of shaders in shaders/.
//
// Built once, after the context, from SPIR-V under `shaderDirectory`. A file
// that is missing or is not SPIR-V is reported with its path, because a build
// tree can be incomplete and the path is the useful half of the message.
class ScenePipelines {
public:
    [[nodiscard]] static std::expected<ScenePipelines, RenderError>
    create(const VulkanContext& context, const std::filesystem::path& shaderDirectory);

    // line.vert and line.frag: line lists over the scene, depth tested and not
    // written, so a line lying on a surface is drawn rather than fought over.
    [[nodiscard]] const GraphicsPipeline& lines() const noexcept ORBSIM_LIFETIMEBOUND {
        return lines_;
    }
    // body.vert and body.frag: filled triangles, depth tested and written.
    [[nodiscard]] const GraphicsPipeline& bodies() const noexcept ORBSIM_LIFETIMEBOUND {
        return bodies_;
    }

private:
    // The two pipelines by name rather than as two parameters: both are
    // GraphicsPipeline, and reversed they would draw lines with the body
    // shaders without anything noticing (non-negotiable 1).
    struct Built {
        GraphicsPipeline lines;
        GraphicsPipeline bodies;
    };
    explicit ScenePipelines(Built built) noexcept;

    GraphicsPipeline lines_;
    GraphicsPipeline bodies_;
};

} // namespace orb::gfx

#endif // ORBSIM_RENDER_PIPELINE_HPP
