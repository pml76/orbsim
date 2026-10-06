#include "app/ProbeMode.hpp" // SF.5: own header, first
#include "app/ExitCodes.hpp"
#include "core/Time.hpp"
#include "render/Probes.hpp"
#include "render/ResolvePass.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Exposure.hpp"
#include "view/FileWrite.hpp"
#include "view/GoldenPath.hpp"
#include "view/ImageCompare.hpp"
#include "view/ImageFiles.hpp"
#include "view/Lambert.hpp"
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
#include <iterator>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
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

[[nodiscard]] std::expected<void, std::string> writeText(const std::filesystem::path& path,
                                                         std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return view::writeFile(path, bytes);
}

// A file's bytes, or why they could not be read -- the other direction of
// view::writeFile, through std::filesystem::path for the same reason (M1-17).
[[nodiscard]] std::expected<std::vector<std::byte>, std::string>
readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected("cannot open " + path.string());
    const std::vector<char> chars{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
    if (file.bad()) return std::unexpected("could not read all of " + path.string());
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

// The five files of one probe, by name, in one directory, and the sixth a
// golden mismatch adds (M1-17, register decision 231).
struct ProbeFiles {
    std::string dump;
    std::string png8;
    std::string png16;
    std::string exr;
    std::string sidecar;
    std::string diff;
};

[[nodiscard]] ProbeFiles filesOf(std::string_view probe) {
    const std::string name(probe);
    return {
        .dump = name + ".hdr.f32",
        .png8 = name + ".png",
        .png16 = name + ".16.png",
        .exr = name + ".exr",
        .sidecar = name + ".txt",
        .diff = name + ".diff.png",
    };
}

// Everything the sidecar needs that a run gathers as it goes.
struct RunRecord {
    gfx::Probe probe;
    gfx::ProbeConditions conditions;
    std::filesystem::path outDirectory;
    ProbeFiles files;
    std::optional<gfx::DeviceDescription> device; // absent if no device was made
    // The validation layer the instance ran with (M1-109); absent without one.
    std::optional<gfx::LayerVersion> validationLayer;
    std::vector<std::string_view> written; // names of the files written
    // What happened with the golden, for the sidecar (register decision 237).
    // Under --golden-dir both name the directory until the device is open,
    // and then the card's own file (decision 289).
    std::string goldenPath;           // empty when no golden was named
    std::filesystem::path goldenFile; // the same, as a path to open
    std::string goldenVerdict;
    std::optional<view::ImageDifference> goldenDifference;
    // The shader files the context was asked for (M1-109), copied from it
    // before each sidecar is written, since the context does not outlive
    // the run's every exit.
    std::vector<std::string> shaders;
};

// A lambert probe's light and surface for its sidecar, or none (M1-18).
[[nodiscard]] std::optional<view::LambertScene>
lambertSceneOf(const gfx::ProbeConditions& conditions) {
    if (const auto* scene = std::get_if<view::LambertScene>(&conditions.picture)) return *scene;
    return std::nullopt;
}

// The UT1 a probe turned the Earth to, for its sidecar, or none (M1-20,
// register decision 323).
[[nodiscard]] std::optional<Ut1Time> ut1Of(const gfx::ProbeConditions& conditions) {
    if (const auto* grid = std::get_if<gfx::GridPicture>(&conditions.picture)) return grid->ut1;
    return std::nullopt;
}

// The validation layer for the sidecar, in its own layer's type (M1-109).
[[nodiscard]] std::optional<view::SidecarLayer> layerOf(const RunRecord& run) {
    if (!run.validationLayer) return std::nullopt;
    return view::SidecarLayer{
        .specVersion = run.validationLayer->specVersion,
        .implementationVersion = run.validationLayer->implementationVersion,
    };
}

// The sidecar, written last and in every case (register decision 193).
[[nodiscard]] std::expected<void, std::string> writeSidecar(const RunRecord& run,
                                                            std::string_view outcome) {
    const gfx::DeviceDescription none{};
    const gfx::DeviceDescription& device = run.device ? *run.device : none;
    const std::string date = runDateUtc();
    const std::string compiler = compilerName();
    const std::vector<std::string_view> shaders(run.shaders.begin(), run.shaders.end());
    const std::string text = view::formatSidecar({
        .probe = run.probe.name,
        .description = run.probe.description,
        .outcome = outcome,
        .golden =
            {
                .path = run.goldenPath,
                .verdict = run.goldenVerdict,
                .difference = run.goldenDifference,
            },
        .epoch = run.conditions.epoch,
        .ut1 = ut1Of(run.conditions),
        .camera = run.conditions.camera,
        .qualityPreset = run.conditions.qualityName,
        .exposure = run.conditions.exposure,
        .scene = lambertSceneOf(run.conditions),
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
        .shaders = shaders,
        .validationLayer = layerOf(run),
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

// The same, as a probe run's whole outcome.
[[nodiscard]] ProbeOutcome stopBeforeFrame(const RunRecord& run, std::string_view what) {
    return {.exitCode = failBeforeFrame(run, what), .toAccept = std::nullopt};
}

// The shader files the context was asked for, into the run's record (M1-109):
// every sidecar written once the context exists names them, whichever exit
// writes it.
void noteShaders(RunRecord& run, const gfx::VulkanContext& gfx) {
    run.shaders.assign(gfx.shadersRequested().begin(), gfx.shadersRequested().end());
}

// stopBeforeFrame, once the context exists.
[[nodiscard]] ProbeOutcome
stopAfterContext(RunRecord& run, const gfx::VulkanContext& gfx, std::string_view what) {
    noteShaders(run, gfx);
    return stopBeforeFrame(run, what);
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
    if (auto ok = view::writeFile(run.outDirectory / name, *encoded); !ok) {
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

// --- the golden (M1-17; ADR 0008; register decisions 228-237) ----------------

// The golden, read and decoded, or why not -- in words that name the file.
[[nodiscard]] std::expected<view::GoldenImage, std::string>
loadGolden(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto image = view::decodePng8(*bytes);
    if (!image) {
        return std::unexpected(std::format("{}: {} ({})",
                                           path.string(),
                                           view::describe(image.error().error),
                                           image.error().detail));
    }
    return view::GoldenImage{*std::move(image)};
}

// What the golden step leaves: an exit code, and the frame to accept.
struct GoldenStep {
    int exitCode{};
    std::optional<GoldenToAccept> toAccept;
};

// With --accept-golden, anything short of a comparison is a note rather than
// a failure: the golden is being replaced, and a missing or unreadable one is
// exactly what a first acceptance, or a repair, looks like.
[[nodiscard]] GoldenStep
acceptWithoutComparing(RunRecord& run, view::Rgb8Image halved, std::string_view why) {
    std::print(
        "golden {}: nothing to compare with ({}); accepting replaces it\n", run.goldenPath, why);
    run.goldenVerdict = "accept requested; nothing to compare with: " + std::string(why);
    return {
        .exitCode = 0,
        .toAccept = GoldenToAccept{.frame = std::move(halved), .file = run.goldenFile},
    };
}

// A comparison that could not be made, which a run asked to compare fails on
// with exit 1: exit 4 is kept for a measured mismatch (decision 230).
[[nodiscard]] GoldenStep cannotCompare(RunRecord& run, std::string_view why) {
    std::print(
        stderr, "orbsim: probe {}: cannot compare with the golden: {}\n", run.probe.name, why);
    run.goldenVerdict = "failed: " + std::string(why);
    return {.exitCode = kExitFailure, .toAccept = std::nullopt};
}

// A card with no golden of this probe, under --golden-dir (decisions 287 and
// 289): exit 1, never a comparison skipped (VERIFICATION.md rule 23), with
// the card named by its numbers and the command that approves one. The five
// files are written already, so the frame can be looked at first.
[[nodiscard]] GoldenStep noGoldenForCard(RunRecord& run, const ProbeRequest& request) {
    const gfx::DeviceDescription none{};
    const gfx::DeviceDescription& device = run.device ? *run.device : none;
    std::print(stderr,
               "orbsim: probe {}: no golden for this graphics card, {} (Vulkan {}): {} does not "
               "exist\n"
               "orbsim: look at {}; if it is right, the owner approves it with\n"
               "    orbsim --probe {} --golden-dir {} --accept-golden\n",
               run.probe.name,
               device.name,
               view::goldenFolderName({.vendorId = device.vendorId, .deviceId = device.deviceId}),
               run.goldenPath,
               (run.outDirectory / run.files.png8).string(),
               run.probe.name,
               request.goldenPath.string());
    run.goldenVerdict = "failed: no golden for this graphics card";
    return {.exitCode = kExitFailure, .toAccept = std::nullopt};
}

// A measured mismatch: both numbers on stderr, the diff image written, exit 4.
[[nodiscard]] GoldenStep reportMismatch(RunRecord& run,
                                        const view::Rgb8Image& halved,
                                        const view::GoldenImage& golden,
                                        const view::ImageDifference& difference) {
    std::print(stderr,
               "orbsim: probe {}: the frame does not match its golden {}\n{}",
               run.probe.name,
               run.goldenPath,
               view::describeDifference(difference));
    run.goldenVerdict = "mismatch";
    const auto diff = view::differenceImage(halved, golden);
    if (!diff) return cannotCompare(run, view::describe(diff.error()));
    if (!writeEncoded(run, run.files.diff, view::encodePng8(*diff))) {
        return {.exitCode = kExitFailure, .toAccept = std::nullopt};
    }
    return {.exitCode = kExitGoldenMismatch, .toAccept = std::nullopt};
}

// The frame, halved in linear light, held against the golden (decisions 228
// to 231), with both numbers printed whichever way it goes.
[[nodiscard]] GoldenStep
compareWithGolden(RunRecord& run, const ProbeRequest& request, const gfx::OffscreenFrame& frame) {
    if (request.golden == GoldenAction::None) return {};
    const bool accepting = request.golden == GoldenAction::Accept;
    auto halved =
        view::halveInLinearLight(view::Rgb8Image::fromRgba(view::kProbeImageSize, frame.display8));
    if (!halved) return cannotCompare(run, view::describe(halved.error()));

    if (request.goldenLocation == GoldenLocation::CardFolders && !accepting) {
        // An error asking leaves the answer to loadGolden, which names it.
        std::error_code error;
        if (!std::filesystem::exists(run.goldenFile, error) && !error) {
            return noGoldenForCard(run, request);
        }
    }
    const auto golden = loadGolden(run.goldenFile);
    if (!golden) {
        return accepting ? acceptWithoutComparing(run, *std::move(halved), golden.error())
                         : cannotCompare(run, golden.error());
    }
    const auto difference = view::measureDifference(*halved, *golden);
    if (!difference) {
        const std::string why = std::format("{}: the golden is {}x{} and the frame halves to {}x{}",
                                            view::describe(difference.error()),
                                            golden->size().width,
                                            golden->size().height,
                                            halved->size().width,
                                            halved->size().height);
        return accepting ? acceptWithoutComparing(run, *std::move(halved), why)
                         : cannotCompare(run, why);
    }
    run.goldenDifference = *difference;

    if (accepting) {
        std::print("golden {}: the frame to be accepted differs from it by\n{}",
                   run.goldenPath,
                   view::describeDifference(*difference));
        run.goldenVerdict = "accept requested";
        return {
            .exitCode = 0,
            .toAccept = GoldenToAccept{.frame = *std::move(halved), .file = run.goldenFile},
        };
    }
    if (view::matchesGolden(*difference)) {
        std::print("golden {}: matches\n{}", run.goldenPath, view::describeDifference(*difference));
        run.goldenVerdict = "matches";
        return {};
    }
    return reportMismatch(run, *halved, *golden, *difference);
}

// Under --golden-dir, the golden is this card's own, which only the device
// just opened can say (decision 289): from here on the run compares with, and
// accepts into, `<dir>/<vendor>-<device>/<probe>.png`.
void resolveCardGolden(RunRecord& run, const ProbeRequest& request) {
    if (request.golden == GoldenAction::None) return;
    if (request.goldenLocation != GoldenLocation::CardFolders || !run.device) return;
    run.goldenFile =
        view::goldenPathFor(request.goldenPath,
                            {.vendorId = run.device->vendorId, .deviceId = run.device->deviceId},
                            run.probe.name);
    run.goldenPath = run.goldenFile.string();
}

} // namespace

ProbeOutcome runProbe(SDL_Window* window,
                      const ProbeRequest& request,
                      std::atomic<std::uint32_t>& validationErrors) {
    const std::optional<gfx::Probe> probe = gfx::findProbe(request.probe);
    if (!probe) {
        std::print(stderr, "orbsim: no probe named '{}'\n", request.probe);
        return {.exitCode = kExitUsage, .toAccept = std::nullopt};
    }
    const bool withGolden = request.golden != GoldenAction::None;
    RunRecord run{
        .probe = *probe,
        .conditions = probe->conditions(),
        .outDirectory = request.outDirectory,
        .files = filesOf(probe->name),
        .device = std::nullopt,
        .validationLayer = std::nullopt,
        .written = {},
        .goldenPath = withGolden ? request.goldenPath.string() : std::string{},
        .goldenFile = withGolden ? request.goldenPath : std::filesystem::path{},
        .goldenVerdict =
            withGolden ? "not compared: the run stopped before a frame existed" : "not compared",
        .goldenDifference = std::nullopt,
        .shaders = {},
    };
    std::error_code error;
    std::filesystem::create_directories(run.outDirectory, error);
    if (error) {
        std::print(
            stderr, "orbsim: cannot create {}: {}\n", run.outDirectory.string(), error.message());
        return {.exitCode = kExitFailure, .toAccept = std::nullopt};
    }
    // A diff image from an earlier run is removed before anything else, so
    // one can never be taken for this run's (decision 231).
    std::filesystem::remove(run.outDirectory / run.files.diff, error);
    if (error) {
        return stopBeforeFrame(
            run, "cannot remove the earlier " + run.files.diff + ": " + error.message());
    }

    // Paced, as the window is: a probe presents nothing, so the choice only
    // has to be the window's, for one device path rather than two (ADR 0008).
    auto created = gfx::VulkanContext::create(
        window, request.validation, validationErrors, gfx::Presentation::Paced);
    if (!created) return stopBeforeFrame(run, created.error().message);
    gfx::VulkanContext gfx = std::move(*created);
    run.device = gfx.deviceDescription();
    run.validationLayer = gfx.validationLayer();
    std::print("GPU: {}\n", gfx.deviceName());
    resolveCardGolden(run, request);

    auto scene = gfx::createScene(gfx, request.shaderDirectory, run.conditions);
    if (!scene) return stopAfterContext(run, gfx, scene.error().message);
    const view::PerRadiance exposure =
        view::radianceExposure(view::exposureValue100(run.conditions.exposure));
    auto resolve8 =
        gfx::ResolvePass::create(gfx, request.shaderDirectory, exposure, gfx::kProbeDisplayFormat8);
    if (!resolve8) return stopAfterContext(run, gfx, resolve8.error().message);
    auto resolve16 = gfx::ResolvePass::create(
        gfx, request.shaderDirectory, exposure, gfx::kProbeDisplayFormat16);
    if (!resolve16) return stopAfterContext(run, gfx, resolve16.error().message);
    // Declared after everything the frame uses, so destroyed before any of it.
    const gfx::DeviceIdleGuard idleBeforeTeardown{gfx.device()};

    const auto frame = gfx.renderOffscreen(
        {.width = view::kProbeImageSize.width, .height = view::kProbeImageSize.height},
        [&scene](VkCommandBuffer cmd) { scene->record(cmd); },
        {.eightBit = &*resolve8, .sixteenBit = &*resolve16});
    if (!frame) return stopAfterContext(run, gfx, frame.error().message);
    noteShaders(run, gfx);

    const bool allWritten = writeFrameFiles(run, *frame);
    GoldenStep golden = compareWithGolden(run, request, *frame);
    const auto sidecar = writeSidecar(run, allWritten ? "rendered" : "rendered; a file failed");
    if (!sidecar) std::print(stderr, "orbsim: {}\n", sidecar.error());
    for (const std::string_view name : run.written) {
        std::print("wrote {}\n", (run.outDirectory / name).string());
    }
    // A file that failed outranks a mismatch (decision 230), and a frame whose
    // files are not all there is never accepted (decision 232).
    if (!allWritten || !sidecar) return {.exitCode = kExitFailure, .toAccept = std::nullopt};
    return {.exitCode = golden.exitCode, .toAccept = std::move(golden.toAccept)};
}

int acceptGolden(const GoldenToAccept& accepted) {
    const std::filesystem::path& golden = accepted.file;
    const auto png = view::encodePng8(accepted.frame);
    if (!png) {
        std::print(stderr, "orbsim: --accept-golden: {}\n", png.error().message);
        return kExitFailure;
    }
    std::error_code error;
    if (golden.has_parent_path()) std::filesystem::create_directories(golden.parent_path(), error);
    if (error) {
        std::print(stderr,
                   "orbsim: --accept-golden: cannot create {}: {}\n",
                   golden.parent_path().string(),
                   error.message());
        return kExitFailure;
    }
    // Written beside the golden and renamed over it, so a failed write never
    // leaves half of one (decision 232; view/FileWrite.hpp since M1-108).
    if (auto replaced = view::replaceFile(golden, *png); !replaced) {
        std::print(stderr, "orbsim: --accept-golden: {}\n", replaced.error());
        return kExitFailure;
    }
    std::print("accepted: wrote {} as the approved frame. Commit it with the code that produces "
               "it, and say in the commit message what changed and why (ADR 0008).\n",
               golden.string());
    return 0;
}

void listProbes() {
    for (const gfx::Probe& probe : gfx::kProbes) {
        std::print("{}\n    {}\n", probe.name, probe.description);
    }
}

} // namespace orb::app
