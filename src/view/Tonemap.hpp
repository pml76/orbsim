#ifndef ORBSIM_VIEW_TONEMAP_HPP
#define ORBSIM_VIEW_TONEMAP_HPP
//
// The AgX tonemap, on the CPU (M1-15; ADR 0014).
//
// **What it is for.** shaders/tonemap.frag is the renderer's display
// transform: exposure, then AgX, then the sRGB encode, once, at the end. This
// file is the same AgX in double precision, for the probe comparison in M1-18
// and the golden-image tooling. **Comparing the two is a port check, not a
// validation**: a display transform is a choice, not a physical claim, so
// what can be verified is that one choice is implemented twice identically.
// What *is* checked against something outside this code is this file itself:
// tests/test_tonemap.cpp holds it to scripts/tonemap-reference.py, which
// evaluates the same definition in 50-digit arithmetic.
//
// **The source.** Benjamin Wrensch's minimal AgX implementation,
// https://iolite-engine.com/blog_posts/minimal_agx_implementation, under the
// MIT licence:
//
//     Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch)
//
//     Permission is hereby granted, free of charge, to any person obtaining a
//     copy of this software and associated documentation files (the
//     "Software"), to deal in the Software without restriction, including
//     without limitation the rights to use, copy, modify, merge, publish,
//     distribute, sublicense, and/or sell copies of the Software, and to
//     permit persons to whom the Software is furnished to do so, subject to
//     the following conditions:
//
//     The above copyright notice and this permission notice shall be included
//     in all copies or substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//     OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//     MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//     IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//     CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//     TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//     SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Its constants derive from Troy Sobotka's AgX configuration,
// https://github.com/sobotka/AgX: the inset matrix and the log2 range are
// copied from its config.ocio, and the polynomial is a fit of its
// AgX_Default_Contrast curve. THIRD_PARTY.md has the record.
//
// **What is copied exactly, and what is added** (register decision 174). The
// constants, the 6th-order polynomial, the order of the steps and the
// matrix's place -- applied to the curve's output directly, as Wrensch does;
// Sobotka has said publicly that applying it after undoing the 2.2 curve is
// equally correct -- and no "look", which would be a colour grade. Three
// guards are added, each because the reference leaves a case undefined:
//
//   * negative input is clamped to zero before the matrix, as Sobotka's own
//     configuration does with a RangeTransform;
//   * each channel is raised to at least 2^minEv before log2, because GLSL
//     leaves log2(0) undefined -- no new constant, since the clamp that
//     follows would put it there anyway;
//   * the output is clamped to [0, 1] before the 2.2 power, because GLSL
//     leaves pow() of a negative number undefined, and the curve does go
//     negative: it is -0.00232 at 0.
//
// And one change of form, not of value: the polynomial is evaluated by
// Horner's rule rather than as a sum of powers. Same coefficients, fewer
// roundings (register decision 174).
//
// **What the output is.** The curve's output is display-encoded for a display
// that decodes with a 2.2 power -- Sobotka's "AgX Base" -- so, as Wrensch's
// agxEotf does, this raises it to 2.2 and returns *linear display light*,
// which view/Srgb.hpp's encode then turns into display values (register
// decision 173). The return type is LinearValue, so encoding is the next step
// by type and cannot be skipped or doubled.
//
// **What it does to the numbers**, measured against the reference before the
// tests were written: 0 maps to exactly 0, and so does every grey up to
// 2.17e-4, where the curve crosses zero; grey is strictly increasing from
// there to 16.3, where the log clamps; and a grey of 0.18 -- mid-grey --
// comes out 0.2145, which the sRGB encode puts on display code 128. Outside
// those 4.88 decades it is flat: AgX's log range is 16.5 stops, which is a
// property of the transform, not a defect.
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "view/Srgb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace orb::view {

// Scene light after exposure: dimensionless, linear Rec. 709, with 0.18 the
// value a metered mid-grey lands on. What view/Exposure.hpp's factor makes of
// a radiance.
struct ExposedRgb {
    f64 red{};
    f64 green{};
    f64 blue{};
};

// Linear display light, ready for the sRGB encode.
struct DisplayRgb {
    LinearValue red;
    LinearValue green;
    LinearValue blue;
};

