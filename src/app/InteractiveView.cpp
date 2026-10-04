#include "app/InteractiveView.hpp" // SF.5: own header, first
#include "astro/EarthOrientation.hpp"
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "render/LineRenderer.hpp"
#include "render/Pipeline.hpp"
#include "render/Probes.hpp"
#include "render/VulkanContext.hpp"
#include "view/Camera.hpp"
#include "view/CameraController.hpp"
#include "view/CameraInput.hpp"
#include "view/LineBatch.hpp"
#include "view/PlanetaryGrid.hpp"
#include "view/Pose.hpp"
#include "view/Projection.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <print>
#include <utility>

namespace orb::app {

namespace {

// The window's camera (decision 344): the probes' 45-degree field of view and
// 1 m near plane, so the window and the grid probes see alike.
constexpr Radians kFieldOfView{kPi / 4.0};
constexpr Metres kNearPlane{1.0};

// The grid's room: the whole grid, which is more than any camera sees of it,
// and the horizon -- as the grid probes size theirs (render/Probes.cpp).
[[nodiscard]] view::VertexCount capacityFor(const view::GridLayout& layout) {
    return layout.vertexCount() + view::VertexCount{2 * view::kHorizonSegments};
}

// Where the camera starts (decision 344): the focus where the Earth's
// equator and prime meridian cross at the grid's date, given to the
// controller as its place in the celestial frame; 1,000 km from it, looking
// north, tilted 30 degrees.
[[nodiscard]] view::ControllerStart startAbove(const Quat& worldFromEarthFixed) {
    const Direction crossing = worldFromEarthFixed.rotate(Direction{1.0, 0.0, 0.0});
    const f64 x = crossing.x.value();
    const f64 y = crossing.y.value();
    const f64 z = crossing.z.value();
    return view::ControllerStart{
        .focusRightAscension = Radians{std::atan2(y, x)},
        .focusDeclination = Radians{std::atan2(z, std::hypot(x, y))},
        .heading = Radians{0.0},
        .tilt = toRadians(Degrees{30.0}),
        .distance = Metres{1'000e3},
    };
}

} // namespace

Quat gridWorldFromEarthFixed() {
    const gfx::GridEpoch epoch = gfx::gridEpoch();
    // ERFA's rotation takes the celestial frame to the Earth-fixed one, and
    // the grid wants the other way, as the grid probes turn it.
    return earthFixedFromInertial(epoch.tt, epoch.ut1).conjugate();
}

InteractiveView::InteractiveView(const view::CameraController& controller,
                                 view::PlanetaryGrid grid,
                                 gfx::LineRenderer renderer)
    : controller_(controller),
      grid_(std::move(grid)),
      batch_(renderer.capacity()),
      renderer_(std::move(renderer)) {}

std::expected<InteractiveView, gfx::RenderError>
InteractiveView::create(gfx::VulkanContext& context) {
    const Quat worldFromEarthFixed = gridWorldFromEarthFixed();
    const auto layout = view::GridLayout::from(view::kEarthGridCounts);
    // The task's counts, which view/PlanetaryGrid.hpp asserts can be made.
    ORBSIM_ENSURES(layout.has_value());
    auto renderer = gfx::LineRenderer::create(context, capacityFor(*layout));
    if (!renderer) return std::unexpected(renderer.error());
    // Constants the factory accepts: a finite start within the limits, the
    // WGS-84 radius and a 45-degree view. A refusal is a defect here.
    const auto controller =
        view::CameraController::from(startAbove(worldFromEarthFixed),
                                     {
                                         .planetRadius = view::kWgs84SemiMajorAxis,
                                         .verticalFov = kFieldOfView,
                                     });
    ORBSIM_ENSURES(controller.has_value());
    return InteractiveView{
        *controller,
        view::PlanetaryGrid::make(view::kWgs84SemiMajorAxis, *layout, worldFromEarthFixed),
        *std::move(renderer),
    };
}

void InteractiveView::apply(const view::CameraCommand& command) {
    if (const auto applied = controller_.apply(command); !applied) {
        std::print(stderr, "Camera command refused: {}\n", view::describe(applied.error()));
    }
}

void InteractiveView::handle(const SDL_Event& event, const ScreenScale& scale) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        // SDL counts the window's units; the mapping counts physical pixels.
        const view::DragDelta drag{
            .right = Pixels{static_cast<f64>(event.motion.xrel) * scale.pixelsPerWindowUnit},
            .down = Pixels{static_cast<f64>(event.motion.yrel) * scale.pixelsPerWindowUnit},
        };
        if ((event.motion.state & SDL_BUTTON_LMASK) != 0U) {
            apply(view::orbitFrom(drag));
        } else if ((event.motion.state & SDL_BUTTON_RMASK) != 0U) {
            apply(view::panFrom(drag, scale.height));
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // Positive away from the user, unless the system scrolls "naturally",
        // when SDL reports the values reversed and says so (SDL_events.h).
        const bool flipped = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED;
        const std::int32_t away = flipped ? -event.wheel.integer_y : event.wheel.integer_y;
        if (away != 0) apply(view::dollyFrom({.away = away}));
        break;
    }
    default:
        break;
    }
}

std::expected<void, gfx::RenderError>
InteractiveView::record(gfx::VulkanContext& context,
                        const gfx::FrameContext& frame,
                        const gfx::ScenePipelines& pipelines) {
    return recordFrom(controller_.pose(), context, frame, pipelines);
}

std::expected<void, gfx::RenderError>
InteractiveView::recordFrom(const view::Pose& pose,
                            gfx::VulkanContext& context,
                            const gfx::FrameContext& frame,
                            const gfx::ScenePipelines& pipelines) {
    // The controller and a camera path make only finite positions and unit
    // orientations, and the field of view and near plane are constants the
    // factory accepts.
    const auto camera =
        view::Camera::from(pose.position, pose.orientation, kFieldOfView, kNearPlane);
    ORBSIM_ENSURES(camera.has_value());

    batch_.clear();
    [[maybe_unused]] const auto added =
        view::addVisibleGrid(batch_, grid_, view::kGridColours, *camera);
    [[maybe_unused]] const auto horizon =
        view::addHorizon(batch_, view::kWgs84SemiMajorAxis, view::kGridColours.horizon, *camera);
    // The batch holds the whole grid and the horizon, so neither can be
    // refused (capacityFor).
    ORBSIM_ENSURES(added.has_value() && horizon.has_value());
    auto lines = renderer_.upload(context, frame.frameIndex, batch_);
    if (!lines) return std::unexpected(lines.error());

    // A frame begins only with an image to draw into, so its extent is not
    // empty and the aspect is positive: a refusal is a defect here.
    const view::Aspect aspect{static_cast<f64>(frame.extent.width) /
                              static_cast<f64>(frame.extent.height)};
    const auto projection = view::infiniteReverseZPerspective(kFieldOfView, aspect, kNearPlane);
    ORBSIM_ENSURES(projection.has_value());
    renderer_.draw(frame.cmd,
                   *lines,
                   pipelines.lines(),
                   {
                       .viewProjection = view::toShaderMatrix(*projection * viewMatrix(*camera)),
                       // White: the tint multiplies every colour, drawn as
                       // the grid probes draw them.
                       .tint = {1.0F, 1.0F, 1.0F, 1.0F},
                   },
                   frame.quality);
    return {};
}

} // namespace orb::app
