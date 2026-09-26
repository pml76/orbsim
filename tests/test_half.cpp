//
// Tests for view/Half.hpp: binary16 to binary32, exactly (M1-16, register
// decision 198).
//
// **All 65,536 inputs, against two implementations that share no code with
// it.** The conversion is exact, so the claim is bit identity, and a bit
// pattern space this small is cheaper to cover than to sample.
//
//   * **A formula written from the definition** with std::ldexp: a finite
//     binary16 value is (-1)^s * m * 2^(e - 25), with m the fraction plus the
//     implicit 1024 for a normal number and e the exponent field (1 for a
//     subnormal). ldexp scales exactly, and the product fits in binary32, so
//     the reference is exact too. It covers every finite input; infinities
//     and NaNs are checked by what the definition says of them instead.
//   * **The compiler's own `_Float16`**, where there is one -- clang and gcc.
//     With F16C assumed (ADR 0023) that is the hardware instruction, and the
//     hardware turns a signalling NaN quiet where this code keeps its bits:
//     measured on 2026-09-26, 0x7D01 becomes 0x7FE02000 there and 0x7FA02000
//     here. So the two are compared on every input except the 1,022
//     signalling NaNs, which are compared with the quiet bit set.
//
#include "core/Scalar.hpp"
#include "view/Half.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <ios>

using namespace orb;
using namespace orb::view;

namespace {

constexpr std::uint32_t kPatterns = 65'536;

// The three fields of a binary16 pattern, read independently of Half.hpp.
struct HalfFields {
    bool negative{};
    std::uint32_t exponent{};
    std::uint32_t fraction{};
};

[[nodiscard]] HalfFields fieldsOf(std::uint32_t pattern) {
    return {
        .negative = (pattern & 0x8000U) != 0,
        .exponent = (pattern >> 10U) & 0x1FU,
        .fraction = pattern & 0x3FFU,
    };
}

// The definition's value for a finite pattern, by std::ldexp.
[[nodiscard]] f32 referenceValue(const HalfFields& fields) {
    const bool subnormal = fields.exponent == 0;
    const f64 significand = static_cast<f64>(fields.fraction + (subnormal ? 0U : 1024U));
    const int power = static_cast<int>(subnormal ? 1U : fields.exponent) - 25;
    const f64 magnitude = std::ldexp(significand, power);
    // Exact: the magnitude has at most 11 significant bits and lies in
    // binary32's range, so narrowing it rounds nothing.
    return static_cast<f32>(fields.negative ? -magnitude : magnitude);
}

} // namespace

TEST_CASE("every finite half converts to the value its definition gives") {
    std::uint32_t checked = 0;
    for (std::uint32_t pattern = 0; pattern < kPatterns; ++pattern) {
        const HalfFields fields = fieldsOf(pattern);
        if (fields.exponent == 0x1FU) continue; // infinities and NaNs: the next case
        const f32 got = halfToFloat(static_cast<std::uint16_t>(pattern));
        const f32 want = referenceValue(fields);
        INFO("pattern 0x" << std::hex << pattern);
        REQUIRE(bitsOf(got) == bitsOf(want));
        ++checked;
    }
    // 63,488 finite patterns: all but the 2 * 1,024 with an all-ones exponent.
    REQUIRE(checked == 63'488U);
}

TEST_CASE("infinities stay infinite and every NaN keeps its payload") {
    for (std::uint32_t pattern = 0; pattern < kPatterns; ++pattern) {
        const HalfFields fields = fieldsOf(pattern);
        if (fields.exponent != 0x1FU) continue;
        const std::uint32_t bits = bitsOf(halfToFloat(static_cast<std::uint16_t>(pattern)));
        INFO("pattern 0x" << std::hex << pattern << " -> 0x" << bits);
        REQUIRE(((bits >> 31U) != 0) == fields.negative);
        REQUIRE(((bits >> 23U) & 0xFFU) == 0xFFU); // all-ones exponent
        // The binary16 fraction, quiet bit included, lands in the top ten
        // fraction bits and nothing else is set: an infinity for zero, the
        // same NaN for anything else.
        REQUIRE((bits & 0x007F'FFFFU) == (fields.fraction << 13U));
    }
}

#ifdef __FLT16_MANT_DIG__
TEST_CASE("the conversion agrees with the compiler's own _Float16") {
    std::uint32_t signalling = 0;
    for (std::uint32_t pattern = 0; pattern < kPatterns; ++pattern) {
        const HalfFields fields = fieldsOf(pattern);
        const auto half = std::bit_cast<_Float16>(static_cast<std::uint16_t>(pattern));
        const std::uint32_t theirs = bitsOf(static_cast<f32>(half));
        std::uint32_t ours = bitsOf(halfToFloat(static_cast<std::uint16_t>(pattern)));
        // A signalling NaN: all-ones exponent, quiet bit (fraction bit 9)
        // clear, fraction non-zero. The hardware sets the quiet bit.
        if (fields.exponent == 0x1FU && fields.fraction != 0 && (fields.fraction & 0x200U) == 0) {
            ours |= 0x0040'0000U;
            ++signalling;
        }
        INFO("pattern 0x" << std::hex << pattern);
        REQUIRE(ours == theirs);
    }
    REQUIRE(signalling == 1'022U);
}
#endif
