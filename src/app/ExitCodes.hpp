#ifndef ORBSIM_APP_EXITCODES_HPP
#define ORBSIM_APP_EXITCODES_HPP
//
// The application's exit codes. 0 is a clean run; anything else says which
// kind of failure, so that a script -- the smoke test, a probe test -- can
// tell a usage error from a lost device from a validation layer complaint
// without parsing the log. In a header of their own since M1-16, when the
// probe mode's source joined main.cpp's in needing them.
//
namespace orb::app {

inline constexpr int kExitFailure = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitValidationErrors = 3;
// A probe's frame differs from its golden image by more than ADR 0008's
// tolerances (M1-17, register decision 230). Only for a measured mismatch,
// which always leaves a diff image; a golden that could not be read is 1.
inline constexpr int kExitGoldenMismatch = 4;

} // namespace orb::app

#endif // ORBSIM_APP_EXITCODES_HPP
