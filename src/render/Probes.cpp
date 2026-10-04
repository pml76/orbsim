#include "render/Probes.hpp" // SF.5: own header, first
#include "astro/EarthOrientation.hpp"
#include "astro/Sun.hpp"
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "render/LineRenderer.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/Lambert.hpp"
#include "view/LineBatch.hpp"
#include "view/Mat4.hpp"
#include "view/PlanetaryGrid.hpp"
#include "view/ProbeGradient.hpp"
#include "view/ProbeImage.hpp"
#include "view/Projection.hpp"
#include "view/PushConstants.hpp"
#include "view/RenderQuality.hpp"
#include "view/VertexLayout.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cmath>
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

// The `lines` probe's scene (register decision 280): axes 1 m long from the
// origin, and a unit square in the XY plane from (0.25, 0.25) to (1.25, 1.25)
// -- touching no axis, so each line is seen on its own.
constexpr f64 kAxisLengthMetres = 1.0;
constexpr f64 kSquareLowMetres = 0.25;
constexpr f64 kSquareHighMetres = 1.25;

// The square's white: 130 W/(m^2 sr) in each channel, the green axis's
// radiance and about the lambert patch's (decision 279).
constexpr view::Rgba kSquareColour{.r = 130.0F, .g = 130.0F, .b = 130.0F, .a = 1.0F};

// Room for exactly what the probe draws: three axes of two vertices each, and
// the square's four sides of two each.
constexpr view::VertexCount kLinesProbeCapacity{14U};

// The view-projection a probe's camera sees the probe frame through, composed
// in f64 and narrowed once (register decision 268). A validated camera's
// projection can fail only on the aspect ratio, and the probe frame's is a
// positive constant: a refusal is a defect here.
[[nodiscard]] view::Mat4f probeViewProjection(const view::Camera& camera) {
    const view::Aspect aspect{static_cast<f64>(view::kProbeImageSize.width) /
                              static_cast<f64>(view::kProbeImageSize.height)};
    const auto projection =
        view::infiniteReverseZPerspective(camera.verticalFov(), aspect, camera.nearPlane());
    ORBSIM_ENSURES(projection.has_value());
    return view::toShaderMatrix(*projection * view::viewMatrix(camera));
}

// The grid probes' epoch (register decisions 323 and 336): the row of the
// committed Skyfield fixture, data/skyfield/earth-orientation.txt, at JD
// 2460886.5 + 0.2699127197265625 TT -- 2025-07-30 near 06:29 TT -- with that
// row's own UT1, so that tests/test_probe_grid.cpp can hold the frame to a
// rotation ERFA did not compute. Both fractions are exact binary fractions,
// as the row writes them.
//
// **Why this row and not the one nearest the day M1-20 ran** (decision 336).
// At this instant the prime meridian points 44.7 degrees from the celestial
// x axis, so latitude 0, longitude 0 has two large coordinates, and the 1 km
// jitter camera's steps change both. On the row first chosen, 2026-09-29, it
// pointed within 0.3 degrees of the x axis: the large coordinate lay along
// the line of sight and never changed between frames, so narrowing to 32 bits
// before subtracting jittered by 0.0016 px there, invisible -- a defect
// planted that way passed the test. Here the same defect jitters by 0.43 px,
// thirteen times the budget, simulated on 2026-10-04.
constexpr JulianDate kGridEpochTt{.day = 2'460'886.5, .fraction = 0.2699127197265625};
constexpr JulianDate kGridEpochUt1{.day = 2'460'886.5, .fraction = 0.26911163330078125};

// A camera above a point of the celestial sphere, looking along a heading
// (register decision 324). The point is given by right ascension and
// declination in the celestial frame -- numbers, not the Earth's rotation, so
// that a wrong rotation moves the grid and leaves the camera where it is.
struct GridView {
    Degrees rightAscension; // of the point straight below
    Degrees declination;    //
    Metres altitude;        // above the WGS-84 sphere
    Degrees heading;        // east of north, north toward the celestial pole
    Degrees pitch;          // below the horizontal
};

