// Frame composite (see Composite.h). OpenGL 3.3 core path: GL 2.0+ entry points are
// resolved via wglGetProcAddress at first use; GL state is saved before each draw and
// restored after so the game's renderer is left exactly as it was.

#include "Composite.h"
#include "MarkerShare.h"    // shared glyph-atlas layout constants

#include <windows.h>
#include <GL/gl.h>          // brings the GL 1.1 names (GL_BLEND, GL_VIEWPORT, GL_FLOAT, ...)
#include <cmath>
#include <cstring>
#include <vector>

#pragma comment(lib, "gdi32.lib")   // glyph-atlas rasterisation (CreateFontW / TextOutW / DIB)

namespace rtx::composite {
namespace {

// ---- GL3+ types / enums not present in the 1.1 <GL/gl.h> on Windows ----------
typedef unsigned int  GLuint_l;
typedef int           GLint_l;
typedef unsigned int  GLenum_l;
typedef int           GLsizei_l;
typedef ptrdiff_t     GLsizeiptr_l;

using PFN_glGenVertexArrays         = void (APIENTRY*)(GLsizei_l, GLuint_l*);
using PFN_glBindVertexArray         = void (APIENTRY*)(GLuint_l);
using PFN_glDeleteVertexArrays      = void (APIENTRY*)(GLsizei_l, const GLuint_l*);
using PFN_glGenBuffers              = void (APIENTRY*)(GLsizei_l, GLuint_l*);
using PFN_glBindBuffer              = void (APIENTRY*)(GLenum_l, GLuint_l);
using PFN_glDeleteBuffers           = void (APIENTRY*)(GLsizei_l, const GLuint_l*);
using PFN_glBufferData              = void (APIENTRY*)(GLenum_l, GLsizeiptr_l, const void*, GLenum_l);
using PFN_glVertexAttribPointer     = void (APIENTRY*)(GLuint_l, GLint_l, GLenum_l, GLubyte, GLsizei_l, const void*);
using PFN_glEnableVertexAttribArray = void (APIENTRY*)(GLuint_l);
using PFN_glCreateShader            = GLuint_l (APIENTRY*)(GLenum_l);
using PFN_glShaderSource            = void (APIENTRY*)(GLuint_l, GLsizei_l, const char* const*, const GLint_l*);
using PFN_glCompileShader           = void (APIENTRY*)(GLuint_l);
using PFN_glCreateProgram           = GLuint_l (APIENTRY*)();
using PFN_glAttachShader            = void (APIENTRY*)(GLuint_l, GLuint_l);
using PFN_glLinkProgram             = void (APIENTRY*)(GLuint_l);
using PFN_glDeleteShader            = void (APIENTRY*)(GLuint_l);
using PFN_glDeleteProgram           = void (APIENTRY*)(GLuint_l);
using PFN_glUseProgram              = void (APIENTRY*)(GLuint_l);
using PFN_glGetUniformLocation      = GLint_l (APIENTRY*)(GLuint_l, const char*);
using PFN_glUniform4f               = void (APIENTRY*)(GLint_l, float, float, float, float);
using PFN_glUniform1i               = void (APIENTRY*)(GLint_l, GLint_l);
using PFN_glActiveTexture           = void (APIENTRY*)(GLenum_l);
using PFN_glBlendFuncSeparate       = void (APIENTRY*)(GLenum_l, GLenum_l, GLenum_l, GLenum_l);
using PFN_glBlendEquationSeparate   = void (APIENTRY*)(GLenum_l, GLenum_l);

// GL >= 1.2 / 1.4 / 2.0 / 3.0 enums absent from the 1.1 header.
#define GL_ARRAY_BUFFER          0x8892
#define GL_DYNAMIC_DRAW          0x88E8
#define GL_VERTEX_SHADER         0x8B31
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_CURRENT_PROGRAM       0x8B8D
#define GL_VERTEX_ARRAY_BINDING  0x85B5
#define GL_ARRAY_BUFFER_BINDING  0x8894
#define GL_BLEND_SRC_RGB         0x80C9
#define GL_BLEND_DST_RGB         0x80C8
#define GL_BLEND_SRC_ALPHA       0x80CB
#define GL_BLEND_DST_ALPHA       0x80CA
#define GL_BLEND_EQUATION_RGB    0x8009
#define GL_BLEND_EQUATION_ALPHA  0x883D
#define GL_FUNC_ADD              0x8006
#define GL_BGRA                  0x80E1
#define GL_TEXTURE0              0x84C0
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_ACTIVE_TEXTURE_E      0x84E0
#define GL_TEXTURE_BINDING_2D_E  0x8069
#define GL_UNPACK_ROW_LENGTH_E   0x0CF2
#define GL_UNPACK_ALIGNMENT_E    0x0CF5
#define GL_RED_E                 0x1903
#define GL_R8_E                  0x8229

PFN_glGenVertexArrays         pGenVertexArrays = nullptr;
PFN_glBindVertexArray         pBindVertexArray = nullptr;
PFN_glDeleteVertexArrays      pDeleteVertexArrays = nullptr;
PFN_glGenBuffers              pGenBuffers = nullptr;
PFN_glBindBuffer              pBindBuffer = nullptr;
PFN_glDeleteBuffers           pDeleteBuffers = nullptr;
PFN_glBufferData              pBufferData = nullptr;
PFN_glVertexAttribPointer     pVertexAttribPointer = nullptr;
PFN_glEnableVertexAttribArray pEnableVertexAttribArray = nullptr;
PFN_glCreateShader            pCreateShader = nullptr;
PFN_glShaderSource            pShaderSource = nullptr;
PFN_glCompileShader           pCompileShader = nullptr;
PFN_glCreateProgram           pCreateProgram = nullptr;
PFN_glAttachShader            pAttachShader = nullptr;
PFN_glLinkProgram             pLinkProgram = nullptr;
PFN_glDeleteShader            pDeleteShader = nullptr;
PFN_glDeleteProgram           pDeleteProgram = nullptr;
PFN_glUseProgram              pUseProgram = nullptr;
PFN_glGetUniformLocation      pGetUniformLocation = nullptr;
PFN_glUniform4f               pUniform4f = nullptr;
PFN_glUniform1i               pUniform1i = nullptr;
PFN_glActiveTexture           pActiveTexture = nullptr;
PFN_glBlendFuncSeparate       pBlendFuncSeparate = nullptr;
PFN_glBlendEquationSeparate   pBlendEquationSeparate = nullptr;

template <typename T>
void resolve(T& fp, const char* name) {
    if (!fp) fp = reinterpret_cast<T>(wglGetProcAddress(name));
}

bool   g_resolved   = false;
bool   g_have_gl    = false;
bool   g_active     = false;   // inside a Begin()/End() bracket
GLuint g_solid_prog = 0;
GLuint g_vao        = 0;
GLuint g_vbo        = 0;
GLint  g_loc_color  = -1;
// Textured sidebar path.
GLuint g_tex_prog   = 0;
GLuint g_tex_vao    = 0;
GLuint g_tex_vbo    = 0;
GLuint g_tex        = 0;
int    g_tex_w      = 0;
int    g_tex_h      = 0;
GLint  g_loc_sampler = -1;
// Glyph atlas path: a coverage texture (GL_RED) of ASCII cells + a tint shader. Reuses the
// textured VAO/VBO (pos.xy, uv.xy). Built lazily the first time a label is drawn.
GLuint g_glyph_prog  = 0;
GLuint g_glyph_tex   = 0;
GLint  g_loc_glyph_color = -1;
GLint  g_loc_glyph_tex   = -1;
int    g_atlas_w   = 0;
int    g_atlas_h   = 0;
bool   g_atlas_ready = false;
// Per-glyph advance width in atlas pixels (cell = ASCII - kGlyphFirst), measured when the
// atlas is rasterised. Lets DrawLabel lay text out proportionally instead of fixed-pitch.
int    g_glyph_adv[rtx::marker::kGlyphLast - rtx::marker::kGlyphFirst + 1] = { 0 };

void ResolveAll() {
    if (g_resolved) return;
    resolve(pGenVertexArrays,         "glGenVertexArrays");
    resolve(pBindVertexArray,         "glBindVertexArray");
    resolve(pDeleteVertexArrays,      "glDeleteVertexArrays");
    resolve(pGenBuffers,              "glGenBuffers");
    resolve(pBindBuffer,              "glBindBuffer");
    resolve(pDeleteBuffers,           "glDeleteBuffers");
    resolve(pBufferData,              "glBufferData");
    resolve(pVertexAttribPointer,     "glVertexAttribPointer");
    resolve(pEnableVertexAttribArray, "glEnableVertexAttribArray");
    resolve(pCreateShader,            "glCreateShader");
    resolve(pShaderSource,            "glShaderSource");
    resolve(pCompileShader,           "glCompileShader");
    resolve(pCreateProgram,           "glCreateProgram");
    resolve(pAttachShader,            "glAttachShader");
    resolve(pLinkProgram,             "glLinkProgram");
    resolve(pDeleteShader,            "glDeleteShader");
    resolve(pDeleteProgram,           "glDeleteProgram");
    resolve(pUseProgram,              "glUseProgram");
    resolve(pGetUniformLocation,      "glGetUniformLocation");
    resolve(pUniform4f,               "glUniform4f");
    resolve(pUniform1i,               "glUniform1i");
    resolve(pActiveTexture,           "glActiveTexture");
    resolve(pBlendFuncSeparate,       "glBlendFuncSeparate");
    resolve(pBlendEquationSeparate,   "glBlendEquationSeparate");
    g_resolved = pGenVertexArrays && pCreateProgram && pUseProgram && pBufferData &&
                 pVertexAttribPointer && pBindVertexArray && pActiveTexture && pUniform1i;
}

// Solid-colour quad. Positions arrive in NDC; the fragment premultiplies the colour
// by its alpha so the premultiplied compositor blend layers correctly over the game.
const char* kVertSrc = R"GLSL(#version 330 core
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
)GLSL";

const char* kFragSrc = R"GLSL(#version 330 core
out vec4 frag;
uniform vec4 u_color;
void main() { frag = vec4(u_color.rgb * u_color.a, u_color.a); }
)GLSL";

