#version 450

// The lambert probes' patch (M1-18; register decisions 257 and 267-271): a
// surface drawn through the camera.
//
// The position arrives in render space -- world-oriented, relative to the
// camera, already narrowed in 64 bits by toRenderSpace (src/view/Camera.hpp)
// -- and the view-projection takes it to clip space. That matrix has no
// translation in it, which is the point: the subtraction has been done.

layout(location = 0) in vec3 inPosition; // metres, render space

// src/view/Lambert.hpp holds the C++ side of this block, member for member;
// lambert.frag declares the same block and reads the rest of it.
layout(push_constant) uniform LambertBlock {
    mat4 viewProjection;
    vec4 surfaceNormal; // world, unit, w = 0
    vec4 towardSun;     // world, unit, w = 0
    float albedo;
    float irradiance; // W/m^2 at normal incidence
} lambert;

void main() {
    gl_Position = lambert.viewProjection * vec4(inPosition, 1.0);
}
