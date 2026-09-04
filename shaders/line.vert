#version 450

// Used for every line primitive in the scene: orbit tracks, the reference
// grid, velocity vectors and body axes. Positions arrive already expressed
// relative to the camera and scaled into a range float can hold precisely,
// so no world transform is applied here.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 tint;
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
    vColor = inColor * pc.tint;
}
