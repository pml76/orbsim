//
// Tests for view/BenchmarkPath.hpp: the `grid-orbit` path and the time each
// measured frame is drawn at (M1-22; register decision 368).
//
// **The path is held to what it is meant to be, not to its own formulas.**
// Its altitude is read back from the positions; that the camera looks at the
// horizon is checked as a ray that grazes the sphere -- its distance from the
// Earth's centre is the Earth's radius -- which no step of the construction
// computes; the inclination is the highest latitude the circle reaches; and
// the period is the 50-digit value of Kepler's third law, worked outside the
// code.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "tests/OrbitTestSupport.hpp"
#include "view/BenchmarkPath.hpp"
#include "view/CameraPath.hpp"
#include "view/Pose.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace orb;
using namespace orb::view;
using orb::test::WithinAbsOf;
using orb::test::WithinRelTo;

namespace {

// 2 pi sqrt(r^3 / mu) for r = 6,778,137 m and WGS-84's mu, in 50-digit
// decimal arithmetic (Python's decimal module, 2026-10-04):
// 5553.6242712522280353830658670637...
constexpr f64 kPeriod = 5553.624271252228;

// The radius of the circle and of the Earth, in metres.
constexpr f64 kRadius = 6'778'137.0;
constexpr f64 kEarth = 6'378'137.0;

// How far straight lines between keyframes a degree apart cut inside the
// circle: r (1 - cos 0.5 degree), 258.0907 m, worked in 50 digits.
constexpr f64 kChordSag = 258.0907;

// The tolerances, each about twice the largest error measured on 2026-10-04
// over every assertion that uses it, read from the failures of a run with
// all of them zero (VERIFICATION.md rule 4):
//
// - a radius or a distance between two positions, relative to the circle's
//   radius: 2.75e-16 measured;
// - the line of sight's distance from the centre, relative to the Earth's
//   radius: 7.3e-16;
// - a component of, or a difference between, two unit directions: 8.3e-16.
//
// The period was the 50-digit value's nearest double exactly, and is
// asserted bit for bit: it is one square root and three products, each
// correctly rounded on every IEEE machine.
constexpr Tolerance kOnCircleRelative{6e-16};
constexpr Tolerance kGrazeRelative{1.5e-15};
constexpr Tolerance kDirection{1.7e-15};

[[nodiscard]] Pose poseOf(const CameraPath& path, Seconds time) {
    const auto pose = path.poseAt(time);
    REQUIRE(pose.has_value());
    return *pose;
}

// The path's keyframes, copied so that they can be read with a checked at().
[[nodiscard]] std::vector<Keyframe> keyframesOf(const CameraPath& path) {
    return {path.keyframes().begin(), path.keyframes().end()};
}

// The camera's axes in the world frame: the columns of its orientation.
// The camera looks down its own -z (view/Pose.hpp).
[[nodiscard]] Direction forwardOf(const Pose& pose) {
    return pose.orientation.rotate(Direction{0.0, 0.0, -1.0});
}
[[nodiscard]] Direction upOf(const Pose& pose) {
    return pose.orientation.rotate(Direction{0.0, 1.0, 0.0});
}
[[nodiscard]] Direction rightOf(const Pose& pose) {
    return pose.orientation.rotate(Direction{1.0, 0.0, 0.0});
}

[[nodiscard]] Direction unitOf(const Position& p) { return directionOf(p); }

[[nodiscard]] f64 radiusOf(const Position& p) { return length(p).value(); }

// A turn that is not about any axis the construction uses.
[[nodiscard]] Quat someRotation() {
    return normalize(Quat::fromAxisAngle(Direction{0.3, -0.5, 0.8}, Radians{1.1}));
}

// The lowest point of the path: ten points in each segment, the middle among
// them, where a straight line between two keyframes dips deepest.
[[nodiscard]] f64 lowestRadius(const CameraPath& path) {
    const std::vector<Keyframe> keyframes = keyframesOf(path);
    f64 lowest = kRadius;
    for (std::size_t i = 1; i < keyframes.size(); ++i) {
        const f64 from = keyframes.at(i - 1).time.value();
        const f64 to = keyframes.at(i).time.value();
        for (int step = 1; step < 10; ++step) {
            const f64 t = from + ((to - from) * static_cast<f64>(step) / 10.0);
            lowest = std::min(lowest, radiusOf(poseOf(path, Seconds{t}).position));
        }
    }
    return lowest;
}

// Where the camera at `pose` looks, with `next` the position of the keyframe
// after it.
void checkLooksAtTheHorizon(const Pose& pose, const Position& next) {
    const Direction forward = forwardOf(pose);
    // The line of sight grazes the Earth: its distance from the centre,
    // |p x f| for a unit f, is the Earth's radius.
    CHECK_THAT(length(cross(pose.position, forward)).value(), WithinRelTo(kEarth, kGrazeRelative));
    // Ahead, not behind: toward the next keyframe.
    CHECK(dot(forward, next - pose.position).value() > 0.0);
    // Up leans toward the local vertical by exactly the dip: cos of it is
    // R / r.
    CHECK_THAT(dot(upOf(pose), unitOf(pose.position)).value(),
               WithinAbsOf(kEarth / kRadius, kDirection));
    // Right is square to the orbit's plane, so the horizon is level.
    CHECK_THAT(dot(rightOf(pose), unitOf(pose.position)).value(), WithinAbsOf(0.0, kDirection));
    CHECK_THAT(dot(rightOf(pose), unitOf(next)).value(), WithinAbsOf(0.0, kDirection));
}

// Frames 1 to count - 1 each come after the one before, by an equal step.
void checkEvenSteps(const CameraPath& path, std::uint32_t count) {
    const f64 start = path.keyframes().front().time.value();
    const f64 end = path.keyframes().back().time.value();
    const f64 step = (end - start) / static_cast<f64>(count - 1U);
    f64 previous = start;
    for (std::uint32_t k = 1; k < count; ++k) {
        INFO("frame " << k);
        const f64 now = timeOfFrame(path, {.index = k, .count = count}).value();
        CHECK(now > previous);
        CHECK_THAT(now - previous, WithinRelTo(step, Tolerance{1e-9}));
        previous = now;
    }
}

} // namespace