// Textured quad. (vec2 pos, vec2 uv) per vertex. The sidebar surface is premultiplied
// BGRA uploaded via GL_BGRA, so texture() already yields premultiplied RGBA.
const char* kTexVertSrc = R"GLSL(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }
)GLSL";

const char* kTexFragSrc = R"GLSL(#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
void main() { frag = texture(u_tex, v_uv); }
)GLSL";

// Glyph: the atlas stores coverage in the red channel; tint by u_color and premultiply
// (coverage * alpha) so it layers with the same premultiplied blend as the solid path.
const char* kGlyphFragSrc = R"GLSL(#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_atlas;
uniform vec4 u_color;
void main() {
    float cov = texture(u_atlas, v_uv).r;
    frag = vec4(u_color.rgb * u_color.a * cov, u_color.a * cov);
}
)GLSL";

GLuint CompileShader(GLenum_l type, const char* src) {
    GLuint sh = pCreateShader(type);
    pShaderSource(sh, 1, &src, nullptr);
    pCompileShader(sh);
    return sh;
}

struct SavedState {
    GLint program, vao, array_buffer, active_texture, texture_2d;
    GLint viewport[4];
    GLint blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_equation_rgb, blend_equation_alpha;
    GLboolean blend, scissor, depth, cull, stencil;
};

