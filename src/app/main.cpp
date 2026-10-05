//
// orbsim -- entry point.
//
// Current milestone: bring up the window, device and swapchain, and prove the
// frame loop runs. The simulation and renderer land on top of this next.
//
#include "app/BenchMode.hpp"
#include "app/ExitCodes.hpp"
#include "app/InteractiveView.hpp"
#include "app/ProbeMode.hpp"
#include "app/SdlHandle.hpp"
#include "core/Units.hpp"
#include "render/Pipeline.hpp"
#include "render/Probes.hpp"
#include "render/ResolvePass.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanHandle.hpp"
#include "view/Exposure.hpp"
#include "view/RenderQuality.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using orb::Seconds;
using orb::app::SdlError;

using orb::app::GoldenAction;
using orb::app::kExitFailure;
using orb::app::kExitUsage;
using orb::app::kExitValidationErrors;

constexpr std::string_view kUsage =
    "usage: orbsim [--validate | --no-validate] [--seconds <n>] [--shader-dir <path>]\n"
    "       orbsim [--validate | --no-validate] --probe <name> [--probe-out <dir>] "
    "[--shader-dir <path>]\n"
    "              [--golden <path> | --golden-dir <dir>] [--accept-golden]\n"
    "       orbsim [--validate | --no-validate] --bench <path> [--frames <n>]\n"
    "              [--width <pixels> --height <pixels>] [--quality <preset>]\n"
    "              [--bench-out <dir>] [--shader-dir <path>]\n"
    "       orbsim --probe-list\n"
    "       orbsim --help\n";

// `--help` (M1-17, register decision 234): every option, one line each, with
// --accept-golden's rule beside it -- ADR 0008 asks that the help text say it.
constexpr std::string_view kHelp =
    "\n"
    "  --validate, --no-validate  the Vulkan validation layers on or off; on by default in a\n"
    "                             Debug build\n"
    "  --seconds <n>              run the window for n seconds, then exit\n"
    "  --shader-dir <path>        where the compiled shaders are\n"
    "  --probe <name>             render one probe's frame at 1280x720, write its files, exit\n"
    "  --probe-out <dir>          where a probe writes its files; the build tree's probes/ by\n"
    "                             default\n"
    "  --probe-list               list the probes\n"
    "  --bench <path>             fly a camera path, time every frame on the CPU and the GPU,\n"
    "                             print the figures, and write them with a table of every\n"
    "                             frame; the one path is grid-orbit. The validation layers are\n"
    "                             off unless --validate is given\n"
    "  --frames <n>               the frames --bench measures after its warm-up; 600 by default\n"
    "  --width, --height <pixels> the image --bench draws; 1920x1080 by default\n"
    "  --quality <preset>         low, medium, high or ultra, for --bench; high by default\n"
    "  --bench-out <dir>          where --bench writes; the build tree's bench/ by default\n"
    "  --golden <path>            compare the probe's frame, halved to 640x360, with this\n"
    "                             golden PNG; on a mismatch write <name>.diff.png and exit 4\n"
    "  --golden-dir <dir>         the same, with this graphics card's own golden,\n"
    "                             <dir>/<vendor>-<device>/<name>.png; a card with none exits 1\n"
    "  --accept-golden            write the probe's halved frame to the --golden path, or to\n"
    "                             the card's file under --golden-dir, as the new approved\n"
    "                             frame. Run by the owner, after looking at the frame --\n"
    "                             never by a script and never from `check` (ADR 0008)\n"
    "  --help                     this text\n"
    "\n"
    "exit codes: 0 success, 1 failure, 2 usage, 3 validation errors, 4 golden mismatch\n";

// How long to sleep when there is no frame to draw (minimised, or mid-rebuild):
// about one frame at 60 Hz, long enough not to spin a core, short enough that
// a restored window is noticed at once.
constexpr Uint32 kIdleDelayMs = 16;

