//
// Tests for the two-body core.
//
// The valuable checks here are the ones that cross two independent code paths
// against each other -- universal-variable propagation against Kepler-element
// propagation, state->elements against elements->state. A sign error in one of
// them cannot hide, because the other does not share it.
//
#include "orbit/Orbit.hpp"

#include <cmath>
#include <print>
#include <string_view>

using namespace orb;

namespace {

// Earth, WGS-84 / EGM-96.
constexpr f64 kMuEarth = 3.986004418e14; // m^3/s^2
constexpr f64 kREarth = 6378137.0;       // m

int g_checks = 0;
int g_failures = 0;

void fail(std::string_view what, f64 got, f64 want, f64 tol) {
    ++g_failures;
    std::print("  FAIL {}\n        got  {:.12g}\n        want {:.12g}  (tol {:g})\n",
               what,
               got,
               want,
               tol);
}

void checkNear(std::string_view what, f64 got, f64 want, f64 tol) {
    ++g_checks;
    if (!(std::abs(got - want) <= tol) || std::isnan(got)) fail(what, got, want, tol);
}

// Relative comparison, for quantities whose magnitude spans many orders (radii
// in metres, speeds in m/s) where an absolute tolerance is meaningless.
void checkRel(std::string_view what, f64 got, f64 want, f64 relTol) {
    ++g_checks;
    const f64 scale = std::max(std::abs(want), 1e-30);
    if (!(std::abs(got - want) / scale <= relTol) || std::isnan(got)) {
        fail(what, got, want, relTol * scale);
    }
}

void checkVecRel(std::string_view what, const Vec3& got, const Vec3& want, f64 relTol) {
    ++g_checks;
    const f64 scale = std::max(length(want), 1e-30);
    if (!(length(got - want) / scale <= relTol)) {
        ++g_failures;
        --g_checks;
        std::print("  FAIL {}\n        got  ({:.10g}, {:.10g}, {:.10g})\n"
                   "        want ({:.10g}, {:.10g}, {:.10g})\n        rel err {:g}\n",
                   what,
                   got.x,
                   got.y,
                   got.z,
                   want.x,
                   want.y,
                   want.z,
                   length(got - want) / scale);
    }
}

// Angles compare modulo a full turn: 0 and tau are the same angle.
void checkAngle(std::string_view what, f64 got, f64 want, f64 tol) {
    ++g_checks;
    if (!(std::abs(wrapPi(got - want)) <= tol)) fail(what, got, want, tol);
}

void section(std::string_view name) { std::print("{}\n", name); }

// --- tests -----------------------------------------------------------------

// Elements -> state -> elements must be the identity for a well-conditioned
// orbit (non-circular, non-equatorial), where every element is meaningful.
void testElementRoundTrip() {
    section("elements <-> state round trip");

    struct Case {
        const char* name;
        Elements el;
    };
    const Case cases[] = {
        {"LEO, inclined, slightly eccentric",
         {.sma = kREarth + 500e3,
          .ecc = 0.01,
          .inc = rad(51.6),
          .lan = rad(120.0),
          .aop = rad(45.0),
          .tra = rad(200.0)}},
        {"GTO, highly eccentric",
         {.sma = 24582e3,
          .ecc = 0.7306,
          .inc = rad(28.5),
          .lan = rad(10.0),
          .aop = rad(178.0),
          .tra = rad(30.0)}},
        {"Polar",
         {.sma = 7200e3,
          .ecc = 0.02,
          .inc = rad(90.0),
          .lan = rad(300.0),
          .aop = rad(90.0),
          .tra = rad(45.0)}},
        {"Retrograde",
         {.sma = 8000e3,
          .ecc = 0.15,
          .inc = rad(145.0),
          .lan = rad(200.0),
          .aop = rad(320.0),
          .tra = rad(275.0)}},
    };

    for (const auto& c : cases) {
        Elements el = c.el;
        el.slr = el.sma * (1.0 - el.ecc * el.ecc);

        const StateVector sv = stateFromElements(el, kMuEarth);
        const Elements back = elementsFromState(sv, kMuEarth);

        std::print("  {}\n", c.name);
        checkRel("    sma", back.sma, el.sma, 1e-12);
        checkNear("    ecc", back.ecc, el.ecc, 1e-12);
        checkAngle("    inc", back.inc, el.inc, 1e-12);
        checkAngle("    lan", back.lan, el.lan, 1e-12);
        checkAngle("    aop", back.aop, el.aop, 1e-11);
        checkAngle("    tra", back.tra, el.tra, 1e-11);
    }
}

// A circular orbit has no periapsis. The canonical form must fold aop into the
// true anomaly rather than producing NaN, and still reproduce the same state.
void testDegenerateOrbits() {
    section("degenerate orbits stay finite");

    {
        Elements el{.sma = 7000e3,
                    .ecc = 0.0,
                    .inc = rad(30.0),
                    .lan = rad(70.0),
                    .aop = rad(40.0),
                    .tra = rad(25.0)};
        el.slr = el.sma;
        const StateVector sv = stateFromElements(el, kMuEarth);
        const Elements back = elementsFromState(sv, kMuEarth);

        std::print("  circular inclined\n");
        checkNear("    aop folded to zero", back.aop, 0.0, 1e-12);
        // aop + tra is the argument of latitude, and that is preserved.
        checkAngle("    argument of latitude", back.tra, el.aop + el.tra, 1e-10);
        checkVecRel(
            "    position reproduced", stateFromElements(back, kMuEarth).pos, sv.pos, 1e-12);
    }

    {
        Elements el{.sma = 42164e3,
                    .ecc = 0.001,
                    .inc = 0.0,
                    .lan = 0.0,
                    .aop = rad(60.0),
                    .tra = rad(15.0)};
        el.slr = el.sma * (1.0 - el.ecc * el.ecc);
        const StateVector sv = stateFromElements(el, kMuEarth);
        const Elements back = elementsFromState(sv, kMuEarth);

        std::print("  equatorial (geostationary)\n");
        checkNear("    lan folded to zero", back.lan, 0.0, 1e-12);
        checkNear("    inc", back.inc, 0.0, 1e-12);
        checkAngle("    aop from x-axis", back.aop, el.aop, 1e-10);
        checkVecRel(
            "    position reproduced", stateFromElements(back, kMuEarth).pos, sv.pos, 1e-12);
    }
}

// Known closed-form values, independent of any of the code under test.
void testKnownValues() {
    section("known analytic values");

    // 400 km circular orbit: period from the elements must match 2*pi*r/v.
    const f64 r = kREarth + 400e3;
    const f64 v = std::sqrt(kMuEarth / r);
    const StateVector sv{{r, 0, 0}, {0, v, 0}};
    const Elements el = elementsFromState(sv, kMuEarth);
    const OrbitInfo info = orbitInfo(el, kMuEarth);

    checkRel("  circular sma equals radius", el.sma, r, 1e-12);
    checkNear("  circular ecc is zero", el.ecc, 0.0, 1e-12);
    checkRel("  period matches circumference / speed", info.period, kTau * r / v, 1e-10);
    checkRel("  periapsis", info.periapsis, r, 1e-12);
    checkRel("  apoapsis", info.apoapsis, r, 1e-12);
    // Sanity anchor: a 400 km orbit takes a bit over 92 minutes.
    checkNear("  period is ~5554 s", info.period, 5554.0, 5.0);

    // Vis-viva on an eccentric orbit, checked at periapsis.
    Elements e2{.sma = 10000e3, .ecc = 0.3, .inc = rad(20.0), .lan = 0.0, .aop = 0.0, .tra = 0.0};
    e2.slr = e2.sma * (1.0 - e2.ecc * e2.ecc);
    const StateVector p = stateFromElements(e2, kMuEarth);
    const f64 rp = e2.sma * (1.0 - e2.ecc);
    const f64 vp = std::sqrt(kMuEarth * (2.0 / rp - 1.0 / e2.sma));
    checkRel("  periapsis radius", length(p.pos), rp, 1e-12);
    checkRel("  periapsis speed (vis-viva)", length(p.vel), vp, 1e-12);
}

// The two propagators share no code. Agreeing to 1e-9 over a range of orbits
// and time steps is strong evidence both are right.
void testPropagatorsAgree() {
    section("universal-variable vs Kepler-element propagation");

    struct Case {
        const char* name;
        Elements el;
    };
    const Case cases[] = {
        {"near-circular LEO",
         {.sma = 6878e3,
          .ecc = 0.001,
          .inc = rad(51.6),
          .lan = rad(30.0),
          .aop = rad(10.0),
          .tra = rad(0.0)}},
        {"GTO",
         {.sma = 24582e3,
          .ecc = 0.7306,
          .inc = rad(28.5),
          .lan = rad(10.0),
          .aop = rad(178.0),
          .tra = rad(5.0)}},
        {"very eccentric",
         {.sma = 100000e3,
          .ecc = 0.95,
          .inc = rad(63.4),
          .lan = rad(90.0),
          .aop = rad(270.0),
          .tra = rad(120.0)}},
    };

    for (const auto& c : cases) {
        Elements el = c.el;
        el.slr = el.sma * (1.0 - el.ecc * el.ecc);
        const OrbitInfo info = orbitInfo(el, kMuEarth);
        const StateVector sv0 = stateFromElements(el, kMuEarth);

        std::print("  {}\n", c.name);
        for (const f64 frac : {0.05, 0.25, 0.5, 0.77, 0.99}) {
            const f64 dt = frac * info.period;
            const StateVector viaUniversal = propagate(sv0, kMuEarth, dt);
            const StateVector viaElements =
                stateFromElements(propagateElements(el, kMuEarth, dt), kMuEarth);

            checkVecRel("    position agrees", viaUniversal.pos, viaElements.pos, 1e-9);
            checkVecRel("    velocity agrees", viaUniversal.vel, viaElements.vel, 1e-9);
        }
    }
}

// Propagation must be time-reversible and must conserve the orbit itself.
void testPropagationInvariants() {
    section("propagation invariants");

    Elements el{.sma = 12000e3,
                .ecc = 0.4,
                .inc = rad(35.0),
                .lan = rad(140.0),
                .aop = rad(25.0),
                .tra = rad(80.0)};
    el.slr = el.sma * (1.0 - el.ecc * el.ecc);
    const StateVector sv0 = stateFromElements(el, kMuEarth);
    const OrbitInfo info = orbitInfo(el, kMuEarth);

    const f64 dt = 0.37 * info.period;
    const StateVector fwd = propagate(sv0, kMuEarth, dt);
    const StateVector back = propagate(fwd, kMuEarth, -dt);
    checkVecRel("  forward then back returns the start (pos)", back.pos, sv0.pos, 1e-10);
    checkVecRel("  forward then back returns the start (vel)", back.vel, sv0.vel, 1e-10);

    // Energy and angular momentum are constants of the two-body motion, so a
    // long propagation must not move them.
    const Elements far = elementsFromState(propagate(sv0, kMuEarth, 500.0 * info.period), kMuEarth);
    checkRel("  sma conserved over 500 revolutions", far.sma, el.sma, 1e-9);
    checkRel("  ecc conserved over 500 revolutions", far.ecc, el.ecc, 1e-9);
    checkAngle("  inc conserved over 500 revolutions", far.inc, el.inc, 1e-9);

    // Half a period from periapsis lands exactly on apoapsis.
    Elements atPeri = el;
    atPeri.tra = 0.0;
    const StateVector apo =
        propagate(stateFromElements(atPeri, kMuEarth), kMuEarth, 0.5 * info.period);
    checkRel("  half period from periapsis reaches apoapsis", length(apo.pos), info.apoapsis, 1e-9);

    // A quarter period on a circular orbit is a quarter turn.
    const f64 rc = 7500e3;
    const StateVector c0{{rc, 0, 0}, {0, std::sqrt(kMuEarth / rc), 0}};
    const OrbitInfo ci = orbitInfo(elementsFromState(c0, kMuEarth), kMuEarth);
    const StateVector c1 = propagate(c0, kMuEarth, 0.25 * ci.period);
    checkNear("  quarter period is a quarter turn", angleBetween(c0.pos, c1.pos), kPi / 2, 1e-9);
    checkRel("  circular radius unchanged", length(c1.pos), rc, 1e-12);
}

// Escape trajectories are not a special case in this code, so they need the
// same coverage as closed orbits.
void testHyperbolic() {
    section("hyperbolic trajectories");

    // Departing Earth well above escape speed.
    const f64 r0 = kREarth + 300e3;
    const f64 vEsc = std::sqrt(2.0 * kMuEarth / r0);
    const StateVector sv{{r0, 0, 0}, {1200.0, vEsc * 1.15, 0}};

    const Elements el = elementsFromState(sv, kMuEarth);
    const OrbitInfo info = orbitInfo(el, kMuEarth);

    ++g_checks;
    if (!(el.ecc > 1.0)) {
        ++g_failures;
        --g_checks;
        std::print("  FAIL not hyperbolic: ecc = {:g}\n", el.ecc);
    }
    checkRel("  sma is negative", el.sma < 0.0 ? 1.0 : 0.0, 1.0, 1e-12);
    ++g_checks;
    if (!std::isinf(info.apoapsis)) {
        ++g_failures;
        --g_checks;
        std::print("  FAIL apoapsis should be infinite\n");
    }
    ++g_checks;
    if (!(info.energy > 0.0)) {
        ++g_failures;
        --g_checks;
        std::print("  FAIL energy should be positive\n");
    }

    // Round trip through the elements.
    const StateVector rebuilt = stateFromElements(el, kMuEarth);
    checkVecRel("  elements reproduce the state (pos)", rebuilt.pos, sv.pos, 1e-11);
    checkVecRel("  elements reproduce the state (vel)", rebuilt.vel, sv.vel, 1e-11);

    // Reversibility, over an hour of coasting outbound.
    const StateVector out = propagate(sv, kMuEarth, 3600.0);
    checkVecRel("  outbound then back (pos)", propagate(out, kMuEarth, -3600.0).pos, sv.pos, 1e-9);

    ++g_checks;
    if (!(length(out.pos) > length(sv.pos))) {
        ++g_failures;
        --g_checks;
        std::print("  FAIL should be receding\n");
    }

    // And the two propagators must still agree out here.
    const StateVector viaElements =
        stateFromElements(propagateElements(el, kMuEarth, 3600.0), kMuEarth);
    checkVecRel("  propagators agree on hyperbola", out.pos, viaElements.pos, 1e-8);
}

// Kepler's equation is solved by Newton iteration; it has to converge for
// every eccentricity the sim can produce, including near-parabolic ones.
void testKeplerSolver() {
    section("Kepler equation solver");

    for (const f64 ecc : {0.0, 0.1, 0.5, 0.9, 0.99, 0.999}) {
        f64 worst = 0.0;
        for (int i = 0; i < 360; ++i) {
            const f64 M = rad(static_cast<f64>(i));
            const f64 E = meanToEccentricAnomaly(M, ecc);
            worst = std::max(worst, std::abs(wrapPi(eccentricToMeanAnomaly(E, ecc) - M)));
        }
        checkNear("  elliptic round trip, ecc", worst, 0.0, 1e-11);
        if (worst > 1e-11) std::print("        (at ecc = {:g})\n", ecc);
    }

    // True <-> eccentric anomaly, elliptic and hyperbolic.
    for (const f64 ecc : {0.0, 0.3, 0.85, 0.999}) {
        for (int i = 0; i < 360; i += 7) {
            const f64 nu = rad(static_cast<f64>(i));
            const f64 E = trueToEccentricAnomaly(nu, ecc);
            checkAngle("  true<->eccentric (elliptic)", eccentricToTrueAnomaly(E, ecc), nu, 1e-10);
        }
    }
    for (const f64 ecc : {1.2, 2.0, 5.0}) {
        // Stay inside the asymptote, where the true anomaly is reachable.
        const f64 nuMax = std::acos(-1.0 / ecc) * 0.95;
        for (int i = -20; i <= 20; ++i) {
            const f64 nu = nuMax * static_cast<f64>(i) / 20.0;
            const f64 H = trueToEccentricAnomaly(nu, ecc);
            checkAngle("  true<->eccentric (hyperbolic)", eccentricToTrueAnomaly(H, ecc), nu, 1e-9);
            const f64 M = eccentricToMeanAnomaly(H, ecc);
            checkNear("  hyperbolic Kepler round trip", meanToEccentricAnomaly(M, ecc), H, 1e-9);
        }
    }
}

} // namespace

int main() {
    std::print("orbsim :: two-body core\n\n");

    testElementRoundTrip();
    testDegenerateOrbits();
    testKnownValues();
    testPropagatorsAgree();
    testPropagationInvariants();
    testHyperbolic();
    testKeplerSolver();

    std::print("\n{} checks, {} failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
