#ifndef ORBSIM_VIEW_PROBEGRADIENT_HPP
#define ORBSIM_VIEW_PROBEGRADIENT_HPP
//
// What the `clear` probe draws (M1-16, register decision 189).
//
// **Four bands and an undrawn strip.** Rows 0-149 grey, 150-299 red, 300-449
// green, 450-599 blue; rows 600-719 are never drawn, so the scene's clear
// radiance -- zero, view/SceneClear.hpp -- shows there. In column x of a band,
//
//     L(x) = L_low * 2^(stops * (x + 0.5) / width)
//
// in W/(m^2 sr), in the band's one channel (all three for grey). The ramp is
// even in stops, so each band steps through the tonemap's curve at an even
// pace, and it spans **exactly the range AgX can display** at the probe's
// exposure: from the radiance whose exposed value is 2^kMinEv, AgX's black, to
// the one at 2^kMaxEv, its white -- 16.5 stops (view/Tonemap.hpp). Nothing
// here is chosen by eye: the two ends are AgX's constants divided by the
// camera's exposure factor.
//
// **Why this picture.** A frame that is not symmetric top to bottom is what
// lets a person see that it is the right way up, and coloured bands in a known
// order are what let them see that no two channels were swapped -- two of the
// three things M1-16 asks the owner to judge. The third, smoothness, is what
// an even ramp in stops shows.
//
// **Checked numerically** by tests/test_probe_clear.cpp, which computes L(x)
// itself from the camera settings and AgX's range and reads the dump back.
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Exposure.hpp"
#include "view/ProbeImage.hpp"
#include "view/PushConstants.hpp"
#include "view/Tonemap.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace orb::view {

// The band layout, in rows of the 1280x720 probe frame.
inline constexpr std::uint32_t kClearProbeBandRows = 150;
inline constexpr std::uint32_t kClearProbeBands = 4; // grey, red, green, blue
static_assert(kClearProbeBandRows * kClearProbeBands < kProbeImageSize.height,
              "the bands leave a strip undrawn, where the clear radiance shows");

// The ends of the ramp: the radiances AgX maps to black and to white.
struct ClearProbeRamp {
    Radiance low;
    Radiance high;
};

// AgX's black and white at `exposure`, as radiances. A radiance times the
// exposure factor is the exposed value AgX takes (view/Exposure.hpp), so each
// end is AgX's exposed limit divided by the factor.
[[nodiscard]] inline ClearProbeRamp clearProbeRamp(PerRadiance exposure) noexcept {
    const f64 perRadiance = exposure.value();
    ORBSIM_EXPECTS(isFinite(perRadiance) && perRadiance > 0.0);
    return {
        .low = Radiance{std::exp2(agx::kMinEv) / perRadiance},
        .high = Radiance{std::exp2(agx::kMaxEv) / perRadiance},
    };
}

// probe_gradient.frag's block, member for member.
struct ClearProbePushConstants {
    f32 log2Low{};  // log2 of the low end, in W/(m^2 sr)
    f32 stops{};    // log2(high / low): how many stops the ramp spans
    f32 width{};    // the frame's width in pixels
    f32 bandRows{}; // each band's height in pixels
};
static_assert(std::is_standard_layout_v<ClearProbePushConstants> &&
                  sizeof(ClearProbePushConstants) == 16U &&
                  offsetof(ClearProbePushConstants, stops) == 4U &&
                  offsetof(ClearProbePushConstants, width) == 8U &&
                  offsetof(ClearProbePushConstants, bandRows) == 12U,
              "probe_gradient.frag's block: four floats at 0, 4, 8 and 12");

inline constexpr PushConstantRange kClearProbePushConstantRange =
    PushConstantRange::wholeBlock<ClearProbePushConstants>(ShaderStages::Fragment).value();

// **The ramp narrowed for the shader: the third place in `src/` that narrows
// to 32 bits**, beside view/Camera.hpp's toRenderSpace and view/Exposure.hpp's
// toShaderExposure, and for the same reason as the second: these are a
// logarithm, a count of stops and two pixel counts, none of them a position,
// so there is nothing to subtract first. The logarithm is taken in f64 and
// only its result narrowed: log2 of the low end is about -3.9, where a float
// resolves 2.4e-7, which moves the radiance by 1.7e-7 of itself -- far inside
// the half binary16 step, 2.4e-4 of itself, that the HDR target rounds to.
[[nodiscard]] inline ClearProbePushConstants toShaderRamp(const ClearProbeRamp& ramp,
                                                          ImageSize size) noexcept {
    const f64 low = ramp.low.value();
    const f64 high = ramp.high.value();
    ORBSIM_EXPECTS(isFinite(low) && low > 0.0 && isFinite(high) && high > low);
    const f64 log2Low = std::log2(low);
    const f64 stops = std::log2(high / low);
    ORBSIM_EXPECTS(absOf(log2Low) <= static_cast<f64>(std::numeric_limits<f32>::max()));
    return {
        .log2Low = static_cast<f32>(log2Low),
        .stops = static_cast<f32>(stops),
        .width = static_cast<f32>(size.width),
        .bandRows = static_cast<f32>(kClearProbeBandRows),
    };
}

} // namespace orb::view

#endif // ORBSIM_VIEW_PROBEGRADIENT_HPP
