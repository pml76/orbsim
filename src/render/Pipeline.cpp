#include "render/Pipeline.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/PushConstants.hpp"
#include "view/VertexLayout.hpp"

#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Vulkan's C structs are written here as designated initialisers that name the
// fields that matter and leave the rest -- pNext, flags, and often more --
// value-initialised to zero or null, which is what the specification asks of a
// field not in use. -Wmissing-designated-field-initializers reports every one
// of those, so it is off for this file, as it is for VulkanContext.cpp, the
// other file that fills in Vulkan's structs (ADR 0017; register decision 149,
// which the owner granted for this file). The cost is the same as there: a
// struct of our own initialised in this file goes unchecked too, which is why
// the descriptions this file reads are built elsewhere.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace orb::gfx {
namespace {

[[nodiscard]] std::unexpected<RenderError> fail(std::string message) {
    return std::unexpected(RenderError{.message = std::move(message)});
}

// VulkanContext.cpp has the same function, in its own anonymous namespace: a
// one-line helper each file keeps to itself rather than a shared header for
// two lines.
[[nodiscard]] std::expected<void, RenderError> vkCheck(VkResult result, std::string_view what) {
    if (result == VK_SUCCESS) return {};
    return fail(std::string(what) + " failed: " + string_VkResult(result));
}

// **Where a byte count becomes Vulkan's 32 bits**, and the only place in this
// file that narrows one (register decision 140: Vulkan's 32-bit fields are a
// boundary crossed in a named function). Every value reaching it is at most
// 2048 -- a stride or an offset within one, or a push-constant bound of 128
// -- and orbsim_view has already refused anything larger, so this can only
// fail if that code is wrong. Asserted, not reported (ADR 0002).
[[nodiscard]] std::uint32_t vulkanBytes(Bytes bytes) noexcept {
    ORBSIM_EXPECTS(std::in_range<std::uint32_t>(bytes.value()));
    return static_cast<std::uint32_t>(bytes.value());
}

// The same, for a number of elements handed to Vulkan beside a pointer.
[[nodiscard]] std::uint32_t vulkanCount(std::size_t count) noexcept {
    ORBSIM_EXPECTS(std::in_range<std::uint32_t>(count));
    return static_cast<std::uint32_t>(count);
}

// The translations. Each switch names every enumerator and has no default, so
// a new one is a -Wswitch error until it is translated. What follows each
// switch is reachable only by a value forged with a cast, and it is a value
// the validation layers refuse -- an undefined format, a zero stage mask, a
// *_MAX_ENUM -- so a forged enum is a validation error, which orbsim_smoke
// fails on, rather than a plausible pipeline.

[[nodiscard]] VkFormat toVulkan(view::AttributeFormat format) noexcept {
    switch (format) {
    case view::AttributeFormat::Float32x3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case view::AttributeFormat::Float32x4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkShaderStageFlags toVulkan(view::ShaderStages stages) noexcept {
    switch (stages) {
    case view::ShaderStages::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case view::ShaderStages::Fragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case view::ShaderStages::VertexAndFragment:
        return VkShaderStageFlags{VK_SHADER_STAGE_VERTEX_BIT} |
               VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT};
    }
    return VkShaderStageFlags{0};
}

[[nodiscard]] VkPrimitiveTopology toVulkan(Topology topology) noexcept {
    switch (topology) {
    case Topology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Topology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

[[nodiscard]] VkPolygonMode toVulkan(PolygonMode mode) noexcept {
    switch (mode) {
    case PolygonMode::Fill:
        return VK_POLYGON_MODE_FILL;
    case PolygonMode::Line:
        return VK_POLYGON_MODE_LINE;
    }
    return VK_POLYGON_MODE_MAX_ENUM;
}

[[nodiscard]] VkCullModeFlags toVulkan(CullMode mode) noexcept {
    switch (mode) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
}

[[nodiscard]] VkBool32 toVulkan(DepthTest test) noexcept {
    return test == DepthTest::Enabled ? VK_TRUE : VK_FALSE;
}

[[nodiscard]] VkBool32 toVulkan(DepthWrite write) noexcept {
    return write == DepthWrite::Enabled ? VK_TRUE : VK_FALSE;
}

[[nodiscard]] std::expected<UniquePipelineLayout, RenderError>
createLayout(VkDevice device, std::span<const view::PushConstantRange> ranges) {
    std::vector<VkPushConstantRange> vulkanRanges;
    vulkanRanges.reserve(ranges.size());
    for (const view::PushConstantRange& range : ranges) {
        vulkanRanges.push_back(VkPushConstantRange{
            .stageFlags = toVulkan(range.stages()),
            .offset = vulkanBytes(range.offset()),
            .size = vulkanBytes(range.size()),
        });
    }
    const VkPipelineLayoutCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = vulkanCount(vulkanRanges.size()),
        .pPushConstantRanges = vulkanRanges.data(),
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreatePipelineLayout(device, &info, nullptr, &layout),
                          "vkCreatePipelineLayout");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniquePipelineLayout{device, layout};
}

[[nodiscard]] std::vector<VkVertexInputAttributeDescription>
toVulkan(std::span<const view::VertexAttribute> attributes) {
    std::vector<VkVertexInputAttributeDescription> result;
    result.reserve(attributes.size());
    for (const view::VertexAttribute& attribute : attributes) {
        const VkFormat format = toVulkan(attribute.format);
        // **The translation above, against Vulkan's own format table.** No
        // validation layer can catch a wrong entry there: Vulkan lets a shader
        // read a vec4 from a three-component format and supplies the missing
        // alpha as 1.0, so describing a colour as three floats is a valid
        // pipeline that draws the wrong thing. The mutation pass found exactly
        // that surviving on 2026-09-24. Vulkan-Utility-Libraries' table is an
        // opinion formed without this code, so the component count and the
        // width are checked against it -- asserted, because a disagreement can
        // only be a mistake in the switch (ADR 0002).
        [[maybe_unused]] const bool agrees =
            vkuFormatComponentCount(format) == static_cast<std::uint32_t>(attribute.format) &&
            vkuFormatElementSize(format) == view::sizeOf(attribute.format).value();
        ORBSIM_EXPECTS(agrees);
        result.push_back(VkVertexInputAttributeDescription{
            .location = attribute.location.value,
            .binding = 0,
            .format = format,
            .offset = vulkanBytes(attribute.offset),
        });
    }
    return result;
}

// The fixed-function states that are plain values, one function each, so the
// function that assembles them fits on a screen (readability-function-size).

[[nodiscard]] VkPipelineInputAssemblyStateCreateInfo inputAssembly(Topology topology) noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = toVulkan(topology),
    };
}

[[nodiscard]] VkPipelineRasterizationStateCreateInfo
rasterization(const GraphicsPipelineDesc& desc) noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = toVulkan(desc.polygonMode),
        .cullMode = toVulkan(desc.cullMode),
        // Which winding is the front is the mesh's to say, and no mesh exists
        // yet; counter-clockwise is Vulkan's usual reading. It matters only
        // once a pipeline culls, and none does today.
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        // The only width every device supports without the wideLines feature.
        .lineWidth = 1.0F,
    };
}

