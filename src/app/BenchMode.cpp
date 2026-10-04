#include "app/BenchMode.hpp" // SF.5: own header, first
#include "app/ExitCodes.hpp"
#include "app/InteractiveView.hpp"
#include "core/Attributes.hpp"
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "render/Pipeline.hpp"
#include "render/ResolvePass.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/BenchmarkPath.hpp"
#include "view/CameraPath.hpp"
#include "view/Exposure.hpp"
#include "view/FrameStatistics.hpp"
#include "view/GpuClock.hpp"
#include "view/RenderQuality.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
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

using Clock = std::chrono::steady_clock;

// The warm-up (register decisions 367 and 378): the path's first view, drawn
// until both of these have passed, and then discarded. Measured 2026-10-04
// over eight runs on the RX 7900 XTX and a Threadripper PRO 3955WX
// (docs/measurements/m1-22-warm-up.md): the GPU's figure settles within a
// second, and the CPU's run up to 6 % fast for 2.5 s, then slow, and stay
// within 1.8 % of steady from 4.0 s on, at the same moments in every run --
// the processor's power management, which takes a time rather than a number
// of frames. 5 s is that 4.0 s with a margin. The first three frames pay a
// one-off cost, and 30 is ten times that, for a scene whose frames are slow
// enough that 5 s holds few of them.
constexpr Seconds kWarmUpTime{5.0};
constexpr std::uint32_t kWarmUpMinimumFrames = 30;

constexpr auto kPathNames = std::to_array<std::string_view>({"grid-orbit"});

constexpr auto kQualityNames = std::to_array<std::string_view>({"low", "medium", "high", "ultra"});

// A stretch of the steady clock, its two ends by name (non-negotiable 1).
struct Span {
    Clock::time_point from;
    Clock::time_point to;
};

[[nodiscard]] Seconds secondsOf(const Span& span) {
    return Seconds{std::chrono::duration<f64>(span.to - span.from).count()};
}

// A failure the run stops on, with what to print.
struct BenchError {
    std::string message;
};

[[nodiscard]] std::unexpected<BenchError> fail(std::string message) {
    return std::unexpected(BenchError{.message = std::move(message)});
}

// The path a name stands for. The names were checked by main.
[[nodiscard]] view::CameraPath pathNamed([[maybe_unused]] std::string_view name) {
    ORBSIM_EXPECTS(name == kPathNames.front());
    return view::gridOrbitPath(gridWorldFromEarthFixed());
}

// Sizes the window so its drawn image is `size` physical pixels (decision
// 370). The window was made `size` window units across; on a display that
// scales, a unit is not a pixel, so it is resized by the measured ratio and
// the result checked rather than assumed. Run 2026-10-04 on a display at
// 150 % scaling, it gave a swapchain of 1920x1080 exactly.
[[nodiscard]] std::expected<void, BenchError> fitWindow(SDL_Window* window, ImageSize size) {
    int unitsWide = 0;
    int unitsHigh = 0;
    int pixelsWide = 0;
    int pixelsHigh = 0;
    if (!SDL_GetWindowSize(window, &unitsWide, &unitsHigh) ||
        !SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh) || pixelsWide <= 0 ||
        pixelsHigh <= 0) {
        return fail(std::string("cannot read the window's size: ") + SDL_GetError());
    }
    if (std::cmp_not_equal(pixelsWide, size.width) || std::cmp_not_equal(pixelsHigh, size.height)) {
        // Window units a pixel, measured across and down.
        const f64 acrossRatio = static_cast<f64>(unitsWide) / static_cast<f64>(pixelsWide);
        const f64 downRatio = static_cast<f64>(unitsHigh) / static_cast<f64>(pixelsHigh);
        const auto unitsWanted = [](f64 pixels, f64 ratio) {
            return static_cast<int>(std::lround(pixels * ratio));
        };
        if (!SDL_SetWindowSize(window,
                               unitsWanted(static_cast<f64>(size.width), acrossRatio),
                               unitsWanted(static_cast<f64>(size.height), downRatio)) ||
            !SDL_SyncWindow(window) ||
            !SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh)) {
            return fail(std::string("cannot size the window: ") + SDL_GetError());
        }
    }
    if (std::cmp_not_equal(pixelsWide, size.width) || std::cmp_not_equal(pixelsHigh, size.height)) {
        return fail(std::format("the window draws {}x{} pixels, not the {}x{} asked for",
                                pixelsWide,
                                pixelsHigh,
                                size.width,
                                size.height));
    }
    return {};
}

