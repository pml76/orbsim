//
// [S1] Tests for the Kepler solver.
//
// The valuable check here is the one that does not share code with what it
// tests: the solver works with a residual and a slope and never references
// Kepler's equation as such, while the test evaluates E - e*sin(E) - M from
// scratch. A sign error in the solver cannot hide behind a test that repeats
// it, which is the whole reason this is worth more than a golden-value table.
//
#include "core/Units.hpp"
#include "core/Vec3.hpp"
#include "tests/TestHarness.hpp"

#include "orbit/Kepler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <print>
#include <random>
#include <ranges>
#include <type_traits>

namespace {

using namespace orbex;
using namespace orbex::literals;

// [S2] The compiler enforces the argument order, so the classic transposition
// is not a bug that can reach a test in the first place.
static_assert(!std::is_invocable_v<decltype(solveKepler), Eccentricity, Radians>,
              "solveKepler must not be callable with its arguments transposed");
static_assert(std::is_invocable_v<decltype(solveKepler), Radians, Eccentricity>,
              "...but must of course be callable correctly");

void testSatisfiesKeplersEquation(test::Run& run) {
    test::section("solutions satisfy M = E - e*sin(E)");

    // [S8] std::array, never a C array: it knows its own size.
    constexpr std::array kEccentricities{0.0, 0.1, 0.5, 0.9, 0.99, 0.999};

    for (const f64 e : kEccentricities) {
        f64 worstResidual = 0.0;

        // [S9] A view over the range, not a hand-written index loop.
        for (const int degree : std::views::iota(0, 360)) {
            const Radians meanAnomaly = toRadians(Degrees{static_cast<f64>(degree)});
            const auto solved = solveKepler(meanAnomaly, Eccentricity{e});

            if (!solved) {
                test::check(run, false, "solver failed for an in-range eccentricity");
                break;
            }

            const f64 bigE = solved->value;
            const f64 residual = bigE - (e * std::sin(bigE)) - wrapToPi(meanAnomaly).value;
            worstResidual = std::max(worstResidual, std::abs(wrapToPi(Radians{residual}).value));
        }

        // The documented guarantee in Kepler.hpp is 1e-13. This is that
        // guarantee, tested rather than trusted.
        test::check(run, worstResidual < 1e-13, "residual within the documented guarantee");
    }
}

// [S19] A seeded generator, with the seed written down. An unseeded or
// time-seeded RNG turns a reproducible test into a lottery, and a failure you
// cannot reproduce is a failure you cannot fix.
void testRandomisedOrbitsAllSolve(test::Run& run) {
    test::section("randomised orbits all solve");

    constexpr std::uint64_t kSeed = 0x5EED'0B17'C0DEULL;

    // [S19] A fixed seed is the whole point, so the random-seed checks (the
    // bugprone one and its two CERT aliases) are inverted here: they warn that
    // the sequence is predictable, and predictable is precisely what a
    // reproducible test requires. A failure you cannot reproduce is a failure
    // you cannot fix. This is the one suppression in the example, and this
    // comment is why it is allowed -- a suppression list without reasons is
    // how a lint config stops meaning anything.
    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::mt19937_64 generator{kSeed};
    std::uniform_real_distribution<f64> eccentricities{0.0, 0.9999};
    std::uniform_real_distribution<f64> anomalies{-10.0 * kPi, 10.0 * kPi};

    bool allSolved = true;
    f64 worstResidual = 0.0;

    for (int trial = 0; trial < 20000; ++trial) {
        const Eccentricity ecc{eccentricities(generator)};
        const Radians meanAnomaly{anomalies(generator)};

        const auto solved = solveKepler(meanAnomaly, ecc);
        if (!solved) {
            allSolved = false;
            break;
        }

        const f64 bigE = solved->value;
        const f64 residual = bigE - (ecc.value * std::sin(bigE)) - wrapToPi(meanAnomaly).value;
        worstResidual = std::max(worstResidual, std::abs(wrapToPi(Radians{residual}).value));
    }

    test::check(run, allSolved, "every randomised orbit converged");
    test::check(run, worstResidual < 1e-13, "every randomised residual within guarantee");
}

// [S20] The contract says failure is reported, never returned as a plausible
// number. A contract is worth exactly as much as its test.
void testFailuresAreReported(test::Run& run) {
    test::section("failures are reported, not approximated");

    const auto parabolic = solveKepler(1.0_rad, Eccentricity{1.0});
    test::check(run, !parabolic.has_value(), "eccentricity 1.0 is rejected");
    test::check(
        run, parabolic.error() == KeplerError::EccentricityOutOfRange, "with the specific error");

    const auto hyperbolic = solveKepler(1.0_rad, Eccentricity{1.4});
    test::check(run, !hyperbolic.has_value(), "eccentricity above 1 is rejected");

    const auto negative = solveKepler(1.0_rad, Eccentricity{-0.1});
    test::check(run, !negative.has_value(), "negative eccentricity is rejected");

    // [S11] The NaN case is the reason the preconditions are written as negated
    // comparisons. A naive `e < 0.0 || e >= 1.0` is false for NaN and would let
    // it straight through into the iteration.
    const auto notANumber =
        solveKepler(1.0_rad, Eccentricity{std::numeric_limits<f64>::quiet_NaN()});
    test::check(run, !notANumber.has_value(), "NaN eccentricity is rejected, not propagated");

    // [S7] Every error can be explained to a human.
    test::check(run, !describe(KeplerError::DidNotConverge).empty(), "errors describe themselves");
    test::check(run,
                !describe(KeplerError::EccentricityOutOfRange).empty(),
                "both errors describe themselves");
}

void testWrapping(test::Run& run) {
    test::section("angle wrapping");

    test::checkNear(run,
                    wrapToPi(Radians{kPi + 0.5}).value,
                    -kPi + 0.5,
                    Tolerance{1e-15},
                    "just past pi wraps negative");
    test::checkNear(run,
                    wrapToPi(Radians{-kPi - 0.5}).value,
                    kPi - 0.5,
                    Tolerance{1e-15},
                    "just past -pi wraps positive");
    test::checkNear(
        run, wrapToPi(Radians{0.25}).value, 0.25, Tolerance{0.0}, "an in-range angle is untouched");
}

} // namespace

// [S7] main is the one function nothing may escape from: an exception leaving
// it is std::terminate, with no message and no exit code worth reading.
// std::print can throw if stdout is closed, so it is caught here and turned
// into a diagnostic and a failing status -- the same "no error is silently
// ignored" rule the rest of the example follows, applied at the top.
//
// The handlers use std::fputs rather than std::print because a reporting path
// that can itself throw is not a reporting path.
int main() {
    try {
        std::print("orbex :: kepler\n\n");

        orbex::test::Run run;
        testSatisfiesKeplersEquation(run);
        testRandomisedOrbitsAllSolve(run);
        testFailuresAreReported(run);
        testWrapping(run);

        return orbex::test::report(run, "kepler");
    } catch (const std::exception& error) {
        // Discarded on purpose: if stderr is gone too there is nobody left to
        // tell, and the exit code still says "failed".
        static_cast<void>(std::fputs("unhandled exception: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 2;
    } catch (...) {
        static_cast<void>(std::fputs("unhandled exception of unknown type\n", stderr));
        return 2;
    }
}
