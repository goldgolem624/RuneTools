// Present callback (see Present.h): runs before the system buffer-swap
// (opengl32 wglSwapBuffers), draws launcher-published overlays into the game's GL
// frame, then calls the original swap through unchanged. SEH-guarded: a bad frame
// degrades to "no overlay this frame", never a crash.

#include "Present.h"
#include "Composite.h"
#include "InputFilter.h"   // window-message filter for render continuity (keep-focused)
#include "MarkerShare.h"   // launcher-published world-marker command list
#include "HudShare.h"      // launcher-published HUD-reminder (top-center sprite + caption)
#include "FrameShare.h"    // launcher-published in-game window UI layer (pixel surface)
#include "IconAtlasShare.h" // launcher-requested item-icon atlas readback (glGetTexImage)

#include <windows.h>
#include <detours.h>
#include <GL/gl.h>
#include <cstdint>
#include <cstring>

namespace rtx::present {
namespace {

typedef BOOL (WINAPI* SwapBuffers_t)(HDC);
SwapBuffers_t g_origSwap = nullptr;
bool          g_installed = false;

// Diagnostic: 1 = draw a fixed cyan box every present, independent of the marker
// channel (confirms the present callback draws when projected markers don't show).
#define RTX_MARKER_SELFTEST 0

// World-marker command channel; the launcher creates it, this side maps it read-only.
rtx::marker::Share* g_marker    = nullptr;
HANDLE              g_markerMap = nullptr;

void EnsureMarkerMapped() {
    if (g_marker) return;
    wchar_t name[64];
    rtx::marker::MakeSectionName(GetCurrentProcessId(), name);
    g_markerMap = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!g_markerMap) return;
    g_marker = reinterpret_cast<rtx::marker::Share*>(
        MapViewOfFile(g_markerMap, FILE_MAP_READ, 0, 0, sizeof(rtx::marker::Share)));
    if (!g_marker) { CloseHandle(g_markerMap); g_markerMap = nullptr; return; }
    OutputDebugStringA("RuneToolsX: marker channel mapped");   // one-time (g_marker guards re-entry)
}

// HUD-reminder channel; the launcher creates it, this side maps it read-only.
rtx::hud::Share* g_hud    = nullptr;
HANDLE           g_hudMap = nullptr;
void EnsureHudMapped() {
    if (g_hud) return;
    wchar_t name[64];
    rtx::hud::MakeSectionName(GetCurrentProcessId(), name);
    g_hudMap = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!g_hudMap) return;
    g_hud = reinterpret_cast<rtx::hud::Share*>(
        MapViewOfFile(g_hudMap, FILE_MAP_READ, 0, 0, sizeof(rtx::hud::Share)));
    if (!g_hud) { CloseHandle(g_hudMap); g_hudMap = nullptr; }
}

// In-game window UI layer; the launcher creates it. Mapped read+write because this
// side publishes the live client size + a liveness counter back (resize handshake).
// Retry is backed off: OpenFileMapping every present before the launcher creates
// the section would be a per-frame syscall.
rtx::frame::Share* g_frame    = nullptr;
HANDLE             g_frameMap = nullptr;
void EnsureFrameMapped() {
    if (g_frame) return;
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[64];
    rtx::frame::MakeSectionName(GetCurrentProcessId(), name);
    g_frameMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!g_frameMap) return;
    g_frame = reinterpret_cast<rtx::frame::Share*>(
        MapViewOfFile(g_frameMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                      sizeof(rtx::frame::Share)));
    if (!g_frame) { CloseHandle(g_frameMap); g_frameMap = nullptr; return; }
    OutputDebugStringA("RuneToolsX: ui-layer channel mapped");
}

