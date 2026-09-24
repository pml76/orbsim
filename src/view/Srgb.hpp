#ifndef ORBSIM_VIEW_SRGB_HPP
#define ORBSIM_VIEW_SRGB_HPP
//
// The sRGB transfer function, on the CPU (M1-14; ADR 0014).
//
// **What it is for.** The renderer draws light into a linear floating-point
// target and encodes it for the display exactly once, at the very end, in
// shaders/tonemap.frag. This file is the same encode written for the CPU: the
// reference the probe comparison in M1-18 and the golden-image tooling hold
// the GPU's output against, and the decode that turns a display colour chosen
// by eye into the linear value the HDR target has to hold.
//
// **The definition is IEC 61966-2-1:1999**, the sRGB standard, amendment 1:
//
//     encode  V = 12.92 L                     for 0 <= L <= 0.0031308
//             V = 1.055 L^(1/2.4) - 0.055     for 0.0031308 < L <= 1
//     decode  L = V / 12.92                   for 0 <= V <= 0.04045
//             L = ((V + 0.055) / 1.055)^2.4   for 0.04045 < V <= 1
//
// **The standard's rounded constants leave the curve slightly broken at its
// knees, and this code keeps the definition rather than smoothing it.** The two
// branches of the encode differ by -2.85e-8 at 0.0031308, so the function
// steps *down* there; those of the decode differ by 2.33e-9 at 0.04045. The
// consequence is measured, not assumed: an encode followed by a decode returns
// its input to 4.4e-16 everywhere except a window 7.3e-9 wide just above the
// linear knee, where it is out by up to 2.33e-9 -- the decode's jump, because a
// value there encodes below 0.04045 and so decodes on the other branch. That
// is the standard's property, not this method's, and tests/test_srgb.cpp
// asserts both halves separately. Every byte a display can show is unaffected:
// 2.33e-9 is a millionth of an 8-bit step.
//
// **Two types, so that encoding twice does not compile.** ADR 0014 keeps the
// encode explicit and in one place so that it cannot be applied twice by
// accident; on the CPU the type system is what enforces that. Neither type
// has arithmetic: linear light adds, but nothing here needs it, and adding two
// encoded values means nothing at all.
//
// **Outside [0, 1] a value is clamped, as the GPU does.** The display target
// is UNORM, which clamps on write, and tonemap.frag clamps before its power
// function because GLSL leaves pow() of a negative number undefined. The CPU
// mirrors that so that it mirrors the GPU. A NaN is a precondition failure:
// only a defect upstream produces one (ADR 0002).
//
// Double precision, although the GPU computes in single: this is the
// reference, and M1-18 compares at 1/255, where the difference is invisible.
// Not constexpr: std::pow is not a constant expression before C++26.
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace orb::view {

// IEC 61966-2-1:1999, amendment 1. Named once so the encode and the decode
// cannot drift apart, and so a reader can check them against the standard.
inline constexpr f64 kSrgbLinearKnee = 0.0031308; // linear value where the encode turns
inline constexpr f64 kSrgbEncodedKnee = 0.04045;  // encoded value where the decode turns
inline constexpr f64 kSrgbLinearSlope = 12.92;    // gradient of the linear segment
inline constexpr f64 kSrgbScale = 1.055;          // power segment: scale ...
inline constexpr f64 kSrgbOffset = 0.055;         // ... offset ...
inline constexpr f64 kSrgbExponent = 2.4;         // ... and exponent

// One channel of light relative to the display's white, before the encode.
class LinearValue {
public:
    explicit constexpr LinearValue(f64 value) noexcept : value_(value) {}
    [[nodiscard]] constexpr f64 value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool bitIdentical(LinearValue other) const noexcept {
        return bitsOf(value_) == bitsOf(other.value_);
    }

private:
    f64 value_;
};

// One channel as the display receives it, after the encode.
class EncodedValue {
public:
    explicit constexpr EncodedValue(f64 value) noexcept : value_(value) {}
    [[nodiscard]] constexpr f64 value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool bitIdentical(EncodedValue other) const noexcept {
        return bitsOf(value_) == bitsOf(other.value_);
    }

private:
    f64 value_;
};

[[nodiscard]] inline EncodedValue srgbEncode(LinearValue linear) noexcept {
    ORBSIM_EXPECTS(!isNaN(linear.value()));
    const f64 x = std::clamp(linear.value(), 0.0, 1.0);
    if (x <= kSrgbLinearKnee) return EncodedValue{kSrgbLinearSlope * x};
    return EncodedValue{(kSrgbScale * std::pow(x, 1.0 / kSrgbExponent)) - kSrgbOffset};
}

[[nodiscard]] inline LinearValue srgbDecode(EncodedValue encoded) noexcept {
    ORBSIM_EXPECTS(!isNaN(encoded.value()));
    const f64 v = std::clamp(encoded.value(), 0.0, 1.0);
    if (v <= kSrgbEncodedKnee) return LinearValue{v / kSrgbLinearSlope};
    return LinearValue{std::pow((v + kSrgbOffset) / kSrgbScale, kSrgbExponent)};
}

// The point of the two types, checked rather than claimed: neither converts
// into the other or from a bare number, so srgbEncode(srgbEncode(x)) and
// srgbEncode(0.5) do not compile.
static_assert(!std::is_convertible_v<EncodedValue, LinearValue> &&
                  !std::is_convertible_v<LinearValue, EncodedValue>,
              "an encoded value is not a linear one, in either direction");
static_assert(!std::is_convertible_v<f64, LinearValue> && !std::is_convertible_v<f64, EncodedValue>,
              "a bare number is neither until somebody says which");

} // namespace orb::view

#endif // ORBSIM_VIEW_SRGB_HPP