TEST_CASE("grid-orbit is one revolution, in the orbit's period") {
    const std::vector<Keyframe> keyframes = keyframesOf(gridOrbitPath(Quat{}));
    REQUIRE(keyframes.size() == kGridOrbitKeyframes);
    CHECK(keyframes.front().time.bitIdentical(Seconds{0.0}));
    CHECK(keyframes.back().time.bitIdentical(Seconds{kPeriod}));
    // It closes: the last keyframe is where the first was.
    CHECK_THAT(radiusOf(keyframes.back().pose.position - keyframes.front().pose.position) / kRadius,
               WithinAbsOf(0.0, kOnCircleRelative));
}

TEST_CASE("every keyframe is 400 km up, and every point between within the chord's sag") {
    const CameraPath path = gridOrbitPath(someRotation());
    for (const Keyframe& keyframe : keyframesOf(path)) {
        INFO("keyframe at " << keyframe.time.value() << " s");
        CHECK_THAT(radiusOf(keyframe.pose.position), WithinRelTo(kRadius, kOnCircleRelative));
    }
    const f64 lowest = lowestRadius(path);
    INFO("lowest point " << (lowest - kEarth) << " m up");
    CHECK(lowest >= kRadius - kChordSag);
    CHECK(lowest <= kRadius);
    // And the sag is really there: the middle of a segment is close to its
    // full depth, so the check above is not passing on a path that never dips.
    CHECK(lowest <= kRadius - (0.99 * kChordSag));
}

TEST_CASE("the camera looks at the horizon, ahead along the track") {
    const std::vector<Keyframe> keyframes = keyframesOf(gridOrbitPath(someRotation()));
    for (std::size_t i = 0; i + 1 < keyframes.size(); ++i) {
        INFO("keyframe " << i);
        checkLooksAtTheHorizon(keyframes.at(i).pose, keyframes.at(i + 1).pose.position);
    }
}