// `grid-400km`'s view. The point below is where the fixture's row puts 6
// degrees south on the prime meridian, worked out from that row on
// 2026-10-04 and written to ten decimal places, 2e-5 m on the ground. 6
// degrees south rather than the 10 first proposed, which put latitude 10
// north, one of the number check's points, beyond the horizon; heading 35
// degrees, so that the equator and the prime meridian cross about 20 degrees
// away from the frame's rows and columns, where a line fit keeps its
// sub-pixel resolution; pitched 27.5 degrees down, 7.7 degrees more than the
// horizon's dip of 19.8 degrees at 400 km, so the limb crosses the upper
// third.
constexpr GridView k400kmView{
    .rightAscension = Degrees{44.7137335893},
    .declination = Degrees{-6.1029606260},
    .altitude = Metres{400e3},
    .heading = Degrees{35.0},
    .pitch = Degrees{27.5},
};

// The close jitter sequence's view (register decision 321): 1 km above where
// the row puts latitude 0, longitude 0, looking straight down, turned 33
// degrees so that the two lines cross 33 degrees from the frame's rows and
// columns -- not 45, where a one-pixel line repeats the same pattern every
// pixel and a fit loses its sub-pixel resolution (measured in simulation on
// 2026-10-04: 0.84 px at 45 degrees, 0.005 px at 33).
constexpr GridView k1kmView{
    .rightAscension = Degrees{44.7033267157},
    .declination = Degrees{-0.1029696166},
    .altitude = Metres{1e3},
    .heading = Degrees{33.0},
    .pitch = Degrees{90.0},
};

// How far the camera moves to its right between jitter frames: the task's
// 0.25 m (register decision 332).
constexpr f64 kJitterStepMetres = 0.25;

// The camera of `view`, moved `toTheRight` along its own right. The
// orientation runs camera to world (view/Camera.hpp), so its columns are the
// camera's right, up and back in the celestial frame, as linesConditions
// builds them.
[[nodiscard]] view::Camera gridCamera(const GridView& view, Metres toTheRight) {
    const f64 ra = toRadians(view.rightAscension).value();
    const f64 dec = toRadians(view.declination).value();
    const f64 heading = toRadians(view.heading).value();
    const f64 pitch = toRadians(view.pitch).value();
    const Direction below{
        std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};
    const Direction east = directionOf(cross(Direction{0.0, 0.0, 1.0}, below));
    const Direction north = cross(below, east);
    const Direction ahead = (north * std::cos(heading)) + (east * std::sin(heading));
    const Direction looking = (ahead * std::cos(pitch)) - (below * std::sin(pitch));
    const Direction right = (east * std::cos(heading)) - (north * std::sin(heading));
    const Direction back = -looking;
    const Direction up = cross(back, right);
    const RotationMatrix rotation{
        // std::to_array, row by row: MSVC (C5246) and gcc want a std::array's
        // inner braces written out.
        .rows = std::to_array({
            std::to_array({right.x.value(), up.x.value(), back.x.value()}),
            std::to_array({right.y.value(), up.y.value(), back.y.value()}),
            std::to_array({right.z.value(), up.z.value(), back.z.value()}),
        }),
    };
    const f64 distance = view::kWgs84SemiMajorAxis.value() + view.altitude.value();
    const f64 aside = toTheRight.value();
    const Position position{(below.x.value() * distance) + (right.x.value() * aside),
                            (below.y.value() * distance) + (right.y.value() * aside),
                            (below.z.value() * distance) + (right.z.value() * aside)};
    // Constants the factory accepts: a finite position, a rotation, the
    // probes' 45-degree field of view and 1 m near plane.
    const auto camera =
        view::Camera::from(position, quaternionFrom(rotation), Radians{kPi / 4.0}, Metres{1.0});
    ORBSIM_ENSURES(camera.has_value());
    return *camera;
}

// The grid probes' conditions: `clear`'s, at the grid epoch, with `camera`.
[[nodiscard]] ProbeConditions gridConditionsWith(const view::Camera& camera) {
    const auto tt = TtTime::fromJulianDate(kGridEpochTt);
    const auto ut1 = Ut1Time::fromJulianDate(kGridEpochUt1);
    // Two dates well inside the calendar: a refusal is a defect in them.
    ORBSIM_ENSURES(tt.has_value() && ut1.has_value());
    ProbeConditions conditions = clearConditions();
    conditions.epoch = *tt;
    conditions.camera = camera;
    conditions.picture = GridPicture{.ut1 = *ut1};
    return conditions;
}

