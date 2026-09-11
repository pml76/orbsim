//
// The two-body core at scales the Earth-orbit suite never reaches, and a
// randomised sweep across all of them.
//
// Why this file exists: propagate() converged for every orbit in
// test_orbit.cpp and failed for a circular orbit at 1 AU, because its
// convergence tolerance was an absolute number in sqrt(metres) and its conic
// thresholds were in 1/metres. No suite built from Earth orbits could see
// either. Everything here is a scale the simulator will actually fly -- the
// Moon, Earth, Jupiter and the Sun as central bodies -- and the sweep checks
// each case against the constants of motion rather than against the code.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <string_view>

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

namespace {

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();

} // namespace

// Circular heliocentric orbits at the distances of Earth, Jupiter and Neptune.
// 1/a spans 6.7e-12 to 2.2e-13 per metre here, which is exactly the range the
// old 1e-12 threshold cut through.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("heliocentric circular orbits", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        Metres radius;
    };
    constexpr std::array kCases = std::to_array<Case>({
        {.name = "Earth distance", .radius = Metres{1.496e11}},
        {.name = "Jupiter distance", .radius = Metres{7.785e11}},
        {.name = "Neptune distance", .radius = Metres{4.495e12}},
    });

    for (const auto& c : kCases) {
        CAPTURE(c.name);
        const StateVector sv0 = circularState(kMuSun, c.radius);
        const Seconds period{kTau * c.radius.value / length(sv0.vel)};

        const auto quarter = propagate(sv0, kMuSun, period * 0.25);
        INFO(errorName(quarter));
        REQUIRE(quarter.has_value());

        INFO("a quarter period is a quarter turn");
        REQUIRE_THAT(wrapPi(angleBetween(sv0.pos, quarter->pos) - Radians{kPi / 2}).value,
                     WithinAbsOf(0.0, Tolerance{1e-9}));
        REQUIRE_THAT(length(quarter->pos), WithinRelTo(c.radius.value, Tolerance{1e-12}));

        const auto whole = propagate(sv0, kMuSun, period);
        INFO(errorName(whole));
        REQUIRE(whole.has_value());
        INFO("one period returns to the start");
        REQUIRE_THAT(whole->pos, WithinRelVec(sv0.pos, Tolerance{1e-9}));

        // Half a turn after many whole ones: the fold and the solve together.
        const auto many = propagate(sv0, kMuSun, period * 500.5);
        INFO(errorName(many));
        REQUIRE(many.has_value());
        INFO("500.5 periods is a half turn");
        REQUIRE_THAT(wrapPi(angleBetween(sv0.pos, many->pos) - Radians{kPi}).value,
                     WithinAbsOf(0.0, Tolerance{1e-8}));

        // The element propagator shares no code with the universal-variable
        // one; agreement out here is the same evidence it is in Earth orbit.
        const auto el = elementsFromState(sv0, kMuSun);
        INFO(errorName(el));
        REQUIRE(el.has_value());

        const auto viaUniversal = propagate(sv0, kMuSun, period * 0.3);
        const auto viaElements = propagateElements(*el, kMuSun, period * 0.3);
        INFO(errorName(viaUniversal));
        REQUIRE(viaUniversal.has_value());
        INFO(errorName(viaElements));
        REQUIRE(viaElements.has_value());

        INFO("the propagators agree");
        REQUIRE_THAT(viaUniversal->pos,
                     WithinRelVec(stateFromElements(*viaElements, kMuSun).pos, Tolerance{1e-9}));
    }
}

