#ifndef ORBSIM_VIEW_LINEBATCH_HPP
#define ORBSIM_VIEW_LINEBATCH_HPP
//
// The lines one frame draws, gathered on the CPU without Vulkan (M1-19; ADR
// 0012 for why it is here, register decisions 279 and 285 for its shape).
//
// **Every position goes through `toRenderSpace` and nowhere else.** A segment
// arrives in world metres, and the batch hands each end to view/Camera.hpp's
// one narrowing function for positions; it does no arithmetic of its own on a
// position, so the precision boundary stays where `grep static_cast<f32>
// src/` finds it. tests/test_line_batch.cpp holds every vertex bit for bit to
// `toRenderSpace` of its world point.
//
// **The capacity is fixed when the batch is made, and exceeding it is
// reported, not grown.** The storage is reserved once, by the constructor, so
// adding lines during a frame never allocates -- JPL's rule 3 in its
// frame-loop form (CODING_GUIDELINES section 20). A request that does not fit
// is refused whole, before anything is written: a caller that ignores the
// error draws the lines that did fit, never half a polyline.
//
// **A line list, two vertices per segment.** `shaders/line.vert` is drawn with
// a line-list topology, so a polyline of n points becomes n - 1 segments and
// 2(n - 1) vertices, its inner points written twice. A strip would be smaller,
// and is one more pipeline and one more way to draw; nothing needs it yet.
//
#include "core/Attributes.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Camera.hpp"
#include "view/VertexLayout.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace orb::view {

// A number of vertices: what a batch holds and what a draw is told to read.
// Counted in vertices rather than segments (register decision 285), because
// the vertex buffer is sized in vertices and vkCmdDraw takes a vertex count,
// so nothing converts between the two at either end.
struct VertexCount : Count<VertexCount> {
    constexpr VertexCount() noexcept = default;
    explicit constexpr VertexCount(std::uint32_t v) noexcept : Count{v} {}
};

// What adding lines can fail with. Reported rather than asserted, because a
// frame's line count comes from the scene -- how many orbits, how long their
// tracks -- which a scenario decides (ADR 0002).
enum class LineBatchError : std::uint8_t {
    OverCapacity, // the lines asked for do not fit in what is left
};

// A switch with one case, deliberately, and the check that objects is off at
// this line alone -- the owner's ruling of 2026-10-04 (M1-19), as decisions
// 91 and 186 ruled for describe(EphemerisError) and describe(CountError).
// With no `default`, a second error value is a -Wswitch compile error here, at
// the function that must then say what it means; written as an `if`, it would
// return "unknown line batch error" in silence.
[[nodiscard]] constexpr std::string_view describe(LineBatchError error) noexcept {
    // NOLINTNEXTLINE(readability-trivial-switch)
    switch (error) {
    case LineBatchError::OverCapacity:
        return "the lines do not fit in the batch's capacity; nothing was added";
    }
    return "unknown line batch error";
}

// One straight line, between two world positions. A struct rather than two
// parameters (register decision 285): both ends are a Position, and a
// function taking two would be the transposable pair non-negotiable 1 names.
struct Segment {
    Position from; // world metres
    Position to;   // world metres
};

// **The axes' colours: radiance in W/(m^2 sr), equally bright to the eye**
// (register decision 279; ADR 0014 says every shader writes radiance). Green
// at 130 W/(m^2 sr) -- about the lambert patch's radiance, which "sunny 16"
// shows mid-grey -- and red and blue at 130 times the green luminance weight
// divided by their own, from Rec. 709's weights 0.2126, 0.7152 and 0.0722:
// 437.33 and 1287.8, rounded to whole numbers. At equal radiance the blue
// axis was barely visible, found in the spike. **Held as `f32`**, already the
// width a vertex carries, so no narrowing function is needed for them.
inline constexpr Rgba kAxisXColour{.r = 437.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
inline constexpr Rgba kAxisYColour{.r = 0.0F, .g = 130.0F, .b = 0.0F, .a = 1.0F};
inline constexpr Rgba kAxisZColour{.r = 0.0F, .g = 0.0F, .b = 1288.0F, .a = 1.0F};

// The lines of one frame, as the vertices line.vert reads.
class LineBatch {
public:
    // Reserves room for `capacity` vertices: the batch's only allocation.
    explicit LineBatch(VertexCount capacity);

    // One segment: two vertices.
    [[nodiscard]] std::expected<void, LineBatchError>
    addSegment(const Segment& world, Rgba colour, const Camera& camera);

    // The segments joining consecutive points: 2(n - 1) vertices for n
    // points. **Fewer than two points add nothing and succeed** (register
    // decision 285): they have no segment, which is not a failure.
    [[nodiscard]] std::expected<void, LineBatchError>
    addPolyline(std::span<const Position> world, Rgba colour, const Camera& camera);

    // Three segments of `length` from `origin`, along world +X, +Y and +Z, in
    // kAxisXColour, kAxisYColour and kAxisZColour: six vertices. The length
    // must be finite and positive -- a defect in the caller otherwise.
    [[nodiscard]] std::expected<void, LineBatchError>
    addAxes(const Position& origin, Metres length, const Camera& camera);

    // Empties the batch and keeps its storage, so refilling it does not
    // allocate.
    void clear() noexcept;

    // A view of the batch's own storage, valid while the batch lives and
    // until the next add or clear -- which ORBSIM_LIFETIMEBOUND tells clang.
    [[nodiscard]] std::span<const LineVertex> vertices() const noexcept ORBSIM_LIFETIMEBOUND {
        return vertices_;
    }
    // The vector never holds more than the capacity, a 32-bit count, so the
    // conversion cannot lose anything.
    [[nodiscard]] VertexCount size() const noexcept {
        return VertexCount{static_cast<std::uint32_t>(vertices_.size())};
    }
    [[nodiscard]] VertexCount capacity() const noexcept { return capacity_; }

private:
    // Whether `more` vertices fit in what is left. Asked before anything is
    // written, which is what makes a refusal leave the batch unchanged.
    [[nodiscard]] bool fits(std::size_t more) const noexcept;
    void append(const Position& world, Rgba colour, const Camera& camera);

    std::vector<LineVertex> vertices_;
    VertexCount capacity_;
};

static_assert(describe(LineBatchError::OverCapacity) != "unknown line batch error",
              "the one error says what went wrong");
static_assert(sizeof(VertexCount) == sizeof(std::uint32_t), "a strong type must cost nothing");

} // namespace orb::view

#endif // ORBSIM_VIEW_LINEBATCH_HPP
