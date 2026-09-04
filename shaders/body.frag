#version 450

layout(location = 0) in vec3 vNormal;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    float ndl = max(dot(n, normalize(pc.sunDir.xyz)), 0.0);

    // A hard terminator reads as a faceted edge at planetary scale, so the
    // falloff is softened slightly and a little ambient keeps the night side
    // from going pure black.
    float lit = 0.04 + 0.96 * pow(ndl, 0.85);

    outColor = vec4(pc.color.rgb * lit, pc.color.a);
}
