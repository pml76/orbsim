//
// orbsim -- entry point.
//
// Current milestone: bring up the window, device and swapchain, and prove the
// frame loop runs. The simulation and renderer land on top of this next.
//
#include "app/ExitCodes.hpp"
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
using orb::app::kExitGoldenMismatch;
using orb::app::kExitUsage;
using orb::app::kExitValidationErrors;

constexpr std::string_view kUsage =
    "usage: orbsim [--validate | --no-validate] [--seconds <n>] [--shader-dir <path>]\n"
    "       orbsim [--validate | --no-validate] --probe <name> [--probe-out <dir>] "
    "[--shader-dir <path>]\n"
    "              [--golden <path> [--accept-golden]]\n"
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
    "  --golden <path>            compare the probe's frame, halved to 640x360, with this\n"
    "                             golden PNG; on a mismatch write <name>.diff.png and exit 4\n"
    "  --accept-golden            write the probe's halved frame to the --golden path as the\n"
    "                             new approved frame. Run by the owner, after looking at the\n"
    "                             frame -- never by a script and never from `check` (ADR 0008)\n"
    "  --help                     this text\n"
    "\n"
    "exit codes: 0 success, 1 failure, 2 usage, 3 validation errors, 4 golden mismatch\n";

// The camera the application exposes the scene with (M1-15, register decision
// 176): **f/16, 1/125 s, ISO 100 -- the "sunny 16" rule**, a photographer's
// setting for a subject in direct sunlight, which is what milestone 1 draws.
// A sunlit surface of albedo 0.3 lands 0.89 stops above a metered mid-grey
// (tests/test_exposure.cpp). Not a tuning constant: it is a published rule
// with a stated purpose, and ADR 0014 leaves the choice of default open. The
// probes of M1-16 pin their own. Unwrapping a refused setting is not a
// constant expression, so a bad value here fails the build.
constexpr orb::view::CameraSettings kDefaultCamera{
    .aperture = orb::view::Aperture::from(16.0).value(),
    .shutterTime = orb::view::ShutterTime::from(Seconds{1.0 / 125.0}).value(),
    .iso = orb::view::Iso::from(100.0).value(),
};

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
    GoldenAction golden{GoldenAction::None};
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
    if (!options.goldenPath) {
        return std::unexpected(
            SdlError{.message = "--accept-golden needs --golden <path>, the golden to write"});
    }
    options.validation = orb::gfx::Validation::Enabled;
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
            return std::unexpected(
                SdlError{.message = "--golden and --accept-golden need --probe <name>"});
        }
        return options;
    }
    if (options.runFor.value() > 0.0) {
        return std::unexpected(
            SdlError{.message = "--probe renders one frame; --seconds does not apply"});
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
[[nodiscard]] std::optional<std::string_view> valueNeededBy(std::string_view arg) {
    if (arg == "--seconds") return "a value";
    if (arg == "--shader-dir") return "a path";
    if (arg == "--probe") return "a probe's name";
    if (arg == "--probe-out") return "a directory";
    if (arg == "--golden") return "a path";
    return std::nullopt;
}

// An option and the value that followed it, by name, so the two cannot be
// handed over the wrong way round (non-negotiable 1).
struct OptionValue {
    std::string_view option;
    std::string_view value;
};

[[nodiscard]] std::expected<void, SdlError> applyValue(Options& options, OptionValue given) {
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
    return checkProbeOptions(options);
}

// Drains the event queue. Returns false once the user has asked to quit.
[[nodiscard]] bool handleEvents(orb::gfx::VulkanContext& gfx) {
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
            break;
        }
    }
    return true;
}

