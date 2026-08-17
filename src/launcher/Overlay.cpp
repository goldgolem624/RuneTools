#include "Overlay.h"

#include "Companion.h"
#include "Dock.h"                           // embedded game-window handle lookup
#include "Markers.h"                        // persistent user tile-marker store
#include "../reader/Reader.h"
#include "../cache/CacheReader.h"           // per-tile terrain height for marker placement
#include "../shared/Log.h"
#include "../../companion/MarkerShare.h"   // in-frame world-marker command channel

#include <Windows.h>
// gdiplus.h references unqualified min/max; if the project suppresses the
// Windows macros (NOMINMAX) the std versions must be pulled into scope first.
#ifdef NOMINMAX
#include <algorithm>
using std::max;
using std::min;
#endif
#include <gdiplus.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

namespace rtx::overlay {

namespace {

std::mutex        g_mu;
// PER-CLIENT overlay state: each panel/alert engine configures ITS pid, and everything in-frame
// (grid, markers, highlights, alert flash) publishes into that pid's own marker section, drawn by
// that client's present layer. One client can never paint on another's render. (Guarded by g_mu.)
std::map<DWORD, Config>    g_cfgs;
std::uint64_t              g_cfgsVer = 0;     // bumped on every g_cfgs mutation (guarded by g_mu):
                                              // the render loop re-snapshots only when this moves
// Every g_cfgs WRITE goes through this (never index g_cfgs directly): it marks
// the render loop's snapshot stale. Caller holds g_mu.
Config& cfg_slot(DWORD pid) {
    ++g_cfgsVer;
    Config& c = g_cfgs[pid];
    c.pid = pid;
    return c;
}
std::map<DWORD, long long> g_flash_until;     // per-pid alert-flash deadline (steady ms)
std::thread       g_thread;
std::atomic<bool> g_running{false};
constexpr long long    kFlashMs = 480;        // flash duration
std::atomic<DWORD>     g_toast_pid{0};         // game pid for the on-screen notification
std::atomic<long long> g_toast_until_ms{0};    // toast visible while now < this
std::string            g_toast_text;           // notification text (guarded by g_mu)

// In-world design tokens, mirroring the panel palette in ui-assets/client.html :root. Deep
// neutral surfaces, hairline borders, and TWO semantic accents where there used to be one
// colour saying both "the thing you are going to" and "which way you are facing":
//   accent purple = the OBJECTIVE      ok green = YOU and your heading
// SINGLE SOURCE OF TRUTH -- do not hardcode per site.
constexpr int kSurfR = 11,  kSurfG = 13,  kSurfB = 18;    // #0B0D12  plate / scrim fill
constexpr int kInkR  = 5,   kInkG  = 7,   kInkB  = 11;    // #05070B  contour underlay
constexpr int kTxtR  = 232, kTxtG  = 237, kTxtB  = 244;   // #E8EDF4  primary type
constexpr int kAccR  = 140, kAccG  = 111, kAccB  = 253;   // #8C6FFD  --accent-hi
constexpr int kOkR   = 77,  kOkG   = 210, kOkB   = 138;   // #4DD28A  --ok
constexpr int kOkTxR = 142, kOkTxG = 240, kOkTxB = 192;   // #8EF0C0  green type on dark
// Mix a hue 1:3 into the surface -> a dark tinted scrim that still reads as that hue. A
// saturated wash competes with the scene at the same luminance; a dark one never does.
inline int MixSurf(int c, int s) { return (c + s * 3) / 4; }

// Persistent, manually-dismissed notifications: they stack at the top-right of the game window and
// are removed by clicking them (the overlay turns interactive only while the cursor is over one).
std::atomic<DWORD>     g_notif_pid{0};         // game pid for the notifications
struct Notif    { int id; std::string text; long long expire_ms = 0; };  // expire_ms 0 = sticky
struct NotifHit { int id; RECT rect; };        // panel rect in client coords
std::vector<Notif>     g_notifs;               // sticky (guarded by g_mu)
std::vector<NotifHit>  g_notif_hit;            // hit rects, last frame (guarded by g_mu)
int                    g_notif_seq = 0;        // monotonic id (guarded by g_mu)
constexpr long long    kToastMs     = 4000;    // on-screen notification duration
constexpr long long    kToastFadeMs = 500;     // fade-out tail

// Tick metronome widget: a clock-style pulse drawn over the game window, phase-aligned to the
// server tick. Click-drag to move, mouse-wheel to resize; position/size persist to disk.
constexpr double kTickMs = 600.0;              // RS3 server tick
struct Metro {
    bool  on    = false;
    bool  audio = false;
    int   interval = 1;                        // beat every N ticks (1..6)
    DWORD pid   = 0;
    int   x     = -1, y = -1;                   // centre in game-client coords
    int   size  = 46;                          // radius in px
    bool  placed = false;                       // false = auto-centre on the frame; true = user-positioned
    bool  locked = false;                       // true = click-through, non-interactive (no drag/resize)
    bool  dragging = false;
    POINT dragOff{0, 0};
    std::uint32_t lastTick = 0xFFFFFFFFu;       // for new-tick edge detection (audio)
};
Metro g_metro;                                  // guarded by g_mu
RECT  g_metro_hit{0, 0, 0, 0};                  // last-frame widget bbox, client coords (guarded by g_mu)
// Physical px -> game-space px for the external input window (widget rects live in the game's
// pixel space, the window in physical px; they differ when the game runs DPI-virtualized).
// Written by the render loop, read by OverlayWndProc.
std::atomic<double> g_inputScale{1.0};

// Play the metronome click (sounds/metronome.wav next to the exe). SND_ASYNC so it
// never blocks the render loop; a new call pre-empts a still-playing one.
void PlayMetroClick() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    auto slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.resize(slash);
    p += L"\\sounds\\metronome.wav";
    PlaySoundW(p.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

std::wstring metro_path() {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return {};
    std::wstring p = up; p += L"\\RuneToolsX";
    CreateDirectoryW(p.c_str(), nullptr);
    return p + L"\\metro.txt";
}
void SaveMetro() {   // caller holds g_mu
    auto p = metro_path(); if (p.empty()) return;
    std::ofstream f(p.c_str(), std::ios::trunc);
    if (f) f << g_metro.x << ' ' << g_metro.y << ' ' << g_metro.size;
}
void LoadMetro() {   // caller holds g_mu
    auto p = metro_path(); if (p.empty()) return;
    std::ifstream f(p.c_str());
    int x = -1, y = -1, s = 46;
    if (f && (f >> x >> y >> s)) {
        g_metro.x = x; g_metro.y = y; g_metro.size = (s < 20 ? 20 : (s > 240 ? 240 : s));
        g_metro.placed = true;   // a saved position exists -> honour it (else auto-centre)
    }
}

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// XP-tracker panel: session XP gains + XP/h drawn over the game window. Sampled off the live skill
// records (1 Hz); per-skill rates run from each skill's FIRST gain, so idle time before training
// doesn't dilute them. Capped skills (200M) show no gains. Position/minimized persist.
struct XpPanelState {
    bool  on = false;
    bool  locked = false;                       // click-through, no drag/minimize
    bool  showTotal = true;                     // the all-skills combined row
    bool  autoSkills = true;                    // rows appear as skills gain XP (else mask)
    std::uint32_t mask = 0;                     // custom mode: bit i = show skill i
    DWORD pid = 0;
    int   x = -1, y = -1;                       // top-left, game-client coords
    bool  placed = false;                       // false = default spot until dragged
    bool  minimized = false;
    bool  dragging = false;
    POINT dragOff{0, 0};
    // session (all guarded by g_mu)
    bool      haveBase = false;
    long long startMs = 0;                      // first observed gain (total-row basis)
    long long sampleMs = 0;                     // last sample time
    int       base[29] = {};                    // session baseline XP (-1 = unseen)
    int       cur[29]  = {};                    // latest XP (-1 = unseen)
    long long firstGain[29] = {};               // per-skill first-gain time (0 = none)
};
XpPanelState g_xp;                              // guarded by g_mu
RECT g_xp_hit{0, 0, 0, 0};                      // last-frame panel bbox (guarded by g_mu)
RECT g_xp_min{0, 0, 0, 0};                      // minimize-box rect (guarded by g_mu)

std::map<DWORD, std::vector<GuideMark>> g_guides;   // transient guide marks per pid (guarded by g_mu)
std::map<DWORD, std::vector<UiHighlight>> g_uiHighlights;   // transient screen-space UI highlights per pid (guarded by g_mu)
std::map<DWORD, std::string> g_centerTexts;         // transient screen-centre text per pid (guarded by g_mu)
std::map<DWORD, std::vector<PanelBox>> g_panelViz;  // Interfaces-tab panel visualizer boxes per pid (guarded by g_mu)
std::map<DWORD, std::vector<PuzzleCell>> g_puzzleCells;  // puzzle-box next-moves cells per pid (guarded by g_mu)
std::map<DWORD, std::vector<KnotCell>> g_knotCells;      // celtic-knot arrow highlights per pid (guarded by g_mu)
std::map<DWORD, std::vector<SkillBar>> g_skillBars;      // Skills-panel XP progress bars per pid (guarded by g_mu)

void ResetXpSession() {   // caller holds g_mu
    g_xp.haveBase = false;
    g_xp.startMs  = 0;
    for (int i = 0; i < 29; ++i) { g_xp.base[i] = -1; g_xp.cur[i] = -1; g_xp.firstGain[i] = 0; }
}

std::wstring xp_path() {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return {};
    std::wstring p = up; p += L"\\RuneToolsX";
    CreateDirectoryW(p.c_str(), nullptr);
    return p + L"\\xppanel.txt";
}
void SaveXp() {   // caller holds g_mu
    auto p = xp_path(); if (p.empty()) return;
    std::ofstream f(p.c_str(), std::ios::trunc);
    if (f) f << g_xp.x << ' ' << g_xp.y << ' ' << (g_xp.minimized ? 1 : 0);
}
void LoadXp() {   // caller holds g_mu
    auto p = xp_path(); if (p.empty()) return;
    std::ifstream f(p.c_str());
    int x = -1, y = -1, m = 0;
    if (f && (f >> x >> y >> m)) {
        g_xp.x = x; g_xp.y = y; g_xp.minimized = (m != 0);
        g_xp.placed = true;   // a saved position exists -> honour it (else default spot)
    }
}

// gained XP -> rate per hour, from `since`. Suppressed for the first moments so a
// single drop doesn't show as an absurd rate.
long long XpRatePerHour(long long gained, long long since, long long now) {
    long long el = now - since;
    return (gained > 0 && since > 0 && el > 3000) ? (gained * 3600000LL) / el : 0;
}

// ---- target game window ------------------------------------------------
// Only a client whose panel is open (embedded in the host) is drawn into -- the dock publishes
// that window handle. A hidden client has no open panel, so this returns null and the overlay
// never draws on (or projects markers into) it: no panel => no drawing.
HWND FindGameWindow(DWORD pid) {
    return (HWND)rtx::launcher::dock::GameWindowHandle(pid);
}

// ---- projection (RE'd WorldToScreen) ----------------------------
// Projects to the GAMEVIEW viewport: (vpX,vpY) = its top-left within the window, (vpW,vpH) = its
// size. The camera renders into the gameview rect, which is smaller than the client when UI
// panels are docked -- projecting against the full window drifts the tiles. Callers pass the
// window rect when the gameview rect isn't resolved.
bool WorldToScreen(const float* m, float vpX, float vpY, float vpW, float vpH,
                   float x, float y, float z, float& sx, float& sy) {
    float w = m[3] * x + m[11] * y + m[7] * z + m[15];
    if (w <= 1.0f) return false;                          // behind / on camera plane
    float nx = (m[0] * x + m[8] * y + m[4] * z + m[12]) / w;
    float ny = (m[1] * x + m[9] * y + m[5] * z + m[13]) / w;
    float cx = vpW / 2.0f, cy = vpH / 2.0f;
    sx = nx * cx - nx * 2.0f + cx + vpX;
    sy = -(ny * cy) + ny + cy + vpY;
    return true;
}

// ---- near-plane clipping ----------------------------------------------------
// A shape whose corners must ALL project drops out entirely the moment ONE corner crosses behind
// the camera. w (the projection denominator) is AFFINE in world position, so the point where an
// edge crosses the near plane is a plain world-space lerp: clip there and draw the surviving part.
// Points behind the camera are never projected (their coords are garbage); the GL consumer
// scissors off-screen spans.
constexpr float kNearW = 32.0f;   // in front of WorldToScreen's w<=1 cull

inline float ProjW(const float* m, const float* p) {
    return m[3] * p[0] + m[11] * p[1] + m[7] * p[2] + m[15];
}

// Project a world segment, clipping against w >= kNearW. False = fully behind.
bool ClipProjectSegment(const float* m, float vpX, float vpY, float vpW, float vpH,
                        const float* A, const float* B,
                        float& ax, float& ay, float& bx, float& by) {
    float wa = ProjW(m, A), wb = ProjW(m, B);
    if (wa < kNearW && wb < kNearW) return false;
    float Ac[3] = { A[0], A[1], A[2] }, Bc[3] = { B[0], B[1], B[2] };
    if (wa < kNearW) {
        float t = (kNearW - wa) / (wb - wa);
        for (int i = 0; i < 3; ++i) Ac[i] = A[i] + (B[i] - A[i]) * t;
    } else if (wb < kNearW) {
        float t = (kNearW - wb) / (wa - wb);
        for (int i = 0; i < 3; ++i) Bc[i] = B[i] + (A[i] - B[i]) * t;
    }
    return WorldToScreen(m, vpX, vpY, vpW, vpH, Ac[0], Ac[1], Ac[2], ax, ay) &&
           WorldToScreen(m, vpX, vpY, vpW, vpH, Bc[0], Bc[1], Bc[2], bx, by);
}

// Project a world polygon (perimeter order, n <= 8), clipping against
// w >= kNearW (Sutherland-Hodgman on one plane: at most n+1 verts out).
// Returns the projected vertex count; 0 = fully behind the camera.
int ClipProjectPoly(const float* m, float vpX, float vpY, float vpW, float vpH,
                    const float (*P)[3], int n, float* sx, float* sy) {
    float C[9][3]; int cn = 0;
    for (int i = 0; i < n && cn < 9; ++i) {
        const float* A = P[i];
        const float* B = P[(i + 1) % n];
        float wa = ProjW(m, A), wb = ProjW(m, B);
        if (wa >= kNearW) {
            C[cn][0] = A[0]; C[cn][1] = A[1]; C[cn][2] = A[2]; ++cn;
        }
        if (cn < 9 && (wa >= kNearW) != (wb >= kNearW)) {
            float t = (kNearW - wa) / (wb - wa);
            for (int k = 0; k < 3; ++k) C[cn][k] = A[k] + (B[k] - A[k]) * t;
            ++cn;
        }
    }
    int outn = 0;
    for (int i = 0; i < cn; ++i)
        if (WorldToScreen(m, vpX, vpY, vpW, vpH, C[i][0], C[i][1], C[i][2],
                          sx[outn], sy[outn])) ++outn;
    return outn;
}

// ---- cached DIB --------------------------------------------------------
struct Dib {
    HDC     dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    void*   bits = nullptr;
    int     w = 0, h = 0;

    bool ensure(int nw, int nh) {
        if (dc && w == nw && h == nh) return true;
        destroy();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = nw;
        bi.bmiHeader.biHeight = -nh;          // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!dc) return false;
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) { DeleteDC(dc); dc = nullptr; return false; }
        old = (HBITMAP)SelectObject(dc, bmp);
        w = nw; h = nh;
        return true;
    }
    void clear() { if (bits) memset(bits, 0, (size_t)w * h * 4); }
    // GDI+ writes straight-alpha ARGB; UpdateLayeredWindow wants premultiplied.
    void premultiply() {
        auto* p = reinterpret_cast<unsigned char*>(bits);
        if (!p) return;
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i, p += 4) {
            unsigned a = p[3];
            if (a == 255) continue;
            if (a == 0) { p[0] = p[1] = p[2] = 0; continue; }
            p[0] = (unsigned char)(p[0] * a / 255);
            p[1] = (unsigned char)(p[1] * a / 255);
            p[2] = (unsigned char)(p[2] * a / 255);
        }
    }
    void destroy() {
        if (dc) { if (old) SelectObject(dc, old); DeleteDC(dc); }
        if (bmp) DeleteObject(bmp);
        dc = nullptr; bmp = nullptr; old = nullptr; bits = nullptr; w = h = 0;
    }
};

void DrawFrame(Gdiplus::Graphics& g, const Config& cfg,
               const rtx::reader::OverlayFrame& f, int W, int H) {
    using namespace Gdiplus;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    // GridFit/ClearType writes broken alpha on a transparent layered window
    // (text disappears); plain AntiAlias writes coverage alpha that survives.
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    // The camera renders into the gameview viewport -- a sub-rect of the window when UI panels are
    // docked. Project against it; fall back to the full window when the reader couldn't resolve the
    // gameview rect (gv_w/h == 0). Clamp to the client rect: the widget can read oversized during a
    // resize, and a gameview larger than the window would throw the grid off.
    float vpW = f.gv_w > 0 ? (float)f.gv_w : (float)W;
    float vpH = f.gv_h > 0 ? (float)f.gv_h : (float)H;
    if (vpW > (float)W) vpW = (float)W;
    if (vpH > (float)H) vpH = (float)H;
    float vpX = f.gv_w > 0 ? (float)f.gv_x : 0.0f;
    float vpY = f.gv_h > 0 ? (float)f.gv_y : 0.0f;
    if (vpX < 0.0f) vpX = 0.0f;
    if (vpY < 0.0f) vpY = 0.0f;
    if (vpX + vpW > (float)W) vpX = (float)W - vpW;
    if (vpY + vpH > (float)H) vpY = (float)H - vpH;

    // --- tile grid (corners projected at the player's plane height), coloured
    //     by walkability: blocked tiles tinted red, or hidden when walk_only ---
    if (cfg.grid && f.grid_r > 0) {
        const int R = f.grid_r;
        const int T = 2 * R + 1;          // tiles per axis
        // PER-TILE corners (SW,SE,NE,NW), not a shared lattice. A shared corner cannot hold
        // two heights, so at a bridge edge the deck quad was dragged down to the ravine.
        static thread_local std::vector<float> px, py;   // reused each frame (render thread only)
        static thread_local std::vector<char>  vis;
        px.assign((size_t)T * T * 4, 0.0f); py.assign((size_t)T * T * 4, 0.0f);
        vis.assign((size_t)T * T * 4, 0);
        // Terrain-follow: heights are ABSOLUTE -- live fine-z = 32 * cache surface
        // height (bridge-aware cumulative, see CacheReader::AbsHeightLocked).
        const float kHScale = 32.0f;
        const std::int16_t kNoH = -32768;
        bool haveH = (int)f.heights.size() == T * T * 4;
        static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
        for (int tgx = 0; tgx < T; ++tgx) {
            for (int tgy = 0; tgy < T; ++tgy) {
                for (int c = 0; c < 4; ++c) {
                    size_t i = ((size_t)tgx * T + tgy) * 4 + c;
                    float wx = (float)(f.player_tx - R + tgx + CX[c]) * 512.0f;
                    float wy = (float)(f.player_ty - R + tgy + CY[c]) * 512.0f;
                    // Corners with no cache height belong to a region with no map data, so leave
                    // them invisible instead of drawing a fake flat grid over unmapped space
                    // (cells need all 4 corners visible).
                    if (haveH && f.heights[i] == kNoH) continue;
                    float cz = (haveH && f.heights[i] != kNoH)
                             ? kHScale * (float)f.heights[i] : f.player_z;
                    float sx, sy;
                    // Corners closest to the camera have tiny w and project to huge coords at
                    // grazing angles -- clip anything more than ~one screen off-edge.
                    if (WorldToScreen(f.matrix, vpX, vpY, vpW, vpH, wx, wy, cz, sx, sy) &&
                        sx > -(float)W && sx < 2.0f * W && sy > -(float)H && sy < 2.0f * H) {
                        px[i] = sx; py[i] = sy; vis[i] = 1;
                    }
                }
            }
        }
        Pen pen(Color(175, 150, 215, 255), 1.3f);        // brighter cyan-white grid
        Pen center(Color(255, 150, 250, 150), 2.4f);     // player's own tile
        Pen wallPen(Color(255, 95, 175, 255), 2.6f);     // blue directional wall edges
        SolidBrush blockFill(Color(125, 240, 80, 80));   // stronger red on full-blocked tiles
        // flags byte per tile: 0x01 N,0x02 S,0x04 E,0x08 W edges; 0x10 full-block; 0x20 has-wall
        auto flagAt = [&](int tgx, int tgy) -> int {
            size_t idx = (size_t)tgx * T + tgy;
            return idx < f.blocked.size() ? f.blocked[idx] : 0;
        };
        for (int tgx = 0; tgx < T; ++tgx) {
            for (int tgy = 0; tgy < T; ++tgy) {
                int fl = flagAt(tgx, tgy);
                bool full = (fl & 0x10) != 0;
                if (full && cfg.walk_only) continue;
                const size_t base = ((size_t)tgx * T + tgy) * 4;   // this tile's own corners
                size_t a = base + 0, b = base + 1, c = base + 2, d = base + 3;   // SW,SE,NE,NW
                if (!(vis[a] && vis[b] && vis[c] && vis[d])) continue;
                if (full) {
                    PointF poly[4] = { {px[a],py[a]}, {px[b],py[b]}, {px[c],py[c]}, {px[d],py[d]} };
                    g.FillPolygon(&blockFill, poly, 4);
                }
                bool self = (tgx == R && tgy == R);
                Pen* e = self ? &center : &pen;
                g.DrawLine(e, px[a], py[a], px[b], py[b]);
                g.DrawLine(e, px[b], py[b], px[c], py[c]);
                g.DrawLine(e, px[c], py[c], px[d], py[d]);
                g.DrawLine(e, px[d], py[d], px[a], py[a]);
                // directional wall edges (corner map: a=SW b=SE c=NE d=NW)
                if (fl & 0x08) g.DrawLine(&wallPen, px[a], py[a], px[d], py[d]);  // W
                if (fl & 0x04) g.DrawLine(&wallPen, px[b], py[b], px[c], py[c]);  // E
                if (fl & 0x02) g.DrawLine(&wallPen, px[a], py[a], px[b], py[b]);  // S
                if (fl & 0x01) g.DrawLine(&wallPen, px[d], py[d], px[c], py[c]);  // N
            }
        }
    }

    // --- entity / object markers ---
    // Two passes: dots first, then labels with a dark rounded background and overlap avoidance.
    // Higher-priority kinds (players > NPCs > objects) claim label space first; a label that would
    // collide with one already placed is dropped (its dot still shows).
    FontFamily ff(L"Segoe UI");
    Font font(&ff, 11.0f, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(Color(255, 245, 245, 245));
    SolidBrush pill(Color(170, 14, 16, 24));         // dark translucent label bg
    Color kindCol[4] = { Color(255, 90, 200, 235),   // 0 object  (cyan)
                         Color(255, 245, 210, 80),   // 1 npc     (yellow)
                         Color(255, 90, 220, 120),   // 2 player  (green)
                         Color(255, 245, 165, 60) }; // 3 special (amber)
    auto wanted = [&](int kind) {
        return kind == 0 ? cfg.objects : kind == 1 ? cfg.npcs : kind == 2 ? cfg.players : cfg.specials;
    };
    auto project = [&](const rtx::reader::OverlayPoint& p, float& sx, float& sy) {
        if (!WorldToScreen(f.matrix, vpX, vpY, vpW, vpH, p.wx, p.wy, p.wz, sx, sy)) return false;
        return !(sx < -40 || sx > W + 40 || sy < -40 || sy > H + 40);
    };
    auto fillRound = [](Graphics& gr, Brush* b, float x, float y, float w, float h, float r) {
        GraphicsPath path;
        path.AddArc(x, y, r * 2, r * 2, 180, 90);
        path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
        path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        gr.FillPath(b, &path);
    };
    for (const auto& p : f.points) {                 // dots / object footprints
        if (!wanted(p.kind)) continue;
        // Anything with a live model AABB (objects + NPCs) draws a true 3D box (real
        // size + height): project the 8 corners, fill the base, outline the 12 edges.
        if (p.has_box3d) {
            float ex[2] = { p.bmin[0], p.bmax[0] };   // east
            float ny[2] = { p.bmin[1], p.bmax[1] };   // north
            float uz[2] = { p.bmin[2], p.bmax[2] };   // up
            Gdiplus::PointF v[8]; bool okAll = true;
            // corner order: bit0=east, bit1=north, bit2=up
            for (int c = 0; c < 8; ++c) {
                float sx, sy;
                if (!WorldToScreen(f.matrix, vpX, vpY, vpW, vpH,
                                   ex[c & 1], ny[(c >> 1) & 1], uz[(c >> 2) & 1], sx, sy) ||
                    sx < -3.0f * W || sx > 4.0f * W || sy < -3.0f * H || sy > 4.0f * H) { okAll = false; break; }
                v[c] = Gdiplus::PointF(sx, sy);
            }
            if (okAll) {
                Color kc = kindCol[p.kind];               // box coloured by kind (cyan obj / yellow npc)
                Pen edge(Color(235, kc.GetR(), kc.GetG(), kc.GetB()), 1.8f);
                SolidBrush baseFill(Color(40, kc.GetR(), kc.GetG(), kc.GetB()));
                Gdiplus::PointF base[4] = { v[0], v[1], v[3], v[2] };   // up=min face (east,north quad)
                g.FillPolygon(&baseFill, base, 4);
                static const int E[12][2] = { {0,1},{1,3},{3,2},{2,0},   // bottom (up=min)
                                              {4,5},{5,7},{7,6},{6,4},   // top (up=max)
                                              {0,4},{1,5},{2,6},{3,7} }; // verticals
                for (auto& e : E) g.DrawLine(&edge, v[e[0]], v[e[1]]);
                continue;
            }
            // projection failed -> fall through to dot
        }
        // Objects with a footprint draw the covered tiles extruded by box_h into a prism
        // (flat quads read as floor decals, not scenery): corners 0-3 = ground, 4-7 = top.
        if (p.kind == 0 && p.has_box) {
            Gdiplus::PointF poly[8];
            bool okAll = true;
            const int nv = (p.box_h > 0.f) ? 8 : 4;
            for (int i = 0; i < nv; ++i) {
                float sx, sy;
                float bz = p.box[(i & 3) * 3 + 2] + ((i & 4) ? p.box_h : 0.f);
                if (!WorldToScreen(f.matrix, vpX, vpY, vpW, vpH,
                                   p.box[(i & 3) * 3], p.box[(i & 3) * 3 + 1], bz, sx, sy) ||
                    sx < -2.0f * W || sx > 3.0f * W || sy < -2.0f * H || sy > 3.0f * H) { okAll = false; break; }
                poly[i] = Gdiplus::PointF(sx, sy);
            }
            if (okAll) {
                SolidBrush fill(Color(46, 90, 200, 235));    // translucent cyan fill
                g.FillPolygon(&fill, poly, 4);               // ground face only
                Pen edge(Color(230, 120, 215, 245), 2.0f);   // cyan outline
                g.DrawPolygon(&edge, poly, 4);
                if (nv == 8) {
                    Gdiplus::PointF top[4] = { poly[4], poly[5], poly[6], poly[7] };
                    g.DrawPolygon(&edge, top, 4);
                    for (int i = 0; i < 4; ++i) g.DrawLine(&edge, poly[i], poly[4 + i]);
                }
                continue;
            }
            // projection failed -> fall through to a dot
        }
        float sx, sy;
        if (!project(p, sx, sy)) continue;
        SolidBrush dot(kindCol[p.kind]);
        if (p.kind == 0)      g.FillRectangle(&dot, sx - 2.5f, sy - 2.5f, 5.0f, 5.0f);
        else if (p.kind == 3) g.FillEllipse(&dot, sx - 4.0f, sy - 4.0f, 8.0f, 8.0f);   // specials: bigger
        else                  g.FillEllipse(&dot, sx - 3.0f, sy - 3.0f, 6.0f, 6.0f);
    }
    std::vector<RectF> placed;                        // labels (priority 3->2->1->0)
    for (int kp = 3; kp >= 0; --kp) {
        if (!wanted(kp)) continue;
        for (const auto& p : f.points) {
            if (p.kind != kp || p.label.empty()) continue;
            float sx, sy;
            if (!project(p, sx, sy)) continue;
            int wl = MultiByteToWideChar(CP_UTF8, 0, p.label.c_str(), -1, nullptr, 0);
            std::wstring w(wl > 1 ? wl - 1 : 0, L'\0');
            if (wl > 1) MultiByteToWideChar(CP_UTF8, 0, p.label.c_str(), -1, &w[0], wl);
            float lx = sx + 7.0f, ly = sy - 8.0f;
            RectF box;
            g.MeasureString(w.c_str(), -1, &font, PointF(lx, ly), &box);
            RectF rect(lx - 3.0f, ly - 1.0f, box.Width + 6.0f, box.Height + 2.0f);
            bool clash = false;
            for (const auto& q : placed) if (rect.IntersectsWith(q)) { clash = true; break; }
            if (clash) continue;
            placed.push_back(rect);
            fillRound(g, &pill, rect.X, rect.Y, rect.Width, rect.Height, 5.0f);
            g.DrawString(w.c_str(), -1, &font, PointF(lx, ly), &textBrush);
        }
    }

    // --- highlighted NPCs (e.g. random events): a pulsing gold ring + label,
    //     drawn until the NPC leaves the scene. Independent of the npc toggle. ---
    if (!f.highlights.empty()) {
        float pulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1000) / 1000.0 * 6.2831853));
        SolidBrush goldText(Color(255, 255, 226, 128));
        for (const auto& hp : f.highlights) {
            if (hp.has_box3d) {                              // Scene-tab outline -> 3D box
                float ex[2] = { hp.bmin[0], hp.bmax[0] }, ny[2] = { hp.bmin[1], hp.bmax[1] }, uz[2] = { hp.bmin[2], hp.bmax[2] };
                Gdiplus::PointF v[8]; bool okAll = true;
                for (int c = 0; c < 8; ++c) {
                    float sx, sy;
                    if (!WorldToScreen(f.matrix, vpX, vpY, vpW, vpH, ex[c & 1], ny[(c >> 1) & 1], uz[(c >> 2) & 1], sx, sy) ||
                        sx < -3.0f * W || sx > 4.0f * W || sy < -3.0f * H || sy > 4.0f * H) { okAll = false; break; }
                    v[c] = Gdiplus::PointF(sx, sy);
                }
                if (okAll) {
                    Pen edge(Color(245, 70, 224, 192), 2.0f);
                    static const int E[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
                    for (auto& e : E) g.DrawLine(&edge, v[e[0]], v[e[1]]);
                    continue;
                }
            }
            float sx, sy;
            if (!WorldToScreen(f.matrix, vpX, vpY, vpW, vpH, hp.wx, hp.wy, hp.wz, sx, sy)) continue;
            if (sx < -60 || sx > W + 60 || sy < -60 || sy > H + 60) continue;
            float rad = 13.0f + 6.0f * pulse;
            Pen ring(Color(255, 255, 205, 70), 3.0f);
            g.DrawEllipse(&ring, sx - rad, sy - rad, rad * 2, rad * 2);
            Pen ring2(Color(150, 255, 235, 140), 1.6f);
            g.DrawEllipse(&ring2, sx - (rad + 5), sy - (rad + 5), (rad + 5) * 2, (rad + 5) * 2);
            SolidBrush core(Color(235, 255, 215, 80));
            g.FillEllipse(&core, sx - 4.0f, sy - 4.0f, 8.0f, 8.0f);
            if (!hp.label.empty()) {
                int wl = MultiByteToWideChar(CP_UTF8, 0, hp.label.c_str(), -1, nullptr, 0);
                std::wstring w(wl > 1 ? wl - 1 : 0, L'\0');
                if (wl > 1) MultiByteToWideChar(CP_UTF8, 0, hp.label.c_str(), -1, &w[0], wl);
                float lx = sx + rad + 6.0f, ly = sy - 7.0f;
                RectF box;
                g.MeasureString(w.c_str(), -1, &font, PointF(lx, ly), &box);
                fillRound(g, &pill, lx - 3.0f, ly - 1.0f, box.Width + 6.0f, box.Height + 2.0f, 5.0f);
                g.DrawString(w.c_str(), -1, &font, PointF(lx, ly), &goldText);
            }
        }
    }
}

