#ifndef ORBSIM_TESTS_COUNTWRAPAROUND_HPP
#define ORBSIM_TESTS_COUNTWRAPAROUND_HPP
//
// Provoke a count wraparound at run time, which `core/Scalar.hpp` must refuse
// by aborting (M1-12, register decision 136).
//
// **Three programs rather than one with an argument.** A probe can only abort
// once, so each of the three guarded operations needs a process of its own --
// and reading `argv` would need the `-Wunsafe-buffer-usage-in-container`
// suppression that `src/app/main.cpp` carries, which is not worth spending on
// a test. Each entry point is three lines; everything they share is here.
//
// **Why the programs exist at all.** The guard has two halves: a call to a
// function that is not `constexpr`, which refuses a bad literal while the
// compiler works the expression out, and an `ORBSIM_EXPECTS` beside it for a
// value that only exists at run time. No Catch2 case can exercise the second
// half, because a case that provoked it would abort and take the reporter,
// the counts and every case that had not run yet with it. So the claim is
// made by programs whose **failure is the pass**, and
// `cmake/VerifyCountWraparound.cmake` decides which way round that is.
//
// **Why all three, and not just the subtraction.** There was one of these for
// one day, and it covered the subtraction only. Deleting the run-time guard
// from `operator+` then passed the entire `check` -- 200 tests, exit 0,
// measured 2026-09-24 -- because nothing anywhere provoked an addition that
// overflowed. Decision 125 guards three operations; testing one of them and
// writing the box as done is how a hole gets a tick beside it.
//
#include <cstdio>

#include <cstdint>

#ifndef NDEBUG
#include "core/Scalar.hpp"

#include <cstdlib>
#endif

namespace orb::probe {

// Named, not a boolean or an int: three call sites, one word each, and a
// `switch` with no `default` so a fourth operation is a build error at the one
// place that must then be updated.
enum class Operation : std::uint8_t {
    Subtract,
    Add,
    Multiply,
};

// `seed` is a value the compiler cannot see through -- each entry point passes
// `argc`. Nothing here is a constant expression, deliberately: the
// compile-time half of the guard is proved by `static_assert`s beside the type,
// and these programs are about the other half.
[[nodiscard]] inline int runProbe([[maybe_unused]] Operation operation,
                                  [[maybe_unused]] int seed) noexcept {
#ifdef NDEBUG
    std::puts("SKIPPED: assertions are not live in this configuration, so the "
              "run-time half of the guard does not exist here");
    return 0;
#else
#ifdef _WIN32
    // **Without this the program does not die, it waits.** Under the debug
    // runtime this tree links, `_WRITE_ABORT_MSG` makes abort's own message a
    // modal box rather than a line in the log, and the process sits there
    // alive. Measured 2026-09-24, after it hung a mutation pass twice in one
    // run. Three other controls were tried and none of them governs this one:
    // `_set_error_mode`, `_CrtSetReportMode`, and clearing only
    // `_CALL_REPORTFAULT`.
    static_cast<void>(_set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG));
#endif

    const auto small = static_cast<std::uint32_t>(seed);
    const std::uint32_t larger = small + 1U;

    switch (operation) {
    case Operation::Subtract: {
        std::printf("provoking %u - %u, which must not be allowed to wrap\n", small, larger);
        std::fflush(stdout);
        const Texels difference = Texels{small} - Texels{larger};
        std::printf("the guard did not fire: the difference came back as %u\n", difference.value());
        break;
    }
    case Operation::Add: {
        std::printf("provoking %u + %u, which must not be allowed to wrap\n", kCountMaximum, small);
        std::fflush(stdout);
        const Texels sum = Texels{kCountMaximum} + Texels{small};
        std::printf("the guard did not fire: the sum came back as %u\n", sum.value());
        break;
    }
    case Operation::Multiply: {
        std::printf(
            "provoking %u * %u, which must not be allowed to wrap\n", kCountMaximum, larger);
        std::fflush(stdout);
        const Texels product = Texels{kCountMaximum} * larger;
        std::printf("the guard did not fire: the product came back as %u\n", product.value());
        break;
    }
    }

    // Only reached if the assertion did not fire, which is the failure these
    // programs exist to report. The value is printed above because it is the
    // evidence, and `VerifyCountWraparound.cmake` reads that line rather than
    // trusting this exit code alone -- it trusted the code alone for a day,
    // and passed a tree whose guard was gone.
    return 1;
#endif
}

} // namespace orb::probe

#endif // ORBSIM_TESTS_COUNTWRAPAROUND_HPP
