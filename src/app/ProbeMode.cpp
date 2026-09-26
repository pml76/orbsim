#include "app/ProbeMode.hpp" // SF.5: own header, first
#include "app/ExitCodes.hpp"
#include "render/Probes.hpp"
#include "render/ResolvePass.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Exposure.hpp"
#include "view/ImageFiles.hpp"
#include "view/ProbeImage.hpp"
#include "view/ProbeSidecar.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace orb::app {
namespace {

// What built this binary, for the sidecar. The configuration is CMake's,
// handed in by CMakeLists.txt; the compiler names itself.
constexpr std::string_view kBuildConfiguration = ORBSIM_BUILD_CONFIG;
// The compiler, naming itself: a string constant where the compiler gives
// its version as one, and MSVC's number formatted where it does not.
#if defined(_MSC_VER) && !defined(__clang__)
[[nodiscard]] std::string compilerName() { return std::format("MSVC {}", _MSC_FULL_VER); }
#else
#ifdef __clang__
constexpr std::string_view kCompilerName = "clang " __clang_version__;
#else
constexpr std::string_view kCompilerName = "gcc " __VERSION__;
#endif
[[nodiscard]] std::string compilerName() { return std::string(kCompilerName); }
#endif

// The run's date and time in UTC, to the second (register decision 191): the
// one clock a probe run reads, and only for the sidecar. No pixel depends on
// it, and the HDR dump that the determinism test compares does not contain it.
[[nodiscard]] std::string runDateUtc() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

// Writes bytes to a file, replacing what was there, through
// std::filesystem::path so that the platform's own encoding of the name is
// used (CODING_GUIDELINES section 18). The bytes become chars by value rather
// than by a pointer cast, which costs one copy of a few megabytes.
[[nodiscard]] std::expected<void, std::string> writeFile(const std::filesystem::path& path,
                                                         std::span<const std::byte> bytes) {
    std::vector<char> chars(bytes.size());
    std::ranges::transform(bytes, chars.begin(), [](std::byte b) { return static_cast<char>(b); });
    // Binary alone: an output stream truncates by itself, and openmode is a
    // signed bitmask type, so combining two of its values is what
    // bugprone-signed-bitwise reports.
    std::ofstream file(path, std::ios::binary);
    if (!file) return std::unexpected("cannot open " + path.string() + " for writing");
    file.write(chars.data(), static_cast<std::streamsize>(chars.size()));
    if (!file) return std::unexpected("could not write all of " + path.string());
    return {};
}

[[nodiscard]] std::expected<void, std::string> writeText(const std::filesystem::path& path,
                                                         std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return writeFile(path, bytes);
}

// The five files of one probe, by name, in one directory.
struct ProbeFiles {
    std::string dump;
    std::string png8;
    std::string png16;
    std::string exr;
    std::string sidecar;
};

[[nodiscard]] ProbeFiles filesOf(std::string_view probe) {
    const std::string name(probe);
    return {
        .dump = name + ".hdr.f32",
        .png8 = name + ".png",
        .png16 = name + ".16.png",
        .exr = name + ".exr",
        .sidecar = name + ".txt",
    };
}

// Everything the sidecar needs that a run gathers as it goes.
struct RunRecord {
    gfx::Probe probe;
    gfx::ProbeConditions conditions;
    std::filesystem::path outDirectory;
    ProbeFiles files;
    std::optional<gfx::DeviceDescription> device; // absent if no device was made
    std::vector<std::string_view> written;        // names of the files written
};

// The sidecar, written last and in every case (register decision 193).
[[nodiscard]] std::expected<void, std::string> writeSidecar(const RunRecord& run,
                                                            std::string_view outcome) {
    const gfx::DeviceDescription none{};
    const gfx::DeviceDescription& device = run.device ? *run.device : none;
    const std::string date = runDateUtc();
    const std::string compiler = compilerName();
    const std::string text = view::formatSidecar({
        .probe = run.probe.name,
        .description = run.probe.description,
        .outcome = outcome,
        .epoch = run.conditions.epoch,
        .camera = run.conditions.camera,
        .qualityPreset = run.conditions.qualityName,
        .exposure = run.conditions.exposure,
        .device =
            {
                .name = device.name,
                .vendorId = device.vendorId,
                .deviceId = device.deviceId,
                .driverName = device.driverName,
                .driverInfo = device.driverInfo,
                .driverVersion = device.driverVersion,
                .apiVersion = device.apiVersion,
            },
        .build = {.configuration = kBuildConfiguration, .compiler = compiler},
        .runDateUtc = date,
        .files = run.written,
    });
    return writeText(run.outDirectory / run.files.sidecar, text);
}

// A run that stopped before it had a frame: the sidecar alone, carrying why.
[[nodiscard]] int failBeforeFrame(const RunRecord& run, std::string_view what) {
    std::print(stderr, "orbsim: probe {}: {}\n", run.probe.name, what);
    if (auto written = writeSidecar(run, "failed: " + std::string(what)); !written) {
        std::print(stderr, "orbsim: {}\n", written.error());
    }
    return kExitFailure;
}

// One file's bytes, or the encoder's refusal, written and recorded.
[[nodiscard]] bool
writeEncoded(RunRecord& run,
             const std::string& name,
             const std::expected<std::vector<std::byte>, view::ImageFileError>& encoded) {
    if (!encoded) {
        std::print(stderr, "orbsim: {}: {}\n", name, encoded.error().message);
        return false;
    }
    if (auto ok = writeFile(run.outDirectory / name, *encoded); !ok) {
        std::print(stderr, "orbsim: {}\n", ok.error());
        return false;
    }
    run.written.emplace_back(name);
    return true;
}

// The four image files of a rendered frame. Each is attempted whatever
// happened to the others, so a failure in one leaves the rest to look at.
[[nodiscard]] bool writeFrameFiles(RunRecord& run, const gfx::OffscreenFrame& frame) {
    const view::ImageSize size = view::kProbeImageSize;
    bool all = writeEncoded(run, run.files.dump, view::encodeHdrDump(size, frame.hdrHalf));
    all = writeEncoded(run, run.files.png8, view::encodePng8(size, frame.display8)) && all;
    all = writeEncoded(run, run.files.png16, view::encodePng16(size, frame.display16)) && all;
    const std::string comment =
        std::format("orbsim probe {}: linear radiance in W/(m^2 sr), Rec. 709 primaries, D65 white",
                    run.probe.name);
    if (auto ok = view::writeExr(run.outDirectory / run.files.exr, size, frame.hdrHalf, comment);
        ok) {
        run.written.emplace_back(run.files.exr);
    } else {
        std::print(stderr, "orbsim: {}: {}\n", run.files.exr, ok.error().message);
        all = false;
    }
    return all;
}

} // namespace