// Item-icon atlas readback channel; the launcher creates it (request/response, see
// IconAtlasShare.h). Mapped read+write; backed-off open like the UI layer.
rtx::iconatlas::Share* g_icon    = nullptr;
HANDLE                 g_iconMap = nullptr;
void EnsureIconMapped() {
    if (g_icon) return;
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[64];
    rtx::iconatlas::MakeSectionName(GetCurrentProcessId(), name);
    g_iconMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!g_iconMap) return;
    g_icon = reinterpret_cast<rtx::iconatlas::Share*>(
        MapViewOfFile(g_iconMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                      sizeof(rtx::iconatlas::Share)));
    if (!g_icon) { CloseHandle(g_iconMap); g_iconMap = nullptr; return; }
    OutputDebugStringA("RuneToolsX: icon-atlas channel mapped");
}

#define GL_TEXTURE_BINDING_2D_P        0x8069
#define GL_ACTIVE_TEXTURE_P            0x84E0
#define GL_TEXTURE0_P                  0x84C0
#define GL_PIXEL_PACK_BUFFER_P         0x88EB
#define GL_PIXEL_PACK_BUFFER_BINDING_P 0x88ED
typedef void (WINAPI* PFN_glActiveTexture_p)(GLenum);
typedef void (WINAPI* PFN_glBindBuffer_p)(GLenum, GLuint);
// Framebuffer readback path (GL 3.0 core): attach the atlas as a colour attachment and
// glReadPixels it. Works where glGetTexImage raises INVALID_OPERATION (non-2D targets,
// sRGB/immutable storage in a core context, driver quirks).
#define GL_READ_FRAMEBUFFER_P          0x8CA8
#define GL_READ_FRAMEBUFFER_BINDING_P  0x8CAA
#define GL_COLOR_ATTACHMENT0_P         0x8CE0
#define GL_FRAMEBUFFER_COMPLETE_P      0x8CD5
#define GL_READ_BUFFER_P               0x0C02
typedef void (WINAPI* PFN_glGenFramebuffers_p)(GLsizei, GLuint*);
typedef void (WINAPI* PFN_glDeleteFramebuffers_p)(GLsizei, const GLuint*);
typedef void (WINAPI* PFN_glBindFramebuffer_p)(GLenum, GLuint);
typedef void (WINAPI* PFN_glFramebufferTexture2D_p)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (WINAPI* PFN_glCheckFramebufferStatus_p)(GLenum);

// True when `name` is a live 2D texture whose level 0 is exactly the atlas size. A name
// bound to another target raises INVALID_OPERATION on the 2D bind; drain and skip it.
bool IsAtlasTexture(GLuint name, GLint savedBinding) {
    if (name == 0 || !glIsTexture(name)) return false;
    glBindTexture(GL_TEXTURE_2D, name);
    if (glGetError() != GL_NO_ERROR) { glBindTexture(GL_TEXTURE_2D, (GLuint)savedBinding); return false; }
    GLint w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glBindTexture(GL_TEXTURE_2D, (GLuint)savedBinding);
    while (glGetError() != GL_NO_ERROR) {}
    return w == (GLint)rtx::iconatlas::kAtlasW && h == (GLint)rtx::iconatlas::kAtlasH;
}

// Committed, readable, user-mode memory at [addr, addr+len)? VirtualQuery first: a stale
// hint must not fault the render thread every frame until the launcher clears it.
bool ReadableRange(std::uintptr_t addr, std::size_t len) {
    if (addr < 0x10000 || addr > 0x00007FFFFFFFFFFFull) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD p = mbi.Protect & 0xFF;
    if (p == PAGE_NOACCESS || p == PAGE_EXECUTE || (mbi.Protect & PAGE_GUARD)) return false;
    std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return addr + len <= regionEnd;
}

struct AtlasCandidates {
    GLuint   names[rtx::iconatlas::kMaxCandidates];
    unsigned count = 0;
    bool add(GLuint n) {
        for (unsigned i = 0; i < count; ++i) if (names[i] == n) return false;
        if (count >= rtx::iconatlas::kMaxCandidates) return false;
        names[count++] = n; return true;
    }
};