// Exactly escape speed: the conic the universal-variable formulation was
// chosen to make ordinary.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("parabolic trajectories", "[orbit][scales]") {
    const f64 r0 = 7.0e6;
    const StateVector para{
        .pos = {r0, 0.0, 0.0},
        .vel = {0.0, std::sqrt(2.0 * kMuEarth.value / r0), 0.0},
    };

    const auto el = elementsFromState(para, kMuEarth);
    INFO(errorName(el));
    REQUIRE(el.has_value());

    REQUIRE_THAT(el->ecc.value, WithinAbsOf(1.0, Tolerance{1e-12}));
    INFO("the semi-major axis is infinite");
    REQUIRE(std::isinf(el->sma.value));
    INFO("the semi-latus rectum is 2 r0");
    REQUIRE_THAT(el->slr.value, WithinRelTo(2.0 * r0, Tolerance{1e-12}));

    const OrbitInfo info = orbitInfo(*el, kMuEarth);
    REQUIRE(!info.closed);
    INFO("the period is infinite");
    REQUIRE(std::isinf(info.period.value));
    INFO("the energy is zero");
    REQUIRE_THAT(info.energy.value, WithinAbsOf(0.0, Tolerance{1e-6 * kMuEarth.value / r0}));

    // The trajectory is time-reversible and conserves energy at every dt,
    // including zero, where the Barker starting guess divides by the time.
    for (const f64 dt : {0.0, 1.0, 100.0, 3600.0, 1.0e6}) {
        CAPTURE(dt);
        const auto fwd = propagate(para, kMuEarth, Seconds{dt});
        INFO(errorName(fwd));
        REQUIRE(fwd.has_value());

        INFO("the position stays finite");
        REQUIRE(std::isfinite(length(fwd->pos)));
        INFO("the energy stays zero");
        REQUIRE_THAT(specificEnergy(*fwd, kMuEarth).value,
                     WithinAbsOf(0.0, Tolerance{1e-9 * kMuEarth.value / r0}));

        const auto back = propagate(*fwd, kMuEarth, Seconds{-dt});
        INFO(errorName(back));
        REQUIRE(back.has_value());

        INFO("forward then back");
        REQUIRE_THAT(back->pos, WithinRelVec(para.pos, Tolerance{1e-9}));
        REQUIRE_THAT(back->vel, WithinRelVec(para.vel, Tolerance{1e-9}));
    }

    // Element propagation needs a finite semi-major axis and says so, instead
    // of feeding an infinity into the Kepler solver and reporting that it
    // "did not converge".
    const auto viaElements = propagateElements(*el, kMuEarth, 100.0_s);
    const bool refusedAsParabolic =
        !viaElements.has_value() && viaElements.error() == OrbitError::ParabolicElements;
    INFO("propagateElements refuses parabolic elements, specifically -> "
         << errorName(viaElements));
    REQUIRE(refusedAsParabolic);
}

// NaN and infinity are what a corrupt scenario file produces. They must be
// refused by name, not reported as a solver that failed to converge.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("non-finite inputs are refused by name", "[orbit][scales]") {
    const StateVector leo{.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 7546.0, 0.0}};

    const auto nanPosition = elementsFromState({.pos = {kNaN, 0.0, 0.0}, .vel = leo.vel}, kMuEarth);
    const bool nanPositionRefused =
        !nanPosition.has_value() && nanPosition.error() == OrbitError::NotFinite;
    INFO("NaN position -> " << errorName(nanPosition));
    REQUIRE(nanPositionRefused);

    const auto infVelocity = propagate({.pos = leo.pos, .vel = {0.0, kInf, 0.0}}, kMuEarth, 60.0_s);
    const bool infVelocityRefused =
        !infVelocity.has_value() && infVelocity.error() == OrbitError::NotFinite;
    INFO("infinite velocity -> " << errorName(infVelocity));
    REQUIRE(infVelocityRefused);

    const auto nanTime = propagate(leo, kMuEarth, Seconds{kNaN});
    const bool nanTimeRefused = !nanTime.has_value() && nanTime.error() == OrbitError::NotFinite;
    INFO("NaN time step -> " << errorName(nanTime));
    REQUIRE(nanTimeRefused);

    const auto nanGravity = propagate(leo, GravParam{kNaN}, 60.0_s);
    const bool nanGravityRefused =
        !nanGravity.has_value() && nanGravity.error() == OrbitError::NotFinite;
    INFO("NaN gravitational parameter -> " << errorName(nanGravity));
    REQUIRE(nanGravityRefused);

    const auto el = elementsFromState(leo, kMuEarth);
    INFO(errorName(el));
    REQUIRE(el.has_value());

    const auto infTime = propagateElements(*el, kMuEarth, Seconds{kInf});
    const bool infTimeRefused = !infTime.has_value() && infTime.error() == OrbitError::NotFinite;
    INFO("infinite time step for propagateElements -> " << errorName(infTime));
    REQUIRE(infTimeRefused);

    // A finite but zero time step is ordinary, and returns the input exactly.
    const auto still = propagate(leo, kMuEarth, 0.0_s);
    INFO(errorName(still));
    REQUIRE(still.has_value());
    INFO("a zero time step is the identity");
    REQUIRE_THAT(still->pos, WithinRelVec(leo.pos, Tolerance{0.0}));

    REQUIRE(!describe(OrbitError::NotFinite).empty());
    REQUIRE(!describe(OrbitError::ParabolicElements).empty());
    REQUIRE(!describe(OrbitError::RectilinearOrbit).empty());
}

