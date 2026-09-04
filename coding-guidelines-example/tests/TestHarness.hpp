#pragma once
//
// A deliberately tiny test harness.
//
// [S17] This exists because three test files need it. Written for one, it would
// have been speculative generality; written for the third, it is the right
// abstraction arriving at the point where it is actually shaped by a
// requirement. When these tests grow past a handful of files, replace it with
// Catch2 or doctest -- then, not before.
//
// [S18] There are no mutable globals here. The counters live in a struct that
// callers pass explicitly, because a mutable static has unspecified
// initialization order across translation units and is a data race waiting for
// the day somebody adds a thread.
//
#include "core/Vec3.hpp"

#include <print>
#include <string_view>

namespace orbex::test {

struct Run {
    int checks{};
    int failures{};
};

inline void check(Run& run, bool condition, std::string_view what) {
    ++run.checks;
    if (!condition) {
        ++run.failures;
        // [S8] '\n', never std::endl. endl flushes, and you did not ask it to.
        std::print("  FAIL {}\n", what);
    }
}

// [S11] Approximate comparison goes through nearlyEqual, and the tolerance is a
// strong type, so `checkNear(run, got, tolerance, want)` will not compile.
inline void checkNear(Run& run, f64 got, f64 want, Tolerance tolerance, std::string_view what) {
    const bool ok = nearlyEqual(got, want, tolerance);
    if (!ok) {
        std::print("  (got {:.17g}, want {:.17g}, tol {:g})\n", got, want, tolerance.value);
    }
    check(run, ok, what);
}

inline void section(std::string_view name) { std::print("{}\n", name); }

[[nodiscard]] inline int report(const Run& run, std::string_view suite) {
    std::print("\n{}: {} checks, {} failures\n", suite, run.checks, run.failures);
    return run.failures == 0 ? 0 : 1;
}

} // namespace orbex::test
