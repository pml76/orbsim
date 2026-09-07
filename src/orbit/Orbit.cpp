#include "orbit/Orbit.hpp"

#include "core/Contract.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orb {
namespace {

constexpr f64 kInf = std::numeric_limits<f64>::infinity();

[[nodiscard]] bool isFinite(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] bool isFinite(const StateVector& sv) noexcept {
    return isFinite(sv.pos) && isFinite(sv.vel);
}

// An orbit is treated as circular / equatorial below these thresholds, at which
// point the periapsis direction / ascending node stops being meaningful.
//
// Where 1e-9 comes from, since a bare number here is exactly what section 12
// warns about. All three are dimensionless by construction -- an eccentricity,
// a ratio |n|/|h|, and a distance from e = 1 -- so unlike the tolerances below
// they carry no hidden length scale. The value is the point at which the angle
// these quantities determine stops being computable to useful precision: the
// periapsis direction of an orbit with e = 1e-9 is set by the ninth
// significant digit of the eccentricity vector, and f64 subtraction in
// `evec` leaves roughly seven behind it. Below that the canonical
// parameterisation (aop = 0, tra = argument of latitude) is not an
// approximation -- it is the only answer that is stable.
constexpr f64 kCircularTol = 1e-9;
constexpr f64 kEquatorialTol = 1e-9;
constexpr f64 kParabolicTol = 1e-9;

// Below this ratio of |h| to |r||v| -- the sine of the angle between position
// and velocity -- the trajectory is radial and has no orbital plane at all.
// Dimensionless, like the three above. See the use site for why 1e-12 cannot
// clip a real orbit.
constexpr f64 kRectilinearTol = 1e-12;

// Newton doubles its correct digits per step, so from the starting guesses used
// below these converge in well under twenty iterations for every eccentricity
// the sim can produce. The caps exist to make the bound provable (JPL Power of
// Ten, rule 2), not because they are expected to be reached.
constexpr int kMaxAnomalyIterations = 100;
constexpr int kMaxUniversalIterations = 200;

// One ulp of a double near pi is about 4.4e-16; two orders above that converges
// without the iteration chasing rounding noise.
constexpr f64 kAnomalyTolerance = 1e-14;

// The universal anomaly chi has units of sqrt(metres), and one revolution is
// 2*pi*sqrt(a): about 1.7e4 in low Earth orbit and 2.4e6 at 1 AU. This used to
// be an absolute 1e-10, which at 1 AU is below the rounding noise of the
// update itself, so Newton ran to its cap and reported non-convergence for a
// perfectly ordinary heliocentric orbit. It is relative to |chi| now, with
// sqrt(r0) as the floor so that dt = 0 (chi = 0) still converges. 1e-13 is
// the same two-orders-above-ulp margin the anomaly solver uses.
constexpr f64 kUniversalRelTolerance = 1e-13;

// alpha = 1/a has units of 1/metres, so a threshold on alpha alone encodes a
// length: the old 1e-12 declared every orbit wider than 1e12 m parabolic, and
// Jupiter is 7.8e11 m from the Sun. alpha * r0 is dimensionless -- 1 on a
// circle, 1 - e at periapsis, 1 + e at apoapsis, 0 on a parabola -- and
// separates the conics at every scale.
constexpr f64 kParabolicAlphaTol = 1e-12;

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
    // A zero-length step has zero universal anomaly on every conic, and saying
    // so before the conic dispatch is a correctness fix rather than a shortcut.
    // The hyperbolic guess below takes the log of a quantity proportional to
    // `seconds`; log(0) is -infinity, which reaches the Stumpff series as NaN
    // and spends the entire iteration budget there before reporting that it did
    // not converge. The elliptic guess is a product and returned zero correctly,
    // and the parabolic one survived by accident through atan(1/0) = pi/2, which
    // is why only hyperbolic trajectories ever failed -- including at dt = 0
    // after a whole revolution was folded out of a closed orbit.
    //
    // `== 0.0` is deliberate, and is the exception CODING_GUIDELINES section 11
    // allows: the claim is exactly zero, not "small". A tolerance here would
    // answer a different question and would make a very short step wrong.
    if (seconds == 0.0) return 0.0;

    const f64 sqrtMu = std::sqrt(mu);

    if (alpha * r0 > kParabolicAlphaTol) { // ellipse
        return sqrtMu * seconds * alpha;
    }

