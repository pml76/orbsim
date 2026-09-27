//
// The round trip through the elements, element propagation across the
// parabola, and elementsFromState and stateFromElements element by element,
// against 60-digit references. Part of what was tests/test_orbit_scales.cpp
// until 2026-09-27 (M1-94); what the two suites share is
// tests/OrbitSweepSupport.hpp.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "tests/OrbitSweepSupport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

using namespace orb;
using namespace orb::literals;
using namespace orb::test;
using namespace orb::test::scales;

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

// --- what the round trip through the elements is worth -----------------------

namespace {

// orbitInfo reads the radius and the speed back out of p, e and the true
// anomaly, and the true anomaly is a double. Near the radial limit r and v
// depend on it steeply, so that is the budget: from r = p / (1 + e cos nu) and
// v^2 = (mu/p)(1 + 2 e cos nu + e^2), with e sin nu = (r.v)|h| / (mu r),
//
//     |d ln r / d nu| = |r.v| / |h|                 = kappa_r
//     |d ln v / d nu| = |r.v| mu / (r v^2 |h|)      = kappa_v
//
// an error of an ulp in the anomaly is kappa ulp in what comes back. Both are
// written in terms of the state, so the budget owes nothing to the code it
// judges, and both are dimensionless -- section 12.
//
// Measured against 60-digit references over 110,004 states -- ordinary, nearly
// radial, near-parabolic, small e, and hyperbolas out to r/|a| = 1e3 -- on
// Windows clang, WSL clang and gcc-14 (2026-09-12), the error stayed within
// 9.3 u (1 + kappa)(1 + |alpha r|) with u = 2^-53. Four times that is the
// budget. The formulation this replaced needed 3.5e8 on the same states, and on
// 417 of them no factor at all would have done: the radius came back infinite.
//
// The second factor is not orbitInfo's either: it is how steeply the radius
// and the speed depend on an anomaly that is only a double, which is what
// |alpha r| measures. It used to carry a second job as well -- far out on a
// hyperbola the eccentricity vector in elementsFromState cancelled, and the
// anomaly it stored lost about 2e-15 r/|a| rad with it, so this sweep stopped
// at r/|a| = 1e3 where the elements still meant something. That is fixed
// (2026-09-12, core/DoubleDouble.hpp), and the sweep now runs to r/|a| = 1e6
// and down to e = 1e-16. Over those wider ranges the worst case was between
// 8.5 and 9.3 u, measured by tightening this factor until it fails.
//
// Since 2026-09-13 the sweep also asserts the whole state through
// stateFromElements and not only the radius and the speed through orbitInfo,
// which is the tighter of the two: the worst case is then between 14 and 16 u,
// so the budget of 40 is about two and a half times it rather than four.
//
// The additive term is the parabolic band's. Where the energy puts a state
// within 1e-12 of a parabola, elementsFromState stores an infinite sma, and
// what comes back is the parabola through that state -- up to |alpha r| / 2
// away from the truth, which no formulation can improve on from those
// elements. It is bounded by the band, so it is at most 5e-13.
constexpr f64 kUnitRoundoff = 0x1p-53;
constexpr f64 kRoundTripFactor = 40.0;
constexpr std::size_t kRoundTripCases = 2000; // per family below

struct RoundTripBudget {
    Tolerance radius;
    Tolerance speed;
};

[[nodiscard]] RoundTripBudget
roundTripBudget(const StateVector& sv, const Elements& el, GravParam mu) {
    const f64 r = length(sv.pos).value();
    const f64 v = length(sv.vel).value();
    const f64 rdotv = std::abs(dot(sv.pos, sv.vel).value());
    const f64 h = length(cross(sv.pos, sv.vel)).value();
    const f64 alphaRadius = std::abs(2.0 - (r * v * v / mu.value()));
    const f64 band = std::isinf(el.sma.value()) ? 0.5 * alphaRadius : 0.0;
    const f64 scale = kRoundTripFactor * kUnitRoundoff * (1.0 + alphaRadius);
    return {
        .radius = Tolerance{(scale * (1.0 + (rdotv / h))) + band},
        .speed = Tolerance{(scale * (1.0 + (rdotv * mu.value() / (r * v * v * h)))) + band},
    };
}

// The state's own |r| and |v| are the reference: they are two hypots of the
// input, and the elements are a round trip away from them.
void checkRadiusAndSpeed(const StateVector& sv, GravParam mu) {
    const auto el = elementsFromState(sv, mu);
    INFO("elementsFromState -> " << errorName(el));
    REQUIRE(el.has_value());

    const OrbitInfo info = orbitInfo(*el, mu);
    const ConicReference want = conicReference(sv, mu);
    const RoundTripBudget budget = roundTripBudget(sv, *el, mu);
    // Formatted by hand: Catch2 prints a tiny double as "-0.0", and 17
    // significant digits round-trip, so a failure pastes back in as a case.
    INFO(std::format("radius {:.17g} m, want {:.17g}, budget {:.3g}; "
                     "speed {:.17g} m/s, want {:.17g}, budget {:.3g}",
                     info.radius.value(),
                     want.radius.value(),
                     budget.radius.value(),
                     info.speed.value(),
                     want.speed.value(),
                     budget.speed.value()));
    REQUIRE(relativeError({.got = info.radius.value(), .want = want.radius.value()}) <=
            budget.radius.value());
    REQUIRE(relativeError({.got = info.speed.value(), .want = want.speed.value()}) <=
            budget.speed.value());

    // And the whole state, not just the radius and the speed read back through
    // orbitInfo. This is the blind spot that let stateFromElements return a NaN
    // position for 1.7% of nearly radial element sets through three commits that
    // were all looking here: it computed 1 + e cos v straight, while orbitInfo
    // called the cancellation-free helper, so the two disagreed and only one of
    // them was being checked. Same budget -- the round trip through the elements
    // is what both are doing.
    const StateVector back = stateOf(*el, mu);
    INFO(std::format("position back {:.17g} m from {:.17g}, velocity {:.17g} from {:.17g}",
                     length(back.pos).value(),
                     length(sv.pos).value(),
                     length(back.vel).value(),
                     length(sv.vel).value()));
    REQUIRE((length(back.pos - sv.pos) / length(sv.pos)).value() <= budget.radius.value());
    REQUIRE((length(back.vel - sv.vel) / length(sv.vel)).value() <= budget.speed.value());
}

// A uniform direction on the sphere: z uniform and the azimuth uniform is the
// one pairing that does not crowd the poles.
[[nodiscard]] Direction randomDirection(Sampler& sampler) {
    const f64 z = (2.0 * sampler.fraction()) - 1.0;
    const Radians azimuth = sampler.angle(kTau);
    const f64 ring = std::sqrt(1.0 - (z * z));
    return {ring * std::cos(azimuth.value()), ring * std::sin(azimuth.value()), z};
}

// A unit vector perpendicular to a unit `dir`, at a random azimuth about it.
// The seed vector is chosen to be well away from `dir`, so the cross product
// is never a ratio of two roundings -- drawing a second random direction here
// could return one parallel to the first.
[[nodiscard]] Direction perpendicularTo(const Direction& dir, Sampler& sampler) {
    const Direction seed =
        (std::abs(dir.x.value()) < 0.9) ? Direction{1, 0, 0} : Direction{0, 1, 0};
    return rotateAxis(normalize(cross(dir, seed)), dir, sampler.angle(kTau));
}

// Any shape at any attitude: a tenth of circular speed to twice it spans
// e = 0 to hyperbolic, and the direction is independent of the position.
void sweepOrdinaryOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const f64 radius =
            sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()});
        const Direction dir = randomDirection(sampler);
        const f64 speed = std::sqrt(body.mu.value() / radius) * (0.1 + (1.9 * sampler.fraction()));
        CAPTURE(kSweepSeed, i, body.name, radius, speed);
        checkRadiusAndSpeed(
            {.pos = dir * Metres{radius}, .vel = randomDirection(sampler) * MetresPerSecond{speed}},
            body.mu);
    }
}

