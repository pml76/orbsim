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
#include "tests/OrbitTestSupport.hpp"
#include "tests/TestHarness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <string_view>

using namespace orb;
using namespace orb::literals;
using namespace orb::test;

namespace {

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();

// Circular heliocentric orbits at the distances of Earth, Jupiter and Neptune.
// 1/a spans 6.7e-12 to 2.2e-13 per metre here, which is exactly the range the
// old 1e-12 threshold cut through.
void testHeliocentric(Run& run) {
    section("heliocentric circular orbits");

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
        std::print("  {}\n", c.name);
        const StateVector sv0 = circularState(kMuSun, c.radius);
        const Seconds period{kTau * c.radius.value / length(sv0.vel)};

        const auto quarter = propagate(sv0, kMuSun, period * 0.25);
        if (expectOk(run, quarter, "    propagate a quarter period")) {
            checkAngle(run,
                       "    quarter period is a quarter turn",
                       angleBetween(sv0.pos, quarter->pos),
                       Radians{kPi / 2},
                       Tolerance{1e-9});
            checkRel(run,
                     "    radius unchanged",
                     length(quarter->pos),
                     c.radius.value,
                     Tolerance{1e-12});
        }

        const auto whole = propagate(sv0, kMuSun, period);
        if (expectOk(run, whole, "    propagate one period")) {
            checkVecRel(
                run, "    one period returns to the start", whole->pos, sv0.pos, Tolerance{1e-9});
        }

        // Half a turn after many whole ones: the fold and the solve together.
        const auto many = propagate(sv0, kMuSun, period * 500.5);
        if (expectOk(run, many, "    propagate 500.5 periods")) {
            checkAngle(run,
                       "    500.5 periods is a half turn",
                       angleBetween(sv0.pos, many->pos),
                       Radians{kPi},
                       Tolerance{1e-8});
        }

        // The element propagator shares no code with the universal-variable
        // one; agreement out here is the same evidence it is in Earth orbit.
        const auto el = elementsFromState(sv0, kMuSun);
        if (!expectOk(run, el, "    elementsFromState")) continue;
        const auto viaUniversal = propagate(sv0, kMuSun, period * 0.3);
        const auto viaElements = propagateElements(*el, kMuSun, period * 0.3);
        if (expectOk(run, viaUniversal, "    propagate 0.3 periods") &&
            expectOk(run, viaElements, "    propagateElements 0.3 periods")) {
            checkVecRel(run,
                        "    propagators agree",
                        viaUniversal->pos,
                        stateFromElements(*viaElements, kMuSun).pos,
                        Tolerance{1e-9});
        }
    }
}

// Exactly escape speed: the conic the universal-variable formulation was
// chosen to make ordinary.
void testParabolic(Run& run) {
    section("parabolic trajectories");

    const f64 r0 = 7.0e6;
    const StateVector para{.pos = {r0, 0.0, 0.0},
                           .vel = {0.0, std::sqrt(2.0 * kMuEarth.value / r0), 0.0}};

    const auto el = elementsFromState(para, kMuEarth);
    if (!expectOk(run, el, "  elementsFromState")) return;
    checkNear(run, "  eccentricity is one", el->ecc.value, 1.0, Tolerance{1e-12});
    check(run, std::isinf(el->sma.value), "  semi-major axis is infinite");
    checkRel(run, "  semi-latus rectum is 2 r0", el->slr.value, 2.0 * r0, Tolerance{1e-12});

    const OrbitInfo info = orbitInfo(*el, kMuEarth);
    check(run, !info.closed, "  not closed");
    check(run, std::isinf(info.period.value), "  period is infinite");
    checkNear(
        run, "  energy is zero", info.energy.value, 0.0, Tolerance{1e-6 * kMuEarth.value / r0});

    // The trajectory is time-reversible and conserves energy at every dt,
    // including zero, where the Barker starting guess divides by the time.
    for (const f64 dt : {0.0, 1.0, 100.0, 3600.0, 1.0e6}) {
        const auto fwd = propagate(para, kMuEarth, Seconds{dt});
        if (!expectOk(run, fwd, "  propagate parabolic")) continue;
        check(run, std::isfinite(length(fwd->pos)), "  position stays finite");
        checkNear(run,
                  "  energy stays zero",
                  specificEnergy(*fwd, kMuEarth).value,
                  0.0,
                  Tolerance{1e-9 * kMuEarth.value / r0});

        const auto back = propagate(*fwd, kMuEarth, Seconds{-dt});
        if (expectOk(run, back, "  propagate parabolic backward")) {
            checkVecRel(run, "  forward then back (pos)", back->pos, para.pos, Tolerance{1e-9});
            checkVecRel(run, "  forward then back (vel)", back->vel, para.vel, Tolerance{1e-9});
        }
    }

    // Element propagation needs a finite semi-major axis and says so, instead
    // of feeding an infinity into the Kepler solver and reporting that it
    // "did not converge".
    const auto viaElements = propagateElements(*el, kMuEarth, 100.0_s);
    check(run,
          !viaElements.has_value() && viaElements.error() == OrbitError::ParabolicElements,
          "  propagateElements refuses parabolic elements, specifically");
}