// Which phase of the run a frame belongs to. An enum, not a bool.
enum class Phase : std::uint8_t {
    WarmUp,
    Measured,
};

// One frame as it is drawn, before the GPU has reported it.
struct DrawnFrame {
    Phase phase{Phase::Measured};
    Seconds pathTime{0.0};
    Clock::time_point start;
    Seconds waited{0.0}; // for the GPU and the display, on the CPU
    std::optional<Seconds> gpu;
};

// One frame once the run is over: every figure known. A separate type, so a
// frame without a GPU time cannot reach the report at all.
struct TimedFrame {
    Phase phase{Phase::Measured};
    Seconds pathTime{0.0};
    Seconds interval{0.0}; // from its start to the next frame's
    Seconds working{0.0};  // the interval less the waits
    Seconds gpu{0.0};
};

// What the frame loop draws with, by name, as main's FrameDrawing: observers,
// each outliving the run.
struct BenchScene {
    const gfx::ScenePipelines* pipelines{};
    const gfx::ResolvePass* resolve{};
    InteractiveView* view{};
};

// The frame loop, one frame at a time, keeping every frame as it goes.
class BenchRun {
public:
    BenchRun(gfx::VulkanContext& gfx ORBSIM_LIFETIMEBOUND,
             const BenchScene& scene,
             const view::GpuClock& clock,
             view::RenderQuality quality)
        : gfx_(&gfx),
          scene_(scene),
          clock_(clock),
          quality_(quality),
          builds_(gfx.swapchainBuilds()) {}

    // Draws one frame of `phase`, from where the path is at `pathTime`.
    [[nodiscard]] std::expected<void, BenchError>
    draw(Phase phase, const view::CameraPath& path, Seconds pathTime) {
        if (auto quit = pumpEvents(); !quit) return quit;
        const Clock::time_point start = Clock::now();
        auto begun = gfx_->beginFrame(quality_);
        if (!begun) return fail("a frame could not begin: " + begun.error().message);
        // The window minimised, or its image rebuilt: either changes what is
        // being measured, so the run stops (decision 377).
        if (!*begun || gfx_->swapchainBuilds() != builds_) {
            return fail("the window was minimised or resized during the run");
        }
        const gfx::FrameContext& frame = **begun;
        ORBSIM_ENSURES(frame.number == drawn_.size());
        record(frame.completed);

        const auto pose = path.poseAt(pathTime);
        // Every time asked for lies on the path (view/BenchmarkPath.hpp).
        ORBSIM_ENSURES(pose.has_value());
        if (auto made = scene_.view->recordFrom(*pose, *gfx_, frame, *scene_.pipelines); !made) {
            return fail("a frame could not be drawn: " + made.error().message);
        }
        const auto presented = gfx_->endFrame(frame, *scene_.resolve);
        if (!presented) return fail("a frame could not be presented: " + presented.error().message);
        drawn_.push_back({
            .phase = phase,
            .pathTime = pathTime,
            .start = start,
            .waited = frame.waited + *presented,
            .gpu = std::nullopt,
        });
        return {};
    }

    // Waits for the GPU to finish, collects the last frames' times, and
    // hands back every frame with all its figures.
    [[nodiscard]] std::expected<std::vector<TimedFrame>, BenchError> finish() {
        const Clock::time_point end = Clock::now();
        auto last = gfx_->drainCompletedFrames();
        if (!last) return fail("the last frames could not be read: " + last.error().message);
        for (const gfx::CompletedFrame& completed : *last) {
            record(completed);
        }

        std::vector<TimedFrame> timed;
        timed.reserve(drawn_.size());
        for (std::size_t i = 0; i < drawn_.size(); ++i) {
            const DrawnFrame& frame = drawn_.at(i);
            if (!frame.gpu) return fail(std::format("frame {} has no GPU time", i));
            // Each frame's interval runs to the next frame's start, and the
            // last frame's to the end of the run.
            const Clock::time_point next = i + 1 < drawn_.size() ? drawn_.at(i + 1).start : end;
            const Seconds interval = secondsOf({.from = frame.start, .to = next});
            timed.push_back({
                .phase = frame.phase,
                .pathTime = frame.pathTime,
                .interval = interval,
                .working = interval - frame.waited,
                .gpu = *frame.gpu,
            });
        }
        return timed;
    }

private:
    // Escape or closing the window stops the run: what was measured is not
    // the run asked for.
    [[nodiscard]] static std::expected<void, BenchError> pumpEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                return fail("the run was stopped before it finished");
            }
        }
        return {};
    }

    void record(const std::optional<gfx::CompletedFrame>& completed) {
        if (!completed) return;
        drawn_.at(completed->number).gpu = clock_.elapsed(completed->ticks);
    }

    gfx::VulkanContext* gfx_; // observer: runBench owns it, and it outlives the run
    BenchScene scene_;
    view::GpuClock clock_;
    view::RenderQuality quality_;
    std::uint64_t builds_;
    std::vector<DrawnFrame> drawn_;
};

