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
#include <expected>
#include <limits>

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
            .el = makeElements({
                .sma = Metres{kEarthRadius.value() + 500e3},
                .ecc = eccentricity(0.01),
                .inc = Degrees{51.6},
                .lan = Degrees{120.0},
                .aop = Degrees{45.0},
                .tra = Degrees{200.0},
            }),
        },
        {
            .name = "GTO, highly eccentric",
            .el = makeElements({
                .sma = Metres{24582e3},
                .ecc = eccentricity(0.7306),
                .inc = Degrees{28.5},
                .lan = Degrees{10.0},
                .aop = Degrees{178.0},
                .tra = Degrees{30.0},
            }),
        },
        {
            .name = "Polar",
            .el = makeElements({
                .sma = Metres{7200e3},
                .ecc = eccentricity(0.02),
                .inc = Degrees{90.0},
                .lan = Degrees{300.0},
                .aop = Degrees{90.0},
                .tra = Degrees{45.0},
            }),
        },
        {
            .name = "Retrograde",
            .el = makeElements({
                .sma = Metres{8000e3},
                .ecc = eccentricity(0.15),
                .inc = Degrees{145.0},
                .lan = Degrees{200.0},
                .aop = Degrees{320.0},
                .tra = Degrees{275.0},
            }),
        },
    });

    for (const auto& c : cases) {
        CAPTURE(c.name);
        const StateVector sv = stateOf(c.el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        INFO(errorName(back));
        REQUIRE(back.has_value());

        REQUIRE_THAT(back->sma.value(), WithinRelTo(c.el.sma.value(), Tolerance{1e-12}));
        REQUIRE_THAT(back->ecc.value(), WithinAbsOf(c.el.ecc.value(), Tolerance{1e-12}));
        REQUIRE_THAT(wrapPi(back->inc - c.el.inc).value(), WithinAbsOf(0.0, Tolerance{1e-12}));
        REQUIRE_THAT(wrapPi(back->lan - c.el.lan).value(), WithinAbsOf(0.0, Tolerance{1e-12}));
        REQUIRE_THAT(wrapPi(back->aop - c.el.aop).value(), WithinAbsOf(0.0, Tolerance{1e-11}));
        REQUIRE_THAT(wrapPi(back->tra - c.el.tra).value(), WithinAbsOf(0.0, Tolerance{1e-11}));
    }
}

