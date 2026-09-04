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
#include "core/Units.hpp"

#include <expected>
#include <string_view>

namespace orb {

// Position and velocity relative to the central body's inertial frame.
struct StateVector {
    Vec3 pos; // m
    Vec3 vel; // m/s
};

// Classical (Keplerian) orbital elements.
//
// Degenerate orbits get a canonical parameterisation rather than NaN: a
// circular orbit has no periapsis, so `aop` is zero and `tra` becomes the
// argument of latitude; an equatorial orbit has no ascending node, so `lan` is
// zero and in-plane angles are measured from the x-axis instead.
struct Elements {
    Metres sma{};       // semi-major axis. Negative for hyperbolic orbits.
    Eccentricity ecc{}; // dimensionless
    Radians inc{};      // inclination, [0, pi]
    Radians lan{};      // longitude of ascending node, [0, tau)
    Radians aop{};      // argument of periapsis, [0, tau)
    Radians tra{};      // true anomaly, [0, tau)
    Metres slr{};       // semi-latus rectum. Kept explicitly so parabolic
                        // orbits (where sma is infinite) remain representable.
};

// Quantities derived from the elements that the HUD and MFDs ask for
// constantly. Computed together because they share intermediate terms.
struct OrbitInfo {
    Metres periapsis{}; // radius at periapsis
    Metres apoapsis{};  // radius at apoapsis. Infinity if not closed.
    Seconds period{};   // orbital period. Infinity if not closed.
    f64 meanMotion{};   // rad/s. Zero if not closed.
    f64 energy{};       // specific orbital energy, J/kg
    Metres radius{};    // current radius
    f64 speed{};        // current speed, m/s
    bool closed{};      // true for elliptic orbits (ecc < 1)
};

// --- errors ----------------------------------------------------------------

// Conditions a caller can legitimately produce. Conditions that can only arise
// from a bug in this file are asserted instead; see core/Contract.hpp.
enum class OrbitError {
    DegenerateState,      // zero radius: a vessel at the exact centre of a body
    NonPositiveGravity,   // mu <= 0 is not a central body
    SolverDidNotConverge, // Newton reached its iteration cap
};

[[nodiscard]] constexpr std::string_view describe(OrbitError error) noexcept {
    switch (error) {
    case OrbitError::DegenerateState:
        return "state vector has zero radius; there is no orbit to describe";
    case OrbitError::NonPositiveGravity:
        return "gravitational parameter must be positive";
    case OrbitError::SolverDidNotConverge:
        return "Kepler solver reached its iteration limit without converging";
    }
    return "unknown orbit error";
}

// --- conversions -----------------------------------------------------------

// `mu` is the standard gravitational parameter GM of the central body.
[[nodiscard]] std::expected<Elements, OrbitError> elementsFromState(const StateVector& sv,
                                                                    GravParam mu);
[[nodiscard]] StateVector stateFromElements(const Elements& el, GravParam mu);
[[nodiscard]] OrbitInfo orbitInfo(const Elements& el, GravParam mu);

// --- anomaly conversions ---------------------------------------------------

// Elliptic orbits use eccentric anomaly E, hyperbolic orbits use H; each pair
// is selected on `ecc` so callers can work in mean anomaly regardless of conic.
[[nodiscard]] Radians trueToEccentricAnomaly(Radians trueAnomaly, Eccentricity ecc);
[[nodiscard]] Radians eccentricToTrueAnomaly(Radians eccAnomaly, Eccentricity ecc);
[[nodiscard]] Radians eccentricToMeanAnomaly(Radians eccAnomaly, Eccentricity ecc);

// Solves Kepler's equation for the eccentric (or hyperbolic) anomaly.
//
// Newton-Raphson with a conic-appropriate starting guess; converges to ~1e-13
// in a handful of iterations even at ecc = 0.999. The iteration is bounded
// (JPL Power of Ten, rule 2) *and* reports failure when it runs out of steps
// (rule 5) -- a silent wrong answer here becomes a spacecraft in the wrong
// place twenty minutes later, for no visible reason.
[[nodiscard]] std::expected<Radians, OrbitError> meanToEccentricAnomaly(Radians meanAnomaly,
                                                                        Eccentricity ecc);

// --- propagation -----------------------------------------------------------

// Advance a state vector by `dt` along its Kepler orbit. Exact for the two-body
// problem at any dt, forward or backward.
//
// Preconditions, reported rather than asserted because a scenario file can
// produce both: the state must have non-zero radius, and mu must be positive.
[[nodiscard]] std::expected<StateVector, OrbitError>
propagate(const StateVector& sv, GravParam mu, Seconds dt);

// Advance only the anomaly of an element set, leaving the orbit shape intact.
[[nodiscard]] std::expected<Elements, OrbitError>
propagateElements(const Elements& el, GravParam mu, Seconds dt);

} // namespace orb
