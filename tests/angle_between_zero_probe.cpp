//
// Ask `angleBetween` for the angle to a zero vector, which must abort (M1-118,
// finding 1.7, register decisions 472 and 476).
//
// A zero vector has no direction. `angleBetween` used to answer pi/2 for one,
// because `directionOf` returns the zero vector and the formula then gives
// 2 atan2(1, 1). No caller can reach it today -- `elementsFromState` refuses a
// zero radius before it asks -- so it is a precondition, asserted, and an
// assertion that fires ends the process. That is why this is a program and not
// a Catch2 case, and why `cmake/VerifyAssertionProbe.cmake` judges it: the
// program aborting is the pass, and it accepts only an abort that carries the
// assertion's own message.
//
// The same shape as `tests/CountWraparound.hpp`, whose comments say why each
// part is there, and the same `tests/AbortProbe.hpp` before it fires.
//
#include <cstdio>

#ifndef NDEBUG
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "tests/AbortProbe.hpp"

#include <print>
#endif

int main(int argc, char** /*argv*/) {
#ifdef NDEBUG
    static_cast<void>(argc);
    static_cast<void>(std::puts("SKIPPED: assertions are not live in this configuration, so "
                                "angleBetween's precondition does not exist here"));
    return 0;
#else
    orb::probe::abortWithoutWaiting();
    // argc is 1, and the compiler cannot see that: the zero is not a constant.
    const auto zero = static_cast<orb::f64>(argc - 1);
    try {
        std::println("provoking the angle between a zero vector and the x-axis");
        static_cast<void>(std::fflush(stdout));
        const orb::Radians angle =
            orb::angleBetween(orb::Direction{zero, zero, zero}, orb::Direction{1.0, 0.0, 0.0});
        std::println("the guard did not fire: the angle came back as {}", angle.value());
    } catch (...) {
        return 1;
    }
    return 1;
#endif
}
