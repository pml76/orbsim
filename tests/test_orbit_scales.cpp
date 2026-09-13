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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
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

// The energy and the semi-major axis of a state, computed here rather than by
// the code under test: std::hypot for the lengths, and none of the orbit
// code's formulas. For the nearly radial cases below.
struct ConicReference {
    SpecificEnergy energy;
    Metres sma; // negative for a hyperbola
    Metres radius;
    MetresPerSecond speed;
};

[[nodiscard]] ConicReference conicReference(const StateVector& sv, GravParam mu) {
    const f64 r = std::hypot(sv.pos.x, sv.pos.y, sv.pos.z);
    const f64 v = std::hypot(sv.vel.x, sv.vel.y, sv.vel.z);
    const f64 energy = (0.5 * v * v) - (mu.value / r);
    return {
        .energy = SpecificEnergy{energy},
        .sma = Metres{-mu.value / (2.0 * energy)},
        .radius = Metres{r},
        .speed = MetresPerSecond{v},
    };
}

// |got / want - 1|, with no floor under `want`: WithinRelTo's floor of 1e-30
// would pass anything at the fuzzer's scale of 1e-159 m. An infinite or NaN
// `got` fails any budget.
[[nodiscard]] f64 relativeError(f64 got, f64 want) { return std::abs((got / want) - 1.0); }

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
constexpr GravParam kFuzzerMu{4.3333423748712802e-35};

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

    // Element propagation used to refuse this element set: no finite semi-major
    // axis, and the classical Kepler equation has no parabolic branch. Since
    // 2026-09-12 it runs the same universal solve propagate() does, where a
    // parabola is the case psi = 0 rather than a special one, so it answers --
    // and the two propagators must land in the same place.
    const auto viaElements = propagateElements(*el, kMuEarth, 100.0_s);
    INFO("propagateElements on a parabola -> " << errorName(viaElements));
    REQUIRE(viaElements.has_value());

    const auto viaState = propagate(para, kMuEarth, 100.0_s);
    INFO(errorName(viaState));
    REQUIRE(viaState.has_value());
    INFO("the two propagators agree on a parabola");
    REQUIRE_THAT(stateFromElements(*viaElements, kMuEarth).pos,
                 WithinRelVec(viaState->pos, Tolerance{1e-12}));
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
    REQUIRE(!describe(OrbitError::DegenerateState).empty());
    REQUIRE(!describe(OrbitError::RectilinearOrbit).empty());
}

// States that pass every input check and still do not describe an orbit. Both
// of these came from the fuzzer rather than from anyone sitting down to think
// of them, which is the argument for rule 13 in one paragraph.
TEST_CASE("states that are finite but are not orbits", "[orbit][scales]") {
    // Finite components whose derived quantities are not. A fuzzer found this
    // (VERIFICATION.md rule 13) when length() squared its components, so |r|
    // reached infinity above about 1.3e154 from inputs that every isfinite()
    // check passes: the elements came back reporting success, with an infinite
    // eccentricity and a NaN argument of periapsis. length() no longer
    // overflows (2026-09-11), but for these states |h|^2 and the eccentricity
    // vector still do, and the answer is still refused rather than returned.
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

// length() is exact wherever its answer is representable, at every scale.
//
// A Pythagorean vector's length is an integer -- |(3, 4, 12)| = 13 -- and
// scaling by a power of two changes only exponents, so (3, 4, 12) * 2^k has
// length 13 * 2^k exactly for every k at which those four numbers are doubles:
// an expected value that owes nothing to the code. sqrt(dot) met it only from
// 2^-537 to 2^508, and was wrong at 1,049 of these 2,095 scales (measured):
// below that band the squares fall into the subnormals, which keep fewer bits
// the smaller they get, and above it they overflow. The first of those is what
// gave the fuzzer's radial hyperbola, below, the wrong eccentricity
// (2026-09-11).
TEST_CASE("length is exact at every binary scale", "[core][scales]") {
    int inexact = 0;
    int firstInexact = 0;
    for (int k = -1074; k <= 1020; ++k) {
        const Vec3 v{std::scalbn(3.0, k), std::scalbn(4.0, k), std::scalbn(12.0, k)};
        const f64 want = std::scalbn(13.0, k);
        if (!nearlyEqual(length(v), want, Tolerance{0.0}) && inexact++ == 0) firstInexact = k;
    }
    INFO(std::format("{} scales inexact, the first at 2^{}", inexact, firstInexact));
    REQUIRE(inexact == 0);
}

// The fuzzer's finding of 2026-09-11 (VERIFICATION.md rule 13), as it found it.
// Position about 1e-158 m, velocity about 9e61 m/s, mu 4.3e-35: an open orbit
// -- the kinetic term of the energy is 2.5 times the potential -- and a nearly
// radial one, with |h| about 1.6e-7 of |r||v|, so e = 1 + 1.8e-13.
// elementsFromState returned a negative semi-major axis, which is a
// hyperbola's, with an eccentricity of 1 - 7e-9, which is an ellipse's;
// orbitInfo believed the eccentricity, called the orbit closed, and took the
// square root of a negative a^3.
//
// The cause was |r|: its square, 1.6e-316, is subnormal, so sqrt(dot) was off
// by 7e-9, and the eccentricity by as much. The test holds e to 8 ulp of a
// reference computed here, independently of core/Math.hpp -- std::hypot for
// the lengths, and e^2 - 1 = 2 E h^2 / mu^2, which cancels nothing. The budget:
// the eccentricity vector is the difference of two terms of about 4 and 5 in
// units where e is 1, each good to an ulp or two, so its length is good to a
// few ulp of 1; 8 ulp sits above that and four million times below the error
// the fuzzer found.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a nearly radial hyperbola at a tiny scale is not reported as closed",
          "[orbit][scales]") {
    const StateVector state = kFuzzerHyperbola;
    const GravParam mu = kFuzzerMu;

    const auto el = elementsFromState(state, mu);
    INFO("elementsFromState -> " << errorName(el));
    REQUIRE(el.has_value());

    const f64 r = std::hypot(state.pos.x, state.pos.y, state.pos.z);
    const f64 v = std::hypot(state.vel.x, state.vel.y, state.vel.z);
    const Vec3 h = cross(state.pos, state.vel);
    const f64 hOverMu = std::hypot(h.x, h.y, h.z) / mu.value;
    const f64 energy = (0.5 * v * v) - (mu.value / r);
    const f64 eSquaredMinusOne = 2.0 * energy * hOverMu * hOverMu;
    const f64 eReference = 1.0 + (eSquaredMinusOne / (1.0 + std::sqrt(1.0 + eSquaredMinusOne)));
    INFO(std::format("ecc {:.17g}, reference {:.17g}", el->ecc.value, eReference));
    INFO("the energy is positive, so the eccentricity exceeds 1");
    REQUIRE(el->ecc.value > 1.0);
    constexpr Tolerance kEccentricityBudget{8.0 * std::numeric_limits<f64>::epsilon()};
    REQUIRE_THAT(el->ecc.value, WithinAbsOf(eReference, kEccentricityBudget));

    const OrbitInfo info = orbitInfo(*el, mu);
    // Formatted by hand: Catch2 prints -4.2e-159 as "-0.0", and 17 significant
    // digits round-trip, so a failure can be pasted back in as a case.
    INFO(std::format("sma {:.17g} m, ecc {:.17g}, period {:.17g} s, mean motion {:.17g} rad/s",
                     el->sma.value,
                     el->ecc.value,
                     info.period.value,
                     info.meanMotion.value));
    INFO("the energy is positive, so the orbit is open");
    REQUIRE_FALSE(info.closed);
    INFO("an open orbit's period and mean motion are infinite and zero, not NaN");
    REQUIRE_FALSE(std::isnan(info.period.value));
    REQUIRE_FALSE(std::isnan(info.meanMotion.value));
    INFO("a negative semi-major axis belongs to a hyperbola, so e > 1");
    const bool agreeOnTheConic = !(el->sma.value < 0.0) || el->ecc.value > 1.0;
    REQUIRE(agreeOnTheConic);

    // And the hyperbola's size and energy, which the band |e - 1| <= 1e-9 hid:
    // see the nearly radial cases below.
    const ConicReference want = conicReference(state, mu);
    INFO(std::format("sma {:.17g} m, want {:.17g}; energy {:.17g} J/kg, want {:.17g}",
                     el->sma.value,
                     want.sma.value,
                     info.energy.value,
                     want.energy.value));
    REQUIRE(relativeError(el->sma.value, want.sma.value) <= kConicBudget.value);
    REQUIRE(relativeError(info.energy.value, want.energy.value) <= kConicBudget.value);
}