// A path from the UTF-8 text SDL hands main its arguments in.
//
// **SDL's arguments are UTF-8 on every platform**: on Windows, where SDL
// supplies the entry point, it re-reads the wide command line and converts it
// (WIN_CheckDefaultArgcArgv in SDL's src/core/windows/SDL_windows.c). A
// std::filesystem::path built from a char string reads it in the *current
// code page* on Windows instead, which is the same text only while every
// character is ASCII -- the failure CODING_GUIDELINES section 18 describes,
// for a user whose account name steps outside it. Relabelling the bytes as
// char8_t is what tells the path they are UTF-8. ORBSIM_SHADER_DIR goes through
// here too: a string literal in this project's sources is UTF-8.
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
    std::u8string utf8(text.size(), u8'\0');
    std::ranges::transform(
        text, utf8.begin(), [](char unit) { return static_cast<char8_t>(unit); });
    return std::filesystem::path{utf8};
}

// What a run is for (M1-16): the interactive window, one probe's frame, the
// list of probes, or since M1-17 the help text. An enum rather than several
// booleans, which could be set together (non-negotiable 2).
enum class Mode : std::uint8_t {
    Window,
    Probe,
    ListProbes,
    Help,
    Bench, // since M1-22
};

struct Options {
    Mode mode{Mode::Window};

    // Validation defaults on in a debug build, but stays reachable from a
    // release build too: most Vulkan synchronisation bugs only reproduce at
    // release timings, and needing a separate build to see them wastes time.
#ifdef NDEBUG
    orb::gfx::Validation validation{orb::gfx::Validation::Disabled};
#else
    orb::gfx::Validation validation{orb::gfx::Validation::Enabled};
#endif
    // What --validate or --no-validate asked for, if either was given:
    // --accept-golden refuses --no-validate (register decision 232), which
    // the default alone cannot tell apart from a request.
    std::optional<orb::gfx::Validation> validationAsked;

    // Runs the loop for a fixed wall-clock time and exits cleanly. Gives an
    // automated smoke test a way to exercise startup, the frame loop and
    // teardown -- the teardown path is where validation errors hide.
    Seconds runFor{0.0}; // zero means run until the user quits

    // Where the compiled shaders are. The build tree's by default, which is
    // where CMake writes them; an argument so that a run can be pointed
    // elsewhere -- and so that a directory with no shaders in it can be
    // tested for being reported by name (register decision 148).
    std::filesystem::path shaderDirectory{pathFromUtf8(ORBSIM_SHADER_DIR)};

    // --probe's name, and where its files go: the build tree's probes/ by
    // default, where CMake points ORBSIM_PROBE_DIR (register decision 192).
    std::string probe;
    std::filesystem::path probeOut{pathFromUtf8(ORBSIM_PROBE_DIR)};

    // --golden's path and what to do with it (M1-17, register decisions
    // 230-232). Absent unless --golden was given.
    std::optional<std::filesystem::path> goldenPath;
    // --golden-dir's directory, holding one folder per graphics card (M1-110,
    // register decision 289). Absent unless --golden-dir was given; never
    // given beside --golden (decision 295).
    std::optional<std::filesystem::path> goldenDirectory;
    GoldenAction golden{GoldenAction::None};

    // --bench's path, and the options only it takes (M1-22, register
    // decisions 369-373). Each is absent unless given, so that one given
    // without --bench is refused rather than ignored.
    std::string bench;
    std::optional<std::uint32_t> frames;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    std::optional<std::string> quality;
    // The preset --quality names, looked up when it is read, so a name
    // nobody knows is refused there and this is never absent.
    orb::view::RenderQuality qualityPreset{orb::view::RenderQuality::high()};
    std::optional<std::filesystem::path> benchOut;
};

// Whether a person is at the window: a run without --seconds goes on until
// somebody quits it, and every automated run -- orbsim_smoke, the mutation
// pass, the measurement script -- passes --seconds.
[[nodiscard]] bool isInteractive(const Options& options) noexcept {
    return !(options.runFor.value() > 0.0);
}

// std::from_chars rather than std::stod: a malformed argument is a user error
// to explain, not a std::invalid_argument to terminate on.
[[nodiscard]] std::expected<Seconds, SdlError> parseSeconds(std::string_view text) {
    double value = 0.0;
    // std::to_address rather than data() + size(): the same one-past-the-end
    // pointer, obtained without pointer arithmetic the compiler cannot bound.
    const char* const last = std::to_address(text.end());
    const auto [end, ec] = std::from_chars(std::to_address(text.begin()), last, value);
    if (ec != std::errc{} || end != last || !(value >= 0.0)) {
        return std::unexpected(SdlError{
            .message = "--seconds needs a non-negative number, got '" + std::string(text) + "'",
        });
    }
    return Seconds{value};
}

