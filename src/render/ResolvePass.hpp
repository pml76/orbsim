#ifndef ORBSIM_RENDER_RESOLVEPASS_HPP
#define ORBSIM_RENDER_RESOLVEPASS_HPP
//
// The resolve pass: the one draw that writes the display (M1-14; ADR 0014).
//
// The scene is drawn into the linear HDR target (kHdrFormat, in
// render/VulkanContext.hpp). This pass then covers the screen with a single
// triangle -- shaders/fullscreen.vert, no vertex buffer -- and
// shaders/tonemap.frag reads the HDR target and writes the swapchain image:
// the exposure multiply, AgX and the sRGB encode, in that order, the one place
// in the renderer that turns light into display values (M1-15).
// VulkanContext::endFrame records it, and a frame cannot be presented without
// it.
//
// **The exposure is fixed when the pass is created** (M1-15), and reaches the
// shader as a four-byte push constant on every draw. Nothing in milestone 1
// changes it while the application runs, and a probe (M1-16) that wants
// another creates the pass with another; auto-exposure, when it comes, is the
// moment to make it per frame (ADR 0014).
//
// **The HDR target is read at the fragment's own pixel** (texelFetch at
// gl_FragCoord), not sampled at a texture coordinate. The two images are the
// same size, so there is nothing to filter; and with no coordinates there is
// no way to flip the image or shift it by half a pixel, which is the class of
// defect decision 154 found once already. It also needs no sampler object.
//
// **One descriptor set per frame in flight, rewritten every frame.** A set
// may be rewritten only while no pending command buffer uses it, and a frame
// slot's set is used only by that slot's command buffer, whose fence
// beginFrame has already waited on. Rewriting it each frame means a resized
// window's new HDR target is picked up without anyone having to say so; the
// cost is one vkUpdateDescriptorSets per frame, measured in M1-14's record.
//
// Built once, after the context and after ScenePipelines -- so that a missing
// shader directory still names line.vert.spv first, which
// shader_missing_is_reported checks -- and held by the frame loop.
//
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Exposure.hpp"
#include "view/PushConstants.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace orb::gfx {

class ResolvePass {
public:
    [[nodiscard]] static std::expected<ResolvePass, RenderError>
    create(const VulkanContext& context,
           const std::filesystem::path& shaderDirectory,
           view::PerRadiance exposure);

    // Records the full-screen draw into the attachment currently being
    // rendered, reading `hdrTarget`, which must already be in
    // SHADER_READ_ONLY_OPTIMAL. `frameIndex` picks the frame slot's own
    // descriptor set; see the header comment for why that is safe to rewrite.
    void record(VkCommandBuffer cmd, std::uint32_t frameIndex, VkImageView hdrTarget) const;

private:
    // Everything the pass owns, by name, so the constructor cannot receive
    // two handles of one type the wrong way round (non-negotiable 1).
    struct Parts {
        VkDevice device{VK_NULL_HANDLE};
        UniqueDescriptorSetLayout setLayout;
        UniqueDescriptorPool pool;
        std::array<VkDescriptorSet, kFramesInFlight> sets{};
        GraphicsPipeline pipeline;
        view::TonemapPushConstants pushConstants;
    };
    explicit ResolvePass(Parts parts) noexcept;

    VkDevice device_{VK_NULL_HANDLE}; // non-owning: the context owns it and outlives this
    // Declared before the pipeline, which is built from the layout and so is
    // destroyed first.
    UniqueDescriptorSetLayout setLayout_;
    UniqueDescriptorPool pool_;
    std::array<VkDescriptorSet, kFramesInFlight> sets_{}; // freed with pool_
    GraphicsPipeline pipeline_;
    view::TonemapPushConstants pushConstants_;
};

} // namespace orb::gfx

#endif // ORBSIM_RENDER_RESOLVEPASS_HPP