// States that pass every input check and still do not describe an orbit. Both
// of these came from the fuzzer rather than from anyone sitting down to think
// of them, which is the argument for rule 13 in one paragraph.
TEST_CASE("states that are finite but are not orbits", "[orbit][scales]") {
    // Finite components whose magnitude is not. Squaring overflows above about
    // 1.3e154, so lengthSq() reaches infinity from inputs that every isfinite()
    // check passes -- and `rmag > 0.0` is then true of infinity, so the
    // precondition let it through. A fuzzer found this (VERIFICATION.md rule
    // 13): the elements came back reporting success, with an infinite
    // eccentricity and a NaN argument of periapsis.
    const StateVector overflowing{
        .pos = {-5.486124068796807e303, 0.0, 0.0},
        .vel = {0.0, 7.418412301374917e-68, 0.0},
    };
    const auto overflowElements = elementsFromState(overflowing, kMuEarth);
    const bool overflowElementsRefused =
        !overflowElements.has_value() && overflowElements.error() == OrbitError::NotFinite;
    INFO("a position whose magnitude overflows -> " << errorName(overflowElements));
    REQUIRE(overflowElementsRefused);

    const auto overflowPropagate = propagate(overflowing, kMuEarth, 60.0_s);
    const bool overflowPropagateRefused =
        !overflowPropagate.has_value() && overflowPropagate.error() == OrbitError::NotFinite;
    INFO("propagate refuses the same state -> " << errorName(overflowPropagate));
    REQUIRE(overflowPropagateRefused);

    // The velocity side of the same hole.
    const auto overflowSpeed =
        elementsFromState({.pos = {7000e3, 0.0, 0.0}, .vel = {1e200, 1e200, 0.0}}, kMuEarth);
    const bool overflowSpeedRefused =
        !overflowSpeed.has_value() && overflowSpeed.error() == OrbitError::NotFinite;
    INFO("a velocity whose magnitude overflows -> " << errorName(overflowSpeed));
    REQUIRE(overflowSpeedRefused);
}

// The other half: states with no orbital plane at all. Both of these came from
// the fuzzer rather than from anyone sitting down to think of them, which is
// the argument for rule 13 in one paragraph.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("states with no orbital plane", "[orbit][scales]") {
    // A radial trajectory: velocity parallel to position, so the specific
    // angular momentum is zero and there is no orbital plane to incline. The
    // inclination was acos(h.z / |h|) = acos(0/0) = NaN, returned as a success.
    // Physically reachable -- a probe released with no horizontal velocity
    // falls straight down -- and found by the fuzzer, not by anyone's
    // imagination (VERIFICATION.md rule 13).
    const auto straightDown =
        elementsFromState({.pos = {7000e3, 0.0, 0.0}, .vel = {-100.0, 0.0, 0.0}}, kMuEarth);
    const bool straightDownRefused =
        !straightDown.has_value() && straightDown.error() == OrbitError::RectilinearOrbit;
    INFO("a radial trajectory has no orbital plane -> " << errorName(straightDown));
    REQUIRE(straightDownRefused);

    const auto atRest =
        elementsFromState({.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 0.0, 0.0}}, kMuEarth);
    const bool atRestRefused =
        !atRest.has_value() && atRest.error() == OrbitError::RectilinearOrbit;
    INFO("a body at rest likewise -> " << errorName(atRest));
    REQUIRE(atRestRefused);

    // Magnitudes finite, mu positive, trajectory not radial -- and the derived
    // elements still overflow, because mu is tiny relative to the state and the
    // eccentricity vector divides by it. The elements came back reporting
    // success with an infinite eccentricity and NaN in-plane angles. The third
    // thing the fuzzer found, and the one that argued for checking the answer
    // rather than adding another input guard.
    const auto tinyMu = elementsFromState(
        {.pos = {1.0e120, 0.0, 0.0}, .vel = {9.68e119, 1.0e118, 0.0}}, GravParam{6.8e-231});
    const bool tinyMuRefused = !tinyMu.has_value() && tinyMu.error() == OrbitError::NotFinite;
    INFO("elements that overflow are not returned as a success -> " << errorName(tinyMu));
    REQUIRE(tinyMuRefused);

    // propagate's postcondition used to be an assertion, so a Debug build
    // aborted the process on this input instead of reporting it -- the wrong
    // half of the ADR 0002 split, since a caller can produce it. An enormous
    // speed with a matching mu makes the Lagrange combination overflow.
    const StateVector violent{
        .pos = {1.5419835033e-313, 7.477078763343729e20, 4.483094976257099e-120},
        .vel = {4.483094640249093e-120, 1.3792778605844018e40, 7.477080264543605e20},
    };
    const auto overflowed = propagate(violent, GravParam{7.477080264551322e20}, 0.0_s);
    const bool reportedRatherThanAsserted = overflowed.has_value() ||
                                            overflowed.error() == OrbitError::NotFinite ||
                                            overflowed.error() == OrbitError::DegenerateState;
    INFO("propagate reports rather than asserting when the result overflows -> "
         << errorName(overflowed));
    REQUIRE(reportedRatherThanAsserted);

    // |h| is nonzero but |h|^2 underflows, so the semi-latus rectum is zero and
    // orbitInfo's radius became slr / (1 + e cos v) = 0/0. The ratio test above
    // cannot see this one, because it never squares anything.
    const auto underflowedSlr = elementsFromState(
        {
            .pos = {-7.8804e115, -4.62693e-179, -1.60283e-180},
            .vel = {-1.60283e-180, -1.60283e-180, -1.60283e-180},
        },
        GravParam{3.01352e296});
    const bool underflowedSlrRefused =
        !underflowedSlr.has_value() && underflowedSlr.error() == OrbitError::RectilinearOrbit;
    INFO("a semi-latus rectum that underflows is not an orbit -> " << errorName(underflowedSlr));
    REQUIRE(underflowedSlrRefused);

    // But a genuinely eccentric orbit is not rectilinear, however thin it is.
    const Elements thin =
        makeElements(Metres{2.0e7}, Eccentricity{0.9999}, 45.0_deg, 0.0_deg, 0.0_deg, 90.0_deg);
    const auto stillAnOrbit = elementsFromState(stateFromElements(thin, kMuEarth), kMuEarth);
    INFO("e = 0.9999 is still an orbit -> " << errorName(stillAnOrbit));
    REQUIRE(stillAnOrbit.has_value());
}

