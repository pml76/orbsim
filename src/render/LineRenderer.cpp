#include "render/LineRenderer.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/LineBatch.hpp"
#include "view/PushConstants.hpp"
#include "view/RenderQuality.hpp"
#include "view/VertexLayout.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace orb::gfx {

// line.vert alone reads the block (view/PushConstants.hpp), so the draw names
// the vertex stage; a range that grew another stage would fail here.
static_assert(view::kLinePushConstantRange.stages() == view::ShaderStages::Vertex);

LineRenderer::LineRenderer(std::array<UniqueBuffer, kFramesInFlight> buffers,
                           view::VertexCount capacity) noexcept
    : buffers_(std::move(buffers)), capacity_(capacity) {}

std::expected<LineRenderer, RenderError> LineRenderer::create(VulkanContext& context,
                                                              view::VertexCount capacity) {
    // In 64 bits: a 32-bit vertex count times a 28-byte vertex cannot wrap.
    const VkDeviceSize size =
        VkDeviceSize{capacity.value()} * VkDeviceSize{sizeof(view::LineVertex)};
    std::array<UniqueBuffer, kFramesInFlight> buffers;
    for (UniqueBuffer& buffer : buffers) {
        auto created = context.createBuffer({
            .size = size,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memory = Memory::HostVisible,
        });
        if (!created) return std::unexpected(created.error());
        buffer = *std::move(created);
    }
    return LineRenderer{std::move(buffers), capacity};
}

std::expected<LinesToDraw, RenderError> LineRenderer::upload(VulkanContext& context,
                                                             std::uint32_t frameIndex,
                                                             const view::LineBatch& batch) {
    ORBSIM_EXPECTS(frameIndex < kFramesInFlight);
    const std::span<const std::byte> bytes = std::as_bytes(batch.vertices());
    if (auto written = context.writeMapped(buffers_.at(frameIndex), bytes); !written) {
        return std::unexpected(written.error());
    }
    return LinesToDraw{frameIndex, batch.size()};
}

void LineRenderer::draw(VkCommandBuffer cmd,
                        const LinesToDraw& lines,
                        const GraphicsPipeline& pipeline,
                        const view::LinePushConstants& pushConstants,
                        view::RenderQuality /*quality*/) const {
    if (lines.vertexCount() == view::VertexCount{0U}) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
    VkBuffer buffer = buffers_.at(lines.frameIndex()).get();
    constexpr VkDeviceSize kOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &kOffset);
    vkCmdPushConstants(cmd,
                       pipeline.layout(),
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       static_cast<std::uint32_t>(sizeof(view::LinePushConstants)),
                       &pushConstants);
    vkCmdDraw(cmd, lines.vertexCount().value(), 1, 0, 0);
}

} // namespace orb::gfx
