#ifndef ORBSIM_VIEW_VERTEXLAYOUT_HPP
#define ORBSIM_VIEW_VERTEXLAYOUT_HPP
//
// What a graphics pipeline is told about one vertex, without Vulkan (M1-13;
// ADR 0012 for why it is here, register decisions 142-144 for its shape).
//
// **Why a second vocabulary for something Vulkan already describes.** The
// pipeline's vertex input is two Vulkan structs, and nothing under tests/ may
// include a Vulkan header -- which the link graph enforces, not a reviewer
// (ADR 0012). So the arithmetic that decides the numbers lives here, where a
// suite can check it without a graphics card, and src/render/Pipeline.cpp
// copies it into Vulkan's structs one field to one field.
//
// **Locations and offsets are computed, never written.** A layout is built
// from its formats in order: the n-th attribute gets location n, and its
// offset is the sum of the widths before it. A location typed twice or an
// offset typed wrong is therefore not a thing that can be written. What *can*
// still be wrong is that the C++ struct the vertices live in is laid out
// differently from what was summed -- padding the compiler added, a member in
// another order -- and that is asserted below each vertex type against the
// compiler's own `sizeof` and `offsetof`, which is an opinion formed
// independently of this code (VERIFICATION.md rule 2).
//
// **The shaders are the other party**, and the numbers below were read out of
// them rather than out of the GLSL: `spirv-cross --reflect` on the compiled
// modules, 2026-09-24. line.vert reads a vec3 at location 0 and a vec4 at
// location 1; body.vert reads a vec3 at location 0. Nothing checks that
// agreement at build time -- a pipeline whose layout disagrees with its
// shader is what the validation layers report, and orbsim_smoke fails on it.
//
#include "core/Scalar.hpp"
#include "view/Camera.hpp" // Vec3f

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <utility>

