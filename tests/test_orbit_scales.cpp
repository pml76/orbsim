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
        testZeroTimeStep(run);
        testComposition(run);
        testScaleInvariance(run);
        testRandomSweep(run);
    });
}