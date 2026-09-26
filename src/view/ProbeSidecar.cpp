#include "view/ProbeSidecar.hpp" // SF.5: own header, first
#include "core/Math.hpp"
#include "core/Time.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

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

[[nodiscard]] std::string formatPosition(const Position& position) {
    return std::format("{} {} {} m", position.x.value(), position.y.value(), position.z.value());
}

[[nodiscard]] std::string formatOrientation(const Quat& q) {
    return std::format("{} {} {} {} (w x y z, camera to world)", q.w, q.x, q.y, q.z);
}

} // namespace

std::string formatSidecar(const SidecarFields& fields) {
    const ExposureValue100 ev = exposureValue100(fields.exposure);
    const std::array entries{
        Entry{.key = "probe", .value = std::string(fields.probe)},
        Entry{.key = "description", .value = std::string(fields.description)},
        Entry{.key = "outcome", .value = std::string(fields.outcome)},
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
    };

    std::string text = "# orbsim probe sidecar (M1-16): what produced the files beside it\n";
    for (const Entry& entry : entries) {
        // Keys padded to one column, so the file reads as a table.
        text += std::format("{:<20} = {}\n", entry.key, entry.value);
    }
    return text;
}

} // namespace orb::view
