// Present callback (see Present.h). OpenGL clients: detours opengl32 wglSwapBuffers. Vulkan clients:
// VkPresent.cpp owns the hook and calls RenderOverlay from the present. SEH-guarded, a bad frame
// skips the overlay.

#include "Present.h"
#include "Composite.h"
#include "VkComposite.h"
#include "VkPresent.h"
#include "InputFilter.h"
#include "MarkerShare.h"
#include "EngineHighlight.h"
#include "SceneHover.h"
#include "EngineComponents.h"
#include "EngineOps.h"
#include "TooltipHook.h"
#include "EngineMarkers.h"
#include "HudShare.h"
#include "FrameShare.h"

#include <vector>
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
    static std::uint32_t s_gen = 0;
    rtx::ipc::RebindIfStale(s_gen, g_marker, g_markerMap);
    if (g_marker) return;
    wchar_t name[rtx::ipc::kNameChars];
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
    static std::uint32_t s_gen = 0;
    rtx::ipc::RebindIfStale(s_gen, g_hud, g_hudMap);
    if (g_hud) return;
    wchar_t name[rtx::ipc::kNameChars];
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
    static std::uint32_t s_gen = 0;
    rtx::ipc::RebindIfStale(s_gen, g_frame, g_frameMap);
    if (g_frame) return;
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[rtx::ipc::kNameChars];
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

// The markers as of their last complete write. The launcher rewrites the whole list every frame
// under a sequence lock; drawn straight from the share, a frame that falls into that write shows
// nothing, or half of the old list and half of the new. Such a frame draws the last complete list.
struct MarkerLatch {
    std::vector<rtx::marker::Command> cmds;
    std::int32_t  fb_w = 0, fb_h = 0;
    std::uint32_t flags = 0;
    float         ref[8] = {};
    float         view_m[16] = {};
    std::uint64_t view_addr = 0;
    std::int32_t  gv[4] = {};
    bool          hover_on = false;   // scenery under the cursor for the game to outline, see scenehover::Mark
    std::int32_t  hover_x = 0, hover_y = 0, hover_id = 0;
    std::vector<rtx::marker::CcRect> cc;
    std::uint32_t op_seq = 0; std::int32_t op_sound = 0, op_zoom = 0, op_fov = 0;
    bool          visible = false;
    unsigned      stale = 0;          // frames in a row without a complete read
};
MarkerLatch g_latch;