// A zero time step is the identity, on every conic and not just on the one the
// suite happened to reach. VERIFICATION.md rule 5: the singular cases are where
// a formula divides by, or takes the log of, something that just became zero,
// and a randomised sweep visits them with probability zero.
//
// The elliptic case was tested above and always passed. The hyperbolic one did
// not: the hyperbolic starting guess takes the log of a quantity proportional
// to dt, and log(0) is -infinity, so chi began at -infinity, the Stumpff series
// produced NaN from it, and Newton spent its whole iteration budget on NaN
// before reporting that it had not converged.
TEST_CASE("a zero time step is the identity on every conic", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        GravParam mu;
        StateVector state;
    };

    constexpr f64 kLeoRadius = 7000e3;
    const f64 escapeSpeed = std::sqrt(2.0 * kMuEarth.value / kLeoRadius);

    // 1 AU, so the hyperbolic branch is exercised at two scales: a conic
    // threshold and a starting guess can each be right at one and wrong at the
    // other, which is the lesson this whole file exists to record.
    constexpr f64 kAuRadius = 1.496e11;
    const f64 solarEscape = std::sqrt(2.0 * kMuSun.value / kAuRadius);

    const std::array kCases = std::to_array<Case>({
        {
            .name = "ellipse, LEO",
            .mu = kMuEarth,
            .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, 7546.0, 0.0}},
        },
        {
            .name = "parabola, LEO",
            .mu = kMuEarth,
            .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, escapeSpeed, 0.0}},
        },
        {
            .name = "hyperbola, LEO",
            .mu = kMuEarth,
            .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, 12000.0, 0.0}},
        },
        {
            .name = "hyperbola, 1 AU",
            .mu = kMuSun,
            .state = {.pos = {kAuRadius, 0.0, 0.0}, .vel = {0.0, solarEscape * 1.2, 0.0}},
        },
        {
            .name = "retrograde hyperbola, LEO",
            .mu = kMuEarth,
            .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, -12000.0, 0.0}},
        },
    });

    for (const Case& c : kCases) {
        CAPTURE(c.name);
        const auto still = propagate(c.state, c.mu, 0.0_s);
        INFO(errorName(still));
        REQUIRE(still.has_value());
        // Exactly, not nearly: the identity is the claim, so any tolerance at
        // all would hide the difference between "returned the input" and
        // "integrated to something indistinguishable from it".
        REQUIRE_THAT(still->pos, WithinRelVec(c.state.pos, Tolerance{0.0}));
        REQUIRE_THAT(still->vel, WithinRelVec(c.state.vel, Tolerance{0.0}));
    }
}

// propagate(s, a + b) must agree with propagate(propagate(s, a), b). This is a
// property rather than an example: it holds for every state and every pair of
// steps, it says nothing about what the right answer is, and it therefore
// cannot be satisfied by a propagator that is consistently wrong in one
// direction. VERIFICATION.md rule 11.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("propagation composes: one long step equals two short ones", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        GravParam mu;
        StateVector state;
        Seconds first;
        Seconds second;
    };

    const std::array kCases = std::to_array<Case>({
        {
            .name = "LEO, 600 s + 900 s",
            .mu = kMuEarth,
            .state = circularState(kMuEarth, Metres{7000e3}),
            .first = 600.0_s,
            .second = 900.0_s,
        },
        {
            .name = "LEO, forward then backward",
            .mu = kMuEarth,
            .state = circularState(kMuEarth, Metres{7000e3}),
            .first = 4000.0_s,
            .second = -1500.0_s,
        },
        {
            .name = "lunar orbit, two half days",
            .mu = kMuMoon,
            .state = circularState(kMuMoon, Metres{2000e3}),
            .first = 43200.0_s,
            .second = 43200.0_s,
        },
        {
            .name = "1 AU, two quarter years",
            .mu = kMuSun,
            .state = circularState(kMuSun, Metres{1.496e11}),
            .first = Seconds{7.9e6},
            .second = Seconds{7.9e6},
        },
        {
            .name = "hyperbolic escape, 100 s + 250 s",
            .mu = kMuEarth,
            .state = {.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 12000.0, 0.0}},
            .first = 100.0_s,
            .second = 250.0_s,
        },
    });

    for (const Case& c : kCases) {
        CAPTURE(c.name);
        const auto together = propagate(c.state, c.mu, c.first + c.second);
        const auto once = propagate(c.state, c.mu, c.first);
        INFO(errorName(together));
        REQUIRE(together.has_value());
        INFO(errorName(once));
        REQUIRE(once.has_value());

        const auto twice = propagate(*once, c.mu, c.second);
        INFO(errorName(twice));
        REQUIRE(twice.has_value());

        REQUIRE_THAT(twice->pos, WithinRelVec(together->pos, Tolerance{1e-9}));
        REQUIRE_THAT(twice->vel, WithinRelVec(together->vel, Tolerance{1e-9}));
    }
}