// Nearly radial orbits at an ordinary scale: the conic is the energy's to
// decide, not the eccentricity's (2026-09-11).
//
// On a nearly radial trajectory e is within a hair of 1 whatever the energy --
// e^2 - 1 = 2 E h^2 / mu^2, and h is small -- so the band |e - 1| <= 1e-9 that
// elementsFromState called parabolic took in ellipses and hyperbolas alike. A
// probe 7000 km from Earth's centre, drifting sideways at 1 mm/s, is at the
// apoapsis of an ellipse with a = 3500 km and a period of 2061 s, and
// e = 1 - 1.8e-14; it was reported as a parabola: open, energy 0, no period.
// The same probe leaving at 20 km/s is a hyperbola, e = 1 + 4.4e-14, with
// 1.4e8 J/kg to spare; it was reported with energy 0.
TEST_CASE("a nearly radial ellipse is closed, with its period", "[orbit][scales]") {
    const StateVector state{.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 1.0e-3, 0.0}};
    const auto el = elementsFromState(state, kMuEarth);
    INFO("elementsFromState -> " << errorName(el));
    REQUIRE(el.has_value());

    const ConicReference want = conicReference(state, kMuEarth);
    const f64 a = want.sma.value;
    const f64 period = kTau * std::sqrt(a * a * a / kMuEarth.value);
    const OrbitInfo info = orbitInfo(*el, kMuEarth);
    INFO(std::format("sma {:.17g} m, want {:.17g}; ecc {:.17g}; energy {:.17g} J/kg, want "
                     "{:.17g}; period {:.17g} s, want {:.17g}",
                     el->sma.value,
                     a,
                     el->ecc.value,
                     info.energy.value,
                     want.energy.value,
                     info.period.value,
                     period));
    INFO("the energy is negative: an ellipse, so e < 1 and the orbit is closed");
    REQUIRE(el->ecc.value < 1.0);
    REQUIRE(info.closed);
    REQUIRE(relativeError(el->sma.value, a) <= kConicBudget.value);
    REQUIRE(relativeError(info.energy.value, want.energy.value) <= kConicBudget.value);
    REQUIRE(relativeError(info.period.value, period) <= kConicBudget.value);
    REQUIRE(relativeError(info.meanMotion.value, kTau / period) <= kConicBudget.value);

    // The velocity is square to the position and slower than circular, so the
    // probe is at apoapsis: the apoapsis is where it is. p / (1 - e) divided by
    // a 1 - e known only to its last bits; a(1 + e) does not.
    INFO(std::format("apoapsis {:.17g} m, want {:.17g}", info.apoapsis.value, want.radius.value));
    REQUIRE(relativeError(info.apoapsis.value, want.radius.value) <= kConicBudget.value);
}