namespace agx {

// A 3x3 matrix, **row by row**: element [i][j] multiplies input j into output
// i. The shader writes the same matrices as GLSL's mat3(), which takes its
// arguments **column by column**, so the numbers appear there transposed --
// scripts/check-tonemap-constants.py compares the two in `check`.
using Matrix3 = std::array<std::array<f64, 3>, 3>;

// The inset: from linear Rec. 709 into AgX's working primaries. Sobotka's
// config.ocio, the MatrixTransform of "AgX Log (Kraken)", whose rows these are.
inline constexpr Matrix3 kInset{
    {
        {{0.842479062253094, 0.0784335999999992, 0.0792237451477643}},
        {{0.0423282422610123, 0.878468636469772, 0.0791661274605434}},
        {{0.0423756549057051, 0.0784336, 0.879142973793104}},
    },
};

// The outset: the inset's inverse, as the minimal implementation publishes it
// (to 3.8e-15 of the exact inverse, measured).
inline constexpr Matrix3 kOutset{
    {
        {{1.19687900512017, -0.0980208811401368, -0.0990297440797205}},
        {{-0.0528968517574562, 1.15190312990417, -0.0989611768448433}},
        {{-0.0529716355144438, -0.0980434501171241, 1.15107367264116}},
    },
};

// The log2 range, from config.ocio's AllocationTransform: 10 stops below
// mid-grey and 6.5 above, log2(0.18) being -2.47393.
inline constexpr f64 kMinEv = -12.47393;
inline constexpr f64 kMaxEv = 4.026069;

// The 6th-order fit of Sobotka's AgX_Default_Contrast curve, highest power
// first, as the minimal implementation publishes it (mean error^2
// 3.6705141e-06 by its own account). Measured against the curve itself on
// 2026-09-25: 5.8e-3 at worst, and strictly increasing on [0, 1] -- the 7th-
// order fit in circulation is not, which is why this one (decision 174).
inline constexpr std::array<f64, 7> kContrast{
    {15.5, -40.14, 31.96, -6.868, 0.4298, 0.1191, -0.00232},
};

// The curve's output is encoded for a display that decodes with this power.
inline constexpr f64 kDisplayExponent = 2.2;

using Channels = std::array<f64, 3>;

[[nodiscard]] constexpr Channels multiply(const Matrix3& matrix, const Channels& v) noexcept {
    Channels out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto& row = matrix.at(i);
        out.at(i) = (row.at(0) * v.at(0)) + (row.at(1) * v.at(1)) + (row.at(2) * v.at(2));
    }
    return out;
}

[[nodiscard]] constexpr f64 contrast(f64 x) noexcept {
    f64 result = 0.0;
    for (const f64 coefficient : kContrast) {
        result = (result * x) + coefficient;
    }
    return result;
}

// One channel from AgX's working space to the curve's output: floor, log2,
// normalise, curve.
[[nodiscard]] inline f64 encodeChannel(f64 working) noexcept {
    const f64 floored = std::max(working, std::exp2(kMinEv));
    const f64 stops = std::clamp(std::log2(floored), kMinEv, kMaxEv);
    return contrast((stops - kMinEv) / (kMaxEv - kMinEv));
}

} // namespace agx

// Exposed scene light in, linear display light out. A NaN is a precondition
// failure: only a defect upstream produces one (ADR 0002).
[[nodiscard]] inline DisplayRgb agxTonemap(const ExposedRgb& exposed) noexcept {
    ORBSIM_EXPECTS(!isNaN(exposed.red) && !isNaN(exposed.green) && !isNaN(exposed.blue));
    const agx::Channels input{
        {std::max(exposed.red, 0.0), std::max(exposed.green, 0.0), std::max(exposed.blue, 0.0)},
    };
    const agx::Channels working = agx::multiply(agx::kInset, input);
    const agx::Channels curved{
        {
            agx::encodeChannel(working.at(0)),
            agx::encodeChannel(working.at(1)),
            agx::encodeChannel(working.at(2)),
        },
    };
    const agx::Channels outset = agx::multiply(agx::kOutset, curved);
    const auto toLinear = [](f64 encoded) noexcept {
        return LinearValue{std::pow(std::clamp(encoded, 0.0, 1.0), agx::kDisplayExponent)};
    };
    return DisplayRgb{
        .red = toLinear(outset.at(0)),
        .green = toLinear(outset.at(1)),
        .blue = toLinear(outset.at(2)),
    };
}

// The curve's two ends, from the published coefficients: it starts below zero
// -- which is why the output is clamped -- and ends just short of one.
static_assert(nearlyEqual(agx::contrast(0.0), -0.00232, Tolerance{0.0}));
static_assert(nearlyEqual(agx::contrast(1.0), 0.99858, Tolerance{1e-15}));

} // namespace orb::view

#endif // ORBSIM_VIEW_TONEMAP_HPP
