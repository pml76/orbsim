//
// The `clear` probe's frame, read back and checked number by number (M1-16,
// register decision 194).
//
// **The frame comes from the GPU; the expected values come from here.** The
// CTest test probe_clear renders the probe and writes clear.hdr.f32; this
// suite runs after it (a CTest fixture) and reads the dump. Nothing it
// expects is taken from src/: the radiances are computed below from the
// definitions -- the camera settings, the luminous efficacy of sunlight, ISO
// 12232's exposure, and AgX's input range -- as the probe's picture is stated
// in view/ProbeGradient.hpp (VERIFICATION.md rule 2).
//
// **The budget: every value is one of the two binary16 values either side of
// the exact radiance.** The HDR target is RGBA16F, so each value was rounded
// to binary16 when it was written, and the Vulkan specification says only
// that a value between two representable ones is "rounded to one or the
// other. The rounding mode is not defined" (Fundamentals, Floating-Point
// Format Conversions). **Measured on this machine's GPU: it rounds toward
// zero** -- all 1,280 values of a grey row came back as the lower neighbour,
// the largest 9.4e-4 below the exact value, just under a full step -- so the
// budget allows either neighbour, not the nearest. What comes before the
// rounding is binary32 arithmetic in the shader: exp2 is within 3 + 2|x| ulp
// by the Vulkan precision rules, |x| <= 12.6 here, about 3.4e-6 of the value;
// the argument and the low end's logarithm add less than 1e-6 more. That is
// far inside a binary16 step -- 2^-10 of the value at most, 2^-11 at least --
// so a value outside the two neighbours is a defect in the chain.
//
// **What it catches that looking would not.** A half-pixel shift moves every
// value by 16.5 * 0.5 / 1280 stops, 0.45 %, several steps. A band in the wrong
// row or channel, a reversed depth test (the gradient is depth-tested, and
// under LESS it is never drawn), a quarter-size full-screen triangle (the
// probe draws with fullscreen.vert), a wrong exposure or efficacy: each moves
// a value out of its two neighbours.
//
// **What it cannot see**: the tonemap and the display encode, which act after
// the HDR target. Those are the PNGs', checked by M1-17's golden and M1-18's
// port check.
//
#include "HdrDumpFile.hpp"
#include "core/Scalar.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>

using namespace orb;
using namespace orb::test;