// Nearly radial: the velocity within 1e-11 to 1e-2 of parallel to the
// position, rising or falling, from well below escape speed to well above it.
// This is where 1 + e cos nu cancels, and where the old formulation returned an
// infinite radius for 417 of 30,000 such states.
void sweepNearlyRadialOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const f64 radius =
            sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()});
        const Direction dir = randomDirection(sampler);
        const f64 speed = std::sqrt(body.mu.value() / radius) * (0.05 + (2.5 * sampler.fraction()));
        const f64 tangential = sampler.logUniform({.lo = 1e-11, .hi = 1e-2});
        const f64 outward = (sampler.fraction() < 0.5) ? 1.0 : -1.0;
        const Velocity vel = ((dir * (outward * std::sqrt(1.0 - (tangential * tangential)))) +
                              (perpendicularTo(dir, sampler) * tangential)) *
                             MetresPerSecond{speed};
        CAPTURE(kSweepSeed, i, body.name, radius, speed, tangential, outward);
        checkRadiusAndSpeed({.pos = dir * Metres{radius}, .vel = vel}, body.mu);
    }
}

// Within 1e-15 to 1e-6 of escape speed, either side, at every flight path
// angle: the states whose energy decides the conic by a hair, and the ones the
// parabolic band catches.
void sweepNearParabolicOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const f64 radius =
            sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()});
        const Direction dir = randomDirection(sampler);
        const f64 offset = sampler.logUniform({.lo = 1e-15, .hi = 1e-6});
        const f64 speed = std::sqrt(2.0 * body.mu.value() / radius) *
                          (1.0 + ((sampler.fraction() < 0.5) ? offset : -offset));
        const Radians flightPath{sampler.angle(kPi).value() - (kPi / 2.0)};
        const Velocity vel = ((dir * std::sin(flightPath.value())) +
                              (perpendicularTo(dir, sampler) * std::cos(flightPath.value()))) *
                             MetresPerSecond{speed};
        CAPTURE(kSweepSeed, i, body.name, radius, speed, offset, flightPath.value());
        checkRadiusAndSpeed({.pos = dir * Metres{radius}, .vel = vel}, body.mu);
    }
}

// The other degenerate end, down to e = 1e-16. It used to stop at 2e-9,
// because below 1e-9 elementsFromState substituted the argument of latitude
// for the true anomaly while leaving `ecc` alone, and the radius a consumer
// rebuilt from that pair was out by about 2e -- 1.8e-9 at the threshold, which
// is 12.6 mm at 7000 km. The substitution now happens below 1e-15 instead,
// where 2e is the resolution of a double (2026-09-12, and the note on
// kCircularTol in Orbit.cpp says why that is the right place for it).
void sweepSmallEccentricities(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Eccentricity ecc = eccentricityOf(sampler.logUniform({.lo = 1e-16, .hi = 1e-2}));
        const f64 sma = sampler.logUniform(
            {.lo = body.minPeriapsis.value() / (1.0 - ecc.value()), .hi = body.maxSma.value()});
        const Elements el{
            .sma = Metres{sma},
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = sampler.angle(kTau),
            .slr = Metres{sma * (1.0 - (ecc.value() * ecc.value()))},
        };
        CAPTURE(kSweepSeed, i, body.name, sma, ecc.value(), el.tra.value());
        checkRadiusAndSpeed(stateOf(el, body.mu), body.mu);
    }
}

// Out along a hyperbola's asymptote, where 1 + e cos nu cancels for the other
// reason: r / |a| from a thousandth to a thousand, e from just above 1 to 100.
// Beyond 1e3 the elements themselves stop meaning much -- see the note on the
// budget above.
void sweepHyperbolicAsymptotes(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Eccentricity ecc = eccentricityOf(1.0 + sampler.logUniform({.lo = 1e-6, .hi = 99.0}));
        const f64 sma =
            -sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()});
        const f64 slr = -sma * ((ecc.value() * ecc.value()) - 1.0);
        // Never inside periapsis, whatever r/|a| was drawn.
        const f64 radius =
            std::max(slr / (1.0 + ecc.value()), -sma * sampler.logUniform({.lo = 1e-3, .hi = 1e6}));
        const f64 tra = std::acos(std::clamp(((slr / radius) - 1.0) / ecc.value(), -1.0, 1.0));
        const Elements el{
            .sma = Metres{sma},
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = Radians{(sampler.fraction() < 0.5) ? tra : kTau - tra},
            .slr = Metres{slr},
        };
        CAPTURE(kSweepSeed, i, body.name, sma, ecc.value(), radius, el.tra.value());
        checkRadiusAndSpeed(stateOf(el, body.mu), body.mu);
    }
}

} // namespace

