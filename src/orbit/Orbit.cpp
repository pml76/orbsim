#include "orbit/Orbit.hpp"

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
        c2 = 0.5 - psi / 24.0 + psi * psi / 720.0;
        c3 = 1.0 / 6.0 - psi / 120.0 + psi * psi / 5040.0;
    }
}

} // namespace

// --- state <-> elements ----------------------------------------------------

Elements elementsFromState(const StateVector& sv, f64 mu) {
    const Vec3& r = sv.pos;
    const Vec3& v = sv.vel;

    const f64 rmag = length(r);
    const f64 vmag = length(v);
    const f64 rdotv = dot(r, v);

    const Vec3 h = cross(r, v); // specific angular momentum
    const f64 hmag = length(h);
    const Vec3 node = cross(Vec3{0, 0, 1}, h); // points at the ascending node
    const f64 nmag = length(node);

    // The eccentricity vector points from the focus toward periapsis.
    const Vec3 evec = (r * (vmag * vmag - mu / rmag) - v * rdotv) / mu;

    Elements el;
    el.ecc = length(evec);
    el.slr = hmag * hmag / mu;
    el.inc = std::acos(clampUnit(h.z / hmag));

    const f64 energy = vmag * vmag * 0.5 - mu / rmag;
    el.sma = (std::abs(el.ecc - 1.0) > kParabolicTol) ? -mu / (2.0 * energy) : kInf;

    const bool circular = el.ecc < kCircularTol;
    const bool equatorial = nmag < kEquatorialTol * hmag;

    // Reference direction for angles measured in the orbital plane: the
    // ascending node where it exists, otherwise the x-axis.
    el.lan = equatorial ? 0.0 : wrapTau(std::atan2(node.y, node.x));
    const Vec3 ref = equatorial ? Vec3{1, 0, 0} : node;

    if (circular) {
        // No periapsis to point at, so angles run from the reference direction
        // straight to the spacecraft: argument of latitude, or true longitude.
        el.aop = 0.0;
        f64 u = angleBetween(ref, r);
        if (dot(cross(ref, r), h) < 0.0) u = kTau - u; // resolve the half-turn
        el.tra = wrapTau(u);
    } else {
        f64 aop = angleBetween(ref, evec);
        if (dot(cross(ref, evec), h) < 0.0) aop = kTau - aop;
        el.aop = wrapTau(aop);

        f64 tra = angleBetween(evec, r);
        if (rdotv < 0.0) tra = kTau - tra; // inbound half of the orbit
        el.tra = wrapTau(tra);
    }

    return el;
}

StateVector stateFromElements(const Elements& el, f64 mu) {
    // Prefer the stored semi-latus rectum: it is the one shape parameter that
    // stays finite on a parabolic orbit.
    const f64 p = (el.slr > 0.0) ? el.slr : el.sma * (1.0 - el.ecc * el.ecc);

    const f64 cosNu = std::cos(el.tra);
    const f64 sinNu = std::sin(el.tra);
    const f64 rmag = p / (1.0 + el.ecc * cosNu);
    const f64 k = std::sqrt(mu / p);

    // Perifocal frame: x toward periapsis, z along angular momentum.
    const Vec3 rPerifocal{rmag * cosNu, rmag * sinNu, 0.0};
    const Vec3 vPerifocal{-k * sinNu, k * (el.ecc + cosNu), 0.0};

    // Perifocal -> inertial: Rz(lan) * Rx(inc) * Rz(aop), applied right to left.
    const Quat rot = Quat::fromAxisAngle({0, 0, 1}, el.lan) *
                     Quat::fromAxisAngle({1, 0, 0}, el.inc) *
                     Quat::fromAxisAngle({0, 0, 1}, el.aop);

    return {rot.rotate(rPerifocal), rot.rotate(vPerifocal)};
}