    if (alpha * r0 < -kParabolicAlphaTol) { // hyperbola
        const f64 a = 1.0 / alpha;          // negative
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

// Everything the Lagrange coefficients need from the universal-anomaly solve.
// A struct rather than four out-parameters, and rather than four adjacent f64
// returns that transpose in silence (I.24).
struct UniversalSolution {
    f64 chi{};
    f64 psi{};
    f64 c2{0.5};
    f64 c3{1.0 / 6.0};
};

// Newton on the universal Kepler equation. Split out of propagate() so that
// each function does one thing (F.2) and neither exceeds the size limit: this
// one finds chi, and propagate() decides what the answer means.
//
// The derivative of the universal Kepler equation with respect to chi is the
// radius at chi, which is why `r` below is both the physical radius and the
// denominator of the Newton step.
[[nodiscard]] std::expected<UniversalSolution, OrbitError>
solveUniversalAnomaly(const StateVector& sv, f64 mu, f64 r0, f64 rdotv, f64 alpha, f64 seconds) {
    const f64 sqrtMu = std::sqrt(mu);

    UniversalSolution out;
    out.chi = initialUniversalAnomaly(sv, mu, r0, rdotv, alpha, seconds);

    for (int i = 0; i < kMaxUniversalIterations; ++i) {
        out.psi = out.chi * out.chi * alpha;
        stumpff(out.psi, out.c2, out.c3);

        const f64 r = (out.chi * out.chi * out.c2) +
                      ((rdotv / sqrtMu) * out.chi * (1.0 - (out.psi * out.c3))) +
                      (r0 * (1.0 - (out.psi * out.c2)));

        const f64 dchi = ((sqrtMu * seconds) - (out.chi * out.chi * out.chi * out.c3) -
                          ((rdotv / sqrtMu) * out.chi * out.chi * out.c2) -
                          (r0 * out.chi * (1.0 - (out.psi * out.c3)))) /
                         r;
        out.chi += dchi;
        if (std::abs(dchi) <= kUniversalRelTolerance * std::max(std::abs(out.chi), std::sqrt(r0))) {
            return out;
        }
    }

    // The bound was the easy half. This is the half that stops a wrong number
    // leaving the function wearing the same face as a right one.
    return std::unexpected(OrbitError::SolverDidNotConverge);
}

// The four vectors the in-plane angles are measured from. Grouped into a struct
// rather than passed as four adjacent Vec3 parameters, which would transpose in
// silence (I.24, and `bugprone-easily-swappable-parameters` would say so).
struct OrbitFrame {
    Vec3 r;    // position
    Vec3 v;    // velocity
    Vec3 h;    // specific angular momentum
    Vec3 node; // toward the ascending node; zero for an equatorial orbit
    Vec3 evec; // toward periapsis; zero for a circular orbit
};

// Fills in lan, aop and tra, which is where the degenerate cases live: a
// circular orbit has no periapsis to measure from, and an equatorial one has no
// ascending node. Split out of elementsFromState so each does one thing (F.2)
// and neither exceeds the size limit.
// The magnitudes are derived here rather than passed in: three adjacent f64
// parameters transpose in silence, which is the defect
// `bugprone-easily-swappable-parameters` exists to catch. Recomputing two
// lengths once per conversion is not a cost anything can measure.
void assignInPlaneAngles(Elements& el, const OrbitFrame& frame) {
    const f64 nmag = length(frame.node);
    const f64 hmag = length(frame.h);
    const f64 rdotv = dot(frame.r, frame.v);

    const bool circular = el.ecc.value < kCircularTol;
    const bool equatorial = nmag < kEquatorialTol * hmag;

    // Reference direction for angles measured in the orbital plane: the
    // ascending node where it exists, otherwise the x-axis.
    el.lan = equatorial ? Radians{0.0} : wrapTau(Radians{std::atan2(frame.node.y, frame.node.x)});
    const Vec3 ref = equatorial ? Vec3{1, 0, 0} : frame.node;

    if (circular) {
        // No periapsis to point at, so angles run from the reference direction
        // straight to the spacecraft: argument of latitude, or true longitude.
        el.aop = Radians{0.0};
        f64 u = angleBetween(ref, frame.r).value;
        if (dot(cross(ref, frame.r), frame.h) < 0.0) u = kTau - u; // resolve the half-turn
        el.tra = wrapTau(Radians{u});
        return;
    }

    f64 aop = angleBetween(ref, frame.evec).value;
    if (dot(cross(ref, frame.evec), frame.h) < 0.0) aop = kTau - aop;
    el.aop = wrapTau(Radians{aop});

    f64 tra = angleBetween(frame.evec, frame.r).value;
    if (rdotv < 0.0) tra = kTau - tra; // inbound half of the orbit
    el.tra = wrapTau(Radians{tra});
}

// The postcondition Orbit.hpp promises: "degenerate orbits get a canonical
// parameterisation rather than NaN". Two specific ways of breaking it are
// refused by name upstream -- a magnitude that overflows, and a radial
// trajectory -- but neither covers the general case, which a fuzzer found:
// with mu tiny relative to the state, the division by `m` in the eccentricity
// vector overflows and `ecc` comes back infinite with the in-plane angles NaN.
//
// Guarding the inputs instead would mean inventing a smallest believable mu,
// and this codebase is deliberately scale-free -- the whole lesson of
// test_orbit_scales.cpp is that a threshold carrying a hidden scale is a bug
// waiting for a bigger orbit. Checking the answer costs seven comparisons and
// carries no scale at all.
//
// `sma` is allowed to be infinite, and only there: a parabolic orbit has no
// finite semi-major axis, which is why `slr` is stored beside it.
[[nodiscard]] bool elementsAreUsable(const Elements& el) noexcept {
    if (std::isnan(el.sma.value)) return false;
    return std::isfinite(el.ecc.value) && std::isfinite(el.slr.value) &&
           std::isfinite(el.inc.value) && std::isfinite(el.lan.value) &&
           std::isfinite(el.aop.value) && std::isfinite(el.tra.value);
}

} // namespace

// --- state <-> elements ----------------------------------------------------

std::expected<Elements, OrbitError> elementsFromState(const StateVector& sv, GravParam mu) {
    if (!isFinite(sv) || !std::isfinite(mu.value)) return std::unexpected(OrbitError::NotFinite);
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

    // Finite components do not imply a finite magnitude. lengthSq squares each
    // component, so anything above about 1.3e154 overflows to infinity and
    // every isfinite() check on the inputs still passes -- and `rmag > 0.0` is
    // true of infinity, so the test above waves it through. A fuzzer found
    // this: the elements came back claiming success with an infinite
    // eccentricity and a NaN argument of periapsis, which is precisely the
    // "succeeded, and the answer is NaN" outcome the type system cannot
    // prevent and the caller has no way to detect.
    if (!std::isfinite(rmag) || !std::isfinite(vmag)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    const f64 rdotv = dot(r, v);

    const Vec3 h = cross(r, v); // specific angular momentum
    const f64 hmag = length(h);

    // A radial trajectory has no orbital plane, and `inc` below would be
    // acos(h.z / hmag) = acos(0/0) = NaN returned as a success. The test is
    // dimensionless on purpose (section 12, and the lesson of this file):
    // hmag / (rmag * vmag) is |sin| of the angle between position and
    // velocity, so the threshold compares like with like at every scale.
    //
    // 1e-12 does not clip real orbits. That ratio is the cosine of the flight
    // path angle, and reaching 1e-12 needs an eccentricity within about 1e-24
    // of 1 -- indistinguishable from a straight line in f64. The e = 0.9999
    // case in the suite sits around 1e-2.
    if (!(hmag > kRectilinearTol * rmag * vmag)) {
        return std::unexpected(OrbitError::RectilinearOrbit);
    }

    const Vec3 node = cross(Vec3{0, 0, 1}, h); // points at the ascending node

    // The eccentricity vector points from the focus toward periapsis.
    const Vec3 evec = (r * ((vmag * vmag) - (m / rmag)) - v * rdotv) / m;

    Elements el;
    el.ecc = Eccentricity{length(evec)};
    el.slr = Metres{hmag * hmag / m};
    el.inc = Radians{std::acos(clampUnit(h.z / hmag))};

    // Every conic has a positive semi-latus rectum, so a zero here is not an
    // orbit shape -- it is `hmag * hmag` underflowing, which the ratio test
    // above cannot see because it never squares anything. The consequence
    // downstream is that orbitInfo's radius becomes slr / (1 + e cos v) =
    // 0/0 at the asymptote of the parabola this then looks like. Same
    // conclusion as the ratio test, reached by a different route, so it is
    // reported as the same thing: too little angular momentum to be an orbit.
    if (!(el.slr.value > 0.0)) return std::unexpected(OrbitError::RectilinearOrbit);

    const f64 energy = (vmag * vmag * 0.5) - (m / rmag);
    el.sma = Metres{(std::abs(el.ecc.value - 1.0) > kParabolicTol) ? -m / (2.0 * energy) : kInf};

    assignInPlaneAngles(el, {.r = r, .v = v, .h = h, .node = node, .evec = evec});

    if (!elementsAreUsable(el)) return std::unexpected(OrbitError::NotFinite);
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
    // Checked first and by name. NaN passes every comparison below unnoticed
    // and then comes out of Newton as "did not converge", which is true but
    // sends whoever reads the log looking at the solver instead of the file.
    if (!isFinite(sv) || !std::isfinite(mu.value) || !std::isfinite(dt.value)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    const f64 r0 = length(sv.pos);
    // This used to silently return the input, which turned a loud, findable
    // error into a spacecraft that mysteriously stops moving three hours into a
    // flight. A zero radius is a bug in whatever produced the state -- but the
    // caller is the one holding the context to say so.
    if (!(r0 > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 m = mu.value;
    const f64 v0 = length(sv.vel);

    // Same overflow as in elementsFromState: a component above about 1.3e154
    // squares to infinity inside lengthSq, and infinity passes the `r0 > 0.0`
    // test above. Without this the Lagrange coefficients below are computed
    // from infinities and the propagated state is NaN.
    if (!std::isfinite(r0) || !std::isfinite(v0)) {
        return std::unexpected(OrbitError::NotFinite);
    }

    const f64 rdotv = dot(sv.pos, sv.vel);
    const f64 sqrtMu = std::sqrt(m);
    const f64 alpha = (2.0 / r0) - (v0 * v0 / m); // reciprocal of the semi-major axis

    f64 seconds = dt.value;

    // Whole revolutions of a closed orbit are a no-op. Folding them away keeps
    // the universal anomaly small, which is what keeps Newton convergent when
    // the sim runs at 100000x and a single dt spans months.
    if (alpha * r0 > kParabolicAlphaTol) {
        const f64 period = kTau / (sqrtMu * alpha * std::sqrt(alpha));
        seconds = std::fmod(seconds, period);
    }

    const auto solved = solveUniversalAnomaly(sv, m, r0, rdotv, alpha, seconds);
    if (!solved) return std::unexpected(solved.error());

    const f64 chi = solved->chi;
    const f64 psi = solved->psi;
    const f64 c2 = solved->c2;
    const f64 c3 = solved->c3;

    // Lagrange coefficients: the new state is a linear combination of the old
    // position and velocity vectors.
    const f64 f = 1.0 - ((chi * chi / r0) * c2);
    const f64 g = seconds - ((chi * chi * chi / sqrtMu) * c3);
    const Vec3 rNew = sv.pos * f + sv.vel * g;

    const f64 rMag = length(rNew);

    // This was ORBSIM_ENSURES(rMag > 0.0), and that was the wrong half of the
    // ADR 0002 split. A caller can reach it: with an enormous speed and a
    // gravitational parameter to match, the Lagrange combination overflows or
    // cancels onto the origin, and a Debug build then aborted the process on
    // user input. A fuzzer found it. The rule is that a condition a caller can
    // produce is reported, and only what a bug in this file could produce is
    // asserted -- so this is reported, by name, for each way it can fail.
    if (!std::isfinite(rMag)) return std::unexpected(OrbitError::NotFinite);
    if (!(rMag > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 gdot = 1.0 - ((chi * chi / rMag) * c2);
    const f64 fdot = (sqrtMu / (rMag * r0)) * chi * ((psi * c3) - 1.0);
    const Vec3 vNew = sv.pos * fdot + sv.vel * gdot;

    const StateVector out{.pos = rNew, .vel = vNew};
    // The same postcondition elementsFromState carries: a success must be a
    // usable number. The velocity can still overflow after the position has
    // survived, because fdot divides by rMag.
    if (!isFinite(out)) return std::unexpected(OrbitError::NotFinite);
    return out;
}

std::expected<Elements, OrbitError>
propagateElements(const Elements& el, GravParam mu, Seconds dt) {
    if (!std::isfinite(mu.value) || !std::isfinite(dt.value)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);
    // The mean motion below is sqrt(mu / a^3), and a parabola has no a. Saying
    // so beats feeding infinity to the Kepler solver and reporting that it
    // did not converge.
    if (!std::isfinite(el.sma.value) || std::abs(el.ecc.value - 1.0) <= kParabolicTol) {
        return std::unexpected(OrbitError::ParabolicElements);
    }

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
