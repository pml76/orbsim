//
// The radiometric chain, measured (M1-18; ADR 0014; register decisions
// 256-271): the lambert probes' read-back HDR frames held to the radiance a
// Lambertian patch must have.
//
// **The pattern every numeric probe copies.** The GPU work stays in the
// application and the arithmetic in a test that links no Vulkan:
//
//   * each probe is a CTest test that runs `orbsim --probe <name>` under the
//     validation layers and writes `<name>.hdr.f32` into the build tree
//     (cmake/RunProbe.cmake), declared `FIXTURES_SETUP probe_data`;
//   * this suite is an ordinary Catch2 program whose cases are declared
//     `FIXTURES_REQUIRED probe_data`, so CTest runs the probes first -- and
//     runs them again if only this suite is asked for -- and reads the dumps
//     with tests/HdrDumpFile.hpp.
//
// A later probe adds its CTest test to the fixture and its cases here, or in
// a suite of its own wired the same way (CMakeLists.txt, beside these).
//
// **The expected radiance is computed here, from the definition**:
//
//     L = albedo * E * cos(theta) / pi
//
// with albedo 0.3, theta the angle between the patch's normal and the
// direction to the Sun, and E the total solar irradiance at the Sun's
// distance: **1361 W/m^2 at 1 AU** -- Kopp & Lean (2011), Geophys. Res. Lett.
// 38, L01706, 1360.8 +/- 0.5 W/m^2, carried rounded -- falling as the inverse
// square. At 1 AU and normal incidence that is 0.3 * 1361 / pi = 129.9659...
// W/(m^2 sr). Nothing is taken from src/ (VERIFICATION.md rule 2), so the
// rounding of a constant there can never be what an assertion here uses.
//
// **The budget: 0.5 % of the analytic radiance, at every pixel of every
// frame** (ADR 0014; register decision 259 widened the task's five points to
// the whole frame). **The quantisation floor is stated and checked as
// well**: the HDR target is RGBA16F, whose step is 2^-11 to 2^-10 of a value,
// and the Vulkan specification leaves the rounding of a write to it
// undefined -- this machine's GPU rounds toward zero (M1-16) -- so each value
// must also be one of the two binary16 values either side of the exact
// radiance: up to 0.1 % away, five times inside the budget, so the 0.5 %
// tests the chain and not the format. What comes before the rounding is
// binary32 arithmetic in lambert.frag -- three multiplications and a
// division, each within 2.5 units in the last place by the Vulkan precision
// rules, about 3e-7 of the value -- and the narrowing of each input, 6e-8
// each: far inside a binary16 step, so a value off its two neighbours is a
// defect in the chain.
//
// **What each probe is for.** `lambert` is the budget. `lambert-half-au` and
// `lambert-two-au` are the inverse-square law on the GPU path, 4x and 1/4x:
// a hard-coded irradiance passes the first and fails these. `lambert-tilted-60`
// is the cosine, half. `lambert-exposure` is the architecture in one claim:
// its HDR dump is byte for byte `lambert`'s, and only its picture differs.
//
#include "HdrDumpFile.hpp"
#include "core/Scalar.hpp"
#include "view/ImageCompare.hpp"
#include "view/ImageFiles.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace orb;
using namespace orb::test;