SavedState g_saved{};   // the game's state captured by Begin(), restored by End()

void SaveState(SavedState& s) {
    glGetIntegerv(GL_CURRENT_PROGRAM,      &s.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.array_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_E,     &s.active_texture);
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_E, &s.texture_2d);
    glGetIntegerv(GL_VIEWPORT,             s.viewport);
    glGetIntegerv(GL_BLEND_SRC_RGB,        &s.blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB,        &s.blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,      &s.blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA,      &s.blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &s.blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blend_equation_alpha);
    s.blend   = glIsEnabled(GL_BLEND);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    s.depth   = glIsEnabled(GL_DEPTH_TEST);
    s.cull    = glIsEnabled(GL_CULL_FACE);
    s.stencil = glIsEnabled(GL_STENCIL_TEST);
}

void RestoreState(const SavedState& s) {
    if (pUseProgram)      pUseProgram((GLuint_l)s.program);
    if (pBindVertexArray) pBindVertexArray((GLuint_l)s.vao);
    if (pBindBuffer)      pBindBuffer(GL_ARRAY_BUFFER, (GLuint_l)s.array_buffer);
    if (pActiveTexture)   pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)s.texture_2d);
    if (pActiveTexture)   pActiveTexture((GLenum_l)s.active_texture);
    if (pBlendEquationSeparate) pBlendEquationSeparate((GLenum_l)s.blend_equation_rgb,
                                                       (GLenum_l)s.blend_equation_alpha);
    if (pBlendFuncSeparate)     pBlendFuncSeparate((GLenum_l)s.blend_src_rgb,
                                                   (GLenum_l)s.blend_dst_rgb,
                                                   (GLenum_l)s.blend_src_alpha,
                                                   (GLenum_l)s.blend_dst_alpha);
    if (s.blend)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
    if (s.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (s.depth)   glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
    if (s.cull)    glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
    if (s.stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
}

// Rasterise the ASCII glyph atlas once (GDI) into a GL_RED coverage texture. Called from
// DrawGlyph on the render thread, so a GL context is current for the upload. One-time cost.
void EnsureGlyphAtlas() {
    if (g_atlas_ready || !g_have_gl) return;
    g_atlas_ready = true;   // attempt once; on failure glyphs simply never draw

    const int first = rtx::marker::kGlyphFirst, last = rtx::marker::kGlyphLast;
    const int cols  = rtx::marker::kGlyphCols;
    const int cw    = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    const int count = last - first + 1;
    const int rows  = (count + cols - 1) / cols;
    const int aw = cols * cw, ah = rows * chh;

    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = aw;
    bi.bmiHeader.biHeight      = -ah;          // top-down rows
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { DeleteDC(memDC); return; }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);
    std::memset(bits, 0, (size_t)aw * ah * 4);   // black, transparent

    // Semibold UI font sized to the cell; ANTIALIASED (grayscale coverage, not ClearType --
    // ClearType's per-channel fringe would tint the white text).
    HFONT font = CreateFontW(-(chh - 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ oldFont = font ? SelectObject(memDC, font) : nullptr;
    SetTextColor(memDC, RGB(255, 255, 255));
    SetBkMode(memDC, TRANSPARENT);
    for (int i = 0; i < count; ++i) {
        wchar_t wc = (wchar_t)(first + i);
        int col = i % cols, row = i / cols;
        SIZE sz{};
        GetTextExtentPoint32W(memDC, &wc, 1, &sz);
        // Left-align each glyph at the cell origin so DrawLabel can crop the cell's UV
        // to the glyph's advance. sz.cx is the full pen advance, so [0, adv] is exact.
        int adv = sz.cx; if (adv < 1) adv = 1; if (adv > cw) adv = cw;
        g_glyph_adv[i] = adv;
        int tx = col * cw;                       // left-aligned
        int ty = row * chh + (chh - sz.cy) / 2;  // vertically centred
        TextOutW(memDC, tx, ty, &wc, 1);
    }
    GdiFlush();

    std::vector<unsigned char> cov((size_t)aw * ah);   // white-on-black => any channel = coverage
    const unsigned char* src = static_cast<const unsigned char*>(bits);
    for (size_t i = 0; i < cov.size(); ++i) cov[i] = src[i * 4];

    if (oldFont) SelectObject(memDC, oldFont);
    if (font)    DeleteObject(font);
    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);

    glGenTextures(1, &g_glyph_tex);
    glBindTexture(GL_TEXTURE_2D, g_glyph_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint oldAlign = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT_E, &oldAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT_E, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8_E, aw, ah, 0, GL_RED_E, GL_UNSIGNED_BYTE, cov.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT_E, oldAlign);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_atlas_w = aw; g_atlas_h = ah;
}

}  // namespace