// The warm-up, then the measured frames, then every frame's figures.
[[nodiscard]] std::expected<std::vector<TimedFrame>, BenchError>
flyPath(BenchRun& run, const view::CameraPath& path, std::uint32_t frames) {
    const Seconds first = path.keyframes().front().time;
    const Clock::time_point warmStart = Clock::now();
    for (std::uint32_t k = 0; k < kWarmUpMinimumFrames ||
                              secondsOf({.from = warmStart, .to = Clock::now()}) < kWarmUpTime;
         ++k) {
        if (auto drawn = run.draw(Phase::WarmUp, path, first); !drawn) {
            return std::unexpected(drawn.error());
        }
    }
    for (std::uint32_t k = 0; k < frames; ++k) {
        const Seconds at = view::timeOfFrame(path, {.index = k, .count = frames});
        if (auto drawn = run.draw(Phase::Measured, path, at); !drawn) {
            return std::unexpected(drawn.error());
        }
    }
    return run.finish();
}

// --- the report --------------------------------------------------------------

// The three series of the measured frames (decision 365).
struct Series {
    std::vector<Seconds> interval;
    std::vector<Seconds> working;
    std::vector<Seconds> gpu;
};

[[nodiscard]] Series measuredSeries(const std::vector<TimedFrame>& frames) {
    Series series;
    for (const TimedFrame& frame : frames) {
        if (frame.phase != Phase::Measured) continue;
        series.interval.push_back(frame.interval);
        series.working.push_back(frame.working);
        series.gpu.push_back(frame.gpu);
    }
    return series;
}

// Formatted rather than a literal in every branch, so that one function
// serves the three compilers this project builds with.
[[nodiscard]] std::string compilerName() {
#ifdef __clang__
    return std::format("clang {}", __clang_version__);
#elifdef _MSC_VER
    return std::format("MSVC {}", _MSC_FULL_VER);
#elifdef __GNUC__
    return std::format("gcc {}", __VERSION__);
#else
    return std::format("an unknown compiler");
#endif
}

[[nodiscard]] std::string refreshRateOf(SDL_Window* window) {
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    if (mode == nullptr || !(mode->refresh_rate > 0.0F)) return "unknown";
    return std::format("{:.2f} Hz", static_cast<f64>(mode->refresh_rate));
}

// Everything a frame time is measured under (decision 372's labels among it).
struct Conditions {
    std::string_view path;
    std::uint32_t frames{};
    std::size_t warmUpFrames{};
    Seconds warmUpTime{0.0};
    gfx::DeviceDescription device;
    std::string presentMode;
    std::string refreshRate;
    VkExtent2D extent{};
    std::string_view quality;
    gfx::Validation validation{gfx::Validation::Disabled};
};

[[nodiscard]] std::string conditionsText(const Conditions& c) {
    std::string text = std::format(
        "orbsim benchmark: {}, {} measured frames after a warm-up of {} frames in {:.2f} s\n"
        "GPU:          {} ({:04x}-{:04x}), {} {}, Vulkan {}.{}.{}\n"
        "Image:        {}x{} pixels, {}, display refreshing at {}\n"
        "Quality:      {}\n"
        "Build:        {}, {}\n"
        "Validation:   {}\n",
        c.path,
        c.frames,
        c.warmUpFrames,
        c.warmUpTime.value(),
        c.device.name,
        c.device.vendorId,
        c.device.deviceId,
        c.device.driverName,
        c.device.driverInfo,
        VK_API_VERSION_MAJOR(c.device.apiVersion),
        VK_API_VERSION_MINOR(c.device.apiVersion),
        VK_API_VERSION_PATCH(c.device.apiVersion),
        c.extent.width,
        c.extent.height,
        c.presentMode,
        c.refreshRate,
        c.quality,
        ORBSIM_BUILD_CONFIG,
        compilerName(),
        c.validation == gfx::Validation::Enabled ? "on" : "off");
    // Decision 372: figures taken under the layers, or with assertions
    // compiled in, are labelled as what they are.
    if (c.validation == gfx::Validation::Enabled) {
        text += "NOTE: the validation layers were on; these figures are not representative.\n";
    }
#ifndef NDEBUG
    text += "NOTE: assertions are compiled in; these figures are not representative.\n";
#endif
    return text;
}