// Closer still to radial, the eccentricity is 1 as a double. A probe falling at
// 100 m/s with a sideways drift of 1 um/s has e = 1 - 1.8e-20, and leaving at
// 20 km/s with the same drift, e = 1 + 4.4e-20: both round to exactly 1.0,
// which is neither an ellipse's eccentricity nor a hyperbola's, and every
// consumer that branches on e < 1 would have guessed. The energy knows which,
// so e is kept on its side of 1: the nearest double below for an ellipse and
// above for a hyperbola.
TEST_CASE("an eccentricity that rounds to 1 keeps the side of 1 its energy says",
          "[orbit][scales]") {
    const StateVector falling{.pos = {7000e3, 0.0, 0.0}, .vel = {-100.0, 1.0e-6, 0.0}};
    const auto ellipse = elementsFromState(falling, kMuEarth);
    INFO("the falling probe -> " << errorName(ellipse));
    REQUIRE(ellipse.has_value());
    INFO(std::format("its eccentricity {:.17g}", ellipse->ecc.value));
    REQUIRE(ellipse->ecc.value < 1.0);
    REQUIRE(orbitInfo(*ellipse, kMuEarth).closed);
    REQUIRE(relativeError(ellipse->sma.value, conicReference(falling, kMuEarth).sma.value) <=
            kConicBudget.value);

    const StateVector leaving{.pos = {7000e3, 0.0, 0.0}, .vel = {20.0e3, 1.0e-6, 0.0}};
    const auto hyperbola = elementsFromState(leaving, kMuEarth);
    INFO("the leaving probe -> " << errorName(hyperbola));
    REQUIRE(hyperbola.has_value());
    INFO(std::format("its eccentricity {:.17g}", hyperbola->ecc.value));
    REQUIRE(hyperbola->ecc.value > 1.0);
    REQUIRE_FALSE(orbitInfo(*hyperbola, kMuEarth).closed);
    REQUIRE(relativeError(hyperbola->sma.value, conicReference(leaving, kMuEarth).sma.value) <=
            kConicBudget.value);
}

// orbitInfo's radius and speed come from the elements, and near the radial
// limit they came out wrong (2026-09-11): p / (1 + e cos nu) divided by a
// 1 + e cos nu that cancels there, and vis-viva subtracted two nearly equal
// terms. The probe falling at 100 m/s from 7000 km was reported 1107 m from
// the centre, moving at 848 km/s; over 30,000 nearly radial states the radius
// came back infinite 417 times and negative 3,960 times, and the speed
// exactly zero 4,138 times.
//
// Each budget is four to ten times what the code now measures against 60-digit
// references, and what it measures is the elements' own floor: most of a nearly
// radial orbit lies in a sliver of true anomaly near pi, which a double
// resolves only so finely. The sweep at the end of this file states that floor
// as a conditioning law; these four are the cases the defect was found by, and
// hold the numbers measured on them.
//
// The drifting probe is the one case that law cannot cover. At an apsis the
// speed's first-order dependence on the true anomaly vanishes and the second
// order is what is left: the double nearest pi is 1.2e-16 short of it, which
// beside a 1 mm/s drift is a radial 7e-6 m/s, or 2.4e-5 of the speed.
TEST_CASE("orbitInfo's radius and speed hold near the radial limit", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        StateVector state;
        GravParam mu;
        Tolerance radius; // budget, with the measured error beside it
        Tolerance speed;
    };
    // std::to_array rather than a braced std::array, which leans on brace
    // elision -- what gcc's -Wmissing-braces reports.
    const std::array cases = std::to_array<Case>({
        Case{
            .name = "drifting at 1 mm/s, at apoapsis",
            .state = {.pos = {7000e3, 0.0, 0.0}, .vel = {0.0, 1.0e-3, 0.0}},
            .mu = kMuEarth,
            .radius = Tolerance{1e-15}, // measured 1.3e-16
            .speed = Tolerance{1e-4},   // measured 2.4e-5, the floor above
        },
        Case{
            .name = "leaving at 20 km/s",
            .state = {.pos = {7000e3, 0.0, 0.0}, .vel = {20.0e3, 1.0e-3, 0.0}},
            .mu = kMuEarth,
            .radius = Tolerance{1e-8}, // measured 1.5e-9
            .speed = Tolerance{1e-9},  // measured 2.2e-10
        },
        Case{
            .name = "falling at 100 m/s",
            .state = {.pos = {7000e3, 0.0, 0.0}, .vel = {-100.0, 1.0e-6, 0.0}},
            .mu = kMuEarth,
            .radius = Tolerance{2e-7}, // measured 3.4e-8
            .speed = Tolerance{1e-3},  // measured 1.9e-4
        },
        Case{
            .name = "the fuzzer's hyperbola at 1e-158 m",
            .state = kFuzzerHyperbola,
            .mu = kFuzzerMu,
            .radius = Tolerance{1e-8}, // measured 1.2e-9
            .speed = Tolerance{1e-9},  // measured 2.5e-10
        },
    });
    for (const Case& c : cases) {
        CAPTURE(c.name);
        const auto el = elementsFromState(c.state, c.mu);
        INFO("elementsFromState -> " << errorName(el));
        REQUIRE(el.has_value());
        const OrbitInfo info = orbitInfo(*el, c.mu);
        const ConicReference want = conicReference(c.state, c.mu);
        INFO(std::format("radius {:.17g} m, want {:.17g}; speed {:.17g} m/s, want {:.17g}",
                         info.radius.value,
                         want.radius.value,
                         info.speed.value,
                         want.speed.value));
        REQUIRE(relativeError(info.radius.value, want.radius.value) <= c.radius.value);
        REQUIRE(relativeError(info.speed.value, want.speed.value) <= c.speed.value);
    }
}