void EnsureGL() {
    ResolveAll();
    if (!g_resolved || g_have_gl) return;

    GLuint vs = CompileShader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
    g_solid_prog = pCreateProgram();
    pAttachShader(g_solid_prog, vs);
    pAttachShader(g_solid_prog, fs);
    pLinkProgram(g_solid_prog);
    pDeleteShader(vs);
    pDeleteShader(fs);
    g_loc_color = pGetUniformLocation(g_solid_prog, "u_color");

    pGenVertexArrays(1, &g_vao);
    pBindVertexArray(g_vao);
    pGenBuffers(1, &g_vbo);
    pBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);
    pVertexAttribPointer(0, 2, GL_FLOAT, 0, (GLsizei_l)(sizeof(float) * 2), (void*)0);
    pEnableVertexAttribArray(0);
    pBindVertexArray(0);

    // Textured program + (pos.xy, uv.xy) quad + the persistent sidebar texture.
    GLuint tvs = CompileShader(GL_VERTEX_SHADER,   kTexVertSrc);
    GLuint tfs = CompileShader(GL_FRAGMENT_SHADER, kTexFragSrc);
    g_tex_prog = pCreateProgram();
    pAttachShader(g_tex_prog, tvs);
    pAttachShader(g_tex_prog, tfs);
    pLinkProgram(g_tex_prog);
    pDeleteShader(tvs);
    pDeleteShader(tfs);
    g_loc_sampler = pGetUniformLocation(g_tex_prog, "u_tex");

    pGenVertexArrays(1, &g_tex_vao);
    pBindVertexArray(g_tex_vao);
    pGenBuffers(1, &g_tex_vbo);
    pBindBuffer(GL_ARRAY_BUFFER, g_tex_vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
    pVertexAttribPointer(0, 2, GL_FLOAT, 0, (GLsizei_l)(sizeof(float) * 4), (void*)0);
    pEnableVertexAttribArray(0);
    pVertexAttribPointer(1, 2, GL_FLOAT, 0, (GLsizei_l)(sizeof(float) * 4), (void*)(sizeof(float) * 2));
    pEnableVertexAttribArray(1);
    pBindVertexArray(0);

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Glyph tint program: shares the (pos.xy, uv.xy) vertex shader + textured VAO/VBO; the
    // atlas coverage texture is created lazily in EnsureGlyphAtlas() on the first label draw.
    GLuint gvs = CompileShader(GL_VERTEX_SHADER,   kTexVertSrc);
    GLuint gfs = CompileShader(GL_FRAGMENT_SHADER, kGlyphFragSrc);
    g_glyph_prog = pCreateProgram();
    pAttachShader(g_glyph_prog, gvs);
    pAttachShader(g_glyph_prog, gfs);
    pLinkProgram(g_glyph_prog);
    pDeleteShader(gvs);
    pDeleteShader(gfs);
    g_loc_glyph_color = pGetUniformLocation(g_glyph_prog, "u_color");
    g_loc_glyph_tex   = pGetUniformLocation(g_glyph_prog, "u_atlas");

    g_have_gl = (g_solid_prog != 0 && g_vao != 0 && g_vbo != 0 &&
                 g_tex_prog != 0 && g_tex_vao != 0 && g_tex != 0);
}

// Viewport de-dup: every primitive in a frame targets the same framebuffer size, so
// only call glViewport when it changes. Begin() resets the tracker.
int g_vp_w = -1, g_vp_h = -1;
inline void SetViewport(int w, int h) {
    if (w == g_vp_w && h == g_vp_h) return;
    g_vp_w = w; g_vp_h = h;
    glViewport(0, 0, w, h);
}

