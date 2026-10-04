//
// The port check (M1-15, M1-18; ADR 0014; register decisions 260 and 261):
// every pixel of every probe's displayed frame against src/view/Tonemap.hpp
// and src/view/Srgb.hpp applied, on the CPU, to the HDR value the GPU read
// back.
//
// **A port check, not a validation.** A display transform is a choice and not
// a physical claim (ADR 0014), so what is tested is that the GPU's resolve
// pass -- shaders/tonemap.frag -- and the CPU's are the same computation:
// the exposure multiply, AgX, and the sRGB encode, in that order. **Here the
// reference is src/, deliberately**: the CPU chain is the thing the shader is
// a port of, and it is itself held to 50-digit values by test_tonemap and
// test_srgb. The HDR dump is the input to both, so the radiances in it need
// not be right, only read back -- their correctness is test_probe_clear's
// and test_radiometry's.
//
// **The budgets, measured before they were written** (register decision 260),
// on `clear` across all 921,600 pixels, on this machine's two GPUs:
//
//   * **the 8-bit image within 1/255.** Measured 0.56/255 on the RTX A2000
//     and 0.51/255 on the Intel UHD: the half step an 8-bit value rounds by,
//     plus binary32 arithmetic in the shader;
//   * **the 16-bit image within 2 of 65,535 steps.** Measured 0.57 steps on
//     both: here the rounding is a tenth of the 8-bit image's, so this check
//     sees a fault 128 times smaller than the first can.
//
// **What it is for.** `tonemap-port` holds light the tonemap's clamps act on
// (shaders/probe_port.frag) -- negative channels, and radiance above AgX's
// white -- because nothing physical does, and the first clamp, negative light
// set to zero, survived M1-15's mutation pass for want of it. Measured before
// the ruling (decision 261): without that clamp a frame with a negative
// channel misses the CPU chain by up to 195.7/255. The suite asserts first
// that the frame really holds such light, so the check cannot silently stop
// checking (VERIFICATION.md rule 23). The second clamp, before AgX's 2.2
// power, is absorbed by the sRGB encode's own clamp on this machine and
// stays a declared survivor (decision 262).
//
// Every probe's frame is checked, `clear` and the lambert probes included:
// the fixture pattern is tests/test_radiometry.cpp's.
//
#include "HdrDumpFile.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Exposure.hpp"
#include "view/ImageCompare.hpp"
#include "view/ImageFiles.hpp"
#include "view/Srgb.hpp"
#include "view/Tonemap.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace orb;
using namespace orb::test;

namespace {

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;

// The budgets above, in each image's own steps.
constexpr f64 kEightBitSteps = 1.0;
constexpr f64 kSixteenBitSteps = 2.0;

// The exposure each probe pins (src/render/Probes.cpp; register decisions 176
// and 256): "sunny 16" for all but `lambert-exposure`, two stops brighter.
// Written here rather than read from the sidecar, so a probe that pinned
// another exposure than the one it claims fails here.
[[nodiscard]] view::CameraSettings exposureAt(f64 fNumber) {
    return {
        .aperture = view::Aperture::from(fNumber).value(),
        .shutterTime = view::ShutterTime::from(Seconds{1.0 / 125.0}).value(),
        .iso = view::Iso::from(100.0).value(),
    };
}

struct ProbeUnderTest {
    std::string_view name;
    f64 fNumber{};
};

// A probe's file, by its whole name: `clear.png`, `lambert.hdr.f32`.
[[nodiscard]] std::filesystem::path probeFile(const std::string& fileName) {
    return std::filesystem::path{ORBSIM_PROBE_DIR} / fileName;
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
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

using Channel = HdrDump::Channel;

// The CPU chain on one read-back pixel: exposure, AgX, the sRGB encode -- the
// shader's order -- giving each channel's encoded value in [0, 1].
[[nodiscard]] std::array<f64, 3> displayed(const HdrDump& dump, Pixel pixel, f64 perRadiance) {
    const view::ExposedRgb exposed{
        .red = static_cast<f64>(dump.at(pixel, Channel::Red)) * perRadiance,
        .green = static_cast<f64>(dump.at(pixel, Channel::Green)) * perRadiance,
        .blue = static_cast<f64>(dump.at(pixel, Channel::Blue)) * perRadiance,
    };
    const view::DisplayRgb linear = view::agxTonemap(exposed);
    // std::to_array: MSVC wants a std::array's inner braces (C5246), as gcc
    // does (-Wmissing-braces), and clang-tidy's trailing comma then objects.
    return std::to_array({
        view::srgbEncode(linear.red).value(),
        view::srgbEncode(linear.green).value(),
        view::srgbEncode(linear.blue).value(),
    });
}

// The largest difference over the frame, in steps of each image, and where.
struct Worst {
    f64 eightBitSteps{};
    f64 sixteenBitSteps{};
    std::size_t eightBitAt{};
    std::size_t sixteenBitAt{};
};

// The two read-back pictures of a probe.
struct Pictures {
    view::Rgb8Image eight;
    view::Rgb16Image sixteen;
};

[[nodiscard]] Pictures readPictures(std::string_view probe) {
    auto eight = view::decodePng8(readBytes(probeFile(std::string(probe) + ".png")));
    auto sixteen = view::decodePng16(readBytes(probeFile(std::string(probe) + ".16.png")));
    INFO(probe << ": " << (eight ? "8-bit read" : eight.error().detail) << "; "
               << (sixteen ? "16-bit read" : sixteen.error().detail));
    REQUIRE(eight.has_value());
    REQUIRE(sixteen.has_value());
    REQUIRE(eight->valueCount() == std::size_t{kWidth} * kHeight * 3);
    REQUIRE(sixteen->valueCount() == eight->valueCount());
    return {.eight = *std::move(eight), .sixteen = *std::move(sixteen)};
}

// One value of the pictures, and what the CPU chain says it should be.
struct Sample {
    std::size_t index{};
    f64 want{};
};

// That value's difference from the CPU chain, in each picture's steps, kept
// if it is the largest so far.
void keepLargest(Worst& worst, const Pictures& pictures, Sample sample) {
    const f64 d8 =
        std::abs(static_cast<f64>(pictures.eight.value(sample.index)) - (255.0 * sample.want));
    const f64 d16 =
        std::abs(static_cast<f64>(pictures.sixteen.value(sample.index)) - (65535.0 * sample.want));
    if (d8 > worst.eightBitSteps) {
        worst.eightBitSteps = d8;
        worst.eightBitAt = sample.index;
    }
    if (d16 > worst.sixteenBitSteps) {
        worst.sixteenBitSteps = d16;
        worst.sixteenBitAt = sample.index;
    }
}

[[nodiscard]] Worst measure(const ProbeUnderTest& probe) {
    const HdrDump dump = readDump(probe.name);
    const Pictures pictures = readPictures(probe.name);
    const f64 perRadiance =
        view::radianceExposure(view::exposureValue100(exposureAt(probe.fNumber))).value();
    Worst worst;
    for (std::uint32_t row = 0; row < kHeight; ++row) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const std::array<f64, 3> want =
                displayed(dump, {.column = column, .row = row}, perRadiance);
            const std::size_t first = ((std::size_t{row} * kWidth) + column) * 3;
            for (std::size_t c = 0; c < 3; ++c) {
                keepLargest(worst, pictures, {.index = first + c, .want = want.at(c)});
            }
        }
    }
    return worst;
}