// Five families, one Sampler, drawn from in this order, as the sweep above is:
// reordering these calls changes every case.
TEST_CASE("orbitInfo's radius and speed stay within the elements' conditioning",
          "[orbit][scales]") {
    Sampler sampler;
    sweepOrdinaryOrbits(sampler);
    sweepNearlyRadialOrbits(sampler);
    sweepNearParabolicOrbits(sampler);
    sweepSmallEccentricities(sampler);
    sweepHyperbolicAsymptotes(sampler);
}

// --- element propagation, across the parabola --------------------------------

namespace {

// propagateElements runs the same universal-variable solve propagate() does,
// driven from the elements. The two therefore share a solver but not an input:
// one takes 1/a from `sma`, the other recovers it from 2/r - v^2/mu, and near
// e = 1 those differ by everything that cancellation costs. Agreement between
// them is still evidence, because the inputs travel different routes.
//
// Measured against 60-digit references over 40,024 element sets (2026-09-12),
// element propagation is within 7.0e-12 of the radius everywhere, and the state
// propagator within 5.3e-11, so 1e-9 for the pair is a hundredfold margin.
// Reversibility is the tighter claim -- the same solve run backwards -- and is
// held to 1e-11.
constexpr std::size_t kPropagationCases = 500; // per family below
constexpr Tolerance kPropagatorsAgree{1e-9};

// Stepping back is not free of the round trip's conditioning, and three things
// set what it can be worth. The anomaly at the far end is a double, and the
// position there moves with it by kappa = |e sin nu| r / p per radian -- the
// same kappa the radius and the speed carry above. The time is a double too:
// the step is known to about u |dt|, which displaces the far end along its own
// path by |dt| v / r of the radius, and a hundred revolutions of a closed orbit
// is fifty of those. And an error at the far end comes back multiplied: the far
// end of an eccentric orbit is the slow one, so an error there is a longer time
// and therefore a bigger arc back here, by the ratio of the two angular rates,
// (v_here r_far) / (v_far r_here).
//
// Measured over 40,024 round trips (2026-09-12), the worst needed 473 times
// u (1 + kappa + swept)(1 + leverage) -- on an ordinary orbit, where all three
// terms are small and the constant is doing the work -- and 98 near a parabola,
// where they are not. The factor below is four times that worst.
constexpr f64 kReversibilityFactor = 2000.0;

[[nodiscard]] Tolerance
reversibilityBudget(const Elements& start, const Elements& end, GravParam mu, Seconds dt) {
    const OrbitInfo far = orbitInfo(end, mu);
    const OrbitInfo home = orbitInfo(start, mu);
    const f64 kappa = std::abs(end.ecc.value() * std::sin(end.tra.value())) * far.radius.value() /
                      end.slr.value();
    const f64 swept = std::abs(dt.value()) * home.speed.value() / home.radius.value();
    const f64 leverage =
        (home.speed.value() * far.radius.value()) / (far.speed.value() * home.radius.value());
    return Tolerance{kReversibilityFactor * kUnitRoundoff * (1.0 + kappa + swept) *
                     (1.0 + leverage)};
}

// The element set of a conic at a given distance from e = 1, built from p and a
// because that pair carries e - 1 to full relative precision. `offset` is
// e - 1: negative for an ellipse, positive for a hyperbola, zero for the
// parabola, which takes an infinite semi-major axis.
[[nodiscard]] Elements conicNear(Metres slr, f64 offset, Radians trueAnomaly, Sampler& sampler) {
    const f64 eccentricity = 1.0 + offset;
    // fpclassify rather than `== 0.0`, which -Wfloat-equal reports: the two
    // agree on every input, and this one says which question it asks.
    const f64 sma = (std::fpclassify(offset) == FP_ZERO)
                        ? kInf
                        : slr.value() / (1.0 - (eccentricity * eccentricity));
    return {
        .sma = Metres{sma},
        .ecc = eccentricityOf(std::isfinite(sma) ? std::sqrt(1.0 - (slr.value() / sma)) : 1.0),
        .inc = sampler.angle(kPi),
        .lan = sampler.angle(kTau),
        .aop = sampler.angle(kTau),
        .tra = trueAnomaly,
        .slr = slr,
    };
}

// One element set and one step: the two propagators land in the same place, and
// stepping back returns the anomaly it started from.
void checkElementPropagation(const Elements& el, GravParam mu, Seconds dt) {
    const auto moved = propagateElements(el, mu, dt);
    INFO("propagateElements -> " << errorName(moved));
    REQUIRE(moved.has_value());

    const auto viaState = propagate(stateOf(el, mu), mu, dt);
    INFO("propagate -> " << errorName(viaState));
    REQUIRE(viaState.has_value());
    INFO(std::format("true anomaly {:.17g} -> {:.17g}", el.tra.value(), moved->tra.value()));
    REQUIRE_THAT(stateOf(*moved, mu).pos, WithinRelVec(viaState->pos, kPropagatorsAgree));

    const auto back = propagateElements(*moved, mu, -dt);
    INFO("propagateElements back -> " << errorName(back));
    REQUIRE(back.has_value());
    const Tolerance budget = reversibilityBudget(el, *moved, mu, dt);
    INFO(std::format("reversibility budget {:.3g}", budget.value()));
    REQUIRE_THAT(stateOf(*back, mu).pos, WithinRelVec(stateOf(el, mu).pos, budget));
}

// A true anomaly the conic actually reaches: a hyperbola only covers the arc
// inside its asymptotes, and the ends of that arc are where p / (1 + e cos nu)
// runs away.
[[nodiscard]] Radians anomalyOn(const Elements& el, Sampler& sampler) {
    if (el.ecc.value() <= 1.0) return sampler.angle(kTau);
    const f64 limit = std::acos(-1.0 / el.ecc.value());
    const f64 inside = 1.0 - sampler.logUniform({.lo = 1e-9, .hi = 0.5});
    return Radians{((2.0 * sampler.fraction()) - 1.0) * limit * inside};
}

// Ordinary shapes, as a control: nothing here is near a parabola.
void sweepOrdinaryPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Metres slr{
            sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()})};
        const bool closed = sampler.fraction() < 0.5;
        const f64 offset =
            closed ? -(0.05 + (sampler.fraction() * 0.9)) : 0.05 + (sampler.fraction() * 4.0);
        Elements el = conicNear(slr, offset, Radians{0.0}, sampler);
        el.tra = anomalyOn(el, sampler);
        const f64 scale = std::abs(el.sma.value());
        const Seconds dt{sampler.logUniform({.lo = 1e-3, .hi = 1e2}) *
                         std::sqrt(scale * scale * scale / body.mu.value()) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, slr.value(), el.ecc.value(), el.tra.value(), dt.value());
        checkElementPropagation(el, body.mu, dt);
    }
}

