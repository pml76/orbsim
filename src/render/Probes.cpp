#include "render/Probes.hpp" // SF.5: own header, first
#include "astro/Sun.hpp"
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/Lambert.hpp"
#include "view/Mat4.hpp"
#include "view/ProbeGradient.hpp"
#include "view/ProbeImage.hpp"
#include "view/Projection.hpp"
#include "view/PushConstants.hpp"
#include "view/RenderQuality.hpp"
#include "view/VertexLayout.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <numbers>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace orb::gfx {
namespace {

// A push-constant block as the bytes vkCmdPushConstants copies.
template <typename Block> [[nodiscard]] std::vector<std::byte> bytesOf(const Block& block) {
    const std::span<const std::byte> bytes = std::as_bytes(std::span(&block, 1));
    return {bytes.begin(), bytes.end()};
}

// The two full-screen probes' pipeline: fullscreen.vert's triangle and the
// named fragment shader, into the HDR target.
[[nodiscard]] std::expected<GraphicsPipeline, RenderError>
fullscreenPipeline(const VulkanContext& context,
                   const std::filesystem::path& shaderDirectory,
                   std::string_view fragmentShader,
                   std::span<const view::PushConstantRange> pushConstants) {
    auto vertex = context.loadShaderModule(shaderDirectory / "fullscreen.vert.spv");
    if (!vertex) return std::unexpected(vertex.error());
    auto fragment = context.loadShaderModule(shaderDirectory / fragmentShader);
    if (!fragment) return std::unexpected(fragment.error());
    return GraphicsPipeline::create(
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
}

// The probes' exposures: "sunny 16", the application's own (register decision
// 176), and two stops brighter for `lambert-exposure` (decision 256).
[[nodiscard]] view::CameraSettings exposureAt(f64 fNumber) {
    return {
        .aperture = view::Aperture::from(fNumber).value(),
        .shutterTime = view::ShutterTime::from(Seconds{1.0 / 125.0}).value(),
        .iso = view::Iso::from(100.0).value(),
    };
}

constexpr f64 kSunny16 = 16.0;
constexpr f64 kTwoStopsBrighter = 8.0;

// The lambert scene's fixed parts (register decisions 269 and 270): a 100 m
// square centred 10 m in front of the camera. The albedo is the task's, 0.3 -- about the Earth's
// Bond albedo, which is why the task chose it, and comfortably inside 0 to 1.
constexpr f64 kPatchAheadMetres = 10.0;
constexpr f64 kPatchSideMetres = 100.0;
constexpr f64 kAlbedo = 0.3;

// Where the Sun is, as the direction toward it. Straight behind the camera,
// which looks down -z, lights the patch's front, the side the camera sees;
// straight beyond the patch lights only its back, so the cosine is exactly
// -1 and lambert.frag's clamp at zero is what decides every pixel (register
// decision 274).
constexpr Direction kSunBehindCamera{0.0, 0.0, 1.0};
constexpr Direction kSunBeyondPatch{0.0, 0.0, -1.0};

// The Sun's distance, the patch's tilt and the direction toward the Sun are
// three different types, so none can be given in another's place
// (non-negotiable 1).
[[nodiscard]] ProbeConditions
withLambert(Metres sunDistance, Radians tilt, const Direction& towardSun) {
    ProbeConditions conditions = clearConditions();
    conditions.picture = view::LambertScene{
        .albedo = view::Albedo::from(kAlbedo).value(),
        .sunDistance = sunDistance,
        // M1-08's inverse-square law from 1361 W/m^2 at 1 AU: exact at half
        // and twice the astronomical unit, as astro/Sun.hpp asserts.
        .irradiance = solarIrradianceAt(sunDistance),
        .patch =
            {
                .centre = Position{0.0, 0.0, -kPatchAheadMetres},
                .side = Metres{kPatchSideMetres},
                .tilt = tilt,
            },
        .towardSun = towardSun,
    };
    return conditions;
}

} // namespace

ProbeScene::ProbeScene(ProbeDraw draw) noexcept : draw_(std::move(draw)) {}

void ProbeScene::record(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_.pipeline.pipeline());
    if (draw_.vertices) {
        VkBuffer buffer = draw_.vertices.get();
        constexpr VkDeviceSize kOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &kOffset);
    }
    if (!draw_.pushConstants.empty()) {
        vkCmdPushConstants(cmd,
                           draw_.pipeline.layout(),
                           draw_.pushStages,
                           0,
                           static_cast<std::uint32_t>(draw_.pushConstants.size()),
                           draw_.pushConstants.data());
    }
    vkCmdDraw(cmd, draw_.vertexCount, 1, 0, 0);
}

