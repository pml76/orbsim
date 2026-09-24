#ifndef ORBSIM_VIEW_PUSHCONSTANTS_HPP
#define ORBSIM_VIEW_PUSHCONSTANTS_HPP
//
// The push constants a pipeline declares, without Vulkan (M1-13; ADR 0012
// for why it is here, register decisions 145-146 for its shape).
//
// **Push constants** are the few bytes a draw call carries with it -- a
// matrix and a colour -- without a buffer. A pipeline declares in advance
// which bytes each shader stage may read, as a list of *ranges*, and the
// Vulkan specification puts five rules on a range (the valid-usage statements
// of VkPushConstantRange, VUID-00294 to -00298, read from the specification's
// source on 2026-09-24):
//
//   * the offset is a multiple of 4, and so is the size;
//   * the size is greater than 0;
//   * the offset is less than maxPushConstantsSize, and the size is at most
//     maxPushConstantsSize minus the offset.
//
// Every one of them is a property of two whole numbers, so it is checked
// here, where a suite can check the check without a graphics card, and
// src/render/Pipeline.cpp only copies the fields across.
//
// **The limit checked is the one every device guarantees, 128 bytes**, not
// what this machine's card reports (256, measured with vulkaninfo on both of
// its GPUs). A range that fits in 128 fits on every Vulkan 1.3 device, which
// is the target, so there is nothing to ask the device and nothing that can
// differ between machines. The first block that needs more is the moment to
// ask the device, and it will fail here, by name, before that.
//
#include "core/Scalar.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace orb::view {

// Which shader stages may read a range. The three combinations there are,
// named, rather than a set of bits: nothing here needs to combine or test
// stages, and an enumerator cannot be half of a bitmask somebody forgot.
enum class ShaderStages : std::uint8_t {
    Vertex,
    Fragment,
    VertexAndFragment,
};

// The ways a range can be asked for and not be allowed. One value per rule in
// the specification's list above, so the caller is told which rule it broke.
// Checked in the order written here, so a range that breaks two rules is
// refused for the first: an empty range before anything else, and a range
// that does not fit before one that is merely misaligned.
enum class PushConstantError : std::uint8_t {
    EmptyRange,            // the size is zero
    BeyondGuaranteedSpace, // the range ends past kGuaranteedPushConstantSpace
    MisalignedOffset,      // the offset is not a multiple of 4
    MisalignedSize,        // the size is not a multiple of 4
};

[[nodiscard]] constexpr std::string_view describe(PushConstantError error) noexcept {
    switch (error) {
    case PushConstantError::EmptyRange:
        return "a push-constant range must not be empty";
    case PushConstantError::BeyondGuaranteedSpace:
        return "a push-constant range must end within the 128 bytes every device guarantees";
    case PushConstantError::MisalignedOffset:
        return "a push-constant range must start at a multiple of four bytes";
    case PushConstantError::MisalignedSize:
        return "a push-constant range must be a multiple of four bytes long";
    }
    return "unknown push-constant error";
}

// Both rules' multiple of 4: VUID-VkPushConstantRange-offset-00295 and
// -size-00297.
inline constexpr Bytes kPushConstantAlignment{4U};

// maxPushConstantsSize's guaranteed minimum in core Vulkan, from the Required
// Limits table of the specification ("128 ({core})"; Vulkan 1.4 raises it to
// 256, and this project targets 1.3).
inline constexpr Bytes kGuaranteedPushConstantSpace{128U};

// Where a range starts and how long it is. A struct rather than two
// parameters, because both are byte counts and reversed they describe a
// different range that may well be allowed -- non-negotiable 1's exact case,
// and the one register decision 140 named when it made `Bytes`.
struct ByteRange {
    Bytes offset;
    Bytes size;
};

// A push-constant range that keeps the specification's rules.
//
// **A private constructor and a factory** (ADR 0022): the only range that
// exists is one `from` accepted, so the translation into Vulkan's struct has
// nothing left to check.
class PushConstantRange {
public:
    [[nodiscard]] static constexpr std::expected<PushConstantRange, PushConstantError>
    from(ShaderStages stages, const ByteRange& bytes) noexcept {
        if (bytes.size == Bytes{0U}) return std::unexpected(PushConstantError::EmptyRange);
        // **The end is never formed as a sum.** offset + size can wrap round
        // 64 bits and come out small, which would be accepted; comparing the
        // offset against the space left after the size cannot, because the
        // size has already been checked to fit. The specification's two
        // bounds (VUID-00294 and -00298) reduce to this one, since the size
        // is not zero.
        if (bytes.size > kGuaranteedPushConstantSpace ||
            bytes.offset > kGuaranteedPushConstantSpace - bytes.size) {
            return std::unexpected(PushConstantError::BeyondGuaranteedSpace);
        }
        // Both values are at most 128 by now, so rounding them up cannot
        // carry past the end of 64 bits.
        if (bytes.offset.alignedUpTo(kPushConstantAlignment) != bytes.offset) {
            return std::unexpected(PushConstantError::MisalignedOffset);
        }
        if (bytes.size.alignedUpTo(kPushConstantAlignment) != bytes.size) {
            return std::unexpected(PushConstantError::MisalignedSize);
        }
        return PushConstantRange{stages, bytes};
    }

