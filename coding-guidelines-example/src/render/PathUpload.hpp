#ifndef ORBEX_RENDER_PATHUPLOAD_HPP
#define ORBEX_RENDER_PATHUPLOAD_HPP
//
// The boundary between the simulation and the GPU.
//
// [S12] This is the renderer's namespace, and it includes no graphics API at
// all -- no Vulkan, no SDL. It takes plain data and returns plain data. That is
// what lets the physics be tested without a device and the renderer be replaced
// without touching a line of orbital mechanics. The dependency runs one way:
// render knows about core, core has never heard of render.
//
// [S17] Keeping heavy headers out of headers is also why the build stays fast.
// If this file included <vulkan/vulkan.h>, every test that touches a Vec3 would
// pay for it.
//
#include "core/Vec3.hpp"

#include <span>
#include <vector>

namespace orbex::gfx {

// What a vertex shader consumes. f32, because that is what a GPU has.
// [S4] Rule of Zero, [S6] every member default-initialized.
struct PathVertex {
    f32 x{};
    f32 y{};
    f32 z{};

    [[nodiscard]] constexpr bool operator==(const PathVertex&) const noexcept = default;
};

// Converts world-space f64 positions into camera-relative f32 vertices.
//
// [S11] This is the only function in the example that narrows f64 to f32 --
// three casts, one per component, and nowhere else. The narrowing lives in one
// named place so the precision boundary is something you can grep for; a
// static_cast<f32> appearing anywhere upstream is a jitter bug report waiting
// to be filed.
//
// [S18] Takes a span: it does not own the input, does not care how the caller
// stored it, and cannot silently copy it.
[[nodiscard]] std::vector<PathVertex> toCameraRelative(std::span<const Vec3> pathWorld,
                                                       const Vec3& cameraWorld);

} // namespace orbex::gfx

#endif // ORBEX_RENDER_PATHUPLOAD_HPP