[[nodiscard]] f64 milliseconds(Seconds s) { return s.value() * 1e3; }

// One line of the table: the five figures of one series, in milliseconds.
[[nodiscard]] std::expected<std::string, BenchError>
statisticsLine(std::string_view name, std::span<const Seconds> sample) {
    const auto summary = view::summarise(sample);
    if (!summary) return fail(std::format("{}: {}", name, view::describe(summary.error())));
    return std::format("{:<14}{:>10.4f}{:>10.4f}{:>10.4f}{:>10.4f}{:>10.4f}\n",
                       name,
                       milliseconds(summary->mean),
                       milliseconds(summary->median),
                       milliseconds(summary->p95),
                       milliseconds(summary->p99),
                       milliseconds(summary->max));
}

[[nodiscard]] std::expected<std::string, BenchError> statisticsText(const Series& series) {
    const auto interval = statisticsLine("frame interval", series.interval);
    if (!interval) return std::unexpected(interval.error());
    const auto working = statisticsLine("CPU working", series.working);
    if (!working) return std::unexpected(working.error());
    const auto gpu = statisticsLine("GPU", series.gpu);
    if (!gpu) return std::unexpected(gpu.error());
    return std::format("\n{:<14}{:>10}{:>10}{:>10}{:>10}{:>10}   (ms)\n",
                       "",
                       "mean",
                       "median",
                       "p95",
                       "p99",
                       "max") +
           *interval + *working + *gpu;
}

// One row a frame, every number at full precision -- std::format's shortest
// text that reads back as the same double -- so a spike can be found rather
// than averaged away (decision 373).
[[nodiscard]] std::string csvText(const std::vector<TimedFrame>& frames) {
    std::string text = "frame,path_time_s,phase,interval_ms,cpu_working_ms,gpu_ms\n";
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const TimedFrame& frame = frames.at(i);
        text += std::format("{},{},{},{},{},{}\n",
                            i,
                            frame.pathTime.value(),
                            frame.phase == Phase::WarmUp ? "warm-up" : "measured",
                            milliseconds(frame.interval),
                            milliseconds(frame.working),
                            milliseconds(frame.gpu));
    }
    return text;
}

[[nodiscard]] std::expected<void, BenchError> writeFile(const std::filesystem::path& file,
                                                        std::string_view text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
    out.close();
    if (!out) return fail("cannot write " + file.string());
    return {};
}

// The report's two texts, by name, so they cannot be handed over the wrong
// way round (non-negotiable 1).
struct ReportTexts {
    std::string_view summary;
    std::string_view table;
};

// The two files, named for the path and the moment, in UTC. Returns the name
// both share, without its extension.
[[nodiscard]] std::expected<std::filesystem::path, BenchError>
writeReport(const BenchRequest& request, const ReportTexts& texts) {
    std::error_code error;
    std::filesystem::create_directories(request.outDirectory, error);
    if (error) {
        return fail("cannot create " + request.outDirectory.string() + ": " + error.message());
    }
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::filesystem::path base =
        request.outDirectory / std::format("{}-{:%Y%m%dT%H%M%SZ}", request.path, now);
    base.make_preferred();
    if (auto ok = writeFile(std::filesystem::path(base).replace_extension(".txt"), texts.summary);
        !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = writeFile(std::filesystem::path(base).replace_extension(".csv"), texts.table);
        !ok) {
        return std::unexpected(ok.error());
    }
    return base;
}

// The warm-up's frames and its length: the frames before the first measured
// one, and the sum of their intervals.
struct WarmUp {
    std::size_t frames{};
    Seconds time{0.0};
};

[[nodiscard]] WarmUp warmUpOf(const std::vector<TimedFrame>& frames) {
    WarmUp warmUp;
    for (const TimedFrame& frame : frames) {
        if (frame.phase != Phase::WarmUp) break;
        ++warmUp.frames;
        warmUp.time = warmUp.time + frame.interval;
    }
    return warmUp;
}