// libFuzzer, 2026-09-12, against the fix above: a hyperbola so energetic that
// -mu/(2E) underflows and `sma` comes back -0. Rebuilding e - 1 from p/a then
// divides by that zero, and the factor, the radius and the speed all came back
// infinite or NaN -- the "succeeded, and the answer is NaN" outcome this file
// keeps finding. It cost two guards, and the second was only visible because
// the first was in place: mu/p is 6.3e-337 here, below the smallest subnormal,
// so sqrt(mu/p) was zero and the speed came back 0 m/s for a trajectory doing
// 7.4e71 m/s. sqrt(mu)/sqrt(p) has the range for it, and the speed is now good
// to 6.8e-8.
//
// The radius used to be the part that could not be saved: alpha r is 8.6e249
// and e is 9.3e239 here, and p / (1 + e cos v) came out -2.3e-157 for a state
// at 1.04e-153 -- the wrong sign and four orders of magnitude out. That was
// read as the elements' own limit rather than a formulation's, and left as a
// task. It was neither. Carrying the conversion in double-double
// (core/DoubleDouble.hpp, 2026-09-12) brings the radius here to 9.3e-9
// relative, from elements that are now the nearest doubles to the exact ones,
// so the assertion below is the claim the earlier comment said was
// unreachable.
//
// 1e-6 rather than something tighter, for both: an ulp of the true anomaly
// moves the radius by about |alpha r| times its own rounding, and the platforms
// do not agree on atan2 to the last bit. The measured 9.3e-9 has two orders of
// magnitude of room inside that.
TEST_CASE("an underflowing semi-major axis is not a NaN radius", "[orbit][scales]") {
    const StateVector state{
        .pos = {6.013470016999446e-154, 6.01347001699909e-154, 6.013470018388293e-154},
        .vel = {-4.252558376478985e+71, -4.252558376500915e+71, -4.252558376500915e+71},
    };
    const GravParam mu{6.554909140857642e-260};

    const auto el = elementsFromState(state, mu);
    INFO("elementsFromState -> " << errorName(el));
    REQUIRE(el.has_value());
    INFO(std::format("sma {:.17g} m, ecc {:.17g}, slr {:.17g} m, tra {:.17g}",
                     el->sma.value,
                     el->ecc.value,
                     el->slr.value,
                     el->tra.value));

    const OrbitInfo info = orbitInfo(*el, mu);
    INFO(std::format("radius {:.17g} m, speed {:.17g} m/s, energy {:.17g} J/kg",
                     info.radius.value,
                     info.speed.value,
                     info.energy.value));
    REQUIRE_FALSE(std::isnan(info.radius.value));
    REQUIRE_FALSE(std::isnan(info.speed.value));
    REQUIRE_FALSE(std::isnan(info.periapsis.value));
    REQUIRE_FALSE(std::isnan(info.apoapsis.value));
    REQUIRE_FALSE(std::isnan(info.period.value));
    REQUIRE_FALSE(std::isnan(info.meanMotion.value));
    REQUIRE_FALSE(std::isnan(info.energy.value));

    const ConicReference want = conicReference(state, mu);
    INFO("the speed still means something");
    REQUIRE(relativeError(info.speed.value, want.speed.value) <= 1e-6);
    INFO("and so does the radius, which is what changed");
    REQUIRE(relativeError(info.radius.value, want.radius.value) <= 1e-6);
}

// Five orbits through the same periapsis, 7000 km up, differing by at most
// 2e-9 in eccentricity: an ellipse and a hyperbola either side of the refusal
// band, one inside it, and the parabola itself. Started at the same true
// anomaly and propagated the same 2782.9 s, they must arrive within a few
// billionths of the same place -- that is what "within 2e-9 of a parabola"
// means.
//
// Until 2026-09-12 `propagateElements` refused two of the five, and was 9.3e-4
// out on the inbound ellipse while being 2.2e-9 out on the same orbit taken
// outbound. The difference was the wrap: with the true anomaly past pi the
// eccentric anomaly comes out just under tau, the mean anomaly is then tau
// minus something tiny, and the tiny part is the whole answer.
//
// The expected values are the Kepler equation of each conic solved in 60-digit
// decimal arithmetic from these same doubles (`scratchpad/pe/reference.py`,
// self-checked against dt = 0, forward-and-back, and a whole period). The
// budget is 1e-12 of the radius, which is 4e-5 m here and about a hundred times
// what the fixed code measures.
TEST_CASE("element propagation holds on both sides of a parabola", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        Metres sma;
        Eccentricity ecc;
        Radians expected; // 60-digit reference, rounded to a double
    };
    // Periapsis 7000 km, so p = 1.4e7 m; the step is three times sqrt(q^3/mu).
    constexpr Metres kSlr{1.4e7};
    constexpr Radians kStart{5.4000000000000004};
    constexpr Seconds kStep{2782.9117013432488};
    const std::array cases = std::to_array<Case>({
        Case{
            .name = "an ellipse 2e-9 inside a parabola",
            .sma = Metres{3499999904697733.5},
            .ecc = Eccentricity{0.99999999799999995},
            .expected = Radians{1.6936408726557204},
        },
        Case{
            .name = "an ellipse 5e-10 inside a parabola, which is refused today",
            .sma = Metres{13999998841634902.0},
            .ecc = Eccentricity{0.99999999949999996},
            .expected = Radians{1.6936408733653279},
        },
        Case{
            .name = "the parabola itself, which is refused today",
            .sma = Metres{kInf},
            .ecc = Eccentricity{1.0},
            .expected = Radians{1.6936408736018638},
        },
        Case{
            .name = "a hyperbola 2e-9 outside a parabola",
            .sma = Metres{-3500000098986763.0},
            .ecc = Eccentricity{1.0000000019999999},
            .expected = Radians{1.6936408745480071},
        },
    });

    for (const Case& c : cases) {
        CAPTURE(c.name);
        const Elements el{
            .sma = c.sma,
            .ecc = c.ecc,
            .inc = Radians{0.0},
            .lan = Radians{0.0},
            .aop = Radians{0.0},
            .tra = kStart,
            .slr = kSlr,
        };
        const auto moved = propagateElements(el, kMuEarth, kStep);
        INFO("propagateElements -> " << errorName(moved));
        REQUIRE(moved.has_value());

        Elements want = el;
        want.tra = c.expected;
        INFO(std::format("true anomaly {:.17g}, want {:.17g}", moved->tra.value, c.expected.value));
        REQUIRE_THAT(stateFromElements(*moved, kMuEarth).pos,
                     WithinRelVec(stateFromElements(want, kMuEarth).pos, Tolerance{1e-12}));
    }
}