// An option and the value that followed it, by name, so the two cannot be
// handed over the wrong way round (non-negotiable 1).
struct OptionValue {
    std::string_view option;
    std::string_view value;
};

// The least and the most a whole-number option accepts, both included.
struct CountRange {
    std::uint32_t least{};
    std::uint32_t most{};
};

// A whole number within `range`, for --frames, --width and --height (M1-22),
// read as --seconds is: a malformed one is a usage error to explain.
[[nodiscard]] std::expected<std::uint32_t, SdlError> parseCount(OptionValue given,
                                                                CountRange range) {
    std::uint32_t value = 0;
    const char* const last = std::to_address(given.value.end());
    const auto [end, ec] = std::from_chars(std::to_address(given.value.begin()), last, value);
    if (ec != std::errc{} || end != last || value < range.least || value > range.most) {
        return std::unexpected(SdlError{
            .message = std::format("{} needs a whole number from {} to {}, got '{}'",
                                   given.option,
                                   range.least,
                                   range.most,
                                   given.value),
        });
    }
    return value;
}

// What --accept-golden refuses (register decisions 232 and 233), in this
// order: it runs under the validation layers, so --no-validate is refused
// first, and it writes to --golden's path, so without one there is nowhere to
// write. Validation is then turned on whatever the build's default.
[[nodiscard]] std::expected<Options, SdlError> checkAcceptGolden(Options options) {
    if (options.golden != GoldenAction::Accept) return options;
    if (options.validationAsked == orb::gfx::Validation::Disabled) {
        return std::unexpected(SdlError{
            .message = "--accept-golden runs under the validation layers; --no-validate "
                       "cannot go with it",
        });
    }
    if (!options.goldenPath && !options.goldenDirectory) {
        return std::unexpected(SdlError{
            .message = "--accept-golden needs --golden <path> or --golden-dir <dir>, the golden "
                       "to write",
        });
    }
    options.validation = orb::gfx::Validation::Enabled;
    return options;
}

// The names in `names`, comma-separated, for a refusal to list.
[[nodiscard]] std::string listOf(std::span<const std::string_view> names) {
    std::string list;
    for (const std::string_view name : names) {
        list += (list.empty() ? "" : ", ") + std::string(name);
    }
    return list;
}

// The combinations a benchmark run refuses (M1-22, register decisions
// 369-372). Its own options mean nothing without --bench, and are refused
// rather than ignored; a running time and a probe mean nothing with it; and
// the path must be one it knows, said here with the list
// rather than discovered after a window and a device have been made.
[[nodiscard]] std::expected<Options, SdlError> checkBenchOptions(Options options) {
    if (options.mode == Mode::Help) return options;
    // Before the mode is looked at: whichever of the two came last set it.
    if (!options.bench.empty() && !options.probe.empty()) {
        return std::unexpected(SdlError{.message = "--bench and --probe cannot go together"});
    }
    if (options.mode != Mode::Bench) {
        if (options.frames || options.width || options.height || options.quality ||
            options.benchOut) {
            return std::unexpected(SdlError{
                .message = "--frames, --width, --height, --quality and --bench-out need "
                           "--bench <path>",
            });
        }
        return options;
    }
    if (options.runFor.value() > 0.0) {
        return std::unexpected(SdlError{
            .message = "--bench measures a number of frames; --seconds does not apply",
        });
    }
    if (std::ranges::find(orb::app::benchPathNames(), options.bench) ==
        orb::app::benchPathNames().end()) {
        return std::unexpected(SdlError{
            .message = "no benchmark path named '" + options.bench +
                       "'; the paths are: " + listOf(orb::app::benchPathNames()),
        });
    }
    return options;
}

