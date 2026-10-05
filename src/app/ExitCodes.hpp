#ifndef ORBSIM_APP_EXITCODES_HPP
#define ORBSIM_APP_EXITCODES_HPP
//
// The application's exit codes. 0 is a clean run; anything else says which
// kind of failure, so that a script -- the smoke test, a probe test -- can
// tell a usage error from a lost device from a validation layer complaint
// without parsing the log. In a header of their own since M1-16, when the
// probe mode's source joined main.cpp's in needing them.
//
// **Since M1-108, the order a probe run's codes are ranked in is here too**,
// as a function the compiler checks below, rather than written into main.cpp
// where no test could reach it (register decisions 273 and 385).
//
#include "core/Contract.hpp"

#include <cstdint>
#include <limits>

namespace orb::app {

inline constexpr int kExitFailure = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitValidationErrors = 3;
// A probe's frame differs from its golden image by more than ADR 0008's
// tolerances (M1-17, register decision 230). Only for a measured mismatch,
// which always leaves a diff image; a golden that could not be read is 1.
inline constexpr int kExitGoldenMismatch = 4;

// What a probe run ended with, before its exit code is decided: the probe's
// own verdict -- 0, kExitFailure, kExitUsage or kExitGoldenMismatch -- and
// the validation layers' count, read once the renderer is torn down. A
// struct, so that the two numbers are named at the call and cannot be
// transposed (non-negotiable 1).
struct ProbeResults {
    int probeExitCode{};
    std::uint32_t validationErrors{};
};

// Whether an --accept-golden run may write its frame over the golden.
enum class GoldenWrite : std::uint8_t { Withheld, Allowed };

struct ProbeExit {
    int exitCode{};
    GoldenWrite golden{};

    friend constexpr bool operator==(const ProbeExit&, const ProbeExit&) = default;
};

// A probe run's exit code, in register decision 230's order: a failure of the
// probe itself first, then validation errors -- which make the frame itself
// suspect -- then a golden mismatch. A usage error ranks with a failure
// (decision 386). The golden may be written only when all of them are clear
// (decision 232), so a mismatch withholds it as well; an --accept-golden run
// reports none, since it compares only to print the difference.
//
// Only the validation count can make the code 3, which is what the
// precondition says: a probe that returned 3 itself would be read as a count.
[[nodiscard]] constexpr ProbeExit probeExit(ProbeResults results) {
    ORBSIM_EXPECTS(results.probeExitCode != kExitValidationErrors);
    if (results.probeExitCode != 0 && results.probeExitCode != kExitGoldenMismatch) {
        return {.exitCode = results.probeExitCode, .golden = GoldenWrite::Withheld};
    }
    if (results.validationErrors != 0) {
        return {.exitCode = kExitValidationErrors, .golden = GoldenWrite::Withheld};
    }
    if (results.probeExitCode == kExitGoldenMismatch) {
        return {.exitCode = kExitGoldenMismatch, .golden = GoldenWrite::Withheld};
    }
    return {.exitCode = 0, .golden = GoldenWrite::Allowed};
}

// Every pairing, checked by the compiler: each probe verdict with no
// validation error, with one, and with the most the counter can hold.
inline constexpr std::uint32_t kMostValidationErrors = std::numeric_limits<std::uint32_t>::max();

// A clean run, and only a clean run, may write the golden.
static_assert(probeExit({.probeExitCode = 0, .validationErrors = 0}) ==
              ProbeExit{.exitCode = 0, .golden = GoldenWrite::Allowed});
static_assert(probeExit({.probeExitCode = 0, .validationErrors = 1}) ==
              ProbeExit{.exitCode = kExitValidationErrors, .golden = GoldenWrite::Withheld});
static_assert(probeExit({
                  .probeExitCode = 0,
                  .validationErrors = kMostValidationErrors,
              }) == ProbeExit{.exitCode = kExitValidationErrors, .golden = GoldenWrite::Withheld});
// A failure outranks validation errors.
static_assert(probeExit({.probeExitCode = kExitFailure, .validationErrors = 0}) ==
              ProbeExit{.exitCode = kExitFailure, .golden = GoldenWrite::Withheld});
static_assert(probeExit({.probeExitCode = kExitFailure, .validationErrors = 1}) ==
              ProbeExit{.exitCode = kExitFailure, .golden = GoldenWrite::Withheld});
static_assert(probeExit({
                  .probeExitCode = kExitFailure,
                  .validationErrors = kMostValidationErrors,
              }) == ProbeExit{.exitCode = kExitFailure, .golden = GoldenWrite::Withheld});
// So does a usage error (decision 386).
static_assert(probeExit({.probeExitCode = kExitUsage, .validationErrors = 0}) ==
              ProbeExit{.exitCode = kExitUsage, .golden = GoldenWrite::Withheld});
static_assert(probeExit({.probeExitCode = kExitUsage, .validationErrors = 1}) ==
              ProbeExit{.exitCode = kExitUsage, .golden = GoldenWrite::Withheld});
// Validation errors outrank a mismatch, and a mismatch alone is 4.
static_assert(probeExit({.probeExitCode = kExitGoldenMismatch, .validationErrors = 0}) ==
              ProbeExit{.exitCode = kExitGoldenMismatch, .golden = GoldenWrite::Withheld});
static_assert(probeExit({.probeExitCode = kExitGoldenMismatch, .validationErrors = 1}) ==
              ProbeExit{.exitCode = kExitValidationErrors, .golden = GoldenWrite::Withheld});
static_assert(probeExit({
                  .probeExitCode = kExitGoldenMismatch,
                  .validationErrors = kMostValidationErrors,
              }) == ProbeExit{.exitCode = kExitValidationErrors, .golden = GoldenWrite::Withheld});

} // namespace orb::app

#endif // ORBSIM_APP_EXITCODES_HPP
