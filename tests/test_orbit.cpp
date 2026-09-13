//
// Tests for the two-body core.
//
// The valuable checks here are the ones that cross two independent code paths
// against each other -- universal-variable propagation against Kepler-element
// propagation, state->elements against elements->state. A sign error in one of
// them cannot hide, because the other does not share it.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace orb;
using namespace orb::literals;
using namespace orb::test;

// A note on the readability-function-cognitive-complexity suppressions below.
//
// Catch2's REQUIRE and REQUIRE_THAT each expand to a do-while wrapping a
// try/catch, so a case scores roughly three points per assertion whether or
// not it branches at all: the cases suppressed here contain no `if`, and the
// only loops are the ones walking a table of cases. The score measures the
// framework, not the code.
//
// Ruled by the project owner on 2026-09-09, with the alternatives measured
// first: the check offers an IgnoreMacros option that clears every one of
// these while still scoring hand-written control flow (a probe function still
// reported 35), and a NOLINTBEGIN/NOLINTEND region would cover a whole file in
// one line. Both were rejected in favour of a suppression per function,
// because that is the only one of the three that still reports a genuinely
// over-complex helper added to this file later. Each new TEST_CASE that
// crosses the threshold gets its own line, deliberately.

// Elements -> state -> elements must be the identity for a well-conditioned
// orbit (non-circular, non-equatorial), where every element is meaningful.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("elements <-> state round trip", "[orbit]") {
    struct Case {
        const char* name{};
        Elements el{};
    };
    const std::array cases = std::to_array<Case>({
        {
            .name = "LEO, inclined, slightly eccentric",
            .el = makeElements(Metres{kEarthRadius.value + 500e3},
                               Eccentricity{0.01},
                               Degrees{51.6},
                               Degrees{120.0},
                               Degrees{45.0},
                               Degrees{200.0}),
        },
        {
            .name = "GTO, highly eccentric",
            .el = makeElements(Metres{24582e3},
                               Eccentricity{0.7306},
                               Degrees{28.5},
                               Degrees{10.0},
                               Degrees{178.0},
                               Degrees{30.0}),
        },
        {
            .name = "Polar",
            .el = makeElements(Metres{7200e3},
                               Eccentricity{0.02},
                               Degrees{90.0},
                               Degrees{300.0},
                               Degrees{90.0},
                               Degrees{45.0}),
        },
        {
            .name = "Retrograde",
            .el = makeElements(Metres{8000e3},
                               Eccentricity{0.15},
                               Degrees{145.0},
                               Degrees{200.0},
                               Degrees{320.0},
                               Degrees{275.0}),
        },
    });

    for (const auto& c : cases) {
        CAPTURE(c.name);
        const StateVector sv = stateOf(c.el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        INFO(errorName(back));
        REQUIRE(back.has_value());

        REQUIRE_THAT(back->sma.value, WithinRelTo(c.el.sma.value, Tolerance{1e-12}));
        REQUIRE_THAT(back->ecc.value, WithinAbsOf(c.el.ecc.value, Tolerance{1e-12}));
        REQUIRE_THAT(wrapPi(back->inc - c.el.inc).value, WithinAbsOf(0.0, Tolerance{1e-12}));
        REQUIRE_THAT(wrapPi(back->lan - c.el.lan).value, WithinAbsOf(0.0, Tolerance{1e-12}));
        REQUIRE_THAT(wrapPi(back->aop - c.el.aop).value, WithinAbsOf(0.0, Tolerance{1e-11}));
        REQUIRE_THAT(wrapPi(back->tra - c.el.tra).value, WithinAbsOf(0.0, Tolerance{1e-11}));
    }
}