namespace {

// --- the definitions, written out here ------------------------------------

// Total solar irradiance at 1 AU, W/m^2: Kopp & Lean (2011), above.
constexpr f64 kSolarIrradianceAtOneAu = 1361.0;
// The patch's albedo, the task's 0.3.
constexpr f64 kAlbedo = 0.3;

// The radiance of the patch with the Sun `distanceInAu` away and `theta` off
// its normal, W/(m^2 sr). The two parameters are named in a struct so they
// cannot be given the wrong way round (non-negotiable 1).
struct Light {
    f64 distanceInAu{};
    f64 thetaRadians{};
};

[[nodiscard]] f64 lambertRadiance(Light light) {
    const f64 irradiance = kSolarIrradianceAtOneAu / (light.distanceInAu * light.distanceInAu);
    return kAlbedo * irradiance * std::cos(light.thetaRadians) / std::numbers::pi;
}

// 0.5 % (ADR 0014), as a fraction.
constexpr f64 kBudget = 0.005;

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;

// The two binary16 values either side of a positive `value` in binary16's
// normal range -- the same value twice if it is one -- exactly as
// tests/test_probe_clear.cpp computes them: a step at exponent e is
// 2^(e - 10), and dividing by a power of two and flooring are exact.
struct Neighbours {
    f64 below{};
    f64 above{};
};

[[nodiscard]] Neighbours halfNeighbours(f64 value) {
    const f64 step = std::exp2(std::floor(std::log2(value)) - 10.0);
    const f64 below = std::floor(value / step) * step;
    return {.below = below, .above = bitsOf(below) == bitsOf(value) ? value : below + step};
}

[[nodiscard]] std::filesystem::path probeFile(std::string_view name) {
    return std::filesystem::path{ORBSIM_PROBE_DIR} / std::string(name);
}

[[nodiscard]] HdrDump readDump(std::string_view probe) {
    const std::filesystem::path path = probeFile(std::string(probe) + ".hdr.f32");
    auto dump = readHdrDump(path);
    INFO("reading " << path.string() << ": " << (dump.has_value() ? "ok" : describe(dump.error())));
    REQUIRE(dump.has_value());
    REQUIRE(dump->width() == kWidth);
    REQUIRE(dump->height() == kHeight);
    return *std::move(dump);
}

[[nodiscard]] std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    INFO("reading " << path.string());
    REQUIRE(file.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        bytes.at(i) = static_cast<std::byte>(chars.at(i));
    }
    return bytes;
}

using Channel = HdrDump::Channel;

// One read-back value: within the budget of `want`, and on one of its two
// binary16 neighbours.
// The two numbers named, so that they cannot be given the wrong way round
// (non-negotiable 1).
struct Reading {
    f64 got{};
    f64 want{};
};

[[nodiscard]] bool isAllowed(Reading reading, const Neighbours& allowed) {
    const bool withinBudget = std::abs((reading.got / reading.want) - 1.0) <= kBudget;
    const bool onNeighbour = bitsOf(reading.got) == bitsOf(allowed.below) ||
                             bitsOf(reading.got) == bitsOf(allowed.above);
    return withinBudget && onNeighbour;
}

// The first pixel of the frame whose R, G or B is not allowed, or whose alpha
// is not one -- or none.
struct FirstFailure {
    bool found{};
    Pixel pixel;
    f64 value{};
    int channel{}; // 0, 1, 2 for R, G, B; 3 for alpha
};

[[nodiscard]] FirstFailure firstFailure(const HdrDump& dump, f64 want) {
    const Neighbours allowed = halfNeighbours(want);
    for (std::uint32_t row = 0; row < kHeight; ++row) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const Pixel pixel{.column = column, .row = row};
            int index = 0;
            for (const Channel channel : {Channel::Red, Channel::Green, Channel::Blue}) {
                // Exact: every binary32 value is a binary64 value.
                const auto got = static_cast<f64>(dump.at(pixel, channel));
                if (!isAllowed({.got = got, .want = want}, allowed)) {
                    return {.found = true, .pixel = pixel, .value = got, .channel = index};
                }
                ++index;
            }
            const f32 alpha = dump.at(pixel, Channel::Alpha);
            if (bitsOf(alpha) != bitsOf(1.0F)) {
                return {
                    .found = true,
                    .pixel = pixel,
                    .value = static_cast<f64>(alpha),
                    .channel = 3,
                };
            }
        }
    }
    return {};
}

// Every pixel of the frame: R, G and B within the budget of `want` and on one
// of its two binary16 neighbours, and alpha one. Names the first that fails.
void requireEveryPixel(const HdrDump& dump, f64 want) {
    const FirstFailure failure = firstFailure(dump, want);
    const Neighbours allowed = halfNeighbours(want);
    INFO("row " << failure.pixel.row << ", column " << failure.pixel.column << ", channel "
                << failure.channel << " (3 is alpha): got " << failure.value << ", want " << want
                << " (" << ((failure.value / want) - 1.0) * 100.0 << " %), binary16 neighbours "
                << allowed.below << " and " << allowed.above);
    REQUIRE(!failure.found);
}

// The first value at which the picture at f/8 is not brighter than at f/16,
// or the count of values if there is none.
// The two pictures by their exposures, so that they cannot be given the wrong
// way round. Observers: the caller keeps both alive.
struct ByExposure {
    const view::Rgb8Image* atF16{};
    const view::Rgb8Image* atF8{};
};

