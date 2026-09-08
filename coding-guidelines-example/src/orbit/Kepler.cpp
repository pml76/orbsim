// [S14] SF.5: a .cpp includes the header that specifies its interface, and
// includes it first. That is not a style preference -- it is the mechanism that
// continuously proves Kepler.hpp is self-contained, on every single build.
#include "orbit/Kepler.hpp"

#include "core/Contract.hpp"
#include "core/Units.hpp"
#include "core/Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>

namespace orbex {
namespace {
// [S14] An unnamed namespace, so these have internal linkage: invisible to the
// linker, and free to rename without rebuilding anything downstream.

// [S16] Every constant says where its number came from. A bare 1e-14 is a
// mystery that nobody will later dare to change.

// Below this eccentricity the mean anomaly is a good enough starting guess.
// Above it the orbit spends nearly all of its mean anomaly close to periapsis,
// M becomes a poor guess, and starting at +/-pi keeps Newton inside the
// convergent basin. 0.8 is the classical threshold from Danby's formulation.
constexpr f64 kHighEccentricity = 0.8;

// The solve runs to the resolution of a double rather than to a hand-picked
// tolerance, in units of the last place, and can afford to because bisection
// bounds the work below.
//
// It was 1e-14, and the reason it changed is worth more than the number. A
// plain Newton needs a loose tolerance to be sure of stopping, and then
// overshoots it by orders through quadratic convergence -- so a suite built
// around one is really measuring the overshoot. A safeguarded solver's last
// step is often a bisection, which lands *on* the tolerance instead.
constexpr f64 kConvergenceUlps = 4.0;

// [S20] JPL Power of Ten, rule 2: every loop must have a provable upper bound.
// The step below is required to at least halve, so the bracket shrinks at least
// as fast as bisection and about 60 iterations close it at double precision.
// The cap is not a performance budget; it is what makes the bound provable.
constexpr int kMaxIterations = 100;

// [S17] Extracted from solveKepler so each function does one thing (F.2) and
// neither exceeds `readability-function-size`. This one refines a guess; the
// caller decides what the answer means.
//
// It is a *safeguarded* Newton, and the safeguarding is the point. Until
// 2026-09-08 this was a plain Newton -- bounded and reporting failure, which
// the comments below rightly called the two halves most numerical code stops
// short of, and which still was not enough. Measured over 2001 anomalies per
// eccentricity, it failed to converge on 2 of them at e = 0.9999. The parent
// project had the same shape and was far worse, failing on 196 of 401
// hyperbolic anomalies at e = 1.0001. This directory is the reference for the
// house style, so it should not be teaching the version that loses.
//
// Why Newton alone fails: the step is `residual / slope`, and the slope
// `1 - e cos E` approaches zero as e approaches 1, so near periapsis the step
// becomes enormous and the iteration oscillates. Raising the cap does not
// help -- it is not converging slowly, it is not converging.
//
// Why this is guaranteed rather than merely better: M(E) = E - e sin E is
// *strictly increasing*, since dM/dE = 1 - e cos E >= 1 - e > 0. A strictly
// monotonic function has exactly one root, and with M wrapped into [-pi, pi]
// the root lies there too -- so the bracket costs nothing. Newton is taken
// only while its step stays inside the bracket *and* at least halves the
// previous one; otherwise the interval is bisected, which cannot diverge.
//
// That pair is the classical rtsafe, and the halving half is the one that is
// easy to leave out: a step can sit well inside the bracket and still creep
// toward the root one unit at a time, which a bracket test never notices.
//
// [S2] The parameters are the strong types rather than three bare doubles, and
// that is not decoration here: `(f64, f64, f64)` puts two interchangeable
// `f64`s side by side, which is I.24 exactly, and
// `bugprone-easily-swappable-parameters` says so out loud. With `Eccentricity`
// between the two `Radians` there is no transposable pair left.
[[nodiscard]] std::expected<Radians, KeplerError>
refineEccentricAnomaly(Radians guess, Eccentricity ecc, Radians mean) noexcept {
    const f64 e = ecc.value;
    const f64 m = mean.value;

    f64 eccentricAnomaly = guess.value;
    f64 low = -kPi;
    f64 high = kPi;
    f64 previousStep = high - low;
    f64 step = previousStep;

    f64 residual = eccentricAnomaly - (e * std::sin(eccentricAnomaly)) - m;
    f64 slope = 1.0 - (e * std::cos(eccentricAnomaly));

    // [S20] Bounded (rule 2) *and* it reports failure (rule 5).
    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        const f64 newtonNext = eccentricAnomaly - (residual / slope);
        const bool leavesBracket =
            !std::isfinite(newtonNext) || newtonNext <= low || newtonNext >= high;
        const bool tooSlow = std::abs(2.0 * residual) > std::abs(previousStep * slope);

        previousStep = step;
        if (leavesBracket || tooSlow) {
            step = 0.5 * (high - low);
            eccentricAnomaly = low + step;
        } else {
            step = residual / slope;
            eccentricAnomaly -= step;
        }

        // [S16] Relative to the magnitude, floored at 1, because an eccentric
        // anomaly is O(1) radians and an absolute tolerance would mean
        // different things at 1e-3 and at pi.
        const f64 resolution = kConvergenceUlps * std::numeric_limits<f64>::epsilon() *
                               std::max(std::abs(eccentricAnomaly), 1.0);
        if (std::abs(step) <= resolution) return Radians{eccentricAnomaly};

        residual = eccentricAnomaly - (e * std::sin(eccentricAnomaly)) - m;
        slope = 1.0 - (e * std::cos(eccentricAnomaly));
        if (residual > 0.0) {
            high = eccentricAnomaly;
        } else {
            low = eccentricAnomaly;
        }
    }