int runProbe(SDL_Window* window,
             const ProbeRequest& request,
             std::atomic<std::uint32_t>& validationErrors) {
    const std::optional<gfx::Probe> probe = gfx::findProbe(request.probe);
    if (!probe) {
        std::print(stderr, "orbsim: no probe named '{}'\n", request.probe);
        return kExitUsage;
    }
    RunRecord run{
        .probe = *probe,
        .conditions = probe->conditions(),
        .outDirectory = request.outDirectory,
        .files = filesOf(probe->name),
        .device = std::nullopt,
        .written = {},
    };
    std::error_code error;
    std::filesystem::create_directories(run.outDirectory, error);
    if (error) {
        std::print(
            stderr, "orbsim: cannot create {}: {}\n", run.outDirectory.string(), error.message());
        return kExitFailure;
    }

    auto created = gfx::VulkanContext::create(window, request.validation, validationErrors);
    if (!created) return failBeforeFrame(run, created.error().message);
    gfx::VulkanContext gfx = std::move(*created);
    run.device = gfx.deviceDescription();
    std::print("GPU: {}\n", gfx.deviceName());

    auto scene = probe->createScene(gfx, request.shaderDirectory, run.conditions);
    if (!scene) return failBeforeFrame(run, scene.error().message);
    const view::PerRadiance exposure =
        view::radianceExposure(view::exposureValue100(run.conditions.exposure));
    auto resolve8 =
        gfx::ResolvePass::create(gfx, request.shaderDirectory, exposure, gfx::kProbeDisplayFormat8);
    if (!resolve8) return failBeforeFrame(run, resolve8.error().message);
    auto resolve16 = gfx::ResolvePass::create(
        gfx, request.shaderDirectory, exposure, gfx::kProbeDisplayFormat16);
    if (!resolve16) return failBeforeFrame(run, resolve16.error().message);
    // Declared after everything the frame uses, so destroyed before any of it.
    const gfx::DeviceIdleGuard idleBeforeTeardown{gfx.device()};

    const auto frame = gfx.renderOffscreen(
        {.width = view::kProbeImageSize.width, .height = view::kProbeImageSize.height},
        [&scene](VkCommandBuffer cmd) { scene->record(cmd); },
        {.eightBit = &*resolve8, .sixteenBit = &*resolve16});
    if (!frame) return failBeforeFrame(run, frame.error().message);

    const bool allWritten = writeFrameFiles(run, *frame);
    const auto sidecar = writeSidecar(run, allWritten ? "rendered" : "rendered; a file failed");
    if (!sidecar) std::print(stderr, "orbsim: {}\n", sidecar.error());
    for (const std::string_view name : run.written) {
        std::print("wrote {}\n", (run.outDirectory / name).string());
    }
    return allWritten && sidecar ? 0 : kExitFailure;
}

void listProbes() {
    for (const gfx::Probe& probe : gfx::kProbes) {
        std::print("{}\n    {}\n", probe.name, probe.description);
    }
}

} // namespace orb::app