// Either side of the parabola, from a tenth away down to the last bit a double
// can hold: the band the old formulation refused, and the decade outside it
// where it was still 1.5% out.
void sweepNearParabolicPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Metres slr{
            sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()})};
        const f64 offset = sampler.logUniform({.lo = 1e-16, .hi = 1e-1}) *
                           ((sampler.fraction() < 0.5) ? 1.0 : -1.0);
        Elements el = conicNear(slr, offset, Radians{0.0}, sampler);
        el.tra = anomalyOn(el, sampler);
        // The time scale of a parabola has no semi-major axis in it: sqrt of
        // the periapsis distance cubed over mu is the one every conic shares.
        const f64 periapsis = 0.5 * slr.value();
        const Seconds dt{sampler.logUniform({.lo = 1e-2, .hi = 1e4}) *
                         std::sqrt(periapsis * periapsis * periapsis / body.mu.value()) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, slr.value(), offset, el.tra.value(), dt.value());
        checkElementPropagation(el, body.mu, dt);
    }
}

// The parabola itself, which had no answer at all before 2026-09-12.
void sweepParabolicPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Metres slr{
            sampler.logUniform({.lo = body.minPeriapsis.value(), .hi = body.maxSma.value()})};
        Elements el = conicNear(slr, 0.0, Radians{0.0}, sampler);
        el.tra = anomalyOn(el, sampler);
        const f64 periapsis = 0.5 * slr.value();
        const Seconds dt{sampler.logUniform({.lo = 1e-2, .hi = 1e4}) *
                         std::sqrt(periapsis * periapsis * periapsis / body.mu.value()) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, slr.value(), el.tra.value(), dt.value());
        checkElementPropagation(el, body.mu, dt);
    }
}

// Small eccentricities, where the anomaly is measured from a periapsis that is
// barely there: the case that reading the new anomaly off p/r - 1 could not
// answer, because on a circle there is nothing to measure from.
void sweepNearCircularPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Eccentricity ecc = eccentricityOf(sampler.logUniform({.lo = 1e-13, .hi = 1e-2}));
        const f64 sma = sampler.logUniform(
            {.lo = body.minPeriapsis.value() / (1.0 - ecc.value()), .hi = body.maxSma.value()});
        const Elements el{
            .sma = Metres{sma},
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = sampler.angle(kTau),
            .slr = Metres{sma * (1.0 - (ecc.value() * ecc.value()))},
        };
        const Seconds dt{sampler.logUniform({.lo = 1e-3, .hi = 1e2}) *
                         std::sqrt(sma * sma * sma / body.mu.value()) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, sma, ecc.value(), el.tra.value(), dt.value());
        checkElementPropagation(el, body.mu, dt);
    }
}

} // namespace

// Four families, one Sampler, drawn from in this order.
TEST_CASE("element propagation agrees with the state propagator on every conic",
          "[orbit][scales]") {
    Sampler sampler;
    sweepOrdinaryPropagation(sampler);
    sweepNearParabolicPropagation(sampler);
    sweepParabolicPropagation(sampler);
    sweepNearCircularPropagation(sampler);
}

// --- elementsFromState, element by element ---------------------------------

namespace {

// Every element of one measured state, with the reference computed at 60
// decimal digits from those exact input doubles and then rounded to the
// nearest double. The reference follows the same definitions the code does --
// p is |h|^2/mu, e is the eccentricity vector's length, nu is the angle from
// that vector to the position -- and evaluates them exactly, so the difference
// is the implementation's own rounding and nothing else.
//
// Each of these was the worst state for one element among 56,532 drawn across
// nine families (2026-09-12). Two kinds of designed behaviour are excluded
// from the selection rather than asserted against: the parabolic band, where
// an infinite sma is the intended answer, and the nudge in assignConic that
// moves an eccentricity rounding to 1 onto the side of 1 the energy says.
struct ElementCase {
    std::string_view name;
    std::string_view symptom;
    StateVector state;
    GravParam mu;
    Elements want;
};

// 40 u, the same factor orbitInfo's contract uses. Every element of every case
// below was measured at or under 9.4 u with the double-double conversion, the
// loosest being 1.04e-15, so the budget is four times the worst measurement.
// The angles are compared absolutely, in radians, because they are O(1)
// quantities and a relative error would be meaningless near zero.
constexpr f64 kElementFactor = 40.0;
constexpr f64 kElementBudget = kElementFactor * kUnitRoundoff;

// Shortest way round the circle, so 0 and tau are the same angle.
// The two are interchangeable: this is |got - want| folded onto the circle and
// then the shorter of the two arcs, both of which are symmetric, so
// transposing them cannot produce a wrong answer.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] f64 angularError(Radians got, Radians want) {
    const f64 wrapped = std::fmod(std::abs(got.value() - want.value()), kTau);
    return std::min(wrapped, kTau - wrapped);
}

void checkEveryElement(const ElementCase& test) {
    const auto el = elementsFromState(test.state, test.mu);
    INFO("elementsFromState -> " << errorName(el));
    REQUIRE(el.has_value());

    INFO(std::format("case \"{}\": {}", test.name, test.symptom));
    INFO(std::format("budget {:.3g} relative, and the same in radians", kElementBudget));

    struct Magnitude {
        std::string_view name;
        f64 got;
        f64 want;
    };
    for (const Magnitude& m : std::to_array<Magnitude>({
             {.name = "sma", .got = el->sma.value(), .want = test.want.sma.value()},
             {.name = "ecc", .got = el->ecc.value(), .want = test.want.ecc.value()},
             {.name = "slr", .got = el->slr.value(), .want = test.want.slr.value()},
         })) {
        INFO(std::format("{} {:.17g}, want {:.17g}, out by {:.3g}",
                         m.name,
                         m.got,
                         m.want,
                         relativeError({.got = m.got, .want = m.want})));
        REQUIRE(relativeError({.got = m.got, .want = m.want}) <= kElementBudget);
    }

    struct Angle {
        std::string_view name;
        Radians got;
        Radians want;
    };
    for (const Angle& a : std::to_array<Angle>({
             {.name = "inc", .got = el->inc, .want = test.want.inc},
             {.name = "lan", .got = el->lan, .want = test.want.lan},
             {.name = "aop", .got = el->aop, .want = test.want.aop},
             {.name = "tra", .got = el->tra, .want = test.want.tra},
         })) {
        INFO(std::format("{} {:.17g} rad, want {:.17g}, out by {:.3g}",
                         a.name,
                         a.got.value(),
                         a.want.value(),
                         angularError(a.got, a.want)));
        REQUIRE(angularError(a.got, a.want) <= kElementBudget);
    }
}

} // namespace

