#include "orbit/Orbit.hpp"

#include "core/Contract.hpp"

#include <algorithm>
#include <limits>

namespace orb {
namespace {

constexpr f64 kInf = std::numeric_limits<f64>::infinity();

// An orbit is treated as circular / equatorial below these thresholds, at which
// point the periapsis direction / ascending node stops being meaningful.
constexpr f64 kCircularTol = 1e-9;
constexpr f64 kEquatorialTol = 1e-9;
constexpr f64 kParabolicTol = 1e-9;

// Newton doubles its correct digits per step, so from the starting guesses used
// below these converge in well under twenty iterations for every eccentricity
// the sim can produce. The caps exist to make the bound provable (JPL Power of
// Ten, rule 2), not because they are expected to be reached.
constexpr int kMaxAnomalyIterations = 100;
constexpr int kMaxUniversalIterations = 200;

// One ulp of a double near pi is about 4.4e-16; two orders above that converges
// without the iteration chasing rounding noise. The universal-variable
// iteration works in units of sqrt(metres), so its tolerance is looser.
constexpr f64 kAnomalyTolerance = 1e-14;
constexpr f64 kUniversalTolerance = 1e-10;

constexpr f64 clampUnit(f64 v) { return std::clamp(v, -1.0, 1.0); }
constexpr f64 sign(f64 v) { return v < 0.0 ? -1.0 : 1.0; }

// Stumpff functions C(psi) and S(psi), the series that make the universal
// variable formulation conic-agnostic. Near psi = 0 the closed forms evaluate
// to 0/0, so a truncated series takes over.
void stumpff(f64 psi, f64& c2, f64& c3) {
    if (psi > 1e-6) {
        const f64 s = std::sqrt(psi);
        c2 = (1.0 - std::cos(s)) / psi;
        c3 = (s - std::sin(s)) / (psi * s);
    } else if (psi < -1e-6) {
        const f64 s = std::sqrt(-psi);
        c2 = (1.0 - std::cosh(s)) / psi;
        c3 = (std::sinh(s) - s) / (s * s * s);
    } else {
        c2 = 0.5 - (psi / 24.0) + (psi * psi / 720.0);
        c3 = (1.0 / 6.0) - (psi / 120.0) + (psi * psi / 5040.0);
    }
}

// The universal-variable iteration needs a starting guess, and the right guess
// depends on the conic. Extracted so that propagate() reads as a sequence of
// decisions rather than three unrelated formulae inlined mid-function (F.2: a
// function does one thing).
[[nodiscard]] f64
initialUniversalAnomaly(const StateVector& sv, f64 mu, f64 r0, f64 rdotv, f64 alpha, f64 seconds) {
    const f64 sqrtMu = std::sqrt(mu);

    if (alpha > 1e-12) { // ellipse
        return sqrtMu * seconds * alpha;
    }

    if (alpha < -1e-12) {          // hyperbola
        const f64 a = 1.0 / alpha; // negative
        const f64 denom = rdotv + (sign(seconds) * std::sqrt(-mu * a) * (1.0 - (r0 * alpha)));
        return sign(seconds) * std::sqrt(-a) * std::log(-2.0 * mu * alpha * seconds / denom);
    }

    // Parabola: Barker's equation, solved through the cubic substitution.
    const f64 hmag = length(cross(sv.pos, sv.vel));
    const f64 p = hmag * hmag / mu;
    const f64 s = 0.5 * std::atan(1.0 / (3.0 * std::sqrt(mu / (p * p * p)) * seconds));
    const f64 w = std::atan(std::cbrt(std::tan(s)));
    return std::sqrt(p) * 2.0 / std::tan(2.0 * w);
}

} // namespace

// --- state <-> elements ----------------------------------------------------

std::expected<Elements, OrbitError> elementsFromState(const StateVector& sv, GravParam mu) {
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    const Vec3& r = sv.pos;
    const Vec3& v = sv.vel;

    const f64 rmag = length(r);
    // Reported rather than asserted: a scenario file can put a vessel at the
    // exact centre of a planet, and that is something to explain to the user
    // rather than a bug in this file.
    if (!(rmag > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 m = mu.value;
    const f64 vmag = length(v);
    const f64 rdotv = dot(r, v);

    const Vec3 h = cross(r, v); // specific angular momentum
    const f64 hmag = length(h);
    const Vec3 node = cross(Vec3{0, 0, 1}, h); // points at the ascending node
    const f64 nmag = length(node);

    // The eccentricity vector points from the focus toward periapsis.
    const Vec3 evec = (r * ((vmag * vmag) - (m / rmag)) - v * rdotv) / m;

    Elements el;
    el.ecc = Eccentricity{length(evec)};
    el.slr = Metres{hmag * hmag / m};
    el.inc = Radians{std::acos(clampUnit(h.z / hmag))};

    const f64 energy = (vmag * vmag * 0.5) - (m / rmag);
    el.sma = Metres{(std::abs(el.ecc.value - 1.0) > kParabolicTol) ? -m / (2.0 * energy) : kInf};

    const bool circular = el.ecc.value < kCircularTol;
    const bool equatorial = nmag < kEquatorialTol * hmag;

    // Reference direction for angles measured in the orbital plane: the
    // ascending node where it exists, otherwise the x-axis.
    el.lan = equatorial ? Radians{0.0} : wrapTau(Radians{std::atan2(node.y, node.x)});
    const Vec3 ref = equatorial ? Vec3{1, 0, 0} : node;

    if (circular) {
        // No periapsis to point at, so angles run from the reference direction
        // straight to the spacecraft: argument of latitude, or true longitude.
        el.aop = Radians{0.0};
        f64 u = angleBetween(ref, r).value;
        if (dot(cross(ref, r), h) < 0.0) u = kTau - u; // resolve the half-turn
        el.tra = wrapTau(Radians{u});
    } else {
        f64 aop = angleBetween(ref, evec).value;
        if (dot(cross(ref, evec), h) < 0.0) aop = kTau - aop;
        el.aop = wrapTau(Radians{aop});

        f64 tra = angleBetween(evec, r).value;
        if (rdotv < 0.0) tra = kTau - tra; // inbound half of the orbit
        el.tra = wrapTau(Radians{tra});
    }

    return el;
}

StateVector stateFromElements(const Elements& el, GravParam mu) {
    // Asserted, not reported: every caller of this obtains its elements from
    // elementsFromState or builds them literally, so a non-positive mu here is
    // a bug in the caller rather than user input.
    ORBSIM_EXPECTS(mu.value > 0.0);

    // Prefer the stored semi-latus rectum: it is the one shape parameter that
    // stays finite on a parabolic orbit.
    const f64 e = el.ecc.value;
    const f64 p = (el.slr.value > 0.0) ? el.slr.value : el.sma.value * (1.0 - (e * e));
    ORBSIM_EXPECTS(p > 0.0);

    const f64 cosNu = std::cos(el.tra.value);
    const f64 sinNu = std::sin(el.tra.value);
    const f64 rmag = p / (1.0 + (e * cosNu));
    const f64 k = std::sqrt(mu.value / p);

    // Perifocal frame: x toward periapsis, z along angular momentum.
    const Vec3 rPerifocal{rmag * cosNu, rmag * sinNu, 0.0};
    const Vec3 vPerifocal{-k * sinNu, k * (e + cosNu), 0.0};

    // Perifocal -> inertial: Rz(lan) * Rx(inc) * Rz(aop), applied right to left.
    const Quat rot = Quat::fromAxisAngle({0, 0, 1}, el.lan) *
                     Quat::fromAxisAngle({1, 0, 0}, el.inc) *
                     Quat::fromAxisAngle({0, 0, 1}, el.aop);

    return {.pos = rot.rotate(rPerifocal), .vel = rot.rotate(vPerifocal)};
}

OrbitInfo orbitInfo(const Elements& el, GravParam mu) {
    ORBSIM_EXPECTS(mu.value > 0.0);

    const f64 e = el.ecc.value;
    const f64 m = mu.value;

    OrbitInfo info;
    info.closed = e < 1.0 - kParabolicTol;

    info.periapsis = Metres{el.slr.value / (1.0 + e)};
    info.apoapsis = Metres{info.closed ? el.slr.value / (1.0 - e) : kInf};
    info.radius = Metres{el.slr.value / (1.0 + (e * std::cos(el.tra.value)))};

    if (info.closed) {
        const f64 a = el.sma.value;
        info.meanMotion = RadiansPerSecond{std::sqrt(m / (a * a * a))};
        info.period = Seconds{kTau / info.meanMotion.value};
        info.energy = SpecificEnergy{-m / (2.0 * a)};
        // Vis-viva. Clamped at zero because rounding can push the radicand a
        // hair negative at the apoapsis of a near-circular orbit.
        info.speed =
            MetresPerSecond{std::sqrt(std::max(0.0, m * ((2.0 / info.radius.value) - (1.0 / a))))};
    } else {
        info.meanMotion = RadiansPerSecond{0.0};
        info.period = Seconds{kInf};
        info.energy = SpecificEnergy{std::isinf(el.sma.value) ? 0.0 : -m / (2.0 * el.sma.value)};
        info.speed = MetresPerSecond{
            std::sqrt(std::max(0.0, 2.0 * (info.energy.value + (m / info.radius.value))))};
    }
    return info;
}

// --- anomaly conversions ---------------------------------------------------

Radians trueToEccentricAnomaly(Radians trueAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    if (e < 1.0) {
        return Radians{2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(trueAnomaly.value * 0.5),
                                        std::sqrt(1.0 + e) * std::cos(trueAnomaly.value * 0.5))};
    }
    // Hyperbolic: build sinh(H) directly instead of going through tan(nu/2),
    // which is unbounded as the true anomaly approaches the asymptote.
    const f64 nu = wrapPi(trueAnomaly).value;
    const f64 sinhH = std::sqrt((e * e) - 1.0) * std::sin(nu) / (1.0 + (e * std::cos(nu)));
    return Radians{std::asinh(sinhH)};
}

Radians eccentricToTrueAnomaly(Radians eccAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    if (e < 1.0) {
        return wrapTau(
            Radians{2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(eccAnomaly.value * 0.5),
                                     std::sqrt(1.0 - e) * std::cos(eccAnomaly.value * 0.5))});
    }
    return Radians{2.0 * std::atan2(std::sqrt(e + 1.0) * std::sinh(eccAnomaly.value * 0.5),
                                    std::sqrt(e - 1.0) * std::cosh(eccAnomaly.value * 0.5))};
}

Radians eccentricToMeanAnomaly(Radians eccAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    if (e < 1.0) return Radians{eccAnomaly.value - (e * std::sin(eccAnomaly.value))};
    return Radians{(e * std::sinh(eccAnomaly.value)) - eccAnomaly.value};
}

std::expected<Radians, OrbitError> meanToEccentricAnomaly(Radians meanAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    ORBSIM_EXPECTS(e >= 0.0);

    if (e < 1.0) {
        // Solving M = E - e*sin(E) for E by Newton-Raphson.
        const f64 mean = wrapPi(meanAnomaly).value;

        // A near-parabolic orbit spends nearly all of its mean anomaly close to
        // periapsis, so the mean anomaly is a poor starting guess there; pi
        // keeps Newton inside the convergent basin.
        f64 eccentric = (e < 0.8) ? mean : sign(mean) * kPi;

        for (int i = 0; i < kMaxAnomalyIterations; ++i) {
            const f64 step =
                -(eccentric - (e * std::sin(eccentric)) - mean) / (1.0 - (e * std::cos(eccentric)));
            eccentric += step;
            if (std::abs(step) < kAnomalyTolerance) return Radians{eccentric};
        }
        // The bound was the easy half. This is the half that stops a wrong
        // number leaving the function wearing the same face as a right one.
        return std::unexpected(OrbitError::SolverDidNotConverge);
    }

    // Hyperbolic: M = e*sinh(H) - H. The log form below is that relation's
    // asymptotic inverse, which is what keeps the guess sane far from
    // periapsis.
    const f64 mean = meanAnomaly.value;
    f64 hyperbolic = (std::abs(mean) > 6.0)
                         ? sign(mean) * std::log((2.0 * std::abs(mean) / e) + 1.8)
                         : mean / (e - 1.0);

    for (int i = 0; i < kMaxAnomalyIterations; ++i) {
        const f64 step = -((e * std::sinh(hyperbolic)) - hyperbolic - mean) /
                         ((e * std::cosh(hyperbolic)) - 1.0);
        hyperbolic += step;
        if (std::abs(step) < kAnomalyTolerance) return Radians{hyperbolic};
    }
    return std::unexpected(OrbitError::SolverDidNotConverge);
}

// --- propagation -----------------------------------------------------------

std::expected<StateVector, OrbitError> propagate(const StateVector& sv, GravParam mu, Seconds dt) {
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    const f64 r0 = length(sv.pos);
    // This used to silently return the input, which turned a loud, findable
    // error into a spacecraft that mysteriously stops moving three hours into a
    // flight. A zero radius is a bug in whatever produced the state -- but the
    // caller is the one holding the context to say so.
    if (!(r0 > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 m = mu.value;
    const f64 v0 = length(sv.vel);
    const f64 rdotv = dot(sv.pos, sv.vel);
    const f64 sqrtMu = std::sqrt(m);
    const f64 alpha = (2.0 / r0) - (v0 * v0 / m); // reciprocal of the semi-major axis

    f64 seconds = dt.value;

    // Whole revolutions of a closed orbit are a no-op. Folding them away keeps
    // the universal anomaly small, which is what keeps Newton convergent when
    // the sim runs at 100000x and a single dt spans months.
    if (alpha > 1e-12) {
        const f64 period = kTau / (sqrtMu * alpha * std::sqrt(alpha));
        seconds = std::fmod(seconds, period);
    }

    f64 chi = initialUniversalAnomaly(sv, m, r0, rdotv, alpha, seconds);

    // psi, c2 and c3 outlive the loop: the Lagrange coefficients below use their
    // values from the final iteration.
    f64 psi = 0.0;
    f64 c2 = 0.5;
    f64 c3 = 1.0 / 6.0;
    bool converged = false;

    for (int i = 0; i < kMaxUniversalIterations; ++i) {
        psi = chi * chi * alpha;
        stumpff(psi, c2, c3);

        const f64 r = (chi * chi * c2) + ((rdotv / sqrtMu) * chi * (1.0 - (psi * c3))) +
                      (r0 * (1.0 - (psi * c2)));

        const f64 dchi = ((sqrtMu * seconds) - (chi * chi * chi * c3) -
                          ((rdotv / sqrtMu) * chi * chi * c2) - (r0 * chi * (1.0 - (psi * c3)))) /
                         r;
        chi += dchi;
        if (std::abs(dchi) < kUniversalTolerance) {
            converged = true;
            break;
        }
    }

    if (!converged) return std::unexpected(OrbitError::SolverDidNotConverge);

    // Lagrange coefficients: the new state is a linear combination of the old
    // position and velocity vectors.
    const f64 f = 1.0 - ((chi * chi / r0) * c2);
    const f64 g = seconds - ((chi * chi * chi / sqrtMu) * c3);
    const Vec3 rNew = sv.pos * f + sv.vel * g;

    const f64 rMag = length(rNew);
    ORBSIM_ENSURES(rMag > 0.0);

    const f64 gdot = 1.0 - ((chi * chi / rMag) * c2);
    const f64 fdot = (sqrtMu / (rMag * r0)) * chi * ((psi * c3) - 1.0);
    const Vec3 vNew = sv.pos * fdot + sv.vel * gdot;

    return StateVector{.pos = rNew, .vel = vNew};
}

std::expected<Elements, OrbitError>
propagateElements(const Elements& el, GravParam mu, Seconds dt) {
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    const Radians eccentricAtStart = trueToEccentricAnomaly(el.tra, el.ecc);
    const Radians meanAtStart = eccentricToMeanAnomaly(eccentricAtStart, el.ecc);

    const f64 a = std::abs(el.sma.value);
    const f64 meanMotion = std::sqrt(mu.value / (a * a * a));

    const std::expected<Radians, OrbitError> eccentricAtEnd =
        meanToEccentricAnomaly(Radians{meanAtStart.value + (meanMotion * dt.value)}, el.ecc);
    if (!eccentricAtEnd) return std::unexpected(eccentricAtEnd.error());

    Elements out = el;
    out.tra = eccentricToTrueAnomaly(*eccentricAtEnd, el.ecc);
    return out;
}

} // namespace orb