namespace orb::view {

// The formats the shaders in this project read, and no others. The value of
// each is its number of 32-bit float components, which is what makes its
// width a multiplication rather than a table somebody has to keep in step.
// A format is added when a shader first reads one.
enum class AttributeFormat : std::uint8_t {
    Float32x3 = 3, // a GLSL vec3
    Float32x4 = 4, // a GLSL vec4
};

// How many bytes one attribute of this format occupies.
[[nodiscard]] constexpr Bytes sizeOf(AttributeFormat format) noexcept {
    return Bytes{sizeof(f32)} * static_cast<std::uint32_t>(format);
}

// A shader's `layout(location = n)`. A type of its own rather than a bare
// integer, so it cannot be handed to a parameter that wants a byte offset --
// the two sit side by side in every attribute.
struct ShaderLocation {
    std::uint32_t value{};
};

// One input of a vertex shader: where the shader reads it, what it is, and
// where it sits in the vertex.
struct VertexAttribute {
    ShaderLocation location;
    AttributeFormat format{AttributeFormat::Float32x3};
    Bytes offset;
};

// A vertex's attributes and the distance from one vertex to the next.
//
// **A private constructor and one factory, `packed`**, so the only layout that
// exists is one whose locations and offsets were computed (ADR 0022's shape,
// here for a value that is not a scalar). "Packed" because nothing is inserted
// between attributes: every format here is a run of 4-byte floats, so each
// offset is already 4-byte aligned and the compiler lays a struct of them out
// with no padding -- which the assertions under each vertex type confirm
// rather than assume.
template <std::size_t kCount> class VertexLayout {
public:
    [[nodiscard]] static constexpr VertexLayout
    packed(const std::array<AttributeFormat, kCount>& formats) noexcept {
        return build(formats, std::make_index_sequence<kCount>{});
    }

    [[nodiscard]] constexpr Bytes stride() const noexcept { return stride_; }
    // By value, not by reference: a handful of 24-byte records, and a copy
    // cannot outlive the layout it came from. A reference would want
    // [[clang::lifetimebound]], whose portable spelling lives in src/render and
    // is not worth moving down for this.
    [[nodiscard]] constexpr std::array<VertexAttribute, kCount> attributes() const noexcept {
        return attributes_;
    }

private:
    constexpr VertexLayout(const std::array<VertexAttribute, kCount>& attributes,
                           Bytes stride) noexcept
        : attributes_(attributes), stride_(stride) {}

    // The width of the first `count` attributes: the offset of the next one,
    // and with `count == kCount` the stride. A fold rather than a loop, so
    // there is no index to get wrong at either end.
    [[nodiscard]] static constexpr Bytes
    widthOfFirst(const std::array<AttributeFormat, kCount>& formats, std::size_t count) noexcept {
        return std::ranges::fold_left(
            formats | std::views::take(count),
            Bytes{0U},
            [](Bytes sum, AttributeFormat format) noexcept { return sum + sizeOf(format); });
    }

    // Every attribute built in one expansion, so the array is initialised
    // element by element rather than zeroed first -- a zeroed format is not an
    // enumerator, which bugprone-invalid-enum-default-initialization reports.
    template <std::size_t... kIndex>
    [[nodiscard]] static constexpr VertexLayout
    build(const std::array<AttributeFormat, kCount>& formats,
          std::index_sequence<kIndex...> /*positions*/) noexcept {
        return VertexLayout{
            {
                {VertexAttribute{
                    .location = ShaderLocation{static_cast<std::uint32_t>(kIndex)},
                    .format = std::get<kIndex>(formats),
                    .offset = widthOfFirst(formats, kIndex),
                }...},
            },
            widthOfFirst(formats, kCount),
        };
    }

    std::array<VertexAttribute, kCount> attributes_;
    Bytes stride_;
};

// A colour as four 32-bit floats, in the order a GLSL vec4 reads them.
// What the numbers mean -- linear radiance, a display colour -- is the
// caller's to say; M1-19 is the first caller.
struct Rgba {
    f32 r{};
    f32 g{};
    f32 b{};
    f32 a{};
};

// What line.vert reads: a position already in render space, from
// `toRenderSpace` and nowhere else (M1-11), and a colour.
struct LineVertex {
    Vec3f position;
    Rgba colour;
};

inline constexpr VertexLayout<2> kLineVertexLayout = VertexLayout<2>::packed({
    {
        AttributeFormat::Float32x3,
        AttributeFormat::Float32x4,
    },
});

// What body.vert reads: a position on a unit sphere, which is also its
// normal. A `Vec3f` and nothing else, so no struct of its own.
using BodyVertex = Vec3f;

inline constexpr VertexLayout<1> kBodyVertexLayout =
    VertexLayout<1>::packed({{AttributeFormat::Float32x3}});

// The guaranteed minimum of maxVertexInputBindingStride, from the Required
// Limits table of the Vulkan specification. Every attribute ends inside the
// stride, so this also keeps every offset under maxVertexInputAttributeOffset's
// guaranteed 2047.
inline constexpr Bytes kGuaranteedVertexStride{2048U};

// The claims the layouts make about the C++ structs, checked against the
// compiler's own layout of them -- the independent opinion this header's
// comment promises. A member added, removed, reordered or padded in either
// struct fails here, while the build runs, and names the struct.
static_assert(std::is_standard_layout_v<LineVertex> && std::is_trivially_copyable_v<LineVertex>,
              "offsetof is defined on it, and a buffer of them is a copy of bytes");
static_assert(kLineVertexLayout.stride() == Bytes{sizeof(LineVertex)},
              "the layout's stride is the compiler's sizeof(LineVertex): no padding, nothing "
              "missing");
static_assert(std::get<0>(kLineVertexLayout.attributes()).offset ==
                  Bytes{offsetof(LineVertex, position)},
              "the position is where the compiler put it");
static_assert(std::get<1>(kLineVertexLayout.attributes()).offset ==
                  Bytes{offsetof(LineVertex, colour)},
              "and so is the colour");
static_assert(kBodyVertexLayout.stride() == Bytes{sizeof(BodyVertex)},
              "a body vertex is one Vec3f, and the layout says so");
static_assert(kLineVertexLayout.stride() <= kGuaranteedVertexStride &&
                  kBodyVertexLayout.stride() <= kGuaranteedVertexStride,
              "every device accepts these strides");

} // namespace orb::view

#endif // ORBSIM_VIEW_VERTEXLAYOUT_HPP
