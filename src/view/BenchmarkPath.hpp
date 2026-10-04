#ifndef ORBSIM_VIEW_BENCHMARKPATH_HPP
#define ORBSIM_VIEW_BENCHMARKPATH_HPP
//
// The benchmark's camera path, `grid-orbit`, and which point on a path each
// measured frame shows (M1-22; register decision 368).
//
// **`grid-orbit` is one full circle at 400 km**, the altitude realism.md
// section 6.5 states its frame-time budget at, **on an orbit inclined 51.6
// degrees** -- the International Space Station's -- so that the view passes
// over the equator, both mid-latitudes and the crowded meridians near 51.6
// degrees north and south rather than one stretch of grid. The ascending node
// is on the prime meridian, in the Earth-fixed frame, and the circle is then
// turned into the world frame by the rotation the caller hands in -- the grid
// probes' `worldFromEarthFixed` -- so the path and the grid it flies over are
// placed by the same rotation.
//
// **The camera looks ahead along the track, pitched down by the dip of the
// horizon**, acos(R / (R + h)) = 19.78 degrees at 400 km, so the horizon
// crosses the middle of the picture: the Earth below it, space above. Up is
// toward the local vertical, never upside down.
//
// **A keyframe every degree, 361 of them**, the last closing the circle. The
// path is straight between keyframes (view/CameraPath.hpp), so it cuts the
// circle's corners by at most r (1 - cos 0.5 degree) = 258 m below 400 km.
//
// **Its time is the orbit's own**: one revolution takes the period of a
// circular orbit of that radius about the Earth, 5,553.6 s, so a path time
// in the benchmark's table reads as seconds of flight. The benchmark does not
// fly it in real time; it spreads the whole circle over its measured frames
// (timeOfFrame below).
//
// **Deterministic**: the same rotation gives the same path bit for bit, and
// the frames' times depend on the frame number and the count alone -- no
// clock -- so frame k shows the same view on every machine at every frame
// rate.
//
#include "core/Math.hpp"
#include "core/Units.hpp"
#include "view/CameraPath.hpp"

#include <cstdint>

namespace orb::view {

// The circle's height above the WGS-84 equatorial radius: realism.md 6.5.
inline constexpr Metres kGridOrbitAltitude{400e3};

// The International Space Station's inclination, to the Earth's equator.
inline constexpr Degrees kGridOrbitInclination{51.6};

// The Earth's gravitational parameter, WGS-84's: 3.986004418e14 m^3/s^2,
// including the atmosphere's mass (NIMA TR8350.2, third edition, table 3.1).
// Used for the period alone, which sets only how the path's time is labelled.
inline constexpr GravParam kEarthGravParam = gravParam(3.986004418e14);

// One keyframe a degree, from 0 to 360 both included.
inline constexpr std::uint32_t kGridOrbitKeyframes = 361;

// The path, with `worldFromEarthFixed` taking an Earth-fixed vector to the
// world frame. It must be a unit quaternion; anything else is a defect in the
// caller, and asserted.
[[nodiscard]] CameraPath gridOrbitPath(const Quat& worldFromEarthFixed);

// A measured frame: which of how many. By name, so the two counts cannot be
// handed over the wrong way round (non-negotiable 1).
struct RunFrame {
    std::uint32_t index{};
    std::uint32_t count{};
};

// When on `path` measured frame `frame.index` of `frame.count` is drawn: the
// whole path spread evenly over the frames, the first at its start and the
// last at its end exactly, and a run of one frame at its start. The index
// must be below the count, which must not be zero: the caller counts, so
// either is a defect, and asserted.
[[nodiscard]] Seconds timeOfFrame(const CameraPath& path, RunFrame frame);

} // namespace orb::view

#endif // ORBSIM_VIEW_BENCHMARKPATH_HPP
