#include "render/PathUpload.hpp" // [S14] SF.5: own header, first
#include "core/Vec3.hpp"

#include <algorithm>
#include <iterator>
#include <span>
#include <vector>

namespace orbex::gfx {

std::vector<PathVertex> toCameraRelative(std::span<const Vec3> pathWorld, const Vec3& cameraWorld) {
    std::vector<PathVertex> vertices;
    vertices.reserve(pathWorld.size()); // [S10] one allocation, size known

    // [S9] A genuine transform over existing data, so it is written as one
    // rather than as an index loop that could get its bound wrong. [S7] The
    // lambda cannot throw and says so (E.8); gcc's -Wnoexcept asks for exactly
    // that.
    std::ranges::transform(
        pathWorld, std::back_inserter(vertices), [&](const Vec3& point) noexcept {
            // [S11] Subtract in f64 FIRST, then narrow. At Earth-orbit radius an
            // f32 has metre-scale spacing, so narrowing before the subtraction
            // would round a ten-metre feature away before the camera offset ever
            // had a chance to remove the large magnitude.
            const Vec3 relative = point - cameraWorld;

            return PathVertex{
                .x = static_cast<f32>(relative.x),
                .y = static_cast<f32>(relative.y),
                .z = static_cast<f32>(relative.z),
            };
        });

    return vertices;
}

} // namespace orbex::gfx