// Seven measured states, one per failure mechanism. Before the double-double
// conversion these were out by up to 1.1e-5 in the semi-latus rectum of a
// nearly radial orbit, 7.1e-4 in the semi-major axis of a near-parabolic one,
// and 0.98 -- that is, all of it -- in the semi-latus rectum at 1e-112 m.
TEST_CASE("elementsFromState is accurate to the resolution of a double", "[orbit][scales]") {
    // Nearly radial. |h| is the difference of two products that agree to
    // fifteen digits, so |h|^2/mu keeps almost none of them, and every angle
    // measured from the node goes with it.
    SECTION("a nearly radial orbit, semi-latus rectum") {
        checkEveryElement({
            .name = "radial/slr",
            .symptom = "slr was out by 1.1e-5, lan by 1.8e-6 rad",
            .state =
                {
                    .pos = {98115305.6517241, -36899386.05634015, -63434678.16796173},
                    .vel = {35263.74015082627, -13262.052775336871, -22799.134065214468},
                },
            .mu = gravParam(1.26686534e17),
            .want =
                {
                    .sma = Metres{984109853.7716752},
                    .ecc = eccentricity(1.0),
                    .inc = Radians{1.6530067199888987},
                    .lan = Radians{5.873583714414224},
                    .aop = Radians{2.595342089300744},
                    // kPi plus the exact offset rather than the decimal, which
                    // is the same double: a nearly radial orbit sits at its far
                    // apsis, so the reference is always within a nanoradian of
                    // pi, and `modernize-use-std-numbers` reports any literal
                    // that close to a named constant. Written this way it also
                    // says what it means.
                    .tra = Radians{kPi + -2.4008794952123935e-11},
                    .slr = Metres{3.765682093767872e-14},
                },
        });
    }

    SECTION("a nearly radial orbit, in-plane angles") {
        checkEveryElement({
            .name = "radial/aop",
            .symptom = "aop was out by 7.5e-6 rad, lan by 8.3e-6, inc by 2.5e-6, slr by 1.1e-5",
            .state =
                {
                    .pos = {41748328800.00065, 50413494846.746025, 28790214535.44694},
                    .vel = {95.73084603298135, 115.60047196533479, 66.01729156934707},
                },
            .mu = gravParam(398600441800000.0),
            .want =
                {
                    .sma = Metres{-25327823861.335873},
                    .ecc = eccentricity(1.0),
                    .inc = Radians{2.7026987962183653},
                    .lan = Radians{2.0930065922253807},
                    .aop = Radians{4.3868367873725855},
                    .tra = Radians{kPi + -5.193623309196482e-11},
                    .slr = Metres{3.999035826672676e-11},
                },
        });
    }

    // Outside the parabolic band, so an infinite sma is not the answer here:
    // v^2/2 and mu/r agree to twelve digits and the energy keeps four.
    SECTION("a near-parabolic orbit outside the band, semi-major axis") {
        checkEveryElement({
            .name = "parabolic/sma",
            .symptom = "sma was out by 7.1e-4, at |alpha r| = 1.2e-9, outside the 1e-12 band",
            .state =
                {
                    .pos = {-640755300159.4368, 29100549031.838127, 346946167705.056},
                    .vel = {-2.2807014201260682, 32.969662883052614, -0.9995679123141444},
                },
            .mu = gravParam(398600441800000.0),
            .want =
                {
                    .sma = Metres{6.583308070576134e+23},
                    .ecc = eccentricity(0.9999999999989005),
                    .inc = Radians{2.6396867135064164},
                    .lan = Radians{4.836596791006594},
                    .aop = Radians{1.5475516847830715},
                    .tra = Radians{0.1722502705583468},
                    .slr = Metres{1447681396713.5178},
                },
        });
    }

    // Far out along a hyperbola's asymptote, where the eccentricity vector
    // cancels for the other reason.
    SECTION("a hyperbola far out along its asymptote") {
        checkEveryElement({
            .name = "asymptote/slr",
            .symptom = "slr was out by 3.7e-8",
            .state =
                {
                    .pos = {184933078210043.56, 12227088438166.695, 154761708918660.75},
                    .vel = {515460.5071996036, 34080.33415690606, 431364.41700335406},
                },
            .mu = gravParam(1.32712440018e20),
            .want =
                {
                    .sma = Metres{-293005379.96767074},
                    .ecc = eccentricity(1.0000034505920776),
                    .inc = Radians{1.7765735746548228},
                    .lan = Radians{0.24121218097744143},
                    .aop = Radians{3.858093101748927},
                    .tra = Radians{3.138965643681166},
                    .slr = Metres{2022.0875743110084},
                },
        });
    }

    // Nearly circular: the eccentricity vector is the difference of two
    // vectors agreeing to nine digits, and its length is what is left.
    SECTION("a nearly circular orbit, eccentricity") {
        checkEveryElement({
            .name = "smalle/ecc",
            .symptom = "ecc was out by 1.7e-7 at a true e of 2.4e-9",
            .state =
                {
                    .pos = {89977497286.24823, 325806749198.3961, -6363974265.222106},
                    .vel = {-33.02472617867604, 9.071987671541756, -2.4776024906511935},
                },
            .mu = gravParam(398600441800000.0),
            .want =
                {
                    .sma = Metres{338062845427.7559},
                    .ecc = eccentricity(2.410249444739502e-09),
                    .inc = Radians{0.07463870595147103},
                    .lan = Radians{4.18840882891478},
                    .aop = Radians{0.26957632809726984},
                    .tra = Radians{3.1272246403208896},
                    .slr = Metres{338062845427.7559},
                },
        });
    }

    // A whole orbit at 1e-112 m around a mu of 1e-213. Nothing here is
    // physical; it is the scale at which every squared quantity falls into the
    // subnormals, and the elements are still a well-posed function of the
    // input doubles.
    SECTION("an orbit far below any physical scale") {
        checkEveryElement({
            .name = "tiny/slr",
            .symptom = "slr was out by 0.98 and aop by 0.31 rad -- the squares went subnormal",
            .state =
                {
                    .pos =
                        {
                            1.1255796242117575e-112,
                            -2.335524288160757e-112,
                            -1.7843712448117626e-112,
                        },
                    .vel =
                        {
                            -5.622784877953291e-51,
                            1.129129308921512e-52,
                            2.0383290377229924e-51,
                        },
                },
            .mu = gravParam(6.762040535602958e-213),
            .want =
                {
                    .sma = Metres{9.408723759066775e-112},
                    .ecc = eccentricity(0.7793327632486454),
                    .inc = Radians{2.537170817576543},
                    .lan = Radians{3.673972445843512},
                    .aop = Radians{6.126893603302964},
                    .tra = Radians{4.9372615743387795},
                    .slr = Metres{3.6942454754304065e-112},
                },
        });
    }

    // A small mu with a large state: the division by mu in the eccentricity
    // vector is where this one loses its digits.
    SECTION("an orbit around a very small gravitational parameter") {
        checkEveryElement({
            .name = "muwide/sma",
            .symptom = "sma was out by 1.2e-11 with mu = 7.3e7",
            .state =
                {
                    .pos = {33533205800013.06, 17263837004043.322, -2700443730722.6323},
                    .vel = {0.001461083271751538, 0.0011796325768950588, -0.00055901478564361},
                },
            .mu = gravParam(72579463.2331308),
            .want =
                {
                    .sma = Metres{8.272163997668141e+17},
                    .ecc = eccentricity(0.9999961169100106),
                    .inc = Radians{0.844951349416489},
                    .lan = Radians{3.5534480537245505},
                    .aop = Radians{0.6871285419999764},
                    .tra = Radians{2.550093699048564},
                    .slr = Metres{6424298968899.643},
                },
        });
    }
}