// The same orbit as the first case above, taken outbound rather than inbound,
// and stepped a hundred times further. Neither was refused by the formulation
// this replaced, and both were wrong: 2.2e-9 and 6.2e-6 of the radius.
TEST_CASE("element propagation holds outbound and over a long step", "[orbit][scales]") {
    constexpr Metres kSlr{1.4e7};
    const Elements el{
        .sma = Metres{3499999904697733.5},
        .ecc = Eccentricity{0.99999999799999995},
        .inc = Radians{0.0},
        .lan = Radians{0.0},
        .aop = Radians{0.0},
        .tra = Radians{0.90000000000000002},
        .slr = kSlr,
    };
    const auto outbound = propagateElements(el, kMuEarth, Seconds{2782.9117013432488});
    INFO("propagateElements outbound -> " << errorName(outbound));
    REQUIRE(outbound.has_value());
    Elements want = el;
    want.tra = Radians{1.9687811287167955}; // 60-digit reference
    INFO(std::format("outbound {:.17g}, want {:.17g}", outbound->tra.value, want.tra.value));
    REQUIRE_THAT(stateFromElements(*outbound, kMuEarth).pos,
                 WithinRelVec(stateFromElements(want, kMuEarth).pos, Tolerance{1e-12}));

    Elements inbound = el;
    inbound.tra = Radians{5.4000000000000004};
    const auto far = propagateElements(inbound, kMuEarth, Seconds{278291.17013432487});
    INFO("propagateElements over 300 periapsis times -> " << errorName(far));
    REQUIRE(far.has_value());
    want.tra = Radians{2.9067816679180335}; // 60-digit reference
    INFO(std::format("long step {:.17g}, want {:.17g}", far->tra.value, want.tra.value));
    REQUIRE_THAT(stateFromElements(*far, kMuEarth).pos,
                 WithinRelVec(stateFromElements(want, kMuEarth).pos, Tolerance{1e-12}));
}

// A million periapsis times from the same periapsis, on the two conics 2e-9
// either side of the parabola. They part company in the eighth digit of the
// true anomaly by then, which is the point: the semi-major axis has to come
// from `sma`, where it is exact, and not be recovered from the shape at the
// current radius, where 2/r and v^2/mu agree to fifteen digits and their
// difference is the whole of 1/a. Recovering it that way moves these two
// answers by 2.8e-12 of the radius, which is what the budget below is set to
// catch.
TEST_CASE("element propagation keeps the semi-major axis it was given", "[orbit][scales]") {
    struct Case {
        std::string_view name;
        Metres sma;
        Eccentricity ecc;
        Radians expected; // 60-digit reference
    };
    const std::array cases = std::to_array<Case>({
        Case{
            .name = "an ellipse 2e-9 inside a parabola",
            .sma = Metres{3499999904697733.5},
            .ecc = Eccentricity{0.99999999799999995},
            .expected = Radians{3.1260267023022133},
        },
        Case{
            .name = "a hyperbola 2e-9 outside a parabola",
            .sma = Metres{-3500000098986763.0},
            .ecc = Eccentricity{1.0000000019999999},
            .expected = Radians{3.1260264967557301},
        },
    });

    for (const Case& c : cases) {
        CAPTURE(c.name);
        const Elements el{
            .sma = c.sma,
            .ecc = c.ecc,
            .inc = Radians{0.0},
            .lan = Radians{0.0},
            .aop = Radians{0.0},
            .tra = Radians{1.5},
            .slr = Metres{1.4e7},
        };
        const auto moved = propagateElements(el, kMuEarth, Seconds{927637233.78108299});
        INFO("propagateElements -> " << errorName(moved));
        REQUIRE(moved.has_value());

        Elements want = el;
        want.tra = c.expected;
        INFO(std::format("true anomaly {:.17g}, want {:.17g}", moved->tra.value, c.expected.value));
        REQUIRE_THAT(stateFromElements(*moved, kMuEarth).pos,
                     WithinRelVec(stateFromElements(want, kMuEarth).pos, Tolerance{1e-12}));
    }
}

