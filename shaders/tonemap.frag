#version 450

// The resolve pass: the one place the renderer turns linear light into display
// values, once, at the very end (ADR 0014; M1-14).
//
// **In M1-14 this does only the sRGB encode.** Exposure and the AgX tonemap
// join it in M1-15, in that order and before the encode; the file carries the
// name of what it will be so that nothing has to be renamed then.
//
// **The encode is here, and not in the hardware.** The swapchain is UNORM,
// never _SRGB (src/render/VulkanContext.cpp refuses one), so this function is
// the only encode between the HDR target and the display, and it is visible
// in code. It is IEC 61966-2-1's definition, the same one src/view/Srgb.hpp
// writes for the CPU; M1-18 compares the two to 1/255.

// Texel access without a sampler: the HDR target is read at this fragment's
// own pixel, so there is nothing to filter. The extension is a GLSL one, and
// it compiles to core SPIR-V -- no Vulkan extension or device feature is
// involved, so it runs wherever Vulkan 1.3 does.
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D hdrTarget;

layout(location = 0) out vec4 outColor;

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
    vec3 light = texelFetch(hdrTarget, ivec2(gl_FragCoord.xy), 0).rgb;
    // Opaque: the window is not composited with what is behind it.
    outColor = vec4(srgbEncode(light), 1.0);
}
