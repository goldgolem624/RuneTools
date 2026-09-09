// Present callback (see Present.h). OpenGL clients: detours opengl32 wglSwapBuffers. Vulkan clients:
// VkPresent.cpp owns the hook and calls RenderOverlay from the present. SEH-guarded, a bad frame
// skips the overlay.

#include "Present.h"
#include "Composite.h"
#include "VkComposite.h"
#include "VkPresent.h"
#include "InputFilter.h"
#include "MarkerShare.h"
#include "HudShare.h"
#include "FrameShare.h"

#include <windows.h>
#include <detours.h>
#include <GL/gl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace rtx::present {
namespace {

typedef BOOL (WINAPI* SwapBuffers_t)(HDC);
SwapBuffers_t g_origSwap = nullptr;
bool          g_glInstalled = false;
bool          g_vkInstalled = false;

#define RTX_MARKER_SELFTEST 0

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

void DrawCommand(const Backend& b, const rtx::marker::Command& c, int cw, int ch) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, bl = c.b / 255.0f, a = c.a / 255.0f;
    switch (c.type) {
        case rtx::marker::kLine:
            b.DrawLine(c.x0, c.y0, c.x1, c.y1, c.thickness, r, g, bl, a, cw, ch);
            break;
        case rtx::marker::kFillRect: {
            int x = (int)c.x0, y = (int)c.y0;
            int w = (int)(c.x1 - c.x0), h = (int)(c.y1 - c.y0);
            b.DrawSolidRect(x, y, w, h, r, g, bl, a, cw, ch);
            break;
        }
        case rtx::marker::kRect: {
            float t = c.thickness < 1.0f ? 1.0f : c.thickness;
            b.DrawLine(c.x0, c.y0, c.x1, c.y0, t, r, g, bl, a, cw, ch);
            b.DrawLine(c.x1, c.y0, c.x1, c.y1, t, r, g, bl, a, cw, ch);
            b.DrawLine(c.x1, c.y1, c.x0, c.y1, t, r, g, bl, a, cw, ch);
            b.DrawLine(c.x0, c.y1, c.x0, c.y0, t, r, g, bl, a, cw, ch);
            break;
        }
        case rtx::marker::kFillQuad:
            b.DrawFillQuad(c.x0, c.y0, c.x1, c.y1, c.x2, c.y2, c.x3, c.y3, r, g, bl, a, cw, ch);
            break;
        case rtx::marker::kGlyph:
            // x0,y0 = top-left; x1,y1 = w,h; glyph = ASCII - kGlyphFirst.
            b.DrawGlyph(c.glyph, c.x0, c.y0, c.x1, c.y1, r, g, bl, a, cw, ch);
            break;
        case rtx::marker::kText: {
            // x1 = px height; glyph = flags (0 pill, kTextPlain bare line, see MarkerShare). Copy guards a torn terminator.
            char buf[rtx::marker::kTextMax + 1];
            std::memcpy(buf, c.text, rtx::marker::kTextMax);
            buf[rtx::marker::kTextMax] = '\0';
            if (c.glyph & rtx::marker::kTextPlain)
                b.DrawPlainText(buf, c.x0, c.y0, c.x1, (c.glyph >> 1) & 3, r, g, bl, a, cw, ch);
            else
                b.DrawLabel(buf, c.x0, c.y0, c.x1, r, g, bl, a, cw, ch);
            break;
        }
        case rtx::marker::kRoundFill:
            b.DrawRoundRect(c.x0, c.y0, c.x1, c.y1, c.thickness, r, g, bl, a, cw, ch);
            break;
        default:
            break;
    }
}

