#ifndef ORBSIM_VIEW_CAMERAPATH_HPP
#define ORBSIM_VIEW_CAMERAPATH_HPP
//
// A scripted camera: keyframes of time, position and orientation, and the pose
// at any time between them (M1-21; register decisions 345, 346 and 348).
//
// **A lookup is a pure function of the path and the time.** No clock, no frame
// counter, no state: the pose at a time is the same on every call, on every
// run, which is what makes a benchmark repeatable (M1-22) and a scripted
// descent replay exactly (M1-60). tests/test_camera_path.cpp asserts it bit
// for bit.
//
// **Straight lines in position, `slerp` in orientation**, keyframe to
// keyframe. Position is `p0 + (p1 - p0) s` rather than `(1 - s) p0 + s p1`:
// between two equal positions the first gives the position itself, bit for
// bit, where the second can wander by an ulp -- and a still camera is two
// equal keyframes (decision 345).
//
// **Exact at every keyframe.** A time that lands on a keyframe returns that
// keyframe's pose as stored, without arithmetic, so the pose is continuous
// across it: the segment before approaches it, and the one after starts from
// it.
//
// **Each orientation is normalised once, when the path is built** (decision
// 348). `Camera` admits an orientation up to 16 ulp from unit length; `slerp`
// adds about 2.5 ulp, so an unnormalised keyframe at that edge could give an
// orientation `Camera::from` refuses. Normalised, every pose a path gives is
// within 1e-15 of unit length, and "exact at the keyframes" means exact to
// the stored keyframe, which `keyframes()` shows.
//
// **Time is seconds from the path's own start**, not a calendar date: a path
// describes a camera's movement, and when it is flown is the caller's.
//
#include "core/Attributes.hpp"
#include "core/Units.hpp"
#include "view/Pose.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace orb::view {

// One keyframe: where the camera is and which way it faces, at a time.
struct Keyframe {
    Seconds time;
    Pose pose;
};

// What building a path or asking it for a pose can fail with. Reported, not
// asserted: a path comes from a script -- M1-22's benchmark, M1-60's descent,
// a file one day -- so every one of these is something a caller can produce
// (ADR 0002). One name per failure, each saying what is wrong.
enum class CameraPathError : std::uint8_t {
    TooFewKeyframes,    // a path needs a start and an end
    NotFiniteTime,      // a keyframe's time is infinite or not a number
    TimesNotIncreasing, // each keyframe must come strictly after the one before
    NotFinitePosition,  // a keyframe's position is infinite or not a number
    NotUnitOrientation, // a keyframe's orientation is not a rotation
    OutsideThePath,     // asked for a time before the first or after the last
};

[[nodiscard]] constexpr std::string_view describe(CameraPathError error) noexcept {
    switch (error) {
    case CameraPathError::TooFewKeyframes:
        return "a camera path needs at least two keyframes";
    case CameraPathError::NotFiniteTime:
        return "every keyframe's time must be finite";
    case CameraPathError::TimesNotIncreasing:
        return "each keyframe must come strictly after the one before it";
    case CameraPathError::NotFinitePosition:
        return "every keyframe's position must be finite";
    case CameraPathError::NotUnitOrientation:
        return "every keyframe's orientation must be a unit quaternion";
    case CameraPathError::OutsideThePath:
        return "the time asked for is outside the camera path";
    }
    return "unknown camera path error";
}

class CameraPath {
public:
    // A path through `keyframes`, in the order given, or the first thing wrong
    // with them -- the keyframes' own checks in order, then their times.
    [[nodiscard]] static std::expected<CameraPath, CameraPathError>
    from(std::span<const Keyframe> keyframes);

    // The pose at `time`, which must lie from the first keyframe's time to
    // the last's, both included (decision 345); anything else, a NaN among
    // it, is OutsideThePath.
    [[nodiscard]] std::expected<Pose, CameraPathError> poseAt(Seconds time) const noexcept;

    // The keyframes as stored, orientations normalised.
    [[nodiscard]] std::span<const Keyframe> keyframes() const noexcept ORBSIM_LIFETIMEBOUND {
        return keyframes_;
    }

private:
    explicit CameraPath(std::vector<Keyframe> keyframes) noexcept;

    std::vector<Keyframe> keyframes_;
};

static_assert(describe(CameraPathError::TooFewKeyframes) !=
                  describe(CameraPathError::NotFiniteTime),
              "each error says what is wrong");
static_assert(describe(CameraPathError::NotFiniteTime) !=
              describe(CameraPathError::TimesNotIncreasing));
static_assert(describe(CameraPathError::TimesNotIncreasing) !=
              describe(CameraPathError::NotFinitePosition));
static_assert(describe(CameraPathError::NotFinitePosition) !=
              describe(CameraPathError::NotUnitOrientation));
static_assert(describe(CameraPathError::NotUnitOrientation) !=
              describe(CameraPathError::OutsideThePath));

} // namespace orb::view

#endif // ORBSIM_VIEW_CAMERAPATH_HPP
