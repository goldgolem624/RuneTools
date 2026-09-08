// Present callback (see Present.h): detours opengl32 wglSwapBuffers. SEH-guarded, a bad frame skips the overlay.

#include "Present.h"
#include "Composite.h"
#include "InputFilter.h"
#include "MarkerShare.h"
#include "HudShare.h"
#include "FrameShare.h"

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

// Diagnostic: 1 = draw a fixed cyan box every present.
#define RTX_MARKER_SELFTEST 0

// Launcher-created shared sections; marker and HUD are mapped read-only.
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
    OutputDebugStringA("RuneToolsX: marker channel mapped");
}

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

// UI layer is read+write: this side publishes client size and a liveness counter. Retry backed off to 1 s.
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


// Coords are client pixels in the projected space (cw x ch).
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
            float t = c.thickness < 1.0f ? 1.0f : c.thickness;
            rtx::composite::DrawLine(c.x0, c.y0, c.x1, c.y0, t, r, g, b, a, cw, ch);
            rtx::composite::DrawLine(c.x1, c.y0, c.x1, c.y1, t, r, g, b, a, cw, ch);
            rtx::composite::DrawLine(c.x1, c.y1, c.x0, c.y1, t, r, g, b, a, cw, ch);
            rtx::composite::DrawLine(c.x0, c.y1, c.x0, c.y0, t, r, g, b, a, cw, ch);
            break;
        }
        case rtx::marker::kFillQuad:
            rtx::composite::DrawFillQuad(c.x0, c.y0, c.x1, c.y1, c.x2, c.y2, c.x3, c.y3,
                                         r, g, b, a, cw, ch);
            break;
        case rtx::marker::kGlyph:
            // x0,y0 = top-left; x1,y1 = w,h; glyph = ASCII - kGlyphFirst.
            rtx::composite::DrawGlyph(c.glyph, c.x0, c.y0, c.x1, c.y1, r, g, b, a, cw, ch);
            break;
        case rtx::marker::kText: {
            // x1 = px height; glyph = flags (0 pill, kTextPlain bare line, see MarkerShare). Copy guards a torn terminator.
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

// Diagnostic: log long render frames.
#define RTX_DIAG 0

// No C++ objects here: the SEH wrapper must have nothing to unwind.
BOOL WINAPI OnPresent_inner(HDC hdc) {
#if RTX_DIAG
    {
        static unsigned long s_last = 0;
        unsigned long now = (unsigned long)GetTickCount64();
        if (s_last) {
            unsigned long dt = now - s_last;
            if (dt > 40) {
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
        // Idempotent; inert until the launcher sets keepFocused.
        rtx::winmsg::Install(hwnd);
        RECT rc;
        if (GetClientRect(hwnd, &rc)) {
            int fbw = rc.right - rc.left;
            int fbh = rc.bottom - rc.top;
            if (fbw > 0 && fbh > 0) {
                EnsureMarkerMapped();
                EnsureHudMapped();
                EnsureFrameMapped();
                // Client size + liveness counter every present, even while hidden.
                if (g_frame && g_frame->magic == rtx::frame::kMagic &&
                    g_frame->version == rtx::frame::kVersion) {
                    g_frame->client_w = fbw;
                    g_frame->client_h = fbh;
                    g_frame->module_seq = g_frame->module_seq + 1;
                }
                // Decide before touching GL so idle frames skip Begin()/End().
                bool haveMarkers = g_marker && g_marker->magic == rtx::marker::kMagic &&
                                   g_marker->version == rtx::marker::kVersion &&
                                   (g_marker->seq & 1u) == 0 && g_marker->visible &&
                                   g_marker->count > 0;
                // w/h are untrusted (any same-user process can pre-create the section): clamp or UploadHud reads OOB.
                bool haveHud = g_hud && g_hud->magic == rtx::hud::kMagic &&
                               g_hud->version == rtx::hud::kVersion &&
                               (g_hud->seq & 1u) == 0 && g_hud->enable &&
                               g_hud->w > 0 && g_hud->h > 0 &&
                               g_hud->w <= rtx::hud::kMaxW && g_hud->h <= rtx::hud::kMaxH;
                // Odd seq still draws (last texture); only the upload needs a stable snapshot.
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
                        // Use the size the launcher projected against; fall back to live.
                        int cw = g_marker->fb_w > 0 ? g_marker->fb_w : fbw;
                        int ch = g_marker->fb_h > 0 ? g_marker->fb_h : fbh;
                        for (std::uint32_t i = 0; i < n; ++i)
                            DrawCommand(g_marker->cmds[i], cw, ch);
                        static bool s_logged = false;
                        if (!s_logged) { s_logged = true; OutputDebugStringA("RuneToolsX: drawing world markers"); }
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
                        // Framed card: shadow, dark panel, 2px accent rule, icon above caption.
                        const int sw = 56;
                        const int sh = (g_hud->w > 0) ? (sw * g_hud->h / g_hud->w) : sw;
                        const int pad = 10;
                        const int capH = cap[0] ? 20 : 0;
                        const int cw = sw + pad * 2;
                        const int ch = sh + capH + pad * 2;
                        const int cx = (fbw - cw) / 2, cy = fbh / 3 - pad;
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
                        // Upload on a new frame_id with even seq; a torn copy leaves frame_id unlatched for a retry.
                        static std::uint32_t s_uiFrameId = 0xFFFFFFFFu;
                        std::uint32_t seq0 = g_frame->seq;
                        std::uint32_t fid0 = g_frame->frame_id;
                        if ((seq0 & 1u) == 0 && fid0 != s_uiFrameId) {
                            int lw = (int)g_frame->width;
                            int lh = (int)g_frame->height;
                            const std::uint32_t lstride = g_frame->stride;
                            // Stride is untrusted: must cover a row and fit the section.
                            if (lw <= (int)rtx::frame::kMaxWidth &&
                                lh <= (int)rtx::frame::kMaxHeight &&
                                lstride >= (std::uint32_t)lw * 4u &&
                                (size_t)lstride * (size_t)lh <= (size_t)rtx::frame::kMaxBytes) {
                                // Dirty rect covers only the latest publish; skipped publishes force a full upload.
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
