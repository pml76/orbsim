#version 450

// A Lambertian surface lit by the Sun, in radiance (M1-18; ADR 0014):
//
//     L = albedo * E * max(cos(theta), 0) / pi        in W/(m^2 sr)
//
// src/view/Lambert.hpp says where each term comes from; tests/
// test_radiometry.cpp computes the expected radiance from the same definition
// on its own and holds every pixel of the read-back frame to it, within 0.5 %
// and on one of the two binary16 values either side.
//
// **No normalize()**: both directions are unit length when they are narrowed
// (toShaderLambert asserts it), and normalising again here would hide one
// that was not. A flat patch has one normal, so it is a constant rather than
// something interpolated across the triangle.

layout(push_constant) uniform LambertBlock {
    mat4 viewProjection;
    vec4 surfaceNormal; // world, unit, w = 0
    vec4 towardSun;     // world, unit, w = 0
    float albedo;
    float irradiance; // W/m^2 at normal incidence
} lambert;

layout(location = 0) out vec4 outRadiance;

// pi to more digits than a float holds; the compiler rounds it once.
const float kPi = 3.14159265358979323846;

void main() {
    float cosTheta = max(dot(lambert.surfaceNormal.xyz, lambert.towardSun.xyz), 0.0);
    float radiance = lambert.albedo * lambert.irradiance * cosTheta / kPi;
    // Grey -- the same radiance in every channel -- and opaque, as the clear is.
    outRadiance = vec4(vec3(radiance), 1.0);
}
