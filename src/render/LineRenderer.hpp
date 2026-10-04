#ifndef ORBSIM_RENDER_LINERENDERER_HPP
#define ORBSIM_RENDER_LINERENDERER_HPP
//
// The GPU half of the line renderer (M1-19; register decisions 282-285). The
// CPU half, which gathers the lines and narrows their positions, is
// view/LineBatch.hpp, where it can be tested without a graphics card.
//
// **One vertex buffer per frame in flight, host-visible, sized once.** A frame
// writes its lines into its own slot's buffer while the GPU may still be
// reading the other slot's, so a write never races a draw; each buffer holds
// the batch's whole capacity, so a frame never allocates. The buffer is
// written by VulkanContext::writeMapped, which flushes as it copies (decision
// 284).
//
// **Two steps, and the second needs the first's receipt.** `upload` copies a
// batch into a slot and returns `LinesToDraw`, which only it can make; `draw`
// records the draw of exactly those lines. A draw cannot be recorded for a
// slot nothing was written to, and the vertex count cannot disagree with what
// the buffer holds. The probe uploads once, when its scene is built, and draws
// when the frame is recorded -- the two moments a frame has too.
//
// **The pipeline is ScenePipelines::lines()** (decision 283): line list, depth
// tested with the reverse-Z comparison and not written, Bresenham's rule. It
// is a parameter rather than a member, because ScenePipelines owns it.
//
// **Both frame slots are drawn with since M1-21** (register decision 344): the
// window uploads its lines into the slot of each frame it begins. The probes
// render one frame and use the first slot alone, which until M1-21 was all
// that ran (decision 282).
//
#include "core/Attributes.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/LineBatch.hpp"
#include "view/PushConstants.hpp"
#include "view/RenderQuality.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <expected>

namespace orb::gfx {

// The lines one upload put into one frame slot: the receipt `draw` asks for.
// Made only by LineRenderer::upload.
class LinesToDraw {
public:
    [[nodiscard]] std::uint32_t frameIndex() const noexcept { return frameIndex_; }
    [[nodiscard]] view::VertexCount vertexCount() const noexcept { return vertexCount_; }

private:
    friend class LineRenderer;
    // Two different types, so they cannot be given the wrong way round.
    LinesToDraw(std::uint32_t frameIndex, view::VertexCount vertexCount) noexcept
        : frameIndex_(frameIndex), vertexCount_(vertexCount) {}

    std::uint32_t frameIndex_;
    view::VertexCount vertexCount_;
};

class LineRenderer {
public:
    // A host-visible vertex buffer for each frame in flight, each room for
    // `capacity` vertices -- the capacity of the batches it will be handed.
    [[nodiscard]] static std::expected<LineRenderer, RenderError>
    create(VulkanContext& context, view::VertexCount capacity);

    // Copies `batch` into the buffer of frame slot `frameIndex`. Call it once
    // that slot's previous frame has finished -- after beginFrame, which waits
    // for it -- and before the frame is submitted. A batch larger than the
    // renderer's capacity is refused, by writeMapped.
    [[nodiscard]] std::expected<LinesToDraw, RenderError>
    upload(VulkanContext& context, std::uint32_t frameIndex, const view::LineBatch& batch);

    // Records the draw of `lines` into `cmd`, inside the HDR target's
    // rendering, with `pipeline` -- ScenePipelines::lines(). An empty batch
    // records nothing, which is not an error. `quality` is taken by value,
    // as every draw takes it (ADR 0007), and nothing about lines depends on
    // it yet.
    void draw(VkCommandBuffer cmd,
              const LinesToDraw& lines,
              const GraphicsPipeline& pipeline,
              const view::LinePushConstants& pushConstants,
              view::RenderQuality quality) const;

    [[nodiscard]] view::VertexCount capacity() const noexcept { return capacity_; }

private:
    LineRenderer(std::array<UniqueBuffer, kFramesInFlight> buffers,
                 view::VertexCount capacity) noexcept;

    std::array<UniqueBuffer, kFramesInFlight> buffers_;
    view::VertexCount capacity_;
};

} // namespace orb::gfx

#endif // ORBSIM_RENDER_LINERENDERER_HPP
