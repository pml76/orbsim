//
// Tests for the two-body core.
//
// The valuable checks here are the ones that cross two independent code paths
// against each other -- universal-variable propagation against Kepler-element
// propagation, state->elements against elements->state. A sign error in one of
// them cannot hide, because the other does not share it.
//
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <expected>
#include <print>
#include <string_view>

using namespace orb;
using namespace orb::literals;

namespace {

// Earth, WGS-84 / EGM-96.
constexpr GravParam kMuEarth{3.986004418e14};
constexpr Metres kEarthRadius{6378137.0};

// No mutable globals: initialization order across translation units is
// unspecified, and a mutable static is a data race waiting for a second thread.
// The counters are carried explicitly instead.
struct Run {
    int checks{};
    int failures{};
};

void reportMismatch(std::string_view what, f64 got, f64 want, f64 tol) {
    std::print("  FAIL {}\n        got  {:.12g}\n        want {:.12g}  (tol {:g})\n",
               what,
               got,
               want,
               tol);
}

void check(Run& run, bool condition, std::string_view what) {
    ++run.checks;
    if (!condition) {
        ++run.failures;
        std::print("  FAIL {}\n", what);
    }
}

// The tolerance is a distinct type, so `checkNear(run, what, got, tol, want)`
// no longer compiles. This used to take three adjacent f64 parameters that
// transposed in silence -- I.24 exactly, and the reason Tolerance exists.
void checkNear(Run& run, std::string_view what, f64 got, f64 want, Tolerance tol) {
    ++run.checks;
    if (!nearlyEqual(got, want, tol) || std::isnan(got)) {
        ++run.failures;
        reportMismatch(what, got, want, tol.value);
    }
}

// Relative comparison, for quantities whose magnitude spans many orders (radii
// in metres, speeds in m/s) where an absolute tolerance is meaningless.
void checkRel(Run& run, std::string_view what, f64 got, f64 want, Tolerance relTol) {
    ++run.checks;
    const f64 scale = std::max(std::abs(want), 1e-30);
    if (!nearlyEqual(got / scale, want / scale, relTol) || std::isnan(got)) {
        ++run.failures;
        reportMismatch(what, got, want, relTol.value * scale);
    }
}

void checkVecRel(
    Run& run, std::string_view what, const Vec3& got, const Vec3& want, Tolerance relTol) {
    ++run.checks;
    const f64 scale = std::max(length(want), 1e-30);
    if (!(length(got - want) / scale <= relTol.value)) {
        ++run.failures;
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
void checkAngle(Run& run, std::string_view what, Radians got, Radians want, Tolerance tol) {
    ++run.checks;
    if (!(std::abs(wrapPi(Radians{got.value - want.value}).value) <= tol.value)) {
        ++run.failures;
        reportMismatch(what, got.value, want.value, tol.value);
    }
}

void section(std::string_view name) { std::print("{}\n", name); }

// Unwraps an expected, counting a failure instead when it holds an error. Every
// orbital entry point can fail now, and a test that ignored that would be
// testing nothing.
template <typename T>
[[nodiscard]] bool
expectOk(Run& run, const std::expected<T, OrbitError>& result, std::string_view what) {
    ++run.checks;
    if (!result) {
        ++run.failures;
        std::print("  FAIL {}: {}\n", what, describe(result.error()));
        return false;
    }
    return true;
}

[[nodiscard]] Elements
makeElements(Metres sma, Eccentricity ecc, Degrees inc, Degrees lan, Degrees aop, Degrees tra) {
    return Elements{.sma = sma,
                    .ecc = ecc,
                    .inc = toRadians(inc),
                    .lan = toRadians(lan),
                    .aop = toRadians(aop),
                    .tra = toRadians(tra),
                    .slr = Metres{sma.value * (1.0 - (ecc.value * ecc.value))}};
}

// --- tests -----------------------------------------------------------------

// Elements -> state -> elements must be the identity for a well-conditioned
// orbit (non-circular, non-equatorial), where every element is meaningful.
void testElementRoundTrip(Run& run) {
    section("elements <-> state round trip");

    struct Case {
        const char* name{};
        Elements el{};
    };
    const std::array cases = std::to_array<Case>({
        {.name = "LEO, inclined, slightly eccentric",
         .el = makeElements(Metres{kEarthRadius.value + 500e3},
                            Eccentricity{0.01},
                            Degrees{51.6},
                            Degrees{120.0},
                            Degrees{45.0},
                            Degrees{200.0})},
        {.name = "GTO, highly eccentric",
         .el = makeElements(Metres{24582e3},
                            Eccentricity{0.7306},
                            Degrees{28.5},
                            Degrees{10.0},
                            Degrees{178.0},
                            Degrees{30.0})},
        {.name = "Polar",
         .el = makeElements(Metres{7200e3},
                            Eccentricity{0.02},
                            Degrees{90.0},
                            Degrees{300.0},
                            Degrees{90.0},
                            Degrees{45.0})},
        {.name = "Retrograde",
         .el = makeElements(Metres{8000e3},
                            Eccentricity{0.15},
                            Degrees{145.0},
                            Degrees{200.0},
                            Degrees{320.0},
                            Degrees{275.0})},
    });

    for (const auto& c : cases) {
        const StateVector sv = stateFromElements(c.el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        std::print("  {}\n", c.name);
        if (!expectOk(run, back, "elementsFromState")) continue;

        checkRel(run, "    sma", back->sma.value, c.el.sma.value, Tolerance{1e-12});
        checkNear(run, "    ecc", back->ecc.value, c.el.ecc.value, Tolerance{1e-12});
        checkAngle(run, "    inc", back->inc, c.el.inc, Tolerance{1e-12});
        checkAngle(run, "    lan", back->lan, c.el.lan, Tolerance{1e-12});
        checkAngle(run, "    aop", back->aop, c.el.aop, Tolerance{1e-11});
        checkAngle(run, "    tra", back->tra, c.el.tra, Tolerance{1e-11});
    }
}

// A circular orbit has no periapsis. The canonical form must fold aop into the
// true anomaly rather than producing NaN, and still reproduce the same state.
void testDegenerateOrbits(Run& run) {
    section("degenerate orbits stay finite");

    {
        const Elements el = makeElements(Metres{7000e3},
                                         Eccentricity{0.0},
                                         Degrees{30.0},
                                         Degrees{70.0},
                                         Degrees{40.0},
                                         Degrees{25.0});
        const StateVector sv = stateFromElements(el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        std::print("  circular inclined\n");
        if (expectOk(run, back, "elementsFromState")) {
            checkNear(run, "    aop folded to zero", back->aop.value, 0.0, Tolerance{1e-12});
            // aop + tra is the argument of latitude, and that is preserved.
            checkAngle(run,
                       "    argument of latitude",
                       back->tra,
                       Radians{el.aop.value + el.tra.value},
                       Tolerance{1e-10});
            checkVecRel(run,
                        "    position reproduced",
                        stateFromElements(*back, kMuEarth).pos,
                        sv.pos,
                        Tolerance{1e-12});
        }
    }

    {
        const Elements el = makeElements(Metres{42164e3},
                                         Eccentricity{0.001},
                                         Degrees{0.0},
                                         Degrees{0.0},
                                         Degrees{60.0},
                                         Degrees{15.0});
        const StateVector sv = stateFromElements(el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        std::print("  equatorial (geostationary)\n");
        if (expectOk(run, back, "elementsFromState")) {
            checkNear(run, "    lan folded to zero", back->lan.value, 0.0, Tolerance{1e-12});
            checkNear(run, "    inc", back->inc.value, 0.0, Tolerance{1e-12});
            checkAngle(run, "    aop from x-axis", back->aop, el.aop, Tolerance{1e-10});
            checkVecRel(run,
                        "    position reproduced",
                        stateFromElements(*back, kMuEarth).pos,
                        sv.pos,
                        Tolerance{1e-12});
        }
    }
}

// Known closed-form values, independent of any of the code under test.
void testKnownValues(Run& run) {
    section("known analytic values");

    // 400 km circular orbit: period from the elements must match 2*pi*r/v.
    const f64 r = kEarthRadius.value + 400e3;
    const f64 v = std::sqrt(kMuEarth.value / r);
    const StateVector sv{.pos = {r, 0, 0}, .vel = {0, v, 0}};
    const auto el = elementsFromState(sv, kMuEarth);
    if (!expectOk(run, el, "elementsFromState")) return;

    const OrbitInfo info = orbitInfo(*el, kMuEarth);

    checkRel(run, "  circular sma equals radius", el->sma.value, r, Tolerance{1e-12});
    checkNear(run, "  circular ecc is zero", el->ecc.value, 0.0, Tolerance{1e-12});
    checkRel(run,
             "  period matches circumference / speed",
             info.period.value,
             kTau * r / v,
             Tolerance{1e-10});
    checkRel(run, "  periapsis", info.periapsis.value, r, Tolerance{1e-12});
    checkRel(run, "  apoapsis", info.apoapsis.value, r, Tolerance{1e-12});
    // Sanity anchor: a 400 km orbit takes a bit over 92 minutes.
    checkNear(run, "  period is ~5554 s", info.period.value, 5554.0, Tolerance{5.0});

    // Vis-viva on an eccentric orbit, checked at periapsis.
    const Elements e2 = makeElements(Metres{10000e3},
                                     Eccentricity{0.3},
                                     Degrees{20.0},
                                     Degrees{0.0},
                                     Degrees{0.0},
                                     Degrees{0.0});
    const StateVector p = stateFromElements(e2, kMuEarth);
    const f64 rp = e2.sma.value * (1.0 - e2.ecc.value);
    const f64 vp = std::sqrt(kMuEarth.value * ((2.0 / rp) - (1.0 / e2.sma.value)));
    checkRel(run, "  periapsis radius", length(p.pos), rp, Tolerance{1e-12});
    checkRel(run, "  periapsis speed (vis-viva)", length(p.vel), vp, Tolerance{1e-12});
}

// The two propagators share no code. Agreeing to 1e-9 over a range of orbits
// and time steps is strong evidence both are right.
void testPropagatorsAgree(Run& run) {
    section("universal-variable vs Kepler-element propagation");

    struct Case {
        const char* name{};
        Elements el{};
    };
    const std::array cases = std::to_array<Case>({
        {.name = "near-circular LEO",
         .el = makeElements(Metres{6878e3},
                            Eccentricity{0.001},
                            Degrees{51.6},
                            Degrees{30.0},
                            Degrees{10.0},
                            Degrees{0.0})},
        {.name = "GTO",
         .el = makeElements(Metres{24582e3},
                            Eccentricity{0.7306},
                            Degrees{28.5},
                            Degrees{10.0},
                            Degrees{178.0},
                            Degrees{5.0})},
        {.name = "very eccentric",
         .el = makeElements(Metres{100000e3},
                            Eccentricity{0.95},
                            Degrees{63.4},
                            Degrees{90.0},
                            Degrees{270.0},
                            Degrees{120.0})},
    });

    for (const auto& c : cases) {
        const OrbitInfo info = orbitInfo(c.el, kMuEarth);
        const StateVector sv0 = stateFromElements(c.el, kMuEarth);

        std::print("  {}\n", c.name);
        for (const f64 frac : {0.05, 0.25, 0.5, 0.77, 0.99}) {
            const Seconds dt{frac * info.period.value};

            const auto viaUniversal = propagate(sv0, kMuEarth, dt);
            const auto viaElementSet = propagateElements(c.el, kMuEarth, dt);
            if (!expectOk(run, viaUniversal, "propagate")) continue;
            if (!expectOk(run, viaElementSet, "propagateElements")) continue;

            const StateVector viaElements = stateFromElements(*viaElementSet, kMuEarth);
            checkVecRel(
                run, "    position agrees", viaUniversal->pos, viaElements.pos, Tolerance{1e-9});
            checkVecRel(
                run, "    velocity agrees", viaUniversal->vel, viaElements.vel, Tolerance{1e-9});
        }
    }
}

// Propagation must be time-reversible and must conserve the orbit itself.
void testPropagationInvariants(Run& run) {
    section("propagation invariants");

    const Elements el = makeElements(Metres{12000e3},
                                     Eccentricity{0.4},
                                     Degrees{35.0},
                                     Degrees{140.0},
                                     Degrees{25.0},
                                     Degrees{80.0});
    const StateVector sv0 = stateFromElements(el, kMuEarth);
    const OrbitInfo info = orbitInfo(el, kMuEarth);

    const Seconds dt{0.37 * info.period.value};
    const auto fwd = propagate(sv0, kMuEarth, dt);
    if (!expectOk(run, fwd, "propagate forward")) return;

    const auto back = propagate(*fwd, kMuEarth, Seconds{-dt.value});
    if (!expectOk(run, back, "propagate backward")) return;

    checkVecRel(
        run, "  forward then back returns the start (pos)", back->pos, sv0.pos, Tolerance{1e-10});
    checkVecRel(
        run, "  forward then back returns the start (vel)", back->vel, sv0.vel, Tolerance{1e-10});

    // Energy and angular momentum are constants of the two-body motion, so a
    // long propagation must not move them.
    const auto distant = propagate(sv0, kMuEarth, Seconds{500.0 * info.period.value});
    if (expectOk(run, distant, "propagate 500 revolutions")) {
        const auto far = elementsFromState(*distant, kMuEarth);
        if (expectOk(run, far, "elementsFromState after 500 revolutions")) {
            checkRel(run,
                     "  sma conserved over 500 revolutions",
                     far->sma.value,
                     el.sma.value,
                     Tolerance{1e-9});
            checkRel(run,
                     "  ecc conserved over 500 revolutions",
                     far->ecc.value,
                     el.ecc.value,
                     Tolerance{1e-9});
            checkAngle(
                run, "  inc conserved over 500 revolutions", far->inc, el.inc, Tolerance{1e-9});
        }
    }

    // Half a period from periapsis lands exactly on apoapsis.
    Elements atPeri = el;
    atPeri.tra = Radians{0.0};
    const auto apo =
        propagate(stateFromElements(atPeri, kMuEarth), kMuEarth, Seconds{0.5 * info.period.value});
    if (expectOk(run, apo, "propagate half a period")) {
        checkRel(run,
                 "  half period from periapsis reaches apoapsis",
                 length(apo->pos),
                 info.apoapsis.value,
                 Tolerance{1e-9});
    }

    // A quarter period on a circular orbit is a quarter turn.
    const f64 rc = 7500e3;
    const StateVector c0{.pos = {rc, 0, 0}, .vel = {0, std::sqrt(kMuEarth.value / rc), 0}};
    const auto circular = elementsFromState(c0, kMuEarth);
    if (!expectOk(run, circular, "elementsFromState circular")) return;

    const OrbitInfo ci = orbitInfo(*circular, kMuEarth);
    const auto c1 = propagate(c0, kMuEarth, Seconds{0.25 * ci.period.value});
    if (expectOk(run, c1, "propagate a quarter period")) {
        checkNear(run,
                  "  quarter period is a quarter turn",
                  angleBetween(c0.pos, c1->pos),
                  kPi / 2,
                  Tolerance{1e-9});
        checkRel(run, "  circular radius unchanged", length(c1->pos), rc, Tolerance{1e-12});
    }
}

// Escape trajectories are not a special case in this code, so they need the
// same coverage as closed orbits.
void testHyperbolic(Run& run) {
    section("hyperbolic trajectories");

    // Departing Earth well above escape speed.
    const f64 r0 = kEarthRadius.value + 300e3;
    const f64 vEsc = std::sqrt(2.0 * kMuEarth.value / r0);
    const StateVector sv{.pos = {r0, 0, 0}, .vel = {1200.0, vEsc * 1.15, 0}};

    const auto el = elementsFromState(sv, kMuEarth);
    if (!expectOk(run, el, "elementsFromState hyperbolic")) return;

    const OrbitInfo info = orbitInfo(*el, kMuEarth);

    check(run, el->ecc.value > 1.0, "  trajectory is hyperbolic");
    check(run, el->sma.value < 0.0, "  sma is negative");
    check(run, std::isinf(info.apoapsis.value), "  apoapsis is infinite");
    check(run, info.energy > 0.0, "  energy is positive");

    // Round trip through the elements.
    const StateVector rebuilt = stateFromElements(*el, kMuEarth);
    checkVecRel(run, "  elements reproduce the state (pos)", rebuilt.pos, sv.pos, Tolerance{1e-11});
    checkVecRel(run, "  elements reproduce the state (vel)", rebuilt.vel, sv.vel, Tolerance{1e-11});

    // Reversibility, over an hour of coasting outbound.
    const auto out = propagate(sv, kMuEarth, 3600.0_s);
    if (!expectOk(run, out, "propagate hyperbolic")) return;

    const auto returned = propagate(*out, kMuEarth, Seconds{-3600.0});
    if (expectOk(run, returned, "propagate hyperbolic backward")) {
        checkVecRel(run, "  outbound then back (pos)", returned->pos, sv.pos, Tolerance{1e-9});
    }

    check(run, length(out->pos) > length(sv.pos), "  trajectory is receding");

    // And the two propagators must still agree out here.
    const auto viaElementSet = propagateElements(*el, kMuEarth, 3600.0_s);
    if (expectOk(run, viaElementSet, "propagateElements hyperbolic")) {
        checkVecRel(run,
                    "  propagators agree on hyperbola",
                    out->pos,
                    stateFromElements(*viaElementSet, kMuEarth).pos,
                    Tolerance{1e-8});
    }
}

// Kepler's equation is solved by Newton iteration; it has to converge for every
// eccentricity the sim can produce, including near-parabolic ones.
void testKeplerSolver(Run& run) {
    section("Kepler equation solver");

    for (const f64 ecc : {0.0, 0.1, 0.5, 0.9, 0.99, 0.999}) {
        f64 worst = 0.0;
        bool allSolved = true;

        for (int i = 0; i < 360; ++i) {
            const Radians meanAnomaly = toRadians(Degrees{static_cast<f64>(i)});
            const auto solved = meanToEccentricAnomaly(meanAnomaly, Eccentricity{ecc});
            if (!solved) {
                allSolved = false;
                break;
            }
            const Radians backAgain = eccentricToMeanAnomaly(*solved, Eccentricity{ecc});
            worst = std::max(worst,
                             std::abs(wrapPi(Radians{backAgain.value - meanAnomaly.value}).value));
        }

        check(run, allSolved, "  every mean anomaly solved");
        checkNear(run, "  elliptic round trip", worst, 0.0, Tolerance{1e-11});
        if (worst > 1e-11) std::print("        (at ecc = {:g})\n", ecc);
    }

    // True <-> eccentric anomaly, elliptic and hyperbolic.
    for (const f64 ecc : {0.0, 0.3, 0.85, 0.999}) {
        for (int i = 0; i < 360; i += 7) {
            const Radians nu = toRadians(Degrees{static_cast<f64>(i)});
            const Radians eccAnomaly = trueToEccentricAnomaly(nu, Eccentricity{ecc});
            checkAngle(run,
                       "  true<->eccentric (elliptic)",
                       eccentricToTrueAnomaly(eccAnomaly, Eccentricity{ecc}),
                       nu,
                       Tolerance{1e-10});
        }
    }

    for (const f64 ecc : {1.2, 2.0, 5.0}) {
        // Stay inside the asymptote, where the true anomaly is reachable.
        const f64 nuMax = std::acos(-1.0 / ecc) * 0.95;
        for (int i = -20; i <= 20; ++i) {
            const Radians nu{nuMax * static_cast<f64>(i) / 20.0};
            const Radians hyperbolic = trueToEccentricAnomaly(nu, Eccentricity{ecc});
            checkAngle(run,
                       "  true<->eccentric (hyperbolic)",
                       eccentricToTrueAnomaly(hyperbolic, Eccentricity{ecc}),
                       nu,
                       Tolerance{1e-9});

            const Radians meanAnomaly = eccentricToMeanAnomaly(hyperbolic, Eccentricity{ecc});
            const auto solved = meanToEccentricAnomaly(meanAnomaly, Eccentricity{ecc});
            if (expectOk(run, solved, "  hyperbolic Kepler solve")) {
                checkNear(run,
                          "  hyperbolic Kepler round trip",
                          solved->value,
                          hyperbolic.value,
                          Tolerance{1e-9});
            }
        }
    }
}

// The contract says failure is reported, never returned as a plausible number.
// A contract is worth exactly as much as its test -- and until this commit,
// propagate() answered a zero-radius state by silently handing the input back.
void testFailuresAreReported(Run& run) {
    section("failures are reported, not approximated");

    const StateVector atCentre{.pos = {0, 0, 0}, .vel = {1000.0, 0, 0}};
    const auto degenerate = propagate(atCentre, kMuEarth, 60.0_s);
    check(run, !degenerate.has_value(), "  a zero-radius state is refused");
    check(run,
          !degenerate.has_value() && degenerate.error() == OrbitError::DegenerateState,
          "  with the specific error");

    const auto degenerateElements = elementsFromState(atCentre, kMuEarth);
    check(run, !degenerateElements.has_value(), "  and refused by elementsFromState too");

    const StateVector leo{.pos = {7000e3, 0, 0}, .vel = {0, 7546.0, 0}};
    const auto massless = propagate(leo, GravParam{0.0}, 60.0_s);
    check(run, !massless.has_value(), "  a massless central body is refused");
    check(run,
          !massless.has_value() && massless.error() == OrbitError::NonPositiveGravity,
          "  with the specific error");

    const auto negativeGravity = elementsFromState(leo, GravParam{-1.0});
    check(run, !negativeGravity.has_value(), "  negative gravity is refused");

    // Every error can be explained to a human.
    check(run, !describe(OrbitError::SolverDidNotConverge).empty(), "  errors describe themselves");
    check(run, !describe(OrbitError::DegenerateState).empty(), "  all of them");
    check(run, !describe(OrbitError::NonPositiveGravity).empty(), "  every one");
}

} // namespace

// main is the one function nothing may escape from: an exception leaving it is
// std::terminate, with no message and no exit code worth reading. std::print can
// throw if stdout is closed, so it is caught here and turned into a diagnostic
// and a failing status -- the same "no error is silently ignored" rule the rest
// of this codebase follows, applied at the top.
//
// The handlers use std::fputs rather than std::print, because a reporting path
// that can itself throw is not a reporting path.
int main() {
    try {
        std::print("orbsim :: two-body core\n\n");

        Run run;
        testElementRoundTrip(run);
        testDegenerateOrbits(run);
        testKnownValues(run);
        testPropagatorsAgree(run);
        testPropagationInvariants(run);
        testHyperbolic(run);
        testKeplerSolver(run);
        testFailuresAreReported(run);

        std::print("\n{} checks, {} failures\n", run.checks, run.failures);
        return run.failures == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::fputs("unhandled exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputs("\n", stderr);
        return 2;
    } catch (...) {
        std::fputs("unhandled exception of unknown type\n", stderr);
        return 2;
    }
}
