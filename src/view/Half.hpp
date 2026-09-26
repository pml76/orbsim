#ifndef ORBSIM_VIEW_HALF_HPP
#define ORBSIM_VIEW_HALF_HPP
//
// IEEE 754 binary16 ("half") to binary32, exactly (M1-16, register decision
// 198).
//
// **What it is for.** The HDR target is RGBA16F (kHdrFormat), so a read-back
// frame arrives as binary16 values, and the probe dump M1-18's tests read is
// binary32. Every binary16 value is exactly representable in binary32 --
// 11 significant bits into 24, exponents -24 to 15 into -149 to 127 -- so the
// conversion is exact and involves no rounding at all.
//
// **Written out rather than taken from the language or the hardware**, and the
// reasons were measured: `std::float16_t` is not provided by the Windows
// standard library this project builds against, MSVC has no `_Float16` at all,
// and the hardware conversion (F16C, which the build assumes since ADR 0023)
// turns a signalling NaN quiet. Here a NaN keeps every bit of its payload, so
// a dump says exactly what the GPU wrote.
//
// **Checked against two other implementations** in tests/test_half.cpp, on all
// 65,536 inputs: a formula written independently with std::ldexp, and, where
// the compiler has `_Float16`, the compiler's own conversion.
//
// The layouts, for reading the arithmetic below:
//
//     binary16   sign 1 | exponent 5  (bias 15)  | fraction 10
//     binary32   sign 1 | exponent 8  (bias 127) | fraction 23
//
#include "core/Scalar.hpp"

#include <bit>
#include <cstdint>
#include <limits>

namespace orb::view {

namespace detail {

inline constexpr std::uint32_t kHalfFractionBits = 10;
inline constexpr std::uint32_t kFloatFractionBits = 23;
// Moves a binary16 fraction into a binary32 fraction's high bits.
inline constexpr std::uint32_t kFractionShift = kFloatFractionBits - kHalfFractionBits;
inline constexpr std::uint32_t kHalfFractionMask = 0x03FFU;
inline constexpr std::uint32_t kHalfExponentMask = 0x1FU;
inline constexpr std::uint32_t kHalfExponentAllOnes = 0x1FU;
// A binary32 exponent field of all ones: infinity or NaN.
inline constexpr std::uint32_t kFloatExponentAllOnes = 0xFFU;
// The two biases, 127 - 15: what turns a binary16 exponent field into a
// binary32 one for a normal number.
inline constexpr std::uint32_t kBiasDifference = 127U - 15U;

} // namespace detail

// The binary32 value of a binary16 bit pattern. Exact for every input,
// signed zeros, subnormals, infinities and NaNs included.
[[nodiscard]] constexpr f32 halfToFloat(std::uint16_t bits) noexcept {
    const std::uint32_t half = bits;
    const std::uint32_t sign = (half >> 15U) << 31U;
    const std::uint32_t exponent = (half >> detail::kHalfFractionBits) & detail::kHalfExponentMask;
    const std::uint32_t fraction = half & detail::kHalfFractionMask;

    if (exponent == detail::kHalfExponentAllOnes) {
        // Infinity for a zero fraction, NaN otherwise; the payload moves with
        // the fraction, so a quiet NaN stays quiet and a signalling one
        // signalling.
        return std::bit_cast<f32>(sign |
                                  (detail::kFloatExponentAllOnes << detail::kFloatFractionBits) |
                                  (fraction << detail::kFractionShift));
    }
    if (exponent != 0) {
        return std::bit_cast<f32>(
            sign | ((exponent + detail::kBiasDifference) << detail::kFloatFractionBits) |
            (fraction << detail::kFractionShift));
    }
    if (fraction == 0) return std::bit_cast<f32>(sign); // a signed zero

    // A subnormal: fraction * 2^-24, which is a normal binary32 number. Shift
    // the fraction until its leading one reaches bit 10, the implicit bit's
    // place; each place shifted lowers the exponent by one below 2^-14.
    // countl_zero rather than a loop, so there is no iteration to bound. A
    // 16-bit value whose leading one sits at bit 10 has 16 - 11 = 5 leading
    // zeros, so the shift is the count less 5: the fraction is 1 to 1023, 6 to
    // 15 leading zeros, a shift of 1 to 10.
    const auto shift =
        static_cast<std::uint32_t>(std::countl_zero(static_cast<std::uint16_t>(fraction))) - 5U;
    const std::uint32_t normalised = (fraction << shift) & detail::kHalfFractionMask;
    const std::uint32_t floatExponent = detail::kBiasDifference + 1U - shift; // 2^(-14 - shift)
    return std::bit_cast<f32>(sign | (floatExponent << detail::kFloatFractionBits) |
                              (normalised << detail::kFractionShift));
}

static_assert(std::numeric_limits<f32>::is_iec559, "the layouts above are IEEE 754's");

// Each branch, at the compiler, on a value whose answer is known exactly.
static_assert(bitsOf(halfToFloat(0x0000U)) == bitsOf(0.0F), "positive zero");
static_assert(bitsOf(halfToFloat(0x8000U)) == bitsOf(-0.0F), "negative zero keeps its sign");
static_assert(bitsOf(halfToFloat(0x3C00U)) == bitsOf(1.0F), "one");
static_assert(bitsOf(halfToFloat(0xC000U)) == bitsOf(-2.0F), "minus two");
static_assert(bitsOf(halfToFloat(0x7BFFU)) == bitsOf(65504.0F), "the largest finite half");
static_assert(bitsOf(halfToFloat(0x0400U)) == bitsOf(0x1p-14F), "the smallest normal half");
static_assert(bitsOf(halfToFloat(0x0001U)) == bitsOf(0x1p-24F), "the smallest subnormal half");
static_assert(bitsOf(halfToFloat(0x03FFU)) == bitsOf(0x1.ff8p-15F), "the largest subnormal half");
static_assert(bitsOf(halfToFloat(0x7C00U)) == bitsOf(std::numeric_limits<f32>::infinity()),
              "infinity");
static_assert(bitsOf(halfToFloat(0x7D01U)) == 0x7FA0'2000U,
              "a signalling NaN keeps its payload and stays signalling");

} // namespace orb::view

#endif // ORBSIM_VIEW_HALF_HPP