OrbitInfo orbitInfo(const Elements& el, f64 mu) {
    OrbitInfo info;
    info.closed = el.ecc < 1.0 - kParabolicTol;

    info.periapsis = el.slr / (1.0 + el.ecc);
    info.apoapsis = info.closed ? el.slr / (1.0 - el.ecc) : kInf;
    info.radius = el.slr / (1.0 + el.ecc * std::cos(el.tra));

    if (info.closed) {
        const f64 a = el.sma;
        info.meanMotion = std::sqrt(mu / (a * a * a));
        info.period = kTau / info.meanMotion;
        info.energy = -mu / (2.0 * a);
        info.speed = std::sqrt(std::max(0.0, mu * (2.0 / info.radius - 1.0 / a)));
    } else {
        info.meanMotion = 0.0;
        info.period = kInf;
        info.energy = std::isinf(el.sma) ? 0.0 : -mu / (2.0 * el.sma);
        info.speed = std::sqrt(std::max(0.0, 2.0 * (info.energy + mu / info.radius)));
    }
    return info;
}

// --- anomaly conversions ---------------------------------------------------

f64 trueToEccentricAnomaly(f64 trueAnomaly, f64 ecc) {
    if (ecc < 1.0) {
        return 2.0 * std::atan2(std::sqrt(1.0 - ecc) * std::sin(trueAnomaly * 0.5),
                                std::sqrt(1.0 + ecc) * std::cos(trueAnomaly * 0.5));
    }
    // Hyperbolic: build sinh(H) directly instead of going through tan(nu/2),
    // which is unbounded as the true anomaly approaches the asymptote.
    const f64 nu = wrapPi(trueAnomaly);
    const f64 sinhH = std::sqrt(ecc * ecc - 1.0) * std::sin(nu) / (1.0 + ecc * std::cos(nu));
    return std::asinh(sinhH);
}

f64 eccentricToTrueAnomaly(f64 eccAnomaly, f64 ecc) {
    if (ecc < 1.0) {
        return wrapTau(2.0 * std::atan2(std::sqrt(1.0 + ecc) * std::sin(eccAnomaly * 0.5),
                                        std::sqrt(1.0 - ecc) * std::cos(eccAnomaly * 0.5)));
    }
    return 2.0 * std::atan2(std::sqrt(ecc + 1.0) * std::sinh(eccAnomaly * 0.5),
                            std::sqrt(ecc - 1.0) * std::cosh(eccAnomaly * 0.5));
}

f64 eccentricToMeanAnomaly(f64 eccAnomaly, f64 ecc) {
    if (ecc < 1.0) return eccAnomaly - ecc * std::sin(eccAnomaly);
    return ecc * std::sinh(eccAnomaly) - eccAnomaly;
}

f64 meanToEccentricAnomaly(f64 meanAnomaly, f64 ecc) {
    if (ecc < 1.0) {
        const f64 M = wrapPi(meanAnomaly);
        // A near-parabolic orbit spends nearly all of its mean anomaly close to
        // periapsis, so M itself is a poor starting guess there; pi keeps
        // Newton inside the convergent basin.
        f64 E = (ecc < 0.8) ? M : sign(M) * kPi;
        for (int i = 0; i < 100; ++i) {
            const f64 dE = -(E - ecc * std::sin(E) - M) / (1.0 - ecc * std::cos(E));
            E += dE;
            if (std::abs(dE) < 1e-14) break;
        }
        return E;
    }

    // Hyperbolic. The log form is the asymptotic inverse of M = e*sinh(H) - H,
    // which is what keeps the guess sane far from periapsis.
    f64 H = (std::abs(meanAnomaly) > 6.0)
                ? sign(meanAnomaly) * std::log(2.0 * std::abs(meanAnomaly) / ecc + 1.8)
                : meanAnomaly / (ecc - 1.0);
    for (int i = 0; i < 100; ++i) {
        const f64 dH = -(ecc * std::sinh(H) - H - meanAnomaly) / (ecc * std::cosh(H) - 1.0);
        H += dH;
        if (std::abs(dH) < 1e-14) break;
    }
    return H;
}

