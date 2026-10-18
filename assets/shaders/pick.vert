#version 460 core

// HRC-3 D3 dedicated pick vertex shader.
//
// Geometry MUST stay byte-for-byte equivalent to sprite_batch.vert so the
// pick pass rasterizes exactly the same pixels as the color pass. If these
// two ever diverge, IDs land on the wrong pixels silently -- D3-C1 forbids
// that, so only aPos + u_ViewProjection are declared here (aTexCoord/aColor
// are irrelevant to picking and are intentionally not consumed).
//
// Attribute locations are kept identical to sprite_batch.vert so the existing
// sprite VAO can be reused as-is.

layout(location = 0) in vec2 aPos;

uniform mat4 u_ViewProjection;

void main() {
    gl_Position = u_ViewProjection * vec4(aPos, 0.0, 1.0);
}
