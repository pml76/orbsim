#ifndef ORBSIM_ORBIT_ORBIT_HPP
#define ORBSIM_ORBIT_ORBIT_HPP
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

#include <cstdint>
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
// Degenerate orbits get a canonical parameterisation rather than NaN *where
// one exists*: a circular orbit has no periapsis, so `aop` is zero and `tra`
// becomes the argument of latitude; an equatorial orbit has no ascending node,
// so `lan` is zero and in-plane angles are measured from the x-axis instead.
//
// Where no canonical answer exists, `elementsFromState` reports instead of
// inventing one. A radial trajectory has no orbital plane at all, so there is
// no inclination to fall back on -- that is `RectilinearOrbit`. A state whose
// derived quantities overflow is `NotFinite`. Both were found by fuzzing, and
// both used to come back as a success carrying NaN, which is the outcome this
// paragraph exists to rule out.
//
// Every member is a unit type, and Quantity gives each one a default member
// initializer of its own (core/Scalar.hpp), so `Elements{}` is fully
// zero-initialised without a `{}` written here. That is what section 6 asks
// for -- the guarantee, not the punctuation -- and it is why the types earn
// their keep beyond the units: a bare `f64 sma;` here would be an
// uninitialised read waiting to happen, and this cannot be.
struct Elements {
    // Semi-major axis: positive for an ellipse, negative for a hyperbola,
    // infinite for a parabola. elementsFromState lets the energy decide which,
    // not e -- near radial, e is 1 whatever the energy -- and keeps e on the
    // same side of 1.
    Metres sma;
    Eccentricity ecc; // dimensionless
    Radians inc;      // inclination, [0, pi]
    Radians lan;      // longitude of ascending node, [0, tau)
    Radians aop;      // argument of periapsis, [0, tau)
    Radians tra;      // true anomaly, [0, tau)
    Metres slr;       // semi-latus rectum. Kept explicitly so parabolic
                      // orbits (where sma is infinite) remain representable.
};

// Quantities derived from the elements that the HUD and MFDs ask for
// constantly. Computed together because they share intermediate terms.
// The unit types self-initialise, as in Elements above. `closed` is a bare
// bool and keeps its `{}`, because that one genuinely would be uninitialised
// without it -- which is the distinction the check is drawing.
struct OrbitInfo {
    Metres periapsis;            // radius at periapsis
    Metres apoapsis;             // radius at apoapsis. Infinity if not closed.
    Seconds period;              // orbital period. Infinity if not closed.
    RadiansPerSecond meanMotion; // zero if not closed
    SpecificEnergy energy;       // specific orbital energy
    Metres radius;               // current radius
    MetresPerSecond speed;       // current speed
    bool closed{};               // true for a bound orbit: a finite, positive sma
};

// --- errors ----------------------------------------------------------------

// Conditions a caller can legitimately produce. Conditions that can only arise
// from a bug in this file are asserted instead; see core/Contract.hpp.
//
// The explicit std::uint8_t base is interface, not optimisation: it makes the
// representation part of the declaration rather than a compiler default, and it
// is what lets the enum be forward-declared.
enum class OrbitError : std::uint8_t {
    // NaN or infinity, either in an input or in a quantity derived from one.
    // The second case is not obvious and a fuzzer found it: every component of
    // a state can be finite while |h|^2 or the eccentricity vector is not --
    // and while |r| was, before length() became safe at every scale. A corrupt
    // scenario, not an orbit.
    NotFinite,
    DegenerateState,    // zero radius: a vessel at the exact centre of a body
    NonPositiveGravity, // mu <= 0 is not a central body
    // There was a `ParabolicElements` here until 2026-09-12: propagateElements
    // refused an element set within 1e-9 of e = 1, or one with no finite
    // semi-major axis, because the classical Kepler equation could not be
    // trusted there. It now runs the same universal-variable solve the state
    // propagator does, which has no such regime, so nothing produces that error
    // and an enumerator no code can return is a lie in a header.
    //
    // Velocity parallel to position, so the specific angular momentum is zero:
    // the trajectory is a straight line through the centre and has no orbital
    // plane, which means no inclination and no ascending node. Reachable in
    // ordinary flight -- a probe released with no horizontal velocity falls
    // straight down -- and reported rather than parameterised, because unlike a
    // circular or equatorial orbit there is no canonical answer to fall back on.
    RectilinearOrbit,
    SolverDidNotConverge, // Newton reached its iteration cap
};

