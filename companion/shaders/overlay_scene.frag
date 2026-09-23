#version 450
// Overlay drawn inside the game's own frame instead of on top of the presented one. Same inputs and
// push constants as the overlay's own fragment shader; two more mode bits say what it is drawn
// into: an image that is upside down relative to the scene depth, and an image that holds linear
// light the game tone maps afterwards. A third shows the scene depth itself, to check the lookup.

// Compiled four ways: SCENE_MS and ACTOR_MS say which of the two depth images are multisampled.
// The game keeps the scenery's depth and the characters' depth in separate images (the characters
// are tested against the scenery but never written into it), so both are read and the nearer wins.
layout(set = 0, binding = 0) uniform sampler2D u_tex;
#ifdef SCENE_MS
layout(set = 1, binding = 0) uniform sampler2DMS u_depth;
#else
layout(set = 1, binding = 0) uniform sampler2D u_depth;
#endif
#ifdef ACTOR_MS
layout(set = 2, binding = 0) uniform sampler2DMS u_actor;
#else
layout(set = 2, binding = 0) uniform sampler2D u_actor;
#endif

layout(push_constant) uniform PC {
    vec2  scale;
    vec2  translate;
    int   mode;          // 1: texture is not premultiplied; 2: test against the scene depth; 8: show that depth;
                         // 16: the target is upside down; 32: the target holds linear light;
                         // 64: a line, v_uv = (pixels from its centre line, half its width)
                         // 256: show the measured height gap instead of hiding by it
                         // 1024: write only the visibility mark (alpha kMark) into the scene target
                         // 2048: show the fragment only where the scene target's alpha still holds the mark
    vec2  depthScale;    // 1 / size of the depth image
    float a;             // depth = -a + b / view distance
    float b;
    float bias;          // how far above the marker SCENERY has to be to hide it, in world height; a
                         // character hides it from a sixth of that (see the hide test)
    float fade;          // what is left of a fragment the scene is in front of
    vec4  view;          // the game view: x, y, width, height in target pixels
    vec4  up;            // world height of a clip position = dot(up, clip); all zero: not known
    vec4  east;          // world x of a clip position = dot(east, clip)
    vec4  north;         // world y of a clip position = dot(north, clip)
} pc;

layout(location = 0) in vec2  v_uv;
layout(location = 1) in vec4  v_col;
layout(location = 2) in float v_z;
layout(location = 0) out vec4 frag;

const float kMark = 0.37;