// The hyperbolic half of the case above.
TEST_CASE("a nearly radial hyperbola is open, with its energy", "[orbit][scales]") {
    const StateVector state{.pos = {7000e3, 0.0, 0.0}, .vel = {20.0e3, 1.0e-3, 0.0}};
    const auto el = elementsFromState(state, kMuEarth);
    INFO("elementsFromState -> " << errorName(el));
    REQUIRE(el.has_value());

    const ConicReference want = conicReference(state, kMuEarth);
    const OrbitInfo info = orbitInfo(*el, kMuEarth);
    INFO(std::format("sma {:.17g} m, want {:.17g}; ecc {:.17g}; energy {:.17g} J/kg, want {:.17g}",
                     el->sma.value,
                     want.sma.value,
                     el->ecc.value,
                     info.energy.value,
                     want.energy.value));
    INFO("the energy is positive: a hyperbola, so e > 1 and the orbit is open");
    REQUIRE(el->ecc.value > 1.0);
    REQUIRE_FALSE(info.closed);
    REQUIRE(std::isinf(info.period.value));
    REQUIRE(relativeError(el->sma.value, want.sma.value) <= kConicBudget.value);
    REQUIRE(relativeError(info.energy.value, want.energy.value) <= kConicBudget.value);
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
// and down to e = 1e-16. Over those wider ranges the worst case is still
// between 8.5 and 9.3 u, measured by tightening this factor until it fails,
// so the budget of 40 is about four times it.
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
    const f64 r = length(sv.pos);
    const f64 v = length(sv.vel);
    const f64 rdotv = std::abs(dot(sv.pos, sv.vel));
    const f64 h = length(cross(sv.pos, sv.vel));
    const f64 alphaRadius = std::abs(2.0 - (r * v * v / mu.value));
    const f64 band = std::isinf(el.sma.value) ? 0.5 * alphaRadius : 0.0;
    const f64 scale = kRoundTripFactor * kUnitRoundoff * (1.0 + alphaRadius);
    return {
        .radius = Tolerance{(scale * (1.0 + (rdotv / h))) + band},
        .speed = Tolerance{(scale * (1.0 + (rdotv * mu.value / (r * v * v * h)))) + band},
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
                     info.radius.value,
                     want.radius.value,
                     budget.radius.value,
                     info.speed.value,
                     want.speed.value,
                     budget.speed.value));
    REQUIRE(relativeError(info.radius.value, want.radius.value) <= budget.radius.value);
    REQUIRE(relativeError(info.speed.value, want.speed.value) <= budget.speed.value);
}

// A uniform direction on the sphere: z uniform and the azimuth uniform is the
// one pairing that does not crowd the poles.
[[nodiscard]] Vec3 randomDirection(Sampler& sampler) {
    const f64 z = (2.0 * sampler.fraction()) - 1.0;
    const Radians azimuth = sampler.angle(kTau);
    const f64 ring = std::sqrt(1.0 - (z * z));
    return {ring * std::cos(azimuth.value), ring * std::sin(azimuth.value), z};
}

// A unit vector perpendicular to a unit `dir`, at a random azimuth about it.
// The seed vector is chosen to be well away from `dir`, so the cross product
// is never a ratio of two roundings -- drawing a second random direction here
// could return one parallel to the first.
[[nodiscard]] Vec3 perpendicularTo(const Vec3& dir, Sampler& sampler) {
    const Vec3 seed = (std::abs(dir.x) < 0.9) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return rotateAxis(normalize(cross(dir, seed)), dir, sampler.angle(kTau));
}

// Any shape at any attitude: a tenth of circular speed to twice it spans
// e = 0 to hyperbolic, and the direction is independent of the position.
void sweepOrdinaryOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const f64 radius =
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value});
        const Vec3 dir = randomDirection(sampler);
        const f64 speed = std::sqrt(body.mu.value / radius) * (0.1 + (1.9 * sampler.fraction()));
        CAPTURE(kSweepSeed, i, body.name, radius, speed);
        checkRadiusAndSpeed({.pos = dir * radius, .vel = randomDirection(sampler) * speed},
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
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value});
        const Vec3 dir = randomDirection(sampler);
        const f64 speed = std::sqrt(body.mu.value / radius) * (0.05 + (2.5 * sampler.fraction()));
        const f64 tangential = sampler.logUniform({.lo = 1e-11, .hi = 1e-2});
        const f64 outward = (sampler.fraction() < 0.5) ? 1.0 : -1.0;
        const Vec3 vel = ((dir * (outward * std::sqrt(1.0 - (tangential * tangential)))) +
                          (perpendicularTo(dir, sampler) * tangential)) *
                         speed;
        CAPTURE(kSweepSeed, i, body.name, radius, speed, tangential, outward);
        checkRadiusAndSpeed({.pos = dir * radius, .vel = vel}, body.mu);
    }
}

// Within 1e-15 to 1e-6 of escape speed, either side, at every flight path
// angle: the states whose energy decides the conic by a hair, and the ones the
// parabolic band catches.
void sweepNearParabolicOrbits(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const f64 radius =
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value});
        const Vec3 dir = randomDirection(sampler);
        const f64 offset = sampler.logUniform({.lo = 1e-15, .hi = 1e-6});
        const f64 speed = std::sqrt(2.0 * body.mu.value / radius) *
                          (1.0 + ((sampler.fraction() < 0.5) ? offset : -offset));
        const Radians flightPath{sampler.angle(kPi).value - (kPi / 2.0)};
        const Vec3 vel = ((dir * std::sin(flightPath.value)) +
                          (perpendicularTo(dir, sampler) * std::cos(flightPath.value))) *
                         speed;
        CAPTURE(kSweepSeed, i, body.name, radius, speed, offset, flightPath.value);
        checkRadiusAndSpeed({.pos = dir * radius, .vel = vel}, body.mu);
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
        const Eccentricity ecc{sampler.logUniform({.lo = 1e-16, .hi = 1e-2})};
        const f64 sma = sampler.logUniform(
            {.lo = body.minPeriapsis.value / (1.0 - ecc.value), .hi = body.maxSma.value});
        const Elements el{
            .sma = Metres{sma},
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = sampler.angle(kTau),
            .slr = Metres{sma * (1.0 - (ecc.value * ecc.value))},
        };
        CAPTURE(kSweepSeed, i, body.name, sma, ecc.value, el.tra.value);
        checkRadiusAndSpeed(stateFromElements(el, body.mu), body.mu);
    }
}