// A circular orbit has no periapsis. The canonical form must fold aop into the
// true anomaly rather than producing NaN, and still reproduce the same state.
TEST_CASE("degenerate orbits stay finite", "[orbit]") {
    SECTION("circular inclined") {
        const Elements el = makeElements(Metres{7000e3},
                                         Eccentricity{0.0},
                                         Degrees{30.0},
                                         Degrees{70.0},
                                         Degrees{40.0},
                                         Degrees{25.0});
        const StateVector sv = stateOf(el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        INFO(errorName(back));
        REQUIRE(back.has_value());

        INFO("aop folded to zero");
        REQUIRE_THAT(back->aop.value, WithinAbsOf(0.0, Tolerance{1e-12}));
        // aop + tra is the argument of latitude, and that is preserved.
        INFO("argument of latitude");
        REQUIRE_THAT(wrapPi(back->tra - (el.aop + el.tra)).value,
                     WithinAbsOf(0.0, Tolerance{1e-10}));
        REQUIRE_THAT(stateOf(*back, kMuEarth).pos, WithinRelVec(sv.pos, Tolerance{1e-12}));
    }

    SECTION("equatorial (geostationary)") {
        const Elements el = makeElements(Metres{42164e3},
                                         Eccentricity{0.001},
                                         Degrees{0.0},
                                         Degrees{0.0},
                                         Degrees{60.0},
                                         Degrees{15.0});
        const StateVector sv = stateOf(el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        INFO(errorName(back));
        REQUIRE(back.has_value());

        INFO("lan folded to zero");
        REQUIRE_THAT(back->lan.value, WithinAbsOf(0.0, Tolerance{1e-12}));
        REQUIRE_THAT(back->inc.value, WithinAbsOf(0.0, Tolerance{1e-12}));
        INFO("aop from the x-axis");
        REQUIRE_THAT(wrapPi(back->aop - el.aop).value, WithinAbsOf(0.0, Tolerance{1e-10}));
        REQUIRE_THAT(stateOf(*back, kMuEarth).pos, WithinRelVec(sv.pos, Tolerance{1e-12}));
    }
}

// Known closed-form values, independent of any of the code under test.
TEST_CASE("known analytic values", "[orbit]") {
    // 400 km circular orbit: period from the elements must match 2*pi*r/v.
    const f64 r = kEarthRadius.value + 400e3;
    const f64 v = std::sqrt(kMuEarth.value / r);
    const StateVector sv{.pos = {r, 0, 0}, .vel = {0, v, 0}};
    const auto el = elementsFromState(sv, kMuEarth);

    INFO(errorName(el));
    REQUIRE(el.has_value());

    const OrbitInfo info = orbitInfo(*el, kMuEarth);

    REQUIRE_THAT(el->sma.value, WithinRelTo(r, Tolerance{1e-12}));
    REQUIRE_THAT(el->ecc.value, WithinAbsOf(0.0, Tolerance{1e-12}));
    REQUIRE_THAT(info.period.value, WithinRelTo(kTau * r / v, Tolerance{1e-10}));
    REQUIRE_THAT(info.periapsis.value, WithinRelTo(r, Tolerance{1e-12}));
    REQUIRE_THAT(info.apoapsis.value, WithinRelTo(r, Tolerance{1e-12}));
    // Sanity anchor: a 400 km orbit takes a bit over 92 minutes.
    REQUIRE_THAT(info.period.value, WithinAbsOf(5554.0, Tolerance{5.0}));

    // Vis-viva on an eccentric orbit, checked at periapsis.
    const Elements e2 = makeElements(Metres{10000e3},
                                     Eccentricity{0.3},
                                     Degrees{20.0},
                                     Degrees{0.0},
                                     Degrees{0.0},
                                     Degrees{0.0});
    const StateVector p = stateOf(e2, kMuEarth);
    const f64 rp = e2.sma.value * (1.0 - e2.ecc.value);
    const f64 vp = std::sqrt(kMuEarth.value * ((2.0 / rp) - (1.0 / e2.sma.value)));
    REQUIRE_THAT(length(p.pos), WithinRelTo(rp, Tolerance{1e-12}));
    INFO("periapsis speed (vis-viva)");
    REQUIRE_THAT(length(p.vel), WithinRelTo(vp, Tolerance{1e-12}));
}

// The two propagators share no code. Agreeing to 1e-9 over a range of orbits
// and time steps is strong evidence both are right.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("universal-variable vs Kepler-element propagation", "[orbit]") {
    struct Case {
        const char* name{};
        Elements el{};
    };
    const std::array cases = std::to_array<Case>({
        {
            .name = "near-circular LEO",
            .el = makeElements(Metres{6878e3},
                               Eccentricity{0.001},
                               Degrees{51.6},
                               Degrees{30.0},
                               Degrees{10.0},
                               Degrees{0.0}),
        },
        {
            .name = "GTO",
            .el = makeElements(Metres{24582e3},
                               Eccentricity{0.7306},
                               Degrees{28.5},
                               Degrees{10.0},
                               Degrees{178.0},
                               Degrees{5.0}),
        },
        {
            .name = "very eccentric",
            .el = makeElements(Metres{100000e3},
                               Eccentricity{0.95},
                               Degrees{63.4},
                               Degrees{90.0},
                               Degrees{270.0},
                               Degrees{120.0}),
        },
    });

    for (const auto& c : cases) {
        const OrbitInfo info = orbitInfo(c.el, kMuEarth);
        const StateVector sv0 = stateOf(c.el, kMuEarth);

        for (const f64 frac : {0.05, 0.25, 0.5, 0.77, 0.99}) {
            CAPTURE(c.name, frac);
            const Seconds dt = info.period * frac;

            const auto viaUniversal = propagate(sv0, kMuEarth, dt);
            const auto viaElementSet = propagateElements(c.el, kMuEarth, dt);
            INFO(errorName(viaUniversal));
            REQUIRE(viaUniversal.has_value());
            INFO(errorName(viaElementSet));
            REQUIRE(viaElementSet.has_value());

            const StateVector viaElements = stateOf(*viaElementSet, kMuEarth);
            REQUIRE_THAT(viaUniversal->pos, WithinRelVec(viaElements.pos, Tolerance{1e-9}));
            REQUIRE_THAT(viaUniversal->vel, WithinRelVec(viaElements.vel, Tolerance{1e-9}));
        }
    }
}

// Propagation must be time-reversible and must conserve the orbit itself.
TEST_CASE("propagation invariants", "[orbit]") {
    const Elements el = makeElements(Metres{12000e3},
                                     Eccentricity{0.4},
                                     Degrees{35.0},
                                     Degrees{140.0},
                                     Degrees{25.0},
                                     Degrees{80.0});
    const StateVector sv0 = stateOf(el, kMuEarth);
    const OrbitInfo info = orbitInfo(el, kMuEarth);

    const Seconds dt = info.period * 0.37;
    const auto fwd = propagate(sv0, kMuEarth, dt);
    INFO(errorName(fwd));
    REQUIRE(fwd.has_value());

    const auto back = propagate(*fwd, kMuEarth, -dt);
    INFO(errorName(back));
    REQUIRE(back.has_value());

    INFO("forward then back returns the start");
    REQUIRE_THAT(back->pos, WithinRelVec(sv0.pos, Tolerance{1e-10}));
    REQUIRE_THAT(back->vel, WithinRelVec(sv0.vel, Tolerance{1e-10}));

    // Energy and angular momentum are constants of the two-body motion, so a
    // long propagation must not move them.
    const auto distant = propagate(sv0, kMuEarth, info.period * 500.0);
    INFO(errorName(distant));
    REQUIRE(distant.has_value());

    const auto far = elementsFromState(*distant, kMuEarth);
    INFO(errorName(far));
    REQUIRE(far.has_value());

    INFO("conserved over 500 revolutions");
    REQUIRE_THAT(far->sma.value, WithinRelTo(el.sma.value, Tolerance{1e-9}));
    REQUIRE_THAT(far->ecc.value, WithinRelTo(el.ecc.value, Tolerance{1e-9}));
    REQUIRE_THAT(wrapPi(far->inc - el.inc).value, WithinAbsOf(0.0, Tolerance{1e-9}));

    // Half a period from periapsis lands exactly on apoapsis.
    Elements atPeri = el;
    atPeri.tra = Radians{0.0};
    const auto apo = propagate(stateOf(atPeri, kMuEarth), kMuEarth, info.period * 0.5);
    INFO(errorName(apo));
    REQUIRE(apo.has_value());

    INFO("half a period from periapsis reaches apoapsis");
    REQUIRE_THAT(length(apo->pos), WithinRelTo(info.apoapsis.value, Tolerance{1e-9}));

    // A quarter period on a circular orbit is a quarter turn.
    const f64 rc = 7500e3;
    const StateVector c0{.pos = {rc, 0, 0}, .vel = {0, std::sqrt(kMuEarth.value / rc), 0}};
    const auto circular = elementsFromState(c0, kMuEarth);
    INFO(errorName(circular));
    REQUIRE(circular.has_value());

    const OrbitInfo ci = orbitInfo(*circular, kMuEarth);
    const auto c1 = propagate(c0, kMuEarth, ci.period * 0.25);
    INFO(errorName(c1));
    REQUIRE(c1.has_value());

    INFO("a quarter period is a quarter turn");
    REQUIRE_THAT(wrapPi(angleBetween(c0.pos, c1->pos) - Radians{kPi / 2}).value,
                 WithinAbsOf(0.0, Tolerance{1e-9}));
    REQUIRE_THAT(length(c1->pos), WithinRelTo(rc, Tolerance{1e-12}));
}

// Escape trajectories are not a special case in this code, so they need the
// same coverage as closed orbits.
TEST_CASE("hyperbolic trajectories", "[orbit]") {
    // Departing Earth well above escape speed.
    const f64 r0 = kEarthRadius.value + 300e3;
    const f64 vEsc = std::sqrt(2.0 * kMuEarth.value / r0);
    const StateVector sv{.pos = {r0, 0, 0}, .vel = {1200.0, vEsc * 1.15, 0}};

    const auto el = elementsFromState(sv, kMuEarth);
    INFO(errorName(el));
    REQUIRE(el.has_value());

    const OrbitInfo info = orbitInfo(*el, kMuEarth);

    REQUIRE(el->ecc.value > 1.0);
    REQUIRE(el->sma.value < 0.0);
    REQUIRE(std::isinf(info.apoapsis.value));
    REQUIRE(info.energy.value > 0.0);

    // Round trip through the elements.
    const StateVector rebuilt = stateOf(*el, kMuEarth);
    INFO("the elements reproduce the state");
    REQUIRE_THAT(rebuilt.pos, WithinRelVec(sv.pos, Tolerance{1e-11}));
    REQUIRE_THAT(rebuilt.vel, WithinRelVec(sv.vel, Tolerance{1e-11}));

    // Reversibility, over an hour of coasting outbound.
    const auto out = propagate(sv, kMuEarth, 3600.0_s);
    INFO(errorName(out));
    REQUIRE(out.has_value());

    const auto returned = propagate(*out, kMuEarth, -3600.0_s);
    INFO(errorName(returned));
    REQUIRE(returned.has_value());

    INFO("outbound then back");
    REQUIRE_THAT(returned->pos, WithinRelVec(sv.pos, Tolerance{1e-9}));

    INFO("the trajectory is receding");
    REQUIRE(length(out->pos) > length(sv.pos));

    // And the two propagators must still agree out here.
    const auto viaElementSet = propagateElements(*el, kMuEarth, 3600.0_s);
    INFO(errorName(viaElementSet));
    REQUIRE(viaElementSet.has_value());

    INFO("the propagators agree on a hyperbola");
    REQUIRE_THAT(out->pos, WithinRelVec(stateOf(*viaElementSet, kMuEarth).pos, Tolerance{1e-8}));
}

// Kepler's equation is solved by Newton iteration; it has to converge for every
// eccentricity the sim can produce, including near-parabolic ones.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Kepler equation solver", "[orbit]") {
    for (const f64 ecc : {0.0, 0.1, 0.5, 0.9, 0.99, 0.999}) {
        CAPTURE(ecc);
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
            worst = std::max(worst, std::abs(wrapPi(backAgain - meanAnomaly).value));
        }

        INFO("every mean anomaly solved");
        REQUIRE(allSolved);
        INFO("elliptic round trip");
        REQUIRE_THAT(worst, WithinAbsOf(0.0, Tolerance{1e-11}));
    }

    // True <-> eccentric anomaly, elliptic and hyperbolic.
    for (const f64 ecc : {0.0, 0.3, 0.85, 0.999}) {
        for (int i = 0; i < 360; i += 7) {
            CAPTURE(ecc, i);
            const Radians nu = toRadians(Degrees{static_cast<f64>(i)});
            const Radians eccAnomaly = trueToEccentricAnomaly(nu, Eccentricity{ecc});
            INFO("true <-> eccentric (elliptic)");
            REQUIRE_THAT(wrapPi(eccentricToTrueAnomaly(eccAnomaly, Eccentricity{ecc}) - nu).value,
                         WithinAbsOf(0.0, Tolerance{1e-10}));
        }
    }

    for (const f64 ecc : {1.2, 2.0, 5.0}) {
        // Stay inside the asymptote, where the true anomaly is reachable.
        const f64 nuMax = std::acos(-1.0 / ecc) * 0.95;
        for (int i = -20; i <= 20; ++i) {
            CAPTURE(ecc, i);
            const Radians nu{nuMax * static_cast<f64>(i) / 20.0};
            const Radians hyperbolic = trueToEccentricAnomaly(nu, Eccentricity{ecc});
            INFO("true <-> eccentric (hyperbolic)");
            REQUIRE_THAT(wrapPi(eccentricToTrueAnomaly(hyperbolic, Eccentricity{ecc}) - nu).value,
                         WithinAbsOf(0.0, Tolerance{1e-9}));

            const Radians meanAnomaly = eccentricToMeanAnomaly(hyperbolic, Eccentricity{ecc});
            const auto solved = meanToEccentricAnomaly(meanAnomaly, Eccentricity{ecc});
            INFO(errorName(solved));
            REQUIRE(solved.has_value());
            INFO("hyperbolic Kepler round trip");
            REQUIRE_THAT(solved->value, WithinAbsOf(hyperbolic.value, Tolerance{1e-9}));
        }
    }
}