// Scan `len` bytes at `base` at 4-byte steps for u32s that are atlas-sized textures.
void ScanForAtlasNames(std::uintptr_t base, std::size_t len, GLint savedBinding, AtlasCandidates& out) {
    if (!ReadableRange(base, 4)) return;
    for (std::size_t off = 0; off + 4 <= len; off += 4) {
        if (!ReadableRange(base + off, 4)) break;
        std::uint32_t v; std::memcpy(&v, reinterpret_cast<const void*>(base + off), 4);
        if (v == 0 || v > 0x100000) continue;
        if (IsAtlasTexture((GLuint)v, savedBinding)) out.add((GLuint)v);
    }
}

// The launcher passes the client's texture-owner object (container+0x20). Build 949-5
// keeps the GL names one pointer level down (owner+0x70/+0x78/+0x178 -> object with the
// name at +0x20), and more than one of them is a 1536x1536 atlas. So: every u32 in the
// owner's first 0x200 bytes, then the first 0x100 bytes of every plausible pointer it
// holds. The launcher decides which candidate is the real item atlas (by content).
void AtlasNamesFromHint(std::uint64_t hint, GLint savedBinding, AtlasCandidates& out) {
    if (!hint) return;
    const std::uintptr_t base = (std::uintptr_t)hint;
    ScanForAtlasNames(base, 0x200, savedBinding, out);
    for (std::size_t off = 0; off + 8 <= 0x200; off += 8) {
        if (!ReadableRange(base + off, 8)) break;
        std::uint64_t p; std::memcpy(&p, reinterpret_cast<const void*>(base + off), 8);
        if (p == 0 || (p & 7) || p == hint) continue;
        if (!ReadableRange((std::uintptr_t)p, 0x100)) continue;
        ScanForAtlasNames((std::uintptr_t)p, 0x100, savedBinding, out);
    }
}