TEST_CASE("the orbit is inclined 51.6 degrees, its node on the prime meridian") {
    // With the Earth-fixed frame as the world, the circle starts on the
    // equator at longitude zero and climbs to 51.6 degrees north a quarter
    // of the way round.
    const std::vector<Keyframe> keyframes = keyframesOf(gridOrbitPath(Quat{}));
    const Direction start = unitOf(keyframes.front().pose.position);
    CHECK_THAT(start.x.value(), WithinAbsOf(1.0, kDirection));
    CHECK_THAT(start.y.value(), WithinAbsOf(0.0, kDirection));
    CHECK_THAT(start.z.value(), WithinAbsOf(0.0, kDirection));
    f64 highest = -1.0;
    for (const Keyframe& keyframe : keyframes) {
        highest = std::max(highest, unitOf(keyframe.pose.position).z.value());
    }
    CHECK_THAT(highest, WithinAbsOf(std::sin(51.6 * kPi / 180.0), kDirection));
    // Ascending: north of the equator just after the node.
    CHECK(keyframes.at(1).pose.position.z.value() > 0.0);
}

TEST_CASE("the rotation handed in places the whole path") {
    const Quat turn = someRotation();
    const std::vector<Keyframe> fixed = keyframesOf(gridOrbitPath(Quat{}));
    const std::vector<Keyframe> turned = keyframesOf(gridOrbitPath(turn));
    REQUIRE(fixed.size() == turned.size());
    for (std::size_t i = 0; i < fixed.size(); ++i) {
        INFO("keyframe " << i);
        const Pose& a = fixed.at(i).pose;
        const Pose& b = turned.at(i).pose;
        CHECK(fixed.at(i).time.bitIdentical(turned.at(i).time));
        CHECK_THAT(radiusOf(turn.rotate(a.position) - b.position) / kRadius,
                   WithinAbsOf(0.0, kOnCircleRelative));
        CHECK_THAT(length(turn.rotate(forwardOf(a)) - forwardOf(b)).value(),
                   WithinAbsOf(0.0, kDirection));
    }
}

TEST_CASE("the path is the same on every build") {
    const std::vector<Keyframe> first = keyframesOf(gridOrbitPath(someRotation()));
    const std::vector<Keyframe> second = keyframesOf(gridOrbitPath(someRotation()));
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        INFO("keyframe " << i);
        CHECK(first.at(i).time.bitIdentical(second.at(i).time));
        CHECK(first.at(i).pose.position.bitIdentical(second.at(i).pose.position));
        CHECK(first.at(i).pose.orientation.bitIdentical(second.at(i).pose.orientation));
    }
}

TEST_CASE("the measured frames spread evenly over the whole path") {
    const CameraPath path = gridOrbitPath(Quat{});
    const Seconds start = path.keyframes().front().time;
    const Seconds end = path.keyframes().back().time;
    CHECK(timeOfFrame(path, {.index = 0, .count = 600}).bitIdentical(start));
    CHECK(timeOfFrame(path, {.index = 599, .count = 600}).bitIdentical(end));
    CHECK(timeOfFrame(path, {.index = 0, .count = 1}).bitIdentical(start));
    CHECK(timeOfFrame(path, {.index = 1, .count = 2}).bitIdentical(end));
    checkEvenSteps(path, 600);
}

TEST_CASE("the last frame is the path's end exactly, where start + (end - start) is not") {
    // grid-orbit starts at zero, where start + (end - start) is the end bit
    // for bit, so the case above cannot tell the stored end from the sum.
    // From 0.3 s to 0.9 s it can: the sum is 0.9000000000000001, past the
    // end, and the path refuses a time past its end.
    const Pose still{.position = Position{kRadius, 0.0, 0.0}, .orientation = Quat{}};
    const std::vector<Keyframe> made{
        Keyframe{.time = Seconds{0.3}, .pose = still},
        Keyframe{.time = Seconds{0.9}, .pose = still},
    };
    const auto path = CameraPath::from(made);
    REQUIRE(path.has_value());
    const Seconds last = timeOfFrame(*path, {.index = 9, .count = 10});
    CHECK(last.bitIdentical(Seconds{0.9}));
    CHECK(path->poseAt(last).has_value());
}