// The contract says failure is reported, never returned as a plausible number.
// A contract is worth exactly as much as its test -- and until this commit,
// propagate() answered a zero-radius state by silently handing the input back.
TEST_CASE("failures are reported, not approximated", "[orbit]") {
    const StateVector atCentre{.pos = {0, 0, 0}, .vel = {1000.0, 0, 0}};
    const auto degenerate = propagate(atCentre, kMuEarth, 60.0_s);
    INFO("a zero-radius state is refused");
    REQUIRE(!degenerate.has_value());
    REQUIRE(degenerate.error() == OrbitError::DegenerateState);

    const auto degenerateElements = elementsFromState(atCentre, kMuEarth);
    INFO("and refused by elementsFromState too");
    REQUIRE(!degenerateElements.has_value());

    const StateVector leo{.pos = {7000e3, 0, 0}, .vel = {0, 7546.0, 0}};
    const auto massless = propagate(leo, GravParam{0.0}, 60.0_s);
    INFO("a massless central body is refused");
    REQUIRE(!massless.has_value());
    REQUIRE(massless.error() == OrbitError::NonPositiveGravity);

    const auto negativeGravity = elementsFromState(leo, GravParam{-1.0});
    INFO("negative gravity is refused");
    REQUIRE(!negativeGravity.has_value());

    // Every error can be explained to a human.
    REQUIRE(!describe(OrbitError::SolverDidNotConverge).empty());
    REQUIRE(!describe(OrbitError::DegenerateState).empty());
    REQUIRE(!describe(OrbitError::NonPositiveGravity).empty());
}