// --- the perifocal velocity's second component ------------------------------

// e + cos v, at the one place it decides an answer.
//
// `propagateElements` reads the new true anomaly off the perifocal position,
// whose second component is f r0 sin v + g sqrt(mu/p) (e + cos v). Near v = pi
// that last factor is e - 1, and `ecc` cannot supply it: `ecc` stores a number
// next to 1, so it carries about 1.1e-16 of absolute error, which is the whole
// of e - 1 once 1 - e falls below about 1e-14. Orbit.cpp takes it from p and a
// instead, through e^2 - 1 = -p/a.
//
// Until 2026-09-13 nothing pinned that, and the comment there said so: written
// straight, the difference was measured at no more than 9.7e-13 in the
// propagated *position*, which is under any budget the suite can justify. The
// position was the wrong thing to measure. This is an orbit at apoapsis, where
// the radius is stationary in the anomaly, so a large error in the anomaly
// barely moves the position at all -- and the anomaly is what
// `propagateElements` returns.
//
// Measured over 504 element sets at 1 - e from 2e-16 to 1e-4 and true anomalies
// within 1e-2 of pi (2026-09-13), worst error in the returned anomaly:
//
//   1 - e     straight      from p and a
//   1e-4      5.16e-15      1.68e-16
//   1e-6      1.05e-13      2.60e-17
//   1e-10     7.81e-12      1.80e-16
//   1e-15     3.70e-09      1.66e-16
//   2e-16     6.56e-09      1.05e-16
//
// The case below is the 1 - e = 1e-6 row, chosen because it is the most
// ordinary orbit that still separates the two decisively: a semi-major axis of
// 7e12 m is about 47 AU, and the straight form misses by 24 times this budget
// while the form in the code comes within a fifth of an ulp.
//
// The expected anomaly is the Kepler equation of the conic that p and a name,
// solved in 60-digit decimal arithmetic from these same doubles
// (`reference.py`, as the goldens above use).
TEST_CASE("the perifocal velocity keeps e - 1 where ecc cannot", "[orbit][scales]") {
    const Elements el{
        .sma = Metres{7000000000000.0},
        .ecc = eccentricity(1.0 - 1e-6),
        .inc = Radians{0.4},
        .lan = Radians{0.9},
        .aop = Radians{1.7},
        // pi + 1e-8. Written as the sum because the literal itself would be
        // within 1e-3 of pi, which `modernize-use-std-numbers` reports.
        .tra = Radians{kPi + 9.99999993922529e-09},
        .slr = Metres{13999992.999999998},
    };
    const GravParam mu = gravParam(398600441800000.0);
    const Seconds dt{2331406655074.406};
    constexpr f64 kBudget = 40.0 * kUnitRoundoff;

    const auto moved = propagateElements(el, mu, dt);
    INFO("propagateElements -> " << errorName(moved));
    REQUIRE(moved.has_value());

    constexpr f64 kExpected = 3.1429299035323592;
    INFO(std::format("tra {:.17g} rad, want {:.17g}, out by {:.3g}, budget {:.3g}",
                     moved->tra.value(),
                     kExpected,
                     angularError(moved->tra, Radians{kExpected}),
                     kBudget));
    REQUIRE(angularError(moved->tra, Radians{kExpected}) <= kBudget);
}

// --- what the conversion promises on every toolchain ------------------------