// The lines of `batch` through the line renderer and ScenePipelines::lines(),
// uploaded into the first frame slot (register decision 282), as `lines` and
// the grid probes draw them.
[[nodiscard]] std::expected<LinesDraw, RenderError>
linesDrawOf(VulkanContext& context,
            const std::filesystem::path& shaderDirectory,
            const ProbeConditions& conditions,
            const view::LineBatch& batch) {
    auto pipelines = ScenePipelines::create(context, shaderDirectory);
    if (!pipelines) return std::unexpected(pipelines.error());
    auto renderer = LineRenderer::create(context, batch.capacity());
    if (!renderer) return std::unexpected(renderer.error());
    auto lines = renderer->upload(context, 0, batch);
    if (!lines) return std::unexpected(lines.error());
    return LinesDraw{
        .pipelines = *std::move(pipelines),
        .renderer = *std::move(renderer),
        .lines = *lines,
        // White: the tint multiplies every colour, and the probes draw them
        // as they are.
        .pushConstants =
            {
                .viewProjection = probeViewProjection(conditions.camera),
                .tint = {1.0F, 1.0F, 1.0F, 1.0F},
            },
        .quality = conditions.quality,
    };
}

// The draw of a probe with its own pipeline.
void recordDraw(VkCommandBuffer cmd, const ProbeDraw& draw) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.pipeline.pipeline());
    if (draw.vertices) {
        VkBuffer buffer = draw.vertices.get();
        constexpr VkDeviceSize kOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &kOffset);
    }
    if (!draw.pushConstants.empty()) {
        vkCmdPushConstants(cmd,
                           draw.pipeline.layout(),
                           draw.pushStages,
                           0,
                           static_cast<std::uint32_t>(draw.pushConstants.size()),
                           draw.pushConstants.data());
    }
    vkCmdDraw(cmd, draw.vertexCount, 1, 0, 0);
}

} // namespace

ProbeScene::ProbeScene(ProbeDraw draw) noexcept : draw_(std::move(draw)) {}
ProbeScene::ProbeScene(LinesDraw draw) noexcept : draw_(std::move(draw)) {}

