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

} // namespace orb::app

#endif // ORBSIM_APP_EXITCODES_HPP
