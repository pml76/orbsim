//
// A deliberate count wraparound, provoked at run time, which must abort
// (M1-12, register decision 136).
//
// **Why this is a program of its own rather than a Catch2 case.** The thing
// under test is an assertion, and an assertion that fires calls abort. Catch2
// cannot survive that -- the process is gone, and with it the reporter, the
// counts and every case that had not run yet. So the claim is made by a
// program whose *failure* is the pass, and `cmake/VerifyCountWraparound.cmake`
// decides which way round that is.
//
// **What it closes.** `core/Scalar.hpp`'s wraparound guard has two halves: a
// call to a function that is not constexpr, which refuses a bad literal while
// the compiler is working the expression out, and an ORBSIM_EXPECTS beside it
// for a value that only exists at run time. The compile-time half is asserted
// beside the type and is proved by the mutation pass. The run-time half was
// exercised by nothing at all: M1-12's pass declared a survivor, because
// deleting that assertion changed nothing any test could see. This is that
// survivor's test.
//
// **Why it reports itself skipped rather than passed under NDEBUG.** There,
// `assert` expands to nothing and the run-time half of the guard genuinely
// does not exist, so the count wraps and the program returns normally. That is
// not a failure and it is not a pass either -- it is the claim being
// inapplicable, which is what a skip means. A test that quietly passed in the
// release tree would be reporting a check it had not made, which is the shape
// `VERIFICATION.md` rule 23 is about.
//
#include <cstdio>

#ifndef NDEBUG

#include "core/Scalar.hpp"

#include <cstdint>
#include <cstdlib>

int main(int argc, char** argv) {
    static_cast<void>(argv);

#ifdef _WIN32
    // **Without this the program does not die, it waits.** Measured
    // 2026-09-23, after the first version hung the build for fifteen minutes:
    // the assertion fired, its message reached stderr, and the process then
    // sat there alive and answering. `_WRITE_ABORT_MSG` is the bit
    // responsible -- under the debug runtime this tree links (`ucrtbased.dll`,
    // checked rather than assumed) that "message" is a modal box, not a line
    // in the log, and nobody was there to click it. Clearing both bits makes
    // abort simply abort, and the process exits 3.
    //
    // Three other things were tried first and none of them worked, recorded so
    // that nobody spends the afternoon again: keeping `_WRITE_ABORT_MSG` on
    // while clearing `_CALL_REPORTFAULT`, which still hangs;
    // `_set_error_mode(_OUT_TO_STDERR)`, which governs a different report; and
    // `_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE)`, which governs
    // `_ASSERTE` and not the standard `assert` this project uses.
    static_cast<void>(_set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG));
#endif

    // Drawn from argc so that nothing here is a constant expression: the
    // compile-time half of the guard is proved beside the type, and this
    // program is about the other half. argc is 1, so these are 1 and 2.
    const auto small = static_cast<std::uint32_t>(argc);
    const auto large = static_cast<std::uint32_t>(argc) + 1U;

    std::printf("provoking %u - %u, which must not be allowed to wrap\n", small, large);
    std::fflush(stdout);

    const orb::Texels difference = orb::Texels{small} - orb::Texels{large};

    // Only reached if the assertion did not fire, which is the failure this
    // program exists to report. The value is printed because it is the
    // evidence: four billion and something, where one less than nothing was
    // asked for.
    std::printf("the guard did not fire: the difference came back as %u\n", difference.value());
    return 1;
}

#else

int main() {
    std::puts("SKIPPED: assertions are not live in this configuration, so the "
              "run-time half of the guard does not exist here");
    return 0;
}

#endif
