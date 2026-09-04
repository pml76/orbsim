//
// Tests for the orbit path sampler and the GPU boundary.
//
#include "tests/TestHarness.hpp"

#include "orbit/OrbitPath.hpp"
#include "render/PathUpload.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <numeric>
#include <optional>
#include <ranges>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace orbex;
using namespace orbex::literals;

// [S16] Earth, EGM-96 and WGS-84. Named so a reader can check them against a
// reference rather than wondering where the digits came from.
constexpr GravParam kMuEarth{3.986004418e14};
constexpr Metres kEarthRadius{6378137.0};

// [S4] The Rule of Zero claim about OrbitPath, checked rather than asserted.
static_assert(std::is_nothrow_move_constructible_v<OrbitPath>,
              "the compiler-generated move must exist and be cheap");
static_assert(std::is_copy_constructible_v<OrbitPath>, "the compiler-generated copy must exist");
static_assert(std::is_destructible_v<OrbitPath>);

[[nodiscard]] Elements testOrbit(Metres semiMajorAxis, Eccentricity eccentricity) noexcept {
    // [S6] Designated initializers: every field named at the point of use, and
    // no chance of filling in inclination where the node belongs.
    return Elements{.semiMajorAxis = semiMajorAxis,
                    .eccentricity = eccentricity,
                    .inclination = toRadians(28.5_deg),
                    .ascendingNode = toRadians(120.0_deg),
                    .periapsisArgument = toRadians(45.0_deg)};
}

void testGeometry(test::Run& run) {
    test::section("sampled points lie on the orbit");

    const Elements elements = testOrbit(Metres{kEarthRadius.value + 2000e3}, Eccentricity{0.35});
    const auto path = OrbitPath::sample(elements,
                                        kMuEarth,
                                        PathOptions{.samples = SampleCount{std::size_t{128}},
                                                    .spacing = Spacing::UniformInAngle,
                                                    .closure = PathClosure::ClosedLoop});

    test::check(run, path.has_value(), "sampling a closed orbit succeeds");
    if (!path) return; // [S21] an early return; NR.2

    test::check(run, path->size() == 129, "ClosedLoop yields samples + 1 points");

    const f64 e = elements.eccentricity.value;
    const f64 periapsis = elements.semiMajorAxis.value * (1.0 - e);
    const f64 apoapsis = elements.semiMajorAxis.value * (1.0 + e);

    // [S9] An algorithm states the intent; an index loop would only imply it.
    const bool onOrbit = std::ranges::all_of(path->points(), [&](const Vec3& point) {
        const f64 radius = length(point);
        return radius >= periapsis * (1.0 - 1e-9) && radius <= apoapsis * (1.0 + 1e-9);
    });
    test::check(run, onOrbit, "every sample lies between periapsis and apoapsis");

    const Vec3 gap = path->points().front() - path->points().back();
    test::check(run, length(gap) < periapsis * 1e-9, "the loop closes");

    // A 2000 km orbit takes a little over two hours. Anchoring against a number
    // a reader can sanity-check by hand is worth more than a golden value.
    test::check(run,
                path->period().value > 7000.0 && path->period().value < 8000.0,
                "period is physically plausible");
}

void testSpacingModesDiffer(test::Run& run) {
    test::section("time and angle spacing genuinely differ");

    const Elements elements = testOrbit(Metres{kEarthRadius.value + 5000e3}, Eccentricity{0.6});
    constexpr PathOptions kByTime{.samples = SampleCount{std::size_t{64}},
                                  .spacing = Spacing::UniformInTime,
                                  .closure = PathClosure::OpenEnded};
    constexpr PathOptions kByAngle{.samples = SampleCount{std::size_t{64}},
                                   .spacing = Spacing::UniformInAngle,
                                   .closure = PathClosure::OpenEnded};

    const auto byTime = OrbitPath::sample(elements, kMuEarth, kByTime);
    const auto byAngle = OrbitPath::sample(elements, kMuEarth, kByAngle);

    test::check(run, byTime.has_value() && byAngle.has_value(), "both spacings succeed");
    if (!byTime || !byAngle) return;

    test::check(run, byTime->size() == 64, "OpenEnded yields exactly samples");

    // A spacecraft loiters near apoapsis, so equal steps of time must bunch
    // points out there. If these two modes agreed, one of them would be broken.
    //
    // [S9] Reduced over the whole path rather than probing one index: an
    // algorithm cannot read past the end, and a hand-picked index would only
    // prove the two differ *there*.
    const f64 largestSeparation = std::transform_reduce(
        byTime->points().begin(),
        byTime->points().end(),
        byAngle->points().begin(),
        0.0,
        [](f64 a, f64 b) { return std::max(a, b); },
        [](const Vec3& a, const Vec3& b) { return length(a - b); });
    test::check(run, largestSeparation > 1.0, "the two spacings place points differently");
}

