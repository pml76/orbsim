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
#include "view/ImageCompare.hpp"
#include "view/Lambert.hpp"
#include "view/ProbeSidecar.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
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

// A run that named no golden -- what every probe run did before M1-17.
constexpr SidecarGolden kNoGolden{
    .path = "",
    .verdict = "not compared",
    .difference = std::nullopt,
};

// A validation layer's two versions, each a value no other field has (M1-109).
constexpr SidecarLayer kLayer{
    .specVersion = (1U << 22U) | (4U << 12U) | 357U, // 1.4.357
    .implementationVersion = 7,
};

// The shader files a resolve pass reads, the most any probe without a scene of
// its own reads (M1-109).
constexpr auto kResolveShaders = std::to_array<std::string_view>({
    "fullscreen.vert.spv",
    "tonemap.frag.spv",
});

[[nodiscard]] std::string sidecarText(const SidecarGolden& golden = kNoGolden,
                                      const std::optional<LambertScene>& scene = std::nullopt,
                                      const std::optional<Ut1Time>& ut1 = std::nullopt,
                                      std::span<const std::string_view> shaders = kResolveShaders,
                                      const std::optional<SidecarLayer>& layer = kLayer) {
    const auto camera = Camera::from(Position{1.5, -2.0, 3.25}, Quat{}, Radians{0.5}, Metres{0.25});
    REQUIRE(camera.has_value());
    const std::array<std::string_view, 2> files{{"a.png", "a.exr"}};
    return formatSidecar({
        .probe = "clear",
        .description = "a description",
        .outcome = "rendered",
        .golden = golden,
        .epoch = kJ2000,
        .ut1 = ut1,
        .camera = *camera,
        .qualityPreset = "high",
        .exposure =
            {
                .aperture = Aperture::from(16.0).value(),
                .shutterTime = ShutterTime::from(Seconds{0.008}).value(),
                .iso = Iso::from(100.0).value(),
            },
        .scene = scene,
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
        .shaders = shaders,
        .validationLayer = layer,
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
    // Read before the message is built: MSVC warns (C4866) that it may not keep
    // the order of a call inside a chain of operator<<.
    const std::string ev100Text = valueOf(lines, "exposure.ev100");
    INFO("ev100 " << ev100Text << ", factor " << factorText);
    REQUIRE(std::abs(leadingNumber(ev100Text) - ev100) <= 1e-12);
    REQUIRE(std::abs((leadingNumber(factorText) / factor) - 1.0) <= 1e-12);
}

TEST_CASE("the sidecar records the golden, its verdict and both measurements") {
    // M1-17, register decision 237. 691 of 691,200 is a mean of 0.001 steps.
    const std::vector<std::string> lines = linesOf(sidecarText({
        .path = "tests/golden/clear.png",
        .verdict = "mismatch",
        .difference = ImageDifference{.largest = 7, .sum = 691, .count = 691'200},
    }));
    REQUIRE(valueOf(lines, "golden") == "tests/golden/clear.png");
    REQUIRE(valueOf(lines, "golden.verdict") == "mismatch");
    REQUIRE(valueOf(lines, "golden.largest") == "7/255 (limit 4/255)");
    REQUIRE(valueOf(lines, "golden.mean") == "0.00100/255 (limit under 0.5/255)");
}

TEST_CASE("a run that named no golden says so, and that nothing was measured") {
    const std::vector<std::string> lines = linesOf(sidecarText());
    REQUIRE(valueOf(lines, "golden") == "none");
    REQUIRE(valueOf(lines, "golden.verdict") == "not compared");
    REQUIRE(valueOf(lines, "golden.largest") == "not measured");
    REQUIRE(valueOf(lines, "golden.mean") == "not measured");
}

// --- a lambert probe's light and surface (M1-18, register decision 264) -------

TEST_CASE("a lambert probe's sidecar records its light and its surface") {
    // Values no other field has, so a value under the wrong key fails by name.
    const f64 tilt = std::numbers::pi / 3.0;
    const LambertScene scene{
        .albedo = Albedo::from(0.3).value(),
        .sunDistance = Metres{74'798'935'350.0},
        .irradiance = Irradiance{5444.0},
        .patch =
            {
                .centre = Position{0.0, 0.0, -10.0},
                .side = Metres{100.0},
                .tilt = Radians{tilt},
            },
        .towardSun = Direction{0.0, 0.0, 1.0},
    };
    const std::vector<std::string> lines = linesOf(sidecarText(kNoGolden, scene));
    REQUIRE(valueOf(lines, "scene.light") == "the Sun, a Lambertian patch");
    REQUIRE(valueOf(lines, "scene.albedo") == "0.3");
    REQUIRE(valueOf(lines, "scene.sun.distance") == "74798935350 m");
    REQUIRE(valueOf(lines, "scene.irradiance") == "5444 W/m^2 at normal incidence");
    REQUIRE(valueOf(lines, "scene.sun.direction") == "0 0 1 (world, toward the Sun)");
    REQUIRE(valueOf(lines, "scene.patch.centre") == "0 0 -10 m");
    REQUIRE(valueOf(lines, "scene.patch.side") == "100 m");
    const std::string tiltText = valueOf(lines, "scene.patch.tilt");
    REQUIRE(tiltText.ends_with(" rad about the horizontal axis"));
    REQUIRE(std::abs(leadingNumber(tiltText) - tilt) <= 1e-15);
    // The angle of incidence is measured between the patch's normal and the
    // Sun, not copied from the tilt; with the Sun straight behind the camera
    // they are the same angle, to the rounding of one atan2.
    const std::string incidenceText = valueOf(lines, "scene.incidence");
    INFO("incidence " << incidenceText);
    REQUIRE(incidenceText.ends_with(" rad"));
    REQUIRE(std::abs(leadingNumber(incidenceText) - tilt) <= 1e-14);
}

TEST_CASE("a probe without a light says so, and writes no scene's numbers") {
    const std::vector<std::string> lines = linesOf(sidecarText());
    REQUIRE(valueOf(lines, "scene.light") == "none: drawn directly in radiance");
    const bool anyNumber = std::ranges::any_of(lines, [](const std::string& line) noexcept {
        return line.starts_with("scene.") && !line.starts_with("scene.light");
    });
    REQUIRE(!anyNumber);
}

// --- a probe that turns the Earth (M1-20, register decision 323) ---------------

TEST_CASE("a probe that turns the Earth records its UT1, and one that does not says so") {
    // The grid probes' UT1, the Skyfield row's: JD 2460886.5 + 35273/131072,
    // so MJD 60886 and 35273/131072 of a day -- 23 251 245 117 187 500 ps,
    // 06:27:31.2451171875 -- worked out by hand.
    const auto ut1 = Ut1Time::fromJulianDate({.day = 2'460'886.5, .fraction = 0.26911163330078125});
    REQUIRE(ut1.has_value());
    const std::vector<std::string> turned = linesOf(sidecarText(kNoGolden, std::nullopt, *ut1));
    REQUIRE(valueOf(turned, "earth.ut1") ==
            "UT1 2025-07-30T06:27:31.245 (MJD 60886 + 23251245117187500 ps)");
    const std::vector<std::string> plain = linesOf(sidecarText());
    REQUIRE(valueOf(plain, "earth.ut1") == "none: no rotating body is drawn");
}

// --- the shader files the run read (M1-109, register decision 394) ------------
//
// scripts/mutants-due.py reads this line to know which shaders a probe's
// verdict depends on, so a shader no probe reads makes no probe's mutant file
// due. A name lost or misspelt here would narrow that list silently.

TEST_CASE("the sidecar names every shader file the run read, in the order it read them") {
    constexpr auto kLambertRead = std::to_array<std::string_view>({
        "lambert.vert.spv",
        "lambert.frag.spv",
        "fullscreen.vert.spv",
        "tonemap.frag.spv",
    });
    const std::vector<std::string> lines =
        linesOf(sidecarText(kNoGolden, std::nullopt, std::nullopt, kLambertRead));
    REQUIRE(valueOf(lines, "shaders") ==
            "lambert.vert.spv lambert.frag.spv fullscreen.vert.spv tonemap.frag.spv");
}

TEST_CASE("a run that read no shader says so, rather than leaving the line empty") {
    // A run that stopped before its scene was built: an empty value would read
    // the same as a line cut short.
    const std::vector<std::string> lines =
        linesOf(sidecarText(kNoGolden, std::nullopt, std::nullopt, {}));
    REQUIRE(valueOf(lines, "shaders") == "none");
}

// --- the validation layer the run had (M1-109, register decision 397) ---------
//
// scripts/mutants-due.py takes the layer's version as part of the graphics
// card a GPU test ran on: a newer layer can report what an older one did not.

TEST_CASE("the sidecar records the validation layer's version and its implementation") {
    const std::vector<std::string> lines = linesOf(sidecarText());
    REQUIRE(valueOf(lines, "validation.layer") == "1.4.357 (variant 0), implementation 7");
}

TEST_CASE("a run without the validation layer says so") {
    const std::vector<std::string> lines =
        linesOf(sidecarText(kNoGolden, std::nullopt, std::nullopt, kResolveShaders, std::nullopt));
    REQUIRE(valueOf(lines, "validation.layer") == "none: the run had no validation layer");
}
