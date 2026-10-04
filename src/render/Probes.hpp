#ifndef ORBSIM_RENDER_PROBES_HPP
#define ORBSIM_RENDER_PROBES_HPP
//
// The probes: frames rendered one at a time, under conditions pinned so that
// the same probe draws the same frame on every run (M1-16; ADR 0008; register
// decisions 187-194).
//
// **A probe is a name, a description, the state it pins, and a scene.** The
// name is a stable identifier -- M1-17's golden images are keyed by it -- and
// is checked while the compiler runs: lowercase letters, digits and hyphens,
// unique. The pinned state is everything that could otherwise vary: the epoch,
// the camera, the quality preset, the photographic exposure and, for the
// lambert probes, the light and the surface. The scene is the pipeline and the
// draws that put the picture into the HDR target.
//
// **Twenty probes** (M1-16, M1-18, M1-19 and M1-20; register decisions 189,
// 256, 261, 280 and 319-336):
//
//   * `clear` (view/ProbeGradient.hpp): the scene's clear radiance and a
//     gradient, through the real pipeline, the real HDR target and the real
//     tonemap -- the probe that fails when the machinery is broken rather than
//     the scene;
//   * `lambert` and its four variants (view/Lambert.hpp): a flat patch of
//     albedo 0.3 drawn through the camera and lit by the Sun -- at 1 AU, at
//     half and at twice that, tilted 60 degrees, and at another exposure --
//     whose read-back radiance tests/test_radiometry.cpp holds to the analytic
//     value;
//   * `tonemap-port` (shaders/probe_port.frag): light the tonemap's clamps act
//     on, negative channels included, for the port check;
//   * `lines` (view/LineBatch.hpp, render/LineRenderer.hpp): three axes and a
//     unit square through the camera, the line renderer's first frame, with a
//     golden image and a number check (tests/test_probe_lines.cpp);
//   * `grid-400km` (view/PlanetaryGrid.hpp): the Earth's latitude and
//     longitude grid, turned to the epoch, from 400 km looking at the limb,
//     with a golden image and a number check (tests/test_probe_grid.cpp);
//   * `grid-jitter-0` to `-4` and `grid-jitter-1km-0` to `-4`: five frames
//     each, the camera stepping 0.25 m to its right between them, at 400 km
//     and at 1 km from latitude 0, longitude 0 -- the frames
//     tests/test_probe_grid.cpp measures the movement in.
//
// **No wall clock anywhere in a probe's path.** Nothing here or in
// VulkanContext::renderOffscreen reads the time: the one clock a probe run
// touches is the sidecar's date, which the application writes and which no
// pixel depends on.
//
#include "core/Time.hpp"
#include "render/LineRenderer.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/Lambert.hpp"
#include "view/PushConstants.hpp"
#include "view/RenderQuality.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace orb::gfx {

// What a probe draws, and the state that only that picture has. One of four,
// as a variant (M1-18): a lambert probe without its light cannot be written,
// which is the first of non-negotiable 3's answers to a bad value.
struct GradientPicture {}; // clear's: view/ProbeGradient.hpp, from the exposure alone
struct PortPicture {};     // tonemap-port's: shaders/probe_port.frag, from nothing at all
struct LinesPicture {};    // lines': the axes and the square of register decision 280
// The grid probes' (M1-20): the UT1 the Earth is turned to, beside the epoch's
// TT, since the rotation is a function of both (register decision 323).
struct GridPicture {
    Ut1Time ut1;
};
using ProbePicture =
    std::variant<GradientPicture, view::LambertScene, PortPicture, LinesPicture, GridPicture>;

// What a probe holds fixed. Every field has a physical or named meaning, and
// each is written into the probe's sidecar.
struct ProbeConditions {
    TtTime epoch;
    view::Camera camera;
    view::RenderQuality quality;
    std::string_view qualityName; // the preset `quality` was built from
    view::CameraSettings exposure;
    // What is drawn: for the lambert probes, the light and the surface.
    ProbePicture picture;
};

// What a probe draws: a pipeline, the vertex buffer it reads if it reads one,
// how many vertices, and the push constants as bytes with the stages that
// read them. Owned together, and built before the frame (render/Pipeline.hpp
// says why pipelines are never made during one).
struct ProbeDraw {
    GraphicsPipeline pipeline;
    UniqueBuffer vertices; // empty when the vertex shader makes its own
    std::uint32_t vertexCount{};
    std::vector<std::byte> pushConstants; // empty when the pipeline reads none
    VkShaderStageFlags pushStages{};
};