bool LatchMarkers() {
    if (!g_marker || g_marker->magic != rtx::marker::kMagic || g_marker->version != rtx::marker::kVersion) {
        g_latch.visible = false; g_latch.cmds.clear();
        return false;
    }
    bool got = false;
    for (int attempt = 0; attempt < 4 && !got; ++attempt) {
        const std::uint32_t s0 = g_marker->seq;
        if (s0 & 1u) { for (int k = 0; k < 400; ++k) YieldProcessor(); continue; }
        std::uint32_t n = g_marker->visible ? g_marker->count : 0;
        if (n > rtx::marker::kMaxCmds) n = rtx::marker::kMaxCmds;
        static std::vector<rtx::marker::Command> scratch;
        scratch.resize(n);
        if (n) std::memcpy(scratch.data(), g_marker->cmds, (size_t)n * sizeof(rtx::marker::Command));
        const std::int32_t w = g_marker->fb_w, h = g_marker->fb_h;
        const std::uint32_t flags = g_marker->flags;
        const float ref[8] = { g_marker->ref_x, g_marker->ref_y, g_marker->ref_z, g_marker->ref_a, g_marker->ref_b,
                               g_marker->ref2_x, g_marker->ref2_y, g_marker->ref2_z };
        float vm[16]; std::memcpy(vm, g_marker->view_m, sizeof(vm));
        const std::uint64_t vaddr = g_marker->view_addr;
        const std::int32_t gv[4] = { g_marker->gv_x, g_marker->gv_y, g_marker->gv_w, g_marker->gv_h };
        const bool hon = g_marker->hover_on != 0;
        const std::int32_t hx = g_marker->hover_x, hy = g_marker->hover_y, hid = g_marker->hover_id;
        static std::vector<rtx::marker::CcRect> ccScratch;
        std::uint32_t ccn = g_marker->cc_count; if (ccn > (std::uint32_t)rtx::marker::kMaxCc) ccn = 0;
        ccScratch.assign(g_marker->cc, g_marker->cc + ccn);
        const std::uint32_t oseq = g_marker->op_seq; const std::int32_t osnd = g_marker->op_sound, ozoom = g_marker->op_zoom, ofov = g_marker->op_fov;
        static std::vector<rtx::marker::Anchor> anScratch;
        std::uint32_t ann = g_marker->anchor_count; if (ann > (std::uint32_t)rtx::marker::kMaxAnchors) ann = 0;
        anScratch.assign(g_marker->anchors, g_marker->anchors + ann);
        if (g_marker->seq != s0) continue;                    // written to while it was read: not this one
        g_latch.hover_on = hon; g_latch.hover_x = hx; g_latch.hover_y = hy; g_latch.hover_id = hid;
        g_latch.cc.swap(ccScratch);
        if (oseq != g_latch.op_seq) { g_latch.op_seq = oseq; g_latch.op_sound = osnd; g_latch.op_zoom = ozoom; g_latch.op_fov = ofov; rtx::engineops::Queue(osnd, ozoom, ofov); }
        rtx::engineops::WantAnchors(anScratch.empty() ? nullptr : anScratch.data(), (int)anScratch.size());
        std::memcpy(g_latch.view_m, vm, sizeof(vm)); g_latch.view_addr = vaddr; std::memcpy(g_latch.gv, gv, sizeof(gv));
        g_latch.cmds.swap(scratch);
        g_latch.fb_w = w; g_latch.fb_h = h; g_latch.flags = flags;
        std::memcpy(g_latch.ref, ref, sizeof(ref));
        g_latch.visible = n > 0;
        got = true;
    }
    if (got) g_latch.stale = 0;
    else if (++g_latch.stale > 120) { g_latch.visible = false; g_latch.hover_on = false; g_latch.cmds.clear(); }   // a launcher that stopped mid-write
    return g_latch.visible && !g_latch.cmds.empty();
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
        g_frame->module_cc = rtx::enginecc::Usable() ? 1u : 0u;
    }
    // The engine's own hover outline follows the launcher's flag whether or not there is anything
    // to draw this frame.
    const bool haveMarkers = LatchMarkers();
    if (g_marker && g_marker->magic == rtx::marker::kMagic && g_marker->version == rtx::marker::kVersion) {
        const bool wantHover = (g_marker->flags & rtx::marker::kFlagEngineHover) != 0;
        rtx::vkpresent::SetInFrameTrial((g_marker->flags & rtx::marker::kFlagInFrameTrial) != 0);
        const rtx::enginehl::Status st = rtx::enginehl::Set(wantHover, g_marker->hover_rgb, g_marker->hover_width);
        // NPCs and players the game marks itself; scenery that opted out is marked here
        // from the last complete write: a frame that falls into a write must not skip the mark, the
        // game takes a gap for the end of the hover and the next mark for a new one, pulse and all
        if (wantHover && st == rtx::enginehl::kActive && g_latch.hover_on)
            rtx::scenehover::Mark(g_latch.hover_x, g_latch.hover_y, g_latch.hover_id);
        rtx::scenehover::SetPulseHold(wantHover && st == rtx::enginehl::kActive);
        rtx::scenehover::Trace();
        // the rectangles the game draws as components: handed to the game thread, which applies them
        rtx::enginecc::Want(g_latch.cc.data(), (std::uint32_t)g_latch.cc.size());
        static rtx::enginehl::Status s_said = rtx::enginehl::kUnknown;
        if (st != s_said) {
            s_said = st;
            OutputDebugStringA(st == rtx::enginehl::kActive ? "RuneToolsX: engine hover outline available"
                                                            : "RuneToolsX: engine hover outline not recognised in this build");
        }
    }
    if (g_marker && g_marker->magic == rtx::marker::kMagic && g_marker->version == rtx::marker::kVersion &&
        (g_marker->seq & 1u) == 0) {
        char tip[sizeof(g_marker->tip_text)];
        std::memcpy(tip, g_marker->tip_text, sizeof(tip));
        tip[sizeof(tip) - 1] = 0;
        rtx::tooltip::Update(g_marker->tip_on != 0, g_marker->tip_slot, g_marker->tip_comp, tip);
        rtx::enginemark::Want mk;
        mk.tile_on = g_marker->mark_tile_on != 0;
        mk.tile_x = g_marker->mark_tile_x; mk.tile_y = g_marker->mark_tile_y; mk.tile_model = g_marker->mark_tile_model;
        mk.arrow_on = g_marker->mark_arrow_on != 0;
        mk.arrow_x = g_marker->mark_arrow_x; mk.arrow_y = g_marker->mark_arrow_y; mk.arrow_plane = g_marker->mark_arrow_plane;
        mk.arrow_style = g_marker->mark_arrow_style; mk.arrow_height = g_marker->mark_arrow_height;
        mk.arrow_pointer = g_marker->mark_arrow_pointer;
        mk.tile_rgb = g_marker->mark_tile_rgb & 0xFFFFFFu; mk.tile_width = g_marker->mark_tile_width;
        mk.arrow_rgb = g_marker->mark_arrow_rgb & 0xFFFFFFu; mk.arrow_width = g_marker->mark_arrow_width;
        mk.arrow_range = g_marker->mark_arrow_range; mk.pointer_scale = g_marker->mark_pointer_scale;
        mk.pointer_reach = g_marker->mark_pointer_reach;
        mk.arrow_npc = g_marker->mark_arrow_npc; mk.tile_steady = g_marker->mark_tile_steady != 0;
        mk.path_on = g_marker->mark_path_on != 0; mk.path_model = g_marker->mark_path_model;
        mk.path_x0 = g_marker->mark_path_x0; mk.path_y0 = g_marker->mark_path_y0;
        mk.path_x1 = g_marker->mark_path_x1; mk.path_y1 = g_marker->mark_path_y1;
        rtx::enginemark::Update(mk);
    }
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
        const std::uint32_t n = (std::uint32_t)g_latch.cmds.size();
        int cw = g_latch.fb_w > 0 ? g_latch.fb_w : fbw;
        int ch = g_latch.fb_h > 0 ? g_latch.fb_h : fbh;
        b.SetDepthMode(g_latch.flags, g_latch.ref);
        rtx::vkcomposite::SetViewInfo(g_latch.view_m, g_latch.view_addr, g_latch.gv, cw, ch);
        for (std::uint32_t i = 0; i < n; ++i) {
            const rtx::marker::Command& c = g_latch.cmds[i];
            const float z[4] = { c.z0, c.z1, c.z2, c.z3 };
            b.SetDepth(z);
            rtx::vkcomposite::SetTopLayer(c.top != 0);
            DrawCommand(b, c, cw, ch);
        }
        b.SetDepth(nullptr);
        rtx::vkcomposite::SetTopLayer(true);    // what follows is ours (the HUD card, the panels): over the interface
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
void GlSetDepthMode(unsigned, const float*) {}

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

