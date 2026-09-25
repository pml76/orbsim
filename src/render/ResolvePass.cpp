#include "render/ResolvePass.hpp" // SF.5: own header, first
#include "core/Scalar.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Exposure.hpp"
#include "view/PushConstants.hpp"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// Vulkan's C structs are written here as designated initialisers that name
// the fields that matter and leave the rest zero, as the specification asks
// of a field not in use. -Wmissing-designated-field-initializers reports every
// one of those, so it is off for this file, as it is for the other two files
// that fill in Vulkan's structs, VulkanContext.cpp and Pipeline.cpp: a warning
// raised where our code meets a library's interface, switched off at that
// site with the reason written there (ADR 0017; CLAUDE.md non-negotiable 10).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace orb::gfx {
namespace {

[[nodiscard]] std::unexpected<RenderError> fail(std::string message) {
    return std::unexpected(RenderError{.message = std::move(message)});
}

// The same one-line helper VulkanContext.cpp and Pipeline.cpp each keep.
[[nodiscard]] std::expected<void, RenderError> vkCheck(VkResult result, std::string_view what) {
    if (result == VK_SUCCESS) return {};
    return fail(std::string(what) + " failed: " + string_VkResult(result));
}

// Binding 0 of set 0: the HDR target, as an image the fragment shader reads
// texel by texel. SAMPLED_IMAGE rather than COMBINED_IMAGE_SAMPLER, because
// texelFetch needs no sampler (see the header).
[[nodiscard]] std::expected<UniqueDescriptorSetLayout, RenderError>
createSetLayout(VkDevice device) {
    const VkDescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout),
                          "vkCreateDescriptorSetLayout (resolve)");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueDescriptorSetLayout{device, layout};
}

// Exactly what the sets below need and no more: one sampled image per frame
// in flight.
[[nodiscard]] std::expected<UniqueDescriptorPool, RenderError> createPool(VkDevice device) {
    const VkDescriptorPoolSize size{
        .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = kFramesInFlight,
    };
    const VkDescriptorPoolCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = kFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &size,
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateDescriptorPool(device, &info, nullptr, &pool),
                          "vkCreateDescriptorPool (resolve)");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueDescriptorPool{device, pool};
}

[[nodiscard]] std::expected<std::array<VkDescriptorSet, kFramesInFlight>, RenderError>
allocateSets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
    layouts.fill(layout);
    const VkDescriptorSetAllocateInfo info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = kFramesInFlight,
        .pSetLayouts = layouts.data(),
    };
    std::array<VkDescriptorSet, kFramesInFlight> sets{};
    if (auto ok = vkCheck(vkAllocateDescriptorSets(device, &info, sets.data()),
                          "vkAllocateDescriptorSets (resolve)");
        !ok) {
        return std::unexpected(ok.error());
    }
    return sets;
}

// fullscreen.vert and tonemap.frag, into the swapchain's format, with no
// vertex buffer, no depth attachment, and tonemap.frag's one push constant,
// the exposure (view/PushConstants.hpp). The modules are
// destroyed on return; the pipeline keeps what it compiled from them.
[[nodiscard]] std::expected<GraphicsPipeline, RenderError>
createPipeline(const VulkanContext& context,
               const std::filesystem::path& shaderDirectory,
               VkDescriptorSetLayout setLayout) {
    auto vertex = context.loadShaderModule(shaderDirectory / "fullscreen.vert.spv");
    if (!vertex) return std::unexpected(vertex.error());
    auto fragment = context.loadShaderModule(shaderDirectory / "tonemap.frag.spv");
    if (!fragment) return std::unexpected(fragment.error());

    const std::array setLayouts{setLayout};
    const std::array pushConstants{view::kTonemapPushConstantRange};
    return GraphicsPipeline::create(
        context.device(),
        {
            .shaders = {.vertex = vertex->get(), .fragment = fragment->get()},
            .vertexInput = {.stride = Bytes{0}, .attributes = {}},
            .topology = Topology::TriangleList,
            .polygonMode = PolygonMode::Fill,
            // The triangle is the whole screen; which way it winds is
            // irrelevant, and culling it by mistake would draw nothing.
            .cullMode = CullMode::None,
            .depth = {.test = DepthTest::Disabled, .write = DepthWrite::Disabled},
            .attachments = {.colour = context.swapchainFormat(), .depth = VK_FORMAT_UNDEFINED},
            .pushConstants = pushConstants,
            .descriptorSetLayouts = setLayouts,
        });
}

} // namespace

ResolvePass::ResolvePass(Parts parts) noexcept
    : device_(parts.device),
      setLayout_(std::move(parts.setLayout)),
      pool_(std::move(parts.pool)),
      sets_(parts.sets),
      pipeline_(std::move(parts.pipeline)),
      pushConstants_(parts.pushConstants) {}

std::expected<ResolvePass, RenderError>
ResolvePass::create(const VulkanContext& context,
                    const std::filesystem::path& shaderDirectory,
                    view::PerRadiance exposure) {
    VkDevice device = context.device();
    auto setLayout = createSetLayout(device);
    if (!setLayout) return std::unexpected(setLayout.error());
    auto pool = createPool(device);
    if (!pool) return std::unexpected(pool.error());
    auto sets = allocateSets(device, pool->get(), setLayout->get());
    if (!sets) return std::unexpected(sets.error());
    auto pipeline = createPipeline(context, shaderDirectory, setLayout->get());
    if (!pipeline) return std::unexpected(pipeline.error());

    return ResolvePass{Parts{
        .device = device,
        .setLayout = std::move(*setLayout),
        .pool = std::move(*pool),
        .sets = *sets,
        .pipeline = std::move(*pipeline),
        .pushConstants = {.radianceExposure = view::toShaderExposure(exposure)},
    }};
}

void ResolvePass::record(VkCommandBuffer cmd,
                         std::uint32_t frameIndex,
                         VkImageView hdrTarget) const {
    VkDescriptorSet set = sets_.at(frameIndex);
    const VkDescriptorImageInfo image{
        .imageView = hdrTarget,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image,
    };
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.pipeline());
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout(), 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd,
                       pipeline_.layout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(pushConstants_),
                       &pushConstants_);
    // Three vertices, one instance: the triangle fullscreen.vert builds from
    // gl_VertexIndex alone.
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace orb::gfx

#ifdef __clang__
#pragma clang diagnostic pop
#endif