namespace {

// SplitMix64, written out rather than taken from <random>, because the states
// this generates have to be the same on every toolchain and nothing in the
// standard library guarantees that. The engines do specify their sequences, but
// the distributions do not: std::uniform_real_distribution's algorithm is left
// to the library, so MSVC's and libstdc++'s disagree given the same engine. exp,
// log, sin and cos disagree by an ulp between the UCRT and glibc for the same
// reason. Either would hand the three toolchains different states, and
// comparing their outputs would then prove nothing -- which is what happened
// the first time this was measured.
//
// Three lines of unsigned integer arithmetic have none of those problems.
[[nodiscard]] std::uint64_t mixedBits(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
}

// A double from those bits: a sign, a 52-bit mantissa and an exponent,
// assembled with scalbn, which is exact.
// A range of binary exponents, so that the two bounds cannot transpose: with
// them reversed the subtraction below is negative and the cast to an unsigned
// modulus is nonsense. Local to this suite, as its twin in
// tests/test_double_double.cpp is to that one -- the two suites share no
// header, and adding one for four lines would cost more than it saves.
struct ExponentRange {
    int lowest{};
    int highest{};
};

[[nodiscard]] f64 exactlyScaledDouble(std::uint64_t& state, const ExponentRange& range) {
    const std::uint64_t bits = mixedBits(state);
    const f64 mantissa = 1.0 + (static_cast<f64>(bits >> 12U) * 0x1p-52);
    const int exponent =
        range.lowest +
        static_cast<int>((bits >> 1U) % static_cast<std::uint64_t>(range.highest - range.lowest));
    const f64 withSign = ((bits & 1U) != 0U) ? -mantissa : mantissa;
    return std::scalbn(withSign, exponent);
}

// FNV-1a over the bytes of a double, so the sum sees the exact bits rather
// than a printed approximation of them. `bitsOf` is core/Scalar.hpp's, which
// is a bit_cast rather than a memcpy -- the latter is what clang's
// -Wunsafe-buffer-usage-in-libc-call reports.
void foldBits(std::uint64_t& hash, f64 value) {
    const std::uint64_t bits = bitsOf(value);
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        hash ^= (bits >> shift) & 0xffU;
        hash *= 0x100000001b3ULL;
    }
}

} // namespace

// The semi-major axis, the eccentricity and the semi-latus rectum are identical
// on every conforming target, and the four angles are not.
//
// That split is not a shortcoming of the conversion, it is where the language
// stops making promises. Those three come out of +, -, *, / and sqrt, every one
// of which IEEE 754 requires to be correctly rounded, so they are the same bits
// everywhere -- and the double-double arithmetic they are built on uses nothing
// else, deliberately, which is why Dekker's splitting is there instead of
// std::fma. The angles additionally pass through atan2, acos and hypot, whose
// accuracy the standard leaves to the implementation; measured, all three
// disagree between the UCRT and glibc on identical inputs.
//
// So this pins the half that can be pinned. The checksum below was equal on
// clang for Windows, clang under WSL and gcc-14 when it was taken, and if a
// change breaks that -- an std::fma slipped into the kit, -ffp-contract left
// on, a reassociated sum -- the number moves and this fails. A caller may rely
// on the three magnitudes being reproducible across machines; it may not rely
// on that for the angles, and nothing here should be extended to claim it.
TEST_CASE("the conversion's magnitudes are identical on every toolchain", "[orbit][scales]") {
    // Written down so the sweep can be reproduced, as every sweep here is.
    constexpr std::uint64_t kSeed = 20260913;
    constexpr int kCases = 4000;
    // Committed golden. Regenerate only with a reason, and only after checking
    // the new value on all three toolchains.
    constexpr std::uint64_t kGolden = 0x3489b7982d902731ULL;

    std::uint64_t engine = kSeed;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    std::size_t accepted = 0;
    for (int i = 0; i < kCases; ++i) {
        const StateVector sv{
            .pos =
                {
                    exactlyScaledDouble(engine, {.lowest = -60, .highest = 60}),
                    exactlyScaledDouble(engine, {.lowest = -60, .highest = 60}),
                    exactlyScaledDouble(engine, {.lowest = -60, .highest = 60}),
                },
            .vel =
                {
                    exactlyScaledDouble(engine, {.lowest = -40, .highest = 40}),
                    exactlyScaledDouble(engine, {.lowest = -40, .highest = 40}),
                    exactlyScaledDouble(engine, {.lowest = -40, .highest = 40}),
                },
        };
        const GravParam mu =
            gravParamOf(std::abs(exactlyScaledDouble(engine, {.lowest = -20, .highest = 60})));
        const auto el = elementsFromState(sv, mu);
        if (!el) continue;
        ++accepted;
        foldBits(hash, el->sma.value());
        foldBits(hash, el->ecc.value());
        foldBits(hash, el->slr.value());
    }

    // A checksum over nothing would match anywhere, so the count is asserted
    // too -- and it is itself toolchain-independent, since the accept/reject
    // decision rests on the same exact quantities.
    INFO(std::format("{} of {} states accepted, checksum {:#018x}", accepted, kCases, hash));
    REQUIRE(accepted > static_cast<std::size_t>(kCases) / 4);
    REQUIRE(hash == kGolden);
}

// The rectilinear threshold, at the boundary, from the correct side.
//
// The guard is |h| / (|r| |v|) > 1e-12 -- the sine of the angle between position
// and velocity, so it carries no scale. Until 2026-09-12 it was written as
// |h| > 1e-12 |r| |v| with |h| from a plain cross product, which is the one
// place in the conversion where cancellation is total: on a nearly radial
// trajectory the three components of r x v are each the difference of two
// products agreeing to fifteen digits.
//
// These two states are what that cost. Their exact |h| / (|r| |v|), in 60-digit
// arithmetic, is 9.99994e-13 and 9.99983e-13 -- both below the threshold, so
// both are radial trajectories and refusing them is the right answer. The old
// form accepted them, because its |h| came out large enough to clear the bar it
// was being compared against.
//
// They were found by classifying 200,000 states whose sine straddles 1e-12 by
// six decades either way under both implementations (2026-09-13). These two are
// the only disagreements, and there were none in the other direction: no state
// the old form refused is accepted now.
TEST_CASE("a radial trajectory at the threshold is refused, not parameterised", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        StateVector state;
    };
    for (const Case& test : std::to_array<Case>({
             {
                 .name = "sine 9.99994e-13",
                 .state =
                     {
                         .pos =
                             {
                                 617320698096.53174,
                                 8191941311512.3789,
                                 -6772527356399.6055,
                             },
                         .vel =
                             {
                                 0.38650210626152892,
                                 5.1289428347254473,
                                 -4.2402532362783445,
                             },
                     },
             },
             {
                 .name = "sine 9.99983e-13",
                 .state =
                     {
                         .pos =
                             {
                                 -2767898319634.1138,
                                 1879302166115.7432,
                                 -9420703505632.8281,
                             },
                         .vel =
                             {
                                 -1.3400261983864743,
                                 0.90982899169791631,
                                 -4.5608573896166167,
                             },
                     },
             },
         })) {
        CAPTURE(test.name);
        const auto el = elementsFromState(test.state, kMuEarth);
        INFO("elementsFromState -> " << errorName(el));
        // REQUIRE(!...) rather than REQUIRE_FALSE: the latter takes a path
        // through Catch2's result flags that clang-analyzer reports as an
        // out-of-range enum cast inside the library's own header, and the
        // suppression for that would be ours to carry for a defect that is not.
        REQUIRE(!el.has_value());
        REQUIRE(el.error() == OrbitError::RectilinearOrbit);
    }
}

