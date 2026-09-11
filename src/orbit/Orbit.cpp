#include "orbit/Orbit.hpp"

#include "core/Contract.hpp"
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

// An orbit is treated as circular / equatorial below these thresholds, at which
// point the periapsis direction / ascending node stops being meaningful.
//
// Where 1e-9 comes from, since a bare number here is exactly what section 12
// warns about. Both are dimensionless by construction -- an eccentricity and a
// ratio |n|/|h| -- so unlike the tolerances below they carry no hidden length
// scale. The value is the point at which the angle these quantities determine
// stops being computable to useful precision: the periapsis direction of an
// orbit with e = 1e-9 is set by the ninth significant digit of the
// eccentricity vector, and f64 subtraction in `evec` leaves roughly seven
// behind it. Below that the canonical parameterisation (aop = 0, tra =
// argument of latitude) is not an approximation -- it is the only answer that
// is stable.
constexpr f64 kCircularTol = 1e-9;
constexpr f64 kEquatorialTol = 1e-9;

// Element propagation refuses an eccentricity within this of 1, where the
// classical Kepler equation is too ill-conditioned to be worth solving:
// without the refusal it was up to 2,500% out on nearly radial and
// near-parabolic states, and just outside it, it is still up to 3.5% out at
// |e - 1| = 1e-9, falling as 1 / |e - 1| (measured against propagate(),
// 2026-09-11). It no longer decides which conic a state is on; conicOf does,
// from the energy. Dimensionless too: a distance from e = 1.
constexpr f64 kParabolicTol = 1e-9;

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

constexpr f64 clampUnit(f64 v) { return std::clamp(v, -1.0, 1.0); }
constexpr f64 sign(f64 v) { return v < 0.0 ? -1.0 : 1.0; }