// NaN and infinity are what a corrupt scenario file produces. They must be
// refused by name, not reported as a solver that failed to converge.
void testNonFiniteInputs(Run& run) {
    section("non-finite inputs are refused by name");

    const StateVector leo{.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 7546.0, 0.0}};

    const auto nanPosition = elementsFromState({.pos = {kNaN, 0.0, 0.0}, .vel = leo.vel}, kMuEarth);
    check(run,
          !nanPosition.has_value() && nanPosition.error() == OrbitError::NotFinite,
          "  NaN position");

    const auto infVelocity = propagate({.pos = leo.pos, .vel = {0.0, kInf, 0.0}}, kMuEarth, 60.0_s);
    check(run,
          !infVelocity.has_value() && infVelocity.error() == OrbitError::NotFinite,
          "  infinite velocity");

    const auto nanTime = propagate(leo, kMuEarth, Seconds{kNaN});
    check(run, !nanTime.has_value() && nanTime.error() == OrbitError::NotFinite, "  NaN time step");

    const auto nanGravity = propagate(leo, GravParam{kNaN}, 60.0_s);
    check(run,
          !nanGravity.has_value() && nanGravity.error() == OrbitError::NotFinite,
          "  NaN gravitational parameter");

    const auto el = elementsFromState(leo, kMuEarth);
    if (expectOk(run, el, "  elementsFromState")) {
        const auto infTime = propagateElements(*el, kMuEarth, Seconds{kInf});
        check(run,
              !infTime.has_value() && infTime.error() == OrbitError::NotFinite,
              "  infinite time step for propagateElements");
    }

    // A finite but zero time step is ordinary, and returns the input exactly.
    const auto still = propagate(leo, kMuEarth, 0.0_s);
    if (expectOk(run, still, "  propagate by zero")) {
        checkVecRel(run, "  zero time step is the identity", still->pos, leo.pos, Tolerance{0.0});
    }

    check(run, !describe(OrbitError::NotFinite).empty(), "  NotFinite describes itself");
    check(run, !describe(OrbitError::ParabolicElements).empty(), "  ParabolicElements too");
    check(run, !describe(OrbitError::RectilinearOrbit).empty(), "  RectilinearOrbit too");
}

// States that pass every input check and still do not describe an orbit. Both
// of these came from the fuzzer rather than from anyone sitting down to think
// of them, which is the argument for rule 13 in one paragraph.
void testDegenerateStates(Run& run) {
    section("states that are finite but are not orbits");

    // Finite components whose magnitude is not. Squaring overflows above about
    // 1.3e154, so lengthSq() reaches infinity from inputs that every isfinite()
    // check passes -- and `rmag > 0.0` is then true of infinity, so the
    // precondition let it through. A fuzzer found this (VERIFICATION.md rule
    // 13): the elements came back reporting success, with an infinite
    // eccentricity and a NaN argument of periapsis.
    const StateVector overflowing{.pos = {-5.486124068796807e303, 0.0, 0.0},
                                  .vel = {0.0, 7.418412301374917e-68, 0.0}};
    const auto overflowElements = elementsFromState(overflowing, kMuEarth);
    check(run,
          !overflowElements.has_value() && overflowElements.error() == OrbitError::NotFinite,
          "  position whose magnitude overflows");

    const auto overflowPropagate = propagate(overflowing, kMuEarth, 60.0_s);
    check(run,
          !overflowPropagate.has_value() && overflowPropagate.error() == OrbitError::NotFinite,
          "  propagate refuses the same state");

    // The velocity side of the same hole.
    const auto overflowSpeed =
        elementsFromState({.pos = {7000e3, 0.0, 0.0}, .vel = {1e200, 1e200, 0.0}}, kMuEarth);
    check(run,
          !overflowSpeed.has_value() && overflowSpeed.error() == OrbitError::NotFinite,
          "  velocity whose magnitude overflows");
}