// Two-body motion has an exact scaling symmetry. Multiply every length by
// lambda, divide every speed by sqrt(lambda) and multiply every duration by
// lambda^(3/2), and the trajectory maps onto itself: the equation of motion
// r'' = -mu*r/|r|^3 is invariant under it for fixed mu.
//
// Nothing in Orbit.cpp knows that, which is what makes this a check rather than
// a tautology. It is also the property that would have found the 1 AU
// convergence bug in one line, because it relates a low Earth orbit to a
// heliocentric-sized one directly -- an absolute tolerance in sqrt(metres)
// cannot survive it. VERIFICATION.md rule 11.
TEST_CASE("two-body motion is invariant under canonical rescaling", "[orbit][scales]") {
    const StateVector base = circularState(kMuEarth, Metres{7000e3});
    constexpr Seconds kStep{1800.0};

    // Spanning ten orders of magnitude in length, which is the range from a low
    // orbit to the outer solar system.
    constexpr std::array kLambdas = std::to_array<f64>({1e-3, 1.0, 1e2, 1e4, 1e7, 1e10});

    for (const f64 lambda : kLambdas) {
        CAPTURE(lambda);
        const f64 speedScale = 1.0 / std::sqrt(lambda);
        const f64 timeScale = lambda * std::sqrt(lambda);

        const StateVector scaled{.pos = base.pos * lambda, .vel = base.vel * speedScale};

        const auto plain = propagate(base, kMuEarth, kStep);
        const auto rescaled = propagate(scaled, kMuEarth, Seconds{kStep.value * timeScale});
        INFO("unscaled propagation -> " << errorName(plain));
        REQUIRE(plain.has_value());
        INFO("rescaled propagation -> " << errorName(rescaled));
        REQUIRE(rescaled.has_value());

        INFO("the position scales");
        REQUIRE_THAT(rescaled->pos, WithinRelVec(plain->pos * lambda, Tolerance{1e-9}));
        INFO("the velocity scales");
        REQUIRE_THAT(rescaled->vel, WithinRelVec(plain->vel * speedScale, Tolerance{1e-9}));
    }
}

