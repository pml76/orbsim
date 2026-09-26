//
// Tests for view/ProbeSidecar.hpp: the text that says what produced a probe's
// files (M1-16, register decision 191).
//
// **Line by line, against lines written here.** Each field is given a value
// no other field has, so a value written under the wrong key -- the defect a
// table of twenty-odd similar lines invites -- fails by name. The two derived
// numbers, EV100 and the exposure factor, are checked against the relations
// computed here from their definitions rather than as text, since the text of
// a double is not the claim.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/ProbeSidecar.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace orb;
using namespace orb::view;

namespace {

[[nodiscard]] std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        REQUIRE(end != std::string::npos); // every line ends in a newline
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// The value on the line whose key is `key`, which must appear exactly once.
[[nodiscard]] std::string valueOf(const std::vector<std::string>& lines, std::string_view key) {
    std::string found;
    int count = 0;
    for (const std::string& line : lines) {
        const std::size_t equals = line.find(" = ");
        if (equals == std::string::npos) continue;
        std::string_view name(line.data(), equals);
        while (name.ends_with(' ')) {
            name.remove_suffix(1);
        }
        if (name == key) {
            found = line.substr(equals + 3);
            ++count;
        }
    }
    INFO("key " << key);
    REQUIRE(count == 1);
    return found;
}

[[nodiscard]] f64 leadingNumber(const std::string& text) {
    f64 value = 0.0;
    const char* const last = std::to_address(text.end());
    const auto [end, ec] = std::from_chars(std::to_address(text.begin()), last, value);
    REQUIRE(ec == std::errc{});
    static_cast<void>(end);
    return value;
}

[[nodiscard]] std::string sidecarText() {
    const auto camera = Camera::from(Position{1.5, -2.0, 3.25}, Quat{}, Radians{0.5}, Metres{0.25});
    REQUIRE(camera.has_value());
    const std::array<std::string_view, 2> files{"a.png", "a.exr"};
    return formatSidecar({
        .probe = "clear",
        .description = "a description",
        .outcome = "rendered",
        .epoch = kJ2000,
        .camera = *camera,
        .qualityPreset = "high",
        .exposure =
            {
                .aperture = Aperture::from(16.0).value(),
                .shutterTime = ShutterTime::from(Seconds{0.008}).value(),
                .iso = Iso::from(100.0).value(),
            },
        .device =
            {
                .name = "A GPU",
                .vendorId = 0x10DE,
                .deviceId = 0x25B8,
                .driverName = "A driver",
                .driverInfo = "582.53",
                .driverVersion = 0x91A34000,
                .apiVersion = (1U << 22U) | (4U << 12U) | 312U, // 1.4.312
            },
        .build = {.configuration = "Debug", .compiler = "clang 23.1.0"},
        .runDateUtc = "2026-09-26T12:34:56Z",
        .files = files,
    });
}

} // namespace

TEST_CASE("the sidecar's text fields are each under their own key") {
    const std::vector<std::string> lines = linesOf(sidecarText());
    REQUIRE(lines.front().starts_with("# "));
    REQUIRE(valueOf(lines, "probe") == "clear");
    REQUIRE(valueOf(lines, "description") == "a description");
    REQUIRE(valueOf(lines, "outcome") == "rendered");
    REQUIRE(valueOf(lines, "quality") == "high");
    REQUIRE(valueOf(lines, "gpu") == "A GPU");
    REQUIRE(valueOf(lines, "driver.name") == "A driver");
    REQUIRE(valueOf(lines, "driver.info") == "582.53");
    REQUIRE(valueOf(lines, "build.configuration") == "Debug");
    REQUIRE(valueOf(lines, "build.compiler") == "clang 23.1.0");
    REQUIRE(valueOf(lines, "run.date") == "2026-09-26T12:34:56Z");
    REQUIRE(valueOf(lines, "files") == "a.png a.exr");
}

TEST_CASE("the sidecar's numbers are written with their units") {
    const std::vector<std::string> lines = linesOf(sidecarText());
    // J2000.0 TT: noon on 2000-01-01, stored as MJD 51544 and half a day of
    // picoseconds (core/Time.hpp asserts both).
    REQUIRE(valueOf(lines, "epoch") ==
            "TT 2000-01-01T12:00:00.000 (MJD 51544 + 43200000000000000 ps)");
    REQUIRE(valueOf(lines, "camera.position") == "1.5 -2 3.25 m");
    REQUIRE(valueOf(lines, "camera.orientation") == "1 0 0 0 (w x y z, camera to world)");
    REQUIRE(valueOf(lines, "camera.fov") == "0.5 rad vertical");
    REQUIRE(valueOf(lines, "camera.near") == "0.25 m");
    REQUIRE(valueOf(lines, "exposure.aperture") == "f/16");
    REQUIRE(valueOf(lines, "exposure.shutter") == "0.008 s");
    REQUIRE(valueOf(lines, "exposure.iso") == "100");
    REQUIRE(valueOf(lines, "gpu.vendor") == "0x10de");
    REQUIRE(valueOf(lines, "gpu.device") == "0x25b8");
    REQUIRE(valueOf(lines, "driver.version") == "0x91a34000");
    REQUIRE(valueOf(lines, "vulkan.api") == "1.4.312 (variant 0)");
}

TEST_CASE("the sidecar's exposure agrees with its definitions") {
    const std::vector<std::string> lines = linesOf(sidecarText());
    // EV100 = log2(N^2 / t * 100 / S), and the factor 98.9225 / (1.2 * 2^EV100)
    // per W/(m^2 sr) (view/Exposure.hpp's sources), computed here.
    const f64 ev100 = std::log2(16.0 * 16.0 / 0.008);
    const f64 factor = 98.9225 / (78.0 / 65.0 * std::exp2(ev100));
    const std::string factorText = valueOf(lines, "exposure.factor");
    REQUIRE(factorText.ends_with(" per W/(m^2 sr)"));
    INFO("ev100 " << valueOf(lines, "exposure.ev100") << ", factor " << factorText);
    REQUIRE(std::abs(leadingNumber(valueOf(lines, "exposure.ev100")) - ev100) <= 1e-12);
    REQUIRE(std::abs((leadingNumber(factorText) / factor) - 1.0) <= 1e-12);
}