// What the `lines` probe draws (M1-19, register decision 283): through the
// line renderer and ScenePipelines::lines(), as the application will, rather
// than with a pipeline of its own -- so the probe is the first draw through
// ScenePipelines. The lines are uploaded when the scene is built.
struct LinesDraw {
    ScenePipelines pipelines;
    LineRenderer renderer;
    LinesToDraw lines;
    view::LinePushConstants pushConstants;
    view::RenderQuality quality;
};

class ProbeScene {
public:
    // Records the probe's draws into the HDR target's rendering, which the
    // caller has begun.
    void record(VkCommandBuffer cmd) const;

    friend std::expected<ProbeScene, RenderError>
    createClearScene(const VulkanContext& context,
                     const std::filesystem::path& shaderDirectory,
                     const ProbeConditions& conditions);
    friend std::expected<ProbeScene, RenderError>
    createLambertScene(VulkanContext& context,
                       const std::filesystem::path& shaderDirectory,
                       const ProbeConditions& conditions,
                       const view::LambertScene& scene);
    friend std::expected<ProbeScene, RenderError>
    createPortScene(const VulkanContext& context,
                    const std::filesystem::path& shaderDirectory,
                    const ProbeConditions& conditions);
    friend std::expected<ProbeScene, RenderError>
    createLinesScene(VulkanContext& context,
                     const std::filesystem::path& shaderDirectory,
                     const ProbeConditions& conditions);
    friend std::expected<ProbeScene, RenderError>
    createGridScene(VulkanContext& context,
                    const std::filesystem::path& shaderDirectory,
                    const ProbeConditions& conditions,
                    const GridPicture& picture);

private:
    explicit ProbeScene(ProbeDraw draw) noexcept;
    explicit ProbeScene(LinesDraw draw) noexcept;

    std::variant<ProbeDraw, LinesDraw> draw_;
};

// `clear`'s scene: probe_gradient.frag over fullscreen.vert, into the HDR
// target, depth-tested with the project's comparison.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createClearScene(const VulkanContext& context,
                 const std::filesystem::path& shaderDirectory,
                 const ProbeConditions& conditions);

// The lambert probes' scene (register decisions 267-271): the patch of
// `scene` as two triangles in a vertex buffer, positions through
// toRenderSpace, drawn by lambert.vert and lambert.frag through the camera's
// view-projection, depth tested and written, back faces culled.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createLambertScene(VulkanContext& context,
                   const std::filesystem::path& shaderDirectory,
                   const ProbeConditions& conditions,
                   const view::LambertScene& scene);

// `tonemap-port`'s scene: probe_port.frag over fullscreen.vert, as `clear`'s
// is drawn, with no push constants -- its picture is its shader's alone.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createPortScene(const VulkanContext& context,
                const std::filesystem::path& shaderDirectory,
                const ProbeConditions& conditions);

// The `lines` probe's scene (register decisions 280 and 283): three axes 1 m
// long from the origin and a unit square in the XY plane, gathered in a
// LineBatch, uploaded into the line renderer's first frame slot and drawn
// with ScenePipelines::lines() through the camera's view-projection.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createLinesScene(VulkanContext& context,
                 const std::filesystem::path& shaderDirectory,
                 const ProbeConditions& conditions);

// The grid probes' scene (M1-20; register decisions 323-327): the Earth's
// grid on the WGS-84 sphere, turned by astro/EarthOrientation.hpp's rotation
// at the epoch's TT and the picture's UT1, cut to what the camera can see,
// with the horizon drawn over it in blue (register decision 338), gathered in
// a LineBatch and drawn as the `lines` probe is.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createGridScene(VulkanContext& context,
                const std::filesystem::path& shaderDirectory,
                const ProbeConditions& conditions,
                const GridPicture& picture);

// The scene `conditions.picture` names (M1-18). One dispatch, so that the
// lambert scene, which makes a buffer, is the one handed a context it may
// change, and the two that only read it are handed it read-only; a picture
// with no scene fails the build.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createScene(VulkanContext& context,
            const std::filesystem::path& shaderDirectory,
            const ProbeConditions& conditions);

// `clear`'s pinned state (register decision 190): J2000.0 TT; the camera at
// the origin, unrotated -- looking down -z -- with a 45-degree field of view
// and a 1 m near plane; the `high` preset; and "sunny 16", the application's
// own exposure.
[[nodiscard]] ProbeConditions clearConditions();