// ---- in-frame world markers --------------------------------------------
// Mirror of DrawFrame's projection, but emitting MarkerShare primitive commands (lines / fills)
// into the game's own frame via the companion instead of GDI+ draws on the external layered
// window. Same WorldToScreen + viewport, so positions match the external overlay.
namespace marker = rtx::marker;

struct MarkerOut {
    HANDLE         map = nullptr;
    marker::Share* p   = nullptr;
    std::uint32_t  pid = 0;

    bool ensure(std::uint32_t target) {
        if (p && pid == target) return true;
        close();
        if (!target) return false;
        wchar_t name[64];
        marker::MakeSectionName(target, name);
        map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                 (DWORD)sizeof(marker::Share), name);
        if (!map) return false;
        p = reinterpret_cast<marker::Share*>(
            MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, sizeof(marker::Share)));
        if (!p) { CloseHandle(map); map = nullptr; return false; }
        p->version = marker::kVersion;
        p->pid     = target;
        p->magic   = marker::kMagic;       // last: signals "initialised" to the reader
        pid = target;
        return true;
    }
    void close() {
        if (p)   { UnmapViewOfFile(p); p = nullptr; }
        if (map) { CloseHandle(map); map = nullptr; }
        pid = 0;
    }
};
std::map<DWORD, MarkerOut> g_marker_outs;   // one channel PER client; render thread only

