//
// The coverage recorder's self-test program (M1-131, register decisions 628,
// 632, 634 and 636): a small program that enters known functions, and stops
// each way decision 634 has the recorder write its record on, run by
// scripts/check-coverage-recorder.py for the CTest test coverage_recorder.
//
// **Built three times.** `coverage_recorder_probe` carries the hook and the
// recorder, as every program of a hooked tree does; `coverage_recorder_unaligned`
// is the same with tests/coverage_recorder_unaligned.cpp beside it, whose flags
// have no entry in the address table, so that the recorder must find them out
// of line; and `coverage_recorder_probe_plain` is this file and
// src/core/Contract.cpp compiled once more with neither the hook nor the
// recorder, so that each way of stopping can be held to the exit code and the
// output the program has without the recorder's handlers (decision 636).
//
// **The first argument names the scenario**: `normally`, which returns from
// main, so that the static object below is destroyed and its destructor enters
// a function of its own; `file-stream`, which writes a small file and reads it
// back through the standard library's file streams before it returns the same
// way, as nearly every program of the project does before it ends (register
// decision 659); or a way of stopping -- `precondition`, a failed
// ORBSIM_EXPECTS (assertions live only); `noexcept`, an exception escaping a
// noexcept function; `crash`, a read through a null pointer; `library-check`,
// the debug standard library's own check (Debug only); `abort`;
// `bad-argument`, a bad argument to the C runtime; and `divide-by-zero` and
// `privileged-instruction`, the two other kinds of crash the recorder's
// handler acts on (register decision 696). Each
// enters `enteredFirst` and the function that stops, and would enter
// `enteredAfterTheStop` if it went on; `neverEntered` is entered by none.
//
// **Every function the check judges is in orb::coverage_probe**, so that it can
// tell them from the standard library's and the compiler's own; none may be
// inlined, so that each keeps a flag of its own; and each can return from its
// first block, without which the hook gives it no flag at all (decision 586):
// so each stop happens under a condition the compiler cannot see -- `count`,
// the number of arguments, which is 1 when the program is run as intended.
//
#include "core/Contract.hpp"
#include "tests/AbortProbe.hpp"

#include <bit>
#include <crtdbg.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <intrin.h>
#include <ios>
#include <memory>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// _itoa_s is declared in the Windows C runtime's <stdlib.h> itself, which
// misc-include-cleaner requires and modernize-deprecated-headers forbids: the
// one-line suppression tests/AbortBehaviour.cpp carries, for the same reason,
// by the owner's go-ahead (register decisions 155, 634 and 646).
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>