namespace {

// Energy and angular momentum before and after a step, against budgets that
// scale with the conic's conditioning.
//
// A fixed tolerance is the wrong shape here: it is far too loose to mean
// anything at e = 0.9 and too tight to pass at e = 0.99999. The scaling is
// 1/(1-e) -- the same geometry that makes the round trip harder -- and the
// constants come from measurement rather than from what happened to pass.
// Drift multiplied by (1-e), which is flat if the law is right:
//
//     e          energy      angular momentum
//     0.9        7.5e-17     1.5e-15
//     0.99       2.1e-16     8.7e-16
//     0.999      1.8e-17     1.7e-15
//     0.9999     6.2e-17     8.2e-16
//     0.99999    8.6e-17     1.4e-14
//
// 2e-15 and 5e-14 leave between three and thirty times headroom over the worst
// measured ratio. Both are around six orders *tighter* than the fixed 1e-9
// they replace, everywhere below e = 0.9999 -- so this is a stricter test than
// it was, not a relaxed one.
void checkConserved(const StateVector& before, const StateVector& after, f64 e) {
    const Tolerance energyBudget{2e-15 / (1.0 - e)};
    const Tolerance momentumBudget{5e-14 / (1.0 - e)};

    INFO("energy conserved");
    REQUIRE_THAT(specificEnergy(after, kMuEarth).value,
                 WithinRelTo(specificEnergy(before, kMuEarth).value, energyBudget));
    INFO("angular momentum conserved");
    REQUIRE_THAT(specificAngularMomentum(after),
                 WithinRelVec(specificAngularMomentum(before), momentumBudget));
}

void checkOneEccentricity(f64 e) {
    CAPTURE(e);
    constexpr Metres kSemiMajor{2.0e7};
    const Elements el =
        makeElements(kSemiMajor, Eccentricity{e}, 45.0_deg, 30.0_deg, 60.0_deg, 10.0_deg);
    const StateVector sv = stateFromElements(el, kMuEarth);
    const OrbitInfo info = orbitInfo(el, kMuEarth);

    // A quarter period is enough to cross the fast part of the orbit.
    const Seconds step{info.period.value * 0.25};

    const auto moved = propagate(sv, kMuEarth, step);
    INFO("propagate near-rectilinear -> " << errorName(moved));
    REQUIRE(moved.has_value());

    checkConserved(sv, *moved, e);

    // The round trip crosses periapsis, where a near-rectilinear orbit is
    // worst conditioned, so a fixed tolerance would either pass everything
    // or fail the extreme case for a reason that is arithmetic rather than
    // a defect. The budget is therefore stated as the conditioning law the
    // error is bounded by, measured rather than guessed. With the
    // safeguarded solver, across six decades of (1 - e):
    //
    //     e          rel err     err / [5e-15/(1-e)^2]
    //     0.9        1.1e-13     0.22
    //     0.99       3.7e-12     0.08
    //     0.999      3.4e-10     0.07
    //     0.9999     1.3e-08     0.03
    //     0.99999    2.0e-05     0.39
    //     0.999999   4.9e-03     0.97
    //
    // The ratio stays below one throughout, so this is the conic's
    // conditioning showing through and not the solver giving up: the error
    // grows as 1/(1-e)^2 because that is how the geometry conditions, and
    // the method tracks it rather than adding to it.
    const Tolerance roundTrip{5e-15 / ((1.0 - e) * (1.0 - e))};

    // The backward step is the one that used to fail. A closed-form
    // solution exists for every valid two-body input, so there is no
    // eccentricity at which "did not converge" is an acceptable answer --
    // it was a weakness of the solver, not a property of the problem, and
    // the solver is safeguarded now. See solveUniversalAnomaly.
    const auto back = propagate(*moved, kMuEarth, -step);
    INFO("propagate back -> " << errorName(back));
    REQUIRE(back.has_value());
    INFO("round trip");
    REQUIRE_THAT(back->pos, WithinRelVec(sv.pos, roundTrip));

    // The Kepler solver on its own, at the eccentricity that breaks the
    // naive starting guess.
    const Radians ecc = trueToEccentricAnomaly(el.tra, el.ecc);
    const Radians mean = eccentricToMeanAnomaly(ecc, el.ecc);
    const auto solved = meanToEccentricAnomaly(mean, el.ecc);
    INFO("Kepler solver converges -> " << errorName(solved));
    REQUIRE(solved.has_value());
    INFO("solver round trip");
    REQUIRE_THAT(wrapPi(*solved - ecc).value, WithinAbsOf(0.0, Tolerance{1e-9}));
}

} // namespace

// Near-rectilinear orbits: eccentricity approaching 1 with the ellipse
// collapsing toward a straight line through the focus. The remaining case from
// VERIFICATION.md rule 5's list, and the hardest one for a Kepler solver --
// almost all of the mean anomaly is spent near periapsis, which is why
// meanToEccentricAnomaly switches its starting guess at e = 0.8.
TEST_CASE("near-rectilinear orbits, e approaching 1", "[orbit][scales]") {
    // 0.9999 and 0.99999 are the regression cases. Plain Newton failed to
    // converge on the backward step at both -- under gcc-14 and clang-on-Linux
    // but not under Windows clang, which is how it was exposed. The
    // safeguarded solver answers for every one of them, on all three
    // toolchains, and each check below carries a budget scaled by the conic's
    // own conditioning rather than a single number that has to serve four
    // decades of (1 - e).
    constexpr std::array kEccentricities = std::to_array<f64>({0.9, 0.99, 0.999, 0.9999, 0.99999});
    for (const f64 e : kEccentricities) {
        checkOneEccentricity(e);
    }
}