// The other half: states with no orbital plane at all. Both of these came from
// the fuzzer rather than from anyone sitting down to think of them, which is
// the argument for rule 13 in one paragraph.
void testNoOrbitalPlane(Run& run) {
    section("states with no orbital plane");

    // A radial trajectory: velocity parallel to position, so the specific
    // angular momentum is zero and there is no orbital plane to incline. The
    // inclination was acos(h.z / |h|) = acos(0/0) = NaN, returned as a success.
    // Physically reachable -- a probe released with no horizontal velocity
    // falls straight down -- and found by the fuzzer, not by anyone's
    // imagination (VERIFICATION.md rule 13).
    const auto straightDown =
        elementsFromState({.pos = {7000e3, 0.0, 0.0}, .vel = {-100.0, 0.0, 0.0}}, kMuEarth);
    check(run,
          !straightDown.has_value() && straightDown.error() == OrbitError::RectilinearOrbit,
          "  radial trajectory has no orbital plane");

    const auto atRest =
        elementsFromState({.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 0.0, 0.0}}, kMuEarth);
    check(run,
          !atRest.has_value() && atRest.error() == OrbitError::RectilinearOrbit,
          "  a body at rest likewise");

    // Magnitudes finite, mu positive, trajectory not radial -- and the derived
    // elements still overflow, because mu is tiny relative to the state and the
    // eccentricity vector divides by it. The elements came back reporting
    // success with an infinite eccentricity and NaN in-plane angles. The third
    // thing the fuzzer found, and the one that argued for checking the answer
    // rather than adding another input guard.
    const auto tinyMu = elementsFromState(
        {.pos = {1.0e120, 0.0, 0.0}, .vel = {9.68e119, 1.0e118, 0.0}}, GravParam{6.8e-231});
    check(run,
          !tinyMu.has_value() && tinyMu.error() == OrbitError::NotFinite,
          "  elements that overflow are not returned as a success");

    // propagate's postcondition used to be an assertion, so a Debug build
    // aborted the process on this input instead of reporting it -- the wrong
    // half of the ADR 0002 split, since a caller can produce it. An enormous
    // speed with a matching mu makes the Lagrange combination overflow.
    const StateVector violent{
        .pos = {1.5419835033e-313, 7.477078763343729e20, 4.483094976257099e-120},
        .vel = {4.483094640249093e-120, 1.3792778605844018e40, 7.477080264543605e20}};
    const auto overflowed = propagate(violent, GravParam{7.477080264551322e20}, 0.0_s);
    check(run,
          overflowed.has_value() || overflowed.error() == OrbitError::NotFinite ||
              overflowed.error() == OrbitError::DegenerateState,
          "  propagate reports rather than asserting when the result overflows");

    // |h| is nonzero but |h|^2 underflows, so the semi-latus rectum is zero and
    // orbitInfo's radius became slr / (1 + e cos v) = 0/0. The ratio test above
    // cannot see this one, because it never squares anything.
    const auto underflowedSlr =
        elementsFromState({.pos = {-7.8804e115, -4.62693e-179, -1.60283e-180},
                           .vel = {-1.60283e-180, -1.60283e-180, -1.60283e-180}},
                          GravParam{3.01352e296});
    check(run,
          !underflowedSlr.has_value() && underflowedSlr.error() == OrbitError::RectilinearOrbit,
          "  a semi-latus rectum that underflows is not an orbit");

    // But a genuinely eccentric orbit is not rectilinear, however thin it is.
    const Elements thin =
        makeElements(Metres{2.0e7}, Eccentricity{0.9999}, 45.0_deg, 0.0_deg, 0.0_deg, 90.0_deg);
    const auto stillAnOrbit = elementsFromState(stateFromElements(thin, kMuEarth), kMuEarth);
    check(run, stillAnOrbit.has_value(), "  e = 0.9999 is still an orbit");
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
void testZeroTimeStep(Run& run) {
    section("a zero time step is the identity on every conic");

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
        {.name = "ellipse, LEO",
         .mu = kMuEarth,
         .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, 7546.0, 0.0}}},
        {.name = "parabola, LEO",
         .mu = kMuEarth,
         .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, escapeSpeed, 0.0}}},
        {.name = "hyperbola, LEO",
         .mu = kMuEarth,
         .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, 12000.0, 0.0}}},
        {.name = "hyperbola, 1 AU",
         .mu = kMuSun,
         .state = {.pos = {kAuRadius, 0.0, 0.0}, .vel = {0.0, solarEscape * 1.2, 0.0}}},
        {.name = "retrograde hyperbola, LEO",
         .mu = kMuEarth,
         .state = {.pos = {kLeoRadius, 0.0, 0.0}, .vel = {0.0, -12000.0, 0.0}}},
    });

    for (const Case& c : kCases) {
        const auto still = propagate(c.state, c.mu, 0.0_s);
        if (!expectOk(run, still, c.name)) continue;
        // Exactly, not nearly: the identity is the claim, so any tolerance at
        // all would hide the difference between "returned the input" and
        // "integrated to something indistinguishable from it".
        checkVecRel(run, c.name, still->pos, c.state.pos, Tolerance{0.0});
        checkVecRel(run, c.name, still->vel, c.state.vel, Tolerance{0.0});
    }
}