// --- stateFromElements, where the elements cancel ---------------------------

// A nearly radial hyperbola, from libFuzzer's own family: 1 + e cos v is the
// difference of two numbers either side of 1, and computed straight from `ecc`
// it comes out exactly zero. The radius is then infinite, the rotation turns the
// infinities into a NaN, and the position is a NaN through a signature that used
// to have no way of saying so. 169 of 10,000 nearly radial element sets that
// elementsFromState itself produced did this (2026-09-13).
//
// The reference is the radius these elements describe, taken the way Orbit.cpp
// takes it: e - 1 from p and a rather than from `ecc`, since e^2 - 1 = -p/a
// keeps it to full relative precision. Evaluated in 60-digit decimal arithmetic,
// 1 + e cos v is 4.34e-17 and the radius 1548134.2629609336 m; the state these
// elements came from was at 1548134.2829065896 m, and the 1.3e-8 between those
// is the conditioning of the encoding, not an error in either.
TEST_CASE("a nearly radial hyperbola's state is not a NaN position", "[orbit][scales]") {
    const Elements el{
        .sma = Metres{-375379.36666112917},
        .ecc = eccentricity(1.0000000000000002),
        .inc = Radians{1.7465823605526356},
        .lan = Radians{2.711287424773329},
        .aop = Radians{1.5649763444984528},
        // pi - 1.63e-8, as the sum because the literal is within 1e-3 of pi.
        .tra = Radians{kPi + -1.630431922805542e-08},
        .slr = Metres{6.719943304116789e-11},
    };
    const GravParam mu = gravParam(1.26686534e17);

    const auto sv = stateFromElements(el, mu);
    INFO("stateFromElements -> " << errorName(sv));
    REQUIRE(sv.has_value());

    constexpr f64 kExpectedRadius = 1548134.2629609336;
    constexpr f64 kExpectedSpeed = 707921.4896974104;
    INFO(std::format("radius {:.17g} m, want {:.17g}; speed {:.17g} m/s, want {:.17g}",
                     length(sv->pos).value(),
                     kExpectedRadius,
                     length(sv->vel).value(),
                     kExpectedSpeed));
    REQUIRE(relativeError({.got = length(sv->pos).value(), .want = kExpectedRadius}) <= 1e-12);
    REQUIRE(relativeError({.got = length(sv->vel).value(), .want = kExpectedSpeed}) <= 1e-12);
}

// The same cancellation without the NaN, which is the worse failure of the two
// because nothing downstream can see it.
//
// `ecc` here stores the double nearest 1 - 1e-16, which is 1 - 1.11e-16, while p
// and a say e - 1 is -5e-17: p/a is 1e-16 and e^2 - 1 = -p/a. The two disagree by
// a factor of 2.2, and 1 + e cos v at v = pi *is* that difference, so the radius
// came back 1.26101e23 m where the elements describe 2.8e23. A plausible number,
// wrong by more than half, with no flag on it.
TEST_CASE("an eccentricity that rounds to 1 does not halve the radius", "[orbit][scales]") {
    const Elements el{
        .sma = Metres{1.4e23},
        .ecc = eccentricity(1.0 - 1e-16),
        .inc = Radians{0.4},
        .lan = Radians{0.9},
        .aop = Radians{1.7},
        .tra = Radians{kPi},
        .slr = Metres{1.4e7},
    };
    const GravParam mu = gravParam(3.986004418e14);

    const auto sv = stateFromElements(el, mu);
    INFO("stateFromElements -> " << errorName(sv));
    REQUIRE(sv.has_value());

    // p and a are authoritative for the shape, which is the convention the
    // function states: the semi-latus rectum is the one shape parameter that
    // survives a parabola.
    constexpr f64 kExpectedRadius = 2.7999999999999998e+23;
    INFO(std::format("radius {:.17g} m, want {:.17g}, out by {:.3g}",
                     length(sv->pos).value(),
                     kExpectedRadius,
                     relativeError({.got = length(sv->pos).value(), .want = kExpectedRadius})));
    REQUIRE(relativeError({.got = length(sv->pos).value(), .want = kExpectedRadius}) <= 1e-12);
}

// An anomaly the conic never reaches is refused rather than answered.
//
// A hyperbola's asymptote is at acos(-1/e); beyond it there is no trajectory, so
// 1 + e cos v goes negative, the radius with it, and the position comes out
// mirrored through the focus. elementsFromState never produces such an element
// set, but a scenario file can write one down, which is why this is reported and
// not asserted. orbitInfo shows the same state as a negative radius, and says so
// in its contract.
TEST_CASE("an anomaly past a hyperbola's asymptote is refused", "[orbit][scales]") {
    // e = 1.5 puts the asymptote at 2.3005 rad; pi is past it.
    const Elements el{
        .sma = Metres{-1.12e7},
        .ecc = eccentricity(1.5),
        .inc = Radians{0.4},
        .lan = Radians{0.9},
        .aop = Radians{1.7},
        .tra = Radians{kPi},
        .slr = Metres{1.4e7},
    };
    const GravParam mu = gravParam(3.986004418e14);

    const auto sv = stateFromElements(el, mu);
    INFO("stateFromElements -> " << errorName(sv));
    REQUIRE(!sv.has_value());
    REQUIRE(sv.error() == OrbitError::UnreachableAnomaly);

    // Just inside the asymptote is an ordinary point on the trajectory, so the
    // refusal is of the anomaly and not of the orbit.
    Elements inside = el;
    inside.tra = Radians{2.2};
    const auto ok = stateFromElements(inside, mu);
    INFO("just inside -> " << errorName(ok));
    REQUIRE(ok.has_value());
}