// Out along a hyperbola's asymptote, where 1 + e cos nu cancels for the other
// reason: r / |a| from a thousandth to a thousand, e from just above 1 to 100.
// Beyond 1e3 the elements themselves stop meaning much -- see the note on the
// budget above.
void sweepHyperbolicAsymptotes(Sampler& sampler) {
    for (std::size_t i = 0; i < kRoundTripCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Eccentricity ecc{1.0 + sampler.logUniform({.lo = 1e-6, .hi = 99.0})};
        const f64 sma =
            -sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value});
        const f64 slr = -sma * ((ecc.value * ecc.value) - 1.0);
        // Never inside periapsis, whatever r/|a| was drawn.
        const f64 radius =
            std::max(slr / (1.0 + ecc.value), -sma * sampler.logUniform({.lo = 1e-3, .hi = 1e6}));
        const f64 tra = std::acos(std::clamp(((slr / radius) - 1.0) / ecc.value, -1.0, 1.0));
        const Elements el{
            .sma = Metres{sma},
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = Radians{(sampler.fraction() < 0.5) ? tra : kTau - tra},
            .slr = Metres{slr},
        };
        CAPTURE(kSweepSeed, i, body.name, sma, ecc.value, radius, el.tra.value);
        checkRadiusAndSpeed(stateFromElements(el, body.mu), body.mu);
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
    const f64 kappa =
        std::abs(end.ecc.value * std::sin(end.tra.value)) * far.radius.value / end.slr.value;
    const f64 swept = std::abs(dt.value) * home.speed.value / home.radius.value;
    const f64 leverage =
        (home.speed.value * far.radius.value) / (far.speed.value * home.radius.value);
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
                        : slr.value / (1.0 - (eccentricity * eccentricity));
    return {
        .sma = Metres{sma},
        .ecc = Eccentricity{std::isfinite(sma) ? std::sqrt(1.0 - (slr.value / sma)) : 1.0},
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

    const auto viaState = propagate(stateFromElements(el, mu), mu, dt);
    INFO("propagate -> " << errorName(viaState));
    REQUIRE(viaState.has_value());
    INFO(std::format("true anomaly {:.17g} -> {:.17g}", el.tra.value, moved->tra.value));
    REQUIRE_THAT(stateFromElements(*moved, mu).pos, WithinRelVec(viaState->pos, kPropagatorsAgree));

    const auto back = propagateElements(*moved, mu, -dt);
    INFO("propagateElements back -> " << errorName(back));
    REQUIRE(back.has_value());
    const Tolerance budget = reversibilityBudget(el, *moved, mu, dt);
    INFO(std::format("reversibility budget {:.3g}", budget.value));
    REQUIRE_THAT(stateFromElements(*back, mu).pos,
                 WithinRelVec(stateFromElements(el, mu).pos, budget));
}

// A true anomaly the conic actually reaches: a hyperbola only covers the arc
// inside its asymptotes, and the ends of that arc are where p / (1 + e cos nu)
// runs away.
[[nodiscard]] Radians anomalyOn(const Elements& el, Sampler& sampler) {
    if (el.ecc.value <= 1.0) return sampler.angle(kTau);
    const f64 limit = std::acos(-1.0 / el.ecc.value);
    const f64 inside = 1.0 - sampler.logUniform({.lo = 1e-9, .hi = 0.5});
    return Radians{((2.0 * sampler.fraction()) - 1.0) * limit * inside};
}

// Ordinary shapes, as a control: nothing here is near a parabola.
void sweepOrdinaryPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Metres slr{
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value})};
        const bool closed = sampler.fraction() < 0.5;
        const f64 offset =
            closed ? -(0.05 + (sampler.fraction() * 0.9)) : 0.05 + (sampler.fraction() * 4.0);
        Elements el = conicNear(slr, offset, Radians{0.0}, sampler);
        el.tra = anomalyOn(el, sampler);
        const f64 scale = std::abs(el.sma.value);
        const Seconds dt{sampler.logUniform({.lo = 1e-3, .hi = 1e2}) *
                         std::sqrt(scale * scale * scale / body.mu.value) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, slr.value, el.ecc.value, el.tra.value, dt.value);
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
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value})};
        const f64 offset = sampler.logUniform({.lo = 1e-16, .hi = 1e-1}) *
                           ((sampler.fraction() < 0.5) ? 1.0 : -1.0);
        Elements el = conicNear(slr, offset, Radians{0.0}, sampler);
        el.tra = anomalyOn(el, sampler);
        // The time scale of a parabola has no semi-major axis in it: sqrt of
        // the periapsis distance cubed over mu is the one every conic shares.
        const f64 periapsis = 0.5 * slr.value;
        const Seconds dt{sampler.logUniform({.lo = 1e-2, .hi = 1e4}) *
                         std::sqrt(periapsis * periapsis * periapsis / body.mu.value) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, slr.value, offset, el.tra.value, dt.value);
        checkElementPropagation(el, body.mu, dt);
    }
}

// The parabola itself, which had no answer at all before 2026-09-12.
void sweepParabolicPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Metres slr{
            sampler.logUniform({.lo = body.minPeriapsis.value, .hi = body.maxSma.value})};
        Elements el = conicNear(slr, 0.0, Radians{0.0}, sampler);
        el.tra = anomalyOn(el, sampler);
        const f64 periapsis = 0.5 * slr.value;
        const Seconds dt{sampler.logUniform({.lo = 1e-2, .hi = 1e4}) *
                         std::sqrt(periapsis * periapsis * periapsis / body.mu.value) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, slr.value, el.tra.value, dt.value);
        checkElementPropagation(el, body.mu, dt);
    }
}

