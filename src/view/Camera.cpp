#include "view/Camera.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Mat4.hpp"
#include "view/Projection.hpp"

#include <expected>

namespace orb::view {

std::expected<Camera, CameraError> Camera::from(const Position& position,
                                                const Quat& orientation,
                                                Radians verticalFov,
                                                Metres nearPlane) noexcept {
    // Checked in the order the members are declared, so that a caller fixing
    // one error and re-calling makes progress rather than ping-ponging.
    if (!isFinite(position.x.value()) || !isFinite(position.y.value()) ||
        !isFinite(position.z.value())) {
        return std::unexpected(CameraError::NotFinitePosition);
    }
    if (!isUnitQuaternion(orientation, kUnitQuaternionTolerance)) {
        return std::unexpected(CameraError::NotUnitOrientation);
    }
    // **These two predicates are `view/Projection.hpp`'s own, deliberately.**
    // Writing a second pair here would be two implementations of one rule that
    // agree only while somebody keeps them in step -- VERIFICATION.md rule 2.
    // Sharing them is what makes "a validated camera's projection can only
    // fail on the aspect ratio" true by construction rather than by
    // coincidence, which is the claim `projectionOf` below makes.
    if (!detail::isUsableFieldOfView(verticalFov.value())) {
        return std::unexpected(CameraError::InvalidFieldOfView);
    }
    if (!detail::isFinitePositive(nearPlane.value())) {
        return std::unexpected(CameraError::InvalidNearPlane);
    }
    return Camera{position, orientation, verticalFov, nearPlane};
}

WorldToView viewMatrix(const Camera& camera) noexcept {
    // The orientation runs camera-to-world, so the world-to-view rotation is
    // its conjugate -- which for a unit quaternion is its inverse, and the
    // factory has already refused anything that is not one.
    //
    // `rotationOf` produces a within-frame transform, because a rotation does
    // not change which space you are in (ADR 0021). `retargetFrame` is where
    // world-to-view is *declared*, and it is unchecked by design: nothing can
    // verify that this camera's view space is what the renderer means by view
    // space, so it is an assertion the camera makes and the one place in
    // `src/` that makes it.
    return retargetFrame<kView>(rotationOf<kWorld>(camera.orientation().conjugate()));
}

Vec3f toRenderSpace(const Position& worldMetres, const Camera& camera) noexcept {
    // **Subtract in f64 FIRST, then narrow.** At Earth radius the gap between
    // neighbouring 32-bit floats is 0.5 m, so narrowing either operand before
    // this line would quantise the position to a half-metre lattice and throw
    // away the difference this function exists to compute.
    const Position relative = worldMetres - camera.position();

    ORBSIM_EXPECTS(detail::isNarrowable(relative.x.value()) &&
                   detail::isNarrowable(relative.y.value()) &&
                   detail::isNarrowable(relative.z.value()));

    // The unit comes off here, in the same three expressions that narrow to
    // 32 bits -- one place, saying both things at once.
    return Vec3f{
        .x = static_cast<f32>(relative.x.value()),
        .y = static_cast<f32>(relative.y.value()),
        .z = static_cast<f32>(relative.z.value()),
    };
}

std::expected<Projection, ProjectionError> projectionOf(const Camera& camera,
                                                        Aspect aspect) noexcept {
    // Temporary; M1-13 owns the projection from then on and deletes this.
    return infiniteReverseZPerspective(camera.verticalFov(), aspect, camera.nearPlane());
}

} // namespace orb::view
