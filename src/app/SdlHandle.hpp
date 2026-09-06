#ifndef ORBSIM_APP_SDLHANDLE_HPP
#define ORBSIM_APP_SDLHANDLE_HPP
//
// RAII for what the application gets from SDL: the library itself, and a
// window.
//
// This is the app layer's counterpart of render/VulkanHandle.hpp -- the one
// place here that writes a destructor, so that main() does not have to call
// SDL_DestroyWindow and SDL_Quit on each of its early returns and get the
// order right every time. It did, once, and had four exit paths to keep in
// step; now it has none.
//
#include <SDL3/SDL.h>

#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace orb::app {

// The app layer's failures are all messages for the user, from SDL or from
// its own argument parsing. One representation, as section 7 asks.
struct SdlError {
    std::string message;
};

// SDL_Init paired with SDL_Quit.
//
// A factory: either the subsystems are up, or there is an error and no
// object. The moved-from state is inert, which is what lets it travel out of
// a std::expected.
class SdlRuntime {
public:
    [[nodiscard]] static std::expected<SdlRuntime, SdlError> init(SDL_InitFlags subsystems) {
        if (!SDL_Init(subsystems)) {
            return std::unexpected(
                SdlError{.message = std::string("SDL_Init failed: ") + SDL_GetError()});
        }
        return SdlRuntime{};
    }

    ~SdlRuntime() {
        if (active_) SDL_Quit();
    }

    SdlRuntime(const SdlRuntime&) = delete;
    SdlRuntime& operator=(const SdlRuntime&) = delete;

    SdlRuntime(SdlRuntime&& other) noexcept : active_(std::exchange(other.active_, false)) {}

    SdlRuntime& operator=(SdlRuntime&& other) noexcept {
        if (this != &other) {
            if (active_) SDL_Quit();
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }

private:
    SdlRuntime() noexcept = default; // only init() gets here, and only after SDL_Init succeeded

    bool active_{true};
};

// SDL_DestroyWindow as a deleter, so a std::unique_ptr does the owning and no
// destructor has to be written for it.
struct WindowDeleter {
    void operator()(SDL_Window* window) const noexcept { SDL_DestroyWindow(window); }
};

using UniqueWindow = std::unique_ptr<SDL_Window, WindowDeleter>;

// One aggregate rather than (title, width, height, flags): two adjacent ints
// transpose in silence, and designated initialisers make the call site say
// which is which.
struct WindowSpec {
    const char* title{"orbsim"};
    int width{1280};
    int height{720};
    SDL_WindowFlags flags{0};
};

[[nodiscard]] inline std::expected<UniqueWindow, SdlError> createWindow(const WindowSpec& spec) {
    UniqueWindow window{SDL_CreateWindow(spec.title, spec.width, spec.height, spec.flags)};
    if (!window) {
        return std::unexpected(
            SdlError{.message = std::string("SDL_CreateWindow failed: ") + SDL_GetError()});
    }
    return window;
}

} // namespace orb::app

#endif // ORBSIM_APP_SDLHANDLE_HPP