// Small eccentricities, where the anomaly is measured from a periapsis that is
// barely there: the case that reading the new anomaly off p/r - 1 could not
// answer, because on a circle there is nothing to measure from.
void sweepNearCircularPropagation(Sampler& sampler) {
    for (std::size_t i = 0; i < kPropagationCases; ++i) {
        const Body& body = kBodies.at(i % kBodies.size());
        const Eccentricity ecc{sampler.logUniform({.lo = 1e-13, .hi = 1e-2})};
        const f64 sma = sampler.logUniform(
            {.lo = body.minPeriapsis.value / (1.0 - ecc.value), .hi = body.maxSma.value});
        const Elements el{
            .sma = Metres{sma},
            .ecc = ecc,
            .inc = sampler.angle(kPi),
            .lan = sampler.angle(kTau),
            .aop = sampler.angle(kTau),
            .tra = sampler.angle(kTau),
            .slr = Metres{sma * (1.0 - (ecc.value * ecc.value))},
        };
        const Seconds dt{sampler.logUniform({.lo = 1e-3, .hi = 1e2}) *
                         std::sqrt(sma * sma * sma / body.mu.value) *
                         ((sampler.fraction() < 0.5) ? 1.0 : -1.0)};
        CAPTURE(kSweepSeed, i, body.name, sma, ecc.value, el.tra.value, dt.value);
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
[[nodiscard]] f64 angularError(Radians got, Radians want) {
    const f64 wrapped = std::fmod(std::abs(got.value - want.value), kTau);
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
             {.name = "sma", .got = el->sma.value, .want = test.want.sma.value},
             {.name = "ecc", .got = el->ecc.value, .want = test.want.ecc.value},
             {.name = "slr", .got = el->slr.value, .want = test.want.slr.value},
         })) {
        INFO(std::format("{} {:.17g}, want {:.17g}, out by {:.3g}",
                         m.name,
                         m.got,
                         m.want,
                         relativeError(m.got, m.want)));
        REQUIRE(relativeError(m.got, m.want) <= kElementBudget);
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
                         a.got.value,
                         a.want.value,
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
            .mu = GravParam{1.26686534e17},
            .want =
                {
                    .sma = Metres{984109853.7716752},
                    .ecc = Eccentricity{1.0},
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
            .mu = GravParam{398600441800000.0},
            .want =
                {
                    .sma = Metres{-25327823861.335873},
                    .ecc = Eccentricity{1.0},
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
            .mu = GravParam{398600441800000.0},
            .want =
                {
                    .sma = Metres{6.583308070576134e+23},
                    .ecc = Eccentricity{0.9999999999989005},
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
            .mu = GravParam{1.32712440018e20},
            .want =
                {
                    .sma = Metres{-293005379.96767074},
                    .ecc = Eccentricity{1.0000034505920776},
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
            .mu = GravParam{398600441800000.0},
            .want =
                {
                    .sma = Metres{338062845427.7559},
                    .ecc = Eccentricity{2.410249444739502e-09},
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
            .mu = GravParam{6.762040535602958e-213},
            .want =
                {
                    .sma = Metres{9.408723759066775e-112},
                    .ecc = Eccentricity{0.7793327632486454},
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
            .mu = GravParam{72579463.2331308},
            .want =
                {
                    .sma = Metres{8.272163997668141e+17},
                    .ecc = Eccentricity{0.9999961169100106},
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
        .ecc = Eccentricity{1.0 - 1e-6},
        .inc = Radians{0.4},
        .lan = Radians{0.9},
        .aop = Radians{1.7},
        // pi + 1e-8. Written as the sum because the literal itself would be
        // within 1e-3 of pi, which `modernize-use-std-numbers` reports.
        .tra = Radians{kPi + 9.99999993922529e-09},
        .slr = Metres{13999992.999999998},
    };
    const GravParam mu{398600441800000.0};
    const Seconds dt{2331406655074.406};
    constexpr f64 kBudget = 40.0 * kUnitRoundoff;

    const auto moved = propagateElements(el, mu, dt);
    INFO("propagateElements -> " << errorName(moved));
    REQUIRE(moved.has_value());

    constexpr f64 kExpected = 3.1429299035323592;
    INFO(std::format("tra {:.17g} rad, want {:.17g}, out by {:.3g}, budget {:.3g}",
                     moved->tra.value,
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
[[nodiscard]] f64 exactlyScaledDouble(std::uint64_t& state, int lowest, int highest) {
    const std::uint64_t bits = mixedBits(state);
    const f64 mantissa = 1.0 + (static_cast<f64>(bits >> 12U) * 0x1p-52);
    const int exponent =
        lowest + static_cast<int>((bits >> 1U) % static_cast<std::uint64_t>(highest - lowest));
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
                    exactlyScaledDouble(engine, -60, 60),
                    exactlyScaledDouble(engine, -60, 60),
                    exactlyScaledDouble(engine, -60, 60),
                },
            .vel =
                {
                    exactlyScaledDouble(engine, -40, 40),
                    exactlyScaledDouble(engine, -40, 40),
                    exactlyScaledDouble(engine, -40, 40),
                },
        };
        const GravParam mu{std::abs(exactlyScaledDouble(engine, -20, 60))};
        const auto el = elementsFromState(sv, mu);
        if (!el) continue;
        ++accepted;
        foldBits(hash, el->sma.value);
        foldBits(hash, el->ecc.value);
        foldBits(hash, el->slr.value);
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
