#ifndef ORBSIM_APP_INTERACTIVEVIEW_HPP
#define ORBSIM_APP_INTERACTIVEVIEW_HPP
//
// What the window shows and how it is flown (M1-21; register decisions 343,
// 344 and 361; ADR 0026).
//
// **The scene is the grid probes'**: the Earth's latitude and longitude grid
// on the WGS-84 sphere and its horizon, at the grid probes' date
// (render/Probes.hpp's `gridEpoch`), drawn by the line renderer every frame --
// which uses both frame slots, closing the gap register decision 282 recorded.
//
// **The camera is view/CameraController.hpp's**, and this file is the part of
// ADR 0026's arrangement only SDL can do: it reads a mouse event, says which
// button is held, converts the window's units to pixels, and hands
// view/CameraInput.hpp's mapping the numbers. Left drag circles, right drag
// pans, the wheel moves in and out (decision 343).
//
// **It starts** with the focus on the equator at the prime meridian, 1,000 km
// from it, looking north and tilted 30 degrees, through a 45-degree view with
// a 1 m near plane (decision 344).
//
#include "render/LineRenderer.hpp"
#include "render/Pipeline.hpp"
#include "render/VulkanContext.hpp"
#include "view/CameraController.hpp"
#include "view/Exposure.hpp"
#include "view/LineBatch.hpp"
#include "view/PlanetaryGrid.hpp"
#include "view/Pose.hpp"

#include <expected>

union SDL_Event;

namespace orb::app {

// The camera the application exposes the scene with (M1-15, register decision
// 176) -- the window's, and since M1-22 the benchmark's, which measures the
// window's frame (decision 351): **f/16, 1/125 s, ISO 100 -- the "sunny 16"
// rule**, a photographer's setting for a subject in direct sunlight, which is
// what milestone 1 draws.
// A sunlit surface of albedo 0.3 lands 0.89 stops above a metered mid-grey
// (tests/test_exposure.cpp). Not a tuning constant: it is a published rule
// with a stated purpose, and ADR 0014 leaves the choice of default open. The
// probes of M1-16 pin their own. Unwrapping a refused setting is not a
// constant expression, so a bad value here fails the build.
inline constexpr view::CameraSettings kDefaultCamera{
    .aperture = view::Aperture::from(16.0).value(),
    .shutterTime = view::ShutterTime::from(Seconds{1.0 / 125.0}).value(),
    .iso = view::Iso::from(100.0).value(),
};

// The rotation that places the grid: Earth-fixed to the world frame at the
// grid probes' date. The window's grid and the benchmark's path (M1-22) are
// both placed by it, so the path flies over the grid it was meant to.
[[nodiscard]] Quat gridWorldFromEarthFixed();

// What the window needs to turn an event into pixels: physical pixels per
// window unit, which is more than one on a high-density display, and the
// height of the drawn image in physical pixels, which a pan is a fraction of.
struct ScreenScale {
    f64 pixelsPerWindowUnit{};
    Pixels height;
};

class InteractiveView {
public:
    [[nodiscard]] static std::expected<InteractiveView, gfx::RenderError>
    create(gfx::VulkanContext& context);

    // One SDL event: a drag or a wheel moves the camera, anything else is
    // not this class's. A command the controller refuses leaves the camera
    // where it was, and says so on stderr (decision 357).
    void handle(const SDL_Event& event, const ScreenScale& scale);

    // Gathers the frame's lines from where the camera now is, uploads them
    // into the frame's slot, and records their draw into its rendering.
    [[nodiscard]] std::expected<void, gfx::RenderError>
    record(gfx::VulkanContext& context,
           const gfx::FrameContext& frame,
           const gfx::ScenePipelines& pipelines);

    // The same, from `pose` rather than from the controller's camera: what
    // the benchmark draws, each frame from its path (M1-22).
    [[nodiscard]] std::expected<void, gfx::RenderError>
    recordFrom(const view::Pose& pose,
               gfx::VulkanContext& context,
               const gfx::FrameContext& frame,
               const gfx::ScenePipelines& pipelines);

private:
    InteractiveView(const view::CameraController& controller,
                    view::PlanetaryGrid grid,
                    gfx::LineRenderer renderer);

    void apply(const view::CameraCommand& command);

    view::CameraController controller_;
    view::PlanetaryGrid grid_;
    view::LineBatch batch_;
    gfx::LineRenderer renderer_;
};

} // namespace orb::app

#endif // ORBSIM_APP_INTERACTIVEVIEW_HPP
