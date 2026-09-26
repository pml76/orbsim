#include "render/Probes.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/ProbeGradient.hpp"
#include "view/ProbeImage.hpp"
#include "view/RenderQuality.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <expected>
#include <filesystem>
#include <utility>

namespace orb::gfx {

ProbeScene::ProbeScene(GraphicsPipeline pipeline,
                       view::ClearProbePushConstants pushConstants) noexcept
    : pipeline_(std::move(pipeline)), pushConstants_(pushConstants) {}

void ProbeScene::record(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.pipeline());
    vkCmdPushConstants(cmd,
                       pipeline_.layout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(pushConstants_),
                       &pushConstants_);
    // Three vertices, one instance: fullscreen.vert's triangle.
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

std::expected<ProbeScene, RenderError>
createClearScene(const VulkanContext& context,
                 const std::filesystem::path& shaderDirectory,
                 const ProbeConditions& conditions) {
    auto vertex = context.loadShaderModule(shaderDirectory / "fullscreen.vert.spv");
    if (!vertex) return std::unexpected(vertex.error());
    auto fragment = context.loadShaderModule(shaderDirectory / "probe_gradient.frag.spv");
    if (!fragment) return std::unexpected(fragment.error());

    const std::array pushConstants{view::kClearProbePushConstantRange};
    auto pipeline = GraphicsPipeline::create(
        context.device(),
        {
            .shaders = {.vertex = vertex->get(), .fragment = fragment->get()},
            .vertexInput = {.stride = Bytes{0}, .attributes = {}},
            .topology = Topology::TriangleList,
            .polygonMode = PolygonMode::Fill,
            // The triangle is the whole screen; which way it winds does not
            // matter, and culling it by mistake would draw nothing.
            .cullMode = CullMode::None,
            // Tested, so the frame depends on the project's reverse-Z
            // comparison: fullscreen.vert writes depth 0.5, which passes
            // GREATER against the cleared 0 and would fail LESS (register
            // decision 194). Not written: nothing is drawn after it.
            .depth = {.test = DepthTest::Enabled, .write = DepthWrite::Disabled},
            .attachments = {.colour = kHdrFormat, .depth = kDepthFormat},
            .pushConstants = pushConstants,
            .descriptorSetLayouts = {},
        });
    if (!pipeline) return std::unexpected(pipeline.error());

    const view::ClearProbeRamp ramp =
        view::clearProbeRamp(view::radianceExposure(view::exposureValue100(conditions.exposure)));
    return ProbeScene{std::move(*pipeline), view::toShaderRamp(ramp, view::kProbeImageSize)};
}

ProbeConditions clearConditions() {
    // A camera from constants the factory accepts: finite, a unit quaternion,
    // a field of view inside (0, pi), a positive near plane. A refusal here is
    // a defect in these four numbers, which is what an assertion is for.
    const auto camera =
        view::Camera::from(Position{0.0, 0.0, 0.0}, Quat{}, Radians{kPi / 4.0}, Metres{1.0});
    ORBSIM_ENSURES(camera.has_value());
    return {
        .epoch = kJ2000,
        .camera = camera.value(),
        .quality = view::RenderQuality::high(),
        .qualityName = "high",
        .exposure =
            {
                .aperture = view::Aperture::from(16.0).value(),
                .shutterTime = view::ShutterTime::from(Seconds{1.0 / 125.0}).value(),
                .iso = view::Iso::from(100.0).value(),
            },
    };
}

} // namespace orb::gfx
