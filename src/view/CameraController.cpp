#include "view/CameraController.hpp" // SF.5: own header, first
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Mat4.hpp"
#include "view/Pose.hpp"
#include "view/Projection.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <variant>

namespace orb::view {

namespace {

// A turn about one of the three axes, written out: the half-angle's cosine and
// sine in the two places they go. Exact to a rounding of each, which is what
// fromAxisAngle does as well, without the normalisation of an axis that is
// already a unit one.
[[nodiscard]] Quat aboutX(Radians angle) noexcept {
    const f64 half = angle.value() / 2.0;
    return Quat{std::cos(half), std::sin(half), 0.0, 0.0};
}

[[nodiscard]] Quat aboutZ(Radians angle) noexcept {
    const f64 half = angle.value() / 2.0;
    return Quat{std::cos(half), 0.0, 0.0, std::sin(half)};
}

// The focus' own east, north and up turned into the world, for the start's
// focus, at its right ascension and declination: a quarter turn plus the right
// ascension about the pole, after tipping up by the colatitude. At a pole
// itself this is still a rotation -- the north it gives there is the one the
// right ascension points along -- which is why the start may be at one.
[[nodiscard]] Quat focusFrameAt(const ControllerStart& start) noexcept {
    const Radians quarterTurn{kPi / 2.0};
    return normalize(aboutZ(start.focusRightAscension + quarterTurn) *
                     aboutX(quarterTurn - start.focusDeclination));
}

[[nodiscard]] bool isFiniteStart(const ControllerStart& start) noexcept {
    return isFinite(start.focusRightAscension.value()) &&
           isFinite(start.focusDeclination.value()) && isFinite(start.heading.value()) &&
           isFinite(start.tilt.value());
}

// Within the limits, written so that a NaN is outside them.
[[nodiscard]] bool isWithinDistanceLimits(Metres distance) noexcept {
    return distance >= kMinimumFocusDistance && distance <= kMaximumFocusDistance;
}

[[nodiscard]] bool isWithinTiltLimits(Radians tilt) noexcept {
    return tilt >= -kTiltLimit && tilt <= kTiltLimit;
}

// The commands are read with std::get_if rather than std::visit, which may
// throw for a variant left empty by an exception and so cannot sit in a
// noexcept function (bugprone-exception-escape). What visit's overload set
// would have checked -- that every alternative is handled -- is this count,
// to be updated with the two functions below when a command is added.
static_assert(std::variant_size_v<CameraCommand> == 3,
              "isFiniteCommand and CameraController::apply handle three commands");

// Every value a command carries, finite (decision 357).
[[nodiscard]] bool isFiniteCommand(const CameraCommand& command) noexcept {
    if (const auto* orbit = std::get_if<OrbitCommand>(&command)) {
        return isFinite(orbit->heading.value()) && isFinite(orbit->tilt.value());
    }
    if (const auto* pan = std::get_if<PanCommand>(&command)) {
        return isFinite(pan->rightward.value()) && isFinite(pan->upward.value());
    }
    const auto* dolly = std::get_if<DollyCommand>(&command);
    return dolly != nullptr && isFinite(dolly->steps.value());
}

} // namespace

CameraController::CameraController(const Quat& focusFrame,
                                   const ControllerStart& start,
                                   const ControllerSettings& settings) noexcept
    : focusFrame_(focusFrame),
      heading_(start.heading),
      tilt_(start.tilt),
      distance_(start.distance),
      settings_(settings) {}

std::expected<CameraController, ControllerError>
CameraController::from(const ControllerStart& start, const ControllerSettings& settings) noexcept {
    if (!isFiniteStart(start)) return std::unexpected(ControllerError::NotFiniteStart);
    if (!detail::isFinitePositive(settings.planetRadius.value())) {
        return std::unexpected(ControllerError::InvalidPlanetRadius);
    }
    if (!detail::isUsableFieldOfView(settings.verticalFov.value())) {
        return std::unexpected(ControllerError::InvalidFieldOfView);
    }
    if (!isWithinDistanceLimits(start.distance)) {
        return std::unexpected(ControllerError::DistanceOutsideLimits);
    }
    if (!isWithinTiltLimits(start.tilt)) return std::unexpected(ControllerError::TiltOutsideLimits);
    return CameraController{focusFrameAt(start), start, settings};
}

std::expected<void, ControllerError>
CameraController::apply(const CameraCommand& command) noexcept {
    if (!isFiniteCommand(command)) return std::unexpected(ControllerError::NotFiniteCommand);
    if (const auto* turn = std::get_if<OrbitCommand>(&command)) orbit(*turn);
    if (const auto* slide = std::get_if<PanCommand>(&command)) pan(*slide);
    if (const auto* move = std::get_if<DollyCommand>(&command)) dolly(*move);
    return {};
}

// The camera in the focus' frame is a turn about up by minus the heading --
// the screen's top swings from north toward east -- after a tip about east by
// a quarter turn less the tilt, which takes the straight-down camera, its top
// toward north, up to the horizon. In the world it is the focus frame times
// that; the camera sits back along its own +z, the distance from the focus.
Pose CameraController::pose() const noexcept {
    const Radians quarterTurn{kPi / 2.0};
    const Quat inFocusFrame = aboutZ(-heading_) * aboutX(quarterTurn - tilt_);
    const Quat orientation = normalize(focusFrame_ * inFocusFrame);
    const Position focus = focusFrame_.rotate(Position{0.0, 0.0, settings_.planetRadius.value()});
    const Position back = orientation.rotate(Position{0.0, 0.0, distance_.value()});
    return Pose{.position = focus + back, .orientation = orientation};
}

// Heading wrapped into (-pi, pi], so that a long session of turning does not
// grow it; the tilt held at its limit rather than passing it (decision 342).
void CameraController::orbit(const OrbitCommand& command) noexcept {
    heading_ = wrapPi(heading_ + command.heading);
    tilt_ = std::clamp(tilt_ + command.tilt, -kTiltLimit, kTiltLimit);
}

// The ground moves `rightward` and `upward` screen heights, so the focus moves
// the other way by that much ground -- what the screen's height covers at the
// focus distance, `d * 2 tan(fov / 2)` (decision 341) -- along the focus'
// horizon. On the sphere that is a turn of the focus frame by the ground over
// the radius, about the horizontal axis square to the move: up crossed with
// the move, written out in the frame's own east and north.
void CameraController::pan(const PanCommand& command) noexcept {
    const f64 screen = distance_.value() * 2.0 * std::tan(settings_.verticalFov.value() / 2.0);
    const f64 right = command.rightward.value() * screen;
    const f64 forward = command.upward.value() * screen;
    const f64 h = heading_.value();
    // The camera's right is (cos h, -sin h) in east and north, its forward
    // (sin h, cos h); the focus moves against both.
    const f64 east = -((right * std::cos(h)) + (forward * std::sin(h)));
    const f64 north = -((forward * std::cos(h)) - (right * std::sin(h)));
    const f64 metres = std::hypot(east, north);
    const Radians angle{metres / settings_.planetRadius.value()};
    // Up (0, 0, 1) crossed with the move (east, north, 0) is (-north, east, 0).
    // A move of zero turns about no axis by nothing, which fromAxisAngle gives
    // as the identity.
    const Direction axis{-north, east, 0.0};
    focusFrame_ = normalize(focusFrame_ * Quat::fromAxisAngle(axis, angle));
}

// Each step multiplies the distance by kDollyFactorPerStep; positive steps
// move in. Held within the limits.
void CameraController::dolly(const DollyCommand& command) noexcept {
    const Metres moved = distance_ * std::pow(kDollyFactorPerStep, command.steps.value());
    distance_ = std::clamp(moved, kMinimumFocusDistance, kMaximumFocusDistance);
}

} // namespace orb::view