void main() {
    float fade = 1.0;
    if ((pc.mode & 1024) != 0) {
        // the mark: where the marker's own coverage is at least half a pixel
        float cov = (pc.mode & 64) != 0 ? clamp(v_uv.y + 0.5 - abs(v_uv.x), 0.0, 1.0) : 1.0;
        if (cov < 0.5) discard;
        frag = vec4(0.0, 0.0, 0.0, kMark);
        return;
    }
    // the pixel in each depth image: y runs the other way when this target is the right way up
    vec2 at = gl_FragCoord.xy * pc.depthScale;
    if ((pc.mode & 16) != 0) at.y = 1.0 - at.y;
    ivec2 ati = ivec2(gl_FragCoord.xy);
    if ((pc.mode & 16) != 0) ati.y = int(round(1.0 / pc.depthScale.y)) - 1 - ati.y;
    // the characters' depth, once per pixel (or per sample below): the nearer of the two images wins
#ifdef ACTOR_MS
    int nA = min(textureSamples(u_actor), 8);
#else
    float dActor = texture(u_actor, at).r;
#endif
    float d;
#ifdef SCENE_MS
    d = texelFetch(u_depth, ati, 0).r;
#else
    d = texture(u_depth, at).r;
#endif
    if ((pc.mode & 2048) != 0) {
        // the scene target is bound in the depth slot: its alpha still reads kMark where nothing the
        // game drew after the mark covers the marker (the GPU depth test and the paint order did the work)
        float vis = 0.0;
#ifdef SCENE_MS
        int nm = min(textureSamples(u_depth), 8);
        for (int i = 0; i < nm; ++i) vis += abs(texelFetch(u_depth, ati, i).a - kMark) < 0.02 ? 1.0 : 0.0;
        vis /= float(max(nm, 1));
#else
        vis = abs(texture(u_depth, at).a - kMark) < 0.02 ? 1.0 : 0.0;
#endif
        fade = vis;
    } else
    if ((pc.mode & 8) != 0) {
        // bands by view distance: every edge in the scene shows as a step
#ifdef ACTOR_MS
        float da0 = texelFetch(u_actor, ati, 0).r;
#else
        float da0 = dActor;
#endif
        float wS0 = abs(pc.b / (d + pc.a)), wA0 = abs(pc.b / (da0 + pc.a));
        float w = min(wS0, wA0);
        float g = 0.5 + 0.5 * sin(log2(max(w, 1e-6)) * 14.0);
        frag = vec4(vec3(g, g * 0.6, 1.0 - g) * 0.7, 0.7);
        return;
    }
    if ((pc.mode & 2) != 0 && v_z >= 0.0 && pc.b != 0.0) {
        float qm = v_z + pc.a;
        if (abs(qm) > 1e-9) {
            float wM = pc.b / qm;
            // A marker on the ground is as far away as the ground under it, so the scene only counts
            // as in front from a margin on. The margin is a HEIGHT: both points go back to the world
            // through the inverse of the view, and what hides the marker is whatever stands that much
            // above it. Measured along the view instead, the margin had to grow with distance, and
            // then a tail or a leg near the ground fell inside it. It sets in over a stretch instead
            // of at once, or the edge of it shows as dots.
            // the pixel as the launcher counts it: y runs the other way in a target that is upside down
            vec2 sc = vec2(gl_FragCoord.x, (pc.mode & 16) != 0 ? gl_FragCoord.y : 1.0 / pc.depthScale.y - gl_FragCoord.y);
            float hx = pc.view.z * 0.5, hy = pc.view.w * 0.5;
            float nx = (sc.x - hx - pc.view.x) / (hx - 2.0), ny = (sc.y - hy - pc.view.y) / (1.0 - hy);
            bool byHeight = dot(abs(pc.up), vec4(1.0)) > 0.0;
            float tol = byHeight ? pc.bias : 128.0 + 0.03 * wM;
            float hidden = 0.0;
            int n = 0;
#ifdef SCENE_MS
            int nS = min(textureSamples(u_depth), 8);
#else
            int nS = 1;
#endif
#ifdef ACTOR_MS
            int nBoth = max(nS, nA);
#else
            int nBoth = nS;
#endif
            for (int i = 0; i < nBoth; ++i) {
#ifdef SCENE_MS
                float ds = texelFetch(u_depth, ati, min(i, nS - 1)).r;
#else
                float ds = d;
#endif
#ifdef ACTOR_MS
                float da = texelFetch(u_actor, ati, min(i, nA - 1)).r;
#else
                float da = dActor;
#endif
                // the nearer of the two, by view distance
                float qs = ds + pc.a, qa = da + pc.a;
                float wS = abs(qs) > 1e-9 ? pc.b / qs : -1.0;
                float wA = abs(qa) > 1e-9 ? pc.b / qa : -1.0;
                // Two margins: scenery hides the marker only from `bias` up, so grass, shards, pebbles
                // and low roots leave a line whole while walls, trunks and crates still cut it; a
                // character hides it from a sixth of that, so a tail or a leg near the ground counts.
                bool fromActor = false;
                if (wA > 0.0 && (wS <= 0.0 || wA < wS)) { wS = wA; ds = da; fromActor = true; }
                if (wS > 0.0 && wM > 0.0) {
                    // The scene point and the marker point lie on the same ray; their difference in clip
                    // space, put through the inverse view, is their difference in the world. The scene
                    // point is the GROUND UNDER THE MARKER only if it is at about the marker's height and
                    // about the marker's place: a surface at the same height but a tile nearer (a cart wheel,
                    // a leg, seen from a low camera) is an object in front of it and hides it.
                    float gap;
                    if (byHeight) {
                        vec4 dc = vec4(nx * (wS - wM), ny * (wS - wM), ds * wS - v_z * wM, wS - wM);
                        float dz = abs(dot(pc.up, dc));
                        vec2 dxy = vec2(dot(pc.east, dc), dot(pc.north, dc));
                        // whichever is the larger departure from "the ground here", in world units, with
                        // the horizontal one counted at a third: the launcher's line and the game's ground
                        // mesh disagree more in height than in place
                        gap = max(dz, length(dxy) / 3.0);
                    } else {
                        gap = wM - wS;
                    }
                    float t = fromActor && byHeight ? tol / 6.0 : tol;
                    if (wS < wM) hidden += smoothstep(t, t * 1.5, gap);
                }
                ++n;
            }
            hidden /= float(max(n, 1));
            if ((pc.mode & 256) != 0) {
                // the gap at the first sample as a colour: green under 8, yellow to 16, orange to 32, red to 64, magenta beyond
                float g0 = 0.0;
#ifdef SCENE_MS
                float ds0 = texelFetch(u_depth, ati, 0).r;
#else
                float ds0 = d;
#endif
#ifdef ACTOR_MS
                float da1 = texelFetch(u_actor, ati, 0).r;
#else
                float da1 = dActor;
#endif
                float qs0 = ds0 + pc.a, qa0 = da1 + pc.a;
                float wS0 = abs(qs0) > 1e-9 ? pc.b / qs0 : -1.0, wA0 = abs(qa0) > 1e-9 ? pc.b / qa0 : -1.0;
                if (wA0 > 0.0 && (wS0 <= 0.0 || wA0 < wS0)) { wS0 = wA0; ds0 = da1; }
                if (wS0 > 0.0) {
                    vec4 dc0 = vec4(nx * (wS0 - wM), ny * (wS0 - wM), ds0 * wS0 - v_z * wM, wS0 - wM);
                    g0 = byHeight ? max(abs(dot(pc.up, dc0)), length(vec2(dot(pc.east, dc0), dot(pc.north, dc0))) / 3.0) : abs(wM - wS0);
                }
                vec3 col = g0 < 8.0 ? vec3(0.1, 1.0, 0.2) : g0 < 16.0 ? vec3(1.0, 1.0, 0.1) : g0 < 32.0 ? vec3(1.0, 0.55, 0.1) : g0 < 64.0 ? vec3(1.0, 0.1, 0.1) : vec3(1.0, 0.1, 1.0);
                float aG = (pc.mode & 64) != 0 ? clamp(v_uv.y + 0.5 - abs(v_uv.x), 0.0, 1.0) : 1.0;
                frag = vec4(col * aG, aG);
                return;
            }
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