// Publish the marker command list for `f` (already gathered for WxH) into cfg.pid's OWN section.
// `f == nullptr` with no flash marks the channel not-visible so that client's in-frame layer stops
// drawing. flashAlpha > 0 paints the alert flash into that client's render. Grid/markers are gated
// on cfg.enabled; highlights + flash draw even with the overlay off (they are alert-driven).
void PublishMarkers(const Config& cfg, const rtx::reader::OverlayFrame* f, int W, int H,
                    float flashAlpha = 0.0f,
                    const std::vector<marker::Command>* widgets = nullptr) {
    auto& out = g_marker_outs[cfg.pid];
    if (!out.ensure(cfg.pid)) return;
    marker::Share* sh = out.p;

    // Mystery guide marks draw INDEPENDENTLY of the overlay toggles (like alert highlights), so they
    // must count as content here or the early-out below publishes an empty frame first.
    bool hasGuides = false;   // gate only -- drawing uses the frame's RESOLVED guides
    std::vector<UiHighlight> uihls;
    std::string ctext;
    std::vector<PanelBox> pviz;
    std::vector<PuzzleCell> pcells;
    std::vector<KnotCell> kcells;
    std::vector<SkillBar> sbars;
    { std::lock_guard<std::mutex> lk(g_mu);
      auto git = g_guides.find(cfg.pid);
      hasGuides = (git != g_guides.end() && !git->second.empty());
      auto uit = g_uiHighlights.find(cfg.pid);
      if (uit != g_uiHighlights.end()) uihls = uit->second;
      auto ctit = g_centerTexts.find(cfg.pid);
      if (ctit != g_centerTexts.end()) ctext = ctit->second;
      auto pit = g_panelViz.find(cfg.pid);
      if (pit != g_panelViz.end()) pviz = pit->second;
      auto pcit = g_puzzleCells.find(cfg.pid);
      if (pcit != g_puzzleCells.end()) pcells = pcit->second;
      auto kcit = g_knotCells.find(cfg.pid);
      if (kcit != g_knotCells.end()) kcells = kcit->second;
      auto sbit = g_skillBars.find(cfg.pid);
      if (sbit != g_skillBars.end()) sbars = sbit->second; }

    bool wantContent = flashAlpha > 0.0f || !uihls.empty() || !ctext.empty() || !pviz.empty() || !pcells.empty() || !kcells.empty() || !sbars.empty() ||
                       (widgets && !widgets->empty()) ||
                       (f && (cfg.enabled || cfg.markers || cfg.nameplates || !f->highlights.empty() || hasGuides));
    if (!wantContent) {
        std::uint32_t s = sh->seq + 1;
        sh->seq = s; MemoryBarrier();
        sh->count = 0; sh->visible = 0;
        MemoryBarrier(); sh->seq = s + 1;
        return;
    }

    // Same gameview viewport math as DrawFrame (full client area when flash-only).
    float vpW = (f && f->gv_w > 0) ? (float)f->gv_w : (float)W;
    float vpH = (f && f->gv_h > 0) ? (float)f->gv_h : (float)H;
    if (vpW > (float)W) vpW = (float)W;
    if (vpH > (float)H) vpH = (float)H;
    float vpX = (f && f->gv_w > 0) ? (float)f->gv_x : 0.0f;
    float vpY = (f && f->gv_h > 0) ? (float)f->gv_y : 0.0f;
    if (vpX < 0.0f) vpX = 0.0f;
    if (vpY < 0.0f) vpY = 0.0f;
    if (vpX + vpW > (float)W) vpX = (float)W - vpW;
    if (vpY + vpH > (float)H) vpY = (float)H - vpH;

    static std::vector<marker::Command> cmds;   // reused each frame (render thread only)
    cmds.clear();
    auto push = [&](const marker::Command& c) { if (cmds.size() < marker::kMaxCmds) cmds.push_back(c); };
    auto line = [&](float x0, float y0, float x1, float y1, float th,
                    int r, int g, int b, int a) {
        marker::Command c{}; c.type = marker::kLine;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.thickness = th;
        c.r = (std::uint8_t)r; c.g = (std::uint8_t)g; c.b = (std::uint8_t)b; c.a = (std::uint8_t)a;
        push(c);
    };
    auto dot = [&](float cx, float cy, float half, int r, int g, int b, int a) {
        marker::Command c{}; c.type = marker::kFillRect;
        c.x0 = cx - half; c.y0 = cy - half; c.x1 = cx + half; c.y1 = cy + half;
        c.r = (std::uint8_t)r; c.g = (std::uint8_t)g; c.b = (std::uint8_t)b; c.a = (std::uint8_t)a;
        push(c);
    };

    // --- tile grid: corner lattice projected at plane height; walls + blocked ---
    if (f && cfg.enabled && cfg.grid && f->grid_r > 0) {
        const int R = f->grid_r;
        const int T = 2 * R + 1;
        // PER-TILE corners (SW,SE,NE,NW). A shared lattice corner can hold only one height,
        // so where a bridge deck meets the ravine beside it the deck quad was pulled down
        // into the water; each tile now carries its own four, decided at its own plane.
        static thread_local std::vector<float> px, py, wz;   // reused each frame (render thread only)
        static thread_local std::vector<char>  vis;
        const size_t NC = (size_t)T * T * 4;
        px.assign(NC, 0.0f); py.assign(NC, 0.0f); wz.assign(NC, 0.0f); vis.assign(NC, 0);
        const float kHScale = 32.0f;       // absolute: live fine-z = 32 * cache height
        const std::int16_t kNoH = -32768;
        bool haveH = f->heights.size() == NC;
        static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
        // Corner classes: 0 = unusable (no height / projects far off-screen), 1 = drawable,
        // 2 = behind the near plane (edges to it get clipped instead of dropped, so the
        // grid stays continuous down to the screen edge when the camera tilts low).
        for (int tgx = 0; tgx < T; ++tgx)
            for (int tgy = 0; tgy < T; ++tgy)
                for (int c = 0; c < 4; ++c) {
                    size_t i = ((size_t)tgx * T + tgy) * 4 + c;
                    float wx = (float)(f->player_tx - R + tgx + CX[c]) * 512.0f;
                    float wy = (float)(f->player_ty - R + tgy + CY[c]) * 512.0f;
                    if (haveH && f->heights[i] == kNoH) continue;
                    float cz = (haveH && f->heights[i] != kNoH)
                             ? kHScale * (float)f->heights[i] : f->player_z;
                    wz[i] = cz;
                    const float wpt[3] = { wx, wy, cz };
                    if (ProjW(f->matrix, wpt) < kNearW) { vis[i] = 2; continue; }
                    float sx, sy;
                    if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, wx, wy, cz, sx, sy) &&
                        sx > -(float)W && sx < 2.0f * W && sy > -(float)H && sy < 2.0f * H) {
                        px[i] = sx; py[i] = sy; vis[i] = 1;
                    }
                }
        auto flagAt = [&](int tgx, int tgy) -> int {
            size_t idx = (size_t)tgx * T + tgy;
            return idx < f->blocked.size() ? f->blocked[idx] : 0;
        };
        // Full-blocked tiles: a soft red fill under the lattice.
        if (!cfg.walk_only)
            for (int tgx = 0; tgx < T; ++tgx)
                for (int tgy = 0; tgy < T; ++tgy) {
                    if (!(flagAt(tgx, tgy) & 0x10)) continue;
                    const size_t base = ((size_t)tgx * T + tgy) * 4;
                    size_t a = base + 0, b = base + 1, c = base + 2, d = base + 3;
                    if (vis[a] != 1 || vis[b] != 1 || vis[c] != 1 || vis[d] != 1) continue;
                    marker::Command q{}; q.type = marker::kFillQuad;
                    q.x0 = px[a]; q.y0 = py[a]; q.x1 = px[b]; q.y1 = py[b];
                    q.x2 = px[c]; q.y2 = py[c]; q.x3 = px[d]; q.y3 = py[d];
                    q.r = 240; q.g = 70; q.b = 70; q.a = 55;
                    push(q);
                }
        // Each lattice edge is drawn ONCE in a uniform colour (player tile green, everything
        // else cyan-white); blocked is the fill, not the lines.
        auto tileShown = [&](int tgx, int tgy, bool& shown, bool& self) {
            shown = self = false;
            if (tgx < 0 || tgy < 0 || tgx >= T || tgy >= T) return;
            if ((flagAt(tgx, tgy) & 0x10) && cfg.walk_only) return;  // hidden by the filter
            shown = true;
            self = (tgx == R && tgy == R);
        };
        auto cornerWorld = [&](size_t i, float* out) {
            const size_t tile = i / 4; const int c = (int)(i % 4);
            out[0] = (float)(f->player_tx - R + (int)(tile / T) + CX[c]) * 512.0f;
            out[1] = (float)(f->player_ty - R + (int)(tile % T) + CY[c]) * 512.0f;
            out[2] = wz[i];
        };
        auto edge = [&](size_t c0, size_t c1, bool self) {
            bool clipped = (vis[c0] == 1 && vis[c1] == 2) || (vis[c0] == 2 && vis[c1] == 1);
            if (!clipped && !(vis[c0] == 1 && vis[c1] == 1)) return;
            float x0 = px[c0], y0 = py[c0], x1 = px[c1], y1 = py[c1];
            if (clipped) {                     // one corner behind the camera
                float A[3], B[3];
                cornerWorld(c0, A); cornerWorld(c1, B);
                if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH, A, B, x0, y0, x1, y1)) return;
            }
            if (self) line(x0, y0, x1, y1, 2.4f, 150, 250, 150, 255);
            else      line(x0, y0, x1, y1, 1.3f, 150, 215, 255, 175);
        };
        // Each tile draws its OWN four edges, so a bridge seam can be a step rather than a
        // shared corner both sides have to agree on. Interior edges are therefore drawn twice
        // (identical colour, so no visual change); the player's tile is drawn in a second pass
        // so its green edges land on top of a neighbour's cyan.
        for (int pass = 0; pass < 2; ++pass)
            for (int tgx = 0; tgx < T; ++tgx)
                for (int tgy = 0; tgy < T; ++tgy) {
                    bool shown = false, self = false;
                    tileShown(tgx, tgy, shown, self);
                    if (!shown || self != (pass == 1)) continue;
                    const size_t base = ((size_t)tgx * T + tgy) * 4;
                    edge(base + 0, base + 1, self);      // S
                    edge(base + 1, base + 2, self);      // E
                    edge(base + 2, base + 3, self);      // N
                    edge(base + 3, base + 0, self);      // W
                }
        // directional wall edges on top (a=SW b=SE c=NE d=NW)
        for (int tgx = 0; tgx < T; ++tgx)
            for (int tgy = 0; tgy < T; ++tgy) {
                int fl = flagAt(tgx, tgy);
                if (!(fl & 0x0f)) continue;
                if ((fl & 0x10) && cfg.walk_only) continue;
                const size_t base = ((size_t)tgx * T + tgy) * 4;
                size_t a = base + 0, b = base + 1, c = base + 2, d = base + 3;
                if (vis[a] != 1 || vis[b] != 1 || vis[c] != 1 || vis[d] != 1) continue;
                if (fl & 0x08) line(px[a], py[a], px[d], py[d], 2.6f, 95, 175, 255, 255);
                if (fl & 0x04) line(px[b], py[b], px[c], py[c], 2.6f, 95, 175, 255, 255);
                if (fl & 0x02) line(px[a], py[a], px[b], py[b], 2.6f, 95, 175, 255, 255);
                if (fl & 0x01) line(px[d], py[d], px[c], py[c], 2.6f, 95, 175, 255, 255);
            }
    }

    // --- entity / object markers: live AABB / footprint outline, else a dot ---
    struct KC { int r, g, b; };
    const KC kindCol[4] = { {90,200,235}, {245,210,80}, {90,220,120}, {245,165,60} };
    auto wanted = [&](int kind) {
        return kind == 0 ? cfg.objects : kind == 1 ? cfg.npcs : kind == 2 ? cfg.players : cfg.specials;
    };
    if (f && cfg.enabled) for (const auto& p : f->points) {
        if (!wanted(p.kind)) continue;
        const KC kc = kindCol[p.kind >= 0 && p.kind < 4 ? p.kind : 0];
        // Box/prism edges are clipped at the near plane INDIVIDUALLY, so a shape keeps its
        // visible part when the camera moves inside it instead of vanishing whole; a shape
        // projecting entirely outside the expanded screen guard still skips.
        if (p.has_box3d) {                                   // true 3D box -> 12 edges
            float wc[8][3];                                  // corner bits: 1=maxE 2=maxN 4=maxUp
            for (int cc = 0; cc < 8; ++cc) {
                wc[cc][0] = (cc & 1) ? p.bmax[0] : p.bmin[0];
                wc[cc][1] = (cc & 2) ? p.bmax[1] : p.bmin[1];
                wc[cc][2] = (cc & 4) ? p.bmax[2] : p.bmin[2];
            }
            static const int E[12][2] = { {0,1},{1,3},{3,2},{2,0},
                                          {4,5},{5,7},{7,6},{6,4},
                                          {0,4},{1,5},{2,6},{3,7} };
            float ex0[12], ey0[12], ex1[12], ey1[12]; int ns = 0; bool onscr = false;
            for (auto& e : E) {
                float ax, ay, bx, by;
                if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH,
                                        wc[e[0]], wc[e[1]], ax, ay, bx, by)) continue;
                ex0[ns] = ax; ey0[ns] = ay; ex1[ns] = bx; ey1[ns] = by; ++ns;
                onscr = onscr || (ax > -3.0f * W && ax < 4.0f * W && ay > -3.0f * H && ay < 4.0f * H)
                              || (bx > -3.0f * W && bx < 4.0f * W && by > -3.0f * H && by < 4.0f * H);
            }
            if (ns && onscr) {
                for (int i = 0; i < ns; ++i)
                    line(ex0[i], ey0[i], ex1[i], ey1[i], 1.8f, kc.r, kc.g, kc.b, 235);
                continue;
            }
        }
        if (p.kind == 0 && p.has_box) {                      // object footprint -> prism
            float wc[8][3];                                  // corners 0-3 ground, 4-7 top
            for (int i = 0; i < 4; ++i) {
                wc[i][0] = p.box[i * 3]; wc[i][1] = p.box[i * 3 + 1]; wc[i][2] = p.box[i * 3 + 2];
                wc[4 + i][0] = wc[i][0]; wc[4 + i][1] = wc[i][1]; wc[4 + i][2] = wc[i][2] + p.box_h;
            }
            const bool top = (p.box_h > 0.f);
            struct Seg { float ax, ay, bx, by, th; int a; };
            Seg segs[12]; int ns = 0; bool onscr = false;
            auto addEdge = [&](int i0, int i1, float th, int alpha) {
                float ax, ay, bx, by;
                if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH,
                                        wc[i0], wc[i1], ax, ay, bx, by)) return;
                segs[ns++] = { ax, ay, bx, by, th, alpha };
                onscr = onscr || (ax > -2.0f * W && ax < 3.0f * W && ay > -2.0f * H && ay < 3.0f * H)
                              || (bx > -2.0f * W && bx < 3.0f * W && by > -2.0f * H && by < 3.0f * H);
            };
            for (int i = 0; i < 4; ++i) {
                addEdge(i, (i + 1) & 3, 2.0f, 230);
                if (top) {
                    addEdge(4 + i, 4 + ((i + 1) & 3), 2.0f, 230);
                    addEdge(i, 4 + i, 1.6f, 200);
                }
            }
            if (ns && onscr) {
                for (int i = 0; i < ns; ++i)
                    line(segs[i].ax, segs[i].ay, segs[i].bx, segs[i].by,
                         segs[i].th, 120, 215, 245, segs[i].a);
                continue;
            }
        }
        float sx, sy;                                        // fallback dot
        if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, p.wx, p.wy, p.wz, sx, sy)) continue;
        if (sx < -40 || sx > W + 40 || sy < -40 || sy > H + 40) continue;
        float half = (p.kind == 3) ? 4.0f : (p.kind == 0 ? 2.5f : 3.0f);
        dot(sx, sy, half, kc.r, kc.g, kc.b, 255);
    }

    // --- entity nameplates: a floating name label over each player / NPC / object, independent of
    //     "Enable overlay" (driven from the Scene tab). Two-stage layout: (1) every entity on the SAME
    //     tile shares that tile's centre anchor -> one column per tile (capped, overflow -> "+N
    //     more"); (2) a greedy 2D de-collision pushes any overlapping pill UPWARD. ---
    if (f && cfg.nameplates) {
        // Fixed glyph height; derive the pill box (see companion DrawLabel) so slot pitch + collision
        // are exact. Width is estimated from the label length (the atlas is proportional).
        const float npTextPx = 11.0f;
        const float cellH  = 34.0f * (npTextPx / 21.0f);     // atlas cell -> screen px
        // MIRRORS Composite.cpp DrawLabel's plate metrics -- these must move in lockstep with
        // it or nameplate stacking silently drifts from what is actually drawn.
        const float pillH  = cellH * 0.78f + 10.0f;          // + padY*2  (padY 5)
        const float padX   = 8.0f;                           // pill horizontal padding
        const float avgAdv = npTextPx * 0.62f;               // over-estimate of a glyph's advance
        const float slotH  = pillH + 3.0f;                   // vertical pitch inside a column
        const float gap    = 2.0f;                           // min gap enforced by de-collision
        const int   kMaxRows = 12;                           // cap per tile; extra -> "+N more"
        const float kMaxRise = slotH * 5.0f;                 // drop a label rather than push it >5 rows off its object

        auto npWanted = [&](int kind) {
            return kind == 0 ? cfg.np_objects : kind == 1 ? cfg.np_npcs
                 : kind == 2 ? cfg.np_players : false;        // specials excluded (scan ring, no tile)
        };
        auto pillW = [&](const std::string& s) { return padX * 2.0f + (float)s.size() * avgAdv; };

        // Gather entities into per-tile buckets; each bucket becomes one column.
        struct Ent { int kind; float wz, head_z; const std::string* label; };
        struct Bucket { int tx, ty, dist; float cx, cy, cwx, cwy; float repZ; bool anyHead; std::vector<Ent> ents; };
        std::map<long long, int> bmap;
        std::vector<Bucket> buckets;
        const int npR = cfg.np_range > 0 ? cfg.np_range : 20;
        for (const auto& p : f->points) {
            if (p.is_self || p.label.empty()) continue;
            if (p.kind == 2) {   // player: if any are individually picked, show ONLY those; else the master toggle = all
                bool show = cfg.np_player_uids.empty()
                    ? cfg.np_players
                    : (std::find(cfg.np_player_uids.begin(), cfg.np_player_uids.end(), p.uid) != cfg.np_player_uids.end());
                if (!show) continue;
            } else if (!npWanted(p.kind)) continue;
            int tx = (int)(p.wx / 512.0f), ty = (int)(p.wy / 512.0f);
            int adx = tx - f->player_tx; if (adx < 0) adx = -adx;
            int ady = ty - f->player_ty; if (ady < 0) ady = -ady;
            int d = adx > ady ? adx : ady;
            if (d > npR) continue;
            long long key = ((long long)tx << 20) | (unsigned)ty;
            auto it = bmap.find(key);
            int bi;
            if (it == bmap.end()) {
                bi = (int)buckets.size(); bmap.emplace(key, bi);
                Bucket b{}; b.tx = tx; b.ty = ty; b.dist = d;
                b.cwx = (float)tx * 512.0f + 256.0f; b.cwy = (float)ty * 512.0f + 256.0f;
                b.repZ = 0.0f; b.anyHead = false;
                buckets.push_back(b);
            } else bi = it->second;
            Bucket& b = buckets[bi];
            b.ents.push_back({ p.kind, p.wz, p.head_z, &p.label });
            // Column height = the tallest head on the tile (NPC box top); players carry none.
            if (p.head_z != 0.0f) { if (!b.anyHead || p.head_z > b.repZ) b.repZ = p.head_z; b.anyHead = true; }
            else if (!b.anyHead && (b.ents.size() == 1 || p.wz > b.repZ)) b.repZ = p.wz;
        }

        // Project each tile's shared anchor (tile centre at the column's head height).
        std::vector<int> keep; keep.reserve(buckets.size());
        for (int i = 0; i < (int)buckets.size(); ++i) {
            Bucket& b = buckets[i];
            float sx, sy;
            if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, b.cwx, b.cwy, b.repZ, sx, sy)) continue;
            if (!b.anyHead) sy -= 26.0f;     // no model box on the tile -> lift off the feet
            if (sx < -120 || sx > W + 120 || sy < -160 || sy > H + 160) continue;
            b.cx = sx; b.cy = sy;
            std::sort(b.ents.begin(), b.ents.end(),
                      [](const Ent& a, const Ent& c) { return *a.label < *c.label; });
            keep.push_back(i);
        }

        // Flatten to individual rows (real names + one "+N more" per over-full tile), each with a
        // desired centre. Nearest tiles + lower rows are placed first so they keep their spot.
        struct Row { float cx, cy, w; int kind; std::string label; int dist, slot; };
        std::vector<Row> rows;
        for (int bi : keep) {
            Bucket& b = buckets[bi];
            int total = (int)b.ents.size();
            int shown = (total > kMaxRows) ? kMaxRows - 1 : total;
            for (int r = 0; r < shown; ++r) {
                const Ent& e = b.ents[r];
                rows.push_back({ b.cx, b.cy - slotH * ((float)r + 0.5f), pillW(*e.label),
                                 e.kind, *e.label, b.dist, r });
            }
            if (total > shown) {
                char buf[24]; std::snprintf(buf, sizeof(buf), "+%d more", total - shown);
                rows.push_back({ b.cx, b.cy - slotH * ((float)shown + 0.5f), pillW(buf),
                                 -1, std::string(buf), b.dist, shown });
            }
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.dist != b.dist ? a.dist < b.dist : a.slot < b.slot;
        });

        // Greedy 2D de-collision: keep the desired X, push the row UP past any placed pill it overlaps.
        // Same-tile rows share X so they stack; adjacent columns interleave upward.
        auto kindRGB = [&](int kind, int& r, int& g, int& b) {
            switch (kind) {
                case 2: r = 120; g = 235; b = 140; break;    // player  green
                case 1: r = 245; g = 210; b = 90;  break;    // NPC     yellow
                case 0: r = 120; g = 215; b = 245; break;    // object  cyan
                default: r = 205; g = 205; b = 210; break;   // overflow / neutral
            }
        };
        struct Rect { float cx, cy, w, h; };
        std::vector<Rect> placed; placed.reserve(rows.size());
        for (auto& row : rows) {
            float cy = row.cy;
            for (int guard = 0; guard < 400; ++guard) {
                float bestTop = 1e30f; bool hit = false;
                for (const auto& pr : placed) {
                    bool ovX = std::abs(pr.cx - row.cx) < (pr.w + row.w) * 0.5f;
                    bool ovY = std::abs(pr.cy - cy) < (pr.h + pillH) * 0.5f + gap;
                    if (ovX && ovY) { hit = true; float top = pr.cy - pr.h * 0.5f; if (top < bestTop) bestTop = top; }
                }
                if (!hit) break;
                cy = bestTop - pillH * 0.5f - gap;           // jump just above the highest collider
            }
            // Rows are placed nearest-first, so a label that had to rise past kMaxRise is a FAR one
            // buried under a dense cluster -- drop it instead of building a tall disconnected tower.
            // Sparse scenes never hit this; only crowded hubs thin out, keeping the nearest.
            if (row.cy - cy > kMaxRise) continue;
            placed.push_back({ row.cx, cy, row.w, pillH });
            int r, g, b; kindRGB(row.kind, r, g, b);
            marker::Command t{}; t.type = marker::kText;
            t.x0 = row.cx; t.y0 = cy; t.x1 = npTextPx;
            t.r = (std::uint8_t)r; t.g = (std::uint8_t)g; t.b = (std::uint8_t)b; t.a = 235;
            int n = (int)row.label.size();
            if (n > marker::kTextMax) {
                std::memcpy(t.text, row.label.data(), marker::kTextMax - 2);
                t.text[marker::kTextMax - 2] = '.'; t.text[marker::kTextMax - 1] = '.';
                t.text[marker::kTextMax] = '\0';
            } else {
                std::memcpy(t.text, row.label.data(), (size_t)n);
                t.text[n] = '\0';
            }
            push(t);
        }
    }

    // --- highlighted NPCs (independent of the grid/NPC toggles) ---
    //   label EMPTY + has_box3d (Scene-tab outline) -> teal model box only;
    //   labelled (guide/alert NPC) -> tile under the NPC (gold, follows them) +
    //   gold model box + compact label panel above the head.
    if (f && !f->highlights.empty()) {
        float pulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1000) / 1000.0 * 6.2831853));
        auto box3d = [&](const rtx::reader::OverlayPoint& hp, float th, int r, int g, int b, int a) {
            float ex[2] = { hp.bmin[0], hp.bmax[0] };
            float ny[2] = { hp.bmin[1], hp.bmax[1] };
            float uz[2] = { hp.bmin[2], hp.bmax[2] };
            float vx[8], vy[8];
            for (int cc = 0; cc < 8; ++cc) {
                float sx, sy;
                if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH,
                                   ex[cc & 1], ny[(cc >> 1) & 1], uz[(cc >> 2) & 1], sx, sy) ||
                    sx < -3.0f * W || sx > 4.0f * W || sy < -3.0f * H || sy > 4.0f * H) return false;
                vx[cc] = sx; vy[cc] = sy;
            }
            static const int E[12][2] = { {0,1},{1,3},{3,2},{2,0},
                                          {4,5},{5,7},{7,6},{6,4},
                                          {0,4},{1,5},{2,6},{3,7} };
            for (auto& e : E)
                line(vx[e[0]], vy[e[0]], vx[e[1]], vy[e[1]], th, r, g, b, a);
            return true;
        };
        for (const auto& hp : f->highlights) {
            // NEUTRAL, not an accent: an inspected entity is not an objective, and the two must
            // never be confusable. Discriminated on an EMPTY label -- do not give this path a
            // default caption or it silently reroutes into the objective style below.
            if (hp.has_box3d && hp.label.empty()) {       // Scene-tab outline
                if (box3d(hp, 1.4f, kTxtR, kTxtG, kTxtB, 190)) continue;
            }
            float sx, sy;
            if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, hp.wx, hp.wy, hp.wz, sx, sy)) continue;
            if (sx < -60 || sx > W + 60 || sy < -60 || sy > H + 60) continue;
            // A labelled highlight IS the objective (guide/alert NPC), so it takes the same
            // accent as the guide box. The pulsing square is only the fallback when no live
            // AABB is known -- pulse stays here because it is a "look here" cue, but it is
            // tightened so it reads as a beacon rather than a throb.
            const bool boxed = hp.has_box3d && box3d(hp, 1.6f, kAccR, kAccG, kAccB, 235);
            if (!boxed) {
                float rad = 14.0f + 3.0f * pulse;
                line(sx - rad, sy - rad, sx + rad, sy - rad, 2.4f, kAccR, kAccG, kAccB, 255);
                line(sx + rad, sy - rad, sx + rad, sy + rad, 2.4f, kAccR, kAccG, kAccB, 255);
                line(sx + rad, sy + rad, sx - rad, sy + rad, 2.4f, kAccR, kAccG, kAccB, 255);
                line(sx - rad, sy + rad, sx - rad, sy - rad, 2.4f, kAccR, kAccG, kAccB, 255);
                dot(sx, sy, 2.5f, kAccR, kAccG, kAccB, 235);
            }
            // label above the head (model-box top when known); one kText command,
            // '\n' renders module-side as a single multi-line panel
            if (!hp.label.empty()) {
                float lx = sx, ly = sy - 21.0f;
                if (hp.head_z != 0.0f) {
                    float hx, hy;
                    if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, hp.wx, hp.wy, hp.head_z, hx, hy)) {
                        lx = hx; ly = hy;
                    }
                }
                int nl = 1;
                for (char ch : hp.label) if (ch == '\n') ++nl;
                if (nl > 4) nl = 4;
                marker::Command t{}; t.type = marker::kText;
                const float estH = 13.0f * (float)nl + 10.0f;   // rough panel height (padY 5, mirrors DrawLabel)
                t.x0 = lx; t.y0 = ly - 6.0f - estH * 0.5f;
                t.x1 = 10.5f;                                   // compact: stay out of the way
                t.r = kAccR; t.g = kAccG; t.b = kAccB; t.a = 230;              // match the box: OBJECTIVE accent
                int n = (int)hp.label.size();
                if (n > marker::kTextMax) {
                    std::memcpy(t.text, hp.label.data(), marker::kTextMax - 2);
                    t.text[marker::kTextMax - 2] = '.';
                    t.text[marker::kTextMax - 1] = '.';
                    t.text[marker::kTextMax]     = '\0';
                } else {
                    std::memcpy(t.text, hp.label.data(), (size_t)n);
                    t.text[n] = '\0';
                }
                push(t);
            }
        }
    }

    // --- user tile markers: persistent region+local markers projected as a filled tile +
    //     outline with an optional floating label. Player's plane only; terrain-followed. ---
    if (f && cfg.markers) {
        auto mlist = rtx::markers::Snapshot(cfg.pid);
        const std::int16_t kNo = -32768;
        for (const auto& m : mlist) {
            if (m.plane != f->plane) continue;
            int gx = ((m.region >> 8) << 6) + m.lx;
            int gy = ((m.region & 0xFF) << 6) + m.ly;
            int adx = gx - f->player_tx; if (adx < 0) adx = -adx;
            int ady = gy - f->player_ty; if (ady < 0) ady = -ady;
            if ((adx > ady ? adx : ady) > 128) continue;     // keep projection sane / uncluttered
            int cr = (m.color >> 16) & 0xFF, cg = (m.color >> 8) & 0xFF, cb = m.color & 0xFF;
            // per-tile corner heights (one bridge decision per tile: no morphing at seams)
            std::int16_t ch[4];
            rtx::cache::TileCornerHeights(gx, gy, f->plane, ch);
            auto cz = [&](int c) { return (ch[c] == kNo) ? f->player_z : 32.0f * (float)ch[c]; };
            float zSW = cz(0), zSE = cz(1), zNE = cz(2), zNW = cz(3);
            const float wc[4][3] = {
                { gx * 512.f,       gy * 512.f,       zSW },
                { (gx + 1) * 512.f, gy * 512.f,       zSE },
                { (gx + 1) * 512.f, (gy + 1) * 512.f, zNE },
                { gx * 512.f,       (gy + 1) * 512.f, zNW },
            };
            float qx[9], qy[9];
            int nq = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, wc, 4, qx, qy);
            bool any = false;
            for (int i = 0; i < nq; ++i)
                if (qx[i] > -(float)W && qx[i] < 2.f * W && qy[i] > -(float)H && qy[i] < 2.f * H) any = true;
            if (nq >= 3 && any) {
                // The clipped polygon can carry up to 5 verts; kFillQuad takes 4,
                // so fill as a fan of quads off vert 0 (tail duplicated when odd).
                for (int i = 1; i + 1 < nq; i += 2) {
                    int j = (i + 2 < nq) ? i + 2 : i + 1;
                    marker::Command q{}; q.type = marker::kFillQuad;
                    q.x0 = qx[0]; q.y0 = qy[0]; q.x1 = qx[i]; q.y1 = qy[i];
                    q.x2 = qx[i + 1]; q.y2 = qy[i + 1]; q.x3 = qx[j]; q.y3 = qy[j];
                    // Denser but far darker: a tinted scrim in the user's hue rather than a
                    // coloured wash, so the tile reads as a lit outline instead of a stain.
                    q.r = (std::uint8_t)MixSurf(cr, kSurfR); q.g = (std::uint8_t)MixSurf(cg, kSurfG);
                    q.b = (std::uint8_t)MixSurf(cb, kSurfB); q.a = 110;
                    push(q);
                }
                // No corner ticks here: a single tile's perimeter already IS four short edges,
                // and ticking it would leave nothing. Ticks are for prisms only.
                for (int i = 0; i < nq; ++i) {
                    line(qx[i], qy[i], qx[(i + 1) % nq], qy[(i + 1) % nq], 3.0f, kInkR, kInkG, kInkB, 140);
                    line(qx[i], qy[i], qx[(i + 1) % nq], qy[(i + 1) % nq], 1.6f, cr, cg, cb, 255);   // USER COLOUR, verbatim
                }
            }
            if (!m.label.empty()) {
                float zc = (zSW + zSE + zNE + zNW) * 0.25f;
                float cx, cy;
                if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH,
                                  (gx + 0.5f) * 512.f, (gy + 0.5f) * 512.f, zc, cx, cy) &&
                    cx > -200.f && cx < W + 200.f && cy > -120.f && cy < H + 120.f) {
                    // One kText command: the module measures + lays the string out proportionally
                    // and draws a rounded pill (dark fill + accent border = the tile's colour).
                    marker::Command t{}; t.type = marker::kText;
                    t.x0 = cx;
                    t.y0 = cy - 22.0f;        // float the label above the tile centre
                    t.x1 = 14.0f;             // glyph height in px
                    t.r = (std::uint8_t)cr; t.g = (std::uint8_t)cg; t.b = (std::uint8_t)cb; t.a = 235;
                    int n = (int)m.label.size();
                    if (n > marker::kTextMax) {   // truncate over-long labels with ".."
                        std::memcpy(t.text, m.label.data(), marker::kTextMax - 2);
                        t.text[marker::kTextMax - 2] = '.';
                        t.text[marker::kTextMax - 1] = '.';
                        t.text[marker::kTextMax]     = '\0';
                    } else {
                        std::memcpy(t.text, m.label.data(), (size_t)n);
                        t.text[n] = '\0';
                    }
                    push(t);
                }
            }
        }
    }

    // --- guide path: the BFS walkable route drawn as the actual tiles walked,
    //     a dim gold fill per tile with a thin dark edge for contrast ---
    if (f && f->guide_path.size() >= 4) {
        const std::int16_t kNo = -32768;
        const std::size_t nT = f->guide_path.size() / 2;
        // Pass 1: per-tile corner heights (one bridge decision per tile). Pass 2: weld the
        // shared edge of consecutive path tiles to the average of the two tiles' values, so
        // the ribbon reads as ONE continuous height map (a deck-to-stair becomes a slope).
        std::vector<float> cz((std::size_t)nT * 4);
        for (std::size_t i = 0; i < nT; ++i) {
            std::int16_t ch[4];
            rtx::cache::TileCornerHeights(f->guide_path[i * 2], f->guide_path[i * 2 + 1], f->plane, ch);
            for (int c = 0; c < 4; ++c)
                cz[i * 4 + c] = (ch[c] == kNo) ? f->player_z : 32.0f * (float)ch[c];
        }
        for (std::size_t i = 0; i + 1 < nT; ++i) {
            int dx = f->guide_path[(i + 1) * 2]     - f->guide_path[i * 2];
            int dy = f->guide_path[(i + 1) * 2 + 1] - f->guide_path[i * 2 + 1];
            // corner order SW(0) SE(1) NE(2) NW(3); pairs = (mine, next's) per direction
            int a0, a1, b0, b1;
            if      (dx ==  1) { a0 = 1; b0 = 0; a1 = 2; b1 = 3; }   // east:  own SE/NE = next SW/NW
            else if (dx == -1) { a0 = 0; b0 = 1; a1 = 3; b1 = 2; }   // west:  own SW/NW = next SE/NE
            else if (dy ==  1) { a0 = 3; b0 = 0; a1 = 2; b1 = 1; }   // north: own NW/NE = next SW/SE
            else if (dy == -1) { a0 = 0; b0 = 3; a1 = 1; b1 = 2; }   // south: own SW/SE = next NW/NE
            else continue;
            float w0 = (cz[i * 4 + a0] + cz[(i + 1) * 4 + b0]) * 0.5f;
            float w1 = (cz[i * 4 + a1] + cz[(i + 1) * 4 + b1]) * 0.5f;
            cz[i * 4 + a0] = cz[(i + 1) * 4 + b0] = w0;
            cz[i * 4 + a1] = cz[(i + 1) * 4 + b1] = w1;
        }
        for (std::size_t i = 0; i < nT; ++i) {
            int gx = f->guide_path[i * 2], gy = f->guide_path[i * 2 + 1];
            static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
            float wcp[4][3];
            for (int c = 0; c < 4; ++c) {
                wcp[c][0] = (gx + CX[c]) * 512.f;
                wcp[c][1] = (gy + CY[c]) * 512.f;
                wcp[c][2] = cz[i * 4 + c];
            }
            float qx[9], qy[9];
            int nq = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, wcp, 4, qx, qy);
            bool onscr = false;
            for (int c = 0; c < nq; ++c)
                if (qx[c] > -200 && qx[c] < W + 200 && qy[c] > -200 && qy[c] < H + 200) onscr = true;
            if (nq < 3 || !onscr) continue;
            for (int c = 1; c + 1 < nq; c += 2) {        // fan fill (see tile markers)
                int j = (c + 2 < nq) ? c + 2 : c + 1;
                marker::Command q{}; q.type = marker::kFillQuad;
                q.x0 = qx[0]; q.y0 = qy[0]; q.x1 = qx[c]; q.y1 = qy[c];
                q.x2 = qx[c + 1]; q.y2 = qy[c + 1]; q.x3 = qx[j]; q.y3 = qy[j];
                q.r = 255; q.g = 215; q.b = 60; q.a = 70;
                push(q);
            }
            for (int c = 0; c < nq; ++c)                 // dark edge: contrast on sand
                line(qx[c], qy[c], qx[(c + 1) % nq], qy[(c + 1) % nq], 1.6f, 30, 24, 8, 130);
        }
    }

    // --- mystery guide marks: resolved guide sites from the frame build -- the named object's
    //     footprint prism with the label above its top, or a flat tile when no cache loc matched.
    //     Labels are collected first and placed in a declutter pass below (co-located marks
    //     otherwise stack their pills on top of each other). ---
    struct GuideLbl { float cx, cy; int nl, mc; const std::string* text; int r, g, b; bool hazard; };
    std::vector<GuideLbl> glbls;
    if (f) for (const auto& p : f->guides) {
        // World corners: base ring 0-3 (SW,SE,NE,NW), top ring 4-7. Each edge and the base
        // fill are near-plane clipped individually, so walking INTO a mark (or tilting the
        // camera across it) trims the shape instead of blinking it out.
        float wc[8][3]; int nv;
        if (p.has_box3d) {
            // live model AABB: the object's true visual volume
            nv = 8;
            static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
            for (int i = 0; i < 8; ++i) {
                wc[i][0] = CX[i & 3] ? p.bmax[0] : p.bmin[0];
                wc[i][1] = CY[i & 3] ? p.bmax[1] : p.bmin[1];
                wc[i][2] = (i >> 2) ? p.bmax[2] : p.bmin[2];
            }
        } else {
            nv = (p.box_h > 0.f) ? 8 : 4;                // corners 0-3 ground, 4-7 top
            for (int i = 0; i < nv; ++i) {
                wc[i][0] = p.box[(i & 3) * 3];
                wc[i][1] = p.box[(i & 3) * 3 + 1];
                wc[i][2] = p.box[(i & 3) * 3 + 2] + ((i & 4) ? p.box_h : 0.f);
            }
        }
        // Per-mark colour: rgb 0 = the default OBJECTIVE accent (single source of truth);
        // hazard/area marks carry their own 0xRRGGBB (BGH: red bones, green/yellow grass).
        // A caller's hue is passed through UNCHANGED -- rgb is load-bearing beyond paint
        // (it gates the direction arrow and drives the declutter hazard priority).
        const int mr = p.rgb ? ((p.rgb >> 16) & 255) : kAccR;
        const int mg = p.rgb ? ((p.rgb >> 8) & 255)  : kAccG;
        const int mb = p.rgb ? (p.rgb & 255)         : kAccB;
        float qx[9], qy[9];
        int nq = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, wc, 4, qx, qy);
        bool onscr = false;
        for (int i = 0; i < nq; ++i)
            if (qx[i] > -2.f * W && qx[i] < 3.f * W && qy[i] > -2.f * H && qy[i] < 3.f * H) onscr = true;
        if (nq >= 3 && onscr) {
            // Dark TINTED scrim rather than a coloured wash: the ground under a marker is
            // whatever the scene happens to be, so a saturated fill washes out on bright
            // terrain and the object we are pointing at fights its own highlight.
            const int sr = MixSurf(mr, kSurfR), sg = MixSurf(mg, kSurfG), sb = MixSurf(mb, kSurfB);
            for (int i = 1; i + 1 < nq; i += 2) {        // fan fill (see tile markers)
                int j = (i + 2 < nq) ? i + 2 : i + 1;
                marker::Command q{}; q.type = marker::kFillQuad;
                q.x0 = qx[0]; q.y0 = qy[0]; q.x1 = qx[i]; q.y1 = qy[i];
                q.x2 = qx[i + 1]; q.y2 = qy[i + 1]; q.x3 = qx[j]; q.y3 = qy[j];
                q.r = (std::uint8_t)sr; q.g = (std::uint8_t)sg; q.b = (std::uint8_t)sb; q.a = 96;
                push(q);
            }
        }
        // Two techniques carry the legibility here, both forced by what the renderer can do:
        // there is no anti-aliasing and no depth test, so contrast has to be BUILT rather
        // than sampled. (1) every silhouette stroke gets a dark CONTOUR underneath it, the
        // same trick the guide path already uses to stay readable on sand. (2) `ticks` draws
        // only the ENDS of an edge, so a prism reads as corner brackets and the object shows
        // through its own outline instead of being caged by twelve opaque lines.
        auto edgeLine = [&](int i0, int i1, float th, int alpha, bool contour, bool ticks) {
            float ax, ay, bx, by;
            if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH,
                                    wc[i0], wc[i1], ax, ay, bx, by)) return;
            if ((ax < -2.f * W || ax > 3.f * W || ay < -2.f * H || ay > 3.f * H) &&
                (bx < -2.f * W || bx > 3.f * W || by < -2.f * H || by > 3.f * H)) return;
            const float dx = bx - ax, dy = by - ay;
            const float len = std::sqrt(dx * dx + dy * dy);
            float f0 = 1.0f;
            if (ticks && len >= 26.0f) {              // short edges: the whole edge IS the tick
                f0 = 14.0f / len;                     // ~14px minimum bracket
                if (f0 < 0.18f) f0 = 0.18f;
                if (f0 > 0.38f) f0 = 0.38f;           // never more than ~a third of the edge
            }
            auto seg = [&](float t0, float t1) {
                const float x0 = ax + dx * t0, y0 = ay + dy * t0;
                const float x1 = ax + dx * t1, y1 = ay + dy * t1;
                if (contour) line(x0, y0, x1, y1, th + 1.6f, kInkR, kInkG, kInkB, 140);
                line(x0, y0, x1, y1, th, mr, mg, mb, alpha);
            };
            if (f0 >= 0.999f) { seg(0.0f, 1.0f); return; }
            // Corners alone lose the SHAPE on a long thin prism -- the two ends stop reading
            // as one object. A faint full-length edge underneath keeps the silhouette legible
            // while the brackets carry the emphasis and still let the model show through.
            line(ax, ay, bx, by, th * 0.75f, mr, mg, mb, alpha / 4);
            seg(0.0f, f0); seg(1.0f - f0, 1.0f);
        };
        // Prisms tick; flat zone tiles must NOT, because edge_mask merges them into one shape
        // and a zone's continuous perimeter is precisely the information it carries.
        const bool tick = (nv == 8);
        for (int i = 0; i < 4; ++i) {
            // Flat region tiles mask off edges shared with a neighbouring tile of the same
            // zone (bit i: 0 south, 1 east, 2 north, 3 west -- corner order SW,SE,NE,NW), so
            // the union outlines as ONE shape. Prisms always draw every edge.
            if (nv == 4 && !((p.edge_mask >> i) & 1)) continue;
            edgeLine(i, (i + 1) & 3, 2.2f, 245, true, tick);              // base ring: silhouette
            if (nv == 8) {
                edgeLine(4 + i, 4 + ((i + 1) & 3), 1.9f, 215, true, tick);   // top ring
                // Verticals draw WHOLE, never ticked: they are short, they are what joins the
                // two rings into a solid, and bracketing them just scattered loose fragments.
                edgeLine(i, 4 + i, 1.5f, 140, false, false);                 // depth cue
            }
        }
        if (p.label.empty()) continue;
        // anchor the label off the projected IN-FRONT corners (robust for both AABB
        // and prism shapes): centred horizontally, just above the box's screen top
        float cx = 0.f, cy = 0.f; int np = 0;
        for (int i = 0; i < nv; ++i) {
            float sxx, syy;
            if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH,
                               wc[i][0], wc[i][1], wc[i][2], sxx, syy)) continue;
            cx += sxx;
            if (!np || syy < cy) cy = syy;
            ++np;
        }
        if (!np) continue;
        cx /= (float)np;
        if (cx < -200.f || cx > W + 200.f || cy < -120.f || cy > H + 120.f) continue;
        int nl = 1, mc = 0, run = 0;
        for (char ch : p.label) { if (ch == '\n') { ++nl; run = 0; } else if (++run > mc) mc = run; }
        if (nl > 4) nl = 4;
        glbls.push_back({ cx, cy, nl, mc, &p.label, mr, mg, mb, p.rgb != 0 });
    }

    // Label DECLUTTER: marks that sit together would stack their pills unreadably. Objective
    // (default-colour) labels place first and keep their natural spot; every later pill is nudged
    // straight UP until it clears what's already placed. Same-text pills anchored within ~70 px
    // collapse into one. Sizes are estimates, but good enough for collision.
    if (!glbls.empty()) {
        std::stable_sort(glbls.begin(), glbls.end(),
                         [](const GuideLbl& a, const GuideLbl& b){ return !a.hazard && b.hazard; });
        struct LblRect { float l, t, r, b; };
        std::vector<LblRect> placedR;
        std::vector<const GuideLbl*> placedL;
        for (const auto& l : glbls) {
            bool dup = false;
            for (const GuideLbl* d : placedL)
                if (*d->text == *l.text && std::fabs(d->cx - l.cx) < 70.f && std::fabs(d->cy - l.cy) < 70.f) { dup = true; break; }
            if (dup) continue;
            placedL.push_back(&l);
            const float estH = 13.0f * (float)l.nl + 10.0f;           // padY 5, mirrors DrawLabel
            const float estW = 0.62f * 10.5f * (float)l.mc + 24.0f;   // proportional-font estimate + pill padding (padX 8)
            float y = l.cy - 8.0f - estH * 0.5f;
            for (int guard = 0; guard < 12; ++guard) {                // shift up past each collision
                bool hit = false;
                for (const auto& r : placedR)
                    if (l.cx - estW * 0.5f < r.r && l.cx + estW * 0.5f > r.l &&
                        y - estH * 0.5f < r.b && y + estH * 0.5f > r.t) { y = r.t - estH * 0.5f - 4.0f; hit = true; break; }
                if (!hit) break;
            }
            placedR.push_back({ l.cx - estW * 0.5f, y - estH * 0.5f, l.cx + estW * 0.5f, y + estH * 0.5f });
            marker::Command t{}; t.type = marker::kText;
            t.x0 = l.cx; t.y0 = y;
            t.x1 = 10.5f;                                    // compact: stay out of the way
            t.r = (std::uint8_t)l.r; t.g = (std::uint8_t)l.g; t.b = (std::uint8_t)l.b; t.a = 230;
            int n = (int)l.text->size();
            if (n > marker::kTextMax) {
                std::memcpy(t.text, l.text->data(), marker::kTextMax - 2);
                t.text[marker::kTextMax - 2] = '.';
                t.text[marker::kTextMax - 1] = '.';
                t.text[marker::kTextMax]     = '\0';
            } else {
                std::memcpy(t.text, l.text->data(), (size_t)n);
                t.text[n] = '\0';
            }
            push(t);
        }
    }

    // --- screen-centre text (e.g. the BGH "Bound - 2s" stun countdown): one big kText
    //     pill above the gameview centre, no world projection. Red accent = warning. ---
    if (!ctext.empty()) {
        marker::Command t{}; t.type = marker::kText;
        t.x0 = vpX + vpW * 0.5f;
        t.y0 = vpY + vpH * 0.35f;                        // above centre: clear of the player model
        t.x1 = 22.0f;                                    // big: must read at a glance mid-fight
        t.r = 235; t.g = 90; t.b = 90; t.a = 245;
        int n = (int)ctext.size();
        if (n > marker::kTextMax) n = marker::kTextMax;
        std::memcpy(t.text, ctext.data(), (size_t)n);
        t.text[n] = '\0';
        push(t);
    }


    // --- direction indicator: a marker ring on the ground UNDER THE PLAYER, with a sleek arrowhead
    //     on its edge pointing toward the objective. Centred on the player's FINE (sub-tile) world
    //     position, so it never jumps tile-to-tile as the player walks. Drawn in-world. ---
    // Within 2 tiles the objective's own box/label is the cue, so the whole cue needs >= 3 tiles.
    int arrowDist = 0;
    if (f && f->has_arrow) {
        int adx = f->arrow_tx - f->player_tx, ady = f->arrow_ty - f->player_ty;
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        arrowDist = adx > ady ? adx : ady;
    }
    if (f && f->has_arrow && arrowDist >= 3) {
        const float* m = f->matrix;
        const float z = f->player_z;
        const float cwx = f->player_fx, cwy = f->player_fy;   // ring centre = under the player (smooth fine pos)
        // The cue is deliberately FLAT, drawn on the player's own ground plane.
        //
        // Terrain-following was tried and removed. Sampling the ground per vertex is correct on
        // open hillside and wrong on everything this cue actually appears over: stairs, wall
        // tops, ledges and bridges all change height by a full step INSIDE the ring's 0.59-tile
        // radius, so the dashes scatter to different heights and the circle reads as debris.
        // The arrow was worse -- its tip sits ~0.84 tile out, so on a descending flight it
        // landed a step below its own wings and projection sheared the dart across the screen.
        // Clamping the deviation, and adaptively draping only over smooth ground, each fixed one
        // case and left the others; every guard added complexity for a benefit nobody had asked
        // for. A flat cue may clip a step, which reads as a cue drawn on the ground. A torn one
        // reads as a bug. Keep it flat.
        {
            float ccsx, ccsy;
            if (WorldToScreen(m, vpX, vpY, vpW, vpH, cwx, cwy, z, ccsx, ccsy)) {   // player is in front of camera
                const float twx = f->arrow_tx * 512.f + 256.f, twy = f->arrow_ty * 512.f + 256.f;
                float wdx = twx - cwx, wdy = twy - cwy;
                float wlen = std::sqrt(wdx * wdx + wdy * wdy);
                if (wlen > 1.0f) {
                    wdx /= wlen; wdy /= wlen;
                    const float perpx = -wdy, perpy = wdx;
                    // Pulled in from 340: with the arrow base pushed out (below) this opens a
                    // real gap between ring and dart instead of welding them into one blob.
                    const float R = 300.f;                  // ring radius in world-fine units (~0.59 tile)
                    // Lowest on-screen point of the whole cue (ring + arrow): the distance pill anchors
                    // just below it. Measuring the real projected extent keeps the gap constant at any
                    // camera pitch/yaw/zoom.
                    float cueMaxY = ccsy + 34.0f;           // fallback when no cue point projects
                    // thin, subtle ground ring
                    const int NSEG = 32;
                    float prevX = 0, prevY = 0; bool prevOk = false;
                    for (int i = 0; i <= NSEG; ++i) {
                        double a = (double)i / NSEG * 6.28318530717959;
                        float wx = cwx + (float)std::cos(a) * R, wy = cwy + (float)std::sin(a) * R;
                        float sx, sy; bool ok = WorldToScreen(m, vpX, vpY, vpW, vpH, wx, wy, z, sx, sy);
                        // DASHED, and that is structural rather than decorative: the ring is 32
                        // independent butt-ended quads with no joins, so a solid stroke notches
                        // visibly at every seam. Emitting alternate segments turns the seam into
                        // the design and halves the segment count, which pays for the dark
                        // contour underneath -- needed because the ring sits on whatever ground
                        // the player happens to be standing on.
                        if (ok && prevOk && (i & 1)) {
                            line(prevX, prevY, sx, sy, 3.2f, kInkR, kInkG, kInkB, 130);
                            line(prevX, prevY, sx, sy, 1.8f, kOkR, kOkG, kOkB, 225);
                        }
                        if (ok && sy > cueMaxY) cueMaxY = sy;
                        prevX = sx; prevY = sy; prevOk = ok;
                    }
                    // Arrowhead just past the ring, pointing at the objective. Local coords = (along-heading,
                    // sideways), projected to the ground; filled as two triangles so the back notch stays open.
                    // Same plane as the ring: the dart is a rigid POINTER, and its proportions
                    // must not depend on what the ground happens to do 0.84 tile ahead.
                    auto W2S = [&](float along, float side, float& sx, float& sy) {
                        return WorldToScreen(m, vpX, vpY, vpW, vpH,
                                             cwx + wdx * along + perpx * side,
                                             cwy + wdy * along + perpy * side, z, sx, sy);
                    };
                    float Tx, Ty, Lx, Ly, Nx, Ny, Rx, Ry;
                    // Base pushed from R+60 to R+150 (0.29 tile clear of the ring) so the dart
                    // reads as a separate pointer rather than a spur welded onto the circle.
                    if (W2S(R + 430.f,   0.f, Tx, Ty) &&    // tip (longer / more visible)
                        W2S(R + 150.f,  96.f, Lx, Ly) &&    // left wing (widest, at the back)
                        W2S(R + 275.f,   0.f, Nx, Ny) &&    // back notch (forward of the wings -> concave)
                        W2S(R + 150.f, -96.f, Rx, Ry)) {    // right wing
                        marker::Command q1{}; q1.type = marker::kFillQuad;
                        q1.x0 = Tx; q1.y0 = Ty; q1.x1 = Lx; q1.y1 = Ly; q1.x2 = Nx; q1.y2 = Ny; q1.x3 = Tx; q1.y3 = Ty;
                        // Painter's algorithm IS the mechanism here (no depth test): lay the dark
                        // contour down FIRST so the fills cover its inner half and only an outer
                        // rim survives, then the bright edge last.
                        line(Tx, Ty, Lx, Ly, 3.0f, kInkR, kInkG, kInkB, 170);
                        line(Lx, Ly, Nx, Ny, 3.0f, kInkR, kInkG, kInkB, 170);
                        line(Nx, Ny, Rx, Ry, 3.0f, kInkR, kInkG, kInkB, 170);
                        line(Rx, Ry, Tx, Ty, 3.0f, kInkR, kInkG, kInkB, 170);
                        q1.r = kOkR; q1.g = kOkG; q1.b = kOkB; q1.a = 235; push(q1);
                        marker::Command q2{}; q2.type = marker::kFillQuad;
                        q2.x0 = Tx; q2.y0 = Ty; q2.x1 = Nx; q2.y1 = Ny; q2.x2 = Rx; q2.y2 = Ry; q2.x3 = Tx; q2.y3 = Ty;
                        q2.r = kOkR; q2.g = kOkG; q2.b = kOkB; q2.a = 235; push(q2);
                        line(Tx, Ty, Lx, Ly, 1.3f, kOkR, kOkG, kOkB, 255);   // crisp outline: tip -> L -> notch -> R -> tip
                        line(Lx, Ly, Nx, Ny, 1.3f, kOkR, kOkG, kOkB, 255);
                        line(Nx, Ny, Rx, Ry, 1.3f, kOkR, kOkG, kOkB, 255);
                        line(Rx, Ry, Tx, Ty, 1.3f, kOkR, kOkG, kOkB, 255);
                        // The arrow can extend below the ring when the objective is screen-down;
                        // include it so the pill clears the whole cue.
                        if (Ty > cueMaxY) cueMaxY = Ty;
                        if (Ly > cueMaxY) cueMaxY = Ly;
                        if (Ry > cueMaxY) cueMaxY = Ry;
                    }
                    // distance readout under the ring: tiles to the objective (Chebyshev, the game's
                    // interaction metric). Anchored a fixed 12px below the cue's lowest projected
                    // point, so the gap never changes with camera pitch/yaw/zoom.
                    const int dist = arrowDist;
                    {
                        // Composed badge rather than a kText pill, because this is the one label
                        // that wants COLOURED type and kText hardcodes its glyph colour module
                        // side. Three commands gets the World Map badge exactly: dark plate,
                        // accent hairline, accent text. Short fixed string, so the 0.62 advance
                        // over-estimate is a couple of px and always errs toward not cropping.
                        char dtxt[24];
                        std::snprintf(dtxt, sizeof(dtxt), "%d tile%s", dist, dist == 1 ? "" : "s");
                        int dn = 0; for (const char* q = dtxt; *q; ++q) ++dn;
                        const float dpx = 10.5f;
                        const float dw = (float)dn * dpx * 0.62f + 16.0f, dh = 19.0f;
                        const float dx = ccsx - dw * 0.5f, dy = cueMaxY + 12.0f;
                        { marker::Command c{}; c.type = marker::kRoundFill;
                          c.x0 = dx - 1.0f; c.y0 = dy - 1.0f; c.x1 = dw + 2.0f; c.y1 = dh + 2.0f;
                          c.thickness = 5.0f;
                          c.r = kOkR; c.g = kOkG; c.b = kOkB; c.a = 190; push(c); }        // hairline
                        { marker::Command c{}; c.type = marker::kRoundFill;
                          c.x0 = dx; c.y0 = dy; c.x1 = dw; c.y1 = dh; c.thickness = 4.0f;
                          c.r = kSurfR; c.g = kSurfG; c.b = kSurfB; c.a = 235; push(c); }  // #0B0D12
                        { marker::Command c{}; c.type = marker::kText;
                          c.glyph = (std::uint16_t)(marker::kTextPlain | marker::kTextAlignCentre);
                          c.x0 = ccsx; c.y0 = dy + dh * 0.5f; c.x1 = dpx;
                          c.r = kOkTxR; c.g = kOkTxG; c.b = kOkTxB; c.a = 255;
                          std::snprintf(c.text, sizeof(c.text), "%s", dtxt); push(c); }
                    }
                }
            }
        }
    }

    // RS3 UI scale: interface coords are in 800x600 DESIGN SPACE. At/above an 800x600 window the UI is
    // 1:1 (scale 1.0); below it the engine keeps design coords and renders downscaled by
    // min(w/800, h/600). Overlays draw in real pixels, so multiply design-space UI coords by this.
    //
    // NOT the reader's derived ui_scale. Every rect that reaches this function through
    // iface_panel_origin already carries a PIXEL panel origin (the position varcs) plus
    // tree-relative offsets, so scaling the whole thing multiplies the origin too and
    // throws it off-screen -- observed live at 150% Interface Scaling. Getting that right
    // means scaling only the tree-relative part, which has to happen where the two are
    // still separate: the reader publishes the factor as InterfaceGroupJson's "ui" field
    // and the skills XP bars (client.html skBarsTick) apply it to their pure-tree anchor
    // math. The iface_panel_origin-based rects below still await that per-path work.
    float uiScale;
    { float sx = (float)W / 800.0f, sy = (float)H / 600.0f; uiScale = sx < sy ? sx : sy; if (uiScale > 1.0f) uiScale = 1.0f; }

    // --- UI highlight: a screen-space accent box over a game UI element. Coords are already client
    //     pixels (panel-position varc + within-group offset, x uiScale), so NO world projection.
    //     Pulses so it reads as a "click here" cue. A caller may set SEVERAL at once
    //     (SetUiHighlights); they all draw the same way, in the order given. ---
    for (const auto& uihl : uihls) {
        if (uihl.w <= 0 || uihl.h <= 0) continue;
        float uipulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1000) / 1000.0 * 6.2831853));
        // A small target (a puzzle-box CELL, ~49px) INSETS so the highlight clearly sits inside that one
        // tile and never bleeds into its neighbours; a larger box (a dialogue option) keeps a slight outset.
        const bool cell = (uihl.w * uiScale < 90.0f && uihl.h * uiScale < 90.0f);
        float pad = cell ? -3.5f : 3.0f;
        float x0 = (float)uihl.x * uiScale - pad, y0 = (float)uihl.y * uiScale - pad;
        float x1 = (float)(uihl.x + uihl.w) * uiScale + pad, y1 = (float)(uihl.y + uihl.h) * uiScale + pad;
        // Translucent fill -- stronger on a cell (no text to keep readable).
        marker::Command q{}; q.type = marker::kFillRect;
        q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
        q.r = kAccR; q.g = kAccG; q.b = kAccB;
        q.a = (std::uint8_t)((cell ? 105 : 58) + (int)((cell ? 70.0f : 46.0f) * uipulse));
        push(q);
        // Soft outer glow (wider, lower alpha) just OUTSIDE the border, so the text inside isn't darkened.
        float g = 2.5f, gx0 = x0 - g, gy0 = y0 - g, gx1 = x1 + g, gy1 = y1 + g;
        int ga = 80 + (int)(45.0f * uipulse);
        line(gx0, gy0, gx1, gy0, 4.5f, kAccR, kAccG, kAccB, ga);
        line(gx1, gy0, gx1, gy1, 4.5f, kAccR, kAccG, kAccB, ga);
        line(gx1, gy1, gx0, gy1, 4.5f, kAccR, kAccG, kAccB, ga);
        line(gx0, gy1, gx0, gy0, 4.5f, kAccR, kAccG, kAccB, ga);
        line(x0, y0, x1, y0, 3.4f, kAccR, kAccG, kAccB, 255);
        line(x1, y0, x1, y1, 3.4f, kAccR, kAccG, kAccB, 255);
        line(x1, y1, x0, y1, 3.4f, kAccR, kAccG, kAccB, 255);
        line(x0, y1, x0, y0, 3.4f, kAccR, kAccG, kAccB, 255);
        // Corner brackets: disambiguate a single grid cell.
        if (cell) {
            float L = (x1 - x0) * 0.34f;
            const float bw = 4.2f; const int ba2 = 255;
            line(x0, y0, x0 + L, y0, bw, 255, 255, 255, ba2); line(x0, y0, x0, y0 + L, bw, 255, 255, 255, ba2);
            line(x1, y0, x1 - L, y0, bw, 255, 255, 255, ba2); line(x1, y0, x1, y0 + L, bw, 255, 255, 255, ba2);
            line(x0, y1, x0 + L, y1, bw, 255, 255, 255, ba2); line(x0, y1, x0, y1 - L, bw, 255, 255, 255, ba2);
            line(x1, y1, x1 - L, y1, bw, 255, 255, 255, ba2); line(x1, y1, x1, y1 - L, bw, 255, 255, 255, ba2);
        }
    }

    // --- Puzzle-box next moves: bold GOLD numbered cells on the live board so the click order is obvious in
    //     advance. step 0 = click now (brightest + pulsing + thickest border), 1/2 = upcoming. Coords are
    //     already screen pixels (puzzleCellRects), so NO uiScale -- draw directly. ---
    if (!pcells.empty()) {
        float ppulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 900) / 900.0 * 6.2831853));
        for (const auto& pc : pcells) {
            if (pc.w <= 0 || pc.h <= 0) continue;
            float x0 = (float)pc.x, y0 = (float)pc.y, x1 = (float)(pc.x + pc.w), y1 = (float)(pc.y + pc.h);
            const int R = 255, G = 196, B = 64;                    // gold (matches the side-panel hot tile)
            int fillA, bordA; float bordTh;
            if (pc.step == 0) { fillA = 85 + (int)(80.0f * ppulse); bordTh = 4.0f; bordA = 255; }   // next: pulsing, boldest
            else if (pc.step == 1) { fillA = 60; bordTh = 3.0f; bordA = 225; }
            else if (pc.step == 2) { fillA = 44; bordTh = 2.5f; bordA = 190; }
            else { fillA = 32; bordTh = 2.0f; bordA = 155; }                                        // step 3 (and beyond): faintest
            marker::Command q{}; q.type = marker::kFillRect; q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
            q.r = (std::uint8_t)R; q.g = (std::uint8_t)G; q.b = (std::uint8_t)B; q.a = (std::uint8_t)fillA; push(q);
            // gold border drawn INSIDE the cell edge (no outward bleed -> never touches a neighbouring cell),
            // plus a thin dark line just inside it so it stays crisp over bright tile art.
            float in = bordTh * 0.5f, bx0 = x0 + in, by0 = y0 + in, bx1 = x1 - in, by1 = y1 - in;
            line(bx0, by0, bx1, by0, bordTh, R, G, B, bordA); line(bx1, by0, bx1, by1, bordTh, R, G, B, bordA);
            line(bx1, by1, bx0, by1, bordTh, R, G, B, bordA); line(bx0, by1, bx0, by0, bordTh, R, G, B, bordA);
            float dx0 = x0 + bordTh + 1.0f, dy0 = y0 + bordTh + 1.0f, dx1 = x1 - bordTh - 1.0f, dy1 = y1 - bordTh - 1.0f;
            line(dx0, dy0, dx1, dy0, 1.4f, 0, 0, 0, 130); line(dx1, dy0, dx1, dy1, 1.4f, 0, 0, 0, 130);
            line(dx1, dy1, dx0, dy1, 1.4f, 0, 0, 0, 130); line(dx0, dy1, dx0, dy0, 1.4f, 0, 0, 0, 130);
            // big step number, centred (dark on the bright next cell, gold on the dimmer ones)
            marker::Command t{}; t.type = marker::kText;
            float gh = (y1 - y0) * 0.5f; if (gh > 24.0f) gh = 24.0f; if (gh < 10.0f) gh = 10.0f;   // glyph height scales to the cell: caps at 24 (big slider tiles), shrinks for small lockbox tiles
            t.x0 = (x0 + x1) * 0.5f; t.y0 = (y0 + y1) * 0.5f; t.x1 = gh;
            if (pc.step == 0) { t.r = 20; t.g = 20; t.b = 24; } else { t.r = (std::uint8_t)R; t.g = (std::uint8_t)G; t.b = (std::uint8_t)B; }
            t.a = 255;
            if (pc.num >= 0) { std::snprintf(t.text, sizeof(t.text), "%d", pc.num); }   // explicit label (knot click count)
            else { t.text[0] = (char)('1' + pc.step); t.text[1] = 0; }                   // default: step number
            push(t);
        }
    }

    // --- Celtic-knot arrows: an Interfaces-hover-style box (sky-blue, corner brackets) on each arrow to click,
    //     with the remaining click count in a pill ABOVE the box so it never covers the arrow. Coords are
    //     already screen pixels (resolved live each tick), so NO uiScale. ---
    if (!kcells.empty()) {
        float kpulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1100) / 1100.0 * 6.2831853));
        for (const auto& kc : kcells) {
            if (kc.w <= 0 || kc.h <= 0) continue;
            float x0 = (float)kc.x, y0 = (float)kc.y, x1 = (float)(kc.x + kc.w), y1 = (float)(kc.y + kc.h);
            marker::Command q{}; q.type = marker::kFillRect; q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
            q.r = kAccR; q.g = kAccG; q.b = kAccB; q.a = (std::uint8_t)(50 + (int)(45.0f * kpulse)); push(q);
            line(x0, y0, x1, y0, 2.4f, kAccR, kAccG, kAccB, 235); line(x1, y0, x1, y1, 2.4f, kAccR, kAccG, kAccB, 235);
            line(x1, y1, x0, y1, 2.4f, kAccR, kAccG, kAccB, 235); line(x0, y1, x0, y0, 2.4f, kAccR, kAccG, kAccB, 235);
            float L = (x1 - x0) * 0.34f; const float bw = 3.4f;
            line(x0, y0, x0 + L, y0, bw, 255, 255, 255, 255); line(x0, y0, x0, y0 + L, bw, 255, 255, 255, 255);
            line(x1, y0, x1 - L, y0, bw, 255, 255, 255, 255); line(x1, y0, x1, y0 + L, bw, 255, 255, 255, 255);
            line(x0, y1, x0 + L, y1, bw, 255, 255, 255, 255); line(x0, y1, x0, y1 - L, bw, 255, 255, 255, 255);
            line(x1, y1, x1 - L, y1, bw, 255, 255, 255, 255); line(x1, y1, x1, y1 - L, bw, 255, 255, 255, 255);
            // count pill ABOVE the box (centred), dark bg + sky-blue edge + bold white number -> readable, off the button
            char lbl[8]; std::snprintf(lbl, sizeof(lbl), "%d", kc.count);
            float cx = (x0 + x1) * 0.5f;
            float pw = (float)((int)std::strlen(lbl) * 11 + 16), ph = 21.0f;
            float px0 = cx - pw * 0.5f, py1 = y0 - 7.0f, py0 = py1 - ph, px1 = px0 + pw;
            marker::Command pb{}; pb.type = marker::kFillRect; pb.x0 = px0; pb.y0 = py0; pb.x1 = px1; pb.y1 = py1;
            pb.r = 16; pb.g = 18; pb.b = 24; pb.a = 238; push(pb);
            line(px0, py0, px1, py0, 1.8f, kAccR, kAccG, kAccB, 255); line(px1, py0, px1, py1, 1.8f, kAccR, kAccG, kAccB, 255);
            line(px1, py1, px0, py1, 1.8f, kAccR, kAccG, kAccB, 255); line(px0, py1, px0, py0, 1.8f, kAccR, kAccG, kAccB, 255);
            marker::Command t{}; t.type = marker::kText; t.x0 = cx; t.y0 = (py0 + py1) * 0.5f; t.x1 = 17.0f;
            t.r = 255; t.g = 255; t.b = 255; t.a = 255; std::snprintf(t.text, sizeof(t.text), "%s", lbl); push(t);
        }
    }

    // --- Skills XP progress bars: a thin track along the bottom edge of each skill cell, filled by
    //     progress to the next level. Rects arrive as screen pixels (client.html converts its
    //     tree-walk coords by the reader-published interface scale), like the
    //     puzzle/knot cells, so NO uiScale. No pulse and no label: this is ambient information
    //     sitting under the game's own numbers, and it has to stay readable at a glance across
    //     29 cells without competing with them.
    //
    //     OVERLAP GUARD: this layer composites ABOVE the finished game frame, so the bars can
    //     never render "under" the game's own pixels -- a hover tooltip or the cell numbers
    //     would always lose. Instead the bars step aside: while the cursor is inside the
    //     bars' panel area (exactly when the game shows its skill tooltip, and when the
    //     player is actually reading the panel), they are skipped for the frame; they return
    //     the moment the cursor leaves. Publish runs every ~33ms, so it feels instant. ---
    bool sbarsHide = false;
    if (!sbars.empty()) {
        if (HWND sgw = FindGameWindow(cfg.pid)) {
            POINT cur;
            if (GetCursorPos(&cur) && ScreenToClient(sgw, &cur)) {
                // Cursor is this process's physical px; the bar rects are game-space px.
                const double sgsf = rtx::launcher::dock::GameSpaceFactor(sgw, cfg.pid);
                const float cx = (float)(cur.x * sgsf), cy = (float)(cur.y * sgsf);
                float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; bool any = false;
                for (const auto& sb : sbars) {
                    if (sb.w <= 0 || sb.h <= 0) continue;
                    const float sx0 = (float)sb.x, sy0 = (float)sb.y;
                    const float sx1 = (float)(sb.x + sb.w), sy1 = (float)(sb.y + sb.h);
                    if (!any) { bx0 = sx0; by0 = sy0; bx1 = sx1; by1 = sy1; any = true; }
                    else {
                        if (sx0 < bx0) bx0 = sx0; if (sy0 < by0) by0 = sy0;
                        if (sx1 > bx1) bx1 = sx1; if (sy1 > by1) by1 = sy1;
                    }
                }
                const float pad = 8.0f;   // a tooltip anchors just past the hovered cell edge
                sbarsHide = any && cx >= bx0 - pad && cx <= bx1 + pad &&
                                   cy >= by0 - pad && cy <= by1 + pad;
            }
        }
    }
    if (!sbarsHide)
    for (const auto& sb : sbars) {
        if (sb.w <= 0 || sb.h <= 0) continue;
        float x0 = (float)sb.x, y1 = (float)(sb.y + sb.h);
        float x1 = (float)(sb.x + sb.w);
        // Inset from the cell edge, and a height that scales with the cell but stays legible.
        float inset = 2.0f;
        float bh = (float)sb.h * 0.10f; if (bh < 2.5f) bh = 2.5f; if (bh > 5.0f) bh = 5.0f;
        float bx0 = x0 + inset, bx1 = x1 - inset, by1 = y1 - inset, by0 = by1 - bh;
        if (bx1 <= bx0) continue;
        // Track: dark plate so the fill reads on any cell art.
        { marker::Command q{}; q.type = marker::kFillRect;
          q.x0 = bx0; q.y0 = by0; q.x1 = bx1; q.y1 = by1;
          q.r = 0; q.g = 0; q.b = 0; q.a = 165; push(q); }
        int pct = sb.pct; if (pct < 0) pct = 0; if (pct > 1000) pct = 1000;
        const float fw = (bx1 - bx0) * (float)pct / 1000.0f;
        if (fw > 0.0f) {
            std::uint8_t r = (std::uint8_t)((sb.rgb >> 16) & 0xFF);
            std::uint8_t g2 = (std::uint8_t)((sb.rgb >> 8) & 0xFF);
            std::uint8_t b = (std::uint8_t)(sb.rgb & 0xFF);
            if (!sb.rgb) { r = kOkR; g2 = kOkG; b = kOkB; }     // default: the same green as "done"
            marker::Command q{}; q.type = marker::kFillRect;
            q.x0 = bx0; q.y0 = by0; q.x1 = bx0 + fw; q.y1 = by1;
            q.r = r; q.g = g2; q.b = b; q.a = 240; push(q);
        }
    }

    // --- Interfaces-tab panel visualizer: each toggled panel's true screen rect, in client pixels
    //     (panel-position varc + size varc). Teal outline + name label so it reads as "panel bounds",
    //     distinct from the sky-blue "click here" highlight above. No pulse (steady reference frame). ---
    for (const auto& pb : pviz) {
        if (pb.w <= 0 || pb.h <= 0) continue;
        float bx0 = (float)pb.x * uiScale, by0 = (float)pb.y * uiScale;
        float bx1 = (float)(pb.x + pb.w) * uiScale, by1 = (float)(pb.y + pb.h) * uiScale;
        marker::Command fillc{}; fillc.type = marker::kFillRect;
        fillc.x0 = bx0; fillc.y0 = by0; fillc.x1 = bx1; fillc.y1 = by1;
        fillc.r = 64; fillc.g = 210; fillc.b = 224; fillc.a = 26;     // faint teal wash
        push(fillc);
        line(bx0, by0, bx1, by0, 2.4f, 90, 220, 235, 235);
        line(bx1, by0, bx1, by1, 2.4f, 90, 220, 235, 235);
        line(bx1, by1, bx0, by1, 2.4f, 90, 220, 235, 235);
        line(bx0, by1, bx0, by0, 2.4f, 90, 220, 235, 235);
        if (!pb.label.empty()) {
            marker::Command t{}; t.type = marker::kText;
            // Small boxes (an inventory slot, ~36x32) -> label ABOVE the box so it never covers the
            // highlighted item; large boxes (panel-visualizer) keep the compact top-left label.
            bool smallBox = (by1 - by0) < 60.0f;                    // 'small' is a Windows macro (= char); don't use it
            t.x1 = 11.0f;                                           // compact font
            // kText CENTRES on x0/y0. Centre a small box's label over the box (not its left edge), then
            // clamp both axes so the pill stays on-screen.
            float cx = smallBox ? ((bx0 + bx1) * 0.5f) : (bx0 + 3.0f);
            float labelW = (float)pb.label.size() * (t.x1 * 0.62f) + 14.0f;   // estimated pill width (padX*2 + advances)
            float halfW = labelW * 0.5f;
            // Wide, short boxes (dropdown-style rows, e.g. the deduction-notes selects) read better
            // with the label BESIDE them: place it to the right when it fits on-screen. Near-square
            // slots (inventory cells) keep the above-the-box label.
            bool wideRow = smallBox && (bx1 - bx0) > 3.0f * (by1 - by0);
            float ty;
            if (wideRow && bx1 + labelW + 12.0f <= (float)W) { cx = bx1 + 8.0f + halfW; ty = (by0 + by1) * 0.5f; }
            else ty = smallBox ? (by0 - 14.0f) : (by0 + 2.0f);
            if (cx - halfW < 4.0f)                cx = 4.0f + halfW;
            if (cx + halfW > (float)W - 4.0f)     cx = (float)W - 4.0f - halfW;
            if (ty < 12.0f) ty = by1 + 2.0f;                        // no room above -> below the box
            if (ty > (float)H - 12.0f) ty = (float)H - 12.0f;       // keep on-screen vertically
            t.x0 = cx; t.y0 = ty;
            t.r = 200; t.g = 245; t.b = 250; t.a = 245;
            int n = (int)pb.label.size();
            if (n > marker::kTextMax) n = marker::kTextMax;
            std::memcpy(t.text, pb.label.data(), (size_t)n);
            t.text[n] = '\0';
            push(t);
        }
    }

    // --- alert flash (random events etc.): a fading translucent wash over the whole
    //     viewport, drawn INTO this client's frame -- strictly per-pid by construction ---
    if (flashAlpha > 0.0f) {
        marker::Command c{}; c.type = marker::kFillRect;
        c.x0 = vpX; c.y0 = vpY; c.x1 = vpX + vpW; c.y1 = vpY + vpH;
        c.r = 255; c.g = 70; c.b = 70;
        c.a = (std::uint8_t)((flashAlpha > 1.0f ? 1.0f : flashAlpha) * 120.0f);
        push(c);
    }

    // --- screen-space widgets (toast / notifications / metronome / XP tracker):
    //     pre-built by BuildWidgetCommands, appended LAST so they sit on top ---
    if (widgets)
        for (const auto& wc : *widgets) push(wc);

    // --- publish under the seqlock ---
    std::uint32_t n = (std::uint32_t)cmds.size();
    if (n > marker::kMaxCmds) n = marker::kMaxCmds;
    std::uint32_t s = sh->seq + 1;
    sh->seq = s; MemoryBarrier();                            // odd: mid-write
    sh->fb_w = W; sh->fb_h = H;
    sh->gv_x = (std::int32_t)vpX; sh->gv_y = (std::int32_t)vpY;
    sh->gv_w = (std::int32_t)vpW; sh->gv_h = (std::int32_t)vpH;
    for (std::uint32_t i = 0; i < n; ++i) sh->cmds[i] = cmds[i];
    sh->count = n;
    sh->visible = 1;
    MemoryBarrier(); sh->seq = s + 1;                        // even: done
}

