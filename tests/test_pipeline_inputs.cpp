//
// Tests for view/VertexLayout.hpp and view/PushConstants.hpp: what a graphics
// pipeline is told about its inputs, described without Vulkan (M1-13; ADR 0012
// for where it lives, register decisions 142-146 for its shape).
//
// **Nothing here includes a Vulkan or SDL header**, and that is the link
// graph's doing rather than anyone's discipline: this suite links orbsim_view,
// which links orbsim_core and nothing else (ADR 0012). src/render translates
// these descriptions into Vulkan's structs, one field to one field, and that
// half is exercised by orbsim_smoke under the validation layers.
//
// **Where the expected numbers come from.** Not from this code. Every offset,
// size and location below was read out of the compiled shaders with
// `spirv-cross --reflect` on 2026-09-24:
//
//   line.vert   inputs  vec3 at location 0, vec4 at location 1
//               push    mat4 at byte 0, vec4 at byte 64       -- 80 bytes
//   body.vert   inputs  vec3 at location 0
//   body.vert   push    mat4 at 0, vec4 at 64, vec4 at 80     -- 96 bytes
//   body.frag   push    the same block, read at 64 and 80
//   line.frag   push    none
//
// and the rules a push-constant range must keep are the Vulkan specification's
// valid-usage statements for VkPushConstantRange (VUID-...-00294 to -00298):
// the offset and the size multiples of 4, the size above zero, and the range
// inside maxPushConstantsSize, which the Required Limits table guarantees is at
// least 128 bytes in core Vulkan.
//
// **The compile-time half lives beside the types**, as test_projection.cpp
// keeps the projection's layout beside its builder: that the C++ structs have
// the size and member offsets the shaders read is a static_assert against the
// compiler's own layout, which is an independent opinion of it. What is here
// is the arithmetic, run at run time, and every refusal asked for by name.
//
#include "core/Scalar.hpp"
#include "view/PushConstants.hpp"
#include "view/VertexLayout.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <limits>

using namespace orb;
using namespace orb::view;

namespace {

// What an attribute should be, as the plain numbers read from the shader.
// A type of its own rather than a second VertexAttribute, so the two
// arguments below cannot be passed the wrong way round (non-negotiable 1).
struct ExpectedAttribute {
    std::uint32_t location{};
    AttributeFormat format{AttributeFormat::Float32x3};
    std::uint64_t offset{};
};

// One attribute, compared field by field, so a failure names the field.
void requireAttribute(const VertexAttribute& actual, const ExpectedAttribute& expected) {
    CAPTURE(actual.location.value, expected.location);
    CAPTURE(actual.offset.value(), expected.offset);
    REQUIRE(actual.location.value == expected.location);
    REQUIRE(actual.format == expected.format);
    REQUIRE(actual.offset.value() == expected.offset);
}

// A range's three fields, checked one by one so a failure names the field.
void requireRange(const PushConstantRange& range, ShaderStages stages, const ByteRange& bytes) {
    CAPTURE(bytes.offset.value(), bytes.size.value());
    REQUIRE(range.stages() == stages);
    REQUIRE(range.offset().value() == bytes.offset.value());
    REQUIRE(range.size().value() == bytes.size.value());
}

// A range the test expects to be accepted.
void requireAccepted(const std::expected<PushConstantRange, PushConstantError>& made,
                     ShaderStages stages,
                     const ByteRange& bytes) {
    REQUIRE(made.has_value());
    requireRange(*made, stages, bytes);
}

// A range the test expects to be refused, and why. `has_value()` first: reading
// error() from an expected that holds a value is undefined behaviour, and in
// practice compares equal to the first enumerator -- which is how a refusal
// test once passed while the factory accepted what it was written to refuse
// (VERIFICATION.md rule 23).
void requireRefused(const ByteRange& bytes, PushConstantError expected) {
    CAPTURE(bytes.offset.value(), bytes.size.value(), describe(expected));
    const auto made = PushConstantRange::from(ShaderStages::Vertex, bytes);
    REQUIRE(!made.has_value());
    REQUIRE(made.error() == expected);
}

} // namespace

// ------------------------------------------------------------ vertex input --

TEST_CASE("an attribute format is as wide as its components") {
    // Four bytes a component, because each is a 32-bit float.
    REQUIRE(sizeOf(AttributeFormat::Float32x3).value() == 12U);
    REQUIRE(sizeOf(AttributeFormat::Float32x4).value() == 16U);
}

TEST_CASE("a line vertex is laid out the way line.vert reads it") {
    REQUIRE(kLineVertexLayout.stride().value() == 28U);
    const auto& attributes = kLineVertexLayout.attributes();
    REQUIRE(attributes.size() == 2U);
    requireAttribute(std::get<0>(attributes),
                     {
                         .location = 0U,
                         .format = AttributeFormat::Float32x3,
                         .offset = 0U,
                     });
    requireAttribute(std::get<1>(attributes),
                     {
                         .location = 1U,
                         .format = AttributeFormat::Float32x4,
                         .offset = 12U,
                     });
}

