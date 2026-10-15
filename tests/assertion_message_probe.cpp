//
// What a failed precondition or postcondition says (M1-129, register decisions
// 549, 556 and 557): the condition, the function and the file -- none of which
// moves when a line is added above it -- and the chain of calls, read from the
// debug information, which names the line. The code itself carries no line
// number, so an edit does not change every function below it (M1-129, item 9).
//
// A program rather than a Catch2 case, for the reason
// tests/angle_between_zero_probe.cpp gives: an assertion that fires ends the
// process. Judged by cmake/VerifyAssertionMessage.cmake, which finds the line
// each mark below sits on in this file's own text, rather than trusting the
// program to report it.
//
// `precondition` as the argument trips ORBSIM_EXPECTS, `postcondition` trips
// ORBSIM_ENSURES.
//
#include <cstddef>
#include <cstdio>
#include <span>

#ifndef NDEBUG
#include "core/Contract.hpp"
#include "tests/AbortProbe.hpp"

#include <print>
#include <string_view>

namespace {

// `count` is 1 when the probe is run as intended; the compiler cannot see that.
int provokedPrecondition(int count) {
    ORBSIM_EXPECTS(count == 0); // assertion-message-probe: the precondition's line
    return count;
}

int provokedPostcondition(int count) {
    const int doubled = 2 * count;
    ORBSIM_ENSURES(doubled == 0); // assertion-message-probe: the postcondition's line
    return doubled;
}

} // namespace
#endif

// The arguments are the C runtime's interface: a pointer and a count, and a
// span of the two is the only way to give them bounds, which is the
// construction -Wunsafe-buffer-usage-in-container reports. Off for this
// function alone, as src/app/main.cpp has it (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
int main(int argc, char** argv) {
    // The first argument names which one. A span, as src/app/main.cpp reads
    // its arguments, in both configurations.
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
#ifdef NDEBUG
    static_cast<void>(args);
    static_cast<void>(std::puts("SKIPPED: assertions are not live in this configuration, so "
                                "there is no message to check"));
    return 0;
#else
    orb::probe::abortWithoutWaiting();
    try {
        const std::string_view which =
            args.size() == 2 ? std::string_view{args.back()} : std::string_view{};
        std::println("provoking the {}", which);
        static_cast<void>(std::fflush(stdout));
        const int count = static_cast<int>(args.size()) - 1;
        if (which == "precondition") {
            static_cast<void>(provokedPrecondition(count));
        } else if (which == "postcondition") {
            static_cast<void>(provokedPostcondition(count));
        } else {
            std::println("name precondition or postcondition");
            return 2;
        }
        std::println("the guard did not fire");
    } catch (...) {
        return 1;
    }
    return 1;
#endif
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