// A circular orbit has no periapsis. The canonical form must fold aop into the
// true anomaly rather than producing NaN, and still reproduce the same state.
TEST_CASE("degenerate orbits stay finite", "[orbit]") {
    SECTION("circular inclined") {
        const Elements el = makeElements({
            .sma = Metres{7000e3},
            .ecc = eccentricity(0.0),
            .inc = Degrees{30.0},
            .lan = Degrees{70.0},
            .aop = Degrees{40.0},
            .tra = Degrees{25.0},
        });
        const StateVector sv = stateOf(el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        INFO(errorName(back));
        REQUIRE(back.has_value());

        INFO("aop folded to zero");
        REQUIRE_THAT(back->aop.value(), WithinAbsOf(0.0, Tolerance{1e-12}));
        // aop + tra is the argument of latitude, and that is preserved.
        INFO("argument of latitude");
        REQUIRE_THAT(wrapPi(back->tra - (el.aop + el.tra)).value(),
                     WithinAbsOf(0.0, Tolerance{1e-10}));
        REQUIRE_THAT(stateOf(*back, kMuEarth).pos, WithinRelVec(sv.pos, Tolerance{1e-12}));
    }

    SECTION("equatorial (geostationary)") {
        const Elements el = makeElements({
            .sma = Metres{42164e3},
            .ecc = eccentricity(0.001),
            .inc = Degrees{0.0},
            .lan = Degrees{0.0},
            .aop = Degrees{60.0},
            .tra = Degrees{15.0},
        });
        const StateVector sv = stateOf(el, kMuEarth);
        const auto back = elementsFromState(sv, kMuEarth);

        INFO(errorName(back));
        REQUIRE(back.has_value());

        INFO("lan folded to zero");
        REQUIRE_THAT(back->lan.value(), WithinAbsOf(0.0, Tolerance{1e-12}));
        REQUIRE_THAT(back->inc.value(), WithinAbsOf(0.0, Tolerance{1e-12}));
        INFO("aop from the x-axis");
        REQUIRE_THAT(wrapPi(back->aop - el.aop).value(), WithinAbsOf(0.0, Tolerance{1e-10}));
        REQUIRE_THAT(stateOf(*back, kMuEarth).pos, WithinRelVec(sv.pos, Tolerance{1e-12}));
    }
}

// Known closed-form values, independent of any of the code under test.
TEST_CASE("known analytic values", "[orbit]") {
    // 400 km circular orbit: period from the elements must match 2*pi*r/v.
    const f64 r = kEarthRadius.value() + 400e3;
    const f64 v = std::sqrt(kMuEarth.value() / r);
    const StateVector sv{.pos = {r, 0, 0}, .vel = {0, v, 0}};
    const auto el = elementsFromState(sv, kMuEarth);

    INFO(errorName(el));
    REQUIRE(el.has_value());

    const OrbitInfo info = orbitInfo(*el, kMuEarth);

    REQUIRE_THAT(el->sma.value(), WithinRelTo(r, Tolerance{1e-12}));
    REQUIRE_THAT(el->ecc.value(), WithinAbsOf(0.0, Tolerance{1e-12}));
    REQUIRE_THAT(info.period.value(), WithinRelTo(kTau * r / v, Tolerance{1e-10}));
    REQUIRE_THAT(info.periapsis.value(), WithinRelTo(r, Tolerance{1e-12}));
    REQUIRE_THAT(info.apoapsis.value(), WithinRelTo(r, Tolerance{1e-12}));
    // Sanity anchor: a 400 km orbit takes a bit over 92 minutes.
    REQUIRE_THAT(info.period.value(), WithinAbsOf(5554.0, Tolerance{5.0}));

    // Vis-viva on an eccentric orbit, checked at periapsis.
    const Elements e2 = makeElements({
        .sma = Metres{10000e3},
        .ecc = eccentricity(0.3),
        .inc = Degrees{20.0},
        .lan = Degrees{0.0},
        .aop = Degrees{0.0},
        .tra = Degrees{0.0},
    });
    const StateVector p = stateOf(e2, kMuEarth);
    const f64 rp = e2.sma.value() * (1.0 - e2.ecc.value());
    const f64 vp = std::sqrt(kMuEarth.value() * ((2.0 / rp) - (1.0 / e2.sma.value())));
    REQUIRE_THAT(length(p.pos).value(), WithinRelTo(rp, Tolerance{1e-12}));
    INFO("periapsis speed (vis-viva)");
    REQUIRE_THAT(length(p.vel).value(), WithinRelTo(vp, Tolerance{1e-12}));
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
            .el = makeElements({
                .sma = Metres{6878e3},
                .ecc = eccentricity(0.001),
                .inc = Degrees{51.6},
                .lan = Degrees{30.0},
                .aop = Degrees{10.0},
                .tra = Degrees{0.0},
            }),
        },
        {
            .name = "GTO",
            .el = makeElements({
                .sma = Metres{24582e3},
                .ecc = eccentricity(0.7306),
                .inc = Degrees{28.5},
                .lan = Degrees{10.0},
                .aop = Degrees{178.0},
                .tra = Degrees{5.0},
            }),
        },
        {
            .name = "very eccentric",
            .el = makeElements({
                .sma = Metres{100000e3},
                .ecc = eccentricity(0.95),
                .inc = Degrees{63.4},
                .lan = Degrees{90.0},
                .aop = Degrees{270.0},
                .tra = Degrees{120.0},
            }),
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
    const Elements el = makeElements({
        .sma = Metres{12000e3},
        .ecc = eccentricity(0.4),
        .inc = Degrees{35.0},
        .lan = Degrees{140.0},
        .aop = Degrees{25.0},
        .tra = Degrees{80.0},
    });
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
    REQUIRE_THAT(far->sma.value(), WithinRelTo(el.sma.value(), Tolerance{1e-9}));
    REQUIRE_THAT(far->ecc.value(), WithinRelTo(el.ecc.value(), Tolerance{1e-9}));
    REQUIRE_THAT(wrapPi(far->inc - el.inc).value(), WithinAbsOf(0.0, Tolerance{1e-9}));

    // Half a period from periapsis lands exactly on apoapsis.
    Elements atPeri = el;
    atPeri.tra = Radians{0.0};
    const auto apo = propagate(stateOf(atPeri, kMuEarth), kMuEarth, info.period * 0.5);
    INFO(errorName(apo));
    REQUIRE(apo.has_value());

    INFO("half a period from periapsis reaches apoapsis");
    REQUIRE_THAT(length(apo->pos).value(), WithinRelTo(info.apoapsis.value(), Tolerance{1e-9}));

    // A quarter period on a circular orbit is a quarter turn.
    const f64 rc = 7500e3;
    const StateVector c0{.pos = {rc, 0, 0}, .vel = {0, std::sqrt(kMuEarth.value() / rc), 0}};
    const auto circular = elementsFromState(c0, kMuEarth);
    INFO(errorName(circular));
    REQUIRE(circular.has_value());

    const OrbitInfo ci = orbitInfo(*circular, kMuEarth);
    const auto c1 = propagate(c0, kMuEarth, ci.period * 0.25);
    INFO(errorName(c1));
    REQUIRE(c1.has_value());

    INFO("a quarter period is a quarter turn");
    REQUIRE_THAT(wrapPi(angleBetween(c0.pos, c1->pos) - Radians{kPi / 2}).value(),
                 WithinAbsOf(0.0, Tolerance{1e-9}));
    REQUIRE_THAT(length(c1->pos).value(), WithinRelTo(rc, Tolerance{1e-12}));
}

// Escape trajectories are not a special case in this code, so they need the
// same coverage as closed orbits.
TEST_CASE("hyperbolic trajectories", "[orbit]") {
    // Departing Earth well above escape speed.
    const f64 r0 = kEarthRadius.value() + 300e3;
    const f64 vEsc = std::sqrt(2.0 * kMuEarth.value() / r0);
    const StateVector sv{.pos = {r0, 0, 0}, .vel = {1200.0, vEsc * 1.15, 0}};

    const auto el = elementsFromState(sv, kMuEarth);
    INFO(errorName(el));
    REQUIRE(el.has_value());

    const OrbitInfo info = orbitInfo(*el, kMuEarth);

    REQUIRE(el->ecc.value() > 1.0);
    REQUIRE(el->sma.value() < 0.0);
    REQUIRE(std::isinf(info.apoapsis.value()));
    REQUIRE(info.energy.value() > 0.0);

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
            const auto solved = meanToEccentricAnomaly(meanAnomaly, eccentricityOf(ecc));
            if (!solved) {
                allSolved = false;
                break;
            }
            const auto backAgain = eccentricToMeanAnomaly(*solved, eccentricityOf(ecc));
            if (!backAgain) {
                allSolved = false;
                break;
            }
            worst = std::max(worst, std::abs(wrapPi(*backAgain - meanAnomaly).value()));
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
            const auto eccAnomaly = trueToEccentricAnomaly(nu, eccentricityOf(ecc));
            REQUIRE(eccAnomaly.has_value());
            const auto back = eccentricToTrueAnomaly(*eccAnomaly, eccentricityOf(ecc));
            REQUIRE(back.has_value());
            INFO("true <-> eccentric (elliptic)");
            REQUIRE_THAT(wrapPi(*back - nu).value(), WithinAbsOf(0.0, Tolerance{1e-10}));
        }
    }

    for (const f64 ecc : {1.2, 2.0, 5.0}) {
        // Stay inside the asymptote, where the true anomaly is reachable.
        const f64 nuMax = std::acos(-1.0 / ecc) * 0.95;
        for (int i = -20; i <= 20; ++i) {
            CAPTURE(ecc, i);
            const Radians nu{nuMax * static_cast<f64>(i) / 20.0};
            const auto hyperbolic = trueToEccentricAnomaly(nu, eccentricityOf(ecc));
            REQUIRE(hyperbolic.has_value());
            const auto back = eccentricToTrueAnomaly(*hyperbolic, eccentricityOf(ecc));
            REQUIRE(back.has_value());
            INFO("true <-> eccentric (hyperbolic)");
            REQUIRE_THAT(wrapPi(*back - nu).value(), WithinAbsOf(0.0, Tolerance{1e-9}));

            const auto meanAnomaly = eccentricToMeanAnomaly(*hyperbolic, eccentricityOf(ecc));
            REQUIRE(meanAnomaly.has_value());
            const auto solved = meanToEccentricAnomaly(*meanAnomaly, eccentricityOf(ecc));
            INFO(errorName(solved));
            REQUIRE(solved.has_value());
            INFO("hyperbolic Kepler round trip");
            REQUIRE_THAT(solved->value(), WithinAbsOf(hyperbolic->value(), Tolerance{1e-9}));
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

    // **Three assertions stood here until M1-87** (register decision 112, with
    // the owner's go-ahead): a massless central body refused as
    // NonPositiveGravity, a negative one refused by elementsFromState, and that
    // error describing itself. None of them can be written now -- GravParam
    // holds mu > 0, so `gravParam(0.0)` and `gravParam(-1.0)` fail the build
    // rather than reaching either function, and the enumerator is gone.
    //
    // What they checked has not stopped being checked. It moved from a runtime
    // report to a type that cannot hold the value, which is VERIFICATION.md
    // rule 24 -- prefer the bug you cannot write -- and the refusals now have
    // their own cases against GravParam::from in test_units_validated.cpp.
    // This case keeps its name, which rule 6 cites, and everything else it did.

    // Every error can be explained to a human.
    REQUIRE(!describe(OrbitError::SolverDidNotConverge).empty());
    REQUIRE(!describe(OrbitError::DegenerateState).empty());
}

// M1-118, finding 1.1: an orbit a hair before periapsis came back with a true
// anomaly of exactly 2 pi, outside the [0, 2 pi) the elements promise. The
// review's state: e = 0.1, at periapsis 7000 km out, and falling inward at
// 1e-13 m/s, so the anomaly is a negative angle far below a double's
// resolution of 2 pi. The nearest angle in range is 0.
TEST_CASE("an anomaly just before periapsis is not 2 pi", "[orbit][regression]") {
    constexpr f64 kPeriapsis = 7000e3;
    constexpr f64 kEcc = 0.1;
    const f64 periapsisSpeed = std::sqrt(kMuEarth.value() * (1.0 + kEcc) / kPeriapsis);
    const StateVector sv{.pos = {kPeriapsis, 0.0, 0.0}, .vel = {-1e-13, periapsisSpeed, 0.0}};

    const auto el = elementsFromState(sv, kMuEarth);
    REQUIRE(el.has_value());
    CAPTURE(el->tra.value());
    REQUIRE(el->tra.value() >= 0.0);
    REQUIRE(el->tra.value() < kTau);
    INFO("and it is the angle just before periapsis, not one a turn away");
    REQUIRE(std::min(el->tra.value(), kTau - el->tra.value()) < 1e-15);
}

// M1-118, finding 1.4: an element set whose anomaly is past a hyperbola's
// asymptote describes no point on the trajectory, and stateFromElements refuses
// it as `UnreachableAnomaly`. propagateElements refused it too, but as
// `NotFinite`, and only because the negative radius led to a NaN that happened
// to spread there: the review measured 6,000 such element sets, all reported
// `NotFinite`, none by name.
namespace {

[[nodiscard]] Elements hyperbolaAt(f64 ecc, Radians anomaly) {
    constexpr f64 kSemiMajorAxis = -1e7; // m, negative on a hyperbola
    return Elements{
        .sma = Metres{kSemiMajorAxis},
        .ecc = Eccentricity::from(ecc).value(),
        .inc = Radians{0.3},
        .lan = Radians{1.0},
        .aop = Radians{2.0},
        .tra = anomaly,
        .slr = Metres{kSemiMajorAxis * (1.0 - (ecc * ecc))},
    };
}

// Time steps of zero, either sign, and several hours.
void requireUnreachableWhenPropagated(const Elements& el) {
    INFO("stateFromElements refuses the same set, so the two agree");
    const auto state = stateFromElements(el, kMuEarth);
    REQUIRE(!state.has_value());
    REQUIRE(state.error() == OrbitError::UnreachableAnomaly);
    for (const f64 dt : std::to_array<f64>({0.0, 60.0, -60.0, 1e4})) {
        const auto moved = propagateElements(el, kMuEarth, Seconds{dt});
        CAPTURE(el.ecc.value(), el.tra.value(), dt);
        REQUIRE(!moved.has_value());
        CAPTURE(describe(moved.error()));
        REQUIRE(moved.error() == OrbitError::UnreachableAnomaly);
    }
}

} // namespace

TEST_CASE("an anomaly past the asymptote is refused by name when propagated",
          "[orbit][regression]") {
    // Eccentricities from just above 1 to 50, three anomalies between the
    // asymptote and the half turn, and both sides of periapsis.
    for (const f64 ecc : std::to_array<f64>({1.01, 1.5, 2.0, 10.0, 50.0})) {
        const f64 asymptote = std::acos(-1.0 / ecc);
        for (const f64 share : std::to_array<f64>({0.01, 0.5, 0.99})) {
            const f64 beyond = asymptote + (share * (kPi - asymptote));
            requireUnreachableWhenPropagated(hyperbolaAt(ecc, Radians{beyond}));
            requireUnreachableWhenPropagated(hyperbolaAt(ecc, Radians{kTau - beyond}));
        }
    }
}

// --- the anomaly converters (M1-118, findings 1.6 and 3.1) -------------------
//
// Finding 1.6: they answered questions that have no answer -- 0 or pi for a
// parabola, a finite H of the wrong sign for an anomaly past the asymptote --
// and register decision 440 made them report instead. Finding 3.1: they were
// tested only against each other, so swapping sqrt(1 - e) and sqrt(1 + e) in
// both passed every suite. They are now checked against the half-angle formula
// and against published worked examples, neither of which is this code.
namespace {

// The converters against the textbook half-angle formula, both in doubles:
// twice the worst difference measured on 2026-10-11 on correct code, over the
// grids below. The review's swapped square roots are 3.05 rad out.
constexpr Tolerance kEllipticTrueToEccentric{1.4e-15};   // rad; measured 6.7e-16
constexpr Tolerance kEllipticEccentricToTrue{9e-16};     // rad; measured 4.4e-16
constexpr Tolerance kHyperbolicTrueToEccentric{5.2e-14}; // relative to max(1, |H|); 2.6e-14
constexpr Tolerance kHyperbolicEccentricToTrue{6.2e-12}; // in cos v; measured 3.1e-12

// What a printed value is worth: half a unit in its last printed digit.
constexpr Tolerance kFiveDecimals{5e-6};
constexpr Tolerance kTwoDecimals{5e-3};
constexpr Tolerance kOneDecimalInTheFourth{5e-5};

[[nodiscard]] Eccentricity eccentricityOrFail(f64 e) {
    const auto ecc = Eccentricity::from(e);
    REQUIRE(ecc.has_value());
    return *ecc;
}

// The error a result carries, read only after asserting there is one: error()
// on a value is undefined behaviour, and the debug library stops the suite on
// it rather than letting the case fail (review finding 6.1's trap).
[[nodiscard]] OrbitError requireRefusal(const std::expected<Radians, OrbitError>& result) {
    CAPTURE(result.has_value() ? result->value() : 0.0);
    REQUIRE(!result.has_value());
    return result.error();
}

[[nodiscard]] Radians requireAnomaly(const std::expected<Radians, OrbitError>& result) {
    INFO(errorName(result));
    REQUIRE(result.has_value());
    return *result;
}

// An eccentricity and an anomaly, named: two adjacent doubles transpose in
// silence, which bugprone-easily-swappable-parameters reports.
struct AnomalyCase {
    f64 ecc{};
    f64 anomaly{};
};

void requireParabolaRefused(Radians anomaly) {
    const Eccentricity parabola = eccentricity(1.0);
    CAPTURE(anomaly.value());
    REQUIRE(requireRefusal(trueToEccentricAnomaly(anomaly, parabola)) ==
            OrbitError::NoEccentricAnomaly);
    REQUIRE(requireRefusal(eccentricToTrueAnomaly(anomaly, parabola)) ==
            OrbitError::NoEccentricAnomaly);
    REQUIRE(requireRefusal(eccentricToMeanAnomaly(anomaly, parabola)) ==
            OrbitError::NoEccentricAnomaly);
    REQUIRE(requireRefusal(meanToEccentricAnomaly(anomaly, parabola)) ==
            OrbitError::NoEccentricAnomaly);
}

void requireNotFiniteRefused(const AnomalyCase& c) {
    CAPTURE(c.ecc, c.anomaly);
    const Eccentricity e = eccentricityOrFail(c.ecc);
    REQUIRE(requireRefusal(trueToEccentricAnomaly(Radians{c.anomaly}, e)) == OrbitError::NotFinite);
    REQUIRE(requireRefusal(eccentricToTrueAnomaly(Radians{c.anomaly}, e)) == OrbitError::NotFinite);
    REQUIRE(requireRefusal(eccentricToMeanAnomaly(Radians{c.anomaly}, e)) == OrbitError::NotFinite);
}

// Past the reach of cosh the answer is just inside the asymptote, on the side
// of H's sign.
void requireJustInsideTheAsymptote(const AnomalyCase& c) {
    const f64 asymptote = std::acos(-1.0 / c.ecc);
    const Radians nu =
        requireAnomaly(eccentricToTrueAnomaly(Radians{c.anomaly}, eccentricityOrFail(c.ecc)));
    CAPTURE(c.ecc, c.anomaly, nu.value(), asymptote);
    REQUIRE(std::abs(nu.value()) <= asymptote);
    REQUIRE(asymptote - std::abs(nu.value()) < 1e-15);
    REQUIRE((nu.value() > 0.0) == (c.anomaly > 0.0));
}

// tan(E/2) = sqrt((1 - e)/(1 + e)) tan(v/2), written out here and not taken
// from the code under test; `anomaly` is used as v one way and as E the other.
void requireEllipticHalfAngle(const AnomalyCase& c) {
    const Eccentricity e = eccentricityOrFail(c.ecc);
    const f64 shrink = std::sqrt((1.0 - c.ecc) / (1.0 + c.ecc));
    CAPTURE(c.ecc, c.anomaly);
    const Radians eccentric = requireAnomaly(trueToEccentricAnomaly(Radians{c.anomaly}, e));
    const f64 eccentricWant = 2.0 * std::atan(shrink * std::tan(c.anomaly / 2.0));
    REQUIRE(std::abs(wrapPi(eccentric - Radians{eccentricWant}).value()) <=
            kEllipticTrueToEccentric.value());
    const Radians trueAnomaly = requireAnomaly(eccentricToTrueAnomaly(Radians{c.anomaly}, e));
    const f64 trueWant = 2.0 * std::atan(std::tan(c.anomaly / 2.0) / shrink);
    REQUIRE(std::abs(wrapPi(trueAnomaly - Radians{trueWant}).value()) <=
            kEllipticEccentricToTrue.value());
}

// tanh(H/2) = sqrt((e - 1)/(e + 1)) tan(v/2), and cos v = (e - cosh H) /
// (e cosh H - 1): the second checks eccentricToTrueAnomaly without the tanh
// form it is now written in.
void requireHyperbolicClosedForms(const AnomalyCase& c) {
    const Eccentricity e = eccentricityOrFail(c.ecc);
    CAPTURE(c.ecc, c.anomaly);
    const Radians h = requireAnomaly(trueToEccentricAnomaly(Radians{c.anomaly}, e));
    const f64 hWant =
        2.0 * std::atanh(std::sqrt((c.ecc - 1.0) / (c.ecc + 1.0)) * std::tan(c.anomaly / 2.0));
    REQUIRE(std::abs(h.value() - hWant) / std::max(1.0, std::abs(hWant)) <=
            kHyperbolicTrueToEccentric.value());
    const Radians back = requireAnomaly(eccentricToTrueAnomaly(h, e));
    const f64 coshH = std::cosh(h.value());
    const f64 cosWant = (c.ecc - coshH) / ((c.ecc * coshH) - 1.0);
    REQUIRE(std::abs(std::cos(back.value()) - cosWant) <= kHyperbolicEccentricToTrue.value());
    REQUIRE((back.value() < 0.0) == (h.value() < 0.0));
}

} // namespace

TEST_CASE("every anomaly converter refuses a parabola by name", "[orbit][anomaly][regression]") {
    for (const f64 anomaly : std::to_array<f64>({0.0, 0.3, 1.0, -2.0})) {
        requireParabolaRefused(Radians{anomaly});
    }
    INFO("and its neighbours either side are an ellipse and a hyperbola, and answer");
    REQUIRE(trueToEccentricAnomaly(Radians{0.3}, eccentricityOrFail(1.0 - 0x1p-53)).has_value());
    REQUIRE(trueToEccentricAnomaly(Radians{0.3}, eccentricityOrFail(1.0 + 0x1p-52)).has_value());
}

TEST_CASE("an anomaly past the asymptote has no eccentric anomaly",
          "[orbit][anomaly][regression]") {
    INFO("the review's case: v = 2.6 at e = 1.5 used to give H = -1.45");
    REQUIRE(requireRefusal(trueToEccentricAnomaly(Radians{2.6}, eccentricity(1.5))) ==
            OrbitError::UnreachableAnomaly);
    for (const f64 ecc : std::to_array<f64>({1.01, 2.0, 50.0})) {
        const f64 asymptote = std::acos(-1.0 / ecc);
        // Just past the asymptote, midway to the half turn, and the same on the
        // other side of periapsis.
        const f64 midway = (asymptote + kPi) / 2.0;
        for (const f64 beyond : std::to_array<f64>({asymptote + 1e-6, midway, kTau - midway})) {
            CAPTURE(ecc, beyond);
            const auto result = trueToEccentricAnomaly(Radians{beyond}, eccentricityOrFail(ecc));
            REQUIRE(requireRefusal(result) == OrbitError::UnreachableAnomaly);
        }
    }
}

TEST_CASE("a hyperbolic anomaly beyond cosh's range converts to just inside the asymptote",
          "[orbit][anomaly][regression]") {
    // The old form overflowed sinh and cosh and returned pi/2 from about
    // H = 1,420 at e = 2, where the answer is just short of 2.0944.
    for (const f64 ecc : std::to_array<f64>({1.01, 2.0, 50.0})) {
        for (const f64 h : std::to_array<f64>({700.0, 1421.0, 2000.0, 1e6, -2000.0})) {
            requireJustInsideTheAsymptote({.ecc = ecc, .anomaly = h});
        }
    }
}

TEST_CASE("a mean anomaly beyond a double is reported, not infinite", "[orbit][anomaly]") {
    INFO("e sinh(H) passes the largest double near H = 710");
    REQUIRE(eccentricToMeanAnomaly(Radians{700.0}, eccentricity(2.0)).has_value());
    REQUIRE(requireRefusal(eccentricToMeanAnomaly(Radians{800.0}, eccentricity(2.0))) ==
            OrbitError::NotFinite);
    REQUIRE(requireRefusal(eccentricToMeanAnomaly(Radians{-800.0}, eccentricity(2.0))) ==
            OrbitError::NotFinite);
}

TEST_CASE("a non-finite anomaly is reported as not finite", "[orbit][anomaly]") {
    constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
    constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();
    for (const f64 ecc : std::to_array<f64>({0.0, 0.5, 2.0})) {
        for (const f64 bad : std::to_array<f64>({kNaN, kInfinity, -kInfinity})) {
            requireNotFiniteRefused({.ecc = ecc, .anomaly = bad});
        }
    }
}

TEST_CASE("the elliptic converters agree with the half-angle formula", "[orbit][anomaly]") {
    for (const f64 ecc : std::to_array<f64>({0.0, 1e-9, 0.1, 0.3, 0.5, 0.85, 0.99, 0.999})) {
        for (int i = 0; i <= 720; ++i) {
            const f64 angle = -kPi + (kTau * static_cast<f64>(i) / 720.0);
            requireEllipticHalfAngle({.ecc = ecc, .anomaly = angle});
        }
    }
}

TEST_CASE("the hyperbolic converters agree with the closed forms", "[orbit][anomaly]") {
    // Out to 0.95 of the asymptote.
    for (const f64 ecc : std::to_array<f64>({1.0001, 1.01, 1.2, 2.0, 5.0, 50.0})) {
        const f64 reach = 0.95 * std::acos(-1.0 / ecc);
        for (int i = -200; i <= 200; ++i) {
            const f64 nu = reach * static_cast<f64>(i) / 200.0;
            requireHyperbolicClosedForms({.ecc = ecc, .anomaly = nu});
        }
    }
}

// Braeunig, *Rocket and Space Technology*, "Orbital Mechanics" problems 4.13
// and 4.14 (http://www.braeunig.us/space/problem.htm, read 2026-10-11): a = 7500
// km, e = 0.1. Printed to five decimals; every number recomputed before it was
// written here (register decision 470).
TEST_CASE("Braeunig's elliptic worked example", "[orbit][anomaly][published]") {
    const Eccentricity e = eccentricity(0.1);
    struct Printed {
        f64 trueDegrees{};
        f64 eccentric{};
        f64 mean{};
    };
    for (const Printed& p : std::to_array<Printed>({
             {.trueDegrees = 30.0, .eccentric = 0.47557, .mean = 0.42978},
             {.trueDegrees = 90.0, .eccentric = 1.47063, .mean = 1.37113},
         })) {
        CAPTURE(p.trueDegrees);
        const Radians eccentric =
            requireAnomaly(trueToEccentricAnomaly(toRadians(Degrees{p.trueDegrees}), e));
        REQUIRE_THAT(eccentric.value(), WithinAbsOf(p.eccentric, kFiveDecimals));
        const Radians mean = requireAnomaly(eccentricToMeanAnomaly(eccentric, e));
        REQUIRE_THAT(mean.value(), WithinAbsOf(p.mean, kFiveDecimals));
    }

    INFO("problem 4.14: M = 2.53755 solves to E = 2.58996, which is v = 2.64034");
    const auto solved = meanToEccentricAnomaly(Radians{2.53755}, e);
    REQUIRE(solved.has_value());
    REQUIRE_THAT(solved->value(), WithinAbsOf(2.58996, kFiveDecimals));
    const Radians later = requireAnomaly(eccentricToTrueAnomaly(Radians{2.58996}, e));
    REQUIRE_THAT(later.value(), WithinAbsOf(2.64034, kFiveDecimals));
}

// Bryan Weber, *Orbital Mechanics & Astrodynamics*, "Example: Elliptical Orbit"
// and "Example: Hyperbolic Trajectory" (https://orbital-mechanics.space,
// time-since-periapsis-and-keplers-equation/, read 2026-10-11) -- Curtis's
// Examples 3.1/3.2 and 3.5, with the page's own inputs. In kilometres and
// seconds, as the page has them; the numbers only are used (decision 470).
constexpr f64 kWeberMu = 3.986004418e5; // km^3/s^2, the page's value

TEST_CASE("Weber's elliptic worked example", "[orbit][anomaly][published]") {
    constexpr f64 kPerigee = 9600.0; // km
    constexpr f64 kApogee = 21000.0; // km
    const f64 ecc = (kApogee - kPerigee) / (kApogee + kPerigee);
    REQUIRE_THAT(ecc, WithinAbsOf(0.3725, kOneDecimalInTheFourth));
    const Eccentricity e = eccentricityOrFail(ecc);

    const Radians eccentric = requireAnomaly(trueToEccentricAnomaly(toRadians(Degrees{120.0}), e));
    REQUIRE_THAT(eccentric.value(), WithinAbsOf(1.73, kTwoDecimals));
    const Radians mean = requireAnomaly(eccentricToMeanAnomaly(eccentric, e));
    REQUIRE_THAT(mean.value(), WithinAbsOf(1.36, kTwoDecimals));

    INFO("three hours after perigee: M = 3.60, E = 3.48, v = 193.16 degrees");
    const f64 sma = (kApogee + kPerigee) / 2.0;
    const f64 period = kTau / std::sqrt(kWeberMu) * std::pow(sma, 1.5);
    const f64 meanLater = kTau * 3.0 * 3600.0 / period;
    REQUIRE_THAT(meanLater, WithinAbsOf(3.60, kTwoDecimals));
    const auto solved = meanToEccentricAnomaly(Radians{meanLater}, e);
    REQUIRE(solved.has_value());
    REQUIRE_THAT(wrapTau(*solved).value(), WithinAbsOf(3.48, kTwoDecimals));
    const Radians later = requireAnomaly(eccentricToTrueAnomaly(*solved, e));
    REQUIRE_THAT(toDegrees(later).value(), WithinAbsOf(193.16, kTwoDecimals));
}

TEST_CASE("Weber's hyperbolic worked example", "[orbit][anomaly][published]") {
    constexpr f64 kPerigee = 300.0 + 6378.1; // km
    constexpr f64 kPerigeeSpeed = 15.0;      // km/s
    const f64 h = kPerigee * kPerigeeSpeed;  // km^2/s
    const f64 ecc = (h * h / (kPerigee * kWeberMu)) - 1.0;
    REQUIRE_THAT(ecc, WithinAbsOf(2.7696, kOneDecimalInTheFourth));
    REQUIRE_THAT(toDegrees(Radians{std::acos(-1.0 / ecc)}).value(),
                 WithinAbsOf(111.17, kTwoDecimals));
    const Eccentricity e = eccentricityOrFail(ecc);
    // Mean anomaly per second: mu^2 / h^3 (e^2 - 1)^(3/2), the page's Eq. (225).
    const f64 meanRate = kWeberMu * kWeberMu / (h * h * h) * std::pow((ecc * ecc) - 1.0, 1.5);

    INFO("from perigee to 100 degrees takes 1.15 h");
    const Radians f1 = requireAnomaly(trueToEccentricAnomaly(toRadians(Degrees{100.0}), e));
    const Radians m1 = requireAnomaly(eccentricToMeanAnomaly(f1, e));
    const f64 t1 = m1.value() / meanRate;
    REQUIRE_THAT(t1 / 3600.0, WithinAbsOf(1.15, kTwoDecimals));

    INFO("three hours later the true anomaly is 107.78 degrees");
    const auto f2 = meanToEccentricAnomaly(Radians{meanRate * (t1 + (3.0 * 3600.0))}, e);
    REQUIRE(f2.has_value());
    const Radians nu2 = requireAnomaly(eccentricToTrueAnomaly(*f2, e));
    REQUIRE_THAT(toDegrees(nu2).value(), WithinAbsOf(107.78, kTwoDecimals));
}

// Register decision 478: an element set with no positive, finite semi-latus
// rectum is something a scenario file can hold, so stateFromElements reports it
// as propagateElements does, rather than asserting. The assertion aborted a
// Debug build; a release build reached the same NotFinite by way of a NaN.
TEST_CASE("an element set with no usable shape is reported, not asserted", "[orbit][regression]") {
    constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
    struct Shape {
        const char* name{};
        f64 sma{};
        f64 slr{};
    };
    for (const Shape& s : std::to_array<Shape>({
             {.name = "negative: a hyperbola's sma with an ellipse's e", .sma = -1e7, .slr = -1.0},
             {.name = "zero", .sma = 0.0, .slr = 0.0},
             {.name = "NaN", .sma = kNaN, .slr = kNaN},
         })) {
        const Elements el{
            .sma = Metres{s.sma},
            .ecc = eccentricity(0.5),
            .inc = Radians{0.3},
            .lan = Radians{1.0},
            .aop = Radians{2.0},
            .tra = Radians{0.5},
            .slr = Metres{s.slr},
        };
        CAPTURE(s.name);
        const auto state = stateFromElements(el, kMuEarth);
        REQUIRE(!state.has_value());
        REQUIRE(state.error() == OrbitError::NotFinite);
        INFO("and propagateElements says the same");
        const auto moved = propagateElements(el, kMuEarth, Seconds{60.0});
        REQUIRE(!moved.has_value());
        REQUIRE(moved.error() == OrbitError::NotFinite);
    }
}