    // Unreachable while the bracket is valid, because the halving condition
    // makes the interval shrink at least as fast as bisection. Kept anyway: a
    // bounded loop that stays silent when it fails has done only half the job,
    // and "unreachable" is a claim about today's arithmetic.
    return std::unexpected(KeplerError::DidNotConverge);
}

} // namespace

Radians wrapToPi(Radians angle) noexcept {
    const f64 wrapped = std::fmod(angle.value, kTau);

    // [S21] Three returns in five lines. NR.2: the single-return rule is a
    // habit from a language without destructors.
    if (wrapped > kPi) return Radians{wrapped - kTau};
    if (wrapped < -kPi) return Radians{wrapped + kTau};
    return Radians{wrapped};
}

std::expected<Radians, KeplerError> solveKepler(Radians meanAnomaly, Eccentricity ecc) noexcept {
    // [S11] Negated comparisons, so that a NaN eccentricity is rejected as
    // well. The natural spelling -- `ecc.value < 0.0 || ecc.value >= 1.0` -- is
    // false for NaN, which would hand NaN to Newton, and Newton would hand back
    // NaN as though it had converged.
    if (!(ecc.value >= 0.0) || !(ecc.value < 1.0)) {
        return std::unexpected(KeplerError::EccentricityOutOfRange);
    }

    // [S21] Declared at first use, in the smallest scope that works (NR.1).
    const f64 e = ecc.value;
    const f64 m = wrapToPi(meanAnomaly).value;

    // [S20] Rule 5: an assertion on something that cannot happen. wrapToPi has
    // just guaranteed this, and if it ever stops doing so, the failure surfaces
    // here rather than as a wrong trajectory twenty minutes into a flight.
    ORBEX_EXPECTS(m >= -kPi && m <= kPi);
    ORBEX_EXPECTS(e >= 0.0 && e < 1.0);

    // [S16] Below the threshold the mean anomaly is a good enough starting
    // guess. Above it, start from the periapsis-side extreme instead. Written
    // as an if rather than a nested conditional, which nobody reads correctly
    // on the first pass. It is only a hint now: the bracket below is what makes
    // the answer safe when the hint is wrong.
    f64 eccentricAnomaly = m;
    if (e >= kHighEccentricity) {
        eccentricAnomaly = m >= 0.0 ? kPi : -kPi;
    }

    return refineEccentricAnomaly(Radians{eccentricAnomaly}, ecc, Radians{m});
}

} // namespace orbex
