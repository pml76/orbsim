#version 450

// The `clear` probe's picture (M1-16, register decision 189): four bands and
// an undrawn strip, drawn into the HDR target in radiance, W/(m^2 sr).
//
// src/view/ProbeGradient.hpp states the picture and computes the numbers in
// the block below; tests/test_probe_clear.cpp computes the same radiances on
// its own and checks every band of the read-back frame against them.
//
//     rows 0 to 149     grey   (all three channels)
//     rows 150 to 299   red
//     rows 300 to 449   green
//     rows 450 to 599   blue
//     rows 600 and on   not drawn: the clear radiance, zero, shows
//
// In column x each band's radiance is 2^(log2Low + stops * (x + 0.5) / width):
// gl_FragCoord.x is x + 0.5 at a pixel's centre.

// terminateInvocation rather than `discard`: for Vulkan 1.3, glslc compiles
// `discard` to OpDemoteToHelperInvocation, which needs the device feature
// shaderDemoteToHelperInvocation enabled, and the validation layers said so
// on this shader's first run (2026-09-26). OpTerminateInvocation is core
// SPIR-V 1.6 and needs no feature; nothing here takes a derivative, so a
// helper invocation would have nothing to help with.
#extension GL_EXT_terminate_invocation : require

layout(push_constant) uniform RampBlock {
    float log2Low;  // log2 of the ramp's low end, in W/(m^2 sr)
    float stops;    // how many stops the ramp spans
    float width;    // the frame's width in pixels
    float bandRows; // each band's height in pixels
} ramp;

layout(location = 0) out vec4 outRadiance;

void main() {
    int band = int(floor(gl_FragCoord.y / ramp.bandRows));
    if (band > 3) {
        terminateInvocation; // the undrawn strip
    }
    float radiance = exp2(ramp.log2Low + ramp.stops * gl_FragCoord.x / ramp.width);
    vec3 colour = band == 0 ? vec3(1.0) : vec3(band == 1, band == 2, band == 3);
    // Opaque, as the clear is.
    outRadiance = vec4(radiance * colour, 1.0);
}
