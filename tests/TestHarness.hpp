#pragma once
//
// A deliberately tiny test harness.
//
// It exists because a second test file needed the same checks as the first.
// Written for one file it would have been speculative; written for the second
// it is the abstraction the code asked for. When the suites grow past a
// handful of files, replace it with Catch2 or doctest -- then, not before
// (CODING_GUIDELINES.md section 17, and the Toolbox).
//
// There are no mutable globals here. The counters live in a struct that every
// check takes explicitly, because a mutable static has unspecified
// initialisation order across translation units and is a data race waiting
// for the day somebody adds a thread (section 18).
//
#include "core/Math.hpp"
#include "core/Units.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <exception>
#include <expected>
#include <print>
#include <string_view>

namespace orb::test {

struct Run {
    int checks{};
    int failures{};
};

inline void section(std::string_view name) { std::print("{}\n", name); }

inline void reportMismatch(std::string_view what, f64 got, f64 want, f64 tol) {
    std::print("  FAIL {}\n        got  {:.12g}\n        want {:.12g}  (tol {:g})\n",
               what,
               got,
               want,
               tol);
}

inline void check(Run& run, bool condition, std::string_view what) {
    ++run.checks;
    if (!condition) {
        ++run.failures;
        std::print("  FAIL {}\n", what);
    }
}

// The tolerance is a distinct type, so `checkNear(run, what, got, tol, want)`
// does not compile. This once took three adjacent f64 parameters that
// transposed in silence -- I.24 exactly, and the reason Tolerance exists.
inline void checkNear(Run& run, std::string_view what, f64 got, f64 want, Tolerance tol) {
    ++run.checks;
    if (!nearlyEqual(got, want, tol) || std::isnan(got)) {
        ++run.failures;
        reportMismatch(what, got, want, tol.value);
    }
}

// Relative comparison, for quantities whose magnitude spans many orders (radii
// in metres, speeds in m/s) where an absolute tolerance is meaningless.
inline void checkRel(Run& run, std::string_view what, f64 got, f64 want, Tolerance relTol) {
    ++run.checks;
    const f64 scale = std::max(std::abs(want), 1e-30);
    if (!nearlyEqual(got / scale, want / scale, relTol) || std::isnan(got)) {
        ++run.failures;
        reportMismatch(what, got, want, relTol.value * scale);
    }
}

inline void
checkVecRel(Run& run, std::string_view what, const Vec3& got, const Vec3& want, Tolerance relTol) {
    ++run.checks;
    const f64 scale = std::max(length(want), 1e-30);
    if (!(length(got - want) / scale <= relTol.value)) {
        ++run.failures;
        std::print("  FAIL {}\n        got  ({:.10g}, {:.10g}, {:.10g})\n"
                   "        want ({:.10g}, {:.10g}, {:.10g})\n        rel err {:g}\n",
                   what,
                   got.x,
                   got.y,
                   got.z,
                   want.x,
                   want.y,
                   want.z,
                   length(got - want) / scale);
    }
}

// Angles compare modulo a full turn: 0 and tau are the same angle.
inline void checkAngle(Run& run, std::string_view what, Radians got, Radians want, Tolerance tol) {
    ++run.checks;
    if (!(std::abs(wrapPi(got - want).value) <= tol.value)) {
        ++run.failures;
        reportMismatch(what, got.value, want.value, tol.value);
    }
}

// Unwraps an expected, counting a failure instead when it holds an error. Every
// orbital entry point can fail, and a test that ignored that would be testing
// nothing. `describe` is found by argument-dependent lookup on the error type.
template <typename T, typename Error>
[[nodiscard]] bool
expectOk(Run& run, const std::expected<T, Error>& result, std::string_view what) {
    ++run.checks;
    if (!result) {
        ++run.failures;
        std::print("  FAIL {}: {}\n", what, describe(result.error()));
        return false;
    }
    return true;
}

// Runs a suite and turns its outcome into a process exit code.
//
// This is the one function nothing may escape from: an exception leaving main
// is std::terminate, with no message and no exit code worth reading. std::print
// can throw if stdout is closed, so it is caught here and turned into a
// diagnostic and a failing status -- the "no error is silently ignored" rule
// the rest of the codebase follows, applied at the top.
//
// The handlers use std::fputs rather than std::print, because a reporting path
// that can itself throw is not a reporting path. Their results are discarded
// on purpose: if stderr is gone too there is nobody left to tell, and the exit
// code still says "failed".
template <std::invocable<Run&> Suite>
[[nodiscard]] int runSuite(std::string_view title, const Suite& suite) noexcept {
    try {
        std::print("orbsim :: {}\n\n", title);

        Run run;
        suite(run);

        std::print("\n{} checks, {} failures\n", run.checks, run.failures);
        return run.failures == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("unhandled exception: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 2;
    } catch (...) {
        static_cast<void>(std::fputs("unhandled exception of unknown type\n", stderr));
        return 2;
    }
}

} // namespace orb::test