// ---- in-frame widgets ---------------------------------------------------------
// The toast, sticky notifications, tick metronome and XP tracker are composed as marker commands
// and rendered INSIDE the game frame by the companion present layer, like every other overlay
// element. The external layered window is an invisible INPUT surface only (see RenderLoop): the
// widget hit rects are filled with alpha-2 pads to catch drag/dismiss clicks.

// Text width estimate for atlas text. Slight OVER-estimate (the companion lays glyphs out with
// its real advances), so panels sized from it never crop their text.
float WTextW(const char* s, float px) {
    int n = 0;
    for (const char* p = s; *p && *p != '\n'; ++p) ++n;
    return (float)n * px * 0.62f;
}

void WLine(std::vector<marker::Command>& v, float x0, float y0, float x1, float y1,
           float th, int r, int g, int b, int a) {
    marker::Command c{}; c.type = marker::kLine;
    c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.thickness = th;
    c.r = (std::uint8_t)r; c.g = (std::uint8_t)g; c.b = (std::uint8_t)b; c.a = (std::uint8_t)a;
    v.push_back(c);
}
void WRound(std::vector<marker::Command>& v, float x, float y, float w, float h,
            float rad, int r, int g, int b, int a) {
    marker::Command c{}; c.type = marker::kRoundFill;
    c.x0 = x; c.y0 = y; c.x1 = w; c.y1 = h; c.thickness = rad;
    c.r = (std::uint8_t)r; c.g = (std::uint8_t)g; c.b = (std::uint8_t)b; c.a = (std::uint8_t)a;
    v.push_back(c);
}
void WRect(std::vector<marker::Command>& v, float x0, float y0, float x1, float y1,
           float th, int r, int g, int b, int a) {
    marker::Command c{}; c.type = marker::kRect;
    c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.thickness = th;
    c.r = (std::uint8_t)r; c.g = (std::uint8_t)g; c.b = (std::uint8_t)b; c.a = (std::uint8_t)a;
    v.push_back(c);
}
// Plain aligned text line (no pill): y = vertical centre; align 0 left / 1 centre / 2 right.
void WText(std::vector<marker::Command>& v, const char* s, float x, float y, float px,
           int align, int r, int g, int b, int a) {
    marker::Command c{}; c.type = marker::kText;
    c.glyph = (std::uint16_t)(marker::kTextPlain | (align << 1));
    c.x0 = x; c.y0 = y; c.x1 = px;
    c.r = (std::uint8_t)r; c.g = (std::uint8_t)g; c.b = (std::uint8_t)b; c.a = (std::uint8_t)a;
    int n = 0;
    for (const char* p = s; *p && *p != '\n' && n < marker::kTextMax; ++p) c.text[n++] = *p;
    c.text[n] = '\0';
    if (n) v.push_back(c);
}

