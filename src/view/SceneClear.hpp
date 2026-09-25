#ifndef ORBSIM_VIEW_SCENECLEAR_HPP
#define ORBSIM_VIEW_SCENECLEAR_HPP
//
// The radiance the HDR target is cleared to before the scene is drawn
// (M1-14; settled by M1-15, register decision 181).
//
// **Zero: where nothing is drawn, no light arrives.** Until M1-15 this held
// the old display colour decoded to linear light, so that M1-14 changed
// nothing visible; ADR 0014 allows no tuning constant in the chain, and once
// exposure multiplies the target, a number chosen by eye stops meaning
// anything. The alternative was a background radiance with a source -- the
// zodiacal light and the integrated starlight -- and it would render
// identically: at the application's exposure, anything dimmer than about
// 8 cd/m^2 lands on display code 0 (AgX is exactly black below an exposed
// 2.17e-4, view/Tonemap.hpp, times the 38 400 cd/m^2 that saturate the sensor
// at f/16, 1/125 s, ISO 100). A dark-sky background of 22 magnitudes per
// square arcsecond is 1.7e-4 cd/m^2 (10.8e4 * 10^(-0.4 * 22)), some forty
// thousand times dimmer. Stars, when a task draws them, are sources and not a
// clear colour.
//
// **Float literals, not a narrowing.** VkClearColorValue takes floats; a
// literal needs no cast, so `grep static_cast<f32> src/` still finds only the
// two narrowing functions.
//
#include "core/Scalar.hpp"

namespace orb::view {

// One radiance per channel, in W/(m^2 sr), with coverage, in the channel order
// Vulkan clears in.
struct LinearRgba {
    f32 red{};
    f32 green{};
    f32 blue{};
    f32 alpha{};
};

inline constexpr LinearRgba kSceneClear{
    .red = 0.0F,
    .green = 0.0F,
    .blue = 0.0F,
    .alpha = 1.0F,
};

// The ruling, held where the constant is: a clear colour that is not zero is a
// radiance somebody chose by eye, which ADR 0014 forbids. Changing it is a
// decision to revisit, not a number to tune, and this is where that shows.
static_assert(bitsOf(kSceneClear.red) == bitsOf(0.0F) &&
                  bitsOf(kSceneClear.green) == bitsOf(0.0F) &&
                  bitsOf(kSceneClear.blue) == bitsOf(0.0F),
              "where nothing is drawn, no light arrives (register decision 181)");
static_assert(bitsOf(kSceneClear.alpha) == bitsOf(1.0F), "and the frame is opaque");

} // namespace orb::view

#endif // ORBSIM_VIEW_SCENECLEAR_HPP
