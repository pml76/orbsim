#include "view/BenchmarkPath.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/CameraPath.hpp"
#include "view/PlanetaryGrid.hpp"
#include "view/Pose.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace orb::view {

namespace {

// The circle's radius, from the Earth's centre.
[[nodiscard]] f64 orbitRadius() noexcept {
    return (kWgs84SemiMajorAxis + kGridOrbitAltitude).value();
}

// The period of a circular orbit of that radius: 2 pi sqrt(r^3 / mu).
[[nodiscard]] f64 orbitPeriod() noexcept {
    const f64 r = orbitRadius();
    return kTau * std::sqrt((r * r * r) / kEarthGravParam.value());
}

// The plane of the circle in the Earth-fixed frame: the ascending node on the
// prime meridian, and the direction a quarter turn on, toward the north. The
// orbit's normal is the pole turned by the inclination about the node, so a
// quarter turn from the node is (0, cos i, sin i).
struct OrbitPlane {
    Direction node;
    Direction quarterOn;
};

[[nodiscard]] OrbitPlane gridOrbitPlane() noexcept {
    const f64 i = toRadians(kGridOrbitInclination).value();
    return OrbitPlane{
        .node = Direction{1.0, 0.0, 0.0},
        .quarterOn = Direction{0.0, std::cos(i), std::sin(i)},
    };
}

// The camera at `angle` round the circle from the node, in the Earth-fixed
// frame: ahead along the track, pitched down by the dip of the horizon.
[[nodiscard]] Pose poseAround(const OrbitPlane& plane, Radians angle) noexcept {
    const f64 r = orbitRadius();
    const f64 c = std::cos(angle.value());
    const f64 s = std::sin(angle.value());
    // Outward, and along the track, both unit and square to each other.
    const Direction outward = (plane.node * c) + (plane.quarterOn * s);
    const Direction along = (plane.quarterOn * c) - (plane.node * s);

    // The horizon of a sphere of radius R seen from radius r lies acos(R / r)
    // below the local horizontal in every direction.
    const f64 cosDip = kWgs84SemiMajorAxis.value() / r;
    const f64 sinDip = std::sqrt((1.0 - cosDip) * (1.0 + cosDip));
    const Direction forward = (along * cosDip) - (outward * sinDip);
    const Direction up = (along * sinDip) + (outward * cosDip);
    const Direction back = forward * -1.0;
    const Direction right = cross(up, back);

    // Camera to world: the columns are the camera's right, up and back.
    const std::array<f64, 3> rowX{right.x.value(), up.x.value(), back.x.value()};
    const std::array<f64, 3> rowY{right.y.value(), up.y.value(), back.y.value()};
    const std::array<f64, 3> rowZ{right.z.value(), up.z.value(), back.z.value()};
    const RotationMatrix axes{.rows = {rowX, rowY, rowZ}};
    return Pose{
        .position = Position{outward.x.value() * r, outward.y.value() * r, outward.z.value() * r},
        .orientation = quaternionFrom(axes),
    };
}

} // namespace

CameraPath gridOrbitPath(const Quat& worldFromEarthFixed) {
    ORBSIM_EXPECTS(isUnitQuaternion(worldFromEarthFixed, kUnitQuaternionTolerance));
    const OrbitPlane plane = gridOrbitPlane();
    const f64 period = orbitPeriod();
    const std::uint32_t steps = kGridOrbitKeyframes - 1U;

    std::vector<Keyframe> keyframes;
    keyframes.reserve(kGridOrbitKeyframes);
    for (std::uint32_t k = 0; k <= steps; ++k) {
        // A share of the whole turn, so the last keyframe is exactly one
        // revolution and one period on.
        const f64 share = static_cast<f64>(k) / static_cast<f64>(steps);
        const Pose earthFixed = poseAround(plane, Radians{kTau * share});
        keyframes.push_back(Keyframe{
            .time = Seconds{period * share},
            .pose =
                {
                    .position = worldFromEarthFixed.rotate(earthFixed.position),
                    .orientation = worldFromEarthFixed * earthFixed.orientation,
                },
        });
    }
    // Finite positions, unit orientations and increasing times, by
    // construction: a refusal here is a defect in this function.
    auto path = CameraPath::from(keyframes);
    ORBSIM_ENSURES(path.has_value());
    return *std::move(path);
}

Seconds timeOfFrame(const CameraPath& path, RunFrame frame) {
    ORBSIM_EXPECTS(frame.count > 0U && frame.index < frame.count);
    const Seconds start = path.keyframes().front().time;
    const Seconds end = path.keyframes().back().time;
    // The last frame is the end as stored, rather than start + (end - start),
    // which can round to a time just past it -- and the path refuses that.
    if (frame.index + 1U == frame.count) return frame.count == 1U ? start : end;
    const f64 share = static_cast<f64>(frame.index) / static_cast<f64>(frame.count - 1U);
    return start + ((end - start) * share);
}

} // namespace orb::view