// The combinations a probe run refuses (register decision 192). A probe is one
// frame and exits, so a running time means nothing to it; and a name must be
// one the registry knows, which is said here with the list rather than
// discovered after a window and a device have been made.
[[nodiscard]] std::expected<Options, SdlError> checkProbeOptions(Options options) {
    if (options.mode == Mode::Help) return options;
    if (options.mode != Mode::Probe) {
        if (options.golden != GoldenAction::None) {
            return std::unexpected(SdlError{
                .message = "--golden, --golden-dir and --accept-golden need --probe <name>",
            });
        }
        return options;
    }
    if (options.runFor.value() > 0.0) {
        return std::unexpected(
            SdlError{.message = "--probe renders one frame; --seconds does not apply"});
    }
    // One golden, named one way (decision 295).
    if (options.goldenPath && options.goldenDirectory) {
        return std::unexpected(SdlError{
            .message = "--golden names one file and --golden-dir a folder per card; give one",
        });
    }
    if (!orb::gfx::findProbe(options.probe)) {
        std::string names;
        for (const orb::gfx::Probe& probe : orb::gfx::kProbes) {
            names += (names.empty() ? "" : ", ") + std::string(probe.name);
        }
        return std::unexpected(SdlError{
            .message = "no probe named '" + options.probe + "'; the probes are: " + names,
        });
    }
    return checkAcceptGolden(options);
}

// A switch that stands alone. Returns false when `arg` is not one, so the
// caller can try the options that take a value (M1-17 split these out of
// parseArguments, which had grown past the lint's size and complexity limits).
[[nodiscard]] bool applySwitch(Options& options, std::string_view arg) {
    if (arg == "--validate") {
        options.validation = orb::gfx::Validation::Enabled;
        options.validationAsked = options.validation;
    } else if (arg == "--no-validate") {
        options.validation = orb::gfx::Validation::Disabled;
        options.validationAsked = options.validation;
    } else if (arg == "--probe-list") {
        options.mode = Mode::ListProbes;
    } else if (arg == "--accept-golden") {
        options.golden = GoldenAction::Accept;
    } else if (arg == "--help") {
        options.mode = Mode::Help;
    } else {
        return false;
    }
    return true;
}

// What an option that takes a value is missing when it has none, as the
// message says it -- or nothing, when `arg` is not such an option.
// The same, for the options only --bench takes (M1-22), which applyBenchValue
// reads.
[[nodiscard]] std::optional<std::string_view> benchValueNeededBy(std::string_view arg) {
    if (arg == "--bench") return "a path's name";
    if (arg == "--frames") return "a number of frames";
    if (arg == "--width" || arg == "--height") return "a number of pixels";
    if (arg == "--quality") return "a preset's name";
    if (arg == "--bench-out") return "a directory";
    return std::nullopt;
}

// The largest image side --width and --height accept: the RX 7900 XTX's
// maxImageDimension2D, measured 2026-10-04 with vulkaninfo, and the common
// figure for desktop GPUs. A size the window cannot reach is refused later,
// by measuring what the swapchain got (register decision 370).
constexpr std::uint32_t kLargestImageSide = 16'384;

[[nodiscard]] std::expected<void, SdlError> applyBenchValue(Options& options, OptionValue given) {
    if (given.option == "--bench") {
        options.mode = Mode::Bench;
        options.bench = std::string(given.value);
    } else if (given.option == "--quality") {
        const auto preset = orb::app::qualityPreset(given.value);
        if (!preset) {
            return std::unexpected(SdlError{
                .message = "no quality preset named '" + std::string(given.value) +
                           "'; the presets are: " + listOf(orb::app::qualityPresetNames()),
            });
        }
        options.quality = std::string(given.value);
        options.qualityPreset = *preset;
    } else if (given.option == "--bench-out") {
        options.benchOut = pathFromUtf8(given.value);
    } else if (given.option == "--frames") {
        const auto frames = parseCount(given, {.least = 1, .most = orb::app::kBenchMaximumFrames});
        if (!frames) return std::unexpected(frames.error());
        options.frames = *frames;
    } else {
        const auto side = parseCount(given, {.least = 1, .most = kLargestImageSide});
        if (!side) return std::unexpected(side.error());
        (given.option == "--width" ? options.width : options.height) = *side;
    }
    return {};
}

[[nodiscard]] std::optional<std::string_view> valueNeededBy(std::string_view arg) {
    if (const auto bench = benchValueNeededBy(arg)) return bench;
    if (arg == "--seconds") return "a value";
    if (arg == "--shader-dir") return "a path";
    if (arg == "--probe") return "a probe's name";
    if (arg == "--probe-out") return "a directory";
    if (arg == "--golden") return "a path";
    if (arg == "--golden-dir") return "a directory";
    return std::nullopt;
}