// No C++ objects here: the SEH wrapper must have nothing to unwind.
void RenderOverlayInner(const Backend& b, HWND hwnd, int fbw, int fbh) {
    if (hwnd) rtx::winmsg::Install(hwnd);
    if (fbw <= 0 || fbh <= 0) return;
    EnsureMarkerMapped();
    EnsureHudMapped();
    EnsureFrameMapped();
    if (g_frame && g_frame->magic == rtx::frame::kMagic &&
        g_frame->version == rtx::frame::kVersion) {
        g_frame->client_w = fbw;
        g_frame->client_h = fbh;
        g_frame->module_seq = g_frame->module_seq + 1;
    }
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
    bool haveUi = g_frame && g_frame->magic == rtx::frame::kMagic &&
                  g_frame->version == rtx::frame::kVersion &&
                  g_frame->visible &&
                  g_frame->width > 0 && g_frame->height > 0;
#if RTX_MARKER_SELFTEST
    const bool draw = true;
#else
    const bool draw = haveMarkers || haveHud || haveUi;
#endif
    if (!draw) return;
    b.Begin();
#if RTX_MARKER_SELFTEST
    {
        int s = 120, x = (fbw - s) / 2, y = (fbh - s) / 2, t = 3;
        b.DrawLine((float)x, (float)y, (float)(x + s), (float)y, (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
        b.DrawLine((float)(x + s), (float)y, (float)(x + s), (float)(y + s), (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
        b.DrawLine((float)(x + s), (float)(y + s), (float)x, (float)(y + s), (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
        b.DrawLine((float)x, (float)(y + s), (float)x, (float)y, (float)t, 0.3f, 0.85f, 1.0f, 0.95f, fbw, fbh);
    }
#endif
    if (haveMarkers) {
        std::uint32_t n = g_marker->count;
        if (n > rtx::marker::kMaxCmds) n = rtx::marker::kMaxCmds;
        int cw = g_marker->fb_w > 0 ? g_marker->fb_w : fbw;
        int ch = g_marker->fb_h > 0 ? g_marker->fb_h : fbh;
        b.SetDepthMode(g_marker->flags, g_marker->ref_x, g_marker->ref_y, g_marker->ref_z);
        for (std::uint32_t i = 0; i < n; ++i) {
            const rtx::marker::Command& c = g_marker->cmds[i];
            const float z[4] = { c.z0, c.z1, c.z2, c.z3 };
            b.SetDepth(z);
            DrawCommand(b, c, cw, ch);
        }
        b.SetDepth(nullptr);
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; OutputDebugStringA("RuneToolsX: drawing world markers"); }
    }
    if (haveHud) {
        static std::uint32_t s_hudImg = 0xFFFFFFFFu;
        static const Backend* s_hudBackend = nullptr;
        if (g_hud->imgSeq != s_hudImg || s_hudBackend != &b) {
            b.UploadHud(g_hud->rgba, g_hud->w, g_hud->h);
            s_hudImg = g_hud->imgSeq;
            s_hudBackend = &b;
        }
        char cap[rtx::hud::kCaptionMax + 1];
        std::memcpy(cap, g_hud->caption, rtx::hud::kCaptionMax);
        cap[rtx::hud::kCaptionMax] = '\0';
        const int sw = 56;
        const int sh = (g_hud->w > 0) ? (sw * g_hud->h / g_hud->w) : sw;
        const int pad = 10;
        const int capH = cap[0] ? 20 : 0;
        const int cw = sw + pad * 2;
        const int ch = sh + capH + pad * 2;
        const int cx = (fbw - cw) / 2, cy = fbh / 3 - pad;
        b.DrawRoundRect((float)(cx + 2), (float)(cy + 3), (float)cw, (float)ch,
                        9.0f, 0.0f, 0.0f, 0.0f, 0.35f, fbw, fbh);
        b.DrawRoundRect((float)cx, (float)cy, (float)cw, (float)ch,
                        9.0f, 0.055f, 0.055f, 0.075f, 0.90f, fbw, fbh);
        b.DrawRoundRect((float)(cx + 8), (float)cy, (float)(cw - 16), 2.0f,
                        1.0f, 0.486f, 0.427f, 0.949f, 0.95f, fbw, fbh);
        b.DrawHud(cx + pad, cy + pad, sw, sh, fbw, fbh);
        if (cap[0])
            b.DrawPlainText(cap, (float)(fbw / 2),
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
            if (lw <= (int)rtx::frame::kMaxWidth &&
                lh <= (int)rtx::frame::kMaxHeight &&
                lstride >= (std::uint32_t)lw * 4u &&
                (size_t)lstride * (size_t)lh <= (size_t)rtx::frame::kMaxBytes) {
                bool contiguous = (fid0 == s_uiFrameId + 1);
                int dx = contiguous ? g_frame->dirty_x : 0;
                int dy = contiguous ? g_frame->dirty_y : 0;
                int dw = contiguous ? g_frame->dirty_w : lw;
                int dh = contiguous ? g_frame->dirty_h : lh;
                b.UploadUiLayer(g_frame->pixels, lw, lh, (int)lstride, dx, dy, dw, dh);
                if (g_frame->seq == seq0 && g_frame->frame_id == fid0)
                    s_uiFrameId = fid0;
            }
        }
        b.DrawUiLayer(g_frame->origin_x, g_frame->origin_y, fbw, fbh);
    }
    b.End();
}

BOOL WINAPI OnPresent_inner(HDC hdc) {
    HWND hwnd = WindowFromDC(hdc);
    if (hwnd) {
        RECT rc;
        if (GetClientRect(hwnd, &rc))
            RenderOverlayInner(GlBackend(), hwnd, rc.right - rc.left, rc.bottom - rc.top);
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

bool InstallGl() {
    if (g_glInstalled) return true;
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
    g_glInstalled = true;
    return true;
}

// renderer= from preferences.cfg next to the client exe; the Jagex launcher writes it before launch.
bool PrefersVulkan() {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) return false;
    *(slash + 1) = 0;
    wchar_t path[MAX_PATH + 32];
    _snwprintf_s(path, _TRUNCATE, L"%spreferences.cfg", exe);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[4096]; DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);
    if (!ok) return false;
    buf[got] = 0;
    const char* p = std::strstr(buf, "renderer=");
    return p && _strnicmp(p + 9, "vulkan", 6) == 0;
}

bool InstallVk() {
    if (g_vkInstalled) return true;
    if (!GetModuleHandleW(L"vulkan-1.dll")) LoadLibraryW(L"vulkan-1.dll");
    g_vkInstalled = rtx::vkpresent::Install();
    return g_vkInstalled;
}

void GlSetDepth(const float*) {}
void GlSetDepthMode(unsigned, float, float, float) {}

}  // namespace

const Backend& GlBackend() {
    static const Backend b = {
        rtx::composite::Begin, rtx::composite::End,
        rtx::composite::DrawSolidRect, rtx::composite::DrawLine, rtx::composite::DrawFillQuad,
        rtx::composite::DrawGlyph, rtx::composite::DrawLabel, rtx::composite::DrawPlainText,
        rtx::composite::DrawRoundRect, rtx::composite::UploadUiLayer, rtx::composite::DrawUiLayer,
        rtx::composite::UploadHud, rtx::composite::DrawHud,
        GlSetDepth, GlSetDepthMode,
    };
    return b;
}

const Backend& VkBackend() {
    static const Backend b = {
        rtx::vkcomposite::Begin, rtx::vkcomposite::End,
        rtx::vkcomposite::DrawSolidRect, rtx::vkcomposite::DrawLine, rtx::vkcomposite::DrawFillQuad,
        rtx::vkcomposite::DrawGlyph, rtx::vkcomposite::DrawLabel, rtx::vkcomposite::DrawPlainText,
        rtx::vkcomposite::DrawRoundRect, rtx::vkcomposite::UploadUiLayer, rtx::vkcomposite::DrawUiLayer,
        rtx::vkcomposite::UploadHud, rtx::vkcomposite::DrawHud,
        rtx::vkcomposite::SetDepth, rtx::vkcomposite::SetDepthMode,
    };
    return b;
}

void RenderOverlay(const Backend& b, void* hwnd, int fbw, int fbh) {
    __try {
        RenderOverlayInner(b, (HWND)hwnd, fbw, fbh);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("RuneToolsX: composite frame fault (skipped)");
    }
}

bool Install() {
    if (PrefersVulkan() || GetModuleHandleW(L"vulkan-1.dll")) return InstallVk();
    return InstallGl();
}

void Poll() {
    if (!g_vkInstalled && GetModuleHandleW(L"vulkan-1.dll")) InstallVk();
    if (g_vkInstalled) rtx::vkpresent::Poll();
}

const char* Mode() {
    if (g_vkInstalled) return rtx::vkpresent::Active() ? "vulkan" : "vulkan (waiting for device)";
    return g_glInstalled ? "opengl" : "off";
}

void Uninstall() {
    if (!g_glInstalled || !g_origSwap) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(g_origSwap), reinterpret_cast<PVOID>(OnPresent));
    DetourTransactionCommit();
    g_glInstalled = false;
}

}  // namespace rtx::present
