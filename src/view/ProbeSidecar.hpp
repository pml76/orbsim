#ifndef ORBSIM_VIEW_PROBESIDECAR_HPP
#define ORBSIM_VIEW_PROBESIDECAR_HPP
//
// A probe's sidecar, `<name>.txt`: what produced the files beside it (M1-16,
// register decision 191). "A frame without provenance is a screenshot."
//
// **`key = value` lines**, the shape of the fixture files' headers
// (tests/FixtureFile.hpp), with a comment line first. Every value is text by
// the time it reaches here -- the application gathers it -- except the
// physical ones, which are formatted here so that their units are written
// beside them by the code that knows them.
//
// **The run's date is the one clock a probe touches**, and it touches only
// this file: the application reads it once and hands it in, so nothing here
// reads a clock and the text is a pure function of its fields -- which is what
// lets tests/test_probe_sidecar.cpp hold it to an exact expected string.
//
#include "core/Time.hpp"
#include "view/Camera.hpp"
#include "view/Exposure.hpp"
#include "view/ImageCompare.hpp"
#include "view/Lambert.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace orb::view {

// The GPU and its driver, as VulkanContext::deviceDescription reads them. A
// type of this layer's own, because orbsim_view does not see src/render.
struct SidecarDevice {
    std::string_view name;
    std::uint32_t vendorId{};
    std::uint32_t deviceId{};
    std::string_view driverName;
    std::string_view driverInfo;
    std::uint32_t driverVersion{};
    std::uint32_t apiVersion{}; // Vulkan's packed form: variant, major, minor, patch
};

// What built the application.
struct SidecarBuild {
    std::string_view configuration; // CMake's: Debug, RelWithDebInfo, ...
    std::string_view compiler;
};

// What the run did with a golden image (M1-17, register decision 237).
struct SidecarGolden {
    std::string_view path;    // empty when no golden was named
    std::string_view verdict; // "not compared", "matches", "mismatch", "accept requested",
                              // or why no comparison could be made
    std::optional<ImageDifference> difference; // absent when nothing was measured
};

// Everything the sidecar records.
struct SidecarFields {
    std::string_view probe;
    std::string_view description;
    // "rendered", or what went wrong: a run that could not render still
    // writes its sidecar (register decision 193).
    std::string_view outcome;
    SidecarGolden golden;
    TtTime epoch;
    // The UT1 the Earth was turned to (M1-20, register decision 323); absent
    // for a probe that draws no rotating body.
    std::optional<Ut1Time> ut1;
    Camera camera;
    std::string_view qualityPreset;
    CameraSettings exposure;
    // The lambert probes' light and surface (M1-18, register decision 264);
    // absent for a probe that draws its picture directly in radiance.
    std::optional<LambertScene> scene;
    SidecarDevice device;
    SidecarBuild build;
    std::string_view runDateUtc; // ISO 8601, to the second, with a Z
    std::span<const std::string_view> files;
};

// The whole file's text, LF line endings, ending in a newline.
// gcc's -Wabi-tag: std::string carries libstdc++'s "cxx11" ABI tag, and
// gcc wants everything holding or returning one to carry it too. The tag
// guards code shipped as a binary against the old string ABI; this project
// builds everything from source with one ABI. Off at this site alone, for
// gcc alone -- register decision 52's ruling and shape.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
[[nodiscard]] std::string formatSidecar(const SidecarFields& fields);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::view

#endif // ORBSIM_VIEW_PROBESIDECAR_HPP