// `tonemap-port`'s: `clear`'s, with its own picture.
[[nodiscard]] ProbeConditions tonemapPortConditions();

// The lambert probes' pinned state (register decisions 256, 269 and 270):
// `clear`'s, plus a patch of albedo 0.3, 100 m square, centred 10 m in front
// of the camera, lit by a Sun straight behind the camera -- at 1 AU, facing
// it, unless the name says otherwise.
[[nodiscard]] ProbeConditions lambertConditions();
[[nodiscard]] ProbeConditions lambertHalfAuConditions();   // the Sun at 0.5 AU
[[nodiscard]] ProbeConditions lambertTwoAuConditions();    // the Sun at 2 AU
[[nodiscard]] ProbeConditions lambertTilted60Conditions(); // the patch tilted 60 degrees
[[nodiscard]] ProbeConditions lambertExposureConditions(); // f/8 instead of f/16
[[nodiscard]] ProbeConditions lambertBacklitConditions();  // the Sun beyond the patch

// The `lines` probe's pinned state (register decision 280): `clear`'s, with
// the camera 3 m from (0.5, 0.5, 0.3) on the azimuth between +X and +Y, 30
// degrees up, world Z up on screen.
[[nodiscard]] ProbeConditions linesConditions();

// The grid probes' pinned state (register decisions 323, 324 and 332): the
// Skyfield fixture's row at 2025-07-30 near 06:29 TT, with its own UT1; the
// camera fixed by numbers in the celestial frame, so that a wrong Earth
// rotation moves the grid and not the camera. `grid-400km`: 400 km above
// where that row puts 6 degrees south on the prime meridian, heading 35
// degrees east of north and looking 27.5 degrees down, so the limb crosses the
// upper third. The jitter frames: that camera, and one 1 km above latitude 0,
// longitude 0 looking straight down, each moved `frame` x 0.25 m to its own
// right.
[[nodiscard]] ProbeConditions grid400kmConditions();

// Which of the two jitter sequences (register decisions 321 and 332).
enum class JitterSequence : std::uint8_t {
    From400km, // grid-400km's camera
    From1km,   // 1 km above latitude 0, longitude 0, looking straight down
};

// Frame `frame` of `sequence`: its camera moved frame x 0.25 m to its right.
[[nodiscard]] ProbeConditions gridJitterConditions(JitterSequence sequence, std::uint32_t frame);

// The same, as a function of no arguments, which is what a Probe holds.
template <JitterSequence Sequence, std::uint32_t Frame>
[[nodiscard]] ProbeConditions gridJitterFrame() {
    return gridJitterConditions(Sequence, Frame);
}

struct Probe {
    std::string_view name;
    std::string_view description;
    ProbeConditions (*conditions)();
};

