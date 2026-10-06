#include "view/CameraPath.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Pose.hpp"

#include <algorithm>
#include <expected>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace orb::view {

namespace {

[[nodiscard]] bool isFinitePosition(const Position& p) noexcept {
    return isFinite(p.x.value()) && isFinite(p.y.value()) && isFinite(p.z.value());
}

// What is wrong with one keyframe on its own, if anything, in the order the
// enumeration names them.
[[nodiscard]] std::optional<CameraPathError> faultIn(const Keyframe& keyframe) noexcept {
    if (!isFinite(keyframe.time.value())) return CameraPathError::NotFiniteTime;
    if (!isFinitePosition(keyframe.pose.position)) return CameraPathError::NotFinitePosition;
    if (!isUnitQuaternion(keyframe.pose.orientation, kUnitQuaternionTolerance)) {
        return CameraPathError::NotUnitOrientation;
    }
    return std::nullopt;
}

// The position `along` of the way from `segment.from` to `segment.to`, as
// p0 + (p1 - p0) s: between two equal positions that is the position itself,
// bit for bit, because the difference is an exact zero (CameraPath.hpp).
struct PositionSegment {
    Position from;
    Position to;
};

[[nodiscard]] Position positionAlong(const PositionSegment& segment, Fraction along) noexcept {
    return segment.from + ((segment.to - segment.from) * along.value());
}

} // namespace

CameraPath::CameraPath(std::vector<Keyframe> keyframes) noexcept
    : keyframes_(std::move(keyframes)) {}

std::expected<CameraPath, CameraPathError> CameraPath::from(std::span<const Keyframe> keyframes) {
    if (keyframes.size() < 2) return std::unexpected(CameraPathError::TooFewKeyframes);
    for (const Keyframe& keyframe : keyframes) {
        if (const auto fault = faultIn(keyframe)) return std::unexpected(*fault);
    }
    // Strictly: two keyframes at one time would make a segment of zero
    // length, and the fraction along it 0 / 0.
    const auto notIncreasing = std::ranges::adjacent_find(
        keyframes, [](const Keyframe& earlier, const Keyframe& later) noexcept {
            return !(earlier.time < later.time);
        });
    if (notIncreasing != keyframes.end()) {
        return std::unexpected(CameraPathError::TimesNotIncreasing);
    }

    std::vector<Keyframe> stored(keyframes.begin(), keyframes.end());
    for (Keyframe& keyframe : stored) {
        keyframe.pose.orientation = normalize(keyframe.pose.orientation);
    }
    return CameraPath{std::move(stored)};
}

std::expected<Pose, CameraPathError> CameraPath::poseAt(Seconds time) const noexcept {
    // Written so that a NaN fails it: every comparison with one is false.
    if (!(time >= keyframes_.front().time && time <= keyframes_.back().time)) {
        return std::unexpected(CameraPathError::OutsideThePath);
    }
    // The first keyframe after `time`; the segment starts at the one before.
    // None after it means `time` is the last keyframe's own.
    const auto after = std::ranges::upper_bound(
        keyframes_, time, std::less<>{}, [](const Keyframe& keyframe) noexcept {
            return keyframe.time;
        });
    if (after == keyframes_.end()) return keyframes_.back().pose;
    const Keyframe& start = *std::prev(after);
    const Keyframe& end = *after;
    // On a keyframe: its pose as stored, without arithmetic, which is what
    // makes the path exact there (CameraPath.hpp).
    if (!(time > start.time)) return start.pose;

    // Inside (0, 1] by construction -- `time` lies after `start` and before
    // `end`, which the factory put strictly later -- so the factory cannot
    // refuse it; a refusal would be a defect here.
    // Seconds over seconds: a plain number, its unit s/s of magnitude one.
    const auto along = Fraction::from(((time - start.time) / (end.time - start.time)).value());
    ORBSIM_ENSURES(along.has_value());
    return Pose{
        .position = positionAlong({.from = start.pose.position, .to = end.pose.position}, *along),
        .orientation = slerp({.from = start.pose.orientation, .to = end.pose.orientation}, *along),
    };
}

} // namespace orb::view