void Begin() {
    ResolveAll();
    if (!g_resolved) return;
    // Capture the game's pristine state before EnsureGL() creates or binds resources.
    SaveState(g_saved);
    g_active = true;
    EnsureGL();
    if (!g_have_gl) return;
    // Set all marker-draw state once per frame; End()/RestoreState puts it back, so
    // per-primitive draws touch only colour, verts and viewport.
    glEnable(GL_BLEND);
    if (pBlendEquationSeparate) pBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    if (pBlendFuncSeparate)     pBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                                                   GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    pUseProgram(g_solid_prog);
    pBindVertexArray(g_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    g_vp_w = g_vp_h = -1;   // force the first draw of this frame to set the viewport
}

void End() {
    if (!g_active) return;
    RestoreState(g_saved);
    g_active = false;
}

void DrawSolidRect(int x, int y, int w, int h,
                   float r, float g, float b, float a,
                   int fb_w, int fb_h) {
    if (!g_have_gl || fb_w <= 0 || fb_h <= 0 || w <= 0 || h <= 0) return;

    // Client-pixel rect (top-left origin) -> NDC; GL screen space is bottom-up, Y flips.
    float L = (float)x / (float)fb_w * 2.0f - 1.0f;
    float R = (float)(x + w) / (float)fb_w * 2.0f - 1.0f;
    float T = 1.0f - (float)y / (float)fb_h * 2.0f;
    float B = 1.0f - (float)(y + h) / (float)fb_h * 2.0f;
    const float verts[8] = { L, T,  R, T,  L, B,  R, B };   // TL, TR, BL, BR (tri-strip)

    SetViewport(fb_w, fb_h);
    if (g_loc_color >= 0 && pUniform4f) pUniform4f(g_loc_color, r, g, b, a);
    pBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void DrawLine(float x0, float y0, float x1, float y1, float thickness,
              float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!g_have_gl || fb_w <= 0 || fb_h <= 0) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    if (thickness < 1.0f) thickness = 1.0f;
    // Unit normal, scaled to half-width: offsets the segment into a quad.
    float hx = (-dy / len) * thickness * 0.5f;
    float hy = ( dx / len) * thickness * 0.5f;
    // 4 corners (client px) -> NDC (Y flipped); tri-strip order x0+, x1+, x0-, x1-.
    const float cx[4] = { x0 + hx, x1 + hx, x0 - hx, x1 - hx };
    const float cy[4] = { y0 + hy, y1 + hy, y0 - hy, y1 - hy };
    float verts[8];
    for (int i = 0; i < 4; ++i) {
        verts[i * 2 + 0] = cx[i] / (float)fb_w * 2.0f - 1.0f;
        verts[i * 2 + 1] = 1.0f - cy[i] / (float)fb_h * 2.0f;
    }

    SetViewport(fb_w, fb_h);
    if (g_loc_color >= 0 && pUniform4f) pUniform4f(g_loc_color, r, g, b, a);
    pBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void DrawFillQuad(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!g_have_gl || fb_w <= 0 || fb_h <= 0) return;
    // Perimeter corners p0,p1,p2,p3 -> triangle strip order p0,p1,p3,p2 (covers the quad).
    const float cx[4] = { x0, x1, x3, x2 };
    const float cy[4] = { y0, y1, y3, y2 };
    float verts[8];
    for (int i = 0; i < 4; ++i) {
        verts[i * 2 + 0] = cx[i] / (float)fb_w * 2.0f - 1.0f;
        verts[i * 2 + 1] = 1.0f - cy[i] / (float)fb_h * 2.0f;
    }
    SetViewport(fb_w, fb_h);
    if (g_loc_color >= 0 && pUniform4f) pUniform4f(g_loc_color, r, g, b, a);
    pBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void DrawGlyph(int cell, float x, float y, float w, float h,
               float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!g_have_gl || fb_w <= 0 || fb_h <= 0 || w <= 0.0f || h <= 0.0f) return;
    EnsureGlyphAtlas();
    if (!g_glyph_tex || g_atlas_w <= 0 || g_atlas_h <= 0) return;
    const int count = rtx::marker::kGlyphLast - rtx::marker::kGlyphFirst + 1;
    if (cell < 0 || cell >= count) return;

    const int cols = rtx::marker::kGlyphCols;
    const int cw = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    int col = cell % cols, row = cell / cols;
    float u0 = (float)(col * cw)      / (float)g_atlas_w;
    float v0 = (float)(row * chh)     / (float)g_atlas_h;
    float u1 = (float)(col * cw + cw) / (float)g_atlas_w;
    float v1 = (float)(row * chh + chh) / (float)g_atlas_h;

    // Screen rect (client px, top-left origin) -> NDC (Y flipped); tri-strip TL,TR,BL,BR.
    float L = x / (float)fb_w * 2.0f - 1.0f;
    float R = (x + w) / (float)fb_w * 2.0f - 1.0f;
    float T = 1.0f - y / (float)fb_h * 2.0f;
    float B = 1.0f - (y + h) / (float)fb_h * 2.0f;
    const float verts[16] = {
        L, T, u0, v0,
        R, T, u1, v0,
        L, B, u0, v1,
        R, B, u1, v1,
    };

    SetViewport(fb_w, fb_h);
    pUseProgram(g_glyph_prog);
    if (g_loc_glyph_tex >= 0)   pUniform1i(g_loc_glyph_tex, 0);
    if (g_loc_glyph_color >= 0 && pUniform4f) pUniform4f(g_loc_glyph_color, r, g, b, a);
    pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_glyph_tex);
    pBindVertexArray(g_tex_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_tex_vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Restore the solid-marker state Begin() established so subsequent line/fill/quad draws work.
    glBindTexture(GL_TEXTURE_2D, 0);
    pUseProgram(g_solid_prog);
    pBindVertexArray(g_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

// Filled rounded rect (pixel rect), solid program, drawn as a triangle fan. Each corner is
// tessellated into SEG segments; the shape is convex so a centroid fan is watertight.
static void DrawRoundFill(float x, float y, float w, float h, float rad,
                          float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!g_have_gl || fb_w <= 0 || fb_h <= 0 || w <= 0.f || h <= 0.f) return;
    float maxr = (w < h ? w : h) * 0.5f;
    if (rad > maxr) rad = maxr;
    if (rad < 0.f)  rad = 0.f;
    const int SEG = 5;
    const float ccx[4] = { x + rad, x + w - rad, x + w - rad, x + rad     };
    const float ccy[4] = { y + rad, y + rad,     y + h - rad, y + h - rad };
    const float a0[4]  = { 3.14159265f, 4.71238898f, 0.0f, 1.57079633f };   // arc start angles
    const float cenx = x + w * 0.5f, ceny = y + h * 0.5f;

    float verts[2 * (4 * (SEG + 1) + 2)];
    int n = 0;
    verts[n * 2] = cenx; verts[n * 2 + 1] = ceny; ++n;       // fan centre
    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= SEG; ++s) {
            float ang = a0[c] + 1.57079633f * (float)s / (float)SEG;
            verts[n * 2]     = ccx[c] + rad * (float)std::cos(ang);
            verts[n * 2 + 1] = ccy[c] + rad * (float)std::sin(ang);
            ++n;
        }
    }
    verts[n * 2] = verts[2]; verts[n * 2 + 1] = verts[3]; ++n;   // close back to first arc point
    for (int i = 0; i < n; ++i) {
        verts[i * 2]     = verts[i * 2]     / (float)fb_w * 2.0f - 1.0f;
        verts[i * 2 + 1] = 1.0f - verts[i * 2 + 1] / (float)fb_h * 2.0f;
    }
    SetViewport(fb_w, fb_h);
    if (g_loc_color >= 0 && pUniform4f) pUniform4f(g_loc_color, r, g, b, a);
    pBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * (size_t)n, verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n);
}

