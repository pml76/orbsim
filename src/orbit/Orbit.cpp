#include "orbit/Orbit.hpp"

#include "core/Contract.hpp"
#include "core/DoubleDouble.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <limits>
#include <tuple>
#include <utility>

namespace orb {
namespace {

constexpr f64 kInf = std::numeric_limits<f64>::infinity();

[[nodiscard]] bool isFinite(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] bool isFinite(const StateVector& sv) noexcept {
    return isFinite(sv.pos) && isFinite(sv.vel);
}

// core/DoubleDouble.hpp declares isFinite(f64), and the two overloads above
// would otherwise hide it: unqualified lookup stops at the first scope holding
// the name, and this anonymous namespace is that scope. Naming it here keeps
// the overload set whole, so isFinite reads the same whatever it is given.
using orb::isFinite;

// An orbit is treated as circular / equatorial below these thresholds, at which
// point the periapsis direction / ascending node stops being meaningful.
//
// Both are dimensionless by construction -- an eccentricity and a ratio
// |n|/|h| -- so unlike the tolerances below they carry no hidden length scale.
//
// Where 1e-15 comes from, since a bare number here is exactly what section 12
// warns about. Substituting the argument of latitude for the true anomaly
// while `ecc` keeps its value leaves an element set that is not self
// consistent: a consumer rebuilds the radius from slr / (1 + e cos tra) with a
// tra that is not measured from periapsis, and is wrong by about 2e. So the
// threshold belongs where 2e is no longer measurable, which is the resolution
// of a double, and not where the periapsis direction stops being well
// determined.
//
// It used to be 1e-9, on the second of those arguments: the direction of
// periapsis at e = 1e-9 is set by the ninth digit of the eccentricity vector,
// and plain f64 subtraction left about seven. Both halves of that have since
// been measured wrong. The eccentricity vector is now formed in
// double-double, so it keeps about sixteen; and an ill-conditioned angle costs
// a round trip nothing, because the error in tra is multiplied by e when the
// position is rebuilt. Measured over 4,000 states per decade of eccentricity
// (2026-09-12), the worst state-elements-state error:
//
//   e in [1e-10, 1e-9)   1.97e-9   ->  1.8e-15
//   e in [1e-12, 1e-11)  1.87e-11  ->  1.8e-15
//   e in [1e-15, 1e-14)  2.09e-14  ->  2.1e-15
//   e in [1e-16, 1e-15)  5.46e-15  ->  5.5e-15   (the canonical form fires)
//
// The equatorial threshold has the same shape and does not have the defect,
// which is why it keeps its value: it sets lan to zero and measures the
// in-plane angles from the x-axis, so lan + aop is preserved and the element
// set stays consistent. Measured flat at 2e-15 for inclinations from 1e-17 to
// 1e-5, on the same states.
constexpr f64 kCircularTol = 1e-15;
constexpr f64 kEquatorialTol = 1e-9;

// Below this ratio of |h| to |r||v| -- the sine of the angle between position
// and velocity -- the trajectory is radial and has no orbital plane at all.
// Dimensionless, like the three above. See the use site for why 1e-12 cannot
// clip a real orbit.
constexpr f64 kRectilinearTol = 1e-12;

// Every solve in this file is a safeguarded Newton whose step is required to
// at least halve, so the interval shrinks at least as fast as bisection: about
// 60 steps close any bracket at double precision. 100 is that with room, and
// it makes the bound provable (JPL Power of Ten, rule 2) rather than hoped for.
constexpr int kMaxSolverIterations = 100;

// Every solve runs to the resolution of a double rather than to a hand-picked
// tolerance, and can afford to because bisection bounds the work.
//
// The previous constants were 1e-14 for the anomalies and 1e-13 for the
// universal variable, and the second one taught the lesson worth keeping. A
// plain Newton needs a loose tolerance to be sure of stopping, and then
// routinely blows past it by several orders, because quadratic convergence
// does not stop politely at a threshold. The accuracy this file's tests
// measured was really the accuracy of that overshoot. A safeguarded solver's
// last step is often a bisection, which lands *on* the tolerance rather than
// far beyond it -- which showed up as a 350x accuracy regression at e = 0.9
// until the tolerance became the resolution of the type instead.
constexpr f64 kSolverToleranceUlps = 4.0;

// alpha = 1/a has units of 1/metres, so a threshold on alpha alone encodes a
// length: the old 1e-12 declared every orbit wider than 1e12 m parabolic, and
// Jupiter is 7.8e11 m from the Sun. alpha * r0 is dimensionless -- 1 on a
// circle, 1 - e at periapsis, 1 + e at apoapsis, 0 on a parabola -- and
// separates the conics at every scale.
constexpr f64 kParabolicAlphaTol = 1e-12;

// Which conic a state is on: the one test for it in this file, which
// propagate() and elementsFromState both ask, on the dimensionless energy
// above. Not on the eccentricity. On a nearly radial trajectory e is within a
// hair of 1 whatever the energy, because e^2 - 1 = 2 E h^2 / mu^2 and h is
// small, and a band on |e - 1| once called a probe 7000 km from Earth,
// drifting sideways at 1 mm/s, a parabola -- when it is on a closed ellipse
// with a period of 2061 s (2026-09-11).
enum class Conic : std::uint8_t { Ellipse, Parabola, Hyperbola };

[[nodiscard]] constexpr Conic conicOf(f64 alphaTimesRadius) noexcept {
    if (alphaTimesRadius > kParabolicAlphaTol) return Conic::Ellipse;
    if (alphaTimesRadius < -kParabolicAlphaTol) return Conic::Hyperbola;
    return Conic::Parabola;
}

static_assert(conicOf(1.0) == Conic::Ellipse && conicOf(-1.0) == Conic::Hyperbola &&
                  conicOf(0.0) == Conic::Parabola && conicOf(1e-13) == Conic::Parabola &&
                  conicOf(-1e-13) == Conic::Parabola,
              "the conic is the sign of the energy, outside a band of 1e-12 around zero");

constexpr f64 sign(f64 v) { return v < 0.0 ? -1.0 : 1.0; }

// x - sin x and sinh x - x: both are x^3/6 to first order, and both are written
// as the series where the subtraction would take everything. The series runs to
// x^15/15!, which leaves 2e-20 relative at the switch of 1/2; truncating it at
// x^9/9! instead left 6e-10, and that showed up as a 1.7e-10 position error.
//
// They exist because the Stumpff functions below are exactly these differences,
// and because the elliptic Kepler equation is E - e sin E, which is the same
// cancellation wearing a different hat.
[[nodiscard]] f64 xMinusSin(f64 x) noexcept {
    if (std::abs(x) > 0.5) return x - std::sin(x);
    const f64 x2 = x * x;
    return x * x2 *
           ((1.0 / 6.0) -
            (x2 * ((1.0 / 120.0) -
                   (x2 * ((1.0 / 5040.0) -
                          (x2 * ((1.0 / 362880.0) - (x2 * ((1.0 / 39916800.0) -
                                                           (x2 * ((1.0 / 6227020800.0) -
                                                                  (x2 / 1307674368000.0))))))))))));
}

[[nodiscard]] f64 sinhMinusX(f64 x) noexcept {
    if (std::abs(x) > 0.5) return std::sinh(x) - x;
    const f64 x2 = x * x;
    return x * x2 *
           ((1.0 / 6.0) +
            (x2 * ((1.0 / 120.0) +
                   (x2 * ((1.0 / 5040.0) +
                          (x2 * ((1.0 / 362880.0) + (x2 * ((1.0 / 39916800.0) +
                                                           (x2 * ((1.0 / 6227020800.0) +
                                                                  (x2 / 1307674368000.0))))))))))));
}

// Stumpff functions C(psi) and S(psi), which make the universal-variable
// formulation conic-agnostic: psi > 0 on an ellipse, < 0 on a hyperbola, 0 on a
// parabola, and both functions are analytic across it.
//
// Every term is written so that nothing cancels: 1 - cos s is 2 sin^2(s/2),
// cosh s - 1 is 2 sinh^2(s/2), and the odd differences are the series above.
// The previous version used the closed forms above |psi| = 1e-6 and a
// three-term series below it, and both sides of that switch were wrong where
// they met: at psi = -1.25e-6, (sinh s - s) is a difference of two numbers
// agreeing to seven digits, so c3 came out 4e-7 wrong in relative terms.
// Measured against 60-digit references (2026-09-12), that cost `propagate()` up
// to 1.9e-7 of the radius on nearly parabolic trajectories and 2.5e-7 within
// 1e-9 of a parabola; this version is 5.3e-11 and 7.0e-12 there.
void stumpff(f64 psi, f64& c2, f64& c3) noexcept {
    if (psi > 0.0) {
        const f64 s = std::sqrt(psi);
        const f64 halfSin = std::sin(0.5 * s);
        c2 = 2.0 * halfSin * halfSin / psi;
        c3 = xMinusSin(s) / (psi * s);
    } else if (psi < 0.0) {
        const f64 s = std::sqrt(-psi);
        const f64 halfSinh = std::sinh(0.5 * s);
        c2 = 2.0 * halfSinh * halfSinh / (-psi);
        c3 = sinhMinusX(s) / (s * s * s);
    } else {
        c2 = 0.5;
        c3 = 1.0 / 6.0;
    }
}

// The universal-variable iteration needs a starting guess, and the right guess
// depends on the conic. Extracted so that propagate() reads as a sequence of
// decisions rather than three unrelated formulae inlined mid-function (F.2: a
// function does one thing).
//
// The scalars below stay fixed for a whole solve, so they travel as one value
// rather than as four adjacent f64 parameters that transpose in silence --
// I.24, and `bugprone-easily-swappable-parameters` says so out loud.
struct UniversalContext {
    f64 mu{};     // gravitational parameter, m^3/s^2
    f64 sqrtMu{}; // sqrt(mu), used on nearly every line below
    f64 r0{};     // |r| at the start of the step
    f64 rdotv{};  // r . v at the start of the step
    f64 alpha{};  // 1/a, the reciprocal semi-major axis
    f64 slr{};    // semi-latus rectum, for the parabolic starting guess alone
};

[[nodiscard]] f64 initialUniversalAnomaly(const UniversalContext& ctx, f64 seconds) {
    // A zero-length step has zero universal anomaly on every conic, and saying
    // so before the conic dispatch is a correctness fix rather than a shortcut.
    // The hyperbolic guess below takes the log of a quantity proportional to
    // `seconds`; log(0) is -infinity, which reaches the Stumpff series as NaN
    // and spends the entire iteration budget there before reporting that it did
    // not converge. The elliptic guess is a product and returned zero correctly,
    // and the parabolic one survived by accident through atan(1/0) = pi/2, which
    // is why only hyperbolic trajectories ever failed -- including at dt = 0
    // after a whole revolution was folded out of a closed orbit.
    //
    // An exact-zero test, deliberately: the claim is exactly zero, not
    // "small". A tolerance here would answer a different question and would
    // make a very short step wrong. Spelled with fpclassify rather than
    // `== 0.0`, which -Wfloat-equal reports: the two agree on every input,
    // both zeros and NaN included, and this one says which question it asks.
    if (std::fpclassify(seconds) == FP_ZERO) return 0.0;

    const Conic conic = conicOf(ctx.alpha * ctx.r0);
    if (conic == Conic::Ellipse) return ctx.sqrtMu * seconds * ctx.alpha;

    if (conic == Conic::Hyperbola) {
        const f64 a = 1.0 / ctx.alpha; // negative
        const f64 denom =
            ctx.rdotv + (sign(seconds) * std::sqrt(-ctx.mu * a) * (1.0 - (ctx.r0 * ctx.alpha)));
        return sign(seconds) * std::sqrt(-a) *
               std::log(-2.0 * ctx.mu * ctx.alpha * seconds / denom);
    }

    // Parabola: Barker's equation, solved through the cubic substitution. The
    // semi-latus rectum comes in with the context -- a caller holding elements
    // has it exactly, and one holding a state computes it from |r x v| once.
    const f64 p = ctx.slr;
    const f64 s = 0.5 * std::atan(1.0 / (3.0 * std::sqrt(ctx.mu / (p * p * p)) * seconds));
    const f64 w = std::atan(std::cbrt(std::tan(s)));
    return std::sqrt(p) * 2.0 / std::tan(2.0 * w);
}

// Everything the Lagrange coefficients need from the universal-anomaly solve.
// A struct rather than four out-parameters, and rather than four adjacent f64
// returns that transpose in silence (I.24).
struct UniversalSolution {
    f64 chi{};
    f64 psi{};
    f64 c2{0.5};
    f64 c3{1.0 / 6.0};
};

// The universal Kepler equation and its derivative, both at one chi.
//
// `time` is sqrt(mu) * t(chi): the flight time to chi, scaled. `radius` is
// r(chi), and it is also d(time)/d(chi) -- the derivative of the universal
// Kepler equation with respect to chi *is* the radius. That identity is what
// makes the solve below both fast and provably safe: r > 0 for every real
// trajectory, so `time` is strictly increasing in chi, so the root is unique
// and can always be bracketed.
struct UniversalTerms {
    f64 time{};
    f64 radius{};
    f64 psi{};
    f64 c2{0.5};
    f64 c3{1.0 / 6.0};
};

[[nodiscard]] UniversalTerms evaluateUniversal(f64 chi, const UniversalContext& ctx) {
    UniversalTerms t;
    t.psi = chi * chi * ctx.alpha;
    stumpff(t.psi, t.c2, t.c3);

    const f64 sigma = ctx.rdotv / ctx.sqrtMu; // r . v / sqrt(mu), the usual grouping
    t.time = (chi * chi * chi * t.c3) + (sigma * chi * chi * t.c2) +
             (ctx.r0 * chi * (1.0 - (t.psi * t.c3)));
    t.radius = (chi * chi * t.c2) + (sigma * chi * (1.0 - (t.psi * t.c3))) +
               (ctx.r0 * (1.0 - (t.psi * t.c2)));
    return t;
}

// A search interval, where to start inside it, and the magnitude below which
// the convergence test stops being relative. A struct because four adjacent f64
// parameters transpose in silence (I.24).
//
// `scaleFloor` is 1 for an anomaly, which is O(1) radians, and sqrt(r0) for the
// universal anomaly, which is in sqrt(metres) and has no natural size -- the
// same reasoning that made the old absolute 1e-10 fail at 1 AU.
struct Bracket {
    f64 lo{};
    f64 hi{};
    f64 guess{};
    f64 scaleFloor{1.0};
};

// Safeguarded Newton on a strictly monotonic function, given a bracket that
// contains the root.
//
// `residualAndSlope(x)` returns {f(x), f'(x)} for an increasing f. Newton is
// taken when its step lands inside the bracket -- the usual case, and
// quadratic -- and bisection takes over when it does not, which cannot
// diverge. The pair therefore converges for every input, which plain Newton
// does not: the Kepler equations flatten out near periapsis at high
// eccentricity, the Newton step divides by that slope, and the iteration
// overshoots and oscillates. Measured before this existed: the hyperbolic
// branch failed on 200 of 401 anomalies at e = 1.0001.
//
// Written once and used by both branches of the Kepler solver, which differ
// only in their residual and slope. The universal-variable solver runs the
// same idea by hand, because it must also return the Stumpff terms from the
// final evaluation rather than only the root.
template <std::invocable<f64> Fn>
[[nodiscard]] std::expected<f64, OrbitError> safeguardedRoot(const Bracket& bracket,
                                                             Fn residualAndSlope) {
    f64 lo = bracket.lo;
    f64 hi = bracket.hi;
    f64 x = std::clamp(bracket.guess, lo, hi);

    auto [residual, slope] = residualAndSlope(x);
    if (std::isnan(residual)) return std::unexpected(OrbitError::NotFinite);

    f64 previousStep = hi - lo;
    f64 step = previousStep;

    for (int i = 0; i < kMaxSolverIterations; ++i) {
        // Two reasons to distrust Newton, and the second one matters more than
        // it looks. The obvious one is a step that leaves the bracket. The
        // subtle one is a step that stays inside but barely moves: for the
        // hyperbolic Kepler equation at large H, M ~ e*sinh(H) and
        // M' ~ e*cosh(H) ~ M, so the Newton step is about 1 whatever the
        // distance to the root. Starting from H = 438 that creeps toward the
        // answer one unit at a time and exhausts any iteration budget --
        // measured as 196 failures out of 401 anomalies at e = 1.0001, all of
        // them with the step comfortably inside the bracket, so a bracket test
        // alone never fired.
        //
        // Requiring each step to at least halve the previous one is the
        // classical rtsafe condition, and it is what turns "usually fast" into
        // "never slower than bisection".
        const bool leavesBracket = !std::isfinite(residual / slope) ||
                                   ((x - (residual / slope)) <= lo) ||
                                   ((x - (residual / slope)) >= hi);
        const bool tooSlow = std::abs(2.0 * residual) > std::abs(previousStep * slope);

        previousStep = step;
        if (leavesBracket || tooSlow) {
            step = 0.5 * (hi - lo);
            x = lo + step;
        } else {
            step = residual / slope;
            x -= step;
        }

        const f64 resolution = kSolverToleranceUlps * std::numeric_limits<f64>::epsilon() *
                               std::max(std::abs(x), bracket.scaleFloor);
        if (std::abs(step) <= resolution) return x;

        std::tie(residual, slope) = residualAndSlope(x);
        if (std::isnan(residual)) return std::unexpected(OrbitError::NotFinite);
        if (residual > 0.0) {
            hi = x;
        } else {
            lo = x;
        }
    }

    // Unreachable while the bracket is valid: the halving condition above means
    // the interval shrinks at least as fast as bisection, which closes any
    // bracket in about 60 steps. Kept because a bounded loop that stays silent
    // when it fails has done only half the job.
    return std::unexpected(OrbitError::SolverDidNotConverge);
}

// Solves the universal Kepler equation for chi, by a Newton iteration that
// cannot run away.
//
// Plain Newton was not enough, and the way that surfaced is worth keeping: a
// test passed under Windows clang and failed under gcc-14 and clang-on-Linux,
// same source, different libm. An algorithm whose success depends on which
// library rounded a cosine is not converging -- it is landing on the right
// side of a coin toss. Raising the iteration cap from 200 to 20000 changed
// nothing.
//
// The failure mode is structural rather than accidental. The Newton step is
// `residual / r(chi)`, and near the periapsis of a near-rectilinear orbit r is
// tiny, so the step is enormous and the iteration overshoots and oscillates.
// At e = 0.9999 that is most of the orbit's arc.
//
// The fix uses the property above: time(chi) is *strictly monotonic*, because
// its derivative is a radius and a radius is positive. A strictly monotonic
// function has exactly one root and can always be bracketed, so:
//
//   * bracket the root first, expanding outward from zero until the sign of
//     the residual flips;
//   * then take the Newton step when it lands inside the bracket -- which is
//     almost always, and gives the usual quadratic convergence;
//   * and bisect when it does not, which cannot fail and cannot leave the
//     bracket.
//
// That is the classical safeguarded Newton. Bisection alone would converge in
// about 60 iterations for any double; Newton alone is fast until it isn't.
// Together they are fast *and* guaranteed, and there is no input for which
// this reports non-convergence -- which matters because a closed-form solution
// exists for every valid two-body state, so declining to answer was always a
// weakness of the method rather than a property of the problem.
[[nodiscard]] std::expected<UniversalSolution, OrbitError>
solveUniversalAnomaly(const UniversalContext& ctx, f64 seconds) {
    const f64 target = ctx.sqrtMu * seconds;

    // chi = 0 exactly; see the guard in the guess, and why it is fpclassify.
    if (std::fpclassify(seconds) == FP_ZERO) return UniversalSolution{};

    // time(0) = 0 exactly, and time is increasing, so the root sits on the same
    // side of zero as the target does.
    const f64 direction = target > 0.0 ? 1.0 : -1.0;

    // Start from the conic-specific guess where it is usable, and from a
    // length scale where it is not -- sqrt(r0) has the units of chi.
    f64 far = initialUniversalAnomaly(ctx, seconds);
    if (!std::isfinite(far) || (far * direction) <= 0.0) far = direction * std::sqrt(ctx.r0);

    // Expand outward until the residual changes sign, which brackets the root.
    // Doubling from a length scale reaches any representable chi well inside
    // this bound, so it is a real bound and not a hopeful one (Power of Ten,
    // rule 2).
    constexpr int kMaxBracketExpansions = 200;
    f64 near = 0.0;
    int expansions = 0;
    for (; expansions < kMaxBracketExpansions; ++expansions) {
        const UniversalTerms t = evaluateUniversal(far, ctx);
        if (std::isnan(t.time)) return std::unexpected(OrbitError::NotFinite);
        if ((t.time - target) * direction >= 0.0) break; // sign flipped: bracketed
        near = far;
        far *= 2.0;
        if (!std::isfinite(far)) return std::unexpected(OrbitError::NotFinite);
    }
    if (expansions == kMaxBracketExpansions) {
        return std::unexpected(OrbitError::SolverDidNotConverge);
    }

    // The same safeguarded solve the Kepler equations use. The residual is
    // time(chi) - target and the slope is the radius, which is what the
    // identity at the top of evaluateUniversal buys: one root-finder, one set
    // of guarantees, for all three equations in this file.
    const auto solved = safeguardedRoot(
        {
            .lo = std::min(near, far),
            .hi = std::max(near, far),
            .guess = 0.5 * (near + far),
            .scaleFloor = std::sqrt(ctx.r0),
        },
        [&ctx, target](f64 x) {
            const UniversalTerms t = evaluateUniversal(x, ctx);
            return std::pair{t.time - target, t.radius};
        });
    if (!solved) return std::unexpected(solved.error());

    // The Lagrange coefficients need the Stumpff terms at the root, not just
    // the root, so evaluate once more there rather than carrying them out of
    // the loop -- which is also what keeps chi and its c2/c3 consistent.
    const UniversalTerms fin = evaluateUniversal(*solved, ctx);
    return UniversalSolution{
        .chi = *solved,
        .psi = fin.psi,
        .c2 = fin.c2,
        .c3 = fin.c3,
    };
}

// --- the conversion in double-double ---------------------------------------
//
// Everything from here to elementsFromState carries the cancelling steps in
// core/DoubleDouble.hpp. The reason is section 3 of the guidelines and one
// measurement: on a nearly radial orbit |h| is the difference of two products
// that agree to fifteen digits, so |h|^2/mu kept almost none of them and the
// semi-latus rectum came out 1.1e-5 wrong; on a near-parabolic one v^2/2 and
// mu/r agree to twelve, and the semi-major axis was 7.1e-4 wrong outside the
// band. Both are now at the resolution of a double. The figures per family are
// in the header comment of core/DoubleDouble.hpp.

// A vector as a mantissa vector and one shared power of two, which is exact.
// Products of two such vectors then stay of order one, which matters twice
// over: Dekker's split has a ceiling, and a squared component of a 1e-112 m
// position falls into the subnormals, where it keeps fewer bits the smaller it
// gets. That second one cost a 1e-112 m orbit 98% of its semi-latus rectum.
struct ScaledVec {
    Vec3 unit;      // scaled so the largest component lands in [0.5, 1)
    int exponent{}; // the vector is unit * 2^exponent, exactly
};

[[nodiscard]] ScaledVec factorOutScale(const Vec3& v) noexcept {
    const f64 largest = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    // Zero, an infinity or a NaN: nothing to scale, and every quantity built
    // from it comes out the same whether it is scaled or not.
    if (!(largest > 0.0) || !isFinite(largest)) return {.unit = v, .exponent = 0};
    const int exponent = std::ilogb(largest) + 1;
    // Per component, as core/Math.hpp's length() does, because scalbn(1.0, -k)
    // would itself overflow when the largest component is subnormal.
    return {
        .unit = Vec3{std::scalbn(v.x, -exponent),
                     std::scalbn(v.y, -exponent),
                     std::scalbn(v.z, -exponent)},
        .exponent = exponent,
    };
}

// A vector whose components are double-doubles.
struct Vec3Exact {
    DoubleDouble x, y, z;
};

// The dot and cross products with every product and every sum exact. This is
// where the whole benefit comes from: r x v cancels to nothing when the
// velocity is nearly parallel to the position, and a plain cross product has
// already thrown the answer away by the time anything else sees it.
[[nodiscard]] DoubleDouble dotExact(const Vec3& a, const Vec3& b) noexcept {
    return (twoProduct(a.x, b.x) + twoProduct(a.y, b.y)) + twoProduct(a.z, b.z);
}

[[nodiscard]] Vec3Exact crossExact(const Vec3& a, const Vec3& b) noexcept {
    return {
        .x = twoProduct(a.y, b.z) - twoProduct(a.z, b.y),
        .y = twoProduct(a.z, b.x) - twoProduct(a.x, b.z),
        .z = twoProduct(a.x, b.y) - twoProduct(a.y, b.x),
    };
}

[[nodiscard]] DoubleDouble normSquaredExact(const Vec3Exact& v) noexcept {
    return ((v.x * v.x) + (v.y * v.y)) + (v.z * v.z);
}

[[nodiscard]] Vec3 roundedToDouble(const Vec3Exact& v) noexcept {
    return {toDouble(v.x), toDouble(v.y), toDouble(v.z)};
}

// The same trick again, for a vector that is already in double-double. The
// eccentricity vector needs it for the reason the inputs do: its length is a
// sum of three squares, which overflows once a component passes 1e154, and
// what comes back is then an infinity rather than the large number it should
// be. libFuzzer found the state that does it -- a hyperbola with e = 9.3e239,
// which the whole conversion reported as NotFinite until this was here
// (tests/test_orbit_scales.cpp, "an underflowing semi-major axis").
struct ScaledVec3Exact {
    Vec3Exact unit;
    int exponent{};
};

[[nodiscard]] ScaledVec3Exact factorOutScale(const Vec3Exact& v) noexcept {
    const f64 largest = std::max({std::abs(v.x.hi), std::abs(v.y.hi), std::abs(v.z.hi)});
    if (!(largest > 0.0) || !isFinite(largest)) return {.unit = v, .exponent = 0};
    const int exponent = std::ilogb(largest) + 1;
    return {
        .unit =
            {
                .x = scaleByTwoPower(v.x, -exponent),
                .y = scaleByTwoPower(v.y, -exponent),
                .z = scaleByTwoPower(v.z, -exponent),
            },
        .exponent = exponent,
    };
}

// Every quantity the elements are built from, with the scaling unwound.
//
// The three dimensionless ones are the point. `alphaRadius` is 2 - r v^2 / mu,
// which is alpha * r, and it is the subtraction that decides the conic and
// sizes the semi-major axis. `eCosNu` is p/r - 1, and `eSinNu` is
// (r . v) |h| / (mu r); neither cancels, and the pair fixes the true anomaly
// without ever normalising a vector that has become shorter than its own
// rounding.
struct ExactState {
    DoubleDouble rmag;        // |r|
    DoubleDouble vmag;        // |v|
    DoubleDouble slr;         // |h|^2 / mu
    DoubleDouble alphaRadius; // 2 - r v^2 / mu
    DoubleDouble ecc;         // |evec|
    DoubleDouble eCosNu;
    DoubleDouble eSinNu;
    Vec3 h;        // r x v, on the scaled vectors: a direction, not a magnitude
    Vec3 evec;     // toward periapsis, likewise a direction; `ecc` is its length
    f64 hOverRv{}; // |h| / (|r| |v|), the sine of the angle between r and v
};

[[nodiscard]] ExactState exactStateOf(const StateVector& sv, GravParam mu) noexcept {
    const ScaledVec r = factorOutScale(sv.pos);
    const ScaledVec v = factorOutScale(sv.vel);
    int muExponent = 0;
    const DoubleDouble muMantissa = exact(std::frexp(mu.value, &muExponent));

    const DoubleDouble rmagScaled = sqrtOf(dotExact(r.unit, r.unit));
    const DoubleDouble vSquared = dotExact(v.unit, v.unit);
    const DoubleDouble vmagScaled = sqrtOf(vSquared);
    const DoubleDouble rdotvScaled = dotExact(r.unit, v.unit);
    const Vec3Exact hExact = crossExact(r.unit, v.unit);
    const DoubleDouble hSquared = normSquaredExact(hExact);
    const DoubleDouble hmagScaled = sqrtOf(hSquared);

    // p/r, e cos nu and e sin nu all carry this one power of two, and each is
    // a ratio of order-one quantities until it is applied.
    const int anomalyExponent = r.exponent + (2 * v.exponent) - muExponent;
    const DoubleDouble overMuR = muMantissa * rmagScaled;
    const DoubleDouble pOverR = scaleByTwoPower(hSquared / overMuR, anomalyExponent);
    const DoubleDouble rvSquaredOverMu =
        scaleByTwoPower((rmagScaled * vSquared) / muMantissa, anomalyExponent);

    // The eccentricity vector as rhat (r v^2/mu - 1) - v (r . v) / mu. The
    // same vector as (r (v^2 - mu/r) - v (r . v)) / mu, rearranged so that the
    // only subtraction left is the one that genuinely cancels -- on a circular
    // orbit, where the answer is zero -- and so that both terms are of order
    // one whatever the scales are.
    const DoubleDouble aMinusOne = rvSquaredOverMu - exact(1.0);
    const auto component = [&](f64 rUnit, f64 vUnit) {
        const DoubleDouble radial = (exact(rUnit) / rmagScaled) * aMinusOne;
        const DoubleDouble along =
            scaleByTwoPower((exact(vUnit) * rdotvScaled) / muMantissa, anomalyExponent);
        return radial - along;
    };
    const ScaledVec3Exact evec = factorOutScale({
        .x = component(r.unit.x, v.unit.x),
        .y = component(r.unit.y, v.unit.y),
        .z = component(r.unit.z, v.unit.z),
    });

    return {
        .rmag = scaleByTwoPower(rmagScaled, r.exponent),
        .vmag = scaleByTwoPower(vmagScaled, v.exponent),
        .slr = scaleByTwoPower(hSquared / muMantissa,
                               (2 * r.exponent) + (2 * v.exponent) - muExponent),
        .alphaRadius = exact(2.0) - rvSquaredOverMu,
        .ecc = scaleByTwoPower(sqrtOf(normSquaredExact(evec.unit)), evec.exponent),
        .eCosNu = pOverR - exact(1.0),
        .eSinNu = scaleByTwoPower((rdotvScaled * hmagScaled) / overMuR, anomalyExponent),
        .h = roundedToDouble(hExact),
        .evec = roundedToDouble(evec.unit),
        .hOverRv = toDouble(hmagScaled / (rmagScaled * vmagScaled)),
    };
}

// The vectors and the two anomaly components the in-plane angles are measured
// from. Grouped into a struct rather than passed as adjacent Vec3 parameters,
// which would transpose in silence (I.24, and
// `bugprone-easily-swappable-parameters` would say so).
struct OrbitFrame {
    Vec3 r;    // position, for the argument of latitude of a circular orbit
    Vec3 h;    // specific angular momentum
    Vec3 node; // toward the ascending node; zero for an equatorial orbit
    Vec3 evec; // toward periapsis; zero for a circular orbit
    f64 eCosNu{};
    f64 eSinNu{};
};

// Fills in lan, aop and tra, which is where the degenerate cases live: a
// circular orbit has no periapsis to measure from, and an equatorial one has no
// ascending node. Split out of elementsFromState so each does one thing (F.2)
// and neither exceeds the size limit.
// The magnitudes are derived here rather than passed in: three adjacent f64
// parameters transpose in silence, which is the defect
// `bugprone-easily-swappable-parameters` exists to catch. Recomputing two
// lengths once per conversion is not a cost anything can measure.
void assignInPlaneAngles(Elements& el, const OrbitFrame& frame) {
    const f64 nmag = length(frame.node);
    const f64 hmag = length(frame.h);

    const bool circular = el.ecc.value < kCircularTol;
    const bool equatorial = nmag < kEquatorialTol * hmag;

    // Reference direction for angles measured in the orbital plane: the
    // ascending node where it exists, otherwise the x-axis.
    el.lan = equatorial ? Radians{0.0} : wrapTau(Radians{std::atan2(frame.node.y, frame.node.x)});
    const Vec3 ref = equatorial ? Vec3{1, 0, 0} : frame.node;

    if (circular) {
        // No periapsis to point at, so angles run from the reference direction
        // straight to the spacecraft: argument of latitude, or true longitude.
        el.aop = Radians{0.0};
        f64 u = angleBetween(ref, frame.r).value;
        if (dot(cross(ref, frame.r), frame.h) < 0.0) u = kTau - u; // resolve the half-turn
        el.tra = wrapTau(Radians{u});
        return;
    }

    // One atan2 rather than an unsigned angle plus a sign test. The sine is
    // the component of ref x evec along h and the cosine is ref . evec, so
    // |ref| and |evec| are common factors that atan2 does not care about --
    // which is what makes this work when the eccentricity vector is short. The
    // half-turn falls out of the pair instead of needing a second test.
    const f64 aopSine = dot(cross(ref, frame.evec), frame.h) / hmag;
    const f64 aopCosine = dot(ref, frame.evec);
    el.aop = wrapTau(Radians{std::atan2(aopSine, aopCosine)});

    // The true anomaly from e sin nu and e cos nu, which the conversion has
    // already formed without cancellation: p/r - 1 and (r . v) |h| / (mu r).
    // Neither is a vector that has become shorter than its own rounding, which
    // is what the angle between the eccentricity vector and the position
    // becomes on a nearly circular orbit.
    el.tra = wrapTau(Radians{std::atan2(frame.eSinNu, frame.eCosNu)});
}

// Fills in sma, and keeps e on the side of 1 that matches it. Split out of
// elementsFromState, as assignInPlaneAngles is, so neither exceeds the size
// limit.
//
// The conic is the energy's to decide, by the same test propagate() uses
// (conicOf), and sma says which: finite and positive for an ellipse, negative
// for a hyperbola, infinite for a parabola.
//
// alpha * r is 2 - r v^2 / mu, and the exact state carries it as one
// double-double rather than as the difference of two plain doubles that agree
// to twelve digits near a parabola. That matters twice: it is what the conic
// is decided on, and a = r / (alpha r) is the semi-major axis, which was 7.1e-4
// out just outside the band before this.
//
// And e is kept on the side of 1 the energy says. Near radial it is 1 as a
// double for ellipses and hyperbolas alike -- a probe falling at 100 m/s with
// a 1 um/s drift has e = 1 - 1.8e-20 -- and every consumer that branches on
// e < 1, the anomaly conversions among them, would be guessing. Moving it to
// the adjacent double changes it by less than the eccentricity vector's own
// rounding.
void assignConic(Elements& el, const ExactState& state) {
    const Conic conic = conicOf(toDouble(state.alphaRadius));
    el.sma = Metres{conic == Conic::Parabola ? kInf : toDouble(state.rmag / state.alphaRadius)};
    if (conic == Conic::Ellipse && !(el.ecc.value < 1.0)) {
        el.ecc = Eccentricity{std::nextafter(1.0, 0.0)};
    }
    if (conic == Conic::Hyperbola && !(el.ecc.value > 1.0)) {
        el.ecc = Eccentricity{std::nextafter(1.0, 2.0)};
    }
}

// The postcondition Orbit.hpp promises: "degenerate orbits get a canonical
// parameterisation rather than NaN". Two specific ways of breaking it are
// refused by name upstream -- a magnitude that overflows, and a radial
// trajectory -- but neither covers the general case, which a fuzzer found:
// with mu tiny relative to the state, the division by `m` in the eccentricity
// vector overflows and `ecc` comes back infinite with the in-plane angles NaN.
//
// Guarding the inputs instead would mean inventing a smallest believable mu,
// and this codebase is deliberately scale-free -- the whole lesson of
// test_orbit_scales.cpp is that a threshold carrying a hidden scale is a bug
// waiting for a bigger orbit. Checking the answer costs seven comparisons and
// carries no scale at all.
//
// `sma` is allowed to be infinite, and only there: a parabolic orbit has no
// finite semi-major axis, which is why `slr` is stored beside it.
[[nodiscard]] bool elementsAreUsable(const Elements& el) noexcept {
    if (std::isnan(el.sma.value)) return false;
    return std::isfinite(el.ecc.value) && std::isfinite(el.slr.value) &&
           std::isfinite(el.inc.value) && std::isfinite(el.lan.value) &&
           std::isfinite(el.aop.value) && std::isfinite(el.tra.value);
}

// 1 + e cos v, which the radius and the speed both hang on: r = p / this, and
// v = sqrt(mu/p) hypot(e sin v, this).
//
// Straight, while e cos v >= -1/2. Below that the sum cancels, and the rounding
// of e cos v -- an ulp of a quantity near 1 -- comes out amplified by
// (1 - q)/q > 1. There it is rebuilt from two terms that keep their own
// relative precision: 2 cos^2(v/2) is 1 + cos v computed accurately at v = pi,
// and e - 1 is taken from p and a rather than from e, which is 1 to the last
// bit on either side of the radial limit. e^2 - 1 = -p/a, so
// e - 1 = -(p/a)/(1 + e); on a parabola it is 0 exactly, which is what an
// infinite sma means.
//
// Measured against 60-digit references over 110,004 states on three toolchains
// (2026-09-12), worst relative error of the radius and the speed, against
// 1 + e cos v alone:
//
//   30,000 nearly radial   inf / 5.4e5        ->  6.4e-5 / 4.1e-3
//   20,000 ordinary        3.3e-11 / 1.9e-10  ->  4.7e-13 / 2.7e-12
//   20,000 near-parabolic  1.9e-7 / 9.5e-8    ->  9.2e-12 / 4.6e-12
//   20,000 at e <= 1e-2    9.6e-16 / 1.2e-15  ->  9.6e-16 / 8.6e-16
//    6,614 hyperbolic      1.1e-4 / 1.1e-7    ->  5.3e-8 / 5.3e-11
//
// The first line is not a typo: of 30,000 nearly radial states, 1 + e cos v
// came out zero for 417 and negative for 3,960. What is left is the elements'
// own floor -- tests/test_orbit_scales.cpp states it as a conditioning law.
// The switch point is not delicate: -1/4 and -3/4 were measured too, and move
// the worst case by less than a quarter.
// e - 1, from p and a rather than from e: e^2 - 1 = -p/a, so
// e - 1 = -(p/a)/(1 + e), and near the radial limit that keeps every digit
// where the stored e has only its last one. Zero on a parabola, which is what
// an infinite sma means.
//
// Not finite when p/a overflows -- a hyperbola with so much energy that
// -mu/(2E) underflows to -0, which libFuzzer found on 2026-09-12. Callers fall
// back to the stored e there; with e = 9.3e239 nothing cancels anyway.
[[nodiscard]] f64 eccentricityMinusOne(const Elements& el) noexcept {
    if (!std::isfinite(el.sma.value)) return 0.0;
    return -(el.slr.value / el.sma.value) / (1.0 + el.ecc.value);
}

[[nodiscard]] f64 onePlusECosTrueAnomaly(const Elements& el) noexcept {
    const f64 e = el.ecc.value;
    const f64 trueAnomaly = el.tra.value;
    const f64 eCosNu = e * std::cos(trueAnomaly);
    if (eCosNu >= -0.5) return 1.0 + eCosNu;

    const f64 eMinusOne = eccentricityMinusOne(el);
    if (!std::isfinite(eMinusOne)) return 1.0 + eCosNu;

    const f64 halfCos = std::cos(0.5 * trueAnomaly);
    return (2.0 * halfCos * halfCos) + (eMinusOne * std::cos(trueAnomaly));
}

// e + cos v, written as (e - 1) + (1 + cos v) for the reason above: at v = pi
// on a nearly parabolic orbit the two terms of the sum are each tiny and the
// straight form is the difference of two numbers near 1. It is the perifocal
// velocity's second component, over sqrt(mu/p).
//
// A test pins this one, since 2026-09-13: "the perifocal velocity keeps e - 1
// where ecc cannot". It did not until then, and the reason is worth keeping,
// because the first attempt measured the wrong thing. Written straight, this
// moves the propagated *position* by at most 9.7e-13, which is under any budget
// the suite can justify -- but these are orbits at apoapsis, where the radius
// is stationary in the anomaly, so a large error in the anomaly barely moves
// the position. The anomaly is what propagateElements returns, and in the
// anomaly the straight form is 1.05e-13 rad out at 1 - e = 1e-6 and 6.6e-9 at
// 1 - e = 2e-16, against 2.6e-17 and 1.1e-16 for this form. The test's budget
// of 40 u separates them by a factor of twenty-four.
[[nodiscard]] f64 eccentricityPlusCosTrueAnomaly(const Elements& el) noexcept {
    const f64 eMinusOne = eccentricityMinusOne(el);
    if (!std::isfinite(eMinusOne)) return el.ecc.value + std::cos(el.tra.value);
    const f64 halfCos = std::cos(0.5 * el.tra.value);
    return eMinusOne + (2.0 * halfCos * halfCos);
}

} // namespace

// --- state <-> elements ----------------------------------------------------

std::expected<Elements, OrbitError> elementsFromState(const StateVector& sv, GravParam mu) {
    if (!isFinite(sv) || !std::isfinite(mu.value)) return std::unexpected(OrbitError::NotFinite);
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    // Everything the elements are built from, computed once, with every
    // cancelling step carried in double-double. See exactStateOf above.
    const ExactState state = exactStateOf(sv, mu);
    const f64 rmag = toDouble(state.rmag);

    // Reported rather than asserted: a scenario file can put a vessel at the
    // exact centre of a planet, and that is something to explain to the user
    // rather than a bug in this file.
    if (!(rmag > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    // Finite components do not imply a finite magnitude. A vector whose length
    // exceeds the largest double has an infinite length, and `rmag > 0.0` is
    // true of infinity, so the test above waves it through. A fuzzer found
    // this when length() still squared its components and overflowed above
    // about 1.3e154: the elements came back claiming success with an infinite
    // eccentricity and a NaN argument of periapsis, which is precisely the
    // "succeeded, and the answer is NaN" outcome the type system cannot
    // prevent and the caller has no way to detect. The magnitudes are scaled
    // now and so are exact at every scale, but one beyond 1.8e308 is still
    // infinite; the postcondition at the end catches what overflows past here.
    if (!isFinite(rmag) || !isFinite(toDouble(state.vmag))) {
        return std::unexpected(OrbitError::NotFinite);
    }

    // A radial trajectory has no orbital plane, and `inc` below would be
    // atan2(0, 0) = 0 returned as a success. The test is dimensionless on
    // purpose (section 12, and the lesson of this file): |h| / (|r| |v|) is
    // |sin| of the angle between position and velocity, so the threshold
    // compares like with like at every scale. It is formed as a ratio of
    // scaled quantities rather than as hmag > tol * rmag * vmag, so that the
    // product on the right cannot overflow at the top of the range.
    //
    // 1e-12 does not clip real orbits. That ratio is the cosine of the flight
    // path angle, and reaching 1e-12 needs an eccentricity within about 1e-24
    // of 1 -- indistinguishable from a straight line in f64. The e = 0.9999
    // case in the suite sits around 1e-2.
    if (!(state.hOverRv > kRectilinearTol)) {
        return std::unexpected(OrbitError::RectilinearOrbit);
    }

    Elements el;
    el.ecc = Eccentricity{toDouble(state.ecc)};
    el.slr = Metres{toDouble(state.slr)};

    // Every conic has a positive semi-latus rectum, so a zero here is not an
    // orbit shape -- it is |h|^2 underflowing, which the ratio test above
    // cannot see because it never squares anything. The consequence downstream
    // is that orbitInfo's radius becomes slr / (1 + e cos v) = 0/0 at the
    // asymptote of the parabola this then looks like. Same conclusion as the
    // ratio test, reached by a different route, so it is reported as the same
    // thing: too little angular momentum to be an orbit.
    if (!(el.slr.value > 0.0)) return std::unexpected(OrbitError::RectilinearOrbit);

    // atan2 against the in-plane magnitude rather than acos(h.z / |h|). acos
    // loses half its digits where its argument approaches +-1, which is an
    // orbit approaching the equator from either side: the error goes as the
    // square root of the rounding, so 1.6e-13 at an inclination of pi - 1e-4
    // became 3.3e-16 when this changed.
    el.inc = Radians{std::atan2(std::hypot(state.h.x, state.h.y), state.h.z)};

    const Vec3 node = cross(Vec3{0, 0, 1}, state.h); // points at the ascending node

    assignConic(el, state);
    assignInPlaneAngles(el,
                        {
                            .r = sv.pos,
                            .h = state.h,
                            .node = node,
                            .evec = state.evec,
                            .eCosNu = toDouble(state.eCosNu),
                            .eSinNu = toDouble(state.eSinNu),
                        });

    if (!elementsAreUsable(el)) return std::unexpected(OrbitError::NotFinite);
    return el;
}

std::expected<StateVector, OrbitError> stateFromElements(const Elements& el, GravParam mu) {
    // Asserted, not reported: every caller of this obtains its elements from
    // elementsFromState or builds them literally, so a non-positive mu here is
    // a bug in the caller rather than user input.
    ORBSIM_EXPECTS(mu.value > 0.0);

    // Prefer the stored semi-latus rectum: it is the one shape parameter that
    // stays finite on a parabolic orbit.
    const f64 e = el.ecc.value;
    const f64 p = (el.slr.value > 0.0) ? el.slr.value : el.sma.value * (1.0 - (e * e));
    ORBSIM_EXPECTS(p > 0.0);

    // Both of these sums cancel, and both have a cancellation-free form above
    // that this used to ignore while orbitInfo and propagateElements both used
    // it. What that cost, measured on element sets elementsFromState itself
    // produced (2026-09-13): a NaN position for 169 of 10,000 nearly radial
    // ones, where 1 + e cos v came out exactly zero from an `ecc` that stores 1
    // on both sides of the radial limit; and, worse because nothing downstream
    // can see it, a radius of 1.26101e23 m where the elements describe 2.8e23 --
    // `ecc` and the pair (p, a) disagreeing about e - 1 by a factor of 2.2.
    const f64 factor = onePlusECosTrueAnomaly(el);

    // The conic reaches this anomaly only where 1 + e cos v is positive. Past a
    // hyperbola's asymptote, at acos(-1/e), it is negative and so is the radius,
    // and what came back was the position mirrored through the focus.
    // elementsFromState never produces such an element set; a scenario file can
    // write one down, which is why this reports rather than asserting (rule 3).
    if (!(factor > 0.0)) return std::unexpected(OrbitError::UnreachableAnomaly);

    const f64 cosNu = std::cos(el.tra.value);
    const f64 sinNu = std::sin(el.tra.value);
    const f64 rmag = p / factor;
    const f64 k = std::sqrt(mu.value / p);

    // Perifocal frame: x toward periapsis, z along angular momentum.
    const Vec3 rPerifocal{rmag * cosNu, rmag * sinNu, 0.0};
    const Vec3 vPerifocal{-k * sinNu, k * eccentricityPlusCosTrueAnomaly(el), 0.0};

    // Perifocal -> inertial: Rz(lan) * Rx(inc) * Rz(aop), applied right to left.
    const Quat rot = Quat::fromAxisAngle({0, 0, 1}, el.lan) *
                     Quat::fromAxisAngle({1, 0, 0}, el.inc) *
                     Quat::fromAxisAngle({0, 0, 1}, el.aop);

    const StateVector out{.pos = rot.rotate(rPerifocal), .vel = rot.rotate(vPerifocal)};

    // The same postcondition elementsFromState carries, and for the same
    // reason: a finite element set can still describe a point beyond what a
    // double holds, and "succeeded, and the answer is NaN" is the outcome this
    // file keeps finding. It also catches a non-positive p in a release build,
    // where the assertion above is compiled out.
    if (!isFinite(out)) return std::unexpected(OrbitError::NotFinite);
    return out;
}

OrbitInfo orbitInfo(const Elements& el, GravParam mu) {
    ORBSIM_EXPECTS(mu.value > 0.0);

    const f64 e = el.ecc.value;
    const f64 m = mu.value;

    OrbitInfo info;
    // Closed is the energy's to say, through sma as elementsFromState set it,
    // and not e < 1, which near radial is 1 whatever the energy.
    info.closed = std::isfinite(el.sma.value) && el.sma.value > 0.0;

    info.periapsis = Metres{el.slr.value / (1.0 + e)};
    // a(1 + e), not p / (1 - e): near e = 1 the second divides by a 1 - e
    // known only to its last bits. Measured against 60-digit references
    // (2026-09-11, re-measured exactly on 2026-09-12), p / (1 - e) was up to
    // 506% out on 27,242 nearly radial ellipses and 3.6e-10 on 138,754
    // ordinary ones; this is within 3.3e-11 and 3.0e-12.
    info.apoapsis = Metres{info.closed ? el.sma.value * (1.0 + e) : kInf};

    // Both of these come from the shape and the anomaly, and neither from
    // vis-viva: v^2 = (mu/p)((e sin v)^2 + (1 + e cos v)^2) is the same
    // quantity as mu(2/r - 1/a) without the subtraction, which near the radial
    // limit was taking the difference of two terms equal to their last bits.
    // It reported a probe falling at 100 m/s as moving at 848 km/s.
    const f64 factor = onePlusECosTrueAnomaly(el);
    info.radius = Metres{el.slr.value / factor};
    // sqrt(mu)/sqrt(p) rather than sqrt(mu/p), and multiplied into each term
    // rather than applied to their hypot. The same number at any ordinary
    // scale, and each form avoids something the other walks into: mu/p
    // underflowed to zero for the state libFuzzer found on 2026-09-12 (6.3e-337,
    // below the smallest subnormal) and reported 0 m/s for a trajectory doing
    // 7.4e71 m/s, and the hypot of the unscaled terms overflows where each
    // scaled term is finite.
    const f64 shape = std::sqrt(m) / std::sqrt(el.slr.value);
    info.speed = MetresPerSecond{std::hypot(shape * e * std::sin(el.tra.value), shape * factor)};

    if (info.closed) {
        const f64 a = el.sma.value;
        info.meanMotion = RadiansPerSecond{std::sqrt(m / (a * a * a))};
        info.period = Seconds{kTau / info.meanMotion.value};
        info.energy = SpecificEnergy{-m / (2.0 * a)};
    } else {
        info.meanMotion = RadiansPerSecond{0.0};
        info.period = Seconds{kInf};
        info.energy = SpecificEnergy{std::isinf(el.sma.value) ? 0.0 : -m / (2.0 * el.sma.value)};
    }
    return info;
}

// --- anomaly conversions ---------------------------------------------------

Radians trueToEccentricAnomaly(Radians trueAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    if (e < 1.0) {
        return Radians{2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(trueAnomaly.value * 0.5),
                                        std::sqrt(1.0 + e) * std::cos(trueAnomaly.value * 0.5))};
    }
    // Hyperbolic: build sinh(H) directly instead of going through tan(nu/2),
    // which is unbounded as the true anomaly approaches the asymptote.
    const f64 nu = wrapPi(trueAnomaly).value;
    const f64 sinhH = std::sqrt((e * e) - 1.0) * std::sin(nu) / (1.0 + (e * std::cos(nu)));
    return Radians{std::asinh(sinhH)};
}

Radians eccentricToTrueAnomaly(Radians eccAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    if (e < 1.0) {
        return wrapTau(
            Radians{2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(eccAnomaly.value * 0.5),
                                     std::sqrt(1.0 - e) * std::cos(eccAnomaly.value * 0.5))});
    }
    return Radians{2.0 * std::atan2(std::sqrt(e + 1.0) * std::sinh(eccAnomaly.value * 0.5),
                                    std::sqrt(e - 1.0) * std::cosh(eccAnomaly.value * 0.5))};
}

Radians eccentricToMeanAnomaly(Radians eccAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    if (e < 1.0) return Radians{eccAnomaly.value - (e * std::sin(eccAnomaly.value))};
    return Radians{(e * std::sinh(eccAnomaly.value)) - eccAnomaly.value};
}

std::expected<Radians, OrbitError> meanToEccentricAnomaly(Radians meanAnomaly, Eccentricity ecc) {
    const f64 e = ecc.value;
    ORBSIM_EXPECTS(e >= 0.0);

    if (e < 1.0) {
        // Solving M = E - e*sin(E) for E. dM/dE = 1 - e*cos(E) >= 1 - e > 0,
        // so M is strictly increasing and the root is unique.
        //
        // The bracket costs nothing: with M wrapped into (-pi, pi], E lies in
        // the same interval, because M(-pi) = -pi and M(pi) = pi for every
        // eccentricity. So the safeguarded solve starts already bracketed.
        const f64 mean = wrapPi(meanAnomaly).value;

        // A near-parabolic orbit spends nearly all of its mean anomaly close to
        // periapsis, so the mean anomaly is a poor starting guess there; pi
        // keeps Newton inside the convergent basin. It is now only a hint --
        // bisection covers the cases where it is wrong, which measurement said
        // included four of 2001 anomalies at e = 0.9999.
        const f64 guess = (e < 0.8) ? mean : sign(mean) * kPi;

        const auto solved =
            safeguardedRoot({.lo = -kPi, .hi = kPi, .guess = guess}, [e, mean](f64 x) {
                return std::pair{x - (e * std::sin(x)) - mean, 1.0 - (e * std::cos(x))};
            });
        if (!solved) return std::unexpected(solved.error());
        return Radians{*solved};
    }

    // Hyperbolic: M = e*sinh(H) - H, with dM/dH = e*cosh(H) - 1 >= e - 1 > 0.
    // Strictly increasing again, and unbounded, so the bracket has to be found
    // rather than assumed.
    const f64 mean = meanAnomaly.value;
    // M(0) = 0 exactly, on every conic. An exact-zero test, as in
    // initialUniversalAnomaly.
    if (std::fpclassify(mean) == FP_ZERO) return Radians{0.0};

    const f64 direction = mean > 0.0 ? 1.0 : -1.0;

    // The asymptotic inverse of M = e*sinh(H) - H far from periapsis, and a
    // linearisation near it. Both are hints; the expansion below is what makes
    // the answer safe. This guess alone failed on half the anomalies at
    // e = 1.0001, because `mean / (e - 1)` explodes as e approaches 1.
    f64 far = (std::abs(mean) > 6.0) ? direction * std::log((2.0 * std::abs(mean) / e) + 1.8)
                                     : mean / (e - 1.0);
    if (!std::isfinite(far) || (far * direction) <= 0.0) far = direction;

    constexpr int kMaxBracketExpansions = 200;
    f64 near = 0.0;
    int expansions = 0;
    for (; expansions < kMaxBracketExpansions; ++expansions) {
        const f64 residual = (e * std::sinh(far)) - far - mean;
        if (std::isnan(residual)) return std::unexpected(OrbitError::NotFinite);
        if ((residual * direction) >= 0.0) break; // sign flipped: bracketed
        near = far;
        far *= 2.0;
        if (!std::isfinite(far)) return std::unexpected(OrbitError::NotFinite);
    }
    if (expansions == kMaxBracketExpansions) {
        return std::unexpected(OrbitError::SolverDidNotConverge);
    }

    const auto solved = safeguardedRoot(
        {.lo = std::min(near, far), .hi = std::max(near, far), .guess = 0.5 * (near + far)},
        [e, mean](f64 x) {
            return std::pair{(e * std::sinh(x)) - x - mean, (e * std::cosh(x)) - 1.0};
        });
    if (!solved) return std::unexpected(solved.error());
    return Radians{*solved};
}

// --- propagation -----------------------------------------------------------

namespace {

// The state at the end of the step, as a linear combination of the state at the
// start: the Lagrange coefficients. Split out of propagate() so that it does
// one thing and stays inside the size limit (F.2).
//
// g is t - chi^3 c3 / sqrt(mu), written here as the rest of the universal
// Kepler equation, which is the same number: sqrt(mu) t *is* the sum of the
// three terms, so subtracting one of them leaves the other two. Far out on a
// nearly parabolic trajectory those two sides agree to seven digits and their
// difference is the whole coefficient -- measured at up to 1.9e-7 of the radius
// (2026-09-12).
[[nodiscard]] std::expected<StateVector, OrbitError>
lagrangeStep(const StateVector& sv, const UniversalContext& ctx, const UniversalSolution& solved) {
    const f64 chi = solved.chi;
    const f64 psi = solved.psi;
    const f64 c2 = solved.c2;
    const f64 c3 = solved.c3;
    const f64 sigma = ctx.rdotv / ctx.sqrtMu;

    const f64 f = 1.0 - ((chi * chi / ctx.r0) * c2);
    const f64 g = ((sigma * chi * chi * c2) + (ctx.r0 * chi * (1.0 - (psi * c3)))) / ctx.sqrtMu;
    const Vec3 rNew = (sv.pos * f) + (sv.vel * g);
    const f64 rMag = length(rNew);

    // This was ORBSIM_ENSURES(rMag > 0.0), and that was the wrong half of the
    // ADR 0002 split. A caller can reach it: with an enormous speed and a
    // gravitational parameter to match, the Lagrange combination overflows or
    // cancels onto the origin, and a Debug build then aborted the process on
    // user input. A fuzzer found it. The rule is that a condition a caller can
    // produce is reported, and only what a bug in this file could produce is
    // asserted -- so this is reported, by name, for each way it can fail.
    if (!std::isfinite(rMag)) return std::unexpected(OrbitError::NotFinite);
    if (!(rMag > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 gdot = 1.0 - ((chi * chi / rMag) * c2);
    const f64 fdot = (ctx.sqrtMu / (rMag * ctx.r0)) * chi * ((psi * c3) - 1.0);
    const StateVector out{.pos = rNew, .vel = (sv.pos * fdot) + (sv.vel * gdot)};

    // The same postcondition elementsFromState carries: a success must be a
    // usable number. The velocity can still overflow after the position has
    // survived, because fdot divides by rMag.
    if (!isFinite(out)) return std::unexpected(OrbitError::NotFinite);
    return out;
}

} // namespace

std::expected<StateVector, OrbitError> propagate(const StateVector& sv, GravParam mu, Seconds dt) {
    // Checked first and by name. NaN passes every comparison below unnoticed
    // and then comes out of Newton as "did not converge", which is true but
    // sends whoever reads the log looking at the solver instead of the file.
    if (!isFinite(sv) || !std::isfinite(mu.value) || !std::isfinite(dt.value)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    const f64 r0 = length(sv.pos);
    // This used to silently return the input, which turned a loud, findable
    // error into a spacecraft that mysteriously stops moving three hours into a
    // flight. A zero radius is a bug in whatever produced the state -- but the
    // caller is the one holding the context to say so.
    if (!(r0 > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 m = mu.value;
    const f64 v0 = length(sv.vel);

    // Same as in elementsFromState: a magnitude beyond the largest double is
    // infinite, and infinity passes the `r0 > 0.0` test above. Without this the
    // Lagrange coefficients below are computed from infinities and the
    // propagated state is NaN.
    if (!std::isfinite(r0) || !std::isfinite(v0)) {
        return std::unexpected(OrbitError::NotFinite);
    }

    const f64 rdotv = dot(sv.pos, sv.vel);
    const f64 sqrtMu = std::sqrt(m);
    const f64 alpha = (2.0 / r0) - (v0 * v0 / m); // reciprocal of the semi-major axis

    f64 seconds = dt.value;

    // Whole revolutions of a closed orbit are a no-op. Folding them away keeps
    // the universal anomaly small, which is what keeps Newton convergent when
    // the sim runs at 100000x and a single dt spans months.
    if (conicOf(alpha * r0) == Conic::Ellipse) {
        const f64 period = kTau / (sqrtMu * alpha * std::sqrt(alpha));
        seconds = std::fmod(seconds, period);
    }

    // The semi-latus rectum, which only the parabolic starting guess uses.
    const f64 hmag = length(cross(sv.pos, sv.vel));
    const UniversalContext ctx{
        .mu = m,
        .sqrtMu = sqrtMu,
        .r0 = r0,
        .rdotv = rdotv,
        .alpha = alpha,
        .slr = hmag * hmag / m,
    };
    const auto solved = solveUniversalAnomaly(ctx, seconds);
    if (!solved) return std::unexpected(solved.error());
    return lagrangeStep(sv, ctx, *solved);
}

// The same universal-variable solve the state propagator runs, driven from the
// elements instead of from a state vector. That is the whole change of
// 2026-09-12, and it is worth stating why.
//
// The classical route -- true anomaly to eccentric to mean, add n dt, and back
// -- has two problems near e = 1, and only one of them was the famous one. The
// famous one is that the Kepler equation is ill-conditioned there. The other,
// which the measurement found and which dominated, is a wrap: with the true
// anomaly past pi the eccentric anomaly comes out just under tau, the mean
// anomaly is then tau minus something tiny, and the tiny part is the answer.
// The same orbit taken outbound was 2.2e-9 out and inbound 9.3e-4 out.
//
// The universal formulation has no anomaly to wrap and no conic to branch on:
// psi carries the shape, the Stumpff functions are analytic through zero, and
// alpha = 1/a comes from the elements exactly rather than from 2/r - v^2/mu,
// which cancels to nothing when the orbit is nearly parabolic. The new true
// anomaly is then read off the perifocal position the solve lands on, for the
// reason given where that happens.
//
// Measured against 60-digit references over 40,024 element sets (2026-09-12),
// worst relative position error, against the classical route it replaces:
//
//   20,000 nearly parabolic   1.5e-2   ->  3.2e-12
//    8,000 within 1e-9 of e=1 refused  ->  7.0e-12
//    2,000 exactly parabolic  refused  ->  1.0e-13
//   10,000 ordinary           5.9e-13  ->  2.1e-12
//
// which is why there is no longer anything to refuse: the refusal band, and the
// `ParabolicElements` error with it, are gone.
std::expected<Elements, OrbitError>
propagateElements(const Elements& el, GravParam mu, Seconds dt) {
    if (!std::isfinite(mu.value) || !std::isfinite(dt.value)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);
    // The shape has to be usable: a positive, finite semi-latus rectum -- the
    // one parameter every conic has -- and a semi-major axis that is not zero
    // and not NaN. An infinite one is a parabola and is welcome.
    if (!std::isfinite(el.slr.value) || !(el.slr.value > 0.0) || !(std::abs(el.sma.value) > 0.0) ||
        !elementsAreUsable(el)) {
        return std::unexpected(OrbitError::NotFinite);
    }

    const f64 m = mu.value;
    const f64 sqrtMu = std::sqrt(m);
    const f64 p = el.slr.value;
    const f64 e = el.ecc.value;
    // 1/a: zero on a parabola, which is exactly what an infinite sma means.
    const f64 alpha = std::isfinite(el.sma.value) ? 1.0 / el.sma.value : 0.0;
    const f64 r0 = p / onePlusECosTrueAnomaly(el);
    // r . v = r sqrt(mu/p) e sin nu, with sqrt(mu)/sqrt(p) rather than
    // sqrt(mu/p) for the range, as orbitInfo does.
    const f64 rdotv = r0 * e * std::sin(el.tra.value) * (sqrtMu / std::sqrt(p));

    f64 seconds = dt.value;
    // Whole revolutions of a closed orbit are a no-op, as in propagate().
    if (alpha > 0.0) {
        const f64 period = kTau / (sqrtMu * alpha * std::sqrt(alpha));
        seconds = std::fmod(seconds, period);
    }

    const UniversalContext ctx{
        .mu = m,
        .sqrtMu = sqrtMu,
        .r0 = r0,
        .rdotv = rdotv,
        .alpha = alpha,
        .slr = p,
    };
    const auto solved = solveUniversalAnomaly(ctx, seconds);
    if (!solved) return std::unexpected(solved.error());

    // The Lagrange coefficients again, this time on the perifocal frame, where
    // the position at the start is (r cos v, r sin v) and the velocity is
    // sqrt(mu/p) (-sin v, e + cos v). The new true anomaly is where that lands.
    //
    // Reading it off the position rather than from the solve's own radius is
    // deliberate: p/r - 1 and (r . v) recover e cos v and e sin v, which both
    // vanish on a circular orbit and leave atan2(0, 0). A circular orbit has no
    // periapsis to measure an anomaly from, and this file's convention is that
    // `tra` is then measured from the ascending node instead -- so the answer
    // has to come from the geometry, not from the shape. Measured over the same
    // 40,024 element sets, the two agree within 4e-12 everywhere they are both
    // defined.
    const f64 chi = solved->chi;
    const f64 sigma = rdotv / sqrtMu;
    const f64 f = 1.0 - ((chi * chi / r0) * solved->c2);
    const f64 g =
        ((sigma * chi * chi * solved->c2) + (r0 * chi * (1.0 - (solved->psi * solved->c3)))) /
        sqrtMu;
    const f64 speedScale = sqrtMu / std::sqrt(p);
    const f64 cosNu = std::cos(el.tra.value);
    const f64 sinNu = std::sin(el.tra.value);
    const f64 x = (f * r0 * cosNu) - (g * speedScale * sinNu);
    const f64 y = (f * r0 * sinNu) + (g * speedScale * eccentricityPlusCosTrueAnomaly(el));

    Elements out = el;
    out.tra = wrapTau(Radians{std::atan2(y, x)});
    if (!elementsAreUsable(out)) return std::unexpected(OrbitError::NotFinite);
    return out;
}

} // namespace orb