[[nodiscard]] std::expected<void, SdlError> applyValue(Options& options, OptionValue given) {
    if (benchValueNeededBy(given.option)) return applyBenchValue(options, given);
    if (given.option == "--seconds") {
        auto seconds = parseSeconds(given.value);
        if (!seconds) return std::unexpected(seconds.error());
        options.runFor = *seconds;
    } else if (given.option == "--shader-dir") {
        options.shaderDirectory = pathFromUtf8(given.value);
    } else if (given.option == "--probe") {
        options.mode = Mode::Probe;
        options.probe = std::string(given.value);
    } else if (given.option == "--probe-out") {
        options.probeOut = pathFromUtf8(given.value);
    } else if (given.option == "--golden") {
        options.goldenPath = pathFromUtf8(given.value);
        if (options.golden == GoldenAction::None) options.golden = GoldenAction::Compare;
    } else if (given.option == "--golden-dir") {
        options.goldenDirectory = pathFromUtf8(given.value);
        if (options.golden == GoldenAction::None) options.golden = GoldenAction::Compare;
    }
    return {};
}

// The arguments arrive as a span rather than the (int, char**) pair main is
// handed: the count then travels with the pointer, so indexing is checked
// rather than unchecked pointer arithmetic, and the elements are const.
[[nodiscard]] std::expected<Options, SdlError> parseArguments(std::span<char* const> args) {
    // Copied into views once so the loop indexes a container with a checked
    // at(), rather than subscripting raw storage.
    const std::vector<std::string_view> tokens(args.begin(), args.end());
    Options options;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string_view arg = tokens.at(i);
        if (applySwitch(options, arg)) continue;
        const std::optional<std::string_view> needed = valueNeededBy(arg);
        if (!needed) {
            return std::unexpected(
                SdlError{.message = "unknown argument '" + std::string(arg) + "'"});
        }
        if (i + 1 >= tokens.size()) {
            return std::unexpected(
                SdlError{.message = std::string(arg) + " needs " + std::string(*needed)});
        }
        if (auto applied = applyValue(options, {.option = arg, .value = tokens.at(++i)});
            !applied) {
            return std::unexpected(applied.error());
        }
    }
    return checkBenchOptions(options).and_then(checkProbeOptions);
}

// Drains the event queue, handing the mouse to the view (M1-21). Returns
// false once the user has asked to quit.
[[nodiscard]] bool
handleEvents(SDL_Window* window, orb::gfx::VulkanContext& gfx, orb::app::InteractiveView& view) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            return false;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            gfx.requestSwapchainRebuild();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE) return false;
            break;
        default:
            // Physical pixels per window unit, which SDL reports as 0 if it
            // cannot say -- a drag then moves nothing, rather than guessing.
            view.handle(
                event,
                {
                    .pixelsPerWindowUnit = static_cast<double>(SDL_GetWindowPixelDensity(window)),
                    .height = orb::Pixels{static_cast<double>(gfx.extent().height)},
                });
            break;
        }
    }
    return true;
}

// The frame loop, until the user quits or `--seconds` runs out. Split out of
// runRenderer, which owns what the loop uses and so decides when it is
// destroyed; this only uses it.
// What the frame loop draws with besides the renderer: the scene's pipelines,
// the resolve pass, and the view the user flies (M1-21). By name, as a struct
// of pointers that do not own -- each outlives the loop, in runRenderer.
struct FrameDrawing {
    const orb::gfx::ScenePipelines* pipelines{};
    const orb::gfx::ResolvePass* resolve{};
    orb::app::InteractiveView* view{};
};

