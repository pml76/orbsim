//
// Tests for view/CameraPath.hpp: keyframes, and the pose at any time between
// them (M1-21; register decisions 345, 346 and 348).
//
// **Nothing here is checked against the path's own arithmetic.** The
// keyframes turn about one fixed axis, so the orientation at any time is a
// turn by a known angle, which `Quat::fromAxisAngle` builds by a route that
// shares nothing with `slerp`; and the position is held to what being on a
// straight line *means* -- its distances to the two ends add up to the
// segment's length, and the first is the right share of it -- rather than to
// the line's formula written out a second time, which a first draft of this
// suite did and which agreed with the code to the last bit for that reason. The claims that are not
// numerical -- exactness at the keyframes, determinism, the refusals -- are bit for bit and by
// name.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/CameraPath.hpp"
#include "view/Pose.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

using namespace orb;
using namespace orb::view;

namespace {

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();

// The axis every keyframe turns about, and the turn at each keyframe. The
// steps between them are under a half turn, so the shortest arc is the one
// about this axis and the expected orientation is a turn by an interpolated
// angle.
constexpr Direction kAxis{0.48, 0.6, 0.64};
constexpr auto kTimes = std::to_array<f64>({0.0, 10.0, 25.0, 25.5});
constexpr auto kAngles = std::to_array<f64>({0.3, 1.9, -0.4, -0.4});

// Positions about 400 km above the Earth's surface, at the scale the path is
// flown, so that a tolerance in metres has to mean something there. The last
// two are equal: a camera that holds still for half a second.
constexpr auto kPositions = std::to_array<Position>({
    Position{6'778'137.0, 0.0, 0.0},
    Position{6'700'000.0, 1'000'000.0, -250'000.5},
    Position{-1'200'000.25, 6'650'000.0, 400'000.0},
    Position{-1'200'000.25, 6'650'000.0, 400'000.0},
});

// How far a position may be from lying on its segment at its share of the
// way, relative to the distance from the Earth's centre, and how far an
// orientation's components may be from the turn about the axis. Each about
// twice what was measured on 2026-10-04 (VERIFICATION.md rule 4; the numbers
// are where they are asserted).
constexpr f64 kLineRelative = 8e-16;
constexpr f64 kTurnComponent = 1e-15;

// The task's unit-length budget (register decision 348).
constexpr Tolerance kUnitLength{1e-15};

[[nodiscard]] std::array<Keyframe, 4> keyframes() {
    std::array<Keyframe, 4> made{};
    for (std::size_t i = 0; i < made.size(); ++i) {
        made.at(i) = Keyframe{
            .time = Seconds{kTimes.at(i)},
            .pose =
                {
                    .position = kPositions.at(i),
                    .orientation = Quat::fromAxisAngle(kAxis, Radians{kAngles.at(i)}),
                },
        };
    }
    return made;
}

[[nodiscard]] CameraPath pathOf(std::span<const Keyframe> made) {
    auto path = CameraPath::from(made);
    REQUIRE(path.has_value());
    return *path;
}

[[nodiscard]] Pose poseOf(const CameraPath& path, Seconds time) {
    const auto pose = path.poseAt(time);
    REQUIRE(pose.has_value());
    return *pose;
}

[[nodiscard]] f64 normOf(const Quat& q) {
    return std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
}

// The largest difference between two quaternions' components, against
// whichever of the second and its negative the first lies nearer: both are
// the same orientation. An arc, so the two cannot be transposed.
[[nodiscard]] f64 worstComponent(const QuatArc& pair) {
    const Quat& got = pair.from;
    const Quat& want = pair.to;
    const f64 dot = (got.w * want.w) + (got.x * want.x) + (got.y * want.y) + (got.z * want.z);
    const f64 sign = dot < 0.0 ? -1.0 : 1.0;
    return std::max({
        std::abs(got.w - (sign * want.w)),
        std::abs(got.x - (sign * want.x)),
        std::abs(got.y - (sign * want.y)),
        std::abs(got.z - (sign * want.z)),
    });
}

// Two poses, named, so that the comparison reads as what it is.
struct PosePair {
    Pose got;
    Pose want;
};

[[nodiscard]] bool bitIdentical(const PosePair& pair) {
    return pair.got.position.bitIdentical(pair.want.position) &&
           pair.got.orientation.bitIdentical(pair.want.orientation);
}

// The stored keyframes, copied out so that a case can index them with a
// checked at().
[[nodiscard]] std::vector<Keyframe> storedOf(const CameraPath& path) {
    return {path.keyframes().begin(), path.keyframes().end()};
}

// One time inside a segment, and how far the path's pose there is from the
// straight line and from the turn about the axis.
constexpr std::size_t kSamplesPerSegment = 999;

struct Sample {
    f64 time{};
    f64 lineError{};
    f64 turnError{};
    f64 norm{};
};

// Sample `index` of all of them: segment index / 999, at step index % 999 + 1
// of a thousand along it.
[[nodiscard]] Sample sampleOf(const CameraPath& path, std::size_t index) {
    const std::size_t segment = index / kSamplesPerSegment;
    const f64 s = static_cast<f64>((index % kSamplesPerSegment) + 1) / 1000.0;
    const f64 t0 = kTimes.at(segment);
    const f64 t1 = kTimes.at(segment + 1);
    const f64 time = t0 + (s * (t1 - t0));
    const f64 along = (time - t0) / (t1 - t0);
    const Position& p0 = kPositions.at(segment);
    const Position& p1 = kPositions.at(segment + 1);
    const f64 angle =
        kAngles.at(segment) + (along * (kAngles.at(segment + 1) - kAngles.at(segment)));
    const Quat turn = Quat::fromAxisAngle(kAxis, Radians{angle});

    const Pose pose = poseOf(path, Seconds{time});
    const f64 whole = distance(p0, p1).value();
    const f64 fromStart = distance(p0, pose.position).value();
    const f64 toEnd = distance(pose.position, p1).value();
    return Sample{
        .time = time,
        .lineError =
            std::max(std::abs((fromStart + toEnd) - whole), std::abs(fromStart - (along * whole))) /
            length(pose.position).value(),
        .turnError = worstComponent({.from = pose.orientation, .to = turn}),
        .norm = normOf(pose.orientation),
    };
}

// A keyframe as the path stored it beside the one it was given: the time and
// the position bit for bit, the orientation normalised (decision 348).
struct StoredAndGiven {
    Keyframe stored;
    Keyframe given;
};

[[nodiscard]] bool isStoredAsGiven(const StoredAndGiven& pair) {
    return pair.stored.time.bitIdentical(pair.given.time) &&
           pair.stored.pose.position.bitIdentical(pair.given.pose.position) &&
           pair.stored.pose.orientation.bitIdentical(normalize(pair.given.pose.orientation));
}

// A path from keyframes with one thing changed, for the refusal cases.
[[nodiscard]] CameraPathError refusalOf(std::span<const Keyframe> made) {
    const auto path = CameraPath::from(made);
    REQUIRE(!path.has_value());
    return path.error();
}

} // namespace

TEST_CASE("a path needs at least two keyframes", "[camera_path]") {
    const auto made = keyframes();
    REQUIRE(refusalOf(std::span<const Keyframe>{}) == CameraPathError::TooFewKeyframes);
    REQUIRE(refusalOf(std::span{made}.first(1)) == CameraPathError::TooFewKeyframes);
    REQUIRE(CameraPath::from(std::span{made}.first(2)).has_value());
}

TEST_CASE("a keyframe time that is not finite is refused by name", "[camera_path]") {
    for (const f64 bad : std::to_array<f64>({kNaN, kInf, -kInf})) {
        auto made = keyframes();
        made.at(1).time = Seconds{bad};
        CAPTURE(bad);
        REQUIRE(refusalOf(made) == CameraPathError::NotFiniteTime);
    }
}

TEST_CASE("keyframe times must strictly increase", "[camera_path]") {
    // Equal, and going back: both refused, and an equal pair is the case a
    // `<` written for a `<=` lets through.
    for (const f64 second : std::to_array<f64>({0.0, -1.0})) {
        auto made = keyframes();
        made.at(1).time = Seconds{second};
        CAPTURE(second);
        REQUIRE(refusalOf(made) == CameraPathError::TimesNotIncreasing);
    }
    auto last = keyframes();
    last.at(3).time = last.at(2).time;
    REQUIRE(refusalOf(last) == CameraPathError::TimesNotIncreasing);
}

TEST_CASE("a keyframe position that is not finite is refused by name", "[camera_path]") {
    for (const f64 bad : std::to_array<f64>({kNaN, kInf, -kInf})) {
        auto made = keyframes();
        made.at(2).pose.position = Position{0.0, bad, 0.0};
        CAPTURE(bad);
        REQUIRE(refusalOf(made) == CameraPathError::NotFinitePosition);
    }
}

TEST_CASE("a keyframe orientation that is not a rotation is refused by name", "[camera_path]") {
    const auto notRotations = std::to_array<Quat>({
        Quat{0.0, 0.0, 0.0, 0.0},
        Quat{2.0, 0.0, 0.0, 0.0},
        Quat{1.0, 0.0, 0.0, 1.0},
        Quat{kNaN, 0.0, 0.0, 0.0},
    });
    for (const Quat& bad : notRotations) {
        auto made = keyframes();
        made.at(0).pose.orientation = bad;
        CAPTURE(bad.w, bad.x, bad.y, bad.z);
        REQUIRE(refusalOf(made) == CameraPathError::NotUnitOrientation);
    }
}

TEST_CASE("a time outside the path is refused by name", "[camera_path]") {
    const auto made = keyframes();
    const CameraPath path = pathOf(made);
    const auto outside = std::to_array<f64>({
        -1e-300,
        -1.0,
        std::nextafter(kTimes.back(), kInf),
        1e300,
        kNaN,
        kInf,
        -kInf,
    });
    for (const f64 time : outside) {
        const auto pose = path.poseAt(Seconds{time});
        CAPTURE(time);
        REQUIRE(!pose.has_value());
        REQUIRE(pose.error() == CameraPathError::OutsideThePath);
    }
}

TEST_CASE("a path is exact at every keyframe, the last included", "[camera_path]") {
    // Exact to the stored keyframe, which holds the orientation normalised
    // (register decision 348) and the position as given.
    const auto made = keyframes();
    const CameraPath path = pathOf(made);
    const std::vector<Keyframe> keyframesStored = storedOf(path);
    REQUIRE(keyframesStored.size() == made.size());
    for (std::size_t i = 0; i < made.size(); ++i) {
        const Keyframe& stored = keyframesStored.at(i);
        CAPTURE(i);
        REQUIRE(isStoredAsGiven({.stored = stored, .given = made.at(i)}));
        REQUIRE(bitIdentical({.got = poseOf(path, stored.time), .want = stored.pose}));
    }
}

TEST_CASE("between keyframes, a straight line and a steady turn", "[camera_path]") {
    // Measured on 2026-10-04 over these 2,997 times, the same in both trees:
    // the line within 3.95e-16 of the distance from the centre -- most of it
    // the rounding of the three distances that test it -- and the turn within
    // 4.4e-16 per component.
    const auto made = keyframes();
    const CameraPath path = pathOf(made);
    f64 worstLine = 0.0;
    f64 worstTurn = 0.0;
    for (std::size_t i = 0; i < kSamplesPerSegment * (made.size() - 1); ++i) {
        const Sample sample = sampleOf(path, i);
        worstLine = std::max(worstLine, sample.lineError);
        worstTurn = std::max(worstTurn, sample.turnError);
        CAPTURE(i, sample.time);
        REQUIRE(sample.lineError <= kLineRelative);
        REQUIRE(sample.turnError <= kTurnComponent);
        REQUIRE(nearlyEqual(sample.norm, 1.0, kUnitLength));
    }
    WARN("worst line " << worstLine << " of the distance; worst turn component " << worstTurn);
}

TEST_CASE("a path is continuous at every keyframe", "[camera_path]") {
    // One representable time either side of each inner keyframe: the pose
    // there is the keyframe's to within what one ulp of time moves it, and
    // the rounding of the arithmetic -- two ulps of the position, which near
    // 7e6 m is 1.9e-9 m, and the turn's tolerance. A jump at a boundary is
    // the length of a segment, millions of times that.
    const auto made = keyframes();
    const CameraPath path = pathOf(made);
    const std::vector<Keyframe> stored = storedOf(path);
    const auto beside = std::to_array<std::size_t>({1, 1, 2, 2});
    const auto times = std::to_array<f64>({
        std::nextafter(kTimes.at(1), -kInf),
        std::nextafter(kTimes.at(1), kInf),
        std::nextafter(kTimes.at(2), -kInf),
        std::nextafter(kTimes.at(2), kInf),
    });
    for (std::size_t i = 0; i < times.size(); ++i) {
        const Pose at = stored.at(beside.at(i)).pose;
        const Pose near = poseOf(path, Seconds{times.at(i)});
        CAPTURE(i, times.at(i));
        REQUIRE(distance(near.position, at.position).value() <= 4e-9);
        REQUIRE(worstComponent({.from = near.orientation, .to = at.orientation}) <= kTurnComponent);
    }
}

TEST_CASE("a path replays bit for bit", "[camera_path]") {
    // The same time twice from one path, and from a second path built from
    // the same keyframes: the claim a benchmark rests on (VERIFICATION.md
    // rule 16).
    const auto made = keyframes();
    const CameraPath first = pathOf(made);
    const CameraPath second = pathOf(made);
    for (std::size_t step = 0; step <= 2550; ++step) {
        const Seconds time{static_cast<f64>(step) * 0.01};
        const Pose once = poseOf(first, time);
        CAPTURE(step);
        REQUIRE(bitIdentical({.got = poseOf(first, time), .want = once}));
        REQUIRE(bitIdentical({.got = poseOf(second, time), .want = once}));
    }
}

TEST_CASE("a still camera holds its position bit for bit", "[camera_path]") {
    // The last two keyframes are equal: between them the position is the
    // position itself, because p0 + (p1 - p0) s adds an exact zero. The
    // orientation is (1 - s) q + s q, within an ulp of q.
    const auto made = keyframes();
    const CameraPath path = pathOf(made);
    const Pose still = storedOf(path).at(3).pose;
    for (std::size_t step = 1; step < 100; ++step) {
        const f64 time = kTimes.at(2) + (static_cast<f64>(step) * 0.005);
        const Pose pose = poseOf(path, Seconds{time});
        CAPTURE(step, time);
        REQUIRE(pose.position.bitIdentical(still.position));
        REQUIRE(worstComponent({.from = pose.orientation, .to = still.orientation}) <=
                std::numeric_limits<f64>::epsilon());
    }
}

TEST_CASE("keyframes at the camera's tolerance still give unit orientations", "[camera_path]") {
    // Each orientation stretched 15 ulp from unit length: still a rotation by
    // `Camera`'s 16 ulp, and a slerp between two of them would be about 17.5
    // ulp out -- an orientation `Camera::from` refuses. The path normalises
    // them when it is built (register decision 348), so every pose is within
    // the task's 1e-15.
    auto made = keyframes();
    constexpr f64 kStretch = 1.0 + (15.0 * std::numeric_limits<f64>::epsilon());
    for (Keyframe& keyframe : made) {
        const Quat q = keyframe.pose.orientation;
        keyframe.pose.orientation =
            Quat{q.w * kStretch, q.x * kStretch, q.y * kStretch, q.z * kStretch};
        REQUIRE(isUnitQuaternion(keyframe.pose.orientation, kUnitQuaternionTolerance));
    }
    const CameraPath path = pathOf(made);
    for (std::size_t step = 0; step <= 255; ++step) {
        const Pose pose = poseOf(path, Seconds{static_cast<f64>(step) * 0.1});
        CAPTURE(step);
        REQUIRE(nearlyEqual(normOf(pose.orientation), 1.0, kUnitLength));
    }
}

TEST_CASE("keyframes written as q and -q do not spin the camera", "[camera_path]") {
    // The same orientation with opposite signs (register decision 349): the
    // camera must face the same way all the way between them.
    auto made = keyframes();
    const Quat q = made.at(0).pose.orientation;
    made.at(1).pose.orientation = Quat{-q.w, -q.x, -q.y, -q.z};
    const CameraPath path = pathOf(std::span{made}.first(2));
    for (std::size_t step = 1; step < 100; ++step) {
        const Pose pose = poseOf(path, Seconds{static_cast<f64>(step) * 0.1});
        CAPTURE(step);
        REQUIRE(worstComponent({.from = pose.orientation, .to = normalize(q)}) <= kTurnComponent);
    }
}

TEST_CASE("every camera path error describes itself", "[camera_path]") {
    const auto all = std::to_array<CameraPathError>({
        CameraPathError::TooFewKeyframes,
        CameraPathError::NotFiniteTime,
        CameraPathError::TimesNotIncreasing,
        CameraPathError::NotFinitePosition,
        CameraPathError::NotUnitOrientation,
        CameraPathError::OutsideThePath,
    });
    for (const CameraPathError error : all) {
        REQUIRE(!describe(error).empty());
    }
}
