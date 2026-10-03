#include "view/ProbeSidecar.hpp" // SF.5: own header, first
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/ImageCompare.hpp"
#include "view/Lambert.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orb::view {
namespace {

// One line of the file. A struct, so that a key and its value are named where
// they are written and cannot be given the wrong way round (non-negotiable 1).
struct Entry {
    std::string_view key;
    std::string value;
};

// TT, as a calendar date and time and as the two numbers the instant is
// stored as, so that the line can be read by a person and checked exactly.
[[nodiscard]] std::string formatEpoch(const TtTime& epoch) {
    const std::string stored =
        std::format("MJD {} + {} ps", epoch.modifiedJulianDay(), epoch.picosecondOfDay());
    const auto date = epoch.toCalendar();
    if (!date) return std::format("TT, outside the calendar ({})", stored);
    return std::format("TT {:04}-{:02}-{:02}T{:02}:{:02}:{:06.3f} ({})",
                       date->year,
                       date->month,
                       date->day,
                       date->hour,
                       date->minute,
                       date->second.value(),
                       stored);
}

// Vulkan's packed version -- variant in the top 3 bits, then 7 of major, 10
// of minor and 12 of patch (the specification's VK_MAKE_API_VERSION) --
// unpacked here, because this layer does not see Vulkan's headers.
[[nodiscard]] std::string formatApiVersion(std::uint32_t packed) {
    return std::format("{}.{}.{} (variant {})",
                       (packed >> 22U) & 0x7FU,
                       (packed >> 12U) & 0x3FFU,
                       packed & 0xFFFU,
                       packed >> 29U);
}

[[nodiscard]] std::string joined(std::span<const std::string_view> words) {
    std::string text;
    for (const std::string_view word : words) {
        if (!text.empty()) text += ' ';
        text += word;
    }
    return text;
}

// A measurement beside its limit, or that there was none.
[[nodiscard]] std::string formatLargestLine(const SidecarGolden& golden) {
    if (!golden.difference) return "not measured";
    return std::format("{} (limit {}/255)", formatLargest(*golden.difference), kGoldenLargestSteps);
}

[[nodiscard]] std::string formatMeanLine(const SidecarGolden& golden) {
    if (!golden.difference) return "not measured";
    const f64 limit =
        static_cast<f64>(kGoldenMeanNumerator) / static_cast<f64>(kGoldenMeanDenominator);
    return std::format("{} (limit under {}/255)", formatMean(*golden.difference), limit);
}

[[nodiscard]] std::string formatPosition(const Position& position) {
    return std::format("{} {} {} m", position.x.value(), position.y.value(), position.z.value());
}

[[nodiscard]] std::string formatOrientation(const Quat& q) {
    return std::format("{} {} {} {} (w x y z, camera to world)", q.w, q.x, q.y, q.z);
}

// A lambert probe's light and surface, or that there is none (M1-18, register
// decision 264). The angle of incidence is measured here, between the
// patch's own normal and the direction toward the Sun, rather than copied
// from the tilt: it is what the shader's cosine is of.
[[nodiscard]] std::vector<Entry> sceneEntries(const std::optional<LambertScene>& scene) {
    if (!scene) return {Entry{.key = "scene.light", .value = "none: drawn directly in radiance"}};
    const Direction& sun = scene->towardSun;
    const Radians incidence = angleBetween(squarePatch(scene->patch).normal, sun);
    return {
        Entry{.key = "scene.light", .value = "the Sun, a Lambertian patch"},
        Entry{.key = "scene.albedo", .value = std::format("{}", scene->albedo.value())},
        Entry{
            .key = "scene.sun.distance",
            .value = std::format("{} m", scene->sunDistance.value()),
        },
        Entry{
            .key = "scene.irradiance",
            .value = std::format("{} W/m^2 at normal incidence", scene->irradiance.value()),
        },
        Entry{
            .key = "scene.sun.direction",
            .value = std::format(
                "{} {} {} (world, toward the Sun)", sun.x.value(), sun.y.value(), sun.z.value()),
        },
        Entry{.key = "scene.incidence", .value = std::format("{} rad", incidence.value())},
        Entry{.key = "scene.patch.centre", .value = formatPosition(scene->patch.centre)},
        Entry{.key = "scene.patch.side", .value = std::format("{} m", scene->patch.side.value())},
        Entry{
            .key = "scene.patch.tilt",
            .value = std::format("{} rad about the horizontal axis", scene->patch.tilt.value()),
        },
    };
}

} // namespace

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
std::string formatSidecar(const SidecarFields& fields) {
    const ExposureValue100 ev = exposureValue100(fields.exposure);
    std::vector<Entry> entries{
        Entry{.key = "probe", .value = std::string(fields.probe)},
        Entry{.key = "description", .value = std::string(fields.description)},
        Entry{.key = "outcome", .value = std::string(fields.outcome)},
        Entry{
            .key = "golden",
            .value =
                fields.golden.path.empty() ? std::string("none") : std::string(fields.golden.path),
        },
        Entry{.key = "golden.verdict", .value = std::string(fields.golden.verdict)},
        Entry{.key = "golden.largest", .value = formatLargestLine(fields.golden)},
        Entry{.key = "golden.mean", .value = formatMeanLine(fields.golden)},
        Entry{.key = "epoch", .value = formatEpoch(fields.epoch)},
        Entry{.key = "camera.position", .value = formatPosition(fields.camera.position())},
        Entry{.key = "camera.orientation", .value = formatOrientation(fields.camera.orientation())},
        Entry{
            .key = "camera.fov",
            .value = std::format("{} rad vertical", fields.camera.verticalFov().value()),
        },
        Entry{
            .key = "camera.near",
            .value = std::format("{} m", fields.camera.nearPlane().value()),
        },
        Entry{.key = "quality", .value = std::string(fields.qualityPreset)},
        Entry{
            .key = "exposure.aperture",
            .value = std::format("f/{}", fields.exposure.aperture.fNumber()),
        },
        Entry{
            .key = "exposure.shutter",
            .value = std::format("{} s", fields.exposure.shutterTime.duration().value()),
        },
        Entry{.key = "exposure.iso", .value = std::format("{}", fields.exposure.iso.speed())},
        Entry{.key = "exposure.ev100", .value = std::format("{}", ev.stops())},
        Entry{
            .key = "exposure.factor",
            .value = std::format("{} per W/(m^2 sr)", radianceExposure(ev).value()),
        },
    };
    const std::vector<Entry> scene = sceneEntries(fields.scene);
    entries.insert(entries.end(), scene.begin(), scene.end());
    const auto rest = std::to_array<Entry>({
        Entry{.key = "gpu", .value = std::string(fields.device.name)},
        Entry{.key = "gpu.vendor", .value = std::format("0x{:04x}", fields.device.vendorId)},
        Entry{.key = "gpu.device", .value = std::format("0x{:04x}", fields.device.deviceId)},
        Entry{.key = "driver.name", .value = std::string(fields.device.driverName)},
        Entry{.key = "driver.info", .value = std::string(fields.device.driverInfo)},
        Entry{
            .key = "driver.version",
            .value = std::format("0x{:08x}", fields.device.driverVersion),
        },
        Entry{.key = "vulkan.api", .value = formatApiVersion(fields.device.apiVersion)},
        Entry{.key = "build.configuration", .value = std::string(fields.build.configuration)},
        Entry{.key = "build.compiler", .value = std::string(fields.build.compiler)},
        Entry{.key = "run.date", .value = std::string(fields.runDateUtc)},
        Entry{.key = "files", .value = joined(fields.files)},
    });
    entries.insert(entries.end(), rest.begin(), rest.end());

    std::string text = "# orbsim probe sidecar (M1-16): what produced the files beside it\n";
    for (const Entry& entry : entries) {
        // Keys padded to one column, so the file reads as a table.
        text += std::format("{:<20} = {}\n", entry.key, entry.value);
    }
    return text;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::view