[[nodiscard]] int runFrameLoop(SDL_Window* window,
                               orb::gfx::VulkanContext& gfx,
                               const FrameDrawing& drawing,
                               const Options& options) {
    // The one quality value the application owns (M1-12, ADR 0007). It is
    // handed to each frame by value and nothing reads it yet -- the fields
    // arrive with the features that cost frames, in M1-46 and M1-59.
    //
    // **A preset named here rather than a --quality argument.** A command-line
    // switch is an actual setting, which this task puts out of scope, and all
    // four presets are the same value today, so it would be a control with no
    // effect. M1-16's probe mode is the task that first needs one by name.
    // Which preset is ADR 0007's open question about the default at first run,
    // and is not settled by naming one here.
    const orb::view::RenderQuality quality = orb::view::RenderQuality::high();

    uint64_t frames = 0;
    const uint64_t startTicks = SDL_GetTicks();

    while (handleEvents(window, gfx, *drawing.view)) {
        const Seconds elapsed{static_cast<double>(SDL_GetTicks() - startTicks) / 1000.0};
        if (options.runFor.value() > 0.0 && elapsed >= options.runFor) break;

        auto frame = gfx.beginFrame(quality);
        if (!frame) {
            std::print(stderr, "Frame could not begin: {}\n", frame.error().message);
            return kExitFailure;
        }
        if (!*frame) {
            SDL_Delay(kIdleDelayMs); // minimised or mid-rebuild
            continue;
        }

        // The Earth's grid from where the camera now is (M1-21), then the
        // resolve pass carries the frame to the display.
        if (const auto drawn = drawing.view->record(gfx, **frame, *drawing.pipelines); !drawn) {
            std::print(stderr, "Frame could not be drawn: {}\n", drawn.error().message);
            return kExitFailure;
        }
        if (const auto ended = gfx.endFrame(**frame, *drawing.resolve); !ended) {
            std::print(stderr, "Frame could not be presented: {}\n", ended.error().message);
            return kExitFailure;
        }
        ++frames;
    }

    const uint64_t elapsedMs = SDL_GetTicks() - startTicks;
    if (elapsedMs > 0) {
        std::print("{} frames in {} ms ({:.1f} fps)\n",
                   frames,
                   elapsedMs,
                   1000.0 * static_cast<double>(frames) / static_cast<double>(elapsedMs));
    }
    return 0;
}

// Creates the renderer, runs the frame loop, and destroys the renderer on
// return -- which is the point of it being a separate function: the caller
// reads the validation error count only once teardown has happened.
[[nodiscard]] int
runRenderer(SDL_Window* window, const Options& options, std::atomic<uint32_t>& validationErrors) {
    // A factory, so there is no moment where `gfx` exists but is not usable.
    auto created = orb::gfx::VulkanContext::create(
        window, options.validation, validationErrors, orb::gfx::Presentation::Paced);
    if (!created) {
        const std::string& message = created.error().message;
        std::print(stderr, "Renderer initialisation failed: {}\n", message);
        // A dialog is for a person, and a run with --seconds is not one: it is
        // orbsim_smoke, a mutation pass or the measurement script, and a dialog
        // there waits for a click nobody gives. M1-14's mutation pass found
        // exactly that -- a failed start-up hung orbsim_smoke until CTest's
        // timeout instead of failing it (register decision 170). stderr
        // already has the message, so a message box that cannot be shown
        // loses nothing worth reporting either.
        if (isInteractive(options)) {
            static_cast<void>(
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "orbsim", message.c_str(), window));
        }
        return kExitFailure;
    }
    orb::gfx::VulkanContext gfx = std::move(*created);

    std::print("GPU: {}\n", gfx.deviceName());
    std::print("Swapchain: {}x{}\n", gfx.extent().width, gfx.extent().height);

    // Every pipeline the scene will draw with, created here and not during a
    // frame (render/Pipeline.hpp says why); the window draws its lines with
    // one of them since M1-21. Declared after `gfx`, so they are destroyed
    // before the device is.
    const auto pipelines = orb::gfx::ScenePipelines::create(gfx, options.shaderDirectory);
    if (!pipelines) {
        std::print(stderr, "Pipelines could not be created: {}\n", pipelines.error().message);
        return kExitFailure;
    }

    // The resolve pass: the one draw that writes the display -- exposure, AgX
    // and the sRGB encode, once at the end of every frame (M1-14, M1-15,
    // ADR 0014). After the scene pipelines, so a missing shader directory
    // still names line.vert.spv first, which shader_missing_is_reported checks.
    const auto resolve = orb::gfx::ResolvePass::create(
        gfx,
        options.shaderDirectory,
        orb::view::radianceExposure(orb::view::exposureValue100(orb::app::kDefaultCamera)),
        gfx.swapchainFormat());
    if (!resolve) {
        std::print(stderr, "Resolve pass could not be created: {}\n", resolve.error().message);
        return kExitFailure;
    }

    // The scene the window shows and the camera that flies it (M1-21): the
    // Earth's grid and horizon, through the line renderer's per-frame buffers.
    auto view = orb::app::InteractiveView::create(gfx);
    if (!view) {
        std::print(stderr, "The view could not be created: {}\n", view.error().message);
        return kExitFailure;
    }

    // Declared after everything a frame uses, so destroyed before any of it:
    // the GPU is idle before the resolve pass and the pipelines go, on every
    // path out of this function. The last frame's commands still use the
    // resolve pass when the loop ends, and the validation layers said so the
    // first time it ran (M1-14) -- the same fault DeviceIdleGuard was written
    // for inside VulkanContext.
    const orb::gfx::DeviceIdleGuard idleBeforeTeardown{gfx.device()};

    const int status = runFrameLoop(window,
                                    gfx,
                                    {
                                        .pipelines = &*pipelines,
                                        .resolve = &*resolve,
                                        .view = &*view,
                                    },
                                    options);

    // No shutdown() call: ~VulkanContext runs here, in reverse declaration
    // order, without anyone having to remember.
    return status;
}

