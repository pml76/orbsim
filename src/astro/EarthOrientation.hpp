#ifndef ORBSIM_ASTRO_EARTHORIENTATION_HPP
#define ORBSIM_ASTRO_EARTHORIENTATION_HPP
//
// How the Earth is turned: precession, nutation and the Earth rotation angle
// (M1-07).
//
// Two frames and the rotation between them. The inertial one is the celestial
// frame ERFA works in, aligned with the ICRS; the terrestrial one turns with
// the crust. Earth has to be drawn with its texture in the right place and its
// terminator where the Sun actually is, and both need this rotation, as do the
// ground track and the Orbit MFD later.
//
// **ERFA computes it** (ADR 0016), through two of its entry points, and these
// are the typed wrappers: eraC2t06a for the whole rotation -- IAU 2006
// precession and IAU 2000A nutation in the CIO-based form, composed with the
// Earth rotation angle -- and eraC2i06a for the part of it that does not
// involve the Earth's rotation at all. ERFA's headers are included in the .cpp
// and nowhere else.
//
// **Which way round.** Every rotation here maps *from* the frame named second
// *to* the frame named first, as a conversion between time scales does:
// earthFixedFromInertial(...).rotate(v) takes an inertial vector and gives the
// Earth-fixed one. That is the direction ERFA's matrix has, so nothing here
// transposes anything (register decision 79).
//
// **Polar motion is not modelled, and neither is a measured DeltaUT1.** Both
// are model error, deliberately taken, and their sizes belong here rather than
// in a commit message somebody would have to find:
//
//   * polar motion omitted: <= 0.6", about 19 m on the ground -- the largest
//     pole excursion in the IERS EOP 20 C04 series, 1962-2025;
//   * DeltaUT1 unmodelled, inside the leap-second table: <= 13.5", about 420 m,
//     since UTC is kept within 0.9 s of UT1;
//   * together **<= 14.1", about 440 m**.
//
//   Past the table's expiry the UT1 handed in comes from a held DeltaT
//   (core/Time.hpp, M1-86), and its drift adds to that: at worst 17.3" for each
//   year since, on the IERS record. The caller chooses the DeltaT and takes
//   that error knowingly; this file only says what it is worth.
//
//   The **code** error is a different number and a much smaller one: the
//   rotation agrees with Skyfield 1.55 -- an independent formulation of the
//   same IAU models -- to **0.1 mas** over 1900-2100, asserted in
//   tests/test_earth_orientation.cpp against a committed fixture. Measured
//   worst on it: 53.6 uas, of which about 47 uas is the TIO locator s' that
//   ERFA applies and the equinox-based route does not.
//
// **Nothing here can fail.** Neither eraC2t06a nor eraC2i06a nor eraEra00
// returns a status, and no date is out of range for them, so none of these
// returns std::expected and nobody need look for an error that does not exist.
// A date that is not finite can reach them only from a Release build whose
// arithmetic precondition was violated upstream, and comes back as a NaN, as
// astro/Tdb.hpp's conversions do.
//
#include "core/Math.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

namespace orb {

// The Earth rotation angle: how far the Earth has turned since J2000.0 about
// the celestial intermediate pole, from UT1 alone. In [0, 2pi).
//
// ERFA's eraEra00, which implements the IAU 2000 Resolution B1.8 definition:
// ERA = 2 pi (0.7790572732640 + 1.00273781191135448 Tu), with Tu the UT1
// Julian date less 2451545.0. Both constants are definitions, not
// measurements, so the angle is exactly what the resolution says it is.
//
// On its own, because the ground track and the MFD will both want it without
// the rest of the rotation.
[[nodiscard]] Radians earthRotationAngle(Ut1Time ut1) noexcept;

// The rotation from the celestial frame to the terrestrial one: precession,
// nutation, the Earth's rotation, and polar motion passed as zero.
[[nodiscard]] RotationMatrix earthFixedFromInertialMatrix(TtTime tt, Ut1Time ut1) noexcept;
[[nodiscard]] Quat earthFixedFromInertial(TtTime tt, Ut1Time ut1) noexcept;

// The same rotation with the Earth's own turn left out: the celestial
// intermediate frame, whose third axis is the pole of date.
//
// **A function of TT alone**, which is the point of it (register decision 77).
// The J2 term in phase E needs the figure axis inside an integrator that runs
// on TT, and M1-08's equinox test needs the true equator of date; neither
// depends on UT1, and neither should have to invent one. The pole it gives is
// bit-for-bit the pole of the full rotation.
//
// It costs about 28 us a call, almost all of it the nutation series, so a
// caller stepping an orbit evaluates it at a cadence of its own rather than at
// every stage.
[[nodiscard]] RotationMatrix intermediateFromInertialMatrix(TtTime tt) noexcept;
[[nodiscard]] Quat intermediateFromInertial(TtTime tt) noexcept;

} // namespace orb

#endif // ORBSIM_ASTRO_EARTHORIENTATION_HPP
