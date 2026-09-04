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

int main(int argc, char** argv) {
    // Validation defaults on in a debug build, but stays reachable from a
    // release build too: most Vulkan synchronisation bugs only reproduce at
    // release timings, and needing a separate build to see them wastes time.
#ifdef NDEBUG
    bool validation = false;
#else
    bool validation = true;
#endif
    double runSeconds = 0.0;   // 0 means run until the user quits

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--validate") validation = true;
        else if (arg == "--no-validate") validation = false;
        // Runs the loop for a fixed wall-clock time and exits cleanly. Gives
        // an automated smoke test a way to exercise startup, the frame loop
        // and teardown -- the teardown path is where validation errors hide.
        else if (arg == "--seconds" && i + 1 < argc) runSeconds = std::stod(argv[++i]);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::print(stderr, "SDL_Init failed: {}\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "orbsim", 1600, 900,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::print(stderr, "SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    orb::gfx::VulkanContext gfx;
    std::string error;

    if (!gfx.init(window, validation, error)) {
        std::print(stderr, "Renderer initialisation failed: {}\n", error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "orbsim", error.c_str(), window);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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
            SDL_Delay(16);   // minimised or mid-rebuild
        }
    }

    const uint64_t elapsed = SDL_GetTicks() - startTicks;
    if (elapsed > 0) {
        std::print("{} frames in {} ms ({:.1f} fps)\n",
                   frames, elapsed, 1000.0 * static_cast<double>(frames) / static_cast<double>(elapsed));
    }

    gfx.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