void reportValidationErrors(uint32_t errors) {
    std::print(stderr, "orbsim: {} validation error(s) reported; see the log above\n", errors);
}

// 3 if the validation layers reported anything, read once the renderer is
// torn down -- teardown is where validation errors hide.
[[nodiscard]] int validationVerdict(uint32_t errors) {
    if (errors == 0) return 0;
    reportValidationErrors(errors);
    return kExitValidationErrors;
}

// A probe run's exit code, in register decision 230's order -- a failure,
// then validation errors, then a golden mismatch -- and the golden accepted
// only once all three are clear (decision 232). The order is probeExit's, in
// app/ExitCodes.hpp, where the compiler checks it (M1-108).
[[nodiscard]] int finishProbe(const orb::app::ProbeOutcome& outcome, uint32_t errors) {
    const orb::app::ProbeExit verdict =
        orb::app::probeExit({.probeExitCode = outcome.exitCode, .validationErrors = errors});
    if (verdict.exitCode == kExitValidationErrors) {
        reportValidationErrors(errors);
        if (outcome.toAccept) {
            std::print(stderr,
                       "orbsim: --accept-golden: nothing written, because of the "
                       "validation errors\n");
        }
    }
    if (outcome.toAccept && verdict.golden == orb::app::GoldenWrite::Allowed) {
        return orb::app::acceptGolden(*outcome.toAccept);
    }
    return verdict.exitCode;
}

// The window a run draws in. A probe's is hidden: it is created, so that there
// is one device path rather than two, and never presented (ADR 0008), and a
// window flashing up during a test run would only be a distraction. Measured
// on 2026-09-26 that a swapchain builds on a hidden SDL window here. A
// benchmark's is shown -- presenting to it is part of what is measured -- the
// size asked for, and not resizable (M1-22, register decision 370); it is made
// that many window units across, and runBench corrects it to that many pixels.
[[nodiscard]] orb::app::WindowSpec windowFor(const Options& options) {
    if (options.mode == Mode::Bench) {
        return {
            .title = "orbsim --bench",
            .width = static_cast<int>(options.width.value_or(orb::app::kBenchDefaultSize.width)),
            .height = static_cast<int>(options.height.value_or(orb::app::kBenchDefaultSize.height)),
            .flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY,
        };
    }
    const SDL_WindowFlags hidden = options.mode == Mode::Probe ? SDL_WINDOW_HIDDEN : 0;
    return {
        .title = "orbsim",
        .width = 1600,
        .height = 900,
        .flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | hidden,
    };
}

// What runBench is asked for, from the options (M1-22). **The validation
// layers are off unless --validate was given**, in every build, where the
// window turns them on in a Debug build: a frame timed under them is not a
// measurement (register decision 372).
[[nodiscard]] orb::app::BenchRequest benchRequestOf(const Options& options) {
    return {
        .path = options.bench,
        .frames = options.frames.value_or(orb::app::kBenchDefaultFrames),
        .size =
            {
                .width = options.width.value_or(orb::app::kBenchDefaultSize.width),
                .height = options.height.value_or(orb::app::kBenchDefaultSize.height),
            },
        .qualityName = options.quality ? std::string_view{*options.quality} : "high",
        .quality = options.qualityPreset,
        .outDirectory = options.benchOut.value_or(pathFromUtf8(ORBSIM_BENCH_DIR)),
        .shaderDirectory = options.shaderDirectory,
        .validation = options.validationAsked.value_or(orb::gfx::Validation::Disabled),
    };
}

