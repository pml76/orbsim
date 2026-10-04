#ifndef ORBSIM_VIEW_CAMERACONTROLLER_HPP
#define ORBSIM_VIEW_CAMERACONTROLLER_HPP
//
// The interactive camera: circle a point on a planet's surface, slide the
// point over the planet, and move toward or away from it (M1-21; register
// decisions 340-344 and 353-360; ADR 0026).
//
// **What it circles is a point on the surface, and "up" is that point's own
// vertical** (decision 340). Tilt shows the horizon, panning slides the point
// over the planet, and moving in descends toward it -- which is what lets the
// limb be seen from 400 km, 70 degrees off the straight-down axis.
//
// **It reads commands, not a device** (ADR 0026). Three of them -- turn by two
// angles, pan by a fraction of the screen, move in or out by a number of steps
// -- and a mapping per device turns its input into them: the mouse's is
// view/CameraInput.hpp. A keyboard or a gamepad is a new mapping, and nothing
// here changes.
//
// **A state machine with no clock.** A command changes the state, the state
// gives the pose, and nothing depends on time or frame rate, so replaying the
// same commands gives the same pose bit for bit (VERIFICATION.md rule 16).
//
// **The state**, and why it is these four:
//
//   * the focus frame, a quaternion taking the focus point's own east, north
//     and up to the world. **Carried by rotations, never rebuilt from a
//     latitude and longitude**, so the poles are not special: north is
//     undefined at a pole, and a frame built from angles would have to say
//     something there. The cost is that after a long pan round a loop the
//     frame's north may have turned (decision 359), which nothing on screen
//     shows -- screen-up is always forward at the focus;
//   * the heading, east of the frame's north, that the camera looks along;
//   * the tilt, how far above the focus' horizon the camera sits -- 90 degrees
//     is straight overhead, looking down -- held within +-89.9 degrees
//     (decision 342) so that the picture does not flip over the top. The
//     orientation is built from the angles directly, with no "look at" cross
//     product, so nothing here can make a NaN at any tilt;
//   * the distance from the focus, within 10 m and 1e9 m.
//
// **Speed scales with the distance to the focus** (decision 341): a pan moves
// the ground by a fraction of what the screen's height covers at that
// distance, so the ground follows the cursor from 400 km and from 10 m alike,
// and a wheel step moves a fixed share of the distance. Circling is a fixed
// angle, which needs no scaling.
//
// **The planet is a sphere centred on the world's origin**, of the radius it
// is given -- the Earth, and the grid's WGS-84 sphere, in milestone 1.
//
// **The world frame**: the focus' start is given as a right ascension and a
// declination, the celestial frame's longitude and latitude, as the grid
// probes' camera is (render/Probes.cpp).
//
#include "core/Math.hpp"
#include "core/Units.hpp"
#include "view/Mat4.hpp"
#include "view/Pose.hpp"

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <variant>

