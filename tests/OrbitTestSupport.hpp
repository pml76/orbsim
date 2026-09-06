#ifndef ORBSIM_TESTS_ORBITTESTSUPPORT_HPP
#define ORBSIM_TESTS_ORBITTESTSUPPORT_HPP
//
// Fixtures shared by the orbit suites: the central bodies they fly around,
// and an element builder that takes degrees so the cases read like a textbook.
//
#include "core/Math.hpp"
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"

#include <cmath>

namespace orb::test {

// Gravitational parameters, m^3/s^2. Earth is WGS-84 / EGM-96; the others are
// the IAU 2015 nominal values as tabulated in JPL's DE440 documentation.
inline constexpr GravParam kMuMoon{4.9028001e12};
inline constexpr GravParam kMuEarth{3.986004418e14};
inline constexpr GravParam kMuJupiter{1.26686534e17};
inline constexpr GravParam kMuSun{1.32712440018e20};

// WGS-84 equatorial radius.
inline constexpr Metres kEarthRadius{6378137.0};

[[nodiscard]] inline Elements
makeElements(Metres sma, Eccentricity ecc, Degrees inc, Degrees lan, Degrees aop, Degrees tra) {
    return Elements{.sma = sma,
                    .ecc = ecc,
                    .inc = toRadians(inc),
                    .lan = toRadians(lan),
                    .aop = toRadians(aop),
                    .tra = toRadians(tra),
                    .slr = Metres{sma.value * (1.0 - (ecc.value * ecc.value))}};
}

// A circular orbit of radius r about mu, starting on the x-axis: the one case
// with a closed-form answer at every time, which makes it the right probe for
// a propagator at an unfamiliar scale.
[[nodiscard]] inline StateVector circularState(GravParam mu, Metres r) {
    return StateVector{.pos = {r.value, 0.0, 0.0},
                       .vel = {0.0, std::sqrt(mu.value / r.value), 0.0}};
}

// Specific orbital energy and angular momentum from a state: the two constants
// of two-body motion, computed independently of anything in Orbit.cpp so that
// a propagator can be checked against physics rather than against itself.
[[nodiscard]] inline SpecificEnergy specificEnergy(const StateVector& sv, GravParam mu) {
    return SpecificEnergy{(0.5 * lengthSq(sv.vel)) - (mu.value / length(sv.pos))};
}

[[nodiscard]] inline Vec3 specificAngularMomentum(const StateVector& sv) {
    return cross(sv.pos, sv.vel);
}

} // namespace orb::test

#endif // ORBSIM_TESTS_ORBITTESTSUPPORT_HPP