[[nodiscard]] int run(std::span<char* const> args) {
    const auto options = parseArguments(args);
    if (!options) {
        std::print(stderr, "orbsim: {}\n{}", options.error().message, kUsage);
        return kExitUsage;
    }

    if (options->mode == Mode::Help) {
        std::print("{}{}", kUsage, kHelp);
        return 0;
    }
    if (options->mode == Mode::ListProbes) {
        orb::app::listProbes();
        return 0;
    }

    // Declaration order is teardown order, reversed: the window goes before
    // SDL_Quit, and the renderer -- inside runRenderer -- before both.
    const auto sdl = orb::app::SdlRuntime::init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    if (!sdl) {
        std::print(stderr, "{}\n", sdl.error().message);
        return kExitFailure;
    }

    const auto window = orb::app::createWindow(windowFor(*options));
    if (!window) {
        std::print(stderr, "{}\n", window.error().message);
        return kExitFailure;
    }

    // Counted outside the renderer's lifetime, because teardown is where
    // validation errors hide and the count has to survive it.
    std::atomic<uint32_t> validationErrors{0};
    if (options->mode == Mode::Bench) {
        const int status =
            orb::app::runBench(window->get(), benchRequestOf(*options), validationErrors);
        if (status != 0) return status;
        return validationVerdict(validationErrors.load());
    }
    if (options->mode != Mode::Probe) {
        const int status = runRenderer(window->get(), *options, validationErrors);
        if (status != 0) return status;
        return validationVerdict(validationErrors.load());
    }
    const orb::app::ProbeOutcome outcome = orb::app::runProbe(
        window->get(),
        {
            .probe = options->probe,
            .outDirectory = options->probeOut,
            .shaderDirectory = options->shaderDirectory,
            .validation = options->validation,
            .golden = options->golden,
            .goldenPath = options->goldenDirectory.value_or(
                options->goldenPath.value_or(std::filesystem::path{})),
            .goldenLocation = options->goldenDirectory ? orb::app::GoldenLocation::CardFolders
                                                       : orb::app::GoldenLocation::File,
        },
        validationErrors);
    return finishProbe(outcome, validationErrors.load());
}

} // namespace

// main is the one function nothing may escape from: an exception leaving it is
// std::terminate, with no message and no exit code worth reading. Nothing here
// throws on purpose, but std::print can if stdout is closed, and the standard
// library can when memory runs out. The handlers use std::fputs because a
// reporting path that can itself throw is not a reporting path; its results
// are discarded on purpose, since if stderr is gone too there is nobody left
// to tell.
//
// The arguments and std::fputs are both the C runtime's interface, and two
// warnings are off for this function alone (ADR 0017): the arguments arrive as
// a pointer and a count, and a span of the two is the only way to give them
// bounds, which is the construction -Wunsafe-buffer-usage-in-container
// reports; and std::fputs is a C library function taking an unbounded string,
// which is what -Wunsafe-buffer-usage-in-libc-call reports.
// _set_abort_behavior and its two flags are the Windows C runtime's, declared
// in its <stdlib.h> itself -- <cstdlib> reaches them only through that header,
// which misc-include-cleaner rightly does not count as providing them. So the
// header that declares them is the one included, and the check that wants
// <cstdlib> instead is silenced for this line alone: it is right for the
// standard names, which this line is not here for. Granted by the owner,
// 2026-09-24 (register decision 155).
#ifdef _WIN32
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    // A failed assertion ends the process rather than asking a question. The
    // Windows debug runtime otherwise turns abort's message into a modal
    // dialog, and the process sits there alive: orbsim_smoke then hangs until
    // CTest's timeout instead of failing. The mutation pass found that on
    // 2026-09-24, when a mutant tripped an assertion in the renderer -- the
    // same thing tests/AbortBehaviour.cpp does for every suite, for the same
    // reason, and the same call.
    static_cast<void>(_set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG));
#endif
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    try {
        return run(args);
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("orbsim: unhandled exception: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return kExitFailure;
    } catch (...) {
        static_cast<void>(std::fputs("orbsim: unhandled exception of unknown type\n", stderr));
        return kExitFailure;
    }
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
