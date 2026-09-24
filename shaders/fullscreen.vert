#version 450

// One triangle that covers the whole screen, from gl_VertexIndex alone -- no
// vertex buffer (M1-14; src/render/ResolvePass.hpp).
//
// The three corners are (-1,-1), (3,-1) and (-1,3) in clip space. The
// triangle's two legs are twice the screen's width and height, so the screen
// is the lower-left quarter of it and every pixel is covered exactly once:
// one triangle rather than two means no diagonal seam where two triangles
// meet, and no pixel shaded twice along it.
//
// No texture coordinates are passed on. tonemap.frag reads the HDR target at
// the fragment's own pixel, so nothing here can flip the image or shift it.

void main() {
    // Vertex 0, 1, 2 -> (0,0), (2,0), (0,2), then scaled to (-1,-1), (3,-1), (-1,3).
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
