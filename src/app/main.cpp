//
// orbsim -- entry point.
//
// Current milestone: bring up the window, device and swapchain, and prove the
// frame loop runs. The simulation and renderer land on top of this next.
//
#include "app/SdlHandle.hpp"
#include "core/Units.hpp"
#include "render/VulkanContext.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <memory>
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

constexpr std::string_view kUsage = "usage: orbsim [--validate | --no-validate] [--seconds <n>]\n";

// Exit codes. 0 is a clean run; anything else says which kind of failure, so
// that a script (the smoke test, CI) can tell a usage error from a lost device
// from a validation layer complaint without parsing the log.
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitValidationErrors = 3;

// How long to sleep when there is no frame to draw (minimised, or mid-rebuild):
// about one frame at 60 Hz, long enough not to spin a core, short enough that
// a restored window is noticed at once.
constexpr Uint32 kIdleDelayMs = 16;

struct Options {
    // Validation defaults on in a debug build, but stays reachable from a
    // release build too: most Vulkan synchronisation bugs only reproduce at
    // release timings, and needing a separate build to see them wastes time.
#ifdef NDEBUG
    orb::gfx::Validation validation{orb::gfx::Validation::Disabled};
#else
    orb::gfx::Validation validation{orb::gfx::Validation::Enabled};
#endif

    // Runs the loop for a fixed wall-clock time and exits cleanly. Gives an
    // automated smoke test a way to exercise startup, the frame loop and
    // teardown -- the teardown path is where validation errors hide.
    Seconds runFor{0.0}; // zero means run until the user quits
};

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
        if (arg == "--validate") {
            options.validation = orb::gfx::Validation::Enabled;
        } else if (arg == "--no-validate") {
            options.validation = orb::gfx::Validation::Disabled;
        } else if (arg == "--seconds") {
            if (i + 1 >= tokens.size()) {
                return std::unexpected(SdlError{.message = "--seconds needs a value"});
            }
            auto seconds = parseSeconds(tokens.at(++i));
            if (!seconds) return std::unexpected(seconds.error());
            options.runFor = *seconds;
        } else {
            return std::unexpected(
                SdlError{.message = "unknown argument '" + std::string(arg) + "'"});
        }
    }
    return options;
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
        // stderr already has the message, so a message box that cannot be
        // shown loses nothing worth reporting.
        static_cast<void>(
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "orbsim", message.c_str(), window));
        return kExitFailure;
    }
    orb::gfx::VulkanContext gfx = std::move(*created);

    std::print("GPU: {}\n", gfx.deviceName());
    std::print("Swapchain: {}x{}\n", gfx.extent().width, gfx.extent().height);

    uint64_t frames = 0;
    const uint64_t startTicks = SDL_GetTicks();

    while (handleEvents(gfx)) {
        const Seconds elapsed{static_cast<double>(SDL_GetTicks() - startTicks) / 1000.0};
        if (options.runFor.value > 0.0 && elapsed >= options.runFor) break;

        const auto frame = gfx.beginFrame();
        if (!frame) {
            std::print(stderr, "Frame could not begin: {}\n", frame.error().message);
            return kExitFailure;
        }
        if (!*frame) {
            SDL_Delay(kIdleDelayMs); // minimised or mid-rebuild
            continue;
        }

        // Nothing drawn yet; the clear colour is the whole frame.
        if (const auto ended = gfx.endFrame(**frame); !ended) {
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

    // No shutdown() call: ~VulkanContext runs here, in reverse declaration
    // order, without anyone having to remember.
    return 0;
}

[[nodiscard]] int run(std::span<char* const> args) {
    const auto options = parseArguments(args);
    if (!options) {
        std::print(stderr, "orbsim: {}\n{}", options.error().message, kUsage);
        return kExitUsage;
    }

    // Declaration order is teardown order, reversed: the window goes before
    // SDL_Quit, and the renderer -- inside runRenderer -- before both.
    const auto sdl = orb::app::SdlRuntime::init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    if (!sdl) {
        std::print(stderr, "{}\n", sdl.error().message);
        return kExitFailure;
    }

    const auto window = orb::app::createWindow({
        .title = "orbsim",
        .width = 1600,
        .height = 900,
        .flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
    });
    if (!window) {
        std::print(stderr, "{}\n", window.error().message);
        return kExitFailure;
    }

    // Counted outside the renderer's lifetime, because teardown is where
    // validation errors hide and the count has to survive it.
    std::atomic<uint32_t> validationErrors{0};
    if (const int status = runRenderer(window->get(), *options, validationErrors); status != 0) {
        return status;
    }
    if (const uint32_t errors = validationErrors.load(); errors > 0) {
        std::print(stderr, "orbsim: {} validation error(s) reported; see the log above\n", errors);
        return kExitValidationErrors;
    }
    return 0;
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
int main(int argc, char** argv) {
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
#pragma clang diagnostic pop
