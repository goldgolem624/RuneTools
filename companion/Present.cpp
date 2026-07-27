// Present callback (see Present.h): runs before the system buffer-swap
// (opengl32 wglSwapBuffers), draws launcher-published overlays into the game's GL
// frame, then calls the original swap through unchanged. SEH-guarded: a bad frame
// degrades to "no overlay this frame", never a crash.

#include "Present.h"
#include "Composite.h"
#include "InputFilter.h"   // window-message filter for render continuity (keep-focused)
#include "MarkerShare.h"   // launcher-published world-marker command list
#include "HudShare.h"      // launcher-published HUD-reminder (top-center sprite + caption)

#include <windows.h>
#include <detours.h>
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
#if RTX_MARKER_SELFTEST
                const bool draw = true;
#else
                const bool draw = haveMarkers || haveHud;
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
                        int sw = 48;                                   // sprite ~48px wide, aspect-preserved
                        int sh = (g_hud->w > 0) ? (sw * g_hud->h / g_hud->w) : 48;
                        int sx = (fbw - sw) / 2, sy = fbh / 3;         // centered, a third down (clear of the player at screen center)
                        rtx::composite::DrawHud(sx, sy, sw, sh, fbw, fbh);
                        char cap[rtx::hud::kCaptionMax + 1];
                        std::memcpy(cap, g_hud->caption, rtx::hud::kCaptionMax);
                        cap[rtx::hud::kCaptionMax] = '\0';
                        if (cap[0])
                            rtx::composite::DrawLabel(cap, (float)(fbw / 2), (float)(sy + sh + 14),
                                                      16.0f, 0.96f, 0.88f, 1.0f, 0.97f, fbw, fbh);
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
