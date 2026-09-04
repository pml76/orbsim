#pragma once
//
// Kepler's equation, M = E - e*sin(E), solved for the eccentric anomaly.
//
// There is no closed form, so this is Newton-Raphson. The interesting part is
// not the iteration -- it is that the function tells you when it failed.
//
// [S12] Pure simulation. This header knows nothing about rendering, windowing
// or any graphics API, and it never will: the dependency runs core -> orbit ->
// render and never back.
//
#include "core/Units.hpp"

#include <expected>
#include <string_view>

namespace orbex {

// [S8] enum class: scoped, typed, no surprise conversions to int.
enum class KeplerError {
    EccentricityOutOfRange,
    DidNotConverge,
};

[[nodiscard]] constexpr std::string_view describe(KeplerError error) noexcept {
    switch (error) {
    case KeplerError::EccentricityOutOfRange:
        return "eccentricity must be in [0, 1) for an elliptic orbit";
    case KeplerError::DidNotConverge:
        return "Kepler solver reached its iteration limit without converging";
    }
    return "unknown Kepler error";
}

// Solves M = E - e*sin(E) for E.
//
// [S2] Preconditions: 0 <= ecc < 1, and a NaN eccentricity is rejected. This is
// *reported* rather than asserted, because an eccentricity generally arrives
// from a scenario file and is therefore user input, not a programmer error.
// docs/adr/0002 draws that line.
//
// [S7] Guarantees: on success, |E - e*sin(E) - M| < 1e-13. On failure nothing
// is returned at all, so there is no "approximately right" value lying around
// waiting to be used by accident.
//
// [S2] Note the parameter types. `solveKepler(f64, f64)` would take two
// adjacent doubles that transpose in silence -- I.24 exactly. These two cannot.
[[nodiscard]] std::expected<Radians, KeplerError> solveKepler(Radians meanAnomaly,
                                                              Eccentricity ecc) noexcept;

// Normalises an angle into (-pi, pi]. Exposed because the tests check the
// solver against Kepler's equation directly and need the same normalisation.
[[nodiscard]] Radians wrapToPi(Radians angle) noexcept;

} // namespace orbex