void DrawLabel(const char* s, float cx, float cy, float text_px,
               float ar, float ag, float ab, float aa, int fb_w, int fb_h) {
    if (!g_have_gl || !s || !*s || fb_w <= 0 || fb_h <= 0) return;
    EnsureGlyphAtlas();
    if (!g_glyph_tex || g_atlas_w <= 0 || g_atlas_h <= 0) return;

    const int first = rtx::marker::kGlyphFirst, last = rtx::marker::kGlyphLast;
    const int cols  = rtx::marker::kGlyphCols;
    const int cw = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    if (text_px < 6.0f)  text_px = 6.0f;
    if (text_px > 40.0f) text_px = 40.0f;

    // The atlas font is ~ (chh-13)px tall; scale the whole cell so the glyph renders ~text_px.
    const float fontPx = (float)(chh - 13);
    const float scale  = text_px / fontPx;     // atlas-px -> screen-px
    const float cellH  = (float)chh * scale;   // full cell height on screen
    const float track  = 0.6f * scale;         // small inter-glyph tracking

    // Split on '\n': a multi-line label renders as one panel with centred lines.
    const char* lstart[4]; int llen[4]; int nl = 0;
    const char* st = s;
    for (const char* p = s;; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (p > st && nl < 4) { lstart[nl] = st; llen[nl] = (int)(p - st); ++nl; }
            if (*p == '\0') break;
            st = p + 1;
        }
    }
    if (!nl) return;

    // Measure each line's proportional width from the per-glyph advances.
    float lw[4] = {}; float maxW = 0.0f; int glyphsTotal = 0;
    for (int li = 0; li < nl; ++li) {
        float tw = 0.0f; int glyphs = 0;
        for (int k = 0; k < llen[li]; ++k) {
            unsigned char ch = (unsigned char)lstart[li][k];
            if (ch < first || ch > last) continue;
            tw += g_glyph_adv[ch - first] * scale + track;
            ++glyphs;
        }
        if (glyphs && tw > track) tw -= track;   // drop the trailing tracking
        lw[li] = tw; glyphsTotal += glyphs;
        if (tw > maxW) maxW = tw;
    }
    if (glyphsTotal == 0) return;

    // One rounded panel behind all lines: dark fill, accent border, soft drop shadow.
    const float padX = 7.0f, padY = 4.0f;
    const float lineH = cellH * 0.92f;                                   // line pitch
    const float pillW = maxW + padX * 2.0f;
    const float pillH = cellH * 0.78f + lineH * (float)(nl - 1) + padY * 2.0f;
    const float pillX = cx - pillW * 0.5f;
    const float pillY = cy - pillH * 0.5f;
    const float rad   = (cellH * 0.78f + padY * 2.0f) * 0.42f;
    DrawRoundFill(pillX + 1.0f, pillY + 2.5f, pillW, pillH, rad, 0.0f, 0.0f, 0.0f, 0.40f, fb_w, fb_h);          // shadow
    DrawRoundFill(pillX - 1.2f, pillY - 1.2f, pillW + 2.4f, pillH + 2.4f, rad + 1.2f, ar, ag, ab, aa, fb_w, fb_h); // accent border
    DrawRoundFill(pillX, pillY, pillW, pillH, rad, 0.055f, 0.070f, 0.100f, 0.92f, fb_w, fb_h);                  // fill

    // Accumulate every glyph quad and submit the whole label in one upload + draw.
    SetViewport(fb_w, fb_h);
    pUseProgram(g_glyph_prog);
    if (g_loc_glyph_tex >= 0) pUniform1i(g_loc_glyph_tex, 0);
    if (g_loc_glyph_color >= 0 && pUniform4f) pUniform4f(g_loc_glyph_color, 0.93f, 0.95f, 0.97f, 1.0f);
    pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_glyph_tex);
    pBindVertexArray(g_tex_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_tex_vbo);

    // First line's ink centre sits padY + ~0.39 cell below the panel top; later lines
    // step down by lineH, each centred horizontally on cx.
    static thread_local std::vector<float> gverts;   // reused each call (present thread only)
    gverts.clear();
    float yc = pillY + padY + cellH * 0.39f;
    for (int li = 0; li < nl; ++li) {
        float penX = cx - lw[li] * 0.5f;
        const float gTop = yc - cellH * 0.5f;
        for (int k = 0; k < llen[li]; ++k) {
            unsigned char ch = (unsigned char)lstart[li][k];
            if (ch < first || ch > last) continue;
            int cell = ch - first;
            int adv  = g_glyph_adv[cell];
            int col  = cell % cols, row = cell / cols;
            float u0 = (float)(col * cw)        / (float)g_atlas_w;
            float u1 = (float)(col * cw + adv)  / (float)g_atlas_w;   // crop to glyph advance
            float v0 = (float)(row * chh)       / (float)g_atlas_h;
            float v1 = (float)(row * chh + chh) / (float)g_atlas_h;
            float gw = adv * scale;
            float L = penX / (float)fb_w * 2.0f - 1.0f;
            float R = (penX + gw) / (float)fb_w * 2.0f - 1.0f;
            float T = 1.0f - gTop / (float)fb_h * 2.0f;
            float B = 1.0f - (gTop + cellH) / (float)fb_h * 2.0f;
            // Two triangles (TL,TR,BL) + (TR,BR,BL), 4 floats (x,y,u,v) per vertex.
            const float quad[24] = {
                L, T, u0, v0,   R, T, u1, v0,   L, B, u0, v1,
                R, T, u1, v0,   R, B, u1, v1,   L, B, u0, v1,
            };
            gverts.insert(gverts.end(), quad, quad + 24);
            penX += gw + track;
        }
        yc += lineH;
    }
    if (!gverts.empty()) {
        pBufferData(GL_ARRAY_BUFFER, (GLsizeiptr_l)(gverts.size() * sizeof(float)), gverts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (int)(gverts.size() / 4));
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    pUseProgram(g_solid_prog);
    pBindVertexArray(g_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

void DrawPlainText(const char* s, float x, float y, float text_px, int align,
                   float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!g_have_gl || !s || !*s || fb_w <= 0 || fb_h <= 0) return;
    EnsureGlyphAtlas();
    if (!g_glyph_tex || g_atlas_w <= 0 || g_atlas_h <= 0) return;

    const int first = rtx::marker::kGlyphFirst, last = rtx::marker::kGlyphLast;
    const int cols  = rtx::marker::kGlyphCols;
    const int cw = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    if (text_px < 6.0f)  text_px = 6.0f;
    if (text_px > 40.0f) text_px = 40.0f;
    const float fontPx = (float)(chh - 13);
    const float scale  = text_px / fontPx;
    const float cellH  = (float)chh * scale;
    const float track  = 0.6f * scale;

    // Single line: stop at '\n' or NUL. Measure with the real advances so
    // centre/right alignment is exact regardless of the launcher's estimate.
    float tw = 0.0f; int glyphs = 0;
    for (const char* p = s; *p && *p != '\n'; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < first || ch > last) continue;
        tw += g_glyph_adv[ch - first] * scale + track;
        ++glyphs;
    }
    if (!glyphs) return;
    if (tw > track) tw -= track;
    float penX = (align == 1) ? x - tw * 0.5f : (align == 2) ? x - tw : x;
    const float gTop = y - cellH * 0.5f;

    SetViewport(fb_w, fb_h);
    pUseProgram(g_glyph_prog);
    if (g_loc_glyph_tex >= 0) pUniform1i(g_loc_glyph_tex, 0);
    if (g_loc_glyph_color >= 0 && pUniform4f) pUniform4f(g_loc_glyph_color, r, g, b, a);
    pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_glyph_tex);
    pBindVertexArray(g_tex_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_tex_vbo);

    static thread_local std::vector<float> pverts;   // reused each call (present thread only)
    pverts.clear();
    for (const char* p = s; *p && *p != '\n'; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < first || ch > last) continue;
        int cell = ch - first;
        int adv  = g_glyph_adv[cell];
        int col  = cell % cols, row = cell / cols;
        float u0 = (float)(col * cw)        / (float)g_atlas_w;
        float u1 = (float)(col * cw + adv)  / (float)g_atlas_w;
        float v0 = (float)(row * chh)       / (float)g_atlas_h;
        float v1 = (float)(row * chh + chh) / (float)g_atlas_h;
        float gw = adv * scale;
        float L = penX / (float)fb_w * 2.0f - 1.0f;
        float R = (penX + gw) / (float)fb_w * 2.0f - 1.0f;
        float T = 1.0f - gTop / (float)fb_h * 2.0f;
        float B = 1.0f - (gTop + cellH) / (float)fb_h * 2.0f;
        const float quad[24] = {
            L, T, u0, v0,   R, T, u1, v0,   L, B, u0, v1,
            R, T, u1, v0,   R, B, u1, v1,   L, B, u0, v1,
        };
        pverts.insert(pverts.end(), quad, quad + 24);
        penX += gw + track;
    }
    if (!pverts.empty()) {
        pBufferData(GL_ARRAY_BUFFER, (GLsizeiptr_l)(pverts.size() * sizeof(float)), pverts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (int)(pverts.size() / 4));
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    pUseProgram(g_solid_prog);
    pBindVertexArray(g_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

void DrawRoundRect(float x, float y, float w, float h, float rad,
                   float r, float g, float b, float a, int fb_w, int fb_h) {
    DrawRoundFill(x, y, w, h, rad, r, g, b, a, fb_w, fb_h);
}

void UploadSidebar(const void* bgra, int w, int h, int stride) {
    if (!g_have_gl || !bgra || w <= 0 || h <= 0) return;
    // Begin() left the active unit at GL_TEXTURE0 and saved its binding; End() restores it.
    glBindTexture(GL_TEXTURE_2D, g_tex);
    bool rowset = (stride > 0 && stride != w * 4);
    if (rowset) glPixelStorei(GL_UNPACK_ROW_LENGTH_E, stride / 4);
    if (w != g_tex_w || h != g_tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
        g_tex_w = w;
        g_tex_h = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    }
    if (rowset) glPixelStorei(GL_UNPACK_ROW_LENGTH_E, 0);
}

void DrawSidebar(int dst_x, int dst_y, int dst_w, int dst_h, int fb_w, int fb_h) {
    if (!g_have_gl || dst_w <= 0 || dst_h <= 0 || fb_w <= 0 || fb_h <= 0) return;
    if (g_tex_w <= 0 || g_tex_h <= 0) return;   // nothing uploaded yet

    float L = (float)dst_x / (float)fb_w * 2.0f - 1.0f;
    float R = (float)(dst_x + dst_w) / (float)fb_w * 2.0f - 1.0f;
    float T = 1.0f - (float)dst_y / (float)fb_h * 2.0f;
    float B = 1.0f - (float)(dst_y + dst_h) / (float)fb_h * 2.0f;
    // (pos.xy, uv.xy): screen top maps to texture row 0 (top of the panel).
    const float verts[16] = {
        L, T, 0.0f, 0.0f,
        R, T, 1.0f, 0.0f,
        L, B, 0.0f, 1.0f,
        R, B, 1.0f, 1.0f,
    };

    glEnable(GL_BLEND);
    if (pBlendEquationSeparate) pBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    if (pBlendFuncSeparate)     pBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                                                   GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    SetViewport(fb_w, fb_h);

    pUseProgram(g_tex_prog);
    if (g_loc_sampler >= 0) pUniform1i(g_loc_sampler, 0);
    pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    pBindVertexArray(g_tex_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_tex_vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    pBindVertexArray(0);
}

// HUD sprite: its own texture (separate from the panel's g_tex), RGBA (straight alpha), drawn
// as a screen-space quad. Mirrors Upload/DrawSidebar but with straight-alpha blend.
GLuint g_hud_tex = 0; int g_hud_w = 0, g_hud_h = 0;

void UploadHud(const void* rgba, int w, int h) {
    if (!g_have_gl || !rgba || w <= 0 || h <= 0) return;
    if (!g_hud_tex) {
        glGenTextures(1, &g_hud_tex);
        glBindTexture(GL_TEXTURE_2D, g_hud_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_hud_tex);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    g_hud_w = w; g_hud_h = h;
}

void DrawHud(int dst_x, int dst_y, int dst_w, int dst_h, int fb_w, int fb_h) {
    if (!g_have_gl || !g_hud_tex || dst_w <= 0 || dst_h <= 0 || fb_w <= 0 || fb_h <= 0) return;
    float L = (float)dst_x / (float)fb_w * 2.0f - 1.0f;
    float R = (float)(dst_x + dst_w) / (float)fb_w * 2.0f - 1.0f;
    float T = 1.0f - (float)dst_y / (float)fb_h * 2.0f;
    float B = 1.0f - (float)(dst_y + dst_h) / (float)fb_h * 2.0f;
    const float verts[16] = {
        L, T, 0.0f, 0.0f,
        R, T, 1.0f, 0.0f,
        L, B, 0.0f, 1.0f,
        R, B, 1.0f, 1.0f,
    };
    glEnable(GL_BLEND);
    if (pBlendEquationSeparate) pBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    if (pBlendFuncSeparate)     pBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                                   GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    SetViewport(fb_w, fb_h);
    pUseProgram(g_tex_prog);
    if (g_loc_sampler >= 0) pUniform1i(g_loc_sampler, 0);
    pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_hud_tex);
    pBindVertexArray(g_tex_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_tex_vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    // Restore the solid-marker state Begin() established (mirror DrawGlyph's tail).
    glBindTexture(GL_TEXTURE_2D, 0);
    pUseProgram(g_solid_prog);
    pBindVertexArray(g_vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

void Shutdown() {
    if (g_solid_prog && pDeleteProgram) { pDeleteProgram(g_solid_prog); g_solid_prog = 0; }
    if (g_tex_prog && pDeleteProgram)   { pDeleteProgram(g_tex_prog); g_tex_prog = 0; }
    if (g_glyph_prog && pDeleteProgram) { pDeleteProgram(g_glyph_prog); g_glyph_prog = 0; }
    if (g_vbo && pDeleteBuffers)        { pDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_tex_vbo && pDeleteBuffers)    { pDeleteBuffers(1, &g_tex_vbo); g_tex_vbo = 0; }
    if (g_vao && pDeleteVertexArrays)   { pDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_tex_vao && pDeleteVertexArrays){ pDeleteVertexArrays(1, &g_tex_vao); g_tex_vao = 0; }
    if (g_tex) { glDeleteTextures(1, &g_tex); g_tex = 0; }
    if (g_glyph_tex) { glDeleteTextures(1, &g_glyph_tex); g_glyph_tex = 0; }
    if (g_hud_tex) { glDeleteTextures(1, &g_hud_tex); g_hud_tex = 0; }
    g_tex_w = g_tex_h = 0;
    g_atlas_w = g_atlas_h = 0;
    g_atlas_ready = false;
    g_have_gl = false;
    g_resolved = false;
    g_active = false;
}

}  // namespace rtx::composite