// --- propagation -----------------------------------------------------------

StateVector propagate(const StateVector& sv, f64 mu, f64 dt) {
    const f64 r0 = length(sv.pos);
    const f64 v0 = length(sv.vel);
    if (r0 <= 0.0) return sv;

    const f64 rdotv = dot(sv.pos, sv.vel);
    const f64 sqrtMu = std::sqrt(mu);
    const f64 alpha = 2.0 / r0 - v0 * v0 / mu; // reciprocal of the semi-major axis

    // Whole revolutions of a closed orbit are a no-op. Folding them away keeps
    // the universal anomaly small, which is what keeps Newton convergent when
    // the sim runs at 100000x and a single dt spans months.
    if (alpha > 1e-12) {
        const f64 period = kTau / (sqrtMu * alpha * std::sqrt(alpha));
        dt = std::fmod(dt, period);
    }

    // Starting guess for the universal anomaly, chosen per conic type.
    f64 chi;
    if (alpha > 1e-12) {
        chi = sqrtMu * dt * alpha;
    } else if (alpha < -1e-12) {
        const f64 a = 1.0 / alpha; // negative on a hyperbola
        const f64 denom = rdotv + sign(dt) * std::sqrt(-mu * a) * (1.0 - r0 * alpha);
        chi = sign(dt) * std::sqrt(-a) * std::log(-2.0 * mu * alpha * dt / denom);
    } else {
        const f64 hmag = length(cross(sv.pos, sv.vel));
        const f64 p = hmag * hmag / mu;
        const f64 s = 0.5 * std::atan(1.0 / (3.0 * std::sqrt(mu / (p * p * p)) * dt));
        const f64 w = std::atan(std::cbrt(std::tan(s)));
        chi = std::sqrt(p) * 2.0 / std::tan(2.0 * w);
    }

    f64 psi = 0.0, c2 = 0.5, c3 = 1.0 / 6.0, r = r0;
    for (int i = 0; i < 200; ++i) {
        psi = chi * chi * alpha;
        stumpff(psi, c2, c3);

        r = chi * chi * c2 + (rdotv / sqrtMu) * chi * (1.0 - psi * c3) + r0 * (1.0 - psi * c2);

        const f64 dchi = (sqrtMu * dt - chi * chi * chi * c3 - (rdotv / sqrtMu) * chi * chi * c2 -
                          r0 * chi * (1.0 - psi * c3)) /
                         r;
        chi += dchi;
        if (std::abs(dchi) < 1e-10) break;
    }

    // Lagrange coefficients: the new state is a linear combination of the old
    // position and velocity vectors.
    const f64 f = 1.0 - (chi * chi / r0) * c2;
    const f64 g = dt - (chi * chi * chi / sqrtMu) * c3;
    const Vec3 rNew = sv.pos * f + sv.vel * g;

    const f64 rMag = length(rNew);
    const f64 gdot = 1.0 - (chi * chi / rMag) * c2;
    const f64 fdot = (sqrtMu / (rMag * r0)) * chi * (psi * c3 - 1.0);
    const Vec3 vNew = sv.pos * fdot + sv.vel * gdot;

    return {rNew, vNew};
}

Elements propagateElements(const Elements& el, f64 mu, f64 dt) {
    Elements out = el;

    const f64 E0 = trueToEccentricAnomaly(el.tra, el.ecc);
    const f64 M0 = eccentricToMeanAnomaly(E0, el.ecc);

    const f64 a = std::abs(el.sma);
    const f64 n = std::sqrt(mu / (a * a * a));

    out.tra = eccentricToTrueAnomaly(meanToEccentricAnomaly(M0 + n * dt, el.ecc), el.ecc);
    return out;
}

} // namespace orb