// Greedy word wrap on the estimated width (toast / notification bodies).
std::vector<std::string> WWrap(const std::string& s, float px, float maxW, int maxLines) {
    std::vector<std::string> lines;
    std::string cur;
    std::size_t i = 0;
    while (i < s.size() && (int)lines.size() < maxLines) {
        std::size_t sp = s.find(' ', i);
        std::size_t end = (sp == std::string::npos) ? s.size() : sp;
        std::string word = s.substr(i, end - i);
        std::string cand = cur.empty() ? word : cur + ' ' + word;
        if (!cur.empty() && WTextW(cand.c_str(), px) > maxW) { lines.push_back(cur); cur = word; }
        else cur = std::move(cand);
        i = (sp == std::string::npos) ? s.size() : sp + 1;
    }
    if (!cur.empty() && (int)lines.size() < maxLines) lines.push_back(cur);
    return lines;
}

// Rounded card chrome shared by the toast / notification / XP panels: soft drop
// shadow + accent border + dark fill (the kText-pill look, launcher-sized).
void WCard(std::vector<marker::Command>& v, float x, float y, float w, float h,
           float rad, int ar, int ag, int ab, float alpha) {
    WRound(v, x, y + 3.0f, w, h, rad, 0, 0, 0, (int)(95.0f * alpha));
    WRound(v, x - 1.2f, y - 1.2f, w + 2.4f, h + 2.4f, rad + 1.2f, ar, ag, ab, (int)(235.0f * alpha));
    WRound(v, x, y, w, h, rad, 20, 21, 28, (int)(240.0f * alpha));
}