[[nodiscard]] VkPipelineDepthStencilStateCreateInfo depthStencil(DepthState depth) noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = toVulkan(depth.test),
        .depthWriteEnable = toVulkan(depth.write),
        // Reverse-Z, from the one place it is written (ADR 0003).
        .depthCompareOp = kDepthCompareOp,
    };
}

[[nodiscard]] VkPipelineShaderStageCreateInfo stage(VkShaderStageFlagBits which,
                                                    VkShaderModule module) noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = which,
        .module = module,
        .pName = "main",
    };
}

// Viewport and scissor are set per frame by VulkanContext::beginRendering, so
// they are dynamic here. Baked in, every resize of the window would need every
// pipeline rebuilt -- the thing this file's header says never happens during
// a frame.
constexpr std::array kDynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

// The states every pipeline here shares, as values. What they point at is
// constant and lives at namespace scope, so returning them is safe.

// One viewport and one scissor, both set per frame (see kDynamicStates).
[[nodiscard]] VkPipelineViewportStateCreateInfo oneViewport() noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
}

[[nodiscard]] VkPipelineMultisampleStateCreateInfo singleSample() noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
}

// Opaque: every channel written, nothing blended. Each bit is converted to the
// unsigned mask type first, because the *FlagBits enums have a signed
// underlying type -- VulkanContext.cpp's note on the same conversion.
constexpr VkPipelineColorBlendAttachmentState kOpaque{
    .colorWriteMask = VkColorComponentFlags{VK_COLOR_COMPONENT_R_BIT} |
                      VkColorComponentFlags{VK_COLOR_COMPONENT_G_BIT} |
                      VkColorComponentFlags{VK_COLOR_COMPONENT_B_BIT} |
                      VkColorComponentFlags{VK_COLOR_COMPONENT_A_BIT},
};