std::expected<ProbeScene, RenderError>
createClearScene(const VulkanContext& context,
                 const std::filesystem::path& shaderDirectory,
                 const ProbeConditions& conditions) {
    const std::array pushConstants{view::kClearProbePushConstantRange};
    auto pipeline =
        fullscreenPipeline(context, shaderDirectory, "probe_gradient.frag.spv", pushConstants);
    if (!pipeline) return std::unexpected(pipeline.error());

    const view::ClearProbeRamp ramp =
        view::clearProbeRamp(view::radianceExposure(view::exposureValue100(conditions.exposure)));
    // Three vertices, one instance: fullscreen.vert's triangle.
    return ProbeScene{ProbeDraw{
        .pipeline = *std::move(pipeline),
        .vertices = {},
        .vertexCount = 3,
        .pushConstants = bytesOf(view::toShaderRamp(ramp, view::kProbeImageSize)),
        .pushStages = VK_SHADER_STAGE_FRAGMENT_BIT,
    }};
}

std::expected<ProbeScene, RenderError> createPortScene(const VulkanContext& context,
                                                       const std::filesystem::path& shaderDirectory,
                                                       const ProbeConditions& /*conditions*/) {
    auto pipeline = fullscreenPipeline(context, shaderDirectory, "probe_port.frag.spv", {});
    if (!pipeline) return std::unexpected(pipeline.error());
    return ProbeScene{ProbeDraw{
        .pipeline = *std::move(pipeline),
        .vertices = {},
        .vertexCount = 3,
        .pushConstants = {},
        .pushStages = {},
    }};
}

