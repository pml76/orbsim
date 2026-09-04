// [S14] SF.5: a .cpp includes the header that specifies its interface, and
// includes it first. That is not a style preference -- it is the mechanism that
// continuously proves Kepler.hpp is self-contained, on every single build.
#include "orbit/Kepler.hpp"

#include "core/Contract.hpp"

#include <cmath>

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

// One ulp of a double near pi is about 4.4e-16. Two orders of magnitude above
// that converges without the iteration chasing rounding noise.
constexpr Tolerance kConvergence{1e-14};

// [S20] JPL Power of Ten, rule 2: every loop must have a provable upper bound.
// Newton roughly doubles its correct digits per step, so from the guesses above
// this converges in well under ten iterations for any eccentricity in range.
// The cap is not a performance budget; it is what makes the bound provable.
constexpr int kMaxIterations = 50;

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
    // on the first pass.
    f64 eccentricAnomaly = m;
    if (e >= kHighEccentricity) {
        eccentricAnomaly = m >= 0.0 ? kPi : -kPi;
    }

    // [S20] Bounded (rule 2) *and* it reports failure (rule 5). The bound on
    // its own is the easy half, and the half most numerical code stops at.
    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        const f64 residual = eccentricAnomaly - (e * std::sin(eccentricAnomaly)) - m;
        const f64 slope = 1.0 - (e * std::cos(eccentricAnomaly));
        const f64 step = -residual / slope;

        eccentricAnomaly += step;

        if (std::abs(step) < kConvergence.value) return Radians{eccentricAnomaly};
    }

    // This is what stops a wrong number leaving the function wearing exactly
    // the same face as a right one.
    return std::unexpected(KeplerError::DidNotConverge);
}

} // namespace orbex
