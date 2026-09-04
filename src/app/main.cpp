//
// orbsim -- entry point.
//
// Current milestone: bring up the window, device and swapchain, and prove the
// frame loop runs. The simulation and renderer land on top of this next.
//
#include "render/VulkanContext.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <print>
#include <string>
#include <utility>

namespace {

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
    double runSeconds{0.0}; // 0 means run until the user quits
};

[[nodiscard]] Options parseArguments(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--validate")
            options.validation = orb::gfx::Validation::Enabled;
        else if (arg == "--no-validate")
            options.validation = orb::gfx::Validation::Disabled;
        else if (arg == "--seconds" && i + 1 < argc)
            options.runSeconds = std::stod(argv[++i]);
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parseArguments(argc, argv);
    const orb::gfx::Validation validation = options.validation;
    const double runSeconds = options.runSeconds;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::print(stderr, "SDL_Init failed: {}\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window =
        SDL_CreateWindow("orbsim",
                         1600,
                         900,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::print(stderr, "SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // A factory, so there is no moment where `gfx` exists but is not usable.
    auto created = orb::gfx::VulkanContext::create(window, validation);
    if (!created) {
        const std::string& message = created.error().message;
        std::print(stderr, "Renderer initialisation failed: {}\n", message);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "orbsim", message.c_str(), window);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    orb::gfx::VulkanContext gfx = std::move(*created);

    std::print("GPU: {}\n", gfx.deviceName());
    std::print("Swapchain: {}x{}\n", gfx.extent().width, gfx.extent().height);

    bool running = true;
    uint64_t frames = 0;
    const uint64_t startTicks = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                gfx.requestSwapchainRebuild();
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) running = false;
                break;
            default:
                break;
            }
        }

        if (runSeconds > 0.0 &&
            static_cast<double>(SDL_GetTicks() - startTicks) >= runSeconds * 1000.0) {
            running = false;
        }

        if (auto frame = gfx.beginFrame()) {
            // Nothing drawn yet; the clear colour is the whole frame.
            gfx.endFrame(*frame);
            ++frames;
        } else {
            SDL_Delay(16); // minimised or mid-rebuild
        }
    }

    const uint64_t elapsed = SDL_GetTicks() - startTicks;
    if (elapsed > 0) {
        std::print("{} frames in {} ms ({:.1f} fps)\n",
                   frames,
                   elapsed,
                   1000.0 * static_cast<double>(frames) / static_cast<double>(elapsed));
    }

    // No shutdown() call: ~VulkanContext does it, in reverse
    // declaration order, without anyone having to remember.
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