// Both hooks go in whenever their library is present. Which renderer the client uses cannot be
// told at load time: the client loads vulkan-1.dll for a moment at start-up to see whether Vulkan
// is available, and the module is injected while that may be so. Choosing Vulkan on that evidence
// left OpenGL clients without a compositor for the whole session (no present ever came through
// the Vulkan hook and nothing fell back). A hook on a renderer the client does not use is inert:
// an OpenGL client never presents through Vulkan and a Vulkan client never calls wglSwapBuffers.
// Written on the game thread, read by the launcher; the count goes up only after the points are in
// place, and the sequence number changes last, so a reader never sees a half-written set.
// What the channel looks like from here, for the check: mapped at all, and the mark and version
// found in it against the ones this build writes.
void FrameChannelState(bool& mapped, std::uint32_t& magic, std::uint32_t& version,
                       std::uint32_t& wantMagic, std::uint32_t& wantVersion) {
    mapped = g_frame != nullptr;
    magic = g_frame ? g_frame->magic : 0u;
    version = g_frame ? g_frame->version : 0u;
    wantMagic = rtx::frame::kMagic; wantVersion = rtx::frame::kVersion;
}

void PublishGlyphWidths(const int* adv, int count, int px) {
    if (!g_frame || g_frame->magic != rtx::frame::kMagic || g_frame->version != rtx::frame::kVersion) return;
    if (!adv || count <= 0 || px <= 0) return;
    if (count > (int)sizeof(g_frame->glyph_adv)) count = (int)sizeof(g_frame->glyph_adv);
    for (int i = 0; i < count; ++i) {
        const int a = adv[i];
        g_frame->glyph_adv[i] = (std::uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
    }
    g_frame->glyph_px = (std::uint32_t)px;
    g_frame->glyph_ready = 1;
}

bool PublishAnchors(const void* points, int count) {
    // Only into a frame share this build understands: a launcher of another version has the
    // fields somewhere else.
    if (!g_frame || g_frame->magic != rtx::frame::kMagic || g_frame->version != rtx::frame::kVersion) return false;
    if (count < 0) count = 0;
    if (count > 64) count = 64;
    if (!points) count = 0;
    if (count) std::memcpy(const_cast<rtx::frame::Share::AnchorPoint*>(g_frame->anchor), points,
                           sizeof(rtx::frame::Share::AnchorPoint) * (std::size_t)count);
    g_frame->anchor_count = (std::uint32_t)count;
    ++g_frame->anchor_seq;
    return true;
}

bool Install() {
    const bool wantVk = PrefersVulkan() || GetModuleHandleW(L"vulkan-1.dll") != nullptr;
    const bool wantGl = !PrefersVulkan() || GetModuleHandleW(L"opengl32.dll") != nullptr;
    bool any = false;
    if (wantVk) any |= InstallVk();
    if (wantGl) any |= InstallGl();
    return any;
}

void Poll() {
    // a library that arrives later gets its hook then
    if (!g_vkInstalled && GetModuleHandleW(L"vulkan-1.dll")) InstallVk();
    if (!g_glInstalled && GetModuleHandleW(L"opengl32.dll")) InstallGl();
    if (g_vkInstalled) rtx::vkpresent::Poll();
}

const char* Mode() {
    if (g_vkInstalled && rtx::vkpresent::Active()) return g_glInstalled ? "vulkan (opengl hook idle)" : "vulkan";
    if (g_vkInstalled && g_glInstalled) return "opengl and vulkan hooks in, waiting for the first present";
    if (g_vkInstalled) return "vulkan (waiting for device)";
    return g_glInstalled ? "opengl" : "off";
}

void Uninstall() {
    rtx::enginehl::Restore();
    if (!g_glInstalled || !g_origSwap) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(g_origSwap), reinterpret_cast<PVOID>(OnPresent));
    DetourTransactionCommit();
    g_glInstalled = false;
}

}  // namespace rtx::present
