//
// What the two orbit-scale suites share (M1-94): test_orbit_scales and
// test_orbit_elements were one file, tests/test_orbit_scales.cpp, until
// 2026-09-27, when it was split so that each compiles and is linted on its own
// (ADR 0024, register decision 214). This is that file's opening helpers and
// its sweep machinery, moved unchanged except that their anonymous namespaces
// are named -- a header cannot hold one -- and their non-template functions
// are inline. Every case builds its own Sampler, so splitting the file changed
// no draw.
//
#ifndef ORBSIM_TESTS_ORBITSWEEPSUPPORT_HPP
#define ORBSIM_TESTS_ORBITSWEEPSUPPORT_HPP

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
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <random>
#include <string_view>

namespace orb::test::scales {

using namespace orb::literals;

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();

// The energy and the semi-major axis of a state, computed here rather than by
// the code under test: std::hypot for the lengths, and none of the orbit
// code's formulas. For the nearly radial cases below.
struct ConicReference {
    SpecificEnergy energy;
    Metres sma; // negative for a hyperbola
    Metres radius;
    MetresPerSecond speed;
};

[[nodiscard]] inline ConicReference conicReference(const StateVector& sv, GravParam mu) {
    const f64 r = std::hypot(sv.pos.x.value(), sv.pos.y.value(), sv.pos.z.value());
    const f64 v = std::hypot(sv.vel.x.value(), sv.vel.y.value(), sv.vel.z.value());
    const f64 energy = (0.5 * v * v) - (mu.value() / r);
    return {
        .energy = SpecificEnergy{energy},
        .sma = Metres{-mu.value() / (2.0 * energy)},
        .radius = Metres{r},
        .speed = MetresPerSecond{v},
    };
}

// |got / want - 1|, with no floor under `want`: WithinRelTo's floor of 1e-30
// would pass anything at the fuzzer's scale of 1e-159 m. An infinite or NaN
// `got` fails any budget.
// A measured value and the reference it is judged against. A struct rather
// than two f64 parameters because `want` is the denominator, so the two do not
// transpose -- which bugprone-easily-swappable-parameters reports since
// SuppressParametersUsedTogether was switched off on 2026-09-20. The field
// names are the ones the case tables in this file already use.
struct Comparison {
    f64 got{};
    f64 want{};
};

[[nodiscard]] inline f64 relativeError(const Comparison& c) {
    return std::abs((c.got / c.want) - 1.0);
}

// The budget for the nearly radial cases. Their energies' two terms never come
// within a factor of 2.4 of each other, so the subtraction amplifies rounding
// at most 2.4-fold, and the semi-major axis, the energy, the period and the
// mean motion each carry a handful of relative roundings on top: 1e-13 sits
// two orders above that and eleven below the errors these cases were written
// for.
constexpr Tolerance kConicBudget{1e-13};

// The state libFuzzer found (2026-09-10): a hyperbola 1e-158 m across, whose
// eccentricity came back below 1 while its energy was positive. Two cases
// below use it, so it is written once.
constexpr StateVector kFuzzerHyperbola{
    .pos = {6.6047118912273269e-313, 8.8544950093349595e-159, 8.8544945874389708e-159},
    .vel = {2.3135945642312217e-157, -9.2559606829389177e+61, -9.2559631349317831e+61},
};
constexpr GravParam kFuzzerMu = gravParam(4.3333423748712802e-35);

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
inline void checkConstantsOfMotion(const Step& step, GravParam mu) {
    INFO("energy conserved");
    REQUIRE_THAT(specificEnergy(step.after, mu).value(),
                 WithinRelTo(specificEnergy(step.before, mu).value(), Tolerance{1e-9}));
    INFO("angular momentum conserved");
    REQUIRE_THAT(specificAngularMomentum(step.after),
                 WithinRelVec(specificAngularMomentum(step.before), Tolerance{1e-9}));
}

// Propagating back by the same interval returns the start. A propagator that
// is consistently wrong in one direction still fails this.
inline void checkReversible(const Step& step, GravParam mu, Seconds dt) {
    const auto back = propagate(step.after, mu, -dt);
    INFO("propagate back -> " << errorName(back));
    REQUIRE(back.has_value());
    INFO("reversible");
    REQUIRE_THAT(back->pos, WithinRelVec(step.before.pos, Tolerance{1e-8}));
    REQUIRE_THAT(back->vel, WithinRelVec(step.before.vel, Tolerance{1e-8}));
}

// The element propagator shares no line of code and no formulation with the
// universal-variable one, so neither can hide a sign error behind the other.
inline void checkAgreesWithElementPropagation(const Elements& el,
                                              const StateVector& after,
                                              GravParam mu,
                                              Seconds dt) {
    const auto viaElements = propagateElements(el, mu, dt);
    INFO("propagateElements -> " << errorName(viaElements));
    REQUIRE(viaElements.has_value());

    const StateVector expected = stateOf(*viaElements, mu);
    INFO("agrees with element propagation");
    REQUIRE_THAT(after.pos, WithinRelVec(expected.pos, Tolerance{1e-8}));
    REQUIRE_THAT(after.vel, WithinRelVec(expected.vel, Tolerance{1e-8}));
}

// Every property a propagated state must have, whatever the orbit: it went
// where the element propagator says, it conserved energy and angular momentum,
// and propagating back by the same time returns the start. None of these
// compares the code against itself.
inline void checkPropagation(const Elements& el, GravParam mu, const StateVector& sv0, Seconds dt) {
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

inline void sweepClosedOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kClosedCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());

        const Eccentricity ecc = eccentricityOf(sampler.fraction() * 0.95);
        const Metres sma{sampler.logUniform(
            {.lo = body.minPeriapsis.value() / (1.0 - ecc.value()), .hi = body.maxSma.value()})};
        const Elements el{
            .sma = sma,
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = sampler.angle(kTau),
            .slr = Metres{sma.value() * (1.0 - (ecc.value() * ecc.value()))},
        };
        const OrbitInfo info = orbitInfo(el, body.mu);
        const Seconds dt = info.period * ((sampler.fraction() * 6.0) - 3.0);

        CAPTURE(kSweepSeed, i, body.name, sma.value(), ecc.value(), el.inc.value(), dt.value());
        checkPropagation(el, body.mu, stateOf(el, body.mu), dt);
    }
}

inline void sweepHyperbolicOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kHyperbolicCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());

        const Eccentricity ecc = eccentricityOf(1.05 + (sampler.fraction() * 4.0));
        const Metres periapsis{sampler.logUniform(
            {.lo = body.minPeriapsis.value(), .hi = body.maxSma.value() / 10.0})};
        const Metres sma{-periapsis.value() / (ecc.value() - 1.0)}; // negative, by convention
        const Elements el{
            .sma = sma,
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = Radians{0.0}, // start at periapsis, where the state is tame
            .slr = Metres{periapsis.value() * (1.0 + ecc.value())},
        };
        // The natural time scale at periapsis; fifty of them is well out on the
        // asymptote in either direction.
        const Seconds scale{
            std::sqrt(periapsis.value() * periapsis.value() * periapsis.value() / body.mu.value())};
        const Seconds dt = scale * (((sampler.fraction() * 2.0) - 1.0) * 50.0);

        CAPTURE(
            kSweepSeed, i, body.name, periapsis.value(), ecc.value(), el.inc.value(), dt.value());
        checkPropagation(el, body.mu, stateOf(el, body.mu), dt);
    }
}

} // namespace orb::test::scales

#endif // ORBSIM_TESTS_ORBITSWEEPSUPPORT_HPP
