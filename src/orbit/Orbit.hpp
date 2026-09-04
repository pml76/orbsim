#pragma once
//
// Two-body orbital mechanics: state vectors, classical elements, and Keplerian
// propagation.
//
// The live simulation integrates numerically (so perturbations are real), but
// the sim needs closed-form Kepler solutions in three places where integration
// is the wrong tool:
//
//   * drawing the orbit path      - one revolution sampled instantly
//   * high time acceleration      - stepping months of flight without drift
//   * MFD predictions             - "where am I at periapsis" must be exact
//
// Propagation uses the universal-variable formulation, which handles elliptic,
// parabolic and hyperbolic trajectories with one code path. Escape trajectories
// are ordinary here, not a special case.
//
#include "core/Math.hpp"

namespace orb {

// Position and velocity relative to the central body's inertial frame.
struct StateVector {
    Vec3 pos;   // m
    Vec3 vel;   // m/s
};

// Classical (Keplerian) orbital elements.
//
// Degenerate orbits get a canonical parameterisation rather than NaN: a
// circular orbit has no periapsis, so `aop` is zero and `tra` becomes the
// argument of latitude; an equatorial orbit has no ascending node, so `lan` is
// zero and in-plane angles are measured from the x-axis instead.
struct Elements {
    f64 sma{};        // semi-major axis, m. Negative for hyperbolic orbits.
    f64 ecc{};        // eccentricity, dimensionless
    f64 inc{};        // inclination, rad, [0, pi]
    f64 lan{};        // longitude of ascending node, rad, [0, tau)
    f64 aop{};        // argument of periapsis, rad, [0, tau)
    f64 tra{};        // true anomaly, rad, [0, tau)
    f64 slr{};        // semi-latus rectum, m. Kept explicitly so parabolic
                      // orbits (where sma is infinite) remain representable.
};

// Quantities derived from the elements that the HUD and MFDs ask for
// constantly. Computed together because they share intermediate terms.
struct OrbitInfo {
    f64 periapsis{};      // radius at periapsis, m
    f64 apoapsis{};       // radius at apoapsis, m. Infinity if not closed.
    f64 period{};         // orbital period, s. Infinity if not closed.
    f64 meanMotion{};     // rad/s. Zero if not closed.
    f64 energy{};         // specific orbital energy, J/kg
    f64 radius{};         // current radius, m
    f64 speed{};          // current speed, m/s
    bool closed{};        // true for elliptic orbits (ecc < 1)
};

// --- conversions -----------------------------------------------------------

// `mu` is the standard gravitational parameter GM of the central body, m^3/s^2.
Elements   elementsFromState(const StateVector& sv, f64 mu);
StateVector stateFromElements(const Elements& el, f64 mu);
OrbitInfo  orbitInfo(const Elements& el, f64 mu);

// --- anomaly conversions ---------------------------------------------------

// Elliptic orbits use eccentric anomaly E, hyperbolic orbits use H; each pair
// is selected on `ecc` so callers can work in mean anomaly regardless of conic.
f64 trueToEccentricAnomaly(f64 trueAnomaly, f64 ecc);
f64 eccentricToTrueAnomaly(f64 eccAnomaly, f64 ecc);
f64 eccentricToMeanAnomaly(f64 eccAnomaly, f64 ecc);

// Solves Kepler's equation for the eccentric (or hyperbolic) anomaly.
// Newton-Raphson with a conic-appropriate starting guess; converges to ~1e-13
// in a handful of iterations even at ecc = 0.999.
f64 meanToEccentricAnomaly(f64 meanAnomaly, f64 ecc);

// --- propagation -----------------------------------------------------------

// Advance a state vector by `dt` seconds along its Kepler orbit. Exact for the
// two-body problem at any dt, forward or backward.
StateVector propagate(const StateVector& sv, f64 mu, f64 dt);

// Advance only the anomaly of an element set, leaving the orbit shape intact.
Elements propagateElements(const Elements& el, f64 mu, f64 dt);

} // namespace orb