[[nodiscard]] constexpr std::string_view describe(OrbitError error) noexcept {
    switch (error) {
    case OrbitError::NotFinite:
        return "input is not finite, or a magnitude derived from it overflowed";
    case OrbitError::DegenerateState:
        return "state vector has zero radius; there is no orbit to describe";
    case OrbitError::NonPositiveGravity:
        return "gravitational parameter must be positive";
    case OrbitError::RectilinearOrbit:
        return "velocity is parallel to position; a radial trajectory has no orbital plane";
    case OrbitError::SolverDidNotConverge:
        return "Kepler solver reached its iteration limit without converging";
    }
    return "unknown orbit error";
}

// --- conversions -----------------------------------------------------------

// `mu` is the standard gravitational parameter GM of the central body.
//
// Reports, rather than returning elements a caller cannot use: `NotFinite` for
// a non-finite input *or* a quantity derived from one that overflows -- every
// component can be finite while |h|^2 or the eccentricity vector is not;
// `NonPositiveGravity`; `DegenerateState` for a vessel at the
// exact centre of the body; and `RectilinearOrbit` for a radial trajectory,
// which has no orbital plane and therefore no inclination. On success every
// element is a usable number, which is checked before returning.
[[nodiscard]] std::expected<Elements, OrbitError> elementsFromState(const StateVector& sv,
                                                                    GravParam mu);
[[nodiscard]] StateVector stateFromElements(const Elements& el, GravParam mu);

// The derived quantities, and what they are worth.
//
// `radius` and `speed` are read back out of p, e and the true anomaly, so they
// are worth what those are worth. The anomaly is a double, and near the radial
// limit r and v depend on it steeply, which puts a floor under any formulation:
// measured against 60-digit references over 110,004 states on three toolchains
// (2026-09-12), the relative error stays within
//
//     40 u (1 + kappa) (1 + |alpha r|),   u = 2^-53
//
// with kappa = |r.v| / |h| for the radius and |r.v| mu / (r v^2 |h|) for the
// speed -- the two log-derivatives with respect to the anomaly -- and
// alpha r = 2 - r v^2 / mu. That second factor is `elementsFromState`'s rather
// than this function's: far out on a hyperbola its eccentricity vector cancels,
// and the anomaly it stores loses about 2e-15 r/|a| rad with it.
// `tests/test_orbit_scales.cpp` asserts the law over a seeded sweep.
//
// Three regimes sit outside it, and no formulation reaches past them from these
// elements:
//   - inside the parabolic band, where the energy puts a state within 1e-12 of
//     a parabola, `sma` is infinite and what comes back is the parabola through
//     that state: up to |alpha r| / 2, and so at most 5e-13, away from the
//     truth;
//   - below e = 1e-9, where `tra` is the argument of latitude rather than the
//     true anomaly: up to 2e, which is 1.8e-9 or 12.6 mm at 7000 km;
//   - at an apsis of a nearly radial orbit, where the first-order term above is
//     zero and the second order is what is left: a probe drifting at 1 mm/s has
//     its speed 2.4e-5 out, because the double nearest pi is 1.2e-16 short of
//     it.
//
// **A caller holding the state vector should read the radius and the speed off
// it** -- |r| and |v| are exact there, and this function is for a caller that
// has only elements.
[[nodiscard]] OrbitInfo orbitInfo(const Elements& el, GravParam mu);

// --- anomaly conversions ---------------------------------------------------

