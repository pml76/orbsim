#include "view/LineBatch.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/VertexLayout.hpp"

#include <cstddef>
#include <expected>
#include <ranges>
#include <span>

namespace orb::view {

LineBatch::LineBatch(VertexCount capacity) : capacity_(capacity) {
    // The one allocation. Nothing after this grows the vector: every add asks
    // fits() first, and a request that does not fit writes nothing.
    vertices_.reserve(capacity.value());
}

std::expected<void, LineBatchError>
LineBatch::addSegment(const Segment& world, Rgba colour, const Camera& camera) {
    if (!fits(2)) return std::unexpected(LineBatchError::OverCapacity);
    append(world.from, colour, camera);
    append(world.to, colour, camera);
    return {};
}

std::expected<void, LineBatchError>
LineBatch::addPolyline(std::span<const Position> world, Rgba colour, const Camera& camera) {
    if (world.size() < 2) return {};
    // Asked in size_t, where 2(n - 1) cannot wrap for any span that exists:
    // n is at most the number of Positions addressable, 24 bytes each.
    if (!fits(2 * (world.size() - 1))) return std::unexpected(LineBatchError::OverCapacity);
    // Each consecutive pair once, with no index to get wrong at either end.
    for (const auto [from, to] : world | std::views::pairwise) {
        append(from, colour, camera);
        append(to, colour, camera);
    }
    return {};
}

std::expected<void, LineBatchError>
LineBatch::addAxes(const Position& origin, Metres length, const Camera& camera) {
    // Computed outside the assertion, so isFinite is used in every build and
    // both trees' include checks agree that core/Scalar.hpp provides it
    // (CODING_GUIDELINES section 2's shape for an asserted result).
    [[maybe_unused]] const bool usableLength = isFinite(length.value()) && length.value() > 0.0;
    ORBSIM_EXPECTS(usableLength);
    if (!fits(6)) return std::unexpected(LineBatchError::OverCapacity);
    // The ends in world metres, in f64: toRenderSpace narrows each of them.
    const Position x{origin.x + length, origin.y, origin.z};
    const Position y{origin.x, origin.y + length, origin.z};
    const Position z{origin.x, origin.y, origin.z + length};
    append(origin, kAxisXColour, camera);
    append(x, kAxisXColour, camera);
    append(origin, kAxisYColour, camera);
    append(y, kAxisYColour, camera);
    append(origin, kAxisZColour, camera);
    append(z, kAxisZColour, camera);
    return {};
}

void LineBatch::clear() noexcept {
    // clear() keeps a vector's capacity, which the standard guarantees
    // ([vector.modifiers]: no reallocation, and capacity() unchanged).
    vertices_.clear();
}

bool LineBatch::fits(std::size_t more) const noexcept {
    // Compared against what is left rather than as a sum, so no addition can
    // wrap; vertices_.size() never exceeds the capacity, so the difference
    // cannot either.
    return more <= std::size_t{capacity_.value()} - vertices_.size();
}

void LineBatch::append(const Position& world, Rgba colour, const Camera& camera) {
    // Within the reserved storage, by fits(): this never allocates.
    ORBSIM_EXPECTS(vertices_.size() < std::size_t{capacity_.value()});
    vertices_.push_back(LineVertex{.position = toRenderSpace(world, camera), .colour = colour});
}

} // namespace orb::view
