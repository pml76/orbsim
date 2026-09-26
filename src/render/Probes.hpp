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
// the camera, the quality preset and the photographic exposure. The scene is
// the pipeline and the draws that put the picture into the HDR target.
//
// **One probe so far, `clear`** (view/ProbeGradient.hpp): the scene's clear
// radiance and a gradient, through the real pipeline, the real HDR target and
// the real tonemap -- the probe that fails when the machinery is broken rather
// than the scene. The registry's shape is what `clear` needs and no more; the
// probes of later tasks widen it when they arrive (CODING_GUIDELINES section
// 17 -- write the specific thing).
//
// **No wall clock anywhere in a probe's path.** Nothing here or in
// VulkanContext::renderOffscreen reads the time: the one clock a probe run
// touches is the sidecar's date, which the application writes and which no
// pixel depends on.
//
#include "core/Time.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/ProbeGradient.hpp"
#include "view/RenderQuality.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>

namespace orb::gfx {

// What a probe holds fixed. Every field has a physical or named meaning, and
// each is written into the probe's sidecar.
struct ProbeConditions {
    TtTime epoch;
    view::Camera camera;
    view::RenderQuality quality;
    std::string_view qualityName; // the preset `quality` was built from
    view::CameraSettings exposure;
};

// The pipeline and push constants a probe's picture is drawn with, created
// before the frame (render/Pipeline.hpp says why pipelines are never made
// during one).
class ProbeScene {
public:
    // Records the probe's draws into the HDR target's rendering, which the
    // caller has begun.
    void record(VkCommandBuffer cmd) const;

    friend std::expected<ProbeScene, RenderError>
    createClearScene(const VulkanContext& context,
                     const std::filesystem::path& shaderDirectory,
                     const ProbeConditions& conditions);

private:
    ProbeScene(GraphicsPipeline pipeline, view::ClearProbePushConstants pushConstants) noexcept;

    GraphicsPipeline pipeline_;
    view::ClearProbePushConstants pushConstants_;
};

// `clear`'s scene: probe_gradient.frag over fullscreen.vert, into the HDR
// target, depth-tested with the project's comparison.
[[nodiscard]] std::expected<ProbeScene, RenderError>
createClearScene(const VulkanContext& context,
                 const std::filesystem::path& shaderDirectory,
                 const ProbeConditions& conditions);

// `clear`'s pinned state (register decision 190): J2000.0 TT; the camera at
// the origin, unrotated -- looking down -z -- with a 45-degree field of view
// and a 1 m near plane; the `high` preset; and "sunny 16", the application's
// own exposure.
[[nodiscard]] ProbeConditions clearConditions();

struct Probe {
    std::string_view name;
    std::string_view description;
    ProbeConditions (*conditions)();
    std::expected<ProbeScene, RenderError> (*createScene)(const VulkanContext&,
                                                          const std::filesystem::path&,
                                                          const ProbeConditions&);
};

inline constexpr std::array kProbes{
    Probe{
        .name = "clear",
        .description = "the clear radiance and a gradient through the whole chain: "
                       "four bands (grey, red, green, blue) ramping evenly in stops "
                       "across AgX's range, over an undrawn strip",
        .conditions = clearConditions,
        .createScene = createClearScene,
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
