#ifndef ORBSIM_VIEW_POSE_HPP
#define ORBSIM_VIEW_POSE_HPP
//
// Where a camera is and which way it faces: what view/CameraController.hpp and
// view/CameraPath.hpp both produce (M1-21, register decision 346).
//
// **A pose, not a camera.** A `Camera` also has a field of view and a near
// plane, which neither the controller nor a path changes, so the application
// builds the camera from a pose with those two beside it -- through
// `Camera::from`, which is where an orientation is checked for being a
// rotation.
//
// **A plain aggregate, not a validated class.** Its two producers make only
// finite positions and unit orientations, and the one consumer that needs
// them to be so, `Camera::from`, checks; a second check here would be the
// same predicate in two places (VERIFICATION.md rule 2's shape).
//
#include "core/Math.hpp"

#include <type_traits>

namespace orb::view {

// The orientation runs camera to world, as view/Camera.hpp's does: its
// columns are the camera's right, up and back in the world frame, and the
// camera looks down its own -z.
struct Pose {
    Position position;
    Quat orientation;
};

static_assert(std::is_trivially_copyable_v<Pose>, "a pose is a value");

} // namespace orb::view

#endif // ORBSIM_VIEW_POSE_HPP
