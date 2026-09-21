#version 450
// Overlay drawn inside the game's own frame instead of on top of the presented one. Same inputs and
// push constants as the overlay's own fragment shader; two more mode bits say what it is drawn
// into: an image that is upside down relative to the scene depth, and an image that holds linear
// light the game tone maps afterwards. A third shows the scene depth itself, to check the lookup.

layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(set = 1, binding = 0) uniform sampler2D u_depth;

layout(push_constant) uniform PC {
    vec2  scale;
    vec2  translate;
    int   mode;          // 1: texture is not premultiplied; 2: test against the scene depth; 8: show that depth;
                         // 16: the target is upside down; 32: the target holds linear light;
                         // 64: a line, v_uv = (pixels from its centre line, half its width)
    vec2  depthScale;    // 1 / size of the depth image
    float a;             // depth = -a + b / view distance
    float b;
    float bias;          // how far in front the scene has to be, in view distance
    float fade;          // what is left of a fragment the scene is in front of
} pc;

layout(location = 0) in vec2  v_uv;
layout(location = 1) in vec4  v_col;
layout(location = 2) in float v_z;
layout(location = 0) out vec4 frag;

void main() {
    float fade = 1.0;
    vec2 at = gl_FragCoord.xy * pc.depthScale;
    if ((pc.mode & 16) != 0) at.y = 1.0 - at.y;
    float d = texture(u_depth, at).r;
    if ((pc.mode & 8) != 0) {
        // bands by view distance: every edge in the scene shows as a step
        float w = abs(pc.b / (d + pc.a));
        float g = 0.5 + 0.5 * sin(log2(max(w, 1e-6)) * 14.0);
        frag = vec4(vec3(g, g * 0.6, 1.0 - g) * 0.7, 0.7);
        return;
    }
    if ((pc.mode & 2) != 0 && v_z >= 0.0 && pc.b != 0.0) {
        float qm = v_z + pc.a;
        if (abs(qm) > 1e-9) {
            float wM = pc.b / qm;
            // A marker on the ground is as far away as the ground under it, so the scene only counts as
            // in front from a margin on, and that margin grows with distance: far off the view grazes
            // the ground and a floor a few units above the marker is a long way nearer along the ray.
            // It sets in over a stretch instead of at once, or the edge of it shows as dots.
            float tol = pc.bias + 0.03 * wM;
            float hidden = 0.0;
            float qs = d + pc.a;
            if (abs(qs) > 1e-9) { float wS = pc.b / qs; if (wS > 0.0 && wM > 0.0) hidden = smoothstep(tol, tol * 1.5, wM - wS); }
            fade = mix(1.0, pc.fade, hidden);
        }
    }
    // to straight colour first: the curve applies to the colour, not to colour times coverage
    vec4 t = (pc.mode & 64) != 0 ? vec4(1.0) : texture(u_tex, v_uv);
    vec3 tc = (pc.mode & 1) != 0 ? t.rgb : (t.a > 0.0 ? t.rgb / t.a : vec3(0.0));
    vec3 c = v_col.rgb * tc;
    if ((pc.mode & 32) != 0) c = pow(c, vec3(2.2));
    float alpha = v_col.a * t.a;
    // lines carry their own edge: what of the pixel the line covers, by the distance from its centre line
    if ((pc.mode & 64) != 0) alpha *= clamp(v_uv.y + 0.5 - abs(v_uv.x), 0.0, 1.0);
    frag = vec4(c * alpha, alpha) * fade;
}