void requirePort(const ProbeUnderTest& probe) {
    const Worst worst = measure(probe);
    // Value index i is pixel i / 3 of a 1280-wide frame, channel i % 3.
    INFO(probe.name << ": 8-bit " << worst.eightBitSteps << " steps at value " << worst.eightBitAt
                    << ", 16-bit " << worst.sixteenBitSteps << " steps at value "
                    << worst.sixteenBitAt);
    REQUIRE(worst.eightBitSteps <= kEightBitSteps);
    REQUIRE(worst.sixteenBitSteps <= kSixteenBitSteps);
}

} // namespace

TEST_CASE("the tonemap-port frame holds the light the clamps act on") {
    // Checked before its port check is believed: a frame of ordinary light
    // would pass that check and test neither clamp.
    const HdrDump dump = readDump("tonemap-port");
    // Bands of 120 rows (shaders/probe_port.frag): the second band's green,
    // the third's red, the fourth's blue and all of the sixth's are negative
    // in every column.
    struct NegativeBand {
        std::uint32_t row{};
        Channel channel{};
    };
    const auto negative = std::to_array<NegativeBand>({
        {.row = 180, .channel = Channel::Green},
        {.row = 300, .channel = Channel::Red},
        {.row = 420, .channel = Channel::Blue},
        {.row = 660, .channel = Channel::Red},
    });
    for (const NegativeBand& band : negative) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const f32 value = dump.at({.column = column, .row = band.row}, band.channel);
            if (!(value < 0.0F)) {
                INFO("row " << band.row << ", column " << column << ": " << value);
                REQUIRE(value < 0.0F);
            }
        }
    }
    // And radiance above AgX's white, 2^kMaxEv exposed, in the grey band's
    // brightest column and the red band's.
    const f64 perRadiance =
        view::radianceExposure(view::exposureValue100(exposureAt(16.0))).value();
    const f64 white = std::exp2(view::agx::kMaxEv) / perRadiance;
    for (const std::uint32_t row : {60U, 540U}) {
        const auto brightest =
            static_cast<f64>(dump.at({.column = kWidth - 1, .row = row}, Channel::Red));
        INFO("row " << row << ": " << brightest << " W/(m^2 sr) against AgX's white, " << white);
        REQUIRE(brightest > 2.0 * white);
    }
}

TEST_CASE("the port check holds on tonemap-port's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "tonemap-port", .fNumber = 16.0});
}

TEST_CASE("the port check holds on clear's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "clear", .fNumber = 16.0});
}

TEST_CASE("the port check holds on lambert's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lambert", .fNumber = 16.0});
}

TEST_CASE("the port check holds on lambert-half-au's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lambert-half-au", .fNumber = 16.0});
}

TEST_CASE("the port check holds on lambert-two-au's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lambert-two-au", .fNumber = 16.0});
}

TEST_CASE("the port check holds on lambert-tilted-60's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lambert-tilted-60", .fNumber = 16.0});
}

TEST_CASE("the port check holds on lambert-exposure's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lambert-exposure", .fNumber = 8.0});
}

TEST_CASE("the port check holds on lambert-backlit's picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lambert-backlit", .fNumber = 16.0});
}

TEST_CASE("the port check holds on lines' picture, to 1/255 and to 2/65535") {
    requirePort({.name = "lines", .fNumber = 16.0});
}