// Launcher asked for the atlas: find it, glGetTexImage it into the share, publish.
// Runs with the game's GL context current (inside the swap hook). Every GL state it
// touches is saved and restored; the readback itself is the only stall.
void CaptureIconAtlas() {
    rtx::iconatlas::Share* s = g_icon;
    GLuint s_name = 0;          // texture read this request (launcher-chosen or candidates[0])
    static PFN_glActiveTexture_p s_activeTexture = nullptr;
    static PFN_glBindBuffer_p    s_bindBuffer = nullptr;
    if (!s_activeTexture) s_activeTexture = reinterpret_cast<PFN_glActiveTexture_p>(wglGetProcAddress("glActiveTexture"));
    if (!s_bindBuffer)    s_bindBuffer    = reinterpret_cast<PFN_glBindBuffer_p>(wglGetProcAddress("glBindBuffer"));

    while (glGetError() != GL_NO_ERROR) {}     // drain the game's pending error, if any
    GLint unit = GL_TEXTURE0_P;
    if (s_activeTexture) { glGetIntegerv(GL_ACTIVE_TEXTURE_P, &unit); s_activeTexture(GL_TEXTURE0_P); }
    GLint binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_P, &binding);

    // Enumerate every atlas-sized texture on each request (cheap: a few hundred GL calls
    // for the hint walk; the 64k probe only when the hint yields nothing). The launcher
    // validates by content, so this never guesses.
    AtlasCandidates cands;
    std::uint32_t hit = 0;
    AtlasNamesFromHint(s->hintTexOwner, binding, cands);
    if (cands.count) hit = 1;
    if (!cands.count) {
        for (GLuint n = 1; n < 65536 && cands.count < rtx::iconatlas::kMaxCandidates; ++n)
            if (IsAtlasTexture(n, binding)) cands.add(n);
        if (cands.count) hit = 2;
    }
    s->candidateCount = cands.count;
    for (unsigned i = 0; i < rtx::iconatlas::kMaxCandidates; ++i)
        s->candidates[i] = i < cands.count ? cands.names[i] : 0;
    s_name = 0;
    if (s->wantName) {
        // A validated name from an earlier request is honoured as long as it still has
        // the atlas size, even if the walk no longer lists it (capped list, moved pointer).
        if (IsAtlasTexture(s->wantName, binding)) s_name = s->wantName;
    } else if (cands.count) {
        s_name = cands.names[0];
    }

    s->glName = s_name; s->hintHit = hit;
    if (!s_name) {
        s->width = s->height = 0;
        s->status = rtx::iconatlas::kStatusNoTexture; s->glError = 0;
    } else {
        GLint pbo = 0;
        if (s_bindBuffer) { glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_P, &pbo); if (pbo) s_bindBuffer(GL_PIXEL_PACK_BUFFER_P, 0); }
        GLint packAlign = 4, packRow = 0, packSkipR = 0, packSkipP = 0, packSwap = 0, packLsb = 0;
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &packRow);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipR);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipP);
        glGetIntegerv(GL_PACK_SWAP_BYTES, &packSwap);
        glGetIntegerv(GL_PACK_LSB_FIRST, &packLsb);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_PACK_SWAP_BYTES, 0);
        glPixelStorei(GL_PACK_LSB_FIRST, 0);

        glBindTexture(GL_TEXTURE_2D, s_name);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
        GLenum err = glGetError();
        glBindTexture(GL_TEXTURE_2D, (GLuint)binding);
        std::uint32_t stage = 1;                                   // 1 = glGetTexImage, 2 = FBO readback
        if (err != GL_NO_ERROR) {
            // Fallback: read the texture through a framebuffer attachment.
            static PFN_glGenFramebuffers_p        s_genFb  = nullptr;
            static PFN_glDeleteFramebuffers_p     s_delFb  = nullptr;
            static PFN_glBindFramebuffer_p        s_bindFb = nullptr;
            static PFN_glFramebufferTexture2D_p   s_fbTex  = nullptr;
            static PFN_glCheckFramebufferStatus_p s_fbStat = nullptr;
            if (!s_genFb)  s_genFb  = reinterpret_cast<PFN_glGenFramebuffers_p>(wglGetProcAddress("glGenFramebuffers"));
            if (!s_delFb)  s_delFb  = reinterpret_cast<PFN_glDeleteFramebuffers_p>(wglGetProcAddress("glDeleteFramebuffers"));
            if (!s_bindFb) s_bindFb = reinterpret_cast<PFN_glBindFramebuffer_p>(wglGetProcAddress("glBindFramebuffer"));
            if (!s_fbTex)  s_fbTex  = reinterpret_cast<PFN_glFramebufferTexture2D_p>(wglGetProcAddress("glFramebufferTexture2D"));
            if (!s_fbStat) s_fbStat = reinterpret_cast<PFN_glCheckFramebufferStatus_p>(wglGetProcAddress("glCheckFramebufferStatus"));
            if (s_genFb && s_delFb && s_bindFb && s_fbTex && s_fbStat) {
                stage = 2;
                while (glGetError() != GL_NO_ERROR) {}
                GLint readFb = 0, readBuf = 0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_P, &readFb);
                glGetIntegerv(GL_READ_BUFFER_P, &readBuf);
                GLuint fb = 0; s_genFb(1, &fb);
                s_bindFb(GL_READ_FRAMEBUFFER_P, fb);
                s_fbTex(GL_READ_FRAMEBUFFER_P, GL_COLOR_ATTACHMENT0_P, GL_TEXTURE_2D, s_name, 0);
                const GLenum fbs = s_fbStat(GL_READ_FRAMEBUFFER_P);
                if (fbs == GL_FRAMEBUFFER_COMPLETE_P) {
                    glReadBuffer(GL_COLOR_ATTACHMENT0_P);
                    glReadPixels(0, 0, (GLsizei)rtx::iconatlas::kAtlasW, (GLsizei)rtx::iconatlas::kAtlasH,
                                 GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
                    err = glGetError();
                } else {
                    err = 0x10000u | (fbs & 0xFFFFu);              // framebuffer status, tagged
                }
                s_bindFb(GL_READ_FRAMEBUFFER_P, (GLuint)readFb);
                if (readFb) glReadBuffer((GLenum)readBuf);
                s_delFb(1, &fb);
            }
        }
        s->reserved = stage;

        glPixelStorei(GL_PACK_ALIGNMENT, packAlign);
        glPixelStorei(GL_PACK_ROW_LENGTH, packRow);
        glPixelStorei(GL_PACK_SKIP_ROWS, packSkipR);
        glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipP);
        glPixelStorei(GL_PACK_SWAP_BYTES, packSwap);
        glPixelStorei(GL_PACK_LSB_FIRST, packLsb);
        if (s_bindBuffer && pbo) s_bindBuffer(GL_PIXEL_PACK_BUFFER_P, (GLuint)pbo);

        if (err == GL_NO_ERROR) {
            // GL hands rows bottom-up; the launcher wants top-down (cell y from the packer).
            const std::uint32_t W = rtx::iconatlas::kAtlasW, H = rtx::iconatlas::kAtlasH, stride = W * 4;
            static std::uint8_t tmp[rtx::iconatlas::kAtlasW * 4];   // 6 KB: keep it off the game's stack
            for (std::uint32_t y = 0; y < H / 2; ++y) {
                std::uint8_t* a = s->pixels + (size_t)y * stride;
                std::uint8_t* b = s->pixels + (size_t)(H - 1 - y) * stride;
                std::memcpy(tmp, a, stride); std::memcpy(a, b, stride); std::memcpy(b, tmp, stride);
            }
            s->width = W; s->height = H;
            s->status = rtx::iconatlas::kStatusOk; s->glError = 0;
        } else {
            s->width = s->height = 0;
            s->status = rtx::iconatlas::kStatusGlError; s->glError = (std::uint32_t)err;
        }
    }
    if (s_activeTexture) s_activeTexture((GLenum)unit);
    s->capturedAtMs = GetTickCount64();
    s->request = 0;
    s->seq = s->seq + 1;    // published last: the launcher waits on seq
}