// Toast: centred card near the top of the frame, purple accent, fading tail.
// Used for the augmented-item level alert ("... level X reached on <item>").
void BuildToastCmds(std::vector<marker::Command>& out, int W, int H,
                    const std::string& text, float alpha) {
    const float px = 14.0f;
    auto lines = WWrap(text, px, (float)W - 84.0f, 3);
    if (lines.empty()) return;
    float tw = 0.0f;
    for (const auto& l : lines) { float w2 = WTextW(l.c_str(), px); if (w2 > tw) tw = w2; }
    const float lineH = px * 1.35f;
    float bw = tw + 36.0f, bh = (float)lines.size() * lineH + 22.0f;
    float bx = ((float)W - bw) * 0.5f, by = (float)H * 0.07f;
    WCard(out, bx, by, bw, bh, 10.0f, 124, 92, 252, alpha);
    for (std::size_t i = 0; i < lines.size(); ++i)
        WText(out, lines[i].c_str(), bx + bw * 0.5f, by + 11.0f + lineH * ((float)i + 0.5f),
              px, 1, 245, 245, 250, (int)(255.0f * alpha));
}

// Sticky notifications: stacked cards at the top-right, amber accent, close 'x'.
// Refreshes g_notif_hit (client coords -> the input window's dismiss pads).
// Caller holds g_mu.
void BuildNotifCmds(std::vector<marker::Command>& out, int W) {
    g_notif_hit.clear();
    const float margin = 12.0f, padX = 14.0f, padY = 10.0f, gap = 8.0f, cxw = 22.0f, maxW = 340.0f;
    const float px = 13.0f, lineH = px * 1.3f;
    float y = margin;
    for (const auto& n : g_notifs) {
        auto lines = WWrap(n.text, px, maxW - padX * 2.0f - cxw, 6);
        if (lines.empty()) continue;
        float tw = 0.0f;
        for (const auto& l : lines) { float w2 = WTextW(l.c_str(), px); if (w2 > tw) tw = w2; }
        float bw = tw + padX * 2.0f + cxw; if (bw > maxW) bw = maxW;
        float bh = (float)lines.size() * lineH + padY * 2.0f;
        float bx = (float)W - margin - bw;
        WCard(out, bx, y, bw, bh, 9.0f, 240, 190, 90, 1.0f);
        for (std::size_t i = 0; i < lines.size(); ++i)
            WText(out, lines[i].c_str(), bx + padX, y + padY + lineH * ((float)i + 0.5f),
                  px, 0, 245, 245, 250, 255);
        float xcx = bx + bw - padX - cxw * 0.5f, xcy = y + bh * 0.5f, xr = 4.0f;
        WLine(out, xcx - xr, xcy - xr, xcx + xr, xcy + xr, 1.7f, 200, 200, 205, 225);
        WLine(out, xcx - xr, xcy + xr, xcx + xr, xcy - xr, 1.7f, 200, 200, 205, 225);
        g_notif_hit.push_back(NotifHit{ n.id, RECT{ (LONG)bx, (LONG)y, (LONG)(bx + bw), (LONG)(y + bh) } });
        y += bh + gap;
    }
}

// Tick metronome: the clock-style pulse, composed from round fills + line
// segments. The sweep hand + filling arc reach 12 o'clock exactly on the tick;
// the core flashes bright at the tick and fades. phase01 in [0,1).
void BuildMetroCmds(std::vector<marker::Command>& out, int cx, int cy, int r,
                    double phase01, bool have) {
    const float fcx = (float)cx, fcy = (float)cy, fr = (float)r;
    constexpr int    kSeg = 28;
    constexpr double kTau = 6.283185307179586;
    WRound(out, fcx - fr, fcy - fr, fr * 2.0f, fr * 2.0f, fr, 10, 12, 20, 150);   // bg disc
    const float rr = fr - 1.0f;                                                   // outer ring
    for (int i = 0; i < kSeg; ++i) {
        double a0 = kTau * i / kSeg, a1 = kTau * (i + 1) / kSeg;
        WLine(out, fcx + (float)std::cos(a0) * rr, fcy + (float)std::sin(a0) * rr,
                   fcx + (float)std::cos(a1) * rr, fcy + (float)std::sin(a1) * rr,
              2.0f, 70, 80, 110, 175);
    }
    if (!have) {                                   // no tick data yet: dim idle dot
        float cr = fr / 3.0f;
        WRound(out, fcx - cr, fcy - cr, cr * 2.0f, cr * 2.0f, cr, 130, 130, 200, 90);
        return;
    }
    const double start = -kTau * 0.25;             // 12 o'clock
    const double sweep = phase01 * kTau;
    const float  ar2   = fr - 4.0f;                // filling arc
    int nseg = (int)(phase01 * kSeg) + 1; if (nseg > kSeg) nseg = kSeg;
    for (int i = 0; i < nseg; ++i) {
        double a0 = start + sweep * i / nseg, a1 = start + sweep * (i + 1) / nseg;
        WLine(out, fcx + (float)std::cos(a0) * ar2, fcy + (float)std::sin(a0) * ar2,
                   fcx + (float)std::cos(a1) * ar2, fcy + (float)std::sin(a1) * ar2,
              3.5f, 140, 111, 253, 225);
    }
    double fl = 1.0 - phase01;                      // bright at the tick, fading
    float cr = fr * 0.42f;
    WRound(out, fcx - cr, fcy - cr, cr * 2.0f, cr * 2.0f, cr,
           170, 140, 255, (int)(fl * fl * 210.0));
    double a = start + sweep;                       // sweep hand
    WLine(out, fcx, fcy, fcx + (float)std::cos(a) * (fr - 6.0f),
          fcy + (float)std::sin(a) * (fr - 6.0f), 2.5f, 255, 255, 255, 235);
}


