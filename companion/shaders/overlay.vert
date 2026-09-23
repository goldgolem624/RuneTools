#version 450
// Overlay vertices: screen positions in pixels, with the game's depth value of the point they
// mark carried along for the fragment shader. In hardware depth mode (mode bit 512) that value
// becomes the vertex's own depth, nudged a little towards the camera, so the GPU's depth test
// against the game's depth buffer decides what is in front, exactly as it does for the game's
// own ground markers drawn in the same pass.

layout(location = 0) in vec2  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in vec4  a_col;
layout(location = 3) in float a_z;

layout(location = 0) out vec2  v_uv;
layout(location = 1) out vec4  v_col;
layout(location = 2) out float v_z;

layout(push_constant) uniform PC {
    vec2  scale;
    vec2  translate;
    int   mode;
    vec2  depthScale;
    float a;             // depth = -a + b / view distance
    float b;
    float bias;          // hardware depth mode: how far towards the camera the marker is moved, in world units
    float fade;
    vec4  view;
    vec4  up;
    vec4  east;
    vec4  north;
} pc;

void main() {
    v_uv = a_uv;
    v_col = a_col;
    v_z = a_z;
    float z = 0.0;
    if ((pc.mode & 512) != 0 && a_z >= 0.0 && pc.b != 0.0) {
        float q = a_z + pc.a;
        if (abs(q) > 1e-9) {
            float w = pc.b / q;
            if (w > 0.0) {
                float w2 = max(w - pc.bias, 1.0);
                z = clamp(-pc.a + pc.b / w2, 0.0, 1.0);
            }
        }
    }
    gl_Position = vec4(a_pos * pc.scale + pc.translate, z, 1.0);
}
