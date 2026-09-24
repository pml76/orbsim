#ifndef ORBSIM_VIEW_SCENECLEAR_HPP
#define ORBSIM_VIEW_SCENECLEAR_HPP
//
// The colour the HDR target is cleared to before the scene is drawn (M1-14).
//
// **A linear value that shows exactly what the display showed before.** Until
// M1-14 the scene was cleared straight into the 8-bit display image to
// (0.004, 0.006, 0.012) -- "deep space is not pure black" -- which a display
// shows as the 8-bit codes (1, 2, 3). The HDR target holds *linear* light and
// is encoded once at the end, so the same three numbers stored there would
// come out as (13, 18, 29), a visibly lighter blue-grey. Stored instead are
// those display values decoded through sRGB (view/Srgb.hpp), the nearest
// floats to 0.004/12.92, 0.006/12.92 and 0.012/12.92, which survive the 16-bit
// target and the encode and land back on (1, 2, 3). tests/test_srgb.cpp
// asserts both: that these are the decoded values, and that the codes do not
// move. M1-14 is meant to change nothing visible, so that if the image changes
// there is exactly one candidate for why.
//
// **This is a tuning constant, and M1-15 has to settle it.** ADR 0014 allows
// none anywhere in the chain, and once exposure multiplies the HDR target a
// number chosen by eye stops meaning anything. It stays here, stated, rather
// than being quietly carried into the exposed chain.
//
// **f32 literals, not a narrowing of the decoded doubles.** VkClearColorValue
// takes floats, and view/Camera.cpp's toRenderSpace is the one place src/
// narrows a double (`grep static_cast<f32> src/` is the audit); a literal
// needs no cast, and the test holds it to the decode.
//
#include "core/Scalar.hpp"

namespace orb::view {

// One linear colour with coverage, in the channel order Vulkan clears in.
struct LinearRgba {
    f32 red{};
    f32 green{};
    f32 blue{};
    f32 alpha{};
};

inline constexpr LinearRgba kSceneClear{
    .red = 3.0959752e-4F,   // decode(0.004)
    .green = 4.6439628e-4F, // decode(0.006)
    .blue = 9.2879257e-4F,  // decode(0.012)
    .alpha = 1.0F,
};

} // namespace orb::view

#endif // ORBSIM_VIEW_SCENECLEAR_HPP
