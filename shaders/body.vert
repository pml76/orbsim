#version 450

// Celestial bodies are drawn from a unit sphere mesh; the model transform is
// folded into the push-constant matrix on the CPU, in double precision, before
// being narrowed to float. That keeps planetary radii from losing precision.

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 color;
} pc;

layout(location = 0) out vec3 vNormal;

void main() {
    // On a unit sphere centred at the origin the position is the normal.
    vNormal = normalize(inPos);
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}
