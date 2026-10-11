#ifndef ORBSIM_TESTS_ABORTPROBE_HPP
#define ORBSIM_TESTS_ABORTPROBE_HPP
//
// What every program whose pass is an assertion firing needs before it fires
// one: an abort that simply aborts. The count-wraparound probes
// (`tests/CountWraparound.hpp`) and `tests/angle_between_zero_probe.cpp` share
// it, and `cmake/VerifyAssertionProbe.cmake` judges them all.
//
// Moved here from tests/CountWraparound.hpp by M1-118 (register decision 479),
// so that the one suppression it needs is written once rather than once per
// probe.
//

#if !defined(NDEBUG) && defined(_WIN32)
// _set_abort_behavior and its two flags are declared in the Windows C
// runtime's <stdlib.h> itself; <cstdlib> reaches them only through it, which
// misc-include-cleaner does not accept (measured 2026-10-11). The same
// one-line suppression src/app/main.cpp carries, for the same reason (register
// decisions 155 and 156).
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>
#endif

namespace orb::probe {

// **Without this the program does not die, it waits.** Under the debug runtime
// this tree links, `_WRITE_ABORT_MSG` makes abort's own message a modal box
// rather than a line in the log, and the process sits there alive. Measured
// 2026-09-24, after it hung a mutation pass twice in one run. Three other
// controls were tried and none of them governs this one: `_set_error_mode`,
// `_CrtSetReportMode`, and clearing only `_CALL_REPORTFAULT`. A no-op where
// assertions are compiled out, and on every platform but Windows, where abort
// already just aborts.
inline void abortWithoutWaiting() noexcept {
#if !defined(NDEBUG) && defined(_WIN32)
    static_cast<void>(_set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG));
#endif
}

} // namespace orb::probe

#endif // ORBSIM_TESTS_ABORTPROBE_HPP