[[nodiscard]] std::expected<void, BenchError> report(SDL_Window* window,
                                                     const gfx::VulkanContext& gfx,
                                                     const BenchRequest& request,
                                                     const std::vector<TimedFrame>& frames) {
    const WarmUp warmUp = warmUpOf(frames);
    const std::string conditions = conditionsText({
        .path = request.path,
        .frames = request.frames,
        .warmUpFrames = warmUp.frames,
        .warmUpTime = warmUp.time,
        .device = gfx.deviceDescription(),
        .presentMode = std::string(gfx.presentModeName()),
        .refreshRate = refreshRateOf(window),
        .extent = gfx.extent(),
        .quality = request.qualityName,
        .validation = request.validation,
    });
    const auto statistics = statisticsText(measuredSeries(frames));
    if (!statistics) return std::unexpected(statistics.error());
    const std::string summary = conditions + *statistics;
    std::print("{}", summary);
    const std::string table = csvText(frames);
    const auto written = writeReport(request, {.summary = summary, .table = table});
    if (!written) return std::unexpected(written.error());
    std::print("\nWritten: {}.txt and .csv\n", written->string());
    return {};
}

// --- the run -------------------------------------------------------------------

// The device, checked: its image the size asked for.
[[nodiscard]] std::expected<gfx::VulkanContext, BenchError> openDevice(
    SDL_Window* window, const BenchRequest& request, std::atomic<std::uint32_t>& validationErrors) {
    auto created = gfx::VulkanContext::create(
        window, request.validation, validationErrors, gfx::Presentation::Unthrottled);
    if (!created) return fail("renderer initialisation failed: " + created.error().message);
    const VkExtent2D extent = created->extent();
    if (extent.width != request.size.width || extent.height != request.size.height) {
        return fail(std::format("the swapchain is {}x{}, not the {}x{} asked for",
                                extent.width,
                                extent.height,
                                request.size.width,
                                request.size.height));
    }
    return std::move(*created);
}

} // namespace

std::span<const std::string_view> benchPathNames() noexcept { return kPathNames; }

std::span<const std::string_view> qualityPresetNames() noexcept { return kQualityNames; }

std::optional<view::RenderQuality> qualityPreset(std::string_view name) noexcept {
    if (name == "low") return view::RenderQuality::low();
    if (name == "medium") return view::RenderQuality::medium();
    if (name == "high") return view::RenderQuality::high();
    if (name == "ultra") return view::RenderQuality::ultra();
    return std::nullopt;
}

int runBench(SDL_Window* window,
             const BenchRequest& request,
             std::atomic<std::uint32_t>& validationErrors) {
    const auto stop = [](const BenchError& error) {
        std::print(stderr, "orbsim: --bench: {}\n", error.message);
        return kExitFailure;
    };
    if (auto fitted = fitWindow(window, request.size); !fitted) return stop(fitted.error());
    auto opened = openDevice(window, request, validationErrors);
    if (!opened) return stop(opened.error());
    gfx::VulkanContext gfx = std::move(*opened);
    const std::optional<view::GpuClock> clock = gfx.gpuClock();
    if (!clock) {
        return stop({
            .message = "this GPU's graphics queue cannot record timestamps, so its frames "
                       "cannot be timed",
        });
    }

    // The window's scene, made as runRenderer in main.cpp makes it.
    const auto pipelines = gfx::ScenePipelines::create(gfx, request.shaderDirectory);
    if (!pipelines) return stop({.message = pipelines.error().message});
    const auto resolve =
        gfx::ResolvePass::create(gfx,
                                 request.shaderDirectory,
                                 view::radianceExposure(view::exposureValue100(kDefaultCamera)),
                                 gfx.swapchainFormat());
    if (!resolve) return stop({.message = resolve.error().message});
    auto view = InteractiveView::create(gfx);
    if (!view) return stop({.message = view.error().message});
    // Declared after everything a frame uses, so destroyed before any of it.
    const gfx::DeviceIdleGuard idleBeforeTeardown{gfx.device()};

    const view::CameraPath path = pathNamed(request.path);
    const BenchScene scene{
        .pipelines = &*pipelines,
        .resolve = &*resolve,
        .view = &*view,
    };
    BenchRun run{gfx, scene, *clock, request.quality};
    const auto frames = flyPath(run, path, request.frames);
    if (!frames) return stop(frames.error());
    if (auto reported = report(window, gfx, request, *frames); !reported) {
        return stop(reported.error());
    }
    return 0;
}

} // namespace orb::app