[[nodiscard]] VkPipelineColorBlendStateCreateInfo opaqueBlend() noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &kOpaque,
    };
}

[[nodiscard]] VkPipelineDynamicStateCreateInfo dynamicViewportAndScissor() noexcept {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = vulkanCount(kDynamicStates.size()),
        .pDynamicStates = kDynamicStates.data(),
    };
}

[[nodiscard]] std::expected<UniquePipeline, RenderError>
createPipeline(VkDevice device, const GraphicsPipelineDesc& desc, VkPipelineLayout layout) {
    const std::array stages{
        stage(VK_SHADER_STAGE_VERTEX_BIT, desc.shaders.vertex),
        stage(VK_SHADER_STAGE_FRAGMENT_BIT, desc.shaders.fragment),
    };

    const VkVertexInputBindingDescription binding{
        .binding = 0,
        .stride = vulkanBytes(desc.vertexInput.stride),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const std::vector<VkVertexInputAttributeDescription> attributes =
        toVulkan(desc.vertexInput.attributes);
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = vulkanCount(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };

    const VkPipelineInputAssemblyStateCreateInfo assembly = inputAssembly(desc.topology);
    const VkPipelineViewportStateCreateInfo viewport = oneViewport();
    const VkPipelineRasterizationStateCreateInfo raster = rasterization(desc);
    const VkPipelineMultisampleStateCreateInfo multisample = singleSample();
    const VkPipelineDepthStencilStateCreateInfo depth = depthStencil(desc.depth);

    const VkPipelineColorBlendStateCreateInfo blend = opaqueBlend();
    const VkPipelineDynamicStateCreateInfo dynamic = dynamicViewportAndScissor();

    // Dynamic rendering: the attachment formats stand in for a render pass.
    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &desc.attachments.colour,
        .depthAttachmentFormat = desc.attachments.depth,
    };

    const VkGraphicsPipelineCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = vulkanCount(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (auto ok =
            vkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                    "vkCreateGraphicsPipelines");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniquePipeline{device, pipeline};
}

// The two modules a pipeline is built from, owned until the pipeline exists.
struct LoadedShaders {
    UniqueShaderModule vertex;
    UniqueShaderModule fragment;
};

// `<directory>/<name>.vert.spv` and `<name>.frag.spv`, as CMakeLists.txt
// writes them. A failure carries the path: loadShaderModule puts it there.
[[nodiscard]] std::expected<LoadedShaders, RenderError> loadShaders(
    const VulkanContext& context, const std::filesystem::path& directory, std::string_view name) {
    auto vertex = context.loadShaderModule(directory / (std::string(name) + ".vert.spv"));
    if (!vertex) return std::unexpected(vertex.error());
    auto fragment = context.loadShaderModule(directory / (std::string(name) + ".frag.spv"));
    if (!fragment) return std::unexpected(fragment.error());
    return LoadedShaders{.vertex = std::move(*vertex), .fragment = std::move(*fragment)};
}

} // namespace

