#version 450

// The resolve pass: the one place the renderer turns light into display
// values, once, at the very end (ADR 0014; M1-14, M1-15).
//
// **Three steps, in this order and only here**: the exposure multiply, AgX,
// and the sRGB encode. Everything before this pass is radiance, W/(m^2 sr) per
// channel; everything after it is a display value. src/view/Exposure.hpp says
// what the exposure factor is and where its one physical constant comes from;
// src/view/Tonemap.hpp is this AgX on the CPU, and M1-18 compares the two to
// 1/255 -- a port check, since a display transform is a choice and not a
// physical claim. scripts/check-tonemap-constants.py holds the constants below
// to that file's, in `check`, until then.
//
// **The encode is here, and not in the hardware.** The swapchain is UNORM,
// never _SRGB (src/render/VulkanContext.cpp refuses one), so this is the only
// encode between the HDR target and the display, and it is visible in code:
// IEC 61966-2-1, the same definition src/view/Srgb.hpp writes for the CPU.
//
// **AgX** is Benjamin Wrensch's minimal implementation,
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
// https://github.com/sobotka/AgX. THIRD_PARTY.md has the record, and
// src/view/Tonemap.hpp says what is copied exactly and what three guards were
// added -- each where GLSL leaves the reference's behaviour undefined.

// Texel access without a sampler: the HDR target is read at this fragment's
// own pixel, so there is nothing to filter. The extension is a GLSL one, and
// it compiles to core SPIR-V -- no Vulkan extension or device feature is
// involved, so it runs wherever Vulkan 1.3 does.
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D hdrTarget;

// The luminous efficacy times the camera's exposure factor: what turns a
// radiance into an exposed value, 0.18 for a metered mid-grey.
// src/view/PushConstants.hpp holds the C++ side of this block.
layout(push_constant) uniform ExposureBlock {
    float radianceExposure;
} exposure;

layout(location = 0) out vec4 outColor;

// --- AgX --------------------------------------------------------------------
//
// GLSL's mat3() takes its arguments **column by column**; src/view/Tonemap.hpp
// writes the same matrices row by row.

const mat3 kAgxInset = mat3(
    0.842479062253094, 0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772, 0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104);

const mat3 kAgxOutset = mat3(
    1.19687900512017, -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433, 1.15107367264116);

const float kAgxMinEv = -12.47393;
const float kAgxMaxEv = 4.026069;

// The 6th-order fit of Sobotka's contrast curve, highest power first,
// evaluated by Horner's rule.
const float kAgxContrast[7] = float[7](15.5, -40.14, 31.96, -6.868, 0.4298, 0.1191, -0.00232);

// The curve's output is encoded for a display that decodes with this power.
const float kAgxDisplayExponent = 2.2;

float agxContrast(float x) {
    float result = 0.0;
    for (int i = 0; i < 7; ++i) {
        result = result * x + kAgxContrast[i];
    }
    return result;
}

// Exposed scene light in, linear display light out.
vec3 agxTonemap(vec3 exposed) {
    // Negative light is none, as Sobotka's configuration clamps it.
    vec3 v = kAgxInset * max(exposed, vec3(0.0));
    // log2(0) is undefined in GLSL; the clamp below would put it here anyway.
    v = max(v, vec3(exp2(kAgxMinEv)));
    v = clamp(log2(v), kAgxMinEv, kAgxMaxEv);
    v = (v - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv);
    v = vec3(agxContrast(v.r), agxContrast(v.g), agxContrast(v.b));
    v = kAgxOutset * v;
    // The curve dips below zero, and pow() of a negative number is undefined.
    return pow(clamp(v, 0.0, 1.0), vec3(kAgxDisplayExponent));
}

// --- the sRGB encode --------------------------------------------------------

// IEC 61966-2-1:1999, amendment 1. Clamped first, as the CPU version is:
// the display target clamps on write anyway, and GLSL leaves pow() of a
// negative number undefined.
vec3 srgbEncode(vec3 linear) {
    vec3 x = clamp(linear, 0.0, 1.0);
    vec3 linearSegment = 12.92 * x;
    vec3 powerSegment = 1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055;
    return mix(powerSegment, linearSegment, lessThanEqual(x, vec3(0.0031308)));
}

void main() {
    vec3 radiance = texelFetch(hdrTarget, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 exposed = radiance * exposure.radianceExposure;
    // Opaque: the window is not composited with what is behind it.
    outColor = vec4(srgbEncode(agxTonemap(exposed)), 1.0);
}
