//
// [S10] "Measure. Do not guess." -- so here is the measuring.
//
// This is deliberately NOT a CTest test. A timing threshold on shared CI
// hardware fails for reasons that have nothing to do with your code, and a test
// that cries wolf gets disabled, and then it is not a test. This prints
// numbers; a human or a trend-tracking job decides what they mean.
//
// It is also not a substitute for a profiler. It answers "did that change make
// it slower", which is the question you can answer cheaply and often.
//
#include "orbit/OrbitPath.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <print>

namespace {

using namespace orbex;
using namespace orbex::literals;

constexpr GravParam kMuEarth{3.986004418e14};

[[nodiscard]] Elements benchmarkOrbit() noexcept {
    return Elements{.semiMajorAxis = Metres{8378137.0},
                    .eccentricity = Eccentricity{0.35},
                    .inclination = toRadians(28.5_deg),
                    .ascendingNode = toRadians(120.0_deg),
                    .periapsisArgument = toRadians(45.0_deg)};
}

struct Timing {
    double nanosecondsPerSample{};
    std::size_t pointsProduced{};
};

[[nodiscard]] Timing measure(Spacing spacing, int repetitions) {
    const Elements elements = benchmarkOrbit();
    const PathOptions options{.samples = SampleCount{std::size_t{1024}},
                              .spacing = spacing,
                              .closure = PathClosure::ClosedLoop};

    // One untimed run first, so the measurement is not dominated by the first
    // allocation and by cold instruction cache.
    const auto warmup = OrbitPath::sample(elements, kMuEarth, options);
    if (!warmup) return {};

    std::size_t produced = 0;
    const auto start = std::chrono::steady_clock::now();

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        const auto path = OrbitPath::sample(elements, kMuEarth, options);
        // Accumulating the size keeps the optimizer from deleting the call
        // outright, without adding meaningful work to the measurement.
        if (path) produced += path->size();
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    if (produced == 0) return {};
    return Timing{.nanosecondsPerSample =
                      static_cast<double>(nanoseconds) / static_cast<double>(produced),
                  .pointsProduced = produced};
}

} // namespace

// [S7] Nothing escapes main; see the test executables for the same guard.
int main() {
    try {
        std::print("orbex :: orbit path benchmark\n\n");

        constexpr int kRepetitions = 2000;

        const Timing byAngle = measure(Spacing::UniformInAngle, kRepetitions);
        const Timing byTime = measure(Spacing::UniformInTime, kRepetitions);

        std::print("  uniform in angle : {:7.2f} ns/point  ({} points)\n",
                   byAngle.nanosecondsPerSample,
                   byAngle.pointsProduced);
        std::print("  uniform in time  : {:7.2f} ns/point  ({} points)\n",
                   byTime.nanosecondsPerSample,
                   byTime.pointsProduced);

        // The interesting number is the ratio: uniform-in-time additionally runs
        // a Newton solve per point, so it should cost noticeably more. If it
        // ever stops doing so, either the solver stopped iterating or the
        // compiler hoisted something it should not have -- both worth knowing.
        if (byAngle.nanosecondsPerSample > 0.0) {
            std::print("\n  time / angle     : {:7.2f}x\n",
                       byTime.nanosecondsPerSample / byAngle.nanosecondsPerSample);
        }

        return 0;
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
