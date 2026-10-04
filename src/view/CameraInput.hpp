#ifndef ORBSIM_VIEW_CAMERAINPUT_HPP
#define ORBSIM_VIEW_CAMERAINPUT_HPP
//
// The mouse's mapping: a drag or a wheel turned into the commands
// view/CameraController.hpp reads (M1-21; ADR 0026, register decisions 343,
// 358 and 360).
//
// **Here rather than in src/app/, so it can be tested without a window.** What
// is left in the application is what only SDL can do: reading the event,
// saying which button is down, and handing these functions numbers. A
// keyboard or a gamepad gets a mapping of its own beside this one (ADR 0026).
//
// **"Grab the world"** (decision 358): drag right and the ground in front of
// the camera moves right, drag down and it moves down; pan, and the ground
// follows the pointer; turn the wheel away and the camera moves in.
//
#include "core/Units.hpp"
#include "view/CameraController.hpp"
#include "view/Mat4.hpp"

#include <cstdint>

namespace orb::view {

// How far the pointer moved, in the window's pixels -- physical pixels, which
// on a high-density display are more than the window's own units -- to the
// right and downward, as a screen counts them.
struct DragDelta {
    Pixels right;
    Pixels down;
};

// Whole notches of the wheel, positive turned away from the user (decision
// 343: SDL's `integer_y`, which counts the same way).
struct WheelTicks {
    std::int32_t away{};
};

// How far a pixel of drag turns the camera: a quarter of a degree, so a drag
// across a 1,080-pixel screen turns 270 degrees (decision 360's starting
// value, tuned when the owner flies it).
inline constexpr auto kOrbitPerPixel = toRadians(Degrees{0.25}) / Pixels{1.0};

// A drag with the left button: right turns the heading east, so the ground
// in front of the camera moves right; down lowers the camera toward the
// horizon, so the scene moves down.
[[nodiscard]] constexpr OrbitCommand orbitFrom(DragDelta drag) noexcept {
    return {
        .heading = Radians{drag.right * kOrbitPerPixel},
        .tilt = Radians{-(drag.down * kOrbitPerPixel)},
    };
}

// A drag with the right button, over the screen's height in the same pixels:
// the ground moves with the pointer, and a screen counts down where the
// command counts up. A height of zero gives a fraction that is infinite or
// not a number, which the controller refuses (decision 357) -- nothing here
// invents a height.
[[nodiscard]] constexpr PanCommand panFrom(DragDelta drag, Pixels screenHeight) noexcept {
    return {
        .rightward = Dimensionless{(drag.right / screenHeight).value()},
        .upward = Dimensionless{-(drag.down / screenHeight).value()},
    };
}

// A notch of the wheel is a step: away from the user moves in.
[[nodiscard]] constexpr DollyCommand dollyFrom(WheelTicks wheel) noexcept {
    return {.steps = Dimensionless{static_cast<f64>(wheel.away)}};
}

// The signs, at compile time: a quarter of a degree a pixel, and the ground
// following the pointer.
static_assert(orbitFrom({.right = Pixels{4.0}, .down = Pixels{0.0}}).heading > Radians{0.0},
              "dragging right turns the heading east");
static_assert(orbitFrom({.right = Pixels{0.0}, .down = Pixels{4.0}}).tilt < Radians{0.0},
              "dragging down lowers the camera");
static_assert(panFrom({.right = Pixels{0.0}, .down = Pixels{10.0}}, Pixels{100.0}).upward <
                  Dimensionless{0.0},
              "dragging down moves the ground down");
static_assert(dollyFrom({.away = 1}).steps > Dimensionless{0.0}, "away moves in");

} // namespace orb::view

#endif // ORBSIM_VIEW_CAMERAINPUT_HPP