namespace {

// --- the definitions, written out here ------------------------------------

// The probe's camera: f/16, 1/125 s, ISO 100 (register decision 190).
constexpr f64 kFNumber = 16.0;
constexpr f64 kShutterSeconds = 1.0 / 125.0;
constexpr f64 kIso = 100.0;

// ISO 12232's saturation-based exposure: the luminance that saturates is
// 78 / (S q) * N^2 / t with q = 0.65 (Lagarde & de Rousiers 2014, 4.2).
[[nodiscard]] f64 saturationLuminance() {
    return 78.0 / (kIso * 0.65) * (kFNumber * kFNumber / kShutterSeconds);
}

// Sunlight's luminous efficacy, 98.9225 lm/W (register decision 175).
constexpr f64 kEfficacy = 98.9225;

// AgX's input range, in stops of exposed value (view/Tonemap.hpp copies the
// minimal implementation's numbers; these are the same published numbers).
constexpr f64 kAgxMinEv = -12.47393;
constexpr f64 kAgxMaxEv = 4.026069;

// The picture (view/ProbeGradient.hpp): four bands of 150 rows, 1280 wide.
constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;
constexpr std::uint32_t kBandRows = 150;
constexpr std::uint32_t kBands = 4;

// A radiance exposes to L * efficacy / saturation luminance; AgX's black is an
// exposed 2^minEv and its white 2^maxEv. The ramp runs between the two, even
// in stops, sampled at each pixel's centre.
[[nodiscard]] f64 expectedRadiance(std::uint32_t column) {
    const f64 perRadiance = kEfficacy / saturationLuminance();
    const f64 low = std::exp2(kAgxMinEv) / perRadiance;
    const f64 stops = kAgxMaxEv - kAgxMinEv;
    return low * std::exp2(stops * (static_cast<f64>(column) + 0.5) / static_cast<f64>(kWidth));
}

// The two binary16 values either side of a positive `value` in binary16's
// normal range -- the same value twice if it is one. A binary16 step at
// exponent e is 2^(e - 10); dividing by a power of two and flooring are exact
// in binary64, so these are exact.
struct Neighbours {
    f64 below{};
    f64 above{};
};

[[nodiscard]] Neighbours halfNeighbours(f64 value) {
    const f64 step = std::exp2(std::floor(std::log2(value)) - 10.0);
    const f64 below = std::floor(value / step) * step;
    return {.below = below, .above = bitsOf(below) == bitsOf(value) ? value : below + step};
}

[[nodiscard]] HdrDump readClearDump() {
    const std::filesystem::path path = std::filesystem::path{ORBSIM_PROBE_DIR} / "clear.hdr.f32";
    auto dump = readHdrDump(path);
    INFO("reading " << path.string() << ": " << (dump.has_value() ? "ok" : describe(dump.error())));
    REQUIRE(dump.has_value());
    REQUIRE(dump->width() == kWidth);
    REQUIRE(dump->height() == kHeight);
    return *std::move(dump);
}

using Channel = HdrDump::Channel;

// The columns checked: both edges, the middle pair, and quarters -- where a
// shift, a mirror or a wrong width would show first.
constexpr auto kColumns = std::to_array<std::uint32_t>({0, 1, 320, 639, 640, 960, 1279});

constexpr auto kColour = std::to_array<Channel>({Channel::Red, Channel::Green, Channel::Blue});

// One sample of one band: its channels either carry the ramp's radiance, to
// one of the two binary16 neighbours, or are exactly zero.
void requireSample(const HdrDump& dump, std::uint32_t band, Pixel pixel) {
    const f64 want = expectedRadiance(pixel.column);
    const Neighbours allowed = halfNeighbours(want);
    for (std::size_t c = 0; c < kColour.size(); ++c) {
        // Grey carries the radiance in all three channels; red, green and blue
        // in their own and nothing in the others.
        const bool lit = band == 0 || band == c + 1;
        // Exact: every binary32 value is a binary64 value.
        const auto got = static_cast<f64>(dump.at(pixel, kColour.at(c)));
        INFO("band " << band << ", row " << pixel.row << ", column " << pixel.column << ", channel "
                     << c << ": got " << got << ", exact " << want << ", allowed " << allowed.below
                     << " or " << allowed.above);
        const bool onNeighbour =
            bitsOf(got) == bitsOf(allowed.below) || bitsOf(got) == bitsOf(allowed.above);
        const bool zero = bitsOf(got) == bitsOf(0.0);
        REQUIRE((lit ? onNeighbour : zero));
    }
}

// The band's first, middle and last rows, at every checked column: a band a
// row out of place fails at its edge.
void requireBand(const HdrDump& dump, std::uint32_t band) {
    const std::array<std::uint32_t, 3> rows{
        band * kBandRows,
        (band * kBandRows) + (kBandRows / 2),
        ((band + 1) * kBandRows) - 1,
    };
    for (const std::uint32_t row : rows) {
        for (const std::uint32_t column : kColumns) {
            requireSample(dump, band, {.column = column, .row = row});
        }
    }
}

// A pixel of the undrawn strip holds the clear radiance, zero, in every
// colour channel.
void requireUndrawn(const HdrDump& dump, Pixel pixel) {
    INFO("row " << pixel.row << ", column " << pixel.column);
    for (const Channel channel : kColour) {
        REQUIRE(bitsOf(dump.at(pixel, channel)) == bitsOf(0.0F));
    }
}

} // namespace

TEST_CASE("every band carries the ramp's radiance, to within one binary16 step") {
    const HdrDump dump = readClearDump();
    for (std::uint32_t band = 0; band < kBands; ++band) {
        requireBand(dump, band);
    }
}

TEST_CASE("the grey band's three channels are the same number") {
    const HdrDump dump = readClearDump();
    for (std::uint32_t row = 0; row < kBandRows; ++row) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const Pixel pixel{.column = column, .row = row};
            const f32 red = dump.at(pixel, Channel::Red);
            INFO("row " << row << ", column " << column);
            REQUIRE(bitsOf(dump.at(pixel, Channel::Green)) == bitsOf(red));
            REQUIRE(bitsOf(dump.at(pixel, Channel::Blue)) == bitsOf(red));
        }
    }
}

TEST_CASE("below the bands nothing is drawn, and every pixel is opaque") {
    const HdrDump dump = readClearDump();
    for (std::uint32_t row = 0; row < kHeight; ++row) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const Pixel pixel{.column = column, .row = row};
            INFO("row " << row << ", column " << column);
            REQUIRE(bitsOf(dump.at(pixel, Channel::Alpha)) == bitsOf(1.0F));
        }
    }
    for (std::uint32_t row = kBands * kBandRows; row < kHeight; ++row) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            requireUndrawn(dump, {.column = column, .row = row});
        }
    }
}