namespace orb::view {

// Turn the camera about the focus: `heading` adds to the bearing it looks
// along, east of north; `tilt` raises it toward overhead. The tilt stops at
// its limit rather than passing it.
struct OrbitCommand {
    Radians heading;
    Radians tilt;
};

// Slide the focus over the planet so that the ground moves `rightward` and
// `upward` on the screen, each in heights of the screen -- "grab the world"
// (decision 358): the ground follows the pan.
struct PanCommand {
    Dimensionless rightward;
    Dimensionless upward;
};

// Move toward the focus by `steps`, each multiplying the distance by
// kDollyFactorPerStep; a negative number of steps moves away. Not whole
// steps necessarily: a held input sampled at a fixed step may give part of
// one (ADR 0026).
struct DollyCommand {
    Dimensionless steps;
};

using CameraCommand = std::variant<OrbitCommand, PanCommand, DollyCommand>;

// The limits (decision 342). The near one is ten times the 1 m near plane the
// window draws with (decision 344), so the focus is never clipped; the far one
// is beyond the Moon, at 3.84e8 m.
inline constexpr Metres kMinimumFocusDistance{10.0};
inline constexpr Metres kMaximumFocusDistance{1e9};
inline constexpr Radians kTiltLimit = toRadians(Degrees{89.9});

// What one step of moving in multiplies the distance by: 0.9, a tenth of the
// way in, so 22 steps go a factor of ten (decision 360's starting value,
// tuned when the owner flies it).
inline constexpr f64 kDollyFactorPerStep = 0.9;

// Where the camera starts: the focus' place on the sphere in the world frame,
// and the camera's heading, tilt and distance from it.
struct ControllerStart {
    Radians focusRightAscension;
    Radians focusDeclination;
    Radians heading;
    Radians tilt;
    Metres distance;
};

// What the controller needs to know of the world and the view: the planet's
// radius, and the camera's vertical field of view, which sets how much ground
// the screen covers and so how far a pan goes.
struct ControllerSettings {
    Metres planetRadius;
    Radians verticalFov;
};

// What a controller refuses. Reported, not asserted: a start and a command can
// both come from outside -- a scenario, a replay file in milestone 2 -- and
// one name per thing wrong (ADR 0002).
enum class ControllerError : std::uint8_t {
    NotFiniteStart,        // an angle of the start is infinite or not a number
    InvalidPlanetRadius,   // not finite, or not greater than zero
    InvalidFieldOfView,    // not between zero and half a turn
    DistanceOutsideLimits, // the start's distance is not within the limits
    TiltOutsideLimits,     // the start's tilt is not within the limits
    NotFiniteCommand,      // a command holds an infinity or a NaN (decision 357)
};

[[nodiscard]] constexpr std::string_view describe(ControllerError error) noexcept {
    switch (error) {
    case ControllerError::NotFiniteStart:
        return "every angle of the camera's start must be finite";
    case ControllerError::InvalidPlanetRadius:
        return "the planet's radius must be finite and greater than zero";
    case ControllerError::InvalidFieldOfView:
        return "the vertical field of view must be between zero and pi";
    case ControllerError::DistanceOutsideLimits:
        return "the camera must start between 10 m and 1e9 m from its focus";
    case ControllerError::TiltOutsideLimits:
        return "the camera's tilt must start within 89.9 degrees of the horizon";
    case ControllerError::NotFiniteCommand:
        return "a camera command must be finite in every value";
    }
    return "unknown camera controller error";
}

class CameraController {
public:
    [[nodiscard]] static std::expected<CameraController, ControllerError>
    from(const ControllerStart& start, const ControllerSettings& settings) noexcept;

    // Applies one command. A command holding a NaN or an infinity is refused
    // and the camera stays exactly where it was (decision 357).
    [[nodiscard]] std::expected<void, ControllerError> apply(const CameraCommand& command) noexcept;

    // Where the camera is and which way it faces, from the state.
    [[nodiscard]] Pose pose() const noexcept;

    [[nodiscard]] Radians heading() const noexcept { return heading_; }
    [[nodiscard]] Radians tilt() const noexcept { return tilt_; }
    [[nodiscard]] Metres distance() const noexcept { return distance_; }

private:
    CameraController(const Quat& focusFrame,
                     const ControllerStart& start,
                     const ControllerSettings& settings) noexcept;

    void orbit(const OrbitCommand& command) noexcept;
    void pan(const PanCommand& command) noexcept;
    void dolly(const DollyCommand& command) noexcept;

    Quat focusFrame_;
    Radians heading_;
    Radians tilt_;
    Metres distance_;
    ControllerSettings settings_;
};

static_assert(std::is_trivially_copyable_v<CameraController>,
              "a controller is a value: replaying from a copy is the determinism test");
static_assert(!std::is_default_constructible_v<CameraController>,
              "and there is no default one: it comes from the factory");
static_assert(kMinimumFocusDistance < kMaximumFocusDistance);
static_assert(kTiltLimit < toRadians(Degrees{90.0}), "short of the pole, as the limit says");
static_assert(describe(ControllerError::NotFiniteStart) !=
                  describe(ControllerError::InvalidPlanetRadius),
              "each error says what is wrong");
static_assert(describe(ControllerError::InvalidPlanetRadius) !=
              describe(ControllerError::InvalidFieldOfView));
static_assert(describe(ControllerError::InvalidFieldOfView) !=
              describe(ControllerError::DistanceOutsideLimits));
static_assert(describe(ControllerError::DistanceOutsideLimits) !=
              describe(ControllerError::TiltOutsideLimits));
static_assert(describe(ControllerError::TiltOutsideLimits) !=
              describe(ControllerError::NotFiniteCommand));

} // namespace orb::view

#endif // ORBSIM_VIEW_CAMERACONTROLLER_HPP