namespace orb::coverage_probe {
namespace {

[[clang::noinline]] void enteredFirst(std::string_view scenario) {
    std::println("scenario {}", scenario);
    static_cast<void>(std::fflush(stdout));
}

[[clang::noinline]] void enteredAfterTheStop(std::string_view scenario) {
    std::println("the scenario {} went on past its stop", scenario);
}

[[clang::noinline]] void neverEntered(std::string_view scenario) {
    std::println("no scenario is called {}", scenario);
}

[[clang::noinline]] void enteredByTheStaticDestructor() {
    std::println("the static object's destructor ran");
}

// It prints, so that it is still a call of its own where assertions are
// compiled out: without the print, the optimiser sees it return its argument
// and drops it (measured in RelWithDebInfo).
[[clang::noinline]] int requiresZero(int count) {
    std::println("requiring {} to be zero", count);
    ORBSIM_EXPECTS(count == 0);
    return count;
}

[[clang::noinline]] int throwsWhenPositive(int count) {
    if (count > 0) throw std::runtime_error("nothing catches this");
    return count;
}

// It promises not to throw and lets the exception escape, so that
// std::terminate ends the program: what this scenario exists to do, and what
// bugprone-exception-escape reports. Off for this function alone, by the
// owner's go-ahead (register decision 645).
// NOLINTNEXTLINE(bugprone-exception-escape)
[[clang::noinline]] int escapesNoexcept(int count) noexcept { return throwsWhenPositive(count); }

// The null pointer is made from `count` at run time, as the stop is chosen.
[[clang::noinline]] int readsThroughNull(int count) {
    const auto* const where =
        std::bit_cast<const volatile int*>(static_cast<std::uintptr_t>(count - 1));
    return *where;
}

[[clang::noinline]] int stepsBeforeBegin(int count) {
    const std::vector<int> values(static_cast<std::size_t>(count), count);
    auto before = values.begin();
    --before;
    return *before;
}

[[clang::noinline]] int abortsWhenPositive(int count) {
    if (count > 0) std::abort();
    return count;
}

// No buffer to write the digits into: the C runtime checks its arguments and,
// finding one invalid, calls its invalid-parameter handler -- the case decision
// 634 measured (decision 646).
[[clang::noinline]] int convertsIntoNoBuffer(int count) { return _itoa_s(count, nullptr, 0, 10); }

// The divisor is made from `count` at run time, as the null pointer above is:
// the processor refuses an integer division by zero, and Windows reports it
// as a crash of its own kind (decision 696, measured 2026-10-19: 0xC0000094).
// The quotient is kept in a volatile, so that the division must be done: no
// caller uses the result, and RelWithDebInfo's optimiser removed the division
// and the call where it was returned alone (measured 2026-10-19).
[[clang::noinline]] int dividesByZero(int count) {
    const volatile int quotient = count / (count - 1);
    return quotient;
}

// HLT, an instruction that only the operating system may run: in a program
// the processor refuses it as a privileged instruction (decision 696, measured
// 2026-10-19: 0xC0000096).
[[clang::noinline]] int runsAPrivilegedInstruction(int count) {
    if (count > 0) __halt();
    return count;
}

// A file written and read back, in the working folder, which the check makes a
// scratch one. Found 2026-10-18 (decision 659): the recorder's writer runs
// after the standard library has torn down what its file streams need, and its
// own stream then failed in any program that had used one -- a crash as the
// program ended, and an empty record -- which no scenario here had used.
[[clang::noinline]] int usesAFileStream(int count) {
    const std::string written = std::to_string(count);
    {
        std::ofstream out("coverage_recorder_stream.txt", std::ios::binary);
        out << written;
        if (!out) return 1;
    }
    std::ifstream in("coverage_recorder_stream.txt", std::ios::binary);
    std::string read;
    in >> read;
    std::println("read back {}", read);
    return read == written ? 0 : 1;
}

[[clang::noinline]] int stopAs(std::string_view scenario, int count) {
    if (scenario == "precondition") return requiresZero(count);
    if (scenario == "noexcept") return escapesNoexcept(count);
    if (scenario == "crash") return readsThroughNull(count);
    if (scenario == "library-check") return stepsBeforeBegin(count);
    if (scenario == "abort") return abortsWhenPositive(count);
    if (scenario == "bad-argument") return convertsIntoNoBuffer(count);
    if (scenario == "divide-by-zero") return dividesByZero(count);
    if (scenario == "privileged-instruction") return runsAPrivilegedInstruction(count);
    neverEntered(scenario);
    return count;
}

[[clang::noinline]] int runScenario(std::string_view scenario, int count) {
    enteredFirst(scenario);
    if (scenario == "normally") return 0;
    if (scenario == "file-stream") return usesAFileStream(count);
    static_cast<void>(stopAs(scenario, count));
    enteredAfterTheStop(scenario);
    return 1;
}

// The static object, destroyed after main returns: a pointer whose deleter
// enters a function of the probe's own, the Rule of Zero kept (Non-negotiable
// 5) -- the deleter is called with the constant it holds and frees nothing.
struct EnterAtExit {
    void operator()(const int* /*held*/) const { enteredByTheStaticDestructor(); }
};
constexpr int kHeld = 0;
// A global object with a destructor is what "done when" asks for, and both
// warnings refuse one: off at this declaration alone, by the owner's
// go-ahead (register decision 628, Non-negotiable 10).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
const std::unique_ptr<const int, EnterAtExit> kEnteredAtExit{&kHeld};
#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace
} // namespace orb::coverage_probe

// The arguments are the C runtime's interface: a pointer and a count, and a
// span of the two is the only way to give them bounds, which is the
// construction -Wunsafe-buffer-usage-in-container reports. Off for this
// function alone, as src/app/main.cpp and tests/assertion_message_probe.cpp
// have it (ADR 0017, register decision 647).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
int main(int argc, char** argv) {
    // Before anything else: a stop that asks a question instead of ending
    // waits for somebody to answer it. Debug's abort opens a dialog unless
    // tests/AbortProbe.hpp's call clears it, and the debug library's check, and
    // the C runtime's own assertion before a bad argument, report through the
    // debug report, sent to stderr here as tests/AbortBehaviour.cpp sends it.
    // No-ops where those do not exist.
    orb::probe::abortWithoutWaiting();
    static_cast<void>(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE));
    static_cast<void>(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR));
    static_cast<void>(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE));
    static_cast<void>(_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR));
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const std::string_view scenario =
        args.size() == 2 ? std::string_view{args.back()} : std::string_view{};
    try {
        return orb::coverage_probe::runScenario(scenario, static_cast<int>(args.size()) - 1);
    } catch (...) {
        return 2;
    }
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