void testFailuresAreReported(test::Run& run) {
    test::section("bad requests are refused, not approximated");

    const auto escape = OrbitPath::sample(testOrbit(Metres{8000e3}, Eccentricity{1.4}), kMuEarth);
    test::check(run,
                !escape.has_value() && escape.error() == PathError::NotAClosedOrbit,
                "a hyperbolic orbit has no closed path");

    const auto massless =
        OrbitPath::sample(testOrbit(Metres{7000e3}, Eccentricity{0.1}), GravParam{0.0});
    test::check(run,
                !massless.has_value() && massless.error() == PathError::NonPositiveGravity,
                "a massless central body is refused");

    const auto degenerate = OrbitPath::sample(testOrbit(Metres{0.0}, Eccentricity{0.1}), kMuEarth);
    test::check(run,
                !degenerate.has_value() &&
                    degenerate.error() == PathError::NonPositiveSemiMajorAxis,
                "a zero semi-major axis is refused");

    const auto tooFew = OrbitPath::sample(testOrbit(Metres{7000e3}, Eccentricity{0.1}),
                                          kMuEarth,
                                          PathOptions{.samples = SampleCount{std::size_t{2}}});
    test::check(run,
                !tooFew.has_value() && tooFew.error() == PathError::TooFewSamples,
                "two points is not a shape");

    test::check(run, !describe(PathError::SolverFailed).empty(), "errors describe themselves");
}

// [S19] A simulator promises that resuming a saved scenario continues the same
// flight. That promise is worth nothing untested.
void testDeterminism(test::Run& run) {
    test::section("determinism");

    const Elements elements = testOrbit(Metres{kEarthRadius.value + 800e3}, Eccentricity{0.2});
    constexpr PathOptions kOptions{.samples = SampleCount{std::size_t{256}},
                                   .spacing = Spacing::UniformInTime,
                                   .closure = PathClosure::ClosedLoop};

    const auto first = OrbitPath::sample(elements, kMuEarth, kOptions);
    const auto second = OrbitPath::sample(elements, kMuEarth, kOptions);

    test::check(run, first.has_value() && second.has_value(), "both runs succeed");
    if (!first || !second) return;

    // [S11][S19] The one place where comparing doubles with == is not merely
    // permitted but the entire point. The claim is bit-identical reproduction,
    // and any tolerance at all would conceal exactly the drift being tested
    // for. Section 11 forbids *approximate* comparison spelled `==`; this is
    // not that.
    test::check(run, *first == *second, "identical inputs give bit-identical output");
}

// [S13] Two paths sampled concurrently. There is no mutex anywhere, because
// nothing is shared: each worker owns its inputs and its result, and the
// sampler is pure. CP.3 (minimize sharing) and CP.4 (think in tasks), rather
// than reaching for a lock.
void testConcurrentSamplingNeedsNoLocks(test::Run& run) {
    test::section("concurrent sampling needs no locks");

    std::optional<OrbitPath> low;
    std::optional<OrbitPath> geostationary;

    {
        // [S13] std::jthread, never std::thread: it joins in its destructor, so
        // the closing brace below is the join. A std::thread you forget to join
        // calls std::terminate, which is a rude way to find out.
        const std::jthread lowWorker{[&low] {
            auto sampled =
                OrbitPath::sample(testOrbit(Metres{7000e3}, Eccentricity{0.05}), kMuEarth);
            if (sampled) low = std::move(*sampled);
        }};
        const std::jthread highWorker{[&geostationary] {
            auto sampled =
                OrbitPath::sample(testOrbit(Metres{42164e3}, Eccentricity{0.001}), kMuEarth);
            if (sampled) geostationary = std::move(*sampled);
        }};
    } // both workers joined here, by destructor

    test::check(run, low.has_value() && geostationary.has_value(), "both workers produced a path");
    if (!low || !geostationary) return;

    // Geostationary period is about 24 hours, low Earth orbit about 97 minutes.
    // If these matched, the two workers had trampled each other.
    test::check(run,
                geostationary->period().value > low->period().value * 10.0,
                "the results are independent and each is correct");
}

void testCameraRelativeUpload(test::Run& run) {
    test::section("the f64 -> f32 boundary");

    const auto path = OrbitPath::sample(
        testOrbit(Metres{kEarthRadius.value + 400e3}, Eccentricity{0.01}), kMuEarth);
    test::check(run, path.has_value(), "sampling succeeds");
    if (!path) return;

    const Vec3 camera = path->points().front();
    const std::vector<gfx::PathVertex> vertices = gfx::toCameraRelative(path->points(), camera);

    test::check(run, vertices.size() == path->size(), "one vertex per point");

    // The camera sits exactly on the first point, so that vertex must land on
    // the origin. Subtracting in f64 makes this exact; narrowing first would
    // leave a residue of tens of metres.
    test::check(run, vertices.front() == gfx::PathVertex{}, "the point under the camera is exact");

    // Camera-relative magnitudes must stay in the range where an f32 still has
    // sub-metre resolution -- which is the entire reason for the subtraction.
    const bool withinFloatComfort = std::ranges::all_of(vertices, [](const gfx::PathVertex& v) {
        return std::abs(v.x) < 2.0e7F && std::abs(v.y) < 2.0e7F && std::abs(v.z) < 2.0e7F;
    });
    test::check(run, withinFloatComfort, "camera-relative values stay in f32's comfortable range");
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
        std::print("orbex :: orbit path\n\n");

        orbex::test::Run run;
        testGeometry(run);
        testSpacingModesDiffer(run);
        testFailuresAreReported(run);
        testDeterminism(run);
        testConcurrentSamplingNeedsNoLocks(run);
        testCameraRelativeUpload(run);

        return orbex::test::report(run, "orbit path");
    } catch (const std::exception& error) {
        std::fputs("unhandled exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputs("\n", stderr);
        return 2;
    } catch (...) {
        std::fputs("unhandled exception of unknown type\n", stderr);
        return 2;
    }
}