inline constexpr std::array kProbes{
    Probe{
        .name = "clear",
        .description = "the clear radiance and a gradient through the whole chain: "
                       "four bands (grey, red, green, blue) ramping evenly in stops "
                       "across AgX's range, over an undrawn strip",
        .conditions = clearConditions,
    },
    Probe{
        .name = "lambert",
        .description = "a patch of albedo 0.3 facing the Sun at 1 AU, drawn through the "
                       "camera and filling the frame: 0.3 x 1361 / pi W/(m^2 sr)",
        .conditions = lambertConditions,
    },
    Probe{
        .name = "lambert-half-au",
        .description = "the lambert patch with the Sun at 0.5 AU: four times the radiance",
        .conditions = lambertHalfAuConditions,
    },
    Probe{
        .name = "lambert-two-au",
        .description = "the lambert patch with the Sun at 2 AU: a quarter of the radiance",
        .conditions = lambertTwoAuConditions,
    },
    Probe{
        .name = "lambert-tilted-60",
        .description = "the lambert patch tilted 60 degrees from the Sun about the "
                       "horizontal axis: half the radiance",
        .conditions = lambertTilted60Conditions,
    },
    Probe{
        .name = "lambert-exposure",
        .description = "the lambert scene at f/8 instead of f/16: the same HDR frame, "
                       "and a brighter picture",
        .conditions = lambertExposureConditions,
    },
    Probe{
        .name = "lambert-backlit",
        .description = "the lambert patch with the Sun straight beyond it, lighting only its "
                       "back: no radiance at all, the patch drawn black",
        .conditions = lambertBacklitConditions,
    },
    Probe{
        .name = "lines",
        .description = "three axes 1 m long from the origin, X red, Y green and Z blue, "
                       "equally bright, and a white unit square in the XY plane, from 3 m "
                       "and 30 degrees up: X points down-left, Y down-right, the square a "
                       "diamond symmetric left to right",
        .conditions = linesConditions,
    },
    Probe{
        .name = "grid-400km",
        .description = "the Earth's grid, parallels every 10 degrees and meridians every 15, the "
                       "equator red, the prime meridian green and the horizon blue, turned to "
                       "2025-07-30 TT, from 400 km above 6 degrees south looking at the limb",
        .conditions = grid400kmConditions,
    },
    Probe{
        .name = "grid-jitter-0",
        .description = "grid-400km's frame, the first of five with the camera 0.25 m further "
                       "to its right each time",
        .conditions = gridJitterFrame<JitterSequence::From400km, 0>,
    },
    Probe{
        .name = "grid-jitter-1",
        .description = "grid-400km's frame with the camera 0.25 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From400km, 1>,
    },
    Probe{
        .name = "grid-jitter-2",
        .description = "grid-400km's frame with the camera 0.5 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From400km, 2>,
    },
    Probe{
        .name = "grid-jitter-3",
        .description = "grid-400km's frame with the camera 0.75 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From400km, 3>,
    },
    Probe{
        .name = "grid-jitter-4",
        .description = "grid-400km's frame with the camera 1 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From400km, 4>,
    },
    Probe{
        .name = "grid-jitter-1km-0",
        .description = "the equator and the prime meridian crossing, from 1 km straight above "
                       "latitude 0, longitude 0; the first of five with the camera 0.25 m "
                       "further to its right each time",
        .conditions = gridJitterFrame<JitterSequence::From1km, 0>,
    },
    Probe{
        .name = "grid-jitter-1km-1",
        .description = "grid-jitter-1km-0's frame with the camera 0.25 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From1km, 1>,
    },
    Probe{
        .name = "grid-jitter-1km-2",
        .description = "grid-jitter-1km-0's frame with the camera 0.5 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From1km, 2>,
    },
    Probe{
        .name = "grid-jitter-1km-3",
        .description = "grid-jitter-1km-0's frame with the camera 0.75 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From1km, 3>,
    },
    Probe{
        .name = "grid-jitter-1km-4",
        .description = "grid-jitter-1km-0's frame with the camera 1 m to its right",
        .conditions = gridJitterFrame<JitterSequence::From1km, 4>,
    },
    Probe{
        .name = "tonemap-port",
        .description = "six bands of light the tonemap's clamps act on, negative channels "
                       "and radiance above AgX's white included, for the port check",
        .conditions = tonemapPortConditions,
    },
};

// A probe name is a stable identifier: lowercase letters, digits and hyphens,
// not empty, and not starting or ending with a hyphen.
[[nodiscard]] constexpr bool isProbeName(std::string_view name) noexcept {
    if (name.empty() || name.front() == '-' || name.back() == '-') return false;
    return std::ranges::all_of(
        name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

[[nodiscard]] constexpr bool probeNamesAreUnique() noexcept {
    for (std::size_t i = 0; i < kProbes.size(); ++i) {
        for (std::size_t j = i + 1; j < kProbes.size(); ++j) {
            if (kProbes.at(i).name == kProbes.at(j).name) return false;
        }
    }
    return true;
}

static_assert(std::ranges::all_of(kProbes, [](const Probe& p) { return isProbeName(p.name); }),
              "every probe name is a stable identifier: lowercase, digits and hyphens");
static_assert(probeNamesAreUnique(), "and no two probes share one");
static_assert(isProbeName("clear") && isProbeName("grid-jitter") && !isProbeName("Clear") &&
                  !isProbeName("-clear") && !isProbeName("clear-") && !isProbeName("") &&
                  !isProbeName("clear probe"),
              "the rule, on names it must accept and refuse");

// The probe with this name, if there is one.
[[nodiscard]] constexpr std::optional<Probe> findProbe(std::string_view name) noexcept {
    const auto found =
        std::ranges::find_if(kProbes, [name](const Probe& p) { return p.name == name; });
    if (found == kProbes.end()) return std::nullopt;
    return *found;
}

} // namespace orb::gfx

#endif // ORBSIM_RENDER_PROBES_HPP