std::expected<ProbeScene, RenderError>
createLambertScene(VulkanContext& context,
                   const std::filesystem::path& shaderDirectory,
                   const ProbeConditions& conditions,
                   const view::LambertScene& scene) {
    const view::Camera& camera = conditions.camera;

    // The patch in world metres, then each corner relative to the camera and
    // narrowed, by toRenderSpace and nowhere else (register decision 267).
    const view::SquarePatch patch = view::squarePatch(scene.patch);
    std::array<view::LambertVertex, 6> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices.at(i) = view::toRenderSpace(patch.vertices.at(i), camera);
    }
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span(vertices));
    auto buffer = context.createBuffer({
        .size = vertexBytes.size(),
        .usage = VkBufferUsageFlags{VK_BUFFER_USAGE_VERTEX_BUFFER_BIT} |
                 VkBufferUsageFlags{VK_BUFFER_USAGE_TRANSFER_DST_BIT},
        .memory = Memory::DeviceLocal,
    });
    if (!buffer) return std::unexpected(buffer.error());
    if (auto uploaded = context.uploadBuffer(*buffer, vertexBytes); !uploaded) {
        return std::unexpected(uploaded.error());
    }

    // The view-projection, composed in f64 and narrowed once (decision 268).
    // A validated camera's projection can fail only on the aspect ratio, and
    // the probe frame's is a positive constant: a refusal is a defect here.
    const view::Aspect aspect{static_cast<f64>(view::kProbeImageSize.width) /
                              static_cast<f64>(view::kProbeImageSize.height)};
    const auto projection =
        view::infiniteReverseZPerspective(camera.verticalFov(), aspect, camera.nearPlane());
    ORBSIM_ENSURES(projection.has_value());
    const view::LambertPushConstants block = view::toShaderLambert(
        {
            .albedo = scene.albedo,
            .irradiance = scene.irradiance,
            .surfaceNormal = patch.normal,
            .towardSun = scene.towardSun,
        },
        view::toShaderMatrix(*projection * view::viewMatrix(camera)));

    auto vertexShader = context.loadShaderModule(shaderDirectory / "lambert.vert.spv");
    if (!vertexShader) return std::unexpected(vertexShader.error());
    auto fragmentShader = context.loadShaderModule(shaderDirectory / "lambert.frag.spv");
    if (!fragmentShader) return std::unexpected(fragmentShader.error());
    const auto attributes = view::kLambertVertexLayout.attributes();
    const std::array pushConstants{view::kLambertPushConstantRange};
    auto pipeline = GraphicsPipeline::create(
        context.device(),
        {
            .shaders = {.vertex = vertexShader->get(), .fragment = fragmentShader->get()},
            .vertexInput =
                {
                    .stride = view::kLambertVertexLayout.stride(),
                    .attributes = attributes,
                },
            .topology = Topology::TriangleList,
            .polygonMode = PolygonMode::Fill,
            // A surface (register decision 271): back faces culled, so a patch
            // wound the wrong way draws nothing and the whole-frame check in
            // tests/test_radiometry.cpp fails at once.
            .cullMode = CullMode::Back,
            .depth = {.test = DepthTest::Enabled, .write = DepthWrite::Enabled},
            .attachments = {.colour = kHdrFormat, .depth = kDepthFormat},
            .pushConstants = pushConstants,
            .descriptorSetLayouts = {},
        });
    if (!pipeline) return std::unexpected(pipeline.error());

    return ProbeScene{ProbeDraw{
        .pipeline = *std::move(pipeline),
        .vertices = *std::move(buffer),
        .vertexCount = static_cast<std::uint32_t>(vertices.size()),
        .pushConstants = bytesOf(block),
        .pushStages = VkShaderStageFlags{VK_SHADER_STAGE_VERTEX_BIT} |
                      VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT},
    }};
}

std::expected<ProbeScene, RenderError> createScene(VulkanContext& context,
                                                   const std::filesystem::path& shaderDirectory,
                                                   const ProbeConditions& conditions) {
    return std::visit(
        [&](const auto& picture) -> std::expected<ProbeScene, RenderError> {
            using Picture = std::decay_t<decltype(picture)>;
            if constexpr (std::is_same_v<Picture, view::LambertScene>) {
                return createLambertScene(context, shaderDirectory, conditions, picture);
            } else if constexpr (std::is_same_v<Picture, PortPicture>) {
                return createPortScene(context, shaderDirectory, conditions);
            } else {
                // Exhaustive: a fourth picture without a scene fails here.
                static_assert(std::is_same_v<Picture, GradientPicture>);
                return createClearScene(context, shaderDirectory, conditions);
            }
        },
        conditions.picture);
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
        .exposure = exposureAt(kSunny16),
        .picture = GradientPicture{},
    };
}

ProbeConditions tonemapPortConditions() {
    ProbeConditions conditions = clearConditions();
    conditions.picture = PortPicture{};
    return conditions;
}

ProbeConditions lambertConditions() {
    return withLambert(kAstronomicalUnit, Radians{0.0}, kSunBehindCamera);
}

ProbeConditions lambertHalfAuConditions() {
    return withLambert(kAstronomicalUnit * 0.5, Radians{0.0}, kSunBehindCamera);
}

ProbeConditions lambertTwoAuConditions() {
    return withLambert(kAstronomicalUnit * 2.0, Radians{0.0}, kSunBehindCamera);
}

ProbeConditions lambertTilted60Conditions() {
    return withLambert(kAstronomicalUnit, Radians{std::numbers::pi / 3.0}, kSunBehindCamera);
}

ProbeConditions lambertBacklitConditions() {
    return withLambert(kAstronomicalUnit, Radians{0.0}, kSunBeyondPatch);
}

ProbeConditions lambertExposureConditions() {
    ProbeConditions conditions = lambertConditions();
    conditions.exposure = exposureAt(kTwoStopsBrighter);
    return conditions;
}

} // namespace orb::gfx