// Elliptic orbits use eccentric anomaly E, hyperbolic orbits use H; each pair
// is selected on `ecc` so callers can work in mean anomaly regardless of conic.
[[nodiscard]] Radians trueToEccentricAnomaly(Radians trueAnomaly, Eccentricity ecc);
[[nodiscard]] Radians eccentricToTrueAnomaly(Radians eccAnomaly, Eccentricity ecc);
[[nodiscard]] Radians eccentricToMeanAnomaly(Radians eccAnomaly, Eccentricity ecc);

// Solves Kepler's equation for the eccentric (or hyperbolic) anomaly.
//
// Safeguarded Newton, to the resolution of a double. The conic-appropriate
// starting guess is only a hint: both Kepler equations are strictly increasing
// (dM/dE = 1 - e cos E > 0, dM/dH = e cosh H - 1 > 0), so the root is unique
// and bracketed, and bisection takes over whenever Newton would leave the
// bracket or fail to halve its step.
//
// That matters at high eccentricity, where the slope collapses and a plain
// Newton oscillates: this was a plain Newton until 2026-09-08, and it failed
// on 196 of 401 hyperbolic anomalies at e = 1.0001 -- most near-parabolic
// escape trajectories. It now converges for every eccentricity measured, from
// 0 to 100, with a worst round-trip error of about 4e-15 radians.
//
// The iteration is still bounded (JPL Power of Ten, rule 2) and still reports
// failure (rule 5). The difference is that the report is now unreachable: a
// silent wrong answer here becomes a spacecraft in the wrong place twenty
// minutes later, and so does a refusal to answer at all.
[[nodiscard]] std::expected<Radians, OrbitError> meanToEccentricAnomaly(Radians meanAnomaly,
                                                                        Eccentricity ecc);

// --- propagation -----------------------------------------------------------

// Advance a state vector by `dt` along its Kepler orbit. Exact for the two-body
// problem at any dt, forward or backward, at any scale from a lunar orbit to
// the outer solar system -- the convergence criterion is relative, and the
// conic thresholds are dimensionless, so nothing here has a built-in size.
//
// The universal-variable solve is safeguarded: the equation it solves is
// strictly monotonic, so the root is bracketed and bisection takes over
// whenever Newton would leave the bracket or fail to halve its step. There is
// no eccentricity or step length for which this reports non-convergence.
//
// Reported rather than asserted, because a scenario file can produce all of
// them: `NotFinite` for a non-finite input, a magnitude that overflows, or a
// *result* that does; `NonPositiveGravity`; and `DegenerateState` for a
// zero-radius state, or for a result that lands on the centre.
[[nodiscard]] std::expected<StateVector, OrbitError>
propagate(const StateVector& sv, GravParam mu, Seconds dt);

// Advance only the anomaly of an element set, leaving the orbit shape intact.
//
// Every conic, with no band around e = 1 and no shape it declines: it runs the
// same universal-variable solve `propagate()` does, with 1/a taken from `sma`
// (zero when that is infinite, which is what a parabola is) rather than
// recovered from a state. Measured against 60-digit references over 40,024
// element sets (2026-09-12), worst relative position error: 2.1e-12 on ordinary
// orbits, 3.2e-12 on nearly parabolic ones, 7.0e-12 within 1e-9 of e = 1, and
// 1.0e-13 on the parabola itself. The classical Kepler route it replaced was up
// to 1.5% out near e = 1 and refused everything inside 1e-9 of it.
//
// Reports `NotFinite` for a non-finite mu, time step or element set, and
// `NonPositiveGravity` for mu <= 0. `slr` must be positive and finite: it is the
// one shape parameter every conic has, and the anomaly is read back through it.
[[nodiscard]] std::expected<Elements, OrbitError>
propagateElements(const Elements& el, GravParam mu, Seconds dt);

} // namespace orb

#endif // ORBSIM_ORBIT_ORBIT_HPP