// The frame loop, until the user quits or `--seconds` runs out. Split out of
// runRenderer, which owns what the loop uses and so decides when it is
// destroyed; this only uses it.
[[nodiscard]] int runFrameLoop(orb::gfx::VulkanContext& gfx,
                               const orb::gfx::ResolvePass& resolve,
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

    while (handleEvents(gfx)) {
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

        // Nothing drawn yet: the clear colour is the whole frame, and the
        // resolve pass carries it to the display.
        if (const auto ended = gfx.endFrame(**frame, resolve); !ended) {
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
    auto created = orb::gfx::VulkanContext::create(window, options.validation, validationErrors);
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
    // frame (render/Pipeline.hpp says why). Nothing draws with them yet --
    // that is M1-19 -- but creating them at start-up is what puts shader
    // loading and pipeline creation under the validation layers on every run.
    // Declared after `gfx`, so they are destroyed before the device is.
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
        orb::view::radianceExposure(orb::view::exposureValue100(kDefaultCamera)),
        gfx.swapchainFormat());
    if (!resolve) {
        std::print(stderr, "Resolve pass could not be created: {}\n", resolve.error().message);
        return kExitFailure;
    }

    // Declared after everything a frame uses, so destroyed before any of it:
    // the GPU is idle before the resolve pass and the pipelines go, on every
    // path out of this function. The last frame's commands still use the
    // resolve pass when the loop ends, and the validation layers said so the
    // first time it ran (M1-14) -- the same fault DeviceIdleGuard was written
    // for inside VulkanContext.
    const orb::gfx::DeviceIdleGuard idleBeforeTeardown{gfx.device()};

    const int status = runFrameLoop(gfx, *resolve, options);

    // No shutdown() call: ~VulkanContext runs here, in reverse declaration
    // order, without anyone having to remember.
    return status;
}

// 3 if the validation layers reported anything, read once the renderer is
// torn down -- teardown is where validation errors hide.
[[nodiscard]] int validationVerdict(uint32_t errors) {
    if (errors == 0) return 0;
    std::print(stderr, "orbsim: {} validation error(s) reported; see the log above\n", errors);
    return kExitValidationErrors;
}

// A probe run's exit code, in register decision 230's order -- a failure,
// then validation errors, then a golden mismatch -- and the golden accepted
// only once all three are clear (decision 232).
[[nodiscard]] int
finishProbe(const orb::app::ProbeOutcome& outcome, uint32_t errors, const Options& options) {
    if (outcome.exitCode != 0 && outcome.exitCode != kExitGoldenMismatch) return outcome.exitCode;
    if (const int verdict = validationVerdict(errors); verdict != 0) {
        if (outcome.toAccept) {
            std::print(stderr,
                       "orbsim: --accept-golden: nothing written, because of the "
                       "validation errors\n");
        }
        return verdict;
    }
    if (outcome.exitCode == kExitGoldenMismatch) return kExitGoldenMismatch;
    if (outcome.toAccept && options.goldenPath) {
        return orb::app::acceptGolden(*options.goldenPath, *outcome.toAccept);
    }
    return 0;
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

    // A probe's window is hidden: it is created, so that there is one device
    // path rather than two, and never presented (ADR 0008), and a window
    // flashing up during a test run would only be a distraction. Measured on
    // 2026-09-26 that a swapchain builds on a hidden SDL window here.
    const SDL_WindowFlags hidden = options->mode == Mode::Probe ? SDL_WINDOW_HIDDEN : 0;
    const auto window = orb::app::createWindow({
        .title = "orbsim",
        .width = 1600,
        .height = 900,
        .flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | hidden,
    });
    if (!window) {
        std::print(stderr, "{}\n", window.error().message);
        return kExitFailure;
    }

    // Counted outside the renderer's lifetime, because teardown is where
    // validation errors hide and the count has to survive it.
    std::atomic<uint32_t> validationErrors{0};
    if (options->mode != Mode::Probe) {
        const int status = runRenderer(window->get(), *options, validationErrors);
        if (status != 0) return status;
        return validationVerdict(validationErrors.load());
    }
    const orb::app::ProbeOutcome outcome =
        orb::app::runProbe(window->get(),
                           {
                               .probe = options->probe,
                               .outDirectory = options->probeOut,
                               .shaderDirectory = options->shaderDirectory,
                               .validation = options->validation,
                               .golden = options->golden,
                               .goldenPath = options->goldenPath.value_or(std::filesystem::path{}),
                           },
                           validationErrors);
    return finishProbe(outcome, validationErrors.load(), *options);
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