GraphicsPipeline::GraphicsPipeline(UniquePipelineLayout layout, UniquePipeline pipeline) noexcept
    : layout_(std::move(layout)), pipeline_(std::move(pipeline)) {}

std::expected<GraphicsPipeline, RenderError>
GraphicsPipeline::create(VkDevice device, const GraphicsPipelineDesc& desc) {
    auto layout = createLayout(device, desc.pushConstants);
    if (!layout) return std::unexpected(layout.error());
    auto pipeline = createPipeline(device, desc, layout->get());
    if (!pipeline) return std::unexpected(pipeline.error());
    return GraphicsPipeline{std::move(*layout), std::move(*pipeline)};
}

ScenePipelines::ScenePipelines(Built built) noexcept
    : lines_(std::move(built.lines)), bodies_(std::move(built.bodies)) {}

std::expected<ScenePipelines, RenderError>
ScenePipelines::create(const VulkanContext& context, const std::filesystem::path& shaderDirectory) {
    const AttachmentFormats attachments{.colour = context.colorFormat(), .depth = kDepthFormat};

    auto lineShaders = loadShaders(context, shaderDirectory, "line");
    if (!lineShaders) return std::unexpected(lineShaders.error());
    const auto lineAttributes = view::kLineVertexLayout.attributes();
    const std::array linePushConstants{view::kLinePushConstantRange};
    auto lines = GraphicsPipeline::create(
        context.device(),
        {
            .shaders =
                {
                    .vertex = lineShaders->vertex.get(),
                    .fragment = lineShaders->fragment.get(),
                },
            .vertexInput =
                {
                    .stride = view::kLineVertexLayout.stride(),
                    .attributes = lineAttributes,
                },
            .topology = Topology::LineList,
            .polygonMode = PolygonMode::Fill,
            .cullMode = CullMode::None,
            .depth = {.test = DepthTest::Enabled, .write = DepthWrite::Disabled},
            .attachments = attachments,
            .pushConstants = linePushConstants,
        });
    if (!lines) return std::unexpected(lines.error());

    auto bodyShaders = loadShaders(context, shaderDirectory, "body");
    if (!bodyShaders) return std::unexpected(bodyShaders.error());
    const auto bodyAttributes = view::kBodyVertexLayout.attributes();
    const std::array bodyPushConstants{view::kBodyPushConstantRange};
    auto bodies = GraphicsPipeline::create(
        context.device(),
        {
            .shaders =
                {
                    .vertex = bodyShaders->vertex.get(),
                    .fragment = bodyShaders->fragment.get(),
                },
            .vertexInput =
                {
                    .stride = view::kBodyVertexLayout.stride(),
                    .attributes = bodyAttributes,
                },
            .topology = Topology::TriangleList,
            .polygonMode = PolygonMode::Fill,
            // No culling until a mesh exists to say which way its triangles
            // wind; see rasterization() above.
            .cullMode = CullMode::None,
            .depth = {.test = DepthTest::Enabled, .write = DepthWrite::Enabled},
            .attachments = attachments,
            .pushConstants = bodyPushConstants,
        });
    if (!bodies) return std::unexpected(bodies.error());

    // The four modules are destroyed on return: a pipeline keeps what it
    // compiled from them, and holding them longer would only hold memory.
    return ScenePipelines{{
        .lines = std::move(*lines),
        .bodies = std::move(*bodies),
    }};
}

} // namespace orb::gfx

#ifdef __clang__
#pragma clang diagnostic pop
#endif