void ProbeScene::record(VkCommandBuffer cmd) const {
    std::visit(
        [cmd](const auto& draw) {
            using Draw = std::decay_t<decltype(draw)>;
            if constexpr (std::is_same_v<Draw, LinesDraw>) {
                draw.renderer.draw(
                    cmd, draw.lines, draw.pipelines.lines(), draw.pushConstants, draw.quality);
            } else {
                static_assert(std::is_same_v<Draw, ProbeDraw>);
                recordDraw(cmd, draw);
            }
        },
        draw_);
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

    const view::LambertPushConstants block = view::toShaderLambert(
        {
            .albedo = scene.albedo,
            .irradiance = scene.irradiance,
            .surfaceNormal = patch.normal,
            .towardSun = scene.towardSun,
        },
        probeViewProjection(camera));

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

std::expected<ProbeScene, RenderError>
createLinesScene(VulkanContext& context,
                 const std::filesystem::path& shaderDirectory,
                 const ProbeConditions& conditions) {
    const view::Camera& camera = conditions.camera;

    view::LineBatch batch{kLinesProbeCapacity};
    const std::array square{
        Position{kSquareLowMetres, kSquareLowMetres, 0.0},
        Position{kSquareHighMetres, kSquareLowMetres, 0.0},
        Position{kSquareHighMetres, kSquareHighMetres, 0.0},
        Position{kSquareLowMetres, kSquareHighMetres, 0.0},
        Position{kSquareLowMetres, kSquareLowMetres, 0.0},
    };
    // The capacity is exactly what these two need, so a refusal is a defect
    // in the constants above.
    [[maybe_unused]] const auto axes =
        batch.addAxes(Position{0.0, 0.0, 0.0}, Metres{kAxisLengthMetres}, camera);
    [[maybe_unused]] const auto outline = batch.addPolyline(square, kSquareColour, camera);
    ORBSIM_ENSURES(axes.has_value() && outline.has_value());

    auto draw = linesDrawOf(context, shaderDirectory, conditions, batch);
    if (!draw) return std::unexpected(draw.error());
    return ProbeScene{*std::move(draw)};
}

std::expected<ProbeScene, RenderError> createGridScene(VulkanContext& context,
                                                       const std::filesystem::path& shaderDirectory,
                                                       const ProbeConditions& conditions,
                                                       const GridPicture& picture) {
    // The Earth turned to the epoch: ERFA's rotation takes the celestial
    // frame to the Earth-fixed one, and the grid wants the other way.
    const Quat worldFromEarthFixed =
        earthFixedFromInertial(conditions.epoch, picture.ut1).conjugate();
    const auto layout = view::GridLayout::from(view::kEarthGridCounts);
    // The task's counts, which view/PlanetaryGrid.hpp asserts can be made.
    ORBSIM_ENSURES(layout.has_value());
    const view::PlanetaryGrid grid =
        view::PlanetaryGrid::make(view::kWgs84SemiMajorAxis, *layout, worldFromEarthFixed);
    // Room for the whole grid, which is more than any camera sees of it, and
    // the horizon, drawn last so that nothing interrupts it (register
    // decision 338).
    view::LineBatch batch{layout->vertexCount() + view::VertexCount{2 * view::kHorizonSegments}};
    [[maybe_unused]] const auto added =
        view::addVisibleGrid(batch, grid, view::kGridColours, conditions.camera);
    [[maybe_unused]] const auto horizon = view::addHorizon(
        batch, view::kWgs84SemiMajorAxis, view::kGridColours.horizon, conditions.camera);
    ORBSIM_ENSURES(added.has_value() && horizon.has_value());
    auto draw = linesDrawOf(context, shaderDirectory, conditions, batch);
    if (!draw) return std::unexpected(draw.error());
    return ProbeScene{*std::move(draw)};
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
            } else if constexpr (std::is_same_v<Picture, LinesPicture>) {
                return createLinesScene(context, shaderDirectory, conditions);
            } else if constexpr (std::is_same_v<Picture, GridPicture>) {
                return createGridScene(context, shaderDirectory, conditions, picture);
            } else {
                // Exhaustive: a picture without a scene fails here.
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

ProbeConditions linesConditions() {
    // Decision 280's camera: 3 m from the point it looks at, on the azimuth
    // halfway between +X and +Y, 30 degrees above the XY plane. The
    // orientation runs camera-to-world (view/Camera.hpp), so the rotation's
    // columns are the camera's right, up and back in world coordinates; the
    // camera looks down its -z, along -back. Right is horizontal, so world Z
    // is up on screen, and up = back x right completes a right-handed frame.
    constexpr f64 kDistanceMetres = 3.0;
    const Position target{0.5, 0.5, 0.3};
    const f64 elevation = kPi / 6.0;
    const f64 inverseRootTwo = 1.0 / std::numbers::sqrt2;
    const Direction back{std::cos(elevation) * inverseRootTwo,
                         std::cos(elevation) * inverseRootTwo,
                         std::sin(elevation)};
    const Direction right{-inverseRootTwo, inverseRootTwo, 0.0};
    const Direction up = cross(back, right);
    const RotationMatrix rotation{
        // std::to_array, row by row: MSVC (C5246) and gcc want a std::array's
        // inner braces written out.
        .rows = std::to_array({
            std::to_array({right.x.value(), up.x.value(), back.x.value()}),
            std::to_array({right.y.value(), up.y.value(), back.y.value()}),
            std::to_array({right.z.value(), up.z.value(), back.z.value()}),
        }),
    };
    const Position position{target.x.value() + (kDistanceMetres * back.x.value()),
                            target.y.value() + (kDistanceMetres * back.y.value()),
                            target.z.value() + (kDistanceMetres * back.z.value())};
    // Constants the factory accepts: a finite position, a rotation, the
    // probes' 45-degree field of view and 1 m near plane.
    const auto camera =
        view::Camera::from(position, quaternionFrom(rotation), Radians{kPi / 4.0}, Metres{1.0});
    ORBSIM_ENSURES(camera.has_value());

    ProbeConditions conditions = clearConditions();
    conditions.camera = camera.value();
    conditions.picture = LinesPicture{};
    return conditions;
}

ProbeConditions lambertExposureConditions() {
    ProbeConditions conditions = lambertConditions();
    conditions.exposure = exposureAt(kTwoStopsBrighter);
    return conditions;
}

ProbeConditions grid400kmConditions() {
    return gridConditionsWith(gridCamera(k400kmView, Metres{0.0}));
}

ProbeConditions gridJitterConditions(JitterSequence sequence, std::uint32_t frame) {
    const GridView& view = sequence == JitterSequence::From400km ? k400kmView : k1kmView;
    return gridConditionsWith(
        gridCamera(view, Metres{static_cast<f64>(frame) * kJitterStepMetres}));
}

} // namespace orb::gfx