// Reader skill order (Attack..Necromancy), shown on the XP panel rows.
const char* kXpSkillNames[29] = {
    "Attack", "Defence", "Strength", "Constitution", "Ranged",
    "Prayer", "Magic", "Cooking", "Woodcutting", "Fletching",
    "Fishing", "Firemaking", "Crafting", "Smithing", "Mining",
    "Herblore", "Agility", "Thieving", "Slayer", "Farming",
    "Runecrafting", "Hunter", "Construction", "Summoning",
    "Dungeoneering", "Divination", "Invention", "Archaeology",
    "Necromancy",
};

// Compact XP formatting: 9,123 / 91.2K / 9.12M / 912M. 64-bit safe (totals
// exceed the 200M per-skill cap).
std::string FmtXp(long long v) {
    char b[40];
    if (v >= 100000000)     std::snprintf(b, sizeof(b), "%lldM", v / 1000000);
    else if (v >= 10000000) std::snprintf(b, sizeof(b), "%.1fM", (double)v / 1e6);
    else if (v >= 1000000)  std::snprintf(b, sizeof(b), "%.2fM", (double)v / 1e6);
    else if (v >= 100000)   std::snprintf(b, sizeof(b), "%lldK", v / 1000);
    else if (v >= 10000)    std::snprintf(b, sizeof(b), "%.1fK", (double)v / 1e3);
    else                    std::snprintf(b, sizeof(b), "%lld", v);
    return b;
}

// XP-tracker card. Caller holds g_mu. Updates the hit rects (whole card +
// minimize box) used by the input window / WndProc / click-through toggle.
void BuildXpCmds(std::vector<marker::Command>& out, int W, int H) {
    long long t = now_ms();

    // visible rows: auto = skills that gained XP this session (first-gain order);
    // custom = the selected mask, fixed skill order.
    struct Row { int id; long long gained, ph; };
    std::vector<Row> rows;
    long long totalGained = 0;
    for (int i = 0; i < 29; ++i) {
        long long gn = (g_xp.haveBase && g_xp.cur[i] >= 0 && g_xp.base[i] >= 0)
                           ? (long long)g_xp.cur[i] - g_xp.base[i] : 0;
        if (gn < 0) gn = 0;
        totalGained += gn;
        bool want = g_xp.autoSkills ? gn > 0 : ((g_xp.mask >> i) & 1u) != 0;
        if (want) rows.push_back({ i, gn, XpRatePerHour(gn, g_xp.firstGain[i], t) });
    }
    if (g_xp.autoSkills)
        std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            return g_xp.firstGain[a.id] < g_xp.firstGain[b.id];   // stable: rows never reorder
        });

    const float bw = 238.0f, rowH = 19.0f, hdrH = 27.0f;
    int nRows = (int)rows.size() + (g_xp.showTotal ? 1 : 0);
    float bh = g_xp.minimized ? hdrH : hdrH + nRows * rowH + (nRows ? 7.0f : 0.0f);
    if (!g_xp.placed) { g_xp.x = 14; g_xp.y = 84; }                  // default spot until dragged
    if (g_xp.x < 0) g_xp.x = 0;
    if (g_xp.y < 0) g_xp.y = 0;
    if (g_xp.x > W - (int)bw) g_xp.x = W - (int)bw;
    if (g_xp.y > H - (int)bh) g_xp.y = H - (int)bh;
    float bx = (float)g_xp.x, by = (float)g_xp.y;

    WCard(out, bx, by, bw, bh, 9.0f, 124, 92, 252, 1.0f);

    WRound(out, bx + 11.0f, by + hdrH * 0.5f - 3.0f, 6.0f, 6.0f, 3.0f, 140, 111, 253, 255);
    WText(out, "XP / hr", bx + 23.0f, by + hdrH * 0.5f, 12.0f, 0, 245, 245, 250, 255);
    if (g_xp.startMs > 0) {
        long long el = (t - g_xp.startMs) / 1000;
        char b[32];
        if (el >= 3600) std::snprintf(b, sizeof(b), "%lldh %02lldm", el / 3600, (el % 3600) / 60);
        else            std::snprintf(b, sizeof(b), "%lldm %02llds", el / 60, el % 60);
        WText(out, b, bx + bw - 34.0f, by + hdrH * 0.5f, 11.5f, 2, 150, 153, 165, 255);
    }
    float mbX = bx + bw - 24.0f, mbY = by + hdrH * 0.5f - 7.0f;
    WRect(out, mbX, mbY, mbX + 14.0f, mbY + 14.0f, 1.5f, 200, 200, 210, 220);
    WLine(out, mbX + 3.5f, mbY + 7.0f, mbX + 10.5f, mbY + 7.0f, 1.5f, 200, 200, 210, 220);
    if (g_xp.minimized)
        WLine(out, mbX + 7.0f, mbY + 3.5f, mbX + 7.0f, mbY + 10.5f, 1.5f, 200, 200, 210, 220);
    g_xp_min = RECT{ (LONG)(mbX - 3), (LONG)(mbY - 3), (LONG)(mbX + 17), (LONG)(mbY + 17) };

    if (!g_xp.minimized && nRows) {
        WLine(out, bx + 9.0f, by + hdrH, bx + bw - 9.0f, by + hdrH, 1.0f, 124, 92, 252, 70);
        float y = by + hdrH + 3.0f;
        const float colGain = bw - 96.0f;   // right edge of the "gained" column
        auto row = [&](const char* name, long long gained, long long ph,
                       int nr, int ng, int nb, int vr, int vg, int vb) {
            WText(out, name, bx + 12.0f, y + rowH * 0.5f, 11.5f, 0, nr, ng, nb, 255);
            std::string gs = "+" + FmtXp(gained);
            WText(out, gs.c_str(), bx + colGain, y + rowH * 0.5f, 11.5f, 2, vr, vg, vb, 255);
            std::string ps = FmtXp(ph) + "/h";
            WText(out, ps.c_str(), bx + bw - 12.0f, y + rowH * 0.5f, 11.5f, 2, vr, vg, vb, 255);
            y += rowH;
        };
        if (g_xp.showTotal)
            row("Total", totalGained, XpRatePerHour(totalGained, g_xp.startMs, t),
                170, 145, 255, 170, 145, 255);
        for (const auto& rw : rows)
            row(kXpSkillNames[rw.id], rw.gained, rw.ph, 208, 210, 220, 245, 245, 250);
    }
    g_xp_hit = RECT{ (LONG)bx, (LONG)by, (LONG)(bx + bw), (LONG)(by + bh) };
}

// Assemble every widget targeting `pid` into `out` and refresh the widget hit
// rects the input window uses. W/H = the game window's client size (the input
// window overlays the client area 1:1, so the rects share one space). mTick/
// mAge/mHave = this iteration's tick sample for the metronome pid. Locks g_mu.
void BuildWidgetCommands(DWORD pid, int W, int H, long long tnow,
                         std::uint32_t mTick, double mAge, bool mHave,
                         std::vector<marker::Command>& out) {
    if (g_toast_pid.load() == pid) {
        long long until = g_toast_until_ms.load();
        if (tnow < until) {
            long long rem = until - tnow;
            float a = rem >= kToastFadeMs ? 1.0f : (float)rem / (float)kToastFadeMs;
            std::string txt;
            { std::lock_guard<std::mutex> lk(g_mu); txt = g_toast_text; }
            if (a > 0.0f && !txt.empty()) BuildToastCmds(out, W, H, txt, a);
        }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_notif_pid.load() == pid) {
        if (!g_notifs.empty()) BuildNotifCmds(out, W);
        else g_notif_hit.clear();
    }
    if (g_metro.pid == pid) {
        if (g_metro.on) {
            int mr = g_metro.size;
            if (!g_metro.placed) { g_metro.x = W / 2; g_metro.y = H / 2; }   // auto-centre until dragged
            if (g_metro.x < mr)     g_metro.x = mr;
            if (g_metro.x > W - mr) g_metro.x = W - mr;
            if (g_metro.y < mr)     g_metro.y = mr;
            if (g_metro.y > H - mr) g_metro.y = H - mr;
            int itv = g_metro.interval < 1 ? 1 : g_metro.interval;
            // Phase spans the whole N-tick beat: 0 at the beat, 1 just before
            // the next, so the sweep/flash lands on every Nth tick.
            bool   have = mHave && mAge >= 0.0;
            double ph = have
                ? ((double)(mTick % (std::uint32_t)itv) +
                   (mAge < kTickMs ? mAge / kTickMs : 1.0)) / (double)itv
                : 0.0;
            BuildMetroCmds(out, g_metro.x, g_metro.y, mr, ph, have);
            g_metro_hit = RECT{ (LONG)(g_metro.x - mr), (LONG)(g_metro.y - mr),
                                (LONG)(g_metro.x + mr), (LONG)(g_metro.y + mr) };
        } else {
            g_metro_hit = RECT{0, 0, 0, 0};
        }
    }
    if (g_xp.pid == pid) {
        if (g_xp.on) BuildXpCmds(out, W, H);
        else { g_xp_hit = RECT{0, 0, 0, 0}; g_xp_min = RECT{0, 0, 0, 0}; }
    }
}

// Overlay window proc: drag/resize the metronome widget, or dismiss a clicked
// notification. The window is made interactive only while the cursor is over a
// widget (see the render loop), so these only fire for deliberate clicks.
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Window coords are physical px; every widget rect and position (g_metro.x ...) is in
    // the game's pixel space -- convert incoming points once, then all math is same-space.
    auto toGameSpace = [](POINT& pt) {
        double f = g_inputScale.load();
        if (f > 0.0 && f != 1.0) {
            pt.x = (LONG)std::lround(pt.x * f);
            pt.y = (LONG)std::lround(pt.y * f);
        }
    };
    if (msg == WM_LBUTTONDOWN) {
        POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
        toGameSpace(pt);
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_metro.on && !g_metro.locked && PtInRect(&g_metro_hit, pt)) {  // start dragging the metronome
            g_metro.dragging = true;
            g_metro.placed   = true;                        // user-positioned -> stop auto-centring
            g_metro.dragOff.x = pt.x - g_metro.x;
            g_metro.dragOff.y = pt.y - g_metro.y;
            SetCapture(hwnd);
            return 0;
        }
        if (g_xp.on && !g_xp.locked) {
            if (PtInRect(&g_xp_min, pt)) {                  // minimize / restore the XP panel
                g_xp.minimized = !g_xp.minimized;
                SaveXp();
                return 0;
            }
            if (PtInRect(&g_xp_hit, pt)) {                  // start dragging the XP panel
                g_xp.dragging = true;
                g_xp.placed   = true;
                g_xp.dragOff.x = pt.x - g_xp.x;
                g_xp.dragOff.y = pt.y - g_xp.y;
                SetCapture(hwnd);
                return 0;
            }
        }
        for (const auto& hit : g_notif_hit) {
            if (PtInRect(&hit.rect, pt)) {
                for (size_t i = 0; i < g_notifs.size(); ++i)
                    if (g_notifs[i].id == hit.id) { g_notifs.erase(g_notifs.begin() + (long)i); break; }
                break;
            }
        }
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        std::lock_guard<std::mutex> lk(g_mu);
        POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
        toGameSpace(pt);
        if (g_metro.dragging) {
            g_metro.x = pt.x - g_metro.dragOff.x;
            g_metro.y = pt.y - g_metro.dragOff.y;
            return 0;
        }
        if (g_xp.dragging) {
            g_xp.x = pt.x - g_xp.dragOff.x;
            g_xp.y = pt.y - g_xp.dragOff.y;
            return 0;
        }
    }
    if (msg == WM_LBUTTONUP) {
        bool was = false;
        { std::lock_guard<std::mutex> lk(g_mu);
          if (g_metro.dragging) { g_metro.dragging = false; was = true; SaveMetro(); }
          if (g_xp.dragging)    { g_xp.dragging    = false; was = true; SaveXp(); } }
        if (was) { ReleaseCapture(); return 0; }
    }
    if (msg == WM_MOUSEWHEEL) {                       // wheel over the widget = resize
        POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
        ScreenToClient(hwnd, &pt);                    // wheel coords are screen-relative
        toGameSpace(pt);
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_metro.on && !g_metro.locked && PtInRect(&g_metro_hit, pt)) {
            int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 5 : -5;
            int s = g_metro.size + d;
            g_metro.size = (s < 20 ? 20 : (s > 240 ? 240 : s));
            SaveMetro();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RenderLoop() {
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);

    const wchar_t* kClass = L"RtxWorldOverlay";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    // Without a class cursor, when the overlay drops WS_EX_TRANSPARENT to catch a
    // notification click it owns the cursor area but defines no cursor, so Windows
    // shows the app-starting/busy cursor. Give it a normal arrow.
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClass, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);

    Dib dib;
    bool shown = false;
    // Last good overlay frame per client: a transient build failure (root/worker hiccup during a
    // region reload, rescan pending after a game update) re-serves the previous frame briefly instead
    // of publishing an empty one, which reads as a full-screen flicker of every marker.
    struct HeldFrame { rtx::reader::OverlayFrame frame; long long at_ms = 0; };
    std::unordered_map<DWORD, HeldFrame> held;
    constexpr long long kHoldMs = 600;
    // Config snapshot, refreshed only when g_cfgsVer moves: Config carries strings and vectors, so
    // deep-copying the whole map every frame is pure heap churn for data that changes on interaction.
    std::map<DWORD, Config> cfgs;
    std::uint64_t cfgsSeen = ~0ull;

    while (g_running.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        std::map<DWORD, long long> flashes;
        bool metroOn, metroAudio; DWORD metroPid; int metroInterval;
        bool xpOn; DWORD xpPid;
        { std::lock_guard<std::mutex> lk(g_mu);
          if (cfgsSeen != g_cfgsVer) { cfgs = g_cfgs; cfgsSeen = g_cfgsVer; }
          flashes = g_flash_until;
          metroOn = g_metro.on; metroAudio = g_metro.audio;
          metroPid = g_metro.pid; metroInterval = g_metro.interval < 1 ? 1 : g_metro.interval;
          xpOn = g_xp.on; xpPid = g_xp.pid; }

        // XP-tracker sampling (1 Hz) -- runs whenever a client is configured, even with
        // the on-game panel hidden, so the XP Tracker tab's session mirror tracks either
        // way. The baseline captures on the first readable sample; a character switch in
        // the same client (any skill's XP DECREASING) re-baselines the session.
        if (xpPid) {
            long long t = now_ms();
            bool due;
            { std::lock_guard<std::mutex> lk(g_mu); due = (t - g_xp.sampleMs) >= 1000; }
            if (due) {
                int cur[29];
                bool ok = rtx::reader::SkillsXp(xpPid, cur);
                std::lock_guard<std::mutex> lk(g_mu);
                g_xp.sampleMs = t;
                if (ok) {
                    bool switched = false;
                    for (int i = 0; i < 29 && !switched; ++i)
                        if (g_xp.cur[i] >= 0 && cur[i] >= 0 && cur[i] < g_xp.cur[i]) switched = true;
                    if (switched || !g_xp.haveBase) {
                        ResetXpSession();
                        for (int i = 0; i < 29; ++i) { g_xp.base[i] = cur[i]; g_xp.cur[i] = cur[i]; }
                        g_xp.haveBase = true;
                    } else {
                        for (int i = 0; i < 29; ++i) {
                            if (cur[i] < 0) continue;
                            if (g_xp.base[i] < 0) g_xp.base[i] = cur[i];   // record appeared mid-session
                            if (g_xp.firstGain[i] == 0 && g_xp.cur[i] >= 0 && cur[i] > g_xp.base[i])
                                g_xp.firstGain[i] = t;
                            if (g_xp.startMs == 0 && g_xp.firstGain[i] != 0) g_xp.startMs = t;
                            g_xp.cur[i] = cur[i];
                        }
                    }
                }
            }
        }

        // Metronome tick state: count drives the beat (every N ticks), age the
        // smooth visual phase. Audio plays ONLY on a confirmed new tick (count
        // change) -> no doubles, no prediction, locked to the real server tick.
        std::uint32_t mTick = 0; double mAge = -1.0; bool mHave = false;
        if ((metroOn || metroAudio) && metroPid)
            mHave = rtx::reader::TickState(metroPid, mTick, mAge);
        if (metroAudio && mHave) {
            std::uint32_t prev;
            { std::lock_guard<std::mutex> lk(g_mu); prev = g_metro.lastTick; g_metro.lastTick = mTick; }
            if (prev != 0xFFFFFFFFu && mTick != prev && (mTick % (std::uint32_t)metroInterval) == 0)
                PlayMetroClick();
        }

        // ---- PER-CLIENT in-frame publishing: each configured pid gets its own frame build
        //      + marker-section publish, so grids, markers, highlights, the alert flash AND the
        //      screen-space widgets draw in the REQUESTING client's render only. ----
        long long tnow = now_ms();
        // Drop expired auto-dismiss notifications BEFORE publishing, so a timed
        // card leaves the frame the moment its ttl lapses.
        bool anyNotifs;
        { std::lock_guard<std::mutex> lk(g_mu);
          for (size_t i = 0; i < g_notifs.size(); )
              if (g_notifs[i].expire_ms != 0 && tnow >= g_notifs[i].expire_ms) g_notifs.erase(g_notifs.begin() + (long)i);
              else ++i;
          anyNotifs = !g_notifs.empty(); }
        bool toasting = tnow < g_toast_until_ms.load(std::memory_order_acquire);
        bool anyFrames = false, anyFlash = false;
        for (auto& kv : cfgs) {
            const DWORD  cpid = kv.first;
            const Config& ccfg = kv.second;
            float fa = 0.0f;
            auto fit = flashes.find(cpid);
            if (fit != flashes.end() && tnow < fit->second) {
                long long rem = fit->second - tnow;
                fa = rem > kFlashMs ? 1.0f : (float)rem / (float)kFlashMs;
                anyFlash = true;
            }
            std::vector<rtx::reader::GuideSite> gsites; bool hasUiHl = false, hasPanelViz = false, hasCenter = false;
            { std::lock_guard<std::mutex> lk(g_mu);
              auto git = g_guides.find(cpid);
              if (git != g_guides.end())
                  for (const auto& m : git->second) gsites.push_back({ m.gx, m.gy, m.label, m.snapObj, m.rgb, m.gx2, m.gy2, m.region, m.plane });
              auto uit = g_uiHighlights.find(cpid);
              hasUiHl = (uit != g_uiHighlights.end() && !uit->second.empty());
              auto ctit = g_centerTexts.find(cpid);
              hasCenter = (ctit != g_centerTexts.end() && !ctit->second.empty());
              auto pit = g_panelViz.find(cpid);
              hasPanelViz = (pit != g_panelViz.end() && !pit->second.empty()); }
            bool wantF = ccfg.enabled || ccfg.markers || ccfg.nameplates || !ccfg.highlight.empty() ||
                         !ccfg.outline.empty() || !gsites.empty();
            bool wantWidgets = (toasting && g_toast_pid.load() == cpid) ||
                               (anyNotifs && g_notif_pid.load() == cpid) ||
                               (metroOn && metroPid == cpid) ||
                               (xpOn && xpPid == cpid);
            // A screen-space UI highlight, centre text, a panel-viz box (inventory-slot test /
            // Interfaces visualizer) OR a widget needs no world frame, but still needs the live
            // window size + a publish, so it can't take the cheap early-out.
            if (!wantF && !hasUiHl && !hasPanelViz && !hasCenter && fa <= 0.0f && !wantWidgets) { PublishMarkers(ccfg, nullptr, 0, 0); continue; }
            HWND gw = FindGameWindow(cpid);
            if (!gw || IsIconic(gw) || !IsWindowVisible(gw)) {
                PublishMarkers(ccfg, nullptr, 0, 0);
                continue;
            }
            RECT rc2; GetClientRect(gw, &rc2);
            // Command coordinates live in the GAME's pixel space: they are drawn into its frame
            // beside viewport values read from its memory. GetClientRect here is this process's
            // physical px, which differs when the game runs DPI-virtualized -- convert. The pid
            // overload uses the companion's MEASURED backbuffer size (the DPI inference reads
            // 1.0 once the game is our embedded child, which put every overlay at 2/3 scale on
            // a 150% display).
            double gsf = rtx::launcher::dock::GameSpaceFactor(gw, cpid);
            int W2 = (int)std::lround((rc2.right - rc2.left) * gsf);
            int H2 = (int)std::lround((rc2.bottom - rc2.top) * gsf);
            if (W2 < 16 || H2 < 16) continue;
            rtx::launcher::companion::EnsureLoaded(cpid);   // hosts the present layer
            std::vector<rtx::marker::Command> wcmds;
            if (wantWidgets)
                BuildWidgetCommands(cpid, W2, H2, tnow, mTick, mAge, mHave, wcmds);
            const std::vector<rtx::marker::Command>* wptr = wcmds.empty() ? nullptr : &wcmds;
            rtx::reader::OverlayFrame frame;
            bool ok = false;
            if (wantF)
                // "Enable overlay" gates the grid + markers; highlights are alert-driven and draw
                // even when the overlay is disabled. Nameplates gather entities independently: a
                // kind is walked when the grid overlay OR the Scene-tab nameplates want it.
                ok = rtx::reader::BuildOverlayFrame(
                         cpid,
                         (ccfg.enabled && ccfg.players)  || (ccfg.nameplates && (ccfg.np_players || !ccfg.np_player_uids.empty())),
                         (ccfg.enabled && ccfg.npcs)     || (ccfg.nameplates && ccfg.np_npcs),
                         (ccfg.enabled && ccfg.objects)  || (ccfg.nameplates && ccfg.np_objects),
                         ccfg.enabled && ccfg.specials,
                         (ccfg.enabled && ccfg.grid) ? ccfg.radius : 0,
                         ccfg.interactable, ccfg.highlight, ccfg.outline, gsites, frame);
            if (wantF && ok) {
                PublishMarkers(ccfg, &frame, W2, H2, fa, wptr);
                held[cpid] = { std::move(frame), tnow };
            } else if (wantF) {
                // Transient failure: re-serve the held frame within the hold window (positions
                // freeze for a beat). Past the window, publish empty.
                auto hit = held.find(cpid);
                bool hold = hit != held.end() && tnow - hit->second.at_ms < kHoldMs;
                PublishMarkers(ccfg, hold ? &hit->second.frame : nullptr, W2, H2, fa, wptr);
            } else {
                PublishMarkers(ccfg, nullptr, W2, H2, fa, wptr);
            }
            anyFrames = anyFrames || wantF || hasUiHl || hasPanelViz || hasCenter || wptr != nullptr;
        }
        // Drop held frames for clients no longer configured (closed docks).
        for (auto it = held.begin(); it != held.end(); )
            if (cfgs.find(it->first) == cfgs.end()) it = held.erase(it);
            else ++it;
        // Drop marker channels for clients that no longer have a config (closed docks).
        for (auto it = g_marker_outs.begin(); it != g_marker_outs.end(); ) {
            if (cfgs.find(it->first) == cfgs.end()) { it->second.close(); it = g_marker_outs.erase(it); }
            else ++it;
        }

        // ---- external layered window: INPUT ONLY. Every widget's pixels render in-frame
        //      (see BuildWidgetCommands); this invisible window just catches drag/dismiss
        //      clicks over the widget hit rects. A layered window hit-tests per PIXEL --
        //      alpha-0 areas pass clicks through no matter the WS_EX_TRANSPARENT state -- so
        //      the hit rects are filled with imperceptible alpha-2 pads. ----
        bool notifying = anyNotifs, metroLocked, xpLocked;
        { std::lock_guard<std::mutex> lk(g_mu);
          metroLocked = g_metro.locked; xpLocked = g_xp.locked; }
        DWORD np = g_notif_pid.load();
        DWORD pid  = (notifying && np)                     ? np
                   : (metroOn && metroPid && !metroLocked) ? metroPid
                   : (xpOn && xpPid && !xpLocked)          ? xpPid : 0;
        HWND  game = pid ? FindGameWindow(pid) : nullptr;   // embedded (panel-open) window only
        bool  visible = game && !IsIconic(game) && IsWindowVisible(game);
        bool  padMetro = metroOn && metroPid && pid == metroPid && !metroLocked;
        bool  padXp    = xpOn && xpPid && pid == xpPid && !xpLocked;
        bool  padNotif = notifying && np && pid == np;

        if (!visible || (!padNotif && !padMetro && !padXp)) {
            if (shown) { ShowWindow(hwnd, SW_HIDE); shown = false; }
            // Keep polling fast for the metronome sweep / audio beats / active frames.
            std::this_thread::sleep_for(std::chrono::milliseconds(
                (metroOn || metroAudio) ? 16 : (anyFrames || anyFlash || toasting) ? 33 : 120));
            continue;
        }

        RECT cr; GetClientRect(game, &cr);
        POINT tl{cr.left, cr.top};
        ClientToScreen(game, &tl);
        int W = cr.right - cr.left, H = cr.bottom - cr.top;   // PHYSICAL px: this window's space
        if (W < 16 || H < 16) { std::this_thread::sleep_for(std::chrono::milliseconds(60)); continue; }
        // Widget hit rects were computed in the game's pixel space; this window lives in
        // physical px. gsfIn maps physical -> game space; pads convert the other way.
        double gsfIn = rtx::launcher::dock::GameSpaceFactor(game, pid);
        if (gsfIn <= 0.0) gsfIn = 1.0;
        g_inputScale.store(gsfIn);

        if (dib.ensure(W, H)) {
            dib.clear();
            // Alpha-2 pads over the interactive rects: invisible, but they receive
            // clicks once the window drops WS_EX_TRANSPARENT. Premultiplied black at
            // alpha 2 is (B,G,R,A) = (0,0,0,2), so no premultiply pass is needed.
            auto pad = [&](const RECT& rcG) {   // rcG: game-space px -> convert to physical
                RECT rc{ (LONG)std::lround(rcG.left  / gsfIn), (LONG)std::lround(rcG.top    / gsfIn),
                         (LONG)std::lround(rcG.right / gsfIn), (LONG)std::lround(rcG.bottom / gsfIn) };
                int x0 = rc.left < 0 ? 0 : rc.left, y0 = rc.top < 0 ? 0 : rc.top;
                int x1 = rc.right > W ? W : rc.right, y1 = rc.bottom > H ? H : rc.bottom;
                auto* px = (std::uint32_t*)dib.bits;
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx)
                        px[(size_t)yy * W + xx] = 0x02000000u;
            };
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (padMetro) pad(g_metro_hit);
                if (padXp)    pad(g_xp_hit);
                if (padNotif)
                    for (const auto& hit : g_notif_hit) pad(hit.rect);
            }

            if (!shown) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); shown = true; }
            POINT src{0, 0};
            SIZE sz{W, H};
            POINT dst{tl.x, tl.y};
            BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            HDC screen = GetDC(nullptr);
            UpdateLayeredWindow(hwnd, screen, &dst, &sz, dib.dc, &src, 0, &bf, ULW_ALPHA);
            ReleaseDC(nullptr, screen);
            // keep it above the game without stealing focus
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else if (shown) {
            ShowWindow(hwnd, SW_HIDE); shown = false;
        }

        // Toggle click-through: steal clicks ONLY while the cursor is over a sticky notification
        // (to dismiss it), the metronome (to drag/resize it) or the XP card (to drag/minimize it);
        // elsewhere stay transparent so gameplay clicks pass straight to the game.
        bool overUi = false;
        if (shown) {
            POINT cur; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
            // Into game space: the hit rects below are in the game's px, cur is physical.
            cur.x = (LONG)std::lround(cur.x * gsfIn);
            cur.y = (LONG)std::lround(cur.y * gsfIn);
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_metro.dragging || g_xp.dragging) overUi = true;
            if (!overUi && padMetro && PtInRect(&g_metro_hit, cur)) overUi = true;
            if (!overUi && padXp && PtInRect(&g_xp_hit, cur)) overUi = true;
            if (!overUi && padNotif)
                for (const auto& hit : g_notif_hit)
                    if (PtInRect(&hit.rect, cur)) { overUi = true; break; }
        }
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        bool transparent = (ex & WS_EX_TRANSPARENT) != 0;
        if (overUi && transparent)    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
        if (!overUi && !transparent)  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | (LONG_PTR)WS_EX_TRANSPARENT);

        std::this_thread::sleep_for(std::chrono::milliseconds(
            (metroOn || metroAudio) ? 16
            : ((anyFrames || anyFlash || toasting || notifying || xpOn) ? 33 : 120)));
    }

    dib.destroy();
    for (auto& kv : g_marker_outs) kv.second.close();
    g_marker_outs.clear();
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(kClass, wc.hInstance);
    Gdiplus::GdiplusShutdown(token);
}