// The same inputs must produce bit-identical outputs, every time.
//
// VERIFICATION.md rule 16, and the one place this codebase compares floating
// point exactly: bit identity is the actual claim, and a tolerance here would
// hide precisely the drift being tested for. It says so by name, through
// bitIdentical(), which also tells +0.0 from -0.0; CAPTURE prints both sides
// of a failure, round-trippably. It catches a class of accidental
// nondeterminism that no other test can see -- iteration over an unordered
// container, uninitialised padding, a branch on wall-clock time, a solver that
// reads a global. None of those exist today, which is the point: this test is
// what notices when one arrives.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("propagation is bit-identical across runs", "[orbit][scales]") {
    const StateVector start = circularState(kMuEarth, Metres{7000e3});

    // A single step, twice.
    const auto first = propagate(start, kMuEarth, 1234.5_s);
    const auto second = propagate(start, kMuEarth, 1234.5_s);
    INFO("first run -> " << errorName(first));
    REQUIRE(first.has_value());
    INFO("second run -> " << errorName(second));
    REQUIRE(second.has_value());

    CAPTURE(first->pos, second->pos);
    INFO("single step: position bit-identical");
    REQUIRE(first->pos.bitIdentical(second->pos));
    CAPTURE(first->vel, second->vel);
    INFO("single step: velocity bit-identical");
    REQUIRE(first->vel.bitIdentical(second->vel));

    // A long chain, where any drift would compound rather than cancel. A
    // failed step ends the chain at the zero state, which two runs that agree
    // reach together; the one variable returned keeps the copy elided.
    const auto chain = [&] -> StateVector {
        StateVector s = start;
        for (int i = 0; i < 100; ++i) {
            const auto next = propagate(s, kMuEarth, 60.0_s);
            if (!next) {
                s = StateVector{};
                break;
            }
            s = *next;
        }
        return s;
    };
    const StateVector chainA = chain();
    const StateVector chainB = chain();
    CAPTURE(chainA.pos, chainB.pos);
    INFO("100 steps: position bit-identical");
    REQUIRE(chainA.pos.bitIdentical(chainB.pos));
    CAPTURE(chainA.vel, chainB.vel);
    INFO("100 steps: velocity bit-identical");
    REQUIRE(chainA.vel.bitIdentical(chainB.vel));

    // The conversions too, since a scenario reload goes through them.
    const auto elA = elementsFromState(start, kMuEarth);
    const auto elB = elementsFromState(start, kMuEarth);
    INFO("elements first -> " << errorName(elA));
    REQUIRE(elA.has_value());
    INFO("elements second -> " << errorName(elB));
    REQUIRE(elB.has_value());

    const bool elementsIdentical =
        elA->sma.bitIdentical(elB->sma) && elA->ecc.bitIdentical(elB->ecc) &&
        elA->inc.bitIdentical(elB->inc) && elA->lan.bitIdentical(elB->lan) &&
        elA->aop.bitIdentical(elB->aop) && elA->tra.bitIdentical(elB->tra);
    INFO("elementsFromState is bit-identical");
    REQUIRE(elementsIdentical);
    const Vec3 backA = stateFromElements(*elA, kMuEarth).pos;
    const Vec3 backB = stateFromElements(*elB, kMuEarth).pos;
    CAPTURE(backA, backB);
    INFO("stateFromElements is bit-identical");
    REQUIRE(backA.bitIdentical(backB));
}

namespace {

// One body's worth of the sweep's parameter space.
struct Body {
    std::string_view name;
    GravParam mu;
    Metres minPeriapsis; // just above the surface, or the cloud tops
    Metres maxSma;       // the outer edge of what a vessel would do here
};

constexpr std::array kBodies = std::to_array<Body>({
    {.name = "Moon", .mu = kMuMoon, .minPeriapsis = Metres{1.8e6}, .maxSma = Metres{1.0e8}},
    {.name = "Earth", .mu = kMuEarth, .minPeriapsis = Metres{6.6e6}, .maxSma = Metres{1.0e9}},
    {.name = "Jupiter", .mu = kMuJupiter, .minPeriapsis = Metres{7.5e7}, .maxSma = Metres{5.0e10}},
    {.name = "Sun", .mu = kMuSun, .minPeriapsis = Metres{1.0e10}, .maxSma = Metres{5.0e12}},
});

// The two ends of one propagation, as a single parameter rather than two: two
// adjacent StateVectors transpose in silence, and a transposed pair here would
// quietly assert the reverse claim. The same reasoning as Decades below, and
// the reason bugprone-easily-swappable-parameters is enabled at all.
struct Step {
    StateVector before;
    StateVector after;
};

// Energy and angular momentum are constants of two-body motion, and both are
// computed here rather than by the code under test, so agreement is evidence.
void checkConstantsOfMotion(const Step& step, GravParam mu) {
    INFO("energy conserved");
    REQUIRE_THAT(specificEnergy(step.after, mu).value,
                 WithinRelTo(specificEnergy(step.before, mu).value, Tolerance{1e-9}));
    INFO("angular momentum conserved");
    REQUIRE_THAT(specificAngularMomentum(step.after),
                 WithinRelVec(specificAngularMomentum(step.before), Tolerance{1e-9}));
}

// Propagating back by the same interval returns the start. A propagator that
// is consistently wrong in one direction still fails this.
void checkReversible(const Step& step, GravParam mu, Seconds dt) {
    const auto back = propagate(step.after, mu, -dt);
    INFO("propagate back -> " << errorName(back));
    REQUIRE(back.has_value());
    INFO("reversible");
    REQUIRE_THAT(back->pos, WithinRelVec(step.before.pos, Tolerance{1e-8}));
    REQUIRE_THAT(back->vel, WithinRelVec(step.before.vel, Tolerance{1e-8}));
}

// The element propagator shares no line of code and no formulation with the
// universal-variable one, so neither can hide a sign error behind the other.
void checkAgreesWithElementPropagation(const Elements& el,
                                       const StateVector& after,
                                       GravParam mu,
                                       Seconds dt) {
    const auto viaElements = propagateElements(el, mu, dt);
    INFO("propagateElements -> " << errorName(viaElements));
    REQUIRE(viaElements.has_value());

    const StateVector expected = stateFromElements(*viaElements, mu);
    INFO("agrees with element propagation");
    REQUIRE_THAT(after.pos, WithinRelVec(expected.pos, Tolerance{1e-8}));
    REQUIRE_THAT(after.vel, WithinRelVec(expected.vel, Tolerance{1e-8}));
}

// Every property a propagated state must have, whatever the orbit: it went
// where the element propagator says, it conserved energy and angular momentum,
// and propagating back by the same time returns the start. None of these
// compares the code against itself.
void checkPropagation(const Elements& el, GravParam mu, const StateVector& sv0, Seconds dt) {
    const auto fwd = propagate(sv0, mu, dt);
    INFO("propagate -> " << errorName(fwd));
    REQUIRE(fwd.has_value());

    const Step step{.before = sv0, .after = *fwd};
    checkConstantsOfMotion(step, mu);
    checkReversible(step, mu, dt);
    checkAgreesWithElementPropagation(el, *fwd, mu, dt);
}

// A randomised sweep: closed orbits from the Moon's surface to the outer solar
// system, hyperbolic ones likewise, each with a random orientation and a random
// time step of up to three revolutions either way.
//
// The seed is fixed and written down, because a failure you cannot reproduce
// is a failure you cannot fix. That inverts the premise of the random-seed
// lint checks, which exist for code that wants unpredictability.
constexpr std::size_t kClosedCases = 200;
constexpr std::size_t kHyperbolicCases = 100;
constexpr unsigned long long kSweepSeed = 20260905ULL; // the date this suite was written

// The two ends of a log-uniform draw, as one parameter rather than two: adjacent
// f64 arguments can be transposed silently, and a transposed range here would
// quietly sample the wrong decades. See CODING_GUIDELINES.md section 2.
struct Decades {
    f64 lo;
    f64 hi;
};

// The one random source for the whole sweep. Both halves draw from it in turn,
// so the sequence -- and therefore every case in the suite -- is reproducible
// from kSweepSeed alone.
// The generator is private because the draw sequence is the invariant: reading
// from it anywhere but through these three functions would shift every case
// that follows, and the seed would no longer describe the suite.
class Sampler {
public:
    [[nodiscard]] f64 fraction() { return unit_(rng_); }
    [[nodiscard]] f64 logUniform(Decades range) {
        return range.lo * std::pow(range.hi / range.lo, unit_(rng_));
    }
    [[nodiscard]] Radians angle(f64 range) { return Radians{unit_(rng_) * range}; }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSweepSeed};
    std::uniform_real_distribution<f64> unit_{0.0, 1.0};
};

void sweepClosedOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kClosedCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());

        const Eccentricity ecc{sampler.fraction() * 0.95};
        const Metres sma{sampler.logUniform(
            {.lo = body.minPeriapsis.value / (1.0 - ecc.value), .hi = body.maxSma.value})};
        const Elements el{
            .sma = sma,
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = sampler.angle(kTau),
            .slr = Metres{sma.value * (1.0 - (ecc.value * ecc.value))},
        };
        const OrbitInfo info = orbitInfo(el, body.mu);
        const Seconds dt = info.period * ((sampler.fraction() * 6.0) - 3.0);

        CAPTURE(kSweepSeed, i, body.name, sma.value, ecc.value, el.inc.value, dt.value);
        checkPropagation(el, body.mu, stateFromElements(el, body.mu), dt);
    }
}

void sweepHyperbolicOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kHyperbolicCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());

        const Eccentricity ecc{1.05 + (sampler.fraction() * 4.0)};
        const Metres periapsis{
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value / 10.0})};
        const Metres sma{-periapsis.value / (ecc.value - 1.0)}; // negative, by convention
        const Elements el{
            .sma = sma,
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = Radians{0.0}, // start at periapsis, where the state is tame
            .slr = Metres{periapsis.value * (1.0 + ecc.value)},
        };
        // The natural time scale at periapsis; fifty of them is well out on the
        // asymptote in either direction.
        const Seconds scale{
            std::sqrt(periapsis.value * periapsis.value * periapsis.value / body.mu.value)};
        const Seconds dt = scale * (((sampler.fraction() * 2.0) - 1.0) * 50.0);

        CAPTURE(kSweepSeed, i, body.name, periapsis.value, ecc.value, el.inc.value, dt.value);
        checkPropagation(el, body.mu, stateFromElements(el, body.mu), dt);
    }
}

} // namespace

// Both halves share one Sampler, drawn from in this order: the closed cases
// consume the first part of the sequence and the hyperbolic ones continue it.
// Reordering these two calls changes every case in the sweep.
TEST_CASE("randomised sweep across bodies, shapes and time steps", "[orbit][scales]") {
    Sampler sampler;
    sweepClosedOrbits(sampler);
    sweepHyperbolicOrbits(sampler);
}