// Stumpff functions C(psi) and S(psi), the series that make the universal
// variable formulation conic-agnostic. Near psi = 0 the closed forms evaluate
// to 0/0, so a truncated series takes over.
void stumpff(f64 psi, f64& c2, f64& c3) {
    if (psi > 1e-6) {
        const f64 s = std::sqrt(psi);
        c2 = (1.0 - std::cos(s)) / psi;
        c3 = (s - std::sin(s)) / (psi * s);
    } else if (psi < -1e-6) {
        const f64 s = std::sqrt(-psi);
        c2 = (1.0 - std::cosh(s)) / psi;
        c3 = (std::sinh(s) - s) / (s * s * s);
    } else {
        c2 = 0.5 - (psi / 24.0) + (psi * psi / 720.0);
        c3 = (1.0 / 6.0) - (psi / 120.0) + (psi * psi / 5040.0);
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
};

[[nodiscard]] f64
initialUniversalAnomaly(const StateVector& sv, const UniversalContext& ctx, f64 seconds) {
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

    // Parabola: Barker's equation, solved through the cubic substitution.
    const f64 hmag = length(cross(sv.pos, sv.vel));
    const f64 p = hmag * hmag / ctx.mu;
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
solveUniversalAnomaly(const StateVector& sv, const UniversalContext& ctx, f64 seconds) {
    const f64 target = ctx.sqrtMu * seconds;

    // chi = 0 exactly; see the guard in the guess, and why it is fpclassify.
    if (std::fpclassify(seconds) == FP_ZERO) return UniversalSolution{};

    // time(0) = 0 exactly, and time is increasing, so the root sits on the same
    // side of zero as the target does.
    const f64 direction = target > 0.0 ? 1.0 : -1.0;

    // Start from the conic-specific guess where it is usable, and from a
    // length scale where it is not -- sqrt(r0) has the units of chi.
    f64 far = initialUniversalAnomaly(sv, ctx, seconds);
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
    return UniversalSolution{.chi = *solved, .psi = fin.psi, .c2 = fin.c2, .c3 = fin.c3};
}

// The four vectors the in-plane angles are measured from. Grouped into a struct
// rather than passed as four adjacent Vec3 parameters, which would transpose in
// silence (I.24, and `bugprone-easily-swappable-parameters` would say so).
struct OrbitFrame {
    Vec3 r;    // position
    Vec3 v;    // velocity
    Vec3 h;    // specific angular momentum
    Vec3 node; // toward the ascending node; zero for an equatorial orbit
    Vec3 evec; // toward periapsis; zero for a circular orbit
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
    const f64 rdotv = dot(frame.r, frame.v);

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

    f64 aop = angleBetween(ref, frame.evec).value;
    if (dot(cross(ref, frame.evec), frame.h) < 0.0) aop = kTau - aop;
    el.aop = wrapTau(Radians{aop});

    f64 tra = angleBetween(frame.evec, frame.r).value;
    if (rdotv < 0.0) tra = kTau - tra; // inbound half of the orbit
    el.tra = wrapTau(Radians{tra});
}

// Fills in sma, and keeps e on the side of 1 that matches it. Split out of
// elementsFromState, as assignInPlaneAngles is, so neither exceeds the size
// limit; and like it, the magnitudes are recomputed here rather than passed as
// adjacent f64 parameters that transpose in silence.
//
// The conic is the energy's to decide, by the same test propagate() uses
// (conicOf), and sma says which: finite and positive for an ellipse, negative
// for a hyperbola, infinite for a parabola.
//
// And e is kept on the side of 1 the energy says. Near radial it is 1 as a
// double for ellipses and hyperbolas alike -- a probe falling at 100 m/s with
// a 1 um/s drift has e = 1 - 1.8e-20 -- and every consumer that branches on
// e < 1, the anomaly conversions among them, would be guessing. Moving it to
// the adjacent double changes it by less than the eccentricity vector's own
// rounding.
void assignConic(Elements& el, const StateVector& sv, GravParam mu) {
    const f64 m = mu.value;
    const f64 rmag = length(sv.pos);
    const f64 vmag = length(sv.vel);
    const f64 energy = (vmag * vmag * 0.5) - (m / rmag);
    const f64 alpha = (2.0 / rmag) - (vmag * vmag / m);
    const Conic conic = conicOf(alpha * rmag);
    el.sma = Metres{conic == Conic::Parabola ? kInf : -m / (2.0 * energy)};
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

} // namespace

// --- state <-> elements ----------------------------------------------------

std::expected<Elements, OrbitError> elementsFromState(const StateVector& sv, GravParam mu) {
    if (!isFinite(sv) || !std::isfinite(mu.value)) return std::unexpected(OrbitError::NotFinite);
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);

    const Vec3& r = sv.pos;
    const Vec3& v = sv.vel;

    const f64 rmag = length(r);
    // Reported rather than asserted: a scenario file can put a vessel at the
    // exact centre of a planet, and that is something to explain to the user
    // rather than a bug in this file.
    if (!(rmag > 0.0)) return std::unexpected(OrbitError::DegenerateState);

    const f64 m = mu.value;
    const f64 vmag = length(v);

    // Finite components do not imply a finite magnitude. A vector whose length
    // exceeds the largest double has an infinite length, and `rmag > 0.0` is
    // true of infinity, so the test above waves it through. A fuzzer found
    // this when length() still squared its components and overflowed above
    // about 1.3e154: the elements came back claiming success with an infinite
    // eccentricity and a NaN argument of periapsis, which is precisely the
    // "succeeded, and the answer is NaN" outcome the type system cannot
    // prevent and the caller has no way to detect. length() is exact to 2 ulp
    // at every scale now (core/Math.hpp), but a magnitude beyond 1.8e308 is
    // still infinite, and the quantities below still square things; the
    // postcondition at the end catches what overflows there.
    if (!std::isfinite(rmag) || !std::isfinite(vmag)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    const f64 rdotv = dot(r, v);

    const Vec3 h = cross(r, v); // specific angular momentum
    const f64 hmag = length(h);

    // A radial trajectory has no orbital plane, and `inc` below would be
    // acos(h.z / hmag) = acos(0/0) = NaN returned as a success. The test is
    // dimensionless on purpose (section 12, and the lesson of this file):
    // hmag / (rmag * vmag) is |sin| of the angle between position and
    // velocity, so the threshold compares like with like at every scale.
    //
    // 1e-12 does not clip real orbits. That ratio is the cosine of the flight
    // path angle, and reaching 1e-12 needs an eccentricity within about 1e-24
    // of 1 -- indistinguishable from a straight line in f64. The e = 0.9999
    // case in the suite sits around 1e-2.
    if (!(hmag > kRectilinearTol * rmag * vmag)) {
        return std::unexpected(OrbitError::RectilinearOrbit);
    }

    const Vec3 node = cross(Vec3{0, 0, 1}, h); // points at the ascending node

    // The eccentricity vector points from the focus toward periapsis.
    const Vec3 evec = (r * ((vmag * vmag) - (m / rmag)) - v * rdotv) / m;

    Elements el;
    el.ecc = Eccentricity{length(evec)};
    el.slr = Metres{hmag * hmag / m};
    el.inc = Radians{std::acos(clampUnit(h.z / hmag))};

    // Every conic has a positive semi-latus rectum, so a zero here is not an
    // orbit shape -- it is `hmag * hmag` underflowing, which the ratio test
    // above cannot see because it never squares anything. The consequence
    // downstream is that orbitInfo's radius becomes slr / (1 + e cos v) =
    // 0/0 at the asymptote of the parabola this then looks like. Same
    // conclusion as the ratio test, reached by a different route, so it is
    // reported as the same thing: too little angular momentum to be an orbit.
    if (!(el.slr.value > 0.0)) return std::unexpected(OrbitError::RectilinearOrbit);

    assignConic(el, sv, mu);
    assignInPlaneAngles(el, {.r = r, .v = v, .h = h, .node = node, .evec = evec});

    if (!elementsAreUsable(el)) return std::unexpected(OrbitError::NotFinite);
    return el;
}

StateVector stateFromElements(const Elements& el, GravParam mu) {
    // Asserted, not reported: every caller of this obtains its elements from
    // elementsFromState or builds them literally, so a non-positive mu here is
    // a bug in the caller rather than user input.
    ORBSIM_EXPECTS(mu.value > 0.0);

    // Prefer the stored semi-latus rectum: it is the one shape parameter that
    // stays finite on a parabolic orbit.
    const f64 e = el.ecc.value;
    const f64 p = (el.slr.value > 0.0) ? el.slr.value : el.sma.value * (1.0 - (e * e));
    ORBSIM_EXPECTS(p > 0.0);

    const f64 cosNu = std::cos(el.tra.value);
    const f64 sinNu = std::sin(el.tra.value);
    const f64 rmag = p / (1.0 + (e * cosNu));
    const f64 k = std::sqrt(mu.value / p);

    // Perifocal frame: x toward periapsis, z along angular momentum.
    const Vec3 rPerifocal{rmag * cosNu, rmag * sinNu, 0.0};
    const Vec3 vPerifocal{-k * sinNu, k * (e + cosNu), 0.0};

    // Perifocal -> inertial: Rz(lan) * Rx(inc) * Rz(aop), applied right to left.
    const Quat rot = Quat::fromAxisAngle({0, 0, 1}, el.lan) *
                     Quat::fromAxisAngle({1, 0, 0}, el.inc) *
                     Quat::fromAxisAngle({0, 0, 1}, el.aop);

    return {.pos = rot.rotate(rPerifocal), .vel = rot.rotate(vPerifocal)};
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
    // (2026-09-11), p / (1 - e) was up to 506% out on 27,242 nearly radial
    // ellipses and 3.6e-10 on 138,754 ordinary ones; this is within 4.1e-11
    // and 2.7e-12.
    info.apoapsis = Metres{info.closed ? el.sma.value * (1.0 + e) : kInf};
    info.radius = Metres{el.slr.value / (1.0 + (e * std::cos(el.tra.value)))};

    if (info.closed) {
        const f64 a = el.sma.value;
        info.meanMotion = RadiansPerSecond{std::sqrt(m / (a * a * a))};
        info.period = Seconds{kTau / info.meanMotion.value};
        info.energy = SpecificEnergy{-m / (2.0 * a)};
        // Vis-viva. Clamped at zero because rounding can push the radicand a
        // hair negative at the apoapsis of a near-circular orbit.
        info.speed =
            MetresPerSecond{std::sqrt(std::max(0.0, m * ((2.0 / info.radius.value) - (1.0 / a))))};
    } else {
        info.meanMotion = RadiansPerSecond{0.0};
        info.period = Seconds{kInf};
        info.energy = SpecificEnergy{std::isinf(el.sma.value) ? 0.0 : -m / (2.0 * el.sma.value)};
        info.speed = MetresPerSecond{
            std::sqrt(std::max(0.0, 2.0 * (info.energy.value + (m / info.radius.value))))};
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

    const UniversalContext ctx{.mu = m, .sqrtMu = sqrtMu, .r0 = r0, .rdotv = rdotv, .alpha = alpha};
    const auto solved = solveUniversalAnomaly(sv, ctx, seconds);
    if (!solved) return std::unexpected(solved.error());

    const f64 chi = solved->chi;
    const f64 psi = solved->psi;
    const f64 c2 = solved->c2;
    const f64 c3 = solved->c3;

    // Lagrange coefficients: the new state is a linear combination of the old
    // position and velocity vectors.
    const f64 f = 1.0 - ((chi * chi / r0) * c2);
    const f64 g = seconds - ((chi * chi * chi / sqrtMu) * c3);
    const Vec3 rNew = sv.pos * f + sv.vel * g;

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
    const f64 fdot = (sqrtMu / (rMag * r0)) * chi * ((psi * c3) - 1.0);
    const Vec3 vNew = sv.pos * fdot + sv.vel * gdot;

    const StateVector out{.pos = rNew, .vel = vNew};
    // The same postcondition elementsFromState carries: a success must be a
    // usable number. The velocity can still overflow after the position has
    // survived, because fdot divides by rMag.
    if (!isFinite(out)) return std::unexpected(OrbitError::NotFinite);
    return out;
}

std::expected<Elements, OrbitError>
propagateElements(const Elements& el, GravParam mu, Seconds dt) {
    if (!std::isfinite(mu.value) || !std::isfinite(dt.value)) {
        return std::unexpected(OrbitError::NotFinite);
    }
    if (!(mu.value > 0.0)) return std::unexpected(OrbitError::NonPositiveGravity);
    // The mean motion below is sqrt(mu / a^3), and a parabola has no a; and
    // within kParabolicTol of e = 1, where a nearly radial ellipse or
    // hyperbola has a finite one, the Kepler equation below is too
    // ill-conditioned to trust. Saying so beats feeding either to the solver.
    if (!std::isfinite(el.sma.value) || std::abs(el.ecc.value - 1.0) <= kParabolicTol) {
        return std::unexpected(OrbitError::ParabolicElements);
    }

    const Radians eccentricAtStart = trueToEccentricAnomaly(el.tra, el.ecc);
    const Radians meanAtStart = eccentricToMeanAnomaly(eccentricAtStart, el.ecc);

    const f64 a = std::abs(el.sma.value);
    const f64 meanMotion = std::sqrt(mu.value / (a * a * a));

    const std::expected<Radians, OrbitError> eccentricAtEnd =
        meanToEccentricAnomaly(Radians{meanAtStart.value + (meanMotion * dt.value)}, el.ecc);
    if (!eccentricAtEnd) return std::unexpected(eccentricAtEnd.error());

    Elements out = el;
    out.tra = eccentricToTrueAnomaly(*eccentricAtEnd, el.ecc);
    return out;
}

} // namespace orb