TEST_CASE("a body vertex is laid out the way body.vert reads it") {
    REQUIRE(kBodyVertexLayout.stride().value() == 12U);
    const auto& attributes = kBodyVertexLayout.attributes();
    REQUIRE(attributes.size() == 1U);
    requireAttribute(std::get<0>(attributes),
                     {
                         .location = 0U,
                         .format = AttributeFormat::Float32x3,
                         .offset = 0U,
                     });
}

TEST_CASE("a packed layout numbers its attributes in order and adds up their widths") {
    // Neither shader's layout, deliberately: three attributes in an order where
    // a wide one comes first, so an offset taken from the wrong neighbour, a
    // location counted from one, or a stride that forgets the last attribute
    // each gives a different number. Worked by hand: 0, 16, 16 + 12 = 28, and
    // a stride of 28 + 16 = 44.
    const auto layout = VertexLayout<3>::packed({
        {
            AttributeFormat::Float32x4,
            AttributeFormat::Float32x3,
            AttributeFormat::Float32x4,
        },
    });
    REQUIRE(layout.stride().value() == 44U);
    const auto& attributes = layout.attributes();
    requireAttribute(std::get<0>(attributes),
                     {
                         .location = 0U,
                         .format = AttributeFormat::Float32x4,
                         .offset = 0U,
                     });
    requireAttribute(std::get<1>(attributes),
                     {
                         .location = 1U,
                         .format = AttributeFormat::Float32x3,
                         .offset = 16U,
                     });
    requireAttribute(std::get<2>(attributes),
                     {
                         .location = 2U,
                         .format = AttributeFormat::Float32x4,
                         .offset = 28U,
                     });
}

// ---------------------------------------------------------- push constants --

TEST_CASE("the push-constant ranges cover the blocks the shaders declare") {
    // line.vert alone reads its block; line.frag declares none.
    requireRange(
        kLinePushConstantRange, ShaderStages::Vertex, {.offset = Bytes{0U}, .size = Bytes{80U}});
    // body.vert reads the matrix and body.frag the two vectors after it, in
    // one block, so one range covers both stages (register decision 145).
    requireRange(kBodyPushConstantRange,
                 ShaderStages::VertexAndFragment,
                 {.offset = Bytes{0U}, .size = Bytes{96U}});
}

TEST_CASE("a push-constant range the specification allows is accepted") {
    // Both ends of the guaranteed space, and the smallest range there is.
    for (const ByteRange bytes : std::to_array<ByteRange>({
             {.offset = Bytes{0U}, .size = Bytes{4U}},
             {.offset = Bytes{0U}, .size = Bytes{128U}},
             {.offset = Bytes{124U}, .size = Bytes{4U}},
             {.offset = Bytes{64U}, .size = Bytes{64U}},
         })) {
        requireAccepted(
            PushConstantRange::from(ShaderStages::Fragment, bytes), ShaderStages::Fragment, bytes);
    }
}

TEST_CASE("a push-constant range the specification forbids is refused by name") {
    SECTION("an empty range") {
        requireRefused({.offset = Bytes{0U}, .size = Bytes{0U}}, PushConstantError::EmptyRange);
        requireRefused({.offset = Bytes{64U}, .size = Bytes{0U}}, PushConstantError::EmptyRange);
    }
    SECTION("an offset that is not a multiple of four") {
        for (const std::uint64_t offset : {1U, 2U, 3U, 5U, 122U}) {
            requireRefused({.offset = Bytes{offset}, .size = Bytes{4U}},
                           PushConstantError::MisalignedOffset);
        }
    }
    SECTION("a size that is not a multiple of four") {
        for (const std::uint64_t size : {1U, 2U, 3U, 6U, 127U}) {
            requireRefused({.offset = Bytes{0U}, .size = Bytes{size}},
                           PushConstantError::MisalignedSize);
        }
    }
    SECTION("a range reaching past the space every device guarantees") {
        requireRefused({.offset = Bytes{0U}, .size = Bytes{132U}},
                       PushConstantError::BeyondGuaranteedSpace);
        requireRefused({.offset = Bytes{128U}, .size = Bytes{4U}},
                       PushConstantError::BeyondGuaranteedSpace);
        requireRefused({.offset = Bytes{124U}, .size = Bytes{8U}},
                       PushConstantError::BeyondGuaranteedSpace);
    }
    SECTION("and one whose end does not fit in 64 bits, without wrapping round") {
        // offset + size would wrap to 3. Refused because the check never forms
        // the sum -- a range that wrapped would look small and be accepted.
        constexpr std::uint64_t kLargest = std::numeric_limits<std::uint64_t>::max();
        requireRefused({.offset = Bytes{kLargest - 3U}, .size = Bytes{8U}},
                       PushConstantError::BeyondGuaranteedSpace);
        requireRefused({.offset = Bytes{0U}, .size = Bytes{kLargest - 3U}},
                       PushConstantError::BeyondGuaranteedSpace);
    }
}
