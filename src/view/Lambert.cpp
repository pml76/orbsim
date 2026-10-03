#include "view/Lambert.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/PushConstants.hpp"

#include <array>
namespace orb::view {
namespace {

// `d` scaled to a length, as a position offset.
[[nodiscard]] Position scaled(const Direction& d, f64 metres) noexcept {
    return Position{d.x.value() * metres, d.y.value() * metres, d.z.value() * metres};
}

[[nodiscard]] Vec4f narrowedDirection(const Direction& d) noexcept {
    // std::to_array, because gcc-14 wants a std::array's inner braces
    // (-Wmissing-braces) and clang-tidy's trailing-comma check then objects to
    // them -- the shape M1-16 settled on.
    return std::to_array({
        static_cast<f32>(d.x.value()),
        static_cast<f32>(d.y.value()),
        static_cast<f32>(d.z.value()),
        0.0F,
    });
}

} // namespace

SquarePatch squarePatch(const PatchPlacement& placement) noexcept {
    ORBSIM_EXPECTS(isFinitePosition(placement.centre));
    ORBSIM_EXPECTS(isFinite(placement.side.value()) && placement.side.value() > 0.0);
    ORBSIM_EXPECTS(isFinite(placement.tilt.value()));

    // Turning about -x by the tilt is turning about +x by minus it, which
    // takes the untilted normal +z to (0, sin, cos) and the untilted "up" +y
    // to (0, cos, -sin): the top of the square goes away from a camera
    // looking down -z (PatchPlacement says why that way round).
    const Direction axis{-1.0, 0.0, 0.0};
    const Direction across{1.0, 0.0, 0.0};
    const Direction up = rotateAxis(Direction{0.0, 1.0, 0.0}, axis, placement.tilt);
    const Direction normal = rotateAxis(Direction{0.0, 0.0, 1.0}, axis, placement.tilt);

    const f64 half = placement.side.value() / 2.0;
    const auto corner = [&](f64 acrossSign, f64 upSign) noexcept {
        return placement.centre + scaled(across, half * acrossSign) + scaled(up, half * upSign);
    };
    const Position lowerLeft = corner(-1.0, -1.0);
    const Position lowerRight = corner(1.0, -1.0);
    const Position upperRight = corner(1.0, 1.0);
    const Position upperLeft = corner(-1.0, 1.0);
    // across x up = normal, so each triangle below runs counter-clockwise
    // seen from the side the normal points to.
    return {
        .vertices =
            std::to_array({lowerLeft, lowerRight, upperRight, lowerLeft, upperRight, upperLeft}),
        .normal = normal,
    };
}

LambertPushConstants toShaderLambert(const LambertLighting& lighting,
                                     const Mat4f& viewProjection) noexcept {
    // An irradiance from solarIrradianceAt is finite and positive; one past
    // float range would be a defect upstream, and narrowing it undefined.
    ORBSIM_EXPECTS(isShaderIrradiance(lighting.irradiance));
    ORBSIM_EXPECTS(isUnitDirection(lighting.surfaceNormal));
    ORBSIM_EXPECTS(isUnitDirection(lighting.towardSun));
    return {
        .viewProjection = viewProjection,
        .surfaceNormal = narrowedDirection(lighting.surfaceNormal),
        .towardSun = narrowedDirection(lighting.towardSun),
        .albedo = static_cast<f32>(lighting.albedo.value()),
        .irradiance = static_cast<f32>(lighting.irradiance.value()),
    };
}

} // namespace orb::view