// Draw one command. Coords are client pixels in the projected space (cw x ch).
void DrawCommand(const rtx::marker::Command& c, int cw, int ch) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f, a = c.a / 255.0f;
    switch (c.type) {
        case rtx::marker::kLine:
            rtx::composite::DrawLine(c.x0, c.y0, c.x1, c.y1, c.thickness, r, g, b, a, cw, ch);
            break;
        case rtx::marker::kFillRect: {
            int x = (int)c.x0, y = (int)c.y0;
            int w = (int)(c.x1 - c.x0), h = (int)(c.y1 - c.y0);
            rtx::composite::DrawSolidRect(x, y, w, h, r, g, b, a, cw, ch);
            break;
        }
        case rtx::marker::kRect: {
            float t = c.thickness < 1.0f ? 1.0f : c.thickness;   // outline = four edges
            rtx::composite::DrawLine(c.x0, c.y0, c.x1, c.y0, t, r, g, b, a, cw, ch);  // top
            rtx::composite::DrawLine(c.x1, c.y0, c.x1, c.y1, t, r, g, b, a, cw, ch);  // right
            rtx::composite::DrawLine(c.x1, c.y1, c.x0, c.y1, t, r, g, b, a, cw, ch);  // bottom
            rtx::composite::DrawLine(c.x0, c.y1, c.x0, c.y0, t, r, g, b, a, cw, ch);  // left
            break;
        }
        case rtx::marker::kFillQuad:
            rtx::composite::DrawFillQuad(c.x0, c.y0, c.x1, c.y1, c.x2, c.y2, c.x3, c.y3,
                                         r, g, b, a, cw, ch);
            break;
        case rtx::marker::kGlyph:
            // x0,y0 = top-left; x1,y1 = size (w,h); glyph = atlas cell (ASCII - kGlyphFirst).
            rtx::composite::DrawGlyph(c.glyph, c.x0, c.y0, c.x1, c.y1, r, g, b, a, cw, ch);
            break;
        case rtx::marker::kText: {
            // x1 = glyph px height. glyph = flags: 0 -> pill centred at (x0,y0), rgba =
            // accent; kTextPlain -> bare aligned line, rgba = text colour (see MarkerShare).
            // Guard against a missing terminator from a torn read.
            char buf[rtx::marker::kTextMax + 1];
            std::memcpy(buf, c.text, rtx::marker::kTextMax);
            buf[rtx::marker::kTextMax] = '\0';
            if (c.glyph & rtx::marker::kTextPlain)
                rtx::composite::DrawPlainText(buf, c.x0, c.y0, c.x1, (c.glyph >> 1) & 3,
                                              r, g, b, a, cw, ch);
            else
                rtx::composite::DrawLabel(buf, c.x0, c.y0, c.x1, r, g, b, a, cw, ch);
            break;
        }
        case rtx::marker::kRoundFill:
            rtx::composite::DrawRoundRect(c.x0, c.y0, c.x1, c.y1, c.thickness,
                                          r, g, b, a, cw, ch);
            break;
        default:
            break;
    }
}