void ensure_thread() {
    if (!g_running.exchange(true)) g_thread = std::thread(RenderLoop);
}

}  // namespace

void Configure(const Config& c) {
    if (!c.pid) return;
    bool need_thread; size_t hisz;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot(c.pid);    // per-client slot: panels never clobber each other
        auto hl = dst.highlight;          // highlight is owned by SetHighlight; preserve it
        auto ol = dst.outline;            // outline is owned by SetOutline; preserve it
        auto npu = dst.np_player_uids;    // owned by SetNameplatePlayers; preserve it
        dst = c;
        dst.highlight = hl;
        dst.outline = ol;
        dst.np_player_uids = npu;
        need_thread = c.enabled || !dst.highlight.empty() || !dst.outline.empty();
        hisz = dst.highlight.size();
    }
    rtx::log::Launcher("[ovl] Configure pid=" + std::to_string(c.pid) + " en=" + std::to_string((int)c.enabled) + " hi=" + std::to_string(hisz));
    // The render thread is also needed for flashes/highlights, so once started it
    // stays running (idling cheaply, window hidden); Stop() tears it down.
    if (need_thread) ensure_thread();
}

void EnableMarkers(std::uint32_t pid) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);   // creates a slot if none exists yet
        dst.pid = pid;
        dst.markers = true;
    }
    ensure_thread();
}

void Metronome(std::uint32_t pid, bool visual, bool audio, int interval, bool locked) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        bool wasVisual = g_metro.on, wasAudio = g_metro.audio;
        g_metro.on       = visual;
        g_metro.audio    = audio;
        g_metro.locked   = locked;
        g_metro.interval = (interval < 1 ? 1 : (interval > 6 ? 6 : interval));
        g_metro.pid      = pid;
        if (locked) g_metro.dragging = false;          // can't be mid-drag while locked
        if (visual && !wasVisual) LoadMetro();         // restore saved position/size on enable
        if (!visual) g_metro_hit = RECT{0, 0, 0, 0};   // no widget -> no input pad
        if (audio && !wasAudio)   g_metro.lastTick = 0xFFFFFFFFu;  // don't fire on the first detection
        if (pid && visual) {
            Config& dst = cfg_slot((DWORD)pid);          // ensure the per-pid frame slot exists
            dst.pid = pid;                             // (the widget publishes in-frame)
        }
    }
    if (visual || audio) ensure_thread();
}


void XpPanel(std::uint32_t pid, bool visible, bool locked, bool total,
             bool auto_skills, std::uint32_t mask) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        bool wasOn = g_xp.on;
        if (g_xp.pid != (DWORD)pid) ResetXpSession();   // new client/account -> fresh session
        g_xp.on         = visible;
        g_xp.locked     = locked;
        g_xp.showTotal  = total;
        g_xp.autoSkills = auto_skills;
        g_xp.mask       = mask;
        g_xp.pid        = (DWORD)pid;
        if (locked) g_xp.dragging = false;              // can't be mid-drag while locked
        if (visible && !wasOn) LoadXp();                // restore saved position/minimized
        if (!visible) { g_xp_hit = RECT{0, 0, 0, 0}; g_xp_min = RECT{0, 0, 0, 0}; }
        if (pid && visible) {
            Config& dst = cfg_slot((DWORD)pid);           // ensure the per-pid frame slot exists
            dst.pid = pid;                              // (the widget publishes in-frame)
        }
    }
    // The render thread also hosts the 1 Hz sampler, so it must run even while the
    // panel is hidden (the tab's session mirror tracks either way). It idles cheaply.
    if (pid) ensure_thread();
}

void XpPanelReset(std::uint32_t) {
    std::lock_guard<std::mutex> lk(g_mu);
    ResetXpSession();                                   // next 1 Hz sample re-baselines
}

std::string XpPanelStateJson(std::uint32_t) {
    std::lock_guard<std::mutex> lk(g_mu);
    long long t = now_ms();
    std::string out = "{\"on\":";
    out += g_xp.on ? "true" : "false";
    out += ",\"elapsed\":" + std::to_string(g_xp.startMs > 0 ? t - g_xp.startMs : 0);
    long long totalGained = 0;
    std::string rows;
    for (int i = 0; i < 29; ++i) {
        long long gn = (g_xp.haveBase && g_xp.cur[i] >= 0 && g_xp.base[i] >= 0)
                           ? (long long)g_xp.cur[i] - g_xp.base[i] : 0;
        if (gn < 0) gn = 0;
        totalGained += gn;
        rows += (i ? "," : "");
        rows += "{\"id\":" + std::to_string(i) + ",\"xp\":" + std::to_string(g_xp.cur[i]) +
                ",\"gained\":" + std::to_string(gn) +
                ",\"ph\":" + std::to_string(XpRatePerHour(gn, g_xp.firstGain[i], t)) + "}";
    }
    out += ",\"total\":{\"gained\":" + std::to_string(totalGained) +
           ",\"ph\":" + std::to_string(XpRatePerHour(totalGained, g_xp.startMs, t)) + "}";
    out += ",\"rows\":[" + rows + "]}";
    return out;
}

void SetGuideMarks(std::uint32_t pid, std::vector<GuideMark> marks) {
    if (!pid) return;
    bool any;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (marks.empty()) g_guides.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_guides[(DWORD)pid] = std::move(marks);
        }
        any = !g_guides.empty();
    }
    if (any) ensure_thread();
}

void SetUiHighlight(std::uint32_t pid, UiHighlight hl) {
    if (hl.w <= 0 || hl.h <= 0) { SetUiHighlights(pid, {}); return; }
    SetUiHighlights(pid, std::vector<UiHighlight>{ hl });
}

void SetUiHighlights(std::uint32_t pid, const std::vector<UiHighlight>& rects) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        std::vector<UiHighlight> keep;
        for (const auto& r : rects) if (r.w > 0 && r.h > 0) keep.push_back(r);
        if (keep.empty()) g_uiHighlights.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_uiHighlights[(DWORD)pid] = keep;
        }
    }
    ensure_thread();
}

void SetPanelViz(std::uint32_t pid, const std::vector<PanelBox>& boxes) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (boxes.empty()) g_panelViz.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_panelViz[(DWORD)pid] = boxes;
        }
    }
    ensure_thread();
}

void SetCenterText(std::uint32_t pid, const std::string& text) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (text.empty()) g_centerTexts.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_centerTexts[(DWORD)pid] = text;
        }
    }
    ensure_thread();
}

void SetPuzzleCells(std::uint32_t pid, const std::vector<PuzzleCell>& cells) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (cells.empty()) g_puzzleCells.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_puzzleCells[(DWORD)pid] = cells;
        }
    }
    ensure_thread();
}

void SetSkillBars(std::uint32_t pid, const std::vector<SkillBar>& bars) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (bars.empty()) g_skillBars.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_skillBars[(DWORD)pid] = bars;
        }
    }
    ensure_thread();
}

void SetKnotCells(std::uint32_t pid, const std::vector<KnotCell>& cells) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (cells.empty()) g_knotCells.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
            dst.pid = pid;
            g_knotCells[(DWORD)pid] = cells;
        }
    }
    ensure_thread();
}

void SetHighlight(std::uint32_t pid, const std::vector<std::string>& names) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);     // creates a (disabled) slot if the grid is off
        dst.pid = pid;
        dst.highlight = names;
    }
    rtx::log::Launcher("[ovl] SetHighlight pid=" + std::to_string(pid) + " n=" + std::to_string(names.size()));
    ensure_thread();
}

void SetOutline(std::uint32_t pid, const std::vector<int>& uids) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);
        dst.pid = pid;
        dst.outline = uids;
    }
    ensure_thread();
}

void SetNameplatePlayers(std::uint32_t pid, const std::vector<int>& uids) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);
        dst.pid = pid;
        dst.np_player_uids = uids;
    }
    ensure_thread();
}

void Flash(std::uint32_t pid) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);     // ensure the per-pid slot exists
        dst.pid = pid;
        g_flash_until[(DWORD)pid] = now_ms() + kFlashMs;
    }
    ensure_thread();
}

void Toast(std::uint32_t pid, const std::string& text) {
    { std::lock_guard<std::mutex> lk(g_mu);
      g_toast_text = text;
      if (pid) {
          Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
          dst.pid = pid;                      // (the toast publishes in-frame)
      } }
    g_toast_pid.store((DWORD)pid);
    g_toast_until_ms.store(now_ms() + kToastMs);
    ensure_thread();
}

// ttl_ms 0 = sticky (stays until clicked); ttl_ms > 0 = auto-dismiss after that long.
// Identical text is deduped -- an existing card's timer is refreshed instead of stacking.
void Notify(std::uint32_t pid, const std::string& text, long long ttl_ms) {
    { std::lock_guard<std::mutex> lk(g_mu);
      long long exp = (ttl_ms > 0) ? now_ms() + ttl_ms : 0;
      bool found = false;
      for (auto& n : g_notifs) if (n.text == text) { n.expire_ms = exp; found = true; break; }
      if (!found) {
          g_notifs.push_back(Notif{ ++g_notif_seq, text, exp });
          while (g_notifs.size() > 6) g_notifs.erase(g_notifs.begin()); }   // cap the stack
      if (pid) {
          Config& dst = cfg_slot((DWORD)pid);   // ensure the per-pid frame slot exists
          dst.pid = pid;                      // (the cards publish in-frame)
      } }
    g_notif_pid.store((DWORD)pid);
    ensure_thread();
}

void ClearNotifs() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_notifs.clear();
}

void Stop() {
    if (g_running.exchange(false)) {
        if (g_thread.joinable()) g_thread.join();
    }
}

// Synchronously mark a client's in-frame marker channel not-visible, so the companion's present
// callback stops drawing into the game's GL context immediately. Used at teardown: it must be
// inert BEFORE the game destroys that context, or it draws into a dying context and crashes.
// Safe from any thread -- opens the pid's section directly and seqlock-writes visible=0.
void QuiesceMarkers(std::uint32_t pid) {
    if (!pid) return;
    // Drop the per-pid overlay state too: the render loop stops building frames for this
    // client and prunes its marker channel on the next pass.
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfgs.erase((DWORD)pid); ++g_cfgsVer;
        g_flash_until.erase((DWORD)pid);
        g_guides.erase((DWORD)pid);         // else these per-pid maps keep a small residue per closed client
        g_uiHighlights.erase((DWORD)pid);
        g_centerTexts.erase((DWORD)pid);
        g_panelViz.erase((DWORD)pid);
    }
    wchar_t name[64];
    marker::MakeSectionName(pid, name);
    HANDLE map = OpenFileMappingW(FILE_MAP_WRITE, FALSE, name);
    if (!map) return;                       // section never created -> nothing is drawing
    auto* sh = reinterpret_cast<marker::Share*>(
        MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, sizeof(marker::Share)));
    if (sh) {
        std::uint32_t s = sh->seq + 1;
        sh->seq = s; MemoryBarrier();
        sh->count = 0; sh->visible = 0;
        MemoryBarrier(); sh->seq = s + 1;
        UnmapViewOfFile(sh);
    }
    CloseHandle(map);
}

}  // namespace rtx::overlay
