#ifndef ORBSIM_APP_BENCHMODE_HPP
#define ORBSIM_APP_BENCHMODE_HPP
//
// `--bench <path>`: fly a scripted camera path over the window's scene, time
// every frame on the CPU and on the GPU, and report (M1-22; register
// decisions 364-377).
//
// **It is an instrument, not a test.** Nothing here, and nothing in `check`,
// asserts a frame time: a threshold on shared hardware fails for reasons that
// have nothing to do with the change (register decision 5). What `check`
// asserts is that the instrument runs cleanly (`orbsim_bench_smoke`).
//
// **Three series a frame** (decision 365): the interval from the start of the
// frame to the start of the next, which is the rate a person sees; the part
// of it the CPU spent working, which is the interval less the waits for the
// GPU and the display (gfx::FrameContext::waited and endFrame's return); and
// the GPU's own time, from timestamps either side of the frame's commands.
// A frame limited by the CPU and one limited by the GPU want different fixes,
// and one number cannot tell them apart.
//
// **The image is the window's frame** (decision 351): the Earth's grid and
// horizon, through the same pipelines, resolve pass and exposure, at
// `--width` x `--height` drawn pixels, through a swapchain that does not wait
// for the display where the device allows (gfx::Presentation::Unthrottled,
// decision 364). A window whose image comes out any other size, or is rebuilt
// mid-run, stops the run (decisions 370 and 377).
//
// **A warm-up first, discarded**: the path's first view, held until both a
// time and a number of frames have passed (decision 367; the two figures and
// where they come from are beside them in BenchMode.cpp). Then the measured
// frames, the whole path spread over them (view/BenchmarkPath.hpp).
//
// **What it writes** (decision 373): the summary on stdout, and the summary
// and a table of every frame -- warm-up frames marked -- to
// `<out>/<path>-<UTC time>.txt` and `.csv`.
//
#include "render/VulkanContext.hpp"
#include "view/RenderQuality.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

struct SDL_Window;

namespace orb::app {

// The drawn image's size, in physical pixels.
struct ImageSize {
    std::uint32_t width{};
    std::uint32_t height{};
};

// realism.md section 6.5's resolution, and the task's frame count.
inline constexpr ImageSize kBenchDefaultSize{.width = 1920, .height = 1080};
inline constexpr std::uint32_t kBenchDefaultFrames = 600;

// The most frames a run may ask for: about eight minutes at 2,000 frames a
// second and four and a half hours at 60, and 48 MB of rows. A bound so that
// a mistyped count is refused by name rather than met by an allocation that
// cannot succeed.
inline constexpr std::uint32_t kBenchMaximumFrames = 1'000'000;

// What a benchmark run is asked for.
struct BenchRequest {
    std::string_view path;                     // a name benchPathNames() lists
    std::uint32_t frames{kBenchDefaultFrames}; // 1 to kBenchMaximumFrames
    ImageSize size{kBenchDefaultSize};
    std::string_view qualityName{"high"}; // as given, for the report
    view::RenderQuality quality{view::RenderQuality::high()};
    std::filesystem::path outDirectory; // created if it does not exist
    std::filesystem::path shaderDirectory;
    gfx::Validation validation{gfx::Validation::Disabled};
};

// The paths `--bench` knows, by name (decision 369).
[[nodiscard]] std::span<const std::string_view> benchPathNames() noexcept;

// The quality preset a `--quality` name stands for (decision 371), or none.
[[nodiscard]] std::optional<view::RenderQuality> qualityPreset(std::string_view name) noexcept;

// The four names qualityPreset knows, for a refusal to list.
[[nodiscard]] std::span<const std::string_view> qualityPresetNames() noexcept;

// Runs the benchmark in `window` and writes its report. Returns 0, or
// kExitFailure with the reason printed; main adds the validation verdict once
// the renderer is torn down.
[[nodiscard]] int runBench(SDL_Window* window,
                           const BenchRequest& request,
                           std::atomic<std::uint32_t>& validationErrors);

} // namespace orb::app

#endif // ORBSIM_APP_BENCHMODE_HPP