    // The range a whole block occupies, from its start: what a shader that
    // declares one `push_constant` block wants.
    template <typename Block>
    [[nodiscard]] static constexpr std::expected<PushConstantRange, PushConstantError>
    wholeBlock(ShaderStages stages) noexcept {
        return from(stages, {.offset = Bytes{0U}, .size = Bytes{sizeof(Block)}});
    }

    [[nodiscard]] constexpr ShaderStages stages() const noexcept { return stages_; }
    [[nodiscard]] constexpr Bytes offset() const noexcept { return bytes_.offset; }
    [[nodiscard]] constexpr Bytes size() const noexcept { return bytes_.size; }

private:
    constexpr PushConstantRange(ShaderStages stages, const ByteRange& bytes) noexcept
        : stages_(stages), bytes_(bytes) {}

    ShaderStages stages_;
    ByteRange bytes_;
};

// A GLSL mat4 or vec4 as it sits in a push-constant block: column-major
// floats, nothing between them.
using Mat4f = std::array<f32, 16>;
using Vec4f = std::array<f32, 4>;

// line.vert's block, member for member.
struct LinePushConstants {
    Mat4f viewProjection{};
    Vec4f tint{};
};

// body.vert's and body.frag's block, member for member. The vertex stage
// reads the matrix and the fragment stage the two vectors after it.
struct BodyPushConstants {
    Mat4f modelViewProjection{};
    Vec4f sunDirection{};
    Vec4f colour{};
};

inline constexpr PushConstantRange kLinePushConstantRange =
    PushConstantRange::wholeBlock<LinePushConstants>(ShaderStages::Vertex).value();

// One range for both stages, over the whole block (register decision 145):
// the two shaders declare the same block, and the specification forbids two
// ranges naming the same stage (VUID-VkPipelineLayoutCreateInfo-
// pPushConstantRanges-00292), so one range is the simplest shape that is
// right.
inline constexpr PushConstantRange kBodyPushConstantRange =
    PushConstantRange::wholeBlock<BodyPushConstants>(ShaderStages::VertexAndFragment).value();

// The blocks against the shaders, read from the compiled modules with
// `spirv-cross --reflect` on 2026-09-24: line.vert's members at 0 and 64, 80
// bytes; body.vert's and body.frag's at 0, 64 and 80, 96 bytes. These are the
// compiler's sizeof and offsetof, so a member added, dropped or moved on the
// C++ side fails the build. A change on the GLSL side is not seen here -- the
// numbers were copied from it -- and is what the validation layers report.
static_assert(std::is_standard_layout_v<LinePushConstants> &&
                  std::is_standard_layout_v<BodyPushConstants>,
              "offsetof is defined on both");
static_assert(sizeof(LinePushConstants) == 80U && offsetof(LinePushConstants, tint) == 64U,
              "line.vert's block: a mat4 at 0 and a vec4 at 64");
static_assert(sizeof(BodyPushConstants) == 96U &&
                  offsetof(BodyPushConstants, sunDirection) == 64U &&
                  offsetof(BodyPushConstants, colour) == 80U,
              "body's block: a mat4 at 0 and vec4s at 64 and 80");

// The rules, while the compiler runs. Unwrapping a refused range is not a
// constant expression, so these are also what make a constant like the two
// above fail the build rather than the program.
static_assert(!PushConstantRange::from(ShaderStages::Vertex,
                                       {.offset = Bytes{0U}, .size = Bytes{0U}})
                   .has_value(),
              "an empty range is refused");
static_assert(!PushConstantRange::from(ShaderStages::Vertex,
                                       {.offset = Bytes{2U}, .size = Bytes{4U}})
                   .has_value(),
              "so is one that starts off a multiple of four");
static_assert(!PushConstantRange::from(ShaderStages::Vertex,
                                       {.offset = Bytes{0U}, .size = Bytes{6U}})
                   .has_value(),
              "and one whose length is not a multiple of four");
static_assert(!PushConstantRange::from(ShaderStages::Vertex,
                                       {.offset = Bytes{128U}, .size = Bytes{4U}})
                   .has_value(),
              "and one that starts where the guaranteed space ends");
static_assert(PushConstantRange::from(ShaderStages::Vertex,
                                      {.offset = Bytes{124U}, .size = Bytes{4U}})
                  .has_value(),
              "while the last four bytes of it are allowed");

static_assert(describe(PushConstantError::EmptyRange) !=
                  describe(PushConstantError::BeyondGuaranteedSpace),
              "each error says which rule was broken");
static_assert(describe(PushConstantError::BeyondGuaranteedSpace) !=
              describe(PushConstantError::MisalignedOffset));
static_assert(describe(PushConstantError::MisalignedOffset) !=
              describe(PushConstantError::MisalignedSize));

} // namespace orb::view

#endif // ORBSIM_VIEW_PUSHCONSTANTS_HPP
