#version 450

// The `tonemap-port` probe's picture (M1-18, register decision 261): light
// the tonemap's clamps act on, so that the port check -- every pixel of the
// displayed frame against src/view/Tonemap.hpp applied to the read-back HDR
// value -- can see a shader that drops one.
//
// **Not a physical scene, on purpose.** Negative radiance does not exist, and
// that is why nothing physical can test the clamp that removes it: the first
// clamp in tonemap.frag's agxTonemap, which M1-15's mutation pass left
// standing. The port check does not need these values to be right, only to be
// read back: the HDR dump is the CPU chain's input. tests/test_tonemap_port.cpp
// asserts first that the frame really holds negative channels and radiance
// above AgX's white, so the check cannot silently stop checking.
//
// Six bands of 120 rows, each a ramp even in stops across the frame's width,
// from two stops below AgX's black to two stops above its white at the
// probe's exposure (f/16, 1/125 s, ISO 100): 0.0171 to 25,297 W/(m^2 sr).
// Each band multiplies the ramp by one colour:
//
//     rows   0-119   ( 1,     1,     1   )   grey
//     rows 120-239   ( 1,    -0.25,  0.5 )   one negative channel
//     rows 240-359   (-0.5,   1,     0.25)   another
//     rows 360-479   ( 0.25,  0.5,  -1   )   and the third
//     rows 480-599   ( 1,     0,     0   )   saturated red, past white
//     rows 600-719   (-1,    -1,    -1   )   all negative: black
//
// In column x the ramp is 2^(kLog2Low + kStops * (x + 0.5) / 1280):
// gl_FragCoord.x is x + 0.5 at a pixel's centre.

layout(location = 0) out vec4 outRadiance;

// log2 of AgX's black radiance at the probe's exposure, less two stops:
// -12.47393 - log2(98.9225 / 38400) - 2, where 38400 cd/m^2 is ISO 12232's
// saturation luminance at f/16, 1/125 s, ISO 100 (src/view/Exposure.hpp).
const float kLog2Low = -5.873338;
// From there to two stops above AgX's white: its 16.5 stops, plus four.
const float kStops = 20.5;
const float kWidth = 1280.0;
const float kBandRows = 120.0;

const vec3 kBandColours[6] = vec3[6](
    vec3(1.0, 1.0, 1.0),
    vec3(1.0, -0.25, 0.5),
    vec3(-0.5, 1.0, 0.25),
    vec3(0.25, 0.5, -1.0),
    vec3(1.0, 0.0, 0.0),
    vec3(-1.0, -1.0, -1.0));

void main() {
    int band = clamp(int(floor(gl_FragCoord.y / kBandRows)), 0, 5);
    float radiance = exp2(kLog2Low + kStops * gl_FragCoord.x / kWidth);
    // Opaque, as the clear is.
    outRadiance = vec4(radiance * kBandColours[band], 1.0);
}