// propagate(s, a + b) must agree with propagate(propagate(s, a), b). This is a
// property rather than an example: it holds for every state and every pair of
// steps, it says nothing about what the right answer is, and it therefore
// cannot be satisfied by a propagator that is consistently wrong in one
// direction. VERIFICATION.md rule 11.
void testComposition(Run& run) {
    section("propagation composes: one long step equals two short ones");

    struct Case {
        std::string_view name;
        GravParam mu;
        StateVector state;
        Seconds first;
        Seconds second;
    };

    const std::array kCases = std::to_array<Case>({
        {.name = "  LEO, 600 s + 900 s",
         .mu = kMuEarth,
         .state = circularState(kMuEarth, Metres{7000e3}),
         .first = 600.0_s,
         .second = 900.0_s},
        {.name = "  LEO, forward then backward",
         .mu = kMuEarth,
         .state = circularState(kMuEarth, Metres{7000e3}),
         .first = 4000.0_s,
         .second = -1500.0_s},
        {.name = "  lunar orbit, two half days",
         .mu = kMuMoon,
         .state = circularState(kMuMoon, Metres{2000e3}),
         .first = 43200.0_s,
         .second = 43200.0_s},
        {.name = "  1 AU, two quarter years",
         .mu = kMuSun,
         .state = circularState(kMuSun, Metres{1.496e11}),
         .first = Seconds{7.9e6},
         .second = Seconds{7.9e6}},
        {.name = "  hyperbolic escape, 100 s + 250 s",
         .mu = kMuEarth,
         .state = {.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 12000.0, 0.0}},
         .first = 100.0_s,
         .second = 250.0_s},
    });

    for (const Case& c : kCases) {
        const auto together = propagate(c.state, c.mu, c.first + c.second);
        const auto once = propagate(c.state, c.mu, c.first);
        if (!expectOk(run, together, c.name)) continue;
        if (!expectOk(run, once, c.name)) continue;
        const auto twice = propagate(*once, c.mu, c.second);
        if (!expectOk(run, twice, c.name)) continue;

        checkVecRel(run, c.name, twice->pos, together->pos, Tolerance{1e-9});
        checkVecRel(run, c.name, twice->vel, together->vel, Tolerance{1e-9});
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
void testScaleInvariance(Run& run) {
    section("two-body motion is invariant under canonical rescaling");

    const StateVector base = circularState(kMuEarth, Metres{7000e3});
    constexpr Seconds kStep{1800.0};

    // Spanning ten orders of magnitude in length, which is the range from a low
    // orbit to the outer solar system.
    constexpr std::array kLambdas = std::to_array<f64>({1e-3, 1.0, 1e2, 1e4, 1e7, 1e10});

    for (const f64 lambda : kLambdas) {
        const f64 speedScale = 1.0 / std::sqrt(lambda);
        const f64 timeScale = lambda * std::sqrt(lambda);

        const StateVector scaled{.pos = base.pos * lambda, .vel = base.vel * speedScale};

        const auto plain = propagate(base, kMuEarth, kStep);
        const auto rescaled = propagate(scaled, kMuEarth, Seconds{kStep.value * timeScale});
        if (!expectOk(run, plain, "  unscaled propagation")) continue;
        if (!expectOk(run, rescaled, "  rescaled propagation")) continue;

        checkVecRel(run, "  position scales", rescaled->pos, plain->pos * lambda, Tolerance{1e-9});
        checkVecRel(
            run, "  velocity scales", rescaled->vel, plain->vel * speedScale, Tolerance{1e-9});
    }

    std::print("  lambda from {:g} to {:g}\n", kLambdas.front(), kLambdas.back());
}

// Near-rectilinear orbits: eccentricity approaching 1 with the ellipse
// collapsing toward a straight line through the focus. The remaining case from
// VERIFICATION.md rule 5's list, and the hardest one for a Kepler solver --
// almost all of the mean anomaly is spent near periapsis, which is why
// meanToEccentricAnomaly switches its starting guess at e = 0.8.
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
void checkConserved(Run& run, const StateVector& before, const StateVector& after, f64 e) {
    const Tolerance energyBudget{2e-15 / (1.0 - e)};
    const Tolerance momentumBudget{5e-14 / (1.0 - e)};

    checkRel(run,
             "  energy conserved",
             specificEnergy(after, kMuEarth).value,
             specificEnergy(before, kMuEarth).value,
             energyBudget);
    checkVecRel(run,
                "  angular momentum conserved",
                specificAngularMomentum(after),
                specificAngularMomentum(before),
                momentumBudget);
}

void checkOneEccentricity(Run& run, f64 e) {
    {
        constexpr Metres kSemiMajor{2.0e7};
        const Elements el =
            makeElements(kSemiMajor, Eccentricity{e}, 45.0_deg, 30.0_deg, 60.0_deg, 10.0_deg);
        const StateVector sv = stateFromElements(el, kMuEarth);
        const OrbitInfo info = orbitInfo(el, kMuEarth);

        // A quarter period is enough to cross the fast part of the orbit.
        const Seconds step{info.period.value * 0.25};

        const auto moved = propagate(sv, kMuEarth, step);
        if (!expectOk(run, moved, "  propagate near-rectilinear")) return;

        checkConserved(run, sv, *moved, e);

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
        if (!expectOk(run, back, "  propagate back")) return;
        checkVecRel(run, "  round trip", back->pos, sv.pos, roundTrip);

        // The Kepler solver on its own, at the eccentricity that breaks the
        // naive starting guess.
        const Radians ecc = trueToEccentricAnomaly(el.tra, el.ecc);
        const Radians mean = eccentricToMeanAnomaly(ecc, el.ecc);
        const auto solved = meanToEccentricAnomaly(mean, el.ecc);
        if (expectOk(run, solved, "  Kepler solver converges")) {
            checkAngle(run, "  solver round trip", *solved, ecc, Tolerance{1e-9});
        }
    }
}

void testNearRectilinear(Run& run) {
    section("near-rectilinear orbits, e approaching 1");

    // 0.9999 and 0.99999 are the regression cases. Plain Newton failed to
    // converge on the backward step at both -- under gcc-14 and clang-on-Linux
    // but not under Windows clang, which is how it was exposed. The
    // safeguarded solver answers for every one of them, on all three
    // toolchains, and each check below carries a budget scaled by the conic's
    // own conditioning rather than a single number that has to serve four
    // decades of (1 - e).
    constexpr std::array kEccentricities = std::to_array<f64>({0.9, 0.99, 0.999, 0.9999, 0.99999});
    for (const f64 e : kEccentricities)
        checkOneEccentricity(run, e);

    std::print("  e from {:g} to {:g}\n", kEccentricities.front(), kEccentricities.back());
}

// The same inputs must produce bit-identical outputs, every time.
//
// VERIFICATION.md rule 16, and the one place this codebase permits `==` on
// floating point: bit identity is the actual claim, and a tolerance here would
// hide precisely the drift being tested for. It catches a class of accidental
// nondeterminism that no other test can see -- iteration over an unordered
// container, uninitialised padding, a branch on wall-clock time, a solver that
// reads a global. None of those exist today, which is the point: this test is
// what notices when one arrives.
void testDeterminism(Run& run) {
    section("propagation is bit-identical across runs");

    const StateVector start = circularState(kMuEarth, Metres{7000e3});

    // A single step, twice.
    const auto first = propagate(start, kMuEarth, 1234.5_s);
    const auto second = propagate(start, kMuEarth, 1234.5_s);
    if (expectOk(run, first, "  first run") && expectOk(run, second, "  second run")) {
        check(run, first->pos == second->pos, "  single step: position bit-identical");
        check(run, first->vel == second->vel, "  single step: velocity bit-identical");
    }

    // A long chain, where any drift would compound rather than cancel.
    const auto chain = [&]() -> StateVector {
        StateVector s = start;
        for (int i = 0; i < 100; ++i) {
            const auto next = propagate(s, kMuEarth, 60.0_s);
            if (!next) return StateVector{};
            s = *next;
        }
        return s;
    };
    const StateVector chainA = chain();
    const StateVector chainB = chain();
    check(run, chainA.pos == chainB.pos, "  100 steps: position bit-identical");
    check(run, chainA.vel == chainB.vel, "  100 steps: velocity bit-identical");

    // The conversions too, since a scenario reload goes through them.
    const auto elA = elementsFromState(start, kMuEarth);
    const auto elB = elementsFromState(start, kMuEarth);
    if (expectOk(run, elA, "  elements first") && expectOk(run, elB, "  elements second")) {
        check(run,
              elA->sma == elB->sma && elA->ecc == elB->ecc && elA->inc == elB->inc &&
                  elA->lan == elB->lan && elA->aop == elB->aop && elA->tra == elB->tra,
              "  elementsFromState is bit-identical");
        check(run,
              stateFromElements(*elA, kMuEarth).pos == stateFromElements(*elB, kMuEarth).pos,
              "  stateFromElements is bit-identical");
    }
}

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

// Every property a propagated state must have, whatever the orbit: it went
// where the element propagator says, it conserved energy and angular momentum,
// and propagating back by the same time returns the start. None of these
// compares the code against itself.
void checkPropagation(
    Run& run, const Elements& el, GravParam mu, const StateVector& sv0, Seconds dt) {
    const auto fwd = propagate(sv0, mu, dt);
    if (!expectOk(run, fwd, "    propagate")) return;

    checkRel(run,
             "    energy conserved",
             specificEnergy(*fwd, mu).value,
             specificEnergy(sv0, mu).value,
             Tolerance{1e-9});
    checkVecRel(run,
                "    angular momentum conserved",
                specificAngularMomentum(*fwd),
                specificAngularMomentum(sv0),
                Tolerance{1e-9});

    const auto back = propagate(*fwd, mu, -dt);
    if (expectOk(run, back, "    propagate back")) {
        checkVecRel(run, "    reversible (pos)", back->pos, sv0.pos, Tolerance{1e-8});
        checkVecRel(run, "    reversible (vel)", back->vel, sv0.vel, Tolerance{1e-8});
    }

    const auto viaElements = propagateElements(el, mu, dt);
    if (expectOk(run, viaElements, "    propagateElements")) {
        const StateVector expected = stateFromElements(*viaElements, mu);
        checkVecRel(run,
                    "    agrees with element propagation (pos)",
                    fwd->pos,
                    expected.pos,
                    Tolerance{1e-8});
        checkVecRel(run,
                    "    agrees with element propagation (vel)",
                    fwd->vel,
                    expected.vel,
                    Tolerance{1e-8});
    }
}

// A randomised sweep: closed orbits from the Moon's surface to the outer solar
// system, hyperbolic ones likewise, each with a random orientation and a random
// time step of up to three revolutions either way.
//
// The seed is fixed and written down, because a failure you cannot reproduce
// is a failure you cannot fix. That inverts the premise of the random-seed
// lint checks, which exist for code that wants unpredictability.
void testRandomSweep(Run& run) {
    section("randomised sweep across bodies, shapes and time steps");

    constexpr std::size_t kClosedCases = 200;
    constexpr std::size_t kHyperbolicCases = 100;
    constexpr unsigned long long kSeed = 20260905ULL; // the date this suite was written

    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng{kSeed};
    std::uniform_real_distribution<f64> unit{0.0, 1.0};
    const auto logUniform = [&](f64 lo, f64 hi) { return lo * std::pow(hi / lo, unit(rng)); };
    const auto angle = [&](f64 range) { return Radians{unit(rng) * range}; };

    for (std::size_t i = 0; i < kClosedCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const int failuresBefore = run.failures;

        const Eccentricity ecc{unit(rng) * 0.95};
        const Metres sma{
            logUniform(body.minPeriapsis.value / (1.0 - ecc.value), body.maxSma.value)};
        const Elements el{.sma = sma,
                          .ecc = ecc,
                          .inc = angle(kPi),
                          .lan = angle(kTau),
                          .aop = angle(kTau),
                          .tra = angle(kTau),
                          .slr = Metres{sma.value * (1.0 - (ecc.value * ecc.value))}};
        const OrbitInfo info = orbitInfo(el, body.mu);
        const Seconds dt = info.period * ((unit(rng) * 6.0) - 3.0);

        checkPropagation(run, el, body.mu, stateFromElements(el, body.mu), dt);

        if (run.failures != failuresBefore) {
            std::print("    closed case {}: {} a={:.6g} e={:.6g} inc={:.4g} dt={:.6g} s\n",
                       i,
                       body.name,
                       sma.value,
                       ecc.value,
                       el.inc.value,
                       dt.value);
        }
    }

    for (std::size_t i = 0; i < kHyperbolicCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const int failuresBefore = run.failures;

        const Eccentricity ecc{1.05 + (unit(rng) * 4.0)};
        const Metres periapsis{logUniform(body.minPeriapsis.value, body.maxSma.value / 10.0)};
        const Metres sma{-periapsis.value / (ecc.value - 1.0)}; // negative, by convention
        const Elements el{.sma = sma,
                          .ecc = ecc,
                          .inc = angle(kPi),
                          .lan = angle(kTau),
                          .aop = angle(kTau),
                          .tra = Radians{0.0}, // start at periapsis, where the state is tame
                          .slr = Metres{periapsis.value * (1.0 + ecc.value)}};
        // The natural time scale at periapsis; fifty of them is well out on the
        // asymptote in either direction.
        const Seconds scale{
            std::sqrt(periapsis.value * periapsis.value * periapsis.value / body.mu.value)};
        const Seconds dt = scale * (((unit(rng) * 2.0) - 1.0) * 50.0);

        checkPropagation(run, el, body.mu, stateFromElements(el, body.mu), dt);

        if (run.failures != failuresBefore) {
            std::print("    hyperbolic case {}: {} rp={:.6g} e={:.6g} inc={:.4g} dt={:.6g} s\n",
                       i,
                       body.name,
                       periapsis.value,
                       ecc.value,
                       el.inc.value,
                       dt.value);
        }
    }

    std::print(
        "  {} closed and {} hyperbolic orbits, seed {}\n", kClosedCases, kHyperbolicCases, kSeed);
}

} // namespace

int main() {
    return runSuite("two-body core across scales", [](Run& run) {
        testHeliocentric(run);
        testParabolic(run);
        testNonFiniteInputs(run);
        testDegenerateStates(run);
        testNoOrbitalPlane(run);
        testZeroTimeStep(run);
        testNearRectilinear(run);
        testComposition(run);
        testScaleInvariance(run);
        testDeterminism(run);
        testRandomSweep(run);
    });
}