[[nodiscard]] std::size_t firstNotBrighter(ByExposure pictures) {
    const view::Rgb8Image& atF16 = *pictures.atF16;
    const view::Rgb8Image& atF8 = *pictures.atF8;
    for (std::size_t i = 0; i < atF16.valueCount(); ++i) {
        if (atF8.value(i) <= atF16.value(i)) return i;
    }
    return atF16.valueCount();
}

} // namespace

TEST_CASE("the patch at 1 AU reads back 0.3 x 1361 / pi, within 0.5 %, at every pixel") {
    const f64 want = lambertRadiance({.distanceInAu = 1.0, .thetaRadians = 0.0});
    // The number the task quotes, held so that the definition above cannot
    // drift from it: 129.9659... W/(m^2 sr).
    REQUIRE(std::abs(want - 129.9659) < 1e-4);
    requireEveryPixel(readDump("lambert"), want);
}

TEST_CASE("the quantisation floor is the format's, five times inside the budget") {
    // The two binary16 neighbours of 129.966 are 129.875 and 130.0, a step of
    // 2^-3: 0.07 % below and 0.03 % above. Whichever the GPU writes, the
    // budget is testing the chain, not the format.
    const f64 want = lambertRadiance({.distanceInAu = 1.0, .thetaRadians = 0.0});
    const Neighbours allowed = halfNeighbours(want);
    REQUIRE(bitsOf(allowed.below) == bitsOf(129.875));
    REQUIRE(bitsOf(allowed.above) == bitsOf(130.0));
    REQUIRE((allowed.above - allowed.below) / want < kBudget / 5.0);
}

TEST_CASE("the inverse-square law holds on the GPU path: 4x at 0.5 AU, 1/4 at 2 AU") {
    const f64 atOneAu = lambertRadiance({.distanceInAu = 1.0, .thetaRadians = 0.0});
    const f64 atHalf = lambertRadiance({.distanceInAu = 0.5, .thetaRadians = 0.0});
    const f64 atTwo = lambertRadiance({.distanceInAu = 2.0, .thetaRadians = 0.0});
    // The law itself, before the frames: exact, since 0.5 and 2 square to
    // powers of two.
    REQUIRE(bitsOf(atHalf) == bitsOf(4.0 * atOneAu));
    REQUIRE(bitsOf(atTwo) == bitsOf(0.25 * atOneAu));
    requireEveryPixel(readDump("lambert-half-au"), atHalf);
    requireEveryPixel(readDump("lambert-two-au"), atTwo);
}

TEST_CASE("the patch tilted 60 degrees from the Sun reads back half the radiance") {
    const f64 want = lambertRadiance({.distanceInAu = 1.0, .thetaRadians = std::numbers::pi / 3.0});
    REQUIRE(std::abs((want / lambertRadiance({.distanceInAu = 1.0, .thetaRadians = 0.0})) - 0.5) <
            1e-15);
    requireEveryPixel(readDump("lambert-tilted-60"), want);
}

TEST_CASE("exposure does not touch the HDR frame, and only the picture differs") {
    // The whole architecture of the chain, asserted once (ADR 0014): two
    // runs of the same scene at f/16 and f/8 write the same HDR dump, byte
    // for byte, and different pictures.
    const std::vector<std::byte> atF16 = readBytes(probeFile("lambert.hdr.f32"));
    const std::vector<std::byte> atF8 = readBytes(probeFile("lambert-exposure.hdr.f32"));
    REQUIRE(atF16.size() == atF8.size());
    REQUIRE(atF16 == atF8);

    const auto pictureAtF16 = view::decodePng8(readBytes(probeFile("lambert.png")));
    const auto pictureAtF8 = view::decodePng8(readBytes(probeFile("lambert-exposure.png")));
    REQUIRE(pictureAtF16.has_value());
    REQUIRE(pictureAtF8.has_value());
    REQUIRE(pictureAtF16->valueCount() == pictureAtF8->valueCount());
    // Two stops brighter at every value: not merely different somewhere.
    const std::size_t at = firstNotBrighter({.atF16 = &*pictureAtF16, .atF8 = &*pictureAtF8});
    INFO("the first value not brighter at f/8 is value " << at << " of "
                                                         << pictureAtF16->valueCount());
    REQUIRE(at == pictureAtF16->valueCount());
}