// Diagnostic: log abnormally long render frames. Off by default -- the per-frame
// file I/O itself loads the render thread.
#define RTX_DIAG 0

// Pure-POD inner so the SEH wrapper has nothing to unwind.
BOOL WINAPI OnPresent_inner(HDC hdc) {
#if RTX_DIAG
    {
        static unsigned long s_last = 0;
        unsigned long now = (unsigned long)GetTickCount64();
        if (s_last) {
            unsigned long dt = now - s_last;
            if (dt > 40) {   // ~2.5 frames at 60 fps = a visible hitch
                char buf[80];
                wsprintfA(buf, "[%lu] LONG FRAME %lu ms", now, dt);
                rtx::winmsg::DiagLogLine(buf);
            }
        }
        s_last = now;
    }
#endif
    HWND hwnd = WindowFromDC(hdc);
    if (hwnd) {
        // Engage the render-continuity filter (idempotent; inert until the launcher
        // sets keepFocused). This window renders AND receives focus-loss notifications.
        rtx::winmsg::Install(hwnd);
        RECT rc;
        if (GetClientRect(hwnd, &rc)) {
            int fbw = rc.right - rc.left;
            int fbh = rc.bottom - rc.top;
            if (fbw > 0 && fbh > 0) {
                EnsureMarkerMapped();
                EnsureHudMapped();
                EnsureFrameMapped();
                EnsureIconMapped();
                // Atlas readback on request only: one load per present while idle.
                if (g_icon && g_icon->request && g_icon->magic == rtx::iconatlas::kMagic &&
                    g_icon->version == rtx::iconatlas::kVersion)
                    CaptureIconAtlas();
                // Resize handshake + liveness: publish the authoritative client size
                // every present (even while the layer is hidden) so the launcher can
                // size its off-screen UI view and detect a live v2 companion.
                if (g_frame && g_frame->magic == rtx::frame::kMagic &&
                    g_frame->version == rtx::frame::kVersion) {
                    g_frame->client_w = fbw;
                    g_frame->client_h = fbh;
                    g_frame->module_seq = g_frame->module_seq + 1;
                }
                // Decide whether there's anything to draw BEFORE touching GL: overlay
                // off = skip Begin()/End(), zero GL state save/restore on idle frames.
                bool haveMarkers = g_marker && g_marker->magic == rtx::marker::kMagic &&
                                   g_marker->version == rtx::marker::kVersion &&
                                   (g_marker->seq & 1u) == 0 && g_marker->visible &&
                                   g_marker->count > 0;
                bool haveHud = g_hud && g_hud->magic == rtx::hud::kMagic &&
                               g_hud->version == rtx::hud::kVersion &&
                               (g_hud->seq & 1u) == 0 && g_hud->enable &&
                               g_hud->w > 0 && g_hud->h > 0;
                // Drawing the UI layer only needs the last-uploaded texture, so a
                // mid-write seq (odd) still draws; only the UPLOAD requires a stable
                // snapshot.
                bool haveUi = g_frame && g_frame->magic == rtx::frame::kMagic &&
                              g_frame->version == rtx::frame::kVersion &&
                              g_frame->visible &&
                              g_frame->width > 0 && g_frame->height > 0;
#if RTX_MARKER_SELFTEST
                const bool draw = true;
#else
                const bool draw = haveMarkers || haveHud || haveUi;
#endif
                if (draw) {
                    rtx::composite::Begin();
#if RTX_MARKER_SELFTEST
                    {
                        int s = 120, x = (fbw - s) / 2, y = (fbh - s) / 2, t = 3;
                        rtx::composite::DrawLine((float)x, (float)y, (float)(x + s), (float)y, (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
                        rtx::composite::DrawLine((float)(x + s), (float)y, (float)(x + s), (float)(y + s), (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
                        rtx::composite::DrawLine((float)(x + s), (float)(y + s), (float)x, (float)(y + s), (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
                        rtx::composite::DrawLine((float)x, (float)(y + s), (float)x, (float)y, (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
                    }
#endif
                    if (haveMarkers) {
                        std::uint32_t n = g_marker->count;
                        if (n > rtx::marker::kMaxCmds) n = rtx::marker::kMaxCmds;
                        // Project against the size the launcher used; fall back to live.
                        int cw = g_marker->fb_w > 0 ? g_marker->fb_w : fbw;
                        int ch = g_marker->fb_h > 0 ? g_marker->fb_h : fbh;
                        for (std::uint32_t i = 0; i < n; ++i)
                            DrawCommand(g_marker->cmds[i], cw, ch);
                        static bool s_logged = false;
                        if (!s_logged) { s_logged = true; OutputDebugStringA("RuneToolsX: drawing world markers"); }
                        // Rare tear (launcher wrote mid-read) = one glitched frame, no crash.
                    }
                    if (haveHud) {
                        static std::uint32_t s_hudImg = 0xFFFFFFFFu;
                        if (g_hud->imgSeq != s_hudImg) {
                            rtx::composite::UploadHud(g_hud->rgba, g_hud->w, g_hud->h);
                            s_hudImg = g_hud->imgSeq;
                        }
                        char cap[rtx::hud::kCaptionMax + 1];
                        std::memcpy(cap, g_hud->caption, rtx::hud::kCaptionMax);
                        cap[rtx::hud::kCaptionMax] = '\0';
                        // A bare sprite on the game view reads as scenery. Seat it in a framed
                        // card: dark rounded panel, accent edge, icon above its caption. The
                        // frame is what makes it register as a prompt rather than decoration.
                        const int sw = 56;                             // icon width, aspect-preserved
                        const int sh = (g_hud->w > 0) ? (sw * g_hud->h / g_hud->w) : sw;
                        const int pad = 10;
                        const int capH = cap[0] ? 20 : 0;
                        const int cw = sw + pad * 2;
                        const int ch = sh + capH + pad * 2;
                        const int cx = (fbw - cw) / 2, cy = fbh / 3 - pad;
                        // shadow, panel, then a 2px accent rule along the top edge
                        rtx::composite::DrawRoundRect((float)(cx + 2), (float)(cy + 3), (float)cw, (float)ch,
                                                      9.0f, 0.0f, 0.0f, 0.0f, 0.35f, fbw, fbh);
                        rtx::composite::DrawRoundRect((float)cx, (float)cy, (float)cw, (float)ch,
                                                      9.0f, 0.055f, 0.055f, 0.075f, 0.90f, fbw, fbh);
                        rtx::composite::DrawRoundRect((float)(cx + 8), (float)cy, (float)(cw - 16), 2.0f,
                                                      1.0f, 0.486f, 0.427f, 0.949f, 0.95f, fbw, fbh);
                        rtx::composite::DrawHud(cx + pad, cy + pad, sw, sh, fbw, fbh);
                        if (cap[0])
                            rtx::composite::DrawPlainText(cap, (float)(fbw / 2),
                                                          (float)(cy + pad + sh + capH / 2 + 1),
                                                          15.0f, 1, 0.96f, 0.93f, 1.0f, 0.98f, fbw, fbh);
                    }
                    if (haveUi) {
                        // Upload only when the content generation changed AND the writer
                        // is not mid-publish. If the launcher writes during our copy,
                        // leave frame_id unlatched so the next present re-uploads clean.
                        static std::uint32_t s_uiFrameId = 0xFFFFFFFFu;
                        std::uint32_t seq0 = g_frame->seq;
                        std::uint32_t fid0 = g_frame->frame_id;
                        if ((seq0 & 1u) == 0 && fid0 != s_uiFrameId) {
                            int lw = (int)g_frame->width;
                            int lh = (int)g_frame->height;
                            const std::uint32_t lstride = g_frame->stride;
                            // Stride is launcher-written: it must cover a row and the
                            // whole surface must fit the pixel area, or the upload reads
                            // past the section.
                            if (lw <= (int)rtx::frame::kMaxWidth &&
                                lh <= (int)rtx::frame::kMaxHeight &&
                                lstride >= (std::uint32_t)lw * 4u &&
                                (size_t)lstride * (size_t)lh <= (size_t)rtx::frame::kMaxBytes) {
                                // The share's dirty rect describes only the LATEST
                                // publish; if more than one publish happened since
                                // our last upload the earlier rects are lost --
                                // upload the whole surface in that case.
                                bool contiguous = (fid0 == s_uiFrameId + 1);
                                int dx = contiguous ? g_frame->dirty_x : 0;
                                int dy = contiguous ? g_frame->dirty_y : 0;
                                int dw = contiguous ? g_frame->dirty_w : lw;
                                int dh = contiguous ? g_frame->dirty_h : lh;
                                rtx::composite::UploadUiLayer(
                                    g_frame->pixels, lw, lh, (int)lstride,
                                    dx, dy, dw, dh);
                                if (g_frame->seq == seq0 && g_frame->frame_id == fid0)
                                    s_uiFrameId = fid0;
                            }
                        }
                        // Always 1:1 pixels, top-left anchored: DrawUiLayer sizes the
                        // quad from its own uploaded texture, so a mid-resize stale
                        // frame renders unscaled instead of swimming.
                        rtx::composite::DrawUiLayer(g_frame->origin_x, g_frame->origin_y,
                                                    fbw, fbh);
                    }
                    rtx::composite::End();
                }
            }
        }
    }
    return g_origSwap(hdc);
}

BOOL WINAPI OnPresent(HDC hdc) {
    __try {
        return OnPresent_inner(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("RuneToolsX: composite frame fault (skipped)");
    }
    return g_origSwap ? g_origSwap(hdc) : FALSE;
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl) gl = LoadLibraryW(L"opengl32.dll");
    if (!gl) return false;
    g_origSwap = reinterpret_cast<SwapBuffers_t>(GetProcAddress(gl, "wglSwapBuffers"));
    if (!g_origSwap) return false;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(g_origSwap), reinterpret_cast<PVOID>(OnPresent));
    if (DetourTransactionCommit() != NO_ERROR) {
        g_origSwap = nullptr;
        return false;
    }
    g_installed = true;
    return true;
}

void Uninstall() {
    if (!g_installed || !g_origSwap) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(g_origSwap), reinterpret_cast<PVOID>(OnPresent));
    DetourTransactionCommit();
    g_installed = false;
}

}  // namespace rtx::present
