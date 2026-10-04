//
// Make a failed assertion end the process instead of asking a question.
//
// **Compiled into every suite** (see `ORBSIM_TESTS` in `CMakeLists.txt`), not
// linked in from `orbsim_test_support`. That is deliberate: a registrar in a
// static library is only pulled in if something references it, and a suite
// that uses none of the support code -- `test_render_quality` uses none --
// would link without it and silently lose the behaviour. A translation unit
// compiled directly into each executable cannot be dropped.
//
// **Why it exists.** On Windows the debug C runtime turns `abort`'s own
// message into a modal dialog rather than a line in the log, and the process
// then sits there alive, waiting for somebody to click. Nothing in a healthy
// build reaches it: assertions only fire when the code is wrong. A **mutation
// pass** makes the code wrong on purpose, which is the point of it, so a
// mutant that trips an assertion inside a Catch2 case hangs the pass and puts
// a dialog on the screen of whoever is running it. That happened on
// 2026-09-24, twice in one run, with two of M1-12's mutants.
//
// `tests/count_wraparound_probe.cpp` carries the same call and the measurement
// behind it: `_WRITE_ABORT_MSG` is the bit responsible, and clearing both bits
// is what makes abort simply abort. Three other controls were tried first and
// none of them worked; the probe's comment lists them so nobody repeats the
// afternoon.
//
// **And the debug library's own checks, since 2026-10-04** (M1-21, register
// decision 363). Microsoft's debug standard library checks its iterators --
// stepping before a vector's first element, say -- and reports a failure
// through the C runtime's debug report, not through `abort`, so the setting
// above does not reach it: the report opens its own dialog and the process
// waits. M1-21's mutation pass found it, a mutant that stepped before the
// first keyframe hanging its suite for the harness's full 300 s. Sending the
// two kinds of report that stop a program -- an assertion and an error -- to
// stderr instead makes the program print the message and stop, measured the
// same day on a program that steps before `begin()`: a hang without this, the
// message "cannot seek vector iterator before begin" and an exit with it. A
// no-op in a release build, where the debug report does not exist.
//
// A no-op everywhere else. On Linux and macOS abort already just aborts.
//
#include <catch2/catch_test_run_info.hpp>
#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

// _set_abort_behavior and its two flags are declared in the Windows C
// runtime's <stdlib.h> itself; <cstdlib> reaches them only through it. The
// same one-line suppression src/app/main.cpp carries, for the same reason
// (register decisions 155 and 156).
#ifdef _WIN32
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>

#include <crtdbg.h>
#endif

namespace {

// A Catch2 listener rather than an object constructed during static
// initialisation: this runs at a defined moment, once, after the runtime is up
// and before the first case, and it needs no global of our own.
class AbortWithoutADialog final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
#ifdef _WIN32
        static_cast<void>(_set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG));
        static_cast<void>(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR));
        static_cast<void>(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR));
#endif
    }
};

} // namespace

CATCH_REGISTER_LISTENER(AbortWithoutADialog)
