#include "Overlay.h"

#include "Companion.h"
#include "Dock.h"
#include "Markers.h"
#include "../reader/Reader.h"
#include "../cache/CacheReader.h"           // terrain height for marker placement
#include "../shared/Log.h"
#include "../../companion/MarkerShare.h"   // in-frame marker command channel

#include <Windows.h>
// gdiplus.h uses unqualified min/max; under NOMINMAX pull in the std versions first.
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
#include <tuple>
#include <unordered_map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

namespace rtx::overlay {

namespace {

std::mutex        g_mu;
// Per-client state; each pid publishes into its own marker section. Guarded by g_mu.
std::map<DWORD, Config>    g_cfgs;
std::uint64_t              g_cfgsVer = 0;     // bumped on every g_cfgs mutation; render loop re-snapshots when it moves
// All g_cfgs writes go through this. Caller holds g_mu.
Config& cfg_slot(DWORD pid) {
    ++g_cfgsVer;
    Config& c = g_cfgs[pid];
    c.pid = pid;
    return c;
}
std::map<DWORD, long long> g_flash_until;     // per-pid flash deadline (steady ms)
std::thread       g_thread;
std::atomic<bool> g_running{false};
constexpr long long    kFlashMs = 480;
std::atomic<DWORD>     g_toast_pid{0};
std::atomic<long long> g_toast_until_ms{0};    // toast visible while now < this
std::string            g_toast_text;           // guarded by g_mu

constexpr int kSurfR = 11,  kSurfG = 13,  kSurfB = 18;    // #0B0D12  plate / scrim fill
constexpr int kInkR  = 5,   kInkG  = 7,   kInkB  = 11;    // #05070B  contour underlay
constexpr int kTxtR  = 232, kTxtG  = 237, kTxtB  = 244;   // #E8EDF4  primary type
constexpr int kAccR  = 140, kAccG  = 111, kAccB  = 253;   // #8C6FFD  --accent-hi

static std::string HighlightHoldKey(const rtx::reader::OverlayPoint& hp) {
    if (!hp.label.empty()) return hp.label;
    if (hp.uid) return "u:" + std::to_string(hp.uid);
    const float cx = hp.has_box3d ? (hp.bmin[0] + hp.bmax[0]) * 0.5f : hp.wx;
    const float cy = hp.has_box3d ? (hp.bmin[1] + hp.bmax[1]) * 0.5f : hp.wy;
    return "t:" + std::to_string((int)(cx / 512.f)) + "," + std::to_string((int)(cy / 512.f)) + "," + std::to_string(hp.kind);
}
constexpr int kOkR   = 77,  kOkG   = 210, kOkB   = 138;   // #4DD28A  --ok
constexpr int kOkTxR = 142, kOkTxG = 240, kOkTxB = 192;   // #8EF0C0  green type on dark
inline int MixSurf(int c, int s) { return (c + s * 3) / 4; }

std::atomic<DWORD>     g_notif_pid{0};
struct Notif    { int id; std::string text; long long expire_ms = 0; };  // expire_ms 0 = sticky
struct NotifHit { int id; RECT rect; };        // client coords
std::vector<Notif>     g_notifs;               // guarded by g_mu
std::vector<NotifHit>  g_notif_hit;            // last frame (guarded by g_mu)
int                    g_notif_seq = 0;        // guarded by g_mu
constexpr long long    kToastMs     = 4000;
constexpr long long    kToastFadeMs = 500;

constexpr double kTickMs = 600.0;              // RS3 server tick
struct Metro {
    bool  on    = false;
    bool  audio = false;
    int   interval = 1;                        // beat every N ticks (1..6)
    DWORD pid   = 0;
    int   x     = -1, y = -1;                   // centre, game-client coords
    int   size  = 46;                          // radius px
    bool  placed = false;                       // false = auto-centre
    bool  locked = false;                       // click-through
    bool  dragging = false;
    POINT dragOff{0, 0};
    std::uint32_t lastTick = 0xFFFFFFFFu;       // new-tick edge detection (audio)
};
Metro g_metro;                                  // guarded by g_mu
RECT  g_metro_hit{0, 0, 0, 0};                  // last-frame bbox, client coords (guarded by g_mu)
// Physical px -> game px for the input window (differ when the game is DPI-virtualized). Render loop writes, WndProc reads.
std::atomic<double> g_inputScale{1.0};

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
        g_metro.placed = true;
    }
}

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct XpPanelState {
    bool  on = false;
    bool  locked = false;                       // click-through
    bool  showTotal = true;
    bool  autoSkills = true;                    // rows appear on gain (else mask)
    std::uint32_t mask = 0;                     // bit i = show skill i
    DWORD pid = 0;
    int   x = -1, y = -1;                       // top-left, game-client coords
    bool  placed = false;
    bool  minimized = false;
    bool  dragging = false;
    POINT dragOff{0, 0};
    float scale = 1.0f;                         // 0.7..1.8, persisted
    double animMin = 0.0;                       // 0 = expanded, 1 = minimized
    double disp[29] = {};                       // eased displayed gain per skill
    double dispTotal = 0.0;
    bool      haveBase = false;
    long long startMs = 0;                      // first observed gain
    long long sampleMs = 0;
    int       base[29] = {};                    // -1 = unseen
    int       cur[29]  = {};                    // -1 = unseen
    long long firstGain[29] = {};               // 0 = none
};
XpPanelState g_xp;                              // guarded by g_mu
RECT g_xp_hit{0, 0, 0, 0};                      // last-frame bbox (guarded by g_mu)
RECT g_xp_min{0, 0, 0, 0};                      // minimize-box rect (guarded by g_mu)

// Transient per-pid channels below are all guarded by g_mu.
std::map<DWORD, std::vector<GuideMark>> g_guides;
std::map<DWORD, std::vector<UiHighlight>> g_uiHighlights;
std::map<DWORD, std::vector<UiLabel>> g_uiLabels;
std::map<DWORD, std::map<int, CenterBanner>> g_centerTexts;   // keyed by slot
std::map<DWORD, std::vector<PanelBox>> g_panelViz;
std::map<DWORD, std::vector<PuzzleCell>> g_puzzleCells;
std::map<DWORD, std::vector<KnotCell>> g_knotCells;
struct SkillBarsEntry { std::vector<SkillBar> bars; long long at_ms = 0; };
constexpr long long kSkillBarsTtlMs = 5000;
std::map<DWORD, SkillBarsEntry> g_skillBars;

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
    if (f) f << g_xp.x << ' ' << g_xp.y << ' ' << (g_xp.minimized ? 1 : 0) << ' ' << g_xp.scale;
}
void LoadXp() {   // caller holds g_mu
    auto p = xp_path(); if (p.empty()) return;
    std::ifstream f(p.c_str());
    int x = -1, y = -1, m = 0; float sc = 1.0f;
    if (f && (f >> x >> y >> m)) {
        g_xp.x = x; g_xp.y = y; g_xp.minimized = (m != 0);
        g_xp.animMin = g_xp.minimized ? 1.0 : 0.0;
        g_xp.placed = true;
        if (f >> sc) g_xp.scale = (sc < 0.7f ? 0.7f : (sc > 1.8f ? 1.8f : sc));   // older files omit it
    }
}

long long XpRatePerHour(long long gained, long long since, long long now) {
    long long el = now - since;
    return (gained > 0 && since > 0 && el > 3000) ? (gained * 3600000LL) / el : 0;
}

HWND FindGameWindow(DWORD pid) {
    return (HWND)rtx::launcher::dock::GameWindowHandle(pid);
}

// Every projected point registers its clip-space depth by screen position, so the marker builder
// can attach depth to lines and quads without threading it through each helper.
static thread_local std::unordered_map<std::uint64_t, float> t_ptZ;
static inline std::uint64_t PtKey(float x, float y) {
    std::uint32_t a, b; std::memcpy(&a, &x, 4); std::memcpy(&b, &y, 4);
    return ((std::uint64_t)a << 32) | b;
}
static inline float ZAt(float x, float y) {
    auto it = t_ptZ.find(PtKey(x, y));
    return it == t_ptZ.end() ? -1.0f : it->second;
}

bool WorldToScreen(const float* m, float vpX, float vpY, float vpW, float vpH,
                   float x, float y, float z, float& sx, float& sy) {
    // Double precision: fine world coordinates run into the millions, and the row sums cancel
    // to a few thousand, which costs float32 about 1e-5 of depth.
    const double dx = x, dy = y, dz = z;
    const double w = m[3] * dx + m[11] * dy + m[7] * dz + m[15];
    if (w <= 1.0) return false;                           // behind camera
    const double nx = (m[0] * dx + m[8] * dy + m[4] * dz + m[12]) / w;
    const double ny = (m[1] * dx + m[9] * dy + m[5] * dz + m[13]) / w;
    const double cx = vpW / 2.0, cy = vpH / 2.0;
    sx = (float)(nx * cx - nx * 2.0 + cx + vpX);
    sy = (float)(-(ny * cy) + ny + cy + vpY);
    const float nz = (float)((m[2] * dx + m[10] * dy + m[6] * dz + m[14]) / w);
    if (t_ptZ.size() < 200000) t_ptZ[PtKey(sx, sy)] = nz;
    return true;
}

constexpr float kNearW = 32.0f;   // in front of WorldToScreen's w<=1 cull

inline float ProjW(const float* m, const float* p) {
    return (float)(m[3] * (double)p[0] + m[11] * (double)p[1] + m[7] * (double)p[2] + m[15]);
}

// False = fully behind.
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

// Perimeter order, n <= 8; Sutherland-Hodgman on one plane (at most n+1 verts out). 0 = fully behind.
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
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

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

    if (cfg.grid && f.grid_r > 0) {
        const int R = f.grid_r;
        const int T = 2 * R + 1;          // tiles per axis
        static thread_local std::vector<float> px, py;
        static thread_local std::vector<char>  vis;
        px.assign((size_t)T * T * 4, 0.0f); py.assign((size_t)T * T * 4, 0.0f);
        vis.assign((size_t)T * T * 4, 0);
        // fine-z = 32 * cache surface height (see CacheReader::AbsHeightLocked)
        const float kHScale = 32.0f;
        const std::int16_t kNoH = -32768;
        bool haveH = (int)f.heights.size() == T * T * 4;
        const bool haveF = (int)f.heights_fine.size() == T * T * 4;   // the game's own terrain, exact
        static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
        for (int tgx = 0; tgx < T; ++tgx) {
            for (int tgy = 0; tgy < T; ++tgy) {
                for (int c = 0; c < 4; ++c) {
                    size_t i = ((size_t)tgx * T + tgy) * 4 + c;
                    float wx = (float)(f.player_tx - R + tgx + CX[c]) * 512.0f;
                    float wy = (float)(f.player_ty - R + tgy + CY[c]) * 512.0f;
                    const bool liveC = haveF && f.heights_fine[i] != INT32_MIN;
                    if (!liveC && haveH && f.heights[i] == kNoH) continue;   // no data at all: leave invisible
                    float cz = liveC ? (float)f.heights_fine[i]
                             : (haveH && f.heights[i] != kNoH) ? kHScale * (float)f.heights[i] : f.player_z;
                    float sx, sy;
                    if (WorldToScreen(f.matrix, vpX, vpY, vpW, vpH, wx, wy, cz, sx, sy) &&
                        sx > -(float)W && sx < 2.0f * W && sy > -(float)H && sy < 2.0f * H) {
                        px[i] = sx; py[i] = sy; vis[i] = 1;
                    }
                }
            }
        }
        Pen pen(Color(175, 150, 215, 255), 1.3f);
        Pen center(Color(255, 150, 250, 150), 2.4f);     // player's own tile
        Pen wallPen(Color(255, 95, 175, 255), 2.6f);
        SolidBrush blockFill(Color(125, 240, 80, 80));
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
                const size_t base = ((size_t)tgx * T + tgy) * 4;
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
                if (fl & 0x08) g.DrawLine(&wallPen, px[a], py[a], px[d], py[d]);  // W
                if (fl & 0x04) g.DrawLine(&wallPen, px[b], py[b], px[c], py[c]);  // E
                if (fl & 0x02) g.DrawLine(&wallPen, px[a], py[a], px[b], py[b]);  // S
                if (fl & 0x01) g.DrawLine(&wallPen, px[d], py[d], px[c], py[c]);  // N
            }
        }
    }

    FontFamily ff(L"Segoe UI");
    Font font(&ff, 11.0f, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(Color(255, 245, 245, 245));
    SolidBrush pill(Color(170, 14, 16, 24));
    Color kindCol[5] = { Color(255, 90, 200, 235),   // 0 object
                         Color(255, 245, 210, 80),   // 1 npc
                         Color(255, 90, 220, 120),   // 2 player
                         Color(255, 245, 165, 60),   // 3 special
                         Color(255, kTxtR, kTxtG, kTxtB) };  // 4 Scene object outline
    auto wanted = [&](int kind) {
        return kind == 4 ? true : kind == 5 ? false : kind == 0 ? cfg.objects : kind == 1 ? cfg.npcs : kind == 2 ? cfg.players : cfg.specials;
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
    for (const auto& p : f.points) {
        if (!wanted(p.kind)) continue;
        // Live model AABB: 3D box, corner bits 0=east 1=north 2=up.
        if (p.has_box3d) {
            float ex[2] = { p.bmin[0], p.bmax[0] };
            float ny[2] = { p.bmin[1], p.bmax[1] };
            float uz[2] = { p.bmin[2], p.bmax[2] };
            Gdiplus::PointF v[8]; bool okAll = true;
            for (int c = 0; c < 8; ++c) {
                float sx, sy;
                if (!WorldToScreen(f.matrix, vpX, vpY, vpW, vpH,
                                   ex[c & 1], ny[(c >> 1) & 1], uz[(c >> 2) & 1], sx, sy) ||
                    sx < -3.0f * W || sx > 4.0f * W || sy < -3.0f * H || sy > 4.0f * H) { okAll = false; break; }
                v[c] = Gdiplus::PointF(sx, sy);
            }
            if (okAll) {
                Color kc = kindCol[p.kind];
                Pen edge(Color(235, kc.GetR(), kc.GetG(), kc.GetB()), 1.8f);
                SolidBrush baseFill(Color(40, kc.GetR(), kc.GetG(), kc.GetB()));
                Gdiplus::PointF base[4] = { v[0], v[1], v[3], v[2] };   // up=min face
                g.FillPolygon(&baseFill, base, 4);
                static const int E[12][2] = { {0,1},{1,3},{3,2},{2,0},
                                              {4,5},{5,7},{7,6},{6,4},
                                              {0,4},{1,5},{2,6},{3,7} };
                for (auto& e : E) g.DrawLine(&edge, v[e[0]], v[e[1]]);
                continue;
            }
        }
        // Footprint extruded by box_h: corners 0-3 = ground, 4-7 = top.
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
                SolidBrush fill(Color(46, 90, 200, 235));
                g.FillPolygon(&fill, poly, 4);               // ground face only
                Pen edge(Color(230, 120, 215, 245), 2.0f);
                g.DrawPolygon(&edge, poly, 4);
                if (nv == 8) {
                    Gdiplus::PointF top[4] = { poly[4], poly[5], poly[6], poly[7] };
                    g.DrawPolygon(&edge, top, 4);
                    for (int i = 0; i < 4; ++i) g.DrawLine(&edge, poly[i], poly[4 + i]);
                }
                continue;
            }
        }
        float sx, sy;
        if (!project(p, sx, sy)) continue;
        SolidBrush dot(kindCol[p.kind]);
        if (p.kind == 0)      g.FillRectangle(&dot, sx - 2.5f, sy - 2.5f, 5.0f, 5.0f);
        else if (p.kind == 3) g.FillEllipse(&dot, sx - 4.0f, sy - 4.0f, 8.0f, 8.0f);
        else                  g.FillEllipse(&dot, sx - 3.0f, sy - 3.0f, 6.0f, 6.0f);
    }
    std::vector<RectF> placed;
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

    std::vector<rtx::reader::OverlayPoint> dfHls;
    {
        static std::map<std::string, std::pair<rtx::reader::OverlayPoint, ULONGLONG>> s_hold;
        constexpr ULONGLONG kHoldMs = 700;
        const ULONGLONG tnow = GetTickCount64();
        dfHls = f.highlights;
        std::vector<std::string> keys; keys.reserve(dfHls.size());
        for (const auto& hp : dfHls) { keys.push_back(HighlightHoldKey(hp)); s_hold[keys.back()] = { hp, tnow }; }
        for (auto it = s_hold.begin(); it != s_hold.end();) {
            if (tnow - it->second.second > kHoldMs) { it = s_hold.erase(it); continue; }
            bool present = false;
            for (const auto& k : keys) if (k == it->first) { present = true; break; }
            if (!present) dfHls.push_back(it->second.first);
            ++it;
        }
    }
    if (!dfHls.empty()) {
        float pulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1000) / 1000.0 * 6.2831853));
        SolidBrush goldText(Color(255, 255, 226, 128));
        for (const auto& hp : dfHls) {
            if (hp.has_box3d) {
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
        p->magic   = marker::kMagic;       // written last: signals initialised
        pid = target;
        return true;
    }
    void close() {
        if (p)   { UnmapViewOfFile(p); p = nullptr; }
        if (map) { CloseHandle(map); map = nullptr; }
        pid = 0;
    }
};
std::map<DWORD, MarkerOut> g_marker_outs;   // one channel per client; render thread only

void PublishMarkers(const Config& cfg, const rtx::reader::OverlayFrame* f, int W, int H,
                    float flashAlpha = 0.0f,
                    const std::vector<marker::Command>* widgets = nullptr) {
    auto& out = g_marker_outs[cfg.pid];
    if (!out.ensure(cfg.pid)) return;
    marker::Share* sh = out.p;

    bool hasGuides = false;   // gate only; drawing uses the frame's resolved guides
    std::vector<UiHighlight> uihls;
    std::vector<UiLabel> uilbls;
    std::map<int, CenterBanner> ctext;
    std::vector<PanelBox> pviz;
    std::vector<PuzzleCell> pcells;
    std::vector<KnotCell> kcells;
    std::vector<SkillBar> sbars;
    { std::lock_guard<std::mutex> lk(g_mu);
      auto git = g_guides.find(cfg.pid);
      hasGuides = (git != g_guides.end() && !git->second.empty());
      auto uit = g_uiHighlights.find(cfg.pid);
      if (uit != g_uiHighlights.end()) uihls = uit->second;
      auto ult = g_uiLabels.find(cfg.pid);
      if (ult != g_uiLabels.end()) uilbls = ult->second;
      auto ctit = g_centerTexts.find(cfg.pid);
      if (ctit != g_centerTexts.end()) ctext = ctit->second;
      auto pit = g_panelViz.find(cfg.pid);
      if (pit != g_panelViz.end()) pviz = pit->second;
      auto pcit = g_puzzleCells.find(cfg.pid);
      if (pcit != g_puzzleCells.end()) pcells = pcit->second;
      auto kcit = g_knotCells.find(cfg.pid);
      if (kcit != g_knotCells.end()) kcells = kcit->second;
      auto sbit = g_skillBars.find(cfg.pid);
      if (sbit != g_skillBars.end() && now_ms() - sbit->second.at_ms <= kSkillBarsTtlMs)
          sbars = sbit->second.bars; }

    std::vector<rtx::reader::OverlayPoint> hls;
    {
        static std::map<DWORD, std::map<std::string, std::pair<rtx::reader::OverlayPoint, ULONGLONG>>> s_hiHold;
        constexpr ULONGLONG kHiHoldMs = 700;
        const ULONGLONG tnow = GetTickCount64();
        auto& hold = s_hiHold[cfg.pid];
        if (f) {
            hls = f->highlights;
            std::vector<std::string> keys; keys.reserve(hls.size());
            for (const auto& hp : hls) { keys.push_back(HighlightHoldKey(hp)); hold[keys.back()] = { hp, tnow }; }
            for (auto it = hold.begin(); it != hold.end();) {
                if (tnow - it->second.second > kHiHoldMs) { it = hold.erase(it); continue; }
                bool present = false;
                for (const auto& k : keys) if (k == it->first) { present = true; break; }
                if (!present) hls.push_back(it->second.first);
                ++it;
            }
        } else hold.clear();
    }

    bool wantContent = flashAlpha > 0.0f || !uihls.empty() || !uilbls.empty() || !ctext.empty() || !pviz.empty() || !pcells.empty() || !kcells.empty() || !sbars.empty() ||
                       (widgets && !widgets->empty()) ||
                       (f && (cfg.enabled || cfg.markers || cfg.nameplates || !hls.empty() || hasGuides));
    if (!wantContent) {
        std::uint32_t s = sh->seq + 1;
        sh->seq = s; MemoryBarrier();
        sh->count = 0; sh->visible = 0;
        MemoryBarrier(); sh->seq = s + 1;
        return;
    }

    float gvScale = 1.0f;
    if (f && f->gv_w > 0 && f->lc_w > 0) {
        const float s = (float)W / (float)f->lc_w;
        if (s > 0.2f && s < 5.0f && (s < 0.995f || s > 1.005f)) gvScale = s;
    }
    float vpW = (f && f->gv_w > 0) ? (float)f->gv_w * gvScale : (float)W;
    float vpH = (f && f->gv_h > 0) ? (float)f->gv_h * gvScale : (float)H;
    if (vpW > (float)W) vpW = (float)W;
    if (vpH > (float)H) vpH = (float)H;
    float vpX = (f && f->gv_w > 0) ? (float)f->gv_x * gvScale : 0.0f;
    float vpY = (f && f->gv_h > 0) ? (float)f->gv_y * gvScale : 0.0f;
    if (vpX < 0.0f) vpX = 0.0f;
    if (vpY < 0.0f) vpY = 0.0f;
    if (vpX + vpW > (float)W) vpX = (float)W - vpW;
    if (vpY + vpH > (float)H) vpY = (float)H - vpH;

    static std::vector<marker::Command> cmds;   // render thread only
    cmds.clear();
    t_ptZ.clear();
    auto push = [&](const marker::Command& c0) {
        if (cmds.size() >= marker::kMaxCmds) return;
        marker::Command c = c0;
        c.z0 = c.z1 = c.z2 = c.z3 = -1.0f;
        if (c.type == marker::kLine) {
            c.z0 = ZAt(c.x0, c.y0); c.z1 = ZAt(c.x1, c.y1);
            if (c.z0 < 0.0f || c.z1 < 0.0f) c.z0 = c.z1 = -1.0f;
        } else if (c.type == marker::kFillQuad) {
            c.z0 = ZAt(c.x0, c.y0); c.z1 = ZAt(c.x1, c.y1); c.z2 = ZAt(c.x2, c.y2); c.z3 = ZAt(c.x3, c.y3);
            if (c.z0 < 0.0f || c.z1 < 0.0f || c.z2 < 0.0f || c.z3 < 0.0f) c.z0 = c.z1 = c.z2 = c.z3 = -1.0f;
        }
        cmds.push_back(c);
    };
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

    if (f && cfg.enabled && cfg.grid && f->grid_r > 0) {
        const int R = f->grid_r;
        const int T = 2 * R + 1;
        static thread_local std::vector<float> px, py, wz;
        static thread_local std::vector<char>  vis;
        const size_t NC = (size_t)T * T * 4;
        px.assign(NC, 0.0f); py.assign(NC, 0.0f); wz.assign(NC, 0.0f); vis.assign(NC, 0);
        const float kHScale = 32.0f;       // fine-z = 32 * cache height
        const std::int16_t kNoH = -32768;
        bool haveH = f->heights.size() == NC;
        const bool haveF = f->heights_fine.size() == NC;   // the game's own terrain, exact
        static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
        // vis: 0 = unusable, 1 = drawable, 2 = behind the near plane (edges to it are clipped, not dropped)
        for (int tgx = 0; tgx < T; ++tgx)
            for (int tgy = 0; tgy < T; ++tgy)
                for (int c = 0; c < 4; ++c) {
                    size_t i = ((size_t)tgx * T + tgy) * 4 + c;
                    float wx = (float)(f->player_tx - R + tgx + CX[c]) * 512.0f;
                    float wy = (float)(f->player_ty - R + tgy + CY[c]) * 512.0f;
                    const bool liveC = haveF && f->heights_fine[i] != INT32_MIN;
                    if (!liveC && haveH && f->heights[i] == kNoH) continue;
                    float cz = liveC ? (float)f->heights_fine[i]
                             : (haveH && f->heights[i] != kNoH) ? kHScale * (float)f->heights[i] : f->player_z;
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
                    q.r = 8; q.g = 6; q.b = 8; q.a = 62;
                    push(q);
                }
        auto tileShown = [&](int tgx, int tgy, bool& shown, bool& self) {
            shown = self = false;
            if (tgx < 0 || tgy < 0 || tgx >= T || tgy >= T) return;
            if (flagAt(tgx, tgy) & 0x10) return;             // blocked: no lattice
            shown = true;
            self = (tgx == R && tgy == R);
        };
        auto blockedAt = [&](int tgx, int tgy) {
            if (tgx < 0 || tgy < 0 || tgx >= T || tgy >= T) return false;  // outside the ring: unknown
            return (flagAt(tgx, tgy) & 0x10) != 0;
        };
        auto cornerWorld = [&](size_t i, float* out) {
            const size_t tile = i / 4; const int c = (int)(i % 4);
            out[0] = (float)(f->player_tx - R + (int)(tile / T) + CX[c]) * 512.0f;
            out[1] = (float)(f->player_ty - R + (int)(tile % T) + CY[c]) * 512.0f;
            out[2] = wz[i];
        };
        auto edge = [&](size_t c0, size_t c1, bool self, bool border) {
            bool clipped = (vis[c0] == 1 && vis[c1] == 2) || (vis[c0] == 2 && vis[c1] == 1);
            if (!clipped && !(vis[c0] == 1 && vis[c1] == 1)) return;
            float x0 = px[c0], y0 = py[c0], x1 = px[c1], y1 = py[c1];
            if (clipped) {
                float A[3], B[3];
                cornerWorld(c0, A); cornerWorld(c1, B);
                if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH, A, B, x0, y0, x1, y1)) return;
            }
            if (self) {
                line(x0, y0, x1, y1, 3.8f, 8, 24, 10, 165);
                line(x0, y0, x1, y1, 2.0f, 150, 250, 150, 255);
            } else if (border) {
                line(x0, y0, x1, y1, 3.4f, 30, 12, 2, 175);
                line(x0, y0, x1, y1, 1.8f, 255, 178, 64, 235);
            } else {
                line(x0, y0, x1, y1, 2.0f, 6, 12, 20, 105);
                line(x0, y0, x1, y1, 1.0f, 170, 225, 255, 175);
            }
        };
        for (int pass = 0; pass < 2; ++pass)
            for (int tgx = 0; tgx < T; ++tgx)
                for (int tgy = 0; tgy < T; ++tgy) {
                    bool shown = false, self = false;
                    tileShown(tgx, tgy, shown, self);
                    if (!shown || self != (pass == 1)) continue;
                    const size_t base = ((size_t)tgx * T + tgy) * 4;
                    auto neighbourDrawsShared = [&](int nx, int ny, size_t c0, size_t c1, size_t n0, size_t n1) {
                        bool ns, nself;
                        tileShown(nx, ny, ns, nself);
                        if (!ns) return false;
                        const size_t nb = ((size_t)nx * T + ny) * 4;
                        return wz[base + c0] == wz[nb + n0] && wz[base + c1] == wz[nb + n1];
                    };
                    edge(base + 0, base + 1, self, blockedAt(tgx, tgy - 1));   // S: owned
                    edge(base + 3, base + 0, self, blockedAt(tgx - 1, tgy));   // W: owned
                    if (self || !neighbourDrawsShared(tgx + 1, tgy, 1, 2, 0, 3))
                        edge(base + 1, base + 2, self, blockedAt(tgx + 1, tgy));   // E
                    if (self || !neighbourDrawsShared(tgx, tgy + 1, 3, 2, 0, 1))
                        edge(base + 2, base + 3, self, blockedAt(tgx, tgy + 1));   // N
                }
        // wall edges on top (a=SW b=SE c=NE d=NW)
        for (int tgx = 0; tgx < T; ++tgx)
            for (int tgy = 0; tgy < T; ++tgy) {
                int fl = flagAt(tgx, tgy);
                if (!(fl & 0x0f)) continue;
                if ((fl & 0x10) && cfg.walk_only) continue;
                const size_t base = ((size_t)tgx * T + tgy) * 4;
                size_t a = base + 0, b = base + 1, c = base + 2, d = base + 3;
                if (vis[a] != 1 || vis[b] != 1 || vis[c] != 1 || vis[d] != 1) continue;
                auto wall = [&](size_t e0, size_t e1) {
                    line(px[e0], py[e0], px[e1], py[e1], 4.4f, 4, 10, 18, 180);
                    line(px[e0], py[e0], px[e1], py[e1], 2.6f, 120, 190, 255, 255);
                };
                if (fl & 0x08) wall(a, d);
                if (fl & 0x04) wall(b, c);
                if (fl & 0x02) wall(a, b);
                if (fl & 0x01) wall(d, c);
            }
    }

    struct KC { int r, g, b; };
    const KC kindCol[5] = { {90,200,235}, {245,210,80}, {90,220,120}, {245,165,60}, {kTxtR,kTxtG,kTxtB} };
    auto wanted = [&](int kind) {
        return kind == 4 ? true : kind == 5 ? cfg.true_tile : kind == 0 ? cfg.objects : kind == 1 ? cfg.npcs : kind == 2 ? cfg.players : cfg.specials;
    };
    if (f && cfg.enabled) for (const auto& p : f->points) {
        if (!wanted(p.kind)) continue;
        const KC kc = kindCol[p.kind >= 0 && p.kind < 5 ? p.kind : 0];
        if (p.kind == 5) {                                   // true-tile outline, flat on the ground
            if (!p.has_box) continue;
            if (!p.is_self && !(p.src == 1 ? cfg.npcs : cfg.players)) continue;
            const KC tc = p.is_self ? KC{ 255, 110, 230 } : kindCol[p.src == 1 ? 1 : 2];
            float wc[4][3];
            for (int i = 0; i < 4; ++i) { wc[i][0] = p.box[i * 3]; wc[i][1] = p.box[i * 3 + 1]; wc[i][2] = p.box[i * 3 + 2]; }
            float ex0[4], ey0[4], ex1[4], ey1[4]; int ns = 0; bool onscr = false;
            for (int i = 0; i < 4; ++i) {
                float ax, ay, bx, by;
                if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH, wc[i], wc[(i + 1) & 3], ax, ay, bx, by)) continue;
                ex0[ns] = ax; ey0[ns] = ay; ex1[ns] = bx; ey1[ns] = by; ++ns;
                onscr = onscr || (ax > -2.0f * W && ax < 3.0f * W && ay > -2.0f * H && ay < 3.0f * H)
                              || (bx > -2.0f * W && bx < 3.0f * W && by > -2.0f * H && by < 3.0f * H);
            }
            if (!ns || !onscr) continue;
            for (int i = 0; i < ns; ++i) line(ex0[i], ey0[i], ex1[i], ey1[i], 4.6f, 8, 4, 10, 170);   // dark underlay
            for (int i = 0; i < ns; ++i) line(ex0[i], ey0[i], ex1[i], ey1[i], 2.6f, tc.r, tc.g, tc.b, 245);
            continue;
        }
        if (p.has_box3d) {
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
        if (p.kind == 0 && p.has_box) {
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

    if (f && cfg.nameplates) {
        const float npTextPx = 11.0f;
        const float cellH  = 34.0f * (npTextPx / 21.0f);     // atlas cell -> screen px
        // Pill metrics must match Composite.cpp DrawLabel or stacking drifts.
        const float pillH  = cellH * 0.78f + 10.0f;          // + padY*2  (padY 5)
        const float padX   = 8.0f;
        const float avgAdv = npTextPx * 0.62f;               // over-estimate of glyph advance
        const float slotH  = pillH + 3.0f;
        const float gap    = 2.0f;
        const int   kMaxRows = 12;                           // per tile; extra -> "+N more"
        const float kMaxRise = slotH * 5.0f;                 // drop a label rather than push it further

        auto npWanted = [&](int kind) {
            return kind == 0 ? cfg.np_objects : kind == 1 ? cfg.np_npcs
                 : kind == 2 ? cfg.np_players : false;        // specials excluded
        };
        auto pillW = [&](const std::string& s) { return padX * 2.0f + (float)s.size() * avgAdv; };

        struct Ent { int kind; float wz, head_z; const std::string* label; };
        struct Bucket { int tx, ty, dist; float cx, cy, cwx, cwy; float repZ; bool anyHead; std::vector<Ent> ents; };
        std::map<long long, int> bmap;
        std::vector<Bucket> buckets;
        const int npR = cfg.np_range > 0 ? cfg.np_range : 20;
        for (const auto& p : f->points) {
            if (p.is_self || p.label.empty()) continue;
            if (p.kind == 2) {   // picked players override the master toggle
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
            if (p.head_z != 0.0f) { if (!b.anyHead || p.head_z > b.repZ) b.repZ = p.head_z; b.anyHead = true; }
            else if (!b.anyHead && (b.ents.size() == 1 || p.wz > b.repZ)) b.repZ = p.wz;
        }

        std::vector<int> keep; keep.reserve(buckets.size());
        for (int i = 0; i < (int)buckets.size(); ++i) {
            Bucket& b = buckets[i];
            float sx, sy;
            if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, b.cwx, b.cwy, b.repZ, sx, sy)) continue;
            if (!b.anyHead) sy -= 26.0f;     // no model box: lift off the feet
            if (sx < -120 || sx > W + 120 || sy < -160 || sy > H + 160) continue;
            b.cx = sx; b.cy = sy;
            std::sort(b.ents.begin(), b.ents.end(),
                      [](const Ent& a, const Ent& c) { return *a.label < *c.label; });
            keep.push_back(i);
        }

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

        auto kindRGB = [&](int kind, int& r, int& g, int& b) {
            switch (kind) {
                case 2: r = 120; g = 235; b = 140; break;    // player
                case 1: r = 245; g = 210; b = 90;  break;    // NPC
                case 0: r = 120; g = 215; b = 245; break;    // object
                default: r = 205; g = 205; b = 210; break;   // overflow
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
                cy = bestTop - pillH * 0.5f - gap;
            }
            if (row.cy - cy > kMaxRise) continue;   // buried far label: drop rather than tower
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

    if (f && !hls.empty()) {
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
        for (const auto& hp : hls) {
            if (hp.has_box3d && hp.label.empty()) {
                if (box3d(hp, 1.4f, kTxtR, kTxtG, kTxtB, 190)) continue;
            }
            float sx, sy;
            if (!WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, hp.wx, hp.wy, hp.wz, sx, sy)) continue;
            if (sx < -60 || sx > W + 60 || sy < -60 || sy > H + 60) continue;
            const bool boxed = hp.has_box3d && box3d(hp, 1.6f, kAccR, kAccG, kAccB, 235);
            if (!boxed) {
                float rad = 14.0f + 3.0f * pulse;
                line(sx - rad, sy - rad, sx + rad, sy - rad, 2.4f, kAccR, kAccG, kAccB, 255);
                line(sx + rad, sy - rad, sx + rad, sy + rad, 2.4f, kAccR, kAccG, kAccB, 255);
                line(sx + rad, sy + rad, sx - rad, sy + rad, 2.4f, kAccR, kAccG, kAccB, 255);
                line(sx - rad, sy + rad, sx - rad, sy - rad, 2.4f, kAccR, kAccG, kAccB, 255);
                dot(sx, sy, 2.5f, kAccR, kAccG, kAccB, 235);
            }
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
                t.x1 = 10.5f;
                t.r = kAccR; t.g = kAccG; t.b = kAccB; t.a = 230;
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

    if (f && cfg.markers) {
        auto mlist = rtx::markers::Snapshot(cfg.pid);
        const std::int16_t kNo = -32768;
        for (const auto& m : mlist) {
            if (m.plane != f->plane) continue;
            int gx = ((m.region >> 8) << 6) + m.lx;
            int gy = ((m.region & 0xFF) << 6) + m.ly;
            int adx = gx - f->player_tx; if (adx < 0) adx = -adx;
            int ady = gy - f->player_ty; if (ady < 0) ady = -ady;
            if ((adx > ady ? adx : ady) > 128) continue;
            int cr = (m.color >> 16) & 0xFF, cg = (m.color >> 8) & 0xFF, cb = m.color & 0xFF;
            std::int16_t ch[4];
            rtx::cache::TileCornerHeights(gx, gy, f->plane, ch);
            std::int32_t lch[4]; const bool liveOk = rtx::reader::LiveCornerHeights(f->pid, gx, gy, f->plane, lch);
            auto cz = [&](int c) { return liveOk ? (float)lch[c] : (ch[c] == kNo) ? f->player_z : 32.0f * (float)ch[c]; };
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
            auto fillPoly = [&](const float* px2, const float* py2, int n, int r2, int g2, int b2,
                                bool bright) {
                for (int i = 1; i + 1 < n; i += 2) {
                    int j = (i + 2 < n) ? i + 2 : i + 1;
                    marker::Command q{}; q.type = marker::kFillQuad;
                    q.x0 = px2[0]; q.y0 = py2[0]; q.x1 = px2[i]; q.y1 = py2[i];
                    q.x2 = px2[i + 1]; q.y2 = py2[i + 1]; q.x3 = px2[j]; q.y3 = py2[j];
                    if (bright) {
                        q.r = (std::uint8_t)((r2 + kSurfR) / 2); q.g = (std::uint8_t)((g2 + kSurfG) / 2);
                        q.b = (std::uint8_t)((b2 + kSurfB) / 2); q.a = 125;
                    } else {
                        q.r = (std::uint8_t)MixSurf(r2, kSurfR); q.g = (std::uint8_t)MixSurf(g2, kSurfG);
                        q.b = (std::uint8_t)MixSurf(b2, kSurfB); q.a = 110;
                    }
                    push(q);
                }
            };
            if (nq >= 3 && any) {
                if (m.color2) {
                    const int c2r = (m.color2 >> 16) & 0xFF, c2g = (m.color2 >> 8) & 0xFF, c2b = m.color2 & 0xFF;
                    const float triSE[3][3] = { { wc[0][0], wc[0][1], wc[0][2] },
                                                { wc[1][0], wc[1][1], wc[1][2] },
                                                { wc[2][0], wc[2][1], wc[2][2] } };
                    const float triNW[3][3] = { { wc[0][0], wc[0][1], wc[0][2] },
                                                { wc[2][0], wc[2][1], wc[2][2] },
                                                { wc[3][0], wc[3][1], wc[3][2] } };
                    float tx[9], ty[9];
                    int tn = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, triSE, 3, tx, ty);
                    if (tn >= 3) fillPoly(tx, ty, tn, c2r, c2g, c2b, true);
                    tn = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, triNW, 3, tx, ty);
                    if (tn >= 3) fillPoly(tx, ty, tn, cr, cg, cb, true);
                } else {
                    fillPoly(qx, qy, nq, cr, cg, cb, false);
                }
                // Two-tone outline: edge i runs corner i -> i+1 (SW,SE,NE,NW); S+E take the second tone, N+W the primary.
                if (m.color2) {
                    const int c2r = (m.color2 >> 16) & 0xFF, c2g = (m.color2 >> 8) & 0xFF, c2b = m.color2 & 0xFF;
                    for (int i = 0; i < 4; ++i) {
                        float ax2, ay2, bx2, by2;
                        if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH,
                                                wc[i], wc[(i + 1) % 4], ax2, ay2, bx2, by2)) continue;
                        const bool second = i < 2;
                        line(ax2, ay2, bx2, by2, 3.0f, kInkR, kInkG, kInkB, 140);
                        line(ax2, ay2, bx2, by2, 1.8f,
                             second ? c2r : cr, second ? c2g : cg, second ? c2b : cb, 255);
                    }
                    float sx2, sy2, ex2, ey2;   // diagonal seam
                    if (ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH, wc[0], wc[2], sx2, sy2, ex2, ey2))
                        line(sx2, sy2, ex2, ey2, 1.4f, kInkR, kInkG, kInkB, 170);
                } else
                    for (int i = 0; i < nq; ++i) {
                        line(qx[i], qy[i], qx[(i + 1) % nq], qy[(i + 1) % nq], 3.0f, kInkR, kInkG, kInkB, 140);
                        line(qx[i], qy[i], qx[(i + 1) % nq], qy[(i + 1) % nq], 1.6f, cr, cg, cb, 255);
                    }
            }
            if (!m.label.empty()) {
                float zc = (zSW + zSE + zNE + zNW) * 0.25f;
                float cx, cy;
                if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH,
                                  (gx + 0.5f) * 512.f, (gy + 0.5f) * 512.f, zc, cx, cy) &&
                    cx > -200.f && cx < W + 200.f && cy > -120.f && cy < H + 120.f) {
                    marker::Command t{}; t.type = marker::kText;
                    t.x0 = cx;
                    t.y0 = cy - 22.0f;
                    t.x1 = 14.0f;             // glyph height px
                    t.r = (std::uint8_t)cr; t.g = (std::uint8_t)cg; t.b = (std::uint8_t)cb; t.a = 235;
                    int n = (int)m.label.size();
                    if (n > marker::kTextMax) {
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

    if (f && f->guide_path.size() >= 4) {
        const std::int16_t kNo = -32768;
        const std::size_t nT = f->guide_path.size() / 2;
        std::vector<float> cz((std::size_t)nT * 4);
        for (std::size_t i = 0; i < nT; ++i) {
            std::int16_t ch[4];
            rtx::cache::TileCornerHeights(f->guide_path[i * 2], f->guide_path[i * 2 + 1], f->plane, ch);
            std::int32_t lch[4]; const bool liveOk = rtx::reader::LiveCornerHeights(f->pid, f->guide_path[i * 2], f->guide_path[i * 2 + 1], f->plane, lch);
            for (int c = 0; c < 4; ++c)
                cz[i * 4 + c] = liveOk ? (float)lch[c] : (ch[c] == kNo) ? f->player_z : 32.0f * (float)ch[c];
        }
        for (std::size_t i = 0; i + 1 < nT; ++i) {
            int dx = f->guide_path[(i + 1) * 2]     - f->guide_path[i * 2];
            int dy = f->guide_path[(i + 1) * 2 + 1] - f->guide_path[i * 2 + 1];
            // corner order SW(0) SE(1) NE(2) NW(3); (own, next) pairs per direction
            int a0, a1, b0, b1;
            if      (dx ==  1) { a0 = 1; b0 = 0; a1 = 2; b1 = 3; }   // east
            else if (dx == -1) { a0 = 0; b0 = 1; a1 = 3; b1 = 2; }   // west
            else if (dy ==  1) { a0 = 3; b0 = 0; a1 = 2; b1 = 1; }   // north
            else if (dy == -1) { a0 = 0; b0 = 3; a1 = 1; b1 = 2; }   // south
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
            for (int c = 1; c + 1 < nq; c += 2) {        // fan fill
                int j = (c + 2 < nq) ? c + 2 : c + 1;
                marker::Command q{}; q.type = marker::kFillQuad;
                q.x0 = qx[0]; q.y0 = qy[0]; q.x1 = qx[c]; q.y1 = qy[c];
                q.x2 = qx[c + 1]; q.y2 = qy[c + 1]; q.x3 = qx[j]; q.y3 = qy[j];
                q.r = 255; q.g = 215; q.b = 60; q.a = 70;
                push(q);
            }
            for (int c = 0; c < nq; ++c)
                line(qx[c], qy[c], qx[(c + 1) % nq], qy[(c + 1) % nq], 1.6f, 30, 24, 8, 130);
        }
    }

    struct GuideLbl { float cx, cy; int nl, mc; const std::string* text; int r, g, b; bool hazard; };
    std::vector<GuideLbl> glbls;
    if (f) for (const auto& p : f->guides) {
        // corners: base ring 0-3 (SW,SE,NE,NW), top ring 4-7
        float wc[8][3]; int nv;
        if (p.has_box3d) {
            nv = 8;
            static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
            for (int i = 0; i < 8; ++i) {
                wc[i][0] = CX[i & 3] ? p.bmax[0] : p.bmin[0];
                wc[i][1] = CY[i & 3] ? p.bmax[1] : p.bmin[1];
                wc[i][2] = (i >> 2) ? p.bmax[2] : p.bmin[2];
            }
        } else {
            nv = (p.box_h > 0.f) ? 8 : 4;
            for (int i = 0; i < nv; ++i) {
                wc[i][0] = p.box[(i & 3) * 3];
                wc[i][1] = p.box[(i & 3) * 3 + 1];
                wc[i][2] = p.box[(i & 3) * 3 + 2] + ((i & 4) ? p.box_h : 0.f);
            }
        }
        // rgb 0 = objective accent; a caller's rgb passes through unchanged (it also gates the arrow and declutter priority).
        const int mr = p.rgb ? ((p.rgb >> 16) & 255) : kAccR;
        const int mg = p.rgb ? ((p.rgb >> 8) & 255)  : kAccG;
        const int mb = p.rgb ? (p.rgb & 255)         : kAccB;
        float qx[9], qy[9];
        int nq = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, wc, 4, qx, qy);
        bool onscr = false;
        for (int i = 0; i < nq; ++i)
            if (qx[i] > -2.f * W && qx[i] < 3.f * W && qy[i] > -2.f * H && qy[i] < 3.f * H) onscr = true;
        if (nq >= 3 && onscr) {
            const int sr = MixSurf(mr, kSurfR), sg = MixSurf(mg, kSurfG), sb = MixSurf(mb, kSurfB);
            auto gfan = [&](const float* fx, const float* fy, int n, int r3, int g3, int b3) {
                for (int i = 1; i + 1 < n; i += 2) {
                    int j = (i + 2 < n) ? i + 2 : i + 1;
                    marker::Command q{}; q.type = marker::kFillQuad;
                    q.x0 = fx[0]; q.y0 = fy[0]; q.x1 = fx[i]; q.y1 = fy[i];
                    q.x2 = fx[i + 1]; q.y2 = fy[i + 1]; q.x3 = fx[j]; q.y3 = fy[j];
                    q.r = (std::uint8_t)r3; q.g = (std::uint8_t)g3; q.b = (std::uint8_t)b3; q.a = 96;
                    push(q);
                }
            };
            if (p.rgb2 && p.box_h <= 0.f) {
                const int r2c = (p.rgb2 >> 16) & 255, g2c = (p.rgb2 >> 8) & 255, b2c = p.rgb2 & 255;
                const float tSE[3][3] = { { wc[0][0], wc[0][1], wc[0][2] },
                                          { wc[1][0], wc[1][1], wc[1][2] },
                                          { wc[2][0], wc[2][1], wc[2][2] } };
                const float tNW[3][3] = { { wc[0][0], wc[0][1], wc[0][2] },
                                          { wc[2][0], wc[2][1], wc[2][2] },
                                          { wc[3][0], wc[3][1], wc[3][2] } };
                float hx[9], hy[9];
                int hn = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, tSE, 3, hx, hy);
                if (hn >= 3) gfan(hx, hy, hn, MixSurf(r2c, kSurfR), MixSurf(g2c, kSurfG), MixSurf(b2c, kSurfB));
                hn = ClipProjectPoly(f->matrix, vpX, vpY, vpW, vpH, tNW, 3, hx, hy);
                if (hn >= 3) gfan(hx, hy, hn, sr, sg, sb);
            } else {
                gfan(qx, qy, nq, sr, sg, sb);
            }
        }
        auto edgeLine = [&](int i0, int i1, float th, int alpha, bool contour, bool ticks,
                            int er = -1, int eg = -1, int eb = -1) {
            if (er < 0) { er = mr; eg = mg; eb = mb; }
            float ax, ay, bx, by;
            if (!ClipProjectSegment(f->matrix, vpX, vpY, vpW, vpH,
                                    wc[i0], wc[i1], ax, ay, bx, by)) return;
            if ((ax < -2.f * W || ax > 3.f * W || ay < -2.f * H || ay > 3.f * H) &&
                (bx < -2.f * W || bx > 3.f * W || by < -2.f * H || by > 3.f * H)) return;
            const float dx = bx - ax, dy = by - ay;
            const float len = std::sqrt(dx * dx + dy * dy);
            float f0 = 1.0f;
            if (ticks && len >= 26.0f) {              // shorter edges: the whole edge is the tick
                f0 = 14.0f / len;                     // ~14px minimum bracket
                if (f0 < 0.18f) f0 = 0.18f;
                if (f0 > 0.38f) f0 = 0.38f;
            }
            auto seg = [&](float t0, float t1) {
                const float x0 = ax + dx * t0, y0 = ay + dy * t0;
                const float x1 = ax + dx * t1, y1 = ay + dy * t1;
                if (contour) line(x0, y0, x1, y1, th + 1.6f, kInkR, kInkG, kInkB, 140);
                line(x0, y0, x1, y1, th, er, eg, eb, alpha);
            };
            if (f0 >= 0.999f) { seg(0.0f, 1.0f); return; }
            line(ax, ay, bx, by, th * 0.75f, er, eg, eb, alpha / 4);   // faint full edge keeps the silhouette
            seg(0.0f, f0); seg(1.0f - f0, 1.0f);
        };
        const bool tick = (nv == 8);   // flat zone tiles never tick: their merged perimeter is the information
        for (int i = 0; i < 4; ++i) {
            // edge_mask bit i: 0 south, 1 east, 2 north, 3 west; flat zone tiles skip edges shared with the zone
            if (nv == 4 && !((p.edge_mask >> i) & 1)) continue;
            if (p.rgb2 && nv == 4 && p.box_h <= 0.f && i < 2)
                edgeLine(i, (i + 1) & 3, 2.2f, 245, true, tick,
                         (p.rgb2 >> 16) & 255, (p.rgb2 >> 8) & 255, p.rgb2 & 255);
            else
                edgeLine(i, (i + 1) & 3, 2.2f, 245, true, tick);          // base ring
            if (nv == 8) {
                edgeLine(4 + i, 4 + ((i + 1) & 3), 1.9f, 215, true, tick);   // top ring
                edgeLine(i, 4 + i, 1.5f, 140, false, false);                 // verticals: whole, never ticked
            }
        }
        if (p.label.empty()) continue;
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
            const float estW = 0.62f * 10.5f * (float)l.mc + 24.0f;   // glyph estimate + padX 8
            float y = l.cy - 8.0f - estH * 0.5f;
            for (int guard = 0; guard < 12; ++guard) {
                bool hit = false;
                for (const auto& r : placedR)
                    if (l.cx - estW * 0.5f < r.r && l.cx + estW * 0.5f > r.l &&
                        y - estH * 0.5f < r.b && y + estH * 0.5f > r.t) { y = r.t - estH * 0.5f - 4.0f; hit = true; break; }
                if (!hit) break;
            }
            placedR.push_back({ l.cx - estW * 0.5f, y - estH * 0.5f, l.cx + estW * 0.5f, y + estH * 0.5f });
            marker::Command t{}; t.type = marker::kText;
            t.x0 = l.cx; t.y0 = y;
            t.x1 = 10.5f;
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

    for (const auto& cb : ctext) {
        if (cb.second.text.empty()) continue;
        marker::Command t{}; t.type = marker::kText;
        t.x0 = vpX + vpW * 0.5f;
        t.y0 = vpY + vpH * (0.35f - 0.075f * (float)cb.first);   // above centre, clear of the player model
        t.x1 = 22.0f;
        int rgb = cb.second.rgb;
        if (rgb < 0) { t.r = 235; t.g = 90; t.b = 90; }  // default warning red
        else { t.r = (std::uint8_t)((rgb >> 16) & 0xFF); t.g = (std::uint8_t)((rgb >> 8) & 0xFF);
               t.b = (std::uint8_t)(rgb & 0xFF); }
        t.a = 245;
        int n = (int)cb.second.text.size();
        if (n > marker::kTextMax) n = marker::kTextMax;
        std::memcpy(t.text, cb.second.text.data(), (size_t)n);
        t.text[n] = '\0';
        push(t);
    }


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
        const float cwx = f->player_fx, cwy = f->player_fy;
        {
            float ccsx, ccsy;
            if (WorldToScreen(m, vpX, vpY, vpW, vpH, cwx, cwy, z, ccsx, ccsy)) {
                const float twx = f->arrow_tx * 512.f + 256.f, twy = f->arrow_ty * 512.f + 256.f;
                float wdx = twx - cwx, wdy = twy - cwy;
                float wlen = std::sqrt(wdx * wdx + wdy * wdy);
                if (wlen > 1.0f) {
                    wdx /= wlen; wdy /= wlen;
                    const float perpx = -wdy, perpy = wdx;
                    const float R = 300.f;                  // ring radius, world-fine units (~0.59 tile)
                    float cueMaxY = ccsy + 34.0f;           // lowest projected point of the cue; distance pill anchors below it
                    const int NSEG = 32;
                    float prevX = 0, prevY = 0; bool prevOk = false;
                    for (int i = 0; i <= NSEG; ++i) {
                        double a = (double)i / NSEG * 6.28318530717959;
                        float wx = cwx + (float)std::cos(a) * R, wy = cwy + (float)std::sin(a) * R;
                        float sx, sy; bool ok = WorldToScreen(m, vpX, vpY, vpW, vpH, wx, wy, z, sx, sy);
                        if (ok && prevOk && (i & 1)) {
                            line(prevX, prevY, sx, sy, 3.2f, kInkR, kInkG, kInkB, 130);
                            line(prevX, prevY, sx, sy, 1.8f, kOkR, kOkG, kOkB, 225);
                        }
                        if (ok && sy > cueMaxY) cueMaxY = sy;
                        prevX = sx; prevY = sy; prevOk = ok;
                    }
                    auto W2S = [&](float along, float side, float& sx, float& sy) {
                        return WorldToScreen(m, vpX, vpY, vpW, vpH,
                                             cwx + wdx * along + perpx * side,
                                             cwy + wdy * along + perpy * side, z, sx, sy);
                    };
                    float Tx, Ty, Lx, Ly, Nx, Ny, Rx, Ry;
                    if (W2S(R + 430.f,   0.f, Tx, Ty) &&    // tip
                        W2S(R + 150.f,  96.f, Lx, Ly) &&    // left wing
                        W2S(R + 275.f,   0.f, Nx, Ny) &&    // back notch
                        W2S(R + 150.f, -96.f, Rx, Ry)) {    // right wing
                        marker::Command q1{}; q1.type = marker::kFillQuad;
                        q1.x0 = Tx; q1.y0 = Ty; q1.x1 = Lx; q1.y1 = Ly; q1.x2 = Nx; q1.y2 = Ny; q1.x3 = Tx; q1.y3 = Ty;
                        line(Tx, Ty, Lx, Ly, 3.0f, kInkR, kInkG, kInkB, 170);
                        line(Lx, Ly, Nx, Ny, 3.0f, kInkR, kInkG, kInkB, 170);
                        line(Nx, Ny, Rx, Ry, 3.0f, kInkR, kInkG, kInkB, 170);
                        line(Rx, Ry, Tx, Ty, 3.0f, kInkR, kInkG, kInkB, 170);
                        q1.r = kOkR; q1.g = kOkG; q1.b = kOkB; q1.a = 235; push(q1);
                        marker::Command q2{}; q2.type = marker::kFillQuad;
                        q2.x0 = Tx; q2.y0 = Ty; q2.x1 = Nx; q2.y1 = Ny; q2.x2 = Rx; q2.y2 = Ry; q2.x3 = Tx; q2.y3 = Ty;
                        q2.r = kOkR; q2.g = kOkG; q2.b = kOkB; q2.a = 235; push(q2);
                        line(Tx, Ty, Lx, Ly, 1.3f, kOkR, kOkG, kOkB, 255);
                        line(Lx, Ly, Nx, Ny, 1.3f, kOkR, kOkG, kOkB, 255);
                        line(Nx, Ny, Rx, Ry, 1.3f, kOkR, kOkG, kOkB, 255);
                        line(Rx, Ry, Tx, Ty, 1.3f, kOkR, kOkG, kOkB, 255);
                        if (Ty > cueMaxY) cueMaxY = Ty;
                        if (Ly > cueMaxY) cueMaxY = Ly;
                        if (Ry > cueMaxY) cueMaxY = Ry;
                    }
                    const int dist = arrowDist;
                    {
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

    // Interface coords are 800x600 design space; below that size the engine downscales by min(w/800, h/600).
    float uiScale;
    { float sx = (float)W / 800.0f, sy = (float)H / 600.0f; uiScale = sx < sy ? sx : sy; if (uiScale > 1.0f) uiScale = 1.0f; }

    struct ScreenRect { float x0, y0, x1, y1; };
    auto toScreen = [&](int x, int y, int w, int h, bool designSpace) -> ScreenRect {
        const float k = gvScale * (designSpace ? uiScale : 1.0f);
        return { (float)x * k, (float)y * k, (float)(x + w) * k, (float)(y + h) * k };
    };

    for (const auto& uihl : uihls) {
        if (uihl.w <= 0 || uihl.h <= 0) continue;
        float uipulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1000) / 1000.0 * 6.2831853));
        const ScreenRect sr = toScreen(uihl.x, uihl.y, uihl.w, uihl.h, true);
        const bool cell = ((sr.x1 - sr.x0) < 90.0f * gvScale && (sr.y1 - sr.y0) < 90.0f * gvScale);
        float pad = cell ? -3.5f : 3.0f;
        float x0 = sr.x0 - pad, y0 = sr.y0 - pad;
        float x1 = sr.x1 + pad, y1 = sr.y1 + pad;
        marker::Command q{}; q.type = marker::kFillRect;
        q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
        q.r = kAccR; q.g = kAccG; q.b = kAccB;
        q.a = (std::uint8_t)((cell ? 105 : 58) + (int)((cell ? 70.0f : 46.0f) * uipulse));
        push(q);
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
        if (cell) {
            float L = (x1 - x0) * 0.34f;
            const float bw = 4.2f; const int ba2 = 255;
            line(x0, y0, x0 + L, y0, bw, 255, 255, 255, ba2); line(x0, y0, x0, y0 + L, bw, 255, 255, 255, ba2);
            line(x1, y0, x1 - L, y0, bw, 255, 255, 255, ba2); line(x1, y0, x1, y0 + L, bw, 255, 255, 255, ba2);
            line(x0, y1, x0 + L, y1, bw, 255, 255, 255, ba2); line(x0, y1, x0, y1 - L, bw, 255, 255, 255, ba2);
            line(x1, y1, x1 - L, y1, bw, 255, 255, 255, ba2); line(x1, y1, x1, y1 - L, bw, 255, 255, 255, ba2);
        }
    }

    for (const auto& lb : uilbls) {
        if (lb.text.empty()) continue;
        const ScreenRect sr = toScreen(lb.x, lb.y, 1, 1, true);
        {   // one line per distinct placement: the design-to-screen mapping is the usual suspect when a label lands off
            static std::map<DWORD, std::pair<int,int>> l_lb;
            auto cur = std::make_pair(lb.x, lb.y);
            auto lit = l_lb.find(cfg.pid);
            if (lit == l_lb.end() || lit->second != cur) {
                l_lb[cfg.pid] = cur;
                char b[200];
                std::snprintf(b, sizeof(b), "uilabel: design %d,%d -> screen %.0f,%.0f  W=%d H=%d gvScale=%.3f uiScale=%.3f lc=%dx%d gv=%dx%d px=%d",
                              lb.x, lb.y, sr.x0, sr.y0, W, H, gvScale, uiScale, f ? f->lc_w : -1, f ? f->lc_h : -1, f ? f->gv_w : -1, f ? f->gv_h : -1, lb.px);
                rtx::log::Client(cfg.pid, b);
            }
        }
        marker::Command t{}; t.type = marker::kText;
        t.glyph = marker::kTextPlain;                       // left aligned at x, centred on y, no pill
        t.x0 = sr.x0; t.y0 = sr.y0;
        t.x1 = (float)lb.px * gvScale * uiScale;
        if (lb.rgb < 0) { t.r = 255; t.g = 226; t.b = 74; }  // RS yellow like the game's own value text
        else { t.r = (std::uint8_t)((lb.rgb >> 16) & 0xFF); t.g = (std::uint8_t)((lb.rgb >> 8) & 0xFF); t.b = (std::uint8_t)(lb.rgb & 0xFF); }
        t.a = 250;
        int n = (int)lb.text.size();
        if (n > marker::kTextMax) n = marker::kTextMax;
        std::memcpy(t.text, lb.text.data(), (size_t)n);
        t.text[n] = '\0';
        push(t);
    }

    if (!pcells.empty()) {
        {
            static std::map<DWORD, std::tuple<int,int,int,int>> l_pc;
            auto cur = std::make_tuple(pcells[0].x, pcells[0].y, W, f ? f->lc_w : -1);
            auto lit = l_pc.find(cfg.pid);
            if (lit == l_pc.end() || lit->second != cur) {
                l_pc[cfg.pid] = cur;
                char b[192];
                std::snprintf(b, sizeof(b),
                    "[pz] cell0=%d,%d %dx%d gvScale=%.3f uiScale=%.3f frame=%dx%d lc=%dx%d gv=%dx%d",
                    pcells[0].x, pcells[0].y, pcells[0].w, pcells[0].h, (double)gvScale,
                    (double)uiScale, W, H, f ? f->lc_w : -1, f ? f->lc_h : -1,
                    f ? f->gv_w : -1, f ? f->gv_h : -1);
                rtx::log::Client(cfg.pid, b);
            }
        }
        float ppulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 900) / 900.0 * 6.2831853));
        for (const auto& pc : pcells) {
            if (pc.w <= 0 || pc.h <= 0) continue;
            const ScreenRect sr = toScreen(pc.x, pc.y, pc.w, pc.h, false);
            float x0 = sr.x0, y0 = sr.y0, x1 = sr.x1, y1 = sr.y1;
            const int R = 255, G = 196, B = 64;                    // gold, matches the side-panel hot tile
            int fillA, bordA; float bordTh;
            if (pc.step == 0) { fillA = 85 + (int)(80.0f * ppulse); bordTh = 4.0f; bordA = 255; }
            else if (pc.step == 1) { fillA = 60; bordTh = 3.0f; bordA = 225; }
            else if (pc.step == 2) { fillA = 44; bordTh = 2.5f; bordA = 190; }
            else { fillA = 32; bordTh = 2.0f; bordA = 155; }
            marker::Command q{}; q.type = marker::kFillRect; q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
            q.r = (std::uint8_t)R; q.g = (std::uint8_t)G; q.b = (std::uint8_t)B; q.a = (std::uint8_t)fillA; push(q);
            float in = bordTh * 0.5f, bx0 = x0 + in, by0 = y0 + in, bx1 = x1 - in, by1 = y1 - in;
            line(bx0, by0, bx1, by0, bordTh, R, G, B, bordA); line(bx1, by0, bx1, by1, bordTh, R, G, B, bordA);
            line(bx1, by1, bx0, by1, bordTh, R, G, B, bordA); line(bx0, by1, bx0, by0, bordTh, R, G, B, bordA);
            float dx0 = x0 + bordTh + 1.0f, dy0 = y0 + bordTh + 1.0f, dx1 = x1 - bordTh - 1.0f, dy1 = y1 - bordTh - 1.0f;
            line(dx0, dy0, dx1, dy0, 1.4f, 0, 0, 0, 130); line(dx1, dy0, dx1, dy1, 1.4f, 0, 0, 0, 130);
            line(dx1, dy1, dx0, dy1, 1.4f, 0, 0, 0, 130); line(dx0, dy1, dx0, dy0, 1.4f, 0, 0, 0, 130);
            marker::Command t{}; t.type = marker::kText;
            float gh = (y1 - y0) * 0.5f; if (gh > 24.0f) gh = 24.0f; if (gh < 10.0f) gh = 10.0f;   // glyph height scales with the cell
            t.x0 = (x0 + x1) * 0.5f; t.y0 = (y0 + y1) * 0.5f; t.x1 = gh;
            if (pc.step == 0) { t.r = 20; t.g = 20; t.b = 24; } else { t.r = (std::uint8_t)R; t.g = (std::uint8_t)G; t.b = (std::uint8_t)B; }
            t.a = 255;
            if (pc.num >= 0) { std::snprintf(t.text, sizeof(t.text), "%d", pc.num); }
            else { t.text[0] = (char)('1' + pc.step); t.text[1] = 0; }
            push(t);
        }
    }

    if (!kcells.empty()) {
        float kpulse = (float)(0.5 + 0.5 * std::sin((now_ms() % 1100) / 1100.0 * 6.2831853));
        for (const auto& kc : kcells) {
            if (kc.w <= 0 || kc.h <= 0) continue;
            const ScreenRect sr = toScreen(kc.x, kc.y, kc.w, kc.h, false);
            float x0 = sr.x0, y0 = sr.y0, x1 = sr.x1, y1 = sr.y1;
            marker::Command q{}; q.type = marker::kFillRect; q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
            q.r = kAccR; q.g = kAccG; q.b = kAccB; q.a = (std::uint8_t)(50 + (int)(45.0f * kpulse)); push(q);
            line(x0, y0, x1, y0, 2.4f, kAccR, kAccG, kAccB, 235); line(x1, y0, x1, y1, 2.4f, kAccR, kAccG, kAccB, 235);
            line(x1, y1, x0, y1, 2.4f, kAccR, kAccG, kAccB, 235); line(x0, y1, x0, y0, 2.4f, kAccR, kAccG, kAccB, 235);
            float L = (x1 - x0) * 0.34f; const float bw = 3.4f;
            line(x0, y0, x0 + L, y0, bw, 255, 255, 255, 255); line(x0, y0, x0, y0 + L, bw, 255, 255, 255, 255);
            line(x1, y0, x1 - L, y0, bw, 255, 255, 255, 255); line(x1, y0, x1, y0 + L, bw, 255, 255, 255, 255);
            line(x0, y1, x0 + L, y1, bw, 255, 255, 255, 255); line(x0, y1, x0, y1 - L, bw, 255, 255, 255, 255);
            line(x1, y1, x1 - L, y1, bw, 255, 255, 255, 255); line(x1, y1, x1, y1 - L, bw, 255, 255, 255, 255);
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

    bool sbarsHide = false;
    if (!sbars.empty()) {
        if (HWND sgw = FindGameWindow(cfg.pid)) {
            POINT cur;
            if (GetCursorPos(&cur) && ScreenToClient(sgw, &cur)) {
                const double sgsf = rtx::launcher::dock::GameSpaceFactor(sgw, cfg.pid);
                const float cx = (float)(cur.x * sgsf), cy = (float)(cur.y * sgsf);
                float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; bool any = false;
                for (const auto& sb : sbars) {
                    if (sb.w <= 0 || sb.h <= 0) continue;
                    const ScreenRect sr = toScreen(sb.x, sb.y, sb.w, sb.h, false);
                    const float sx0 = sr.x0, sy0 = sr.y0, sx1 = sr.x1, sy1 = sr.y1;
                    if (!any) { bx0 = sx0; by0 = sy0; bx1 = sx1; by1 = sy1; any = true; }
                    else {
                        if (sx0 < bx0) bx0 = sx0; if (sy0 < by0) by0 = sy0;
                        if (sx1 > bx1) bx1 = sx1; if (sy1 > by1) by1 = sy1;
                    }
                }
                const float pad = 8.0f;
                sbarsHide = any && cx >= bx0 - pad && cx <= bx1 + pad &&
                                   cy >= by0 - pad && cy <= by1 + pad;
            }
        }
    }
    if (!sbarsHide)
    for (const auto& sb : sbars) {
        if (sb.w <= 0 || sb.h <= 0) continue;
        const ScreenRect sr = toScreen(sb.x, sb.y, sb.w, sb.h, false);
        float x0 = sr.x0, y1 = sr.y1;
        float x1 = sr.x1;
        float inset = 2.0f;
        float bh = (float)sb.h * 0.10f; if (bh < 2.5f) bh = 2.5f; if (bh > 5.0f) bh = 5.0f;
        float bx0 = x0 + inset, bx1 = x1 - inset, by1 = y1 - inset, by0 = by1 - bh;
        if (bx1 <= bx0) continue;
        { marker::Command q{}; q.type = marker::kFillRect;
          q.x0 = bx0; q.y0 = by0; q.x1 = bx1; q.y1 = by1;
          q.r = 0; q.g = 0; q.b = 0; q.a = 165; push(q); }
        int pct = sb.pct; if (pct < 0) pct = 0; if (pct > 1000) pct = 1000;
        const float fw = (bx1 - bx0) * (float)pct / 1000.0f;
        if (fw > 0.0f) {
            std::uint8_t r = (std::uint8_t)((sb.rgb >> 16) & 0xFF);
            std::uint8_t g2 = (std::uint8_t)((sb.rgb >> 8) & 0xFF);
            std::uint8_t b = (std::uint8_t)(sb.rgb & 0xFF);
            if (!sb.rgb) { r = kOkR; g2 = kOkG; b = kOkB; }
            marker::Command q{}; q.type = marker::kFillRect;
            q.x0 = bx0; q.y0 = by0; q.x1 = bx0 + fw; q.y1 = by1;
            q.r = r; q.g = g2; q.b = b; q.a = 240; push(q);
        }
    }

    for (const auto& pb : pviz) {
        if (pb.w <= 0 || pb.h <= 0) continue;
        const ScreenRect sr = toScreen(pb.x, pb.y, pb.w, pb.h, true);
        float bx0 = sr.x0, by0 = sr.y0, bx1 = sr.x1, by1 = sr.y1;
        marker::Command fillc{}; fillc.type = marker::kFillRect;
        fillc.x0 = bx0; fillc.y0 = by0; fillc.x1 = bx1; fillc.y1 = by1;
        fillc.r = 64; fillc.g = 210; fillc.b = 224; fillc.a = 26;
        push(fillc);
        line(bx0, by0, bx1, by0, 2.4f, 90, 220, 235, 235);
        line(bx1, by0, bx1, by1, 2.4f, 90, 220, 235, 235);
        line(bx1, by1, bx0, by1, 2.4f, 90, 220, 235, 235);
        line(bx0, by1, bx0, by0, 2.4f, 90, 220, 235, 235);
        if (!pb.label.empty()) {
            marker::Command t{}; t.type = marker::kText;
            bool smallBox = (by1 - by0) < 60.0f;                    // 'small' is a Windows macro
            t.x1 = 11.0f;
            float cx = smallBox ? ((bx0 + bx1) * 0.5f) : (bx0 + 3.0f);
            float labelW = (float)pb.label.size() * (t.x1 * 0.62f) + 14.0f;   // padX*2 + advances
            float halfW = labelW * 0.5f;
            bool wideRow = smallBox && (bx1 - bx0) > 3.0f * (by1 - by0);
            float ty;
            if (wideRow && bx1 + labelW + 12.0f <= (float)W) { cx = bx1 + 8.0f + halfW; ty = (by0 + by1) * 0.5f; }
            else ty = smallBox ? (by0 - 14.0f) : (by0 + 2.0f);
            if (cx - halfW < 4.0f)                cx = 4.0f + halfW;
            if (cx + halfW > (float)W - 4.0f)     cx = (float)W - 4.0f - halfW;
            if (ty < 12.0f) ty = by1 + 2.0f;                        // no room above: below the box
            if (ty > (float)H - 12.0f) ty = (float)H - 12.0f;
            t.x0 = cx; t.y0 = ty;
            t.r = 200; t.g = 245; t.b = 250; t.a = 245;
            int n = (int)pb.label.size();
            if (n > marker::kTextMax) n = marker::kTextMax;
            std::memcpy(t.text, pb.label.data(), (size_t)n);
            t.text[n] = '\0';
            push(t);
        }
    }

    if (flashAlpha > 0.0f) {
        marker::Command c{}; c.type = marker::kFillRect;
        c.x0 = vpX; c.y0 = vpY; c.x1 = vpX + vpW; c.y1 = vpY + vpH;
        c.r = 255; c.g = 70; c.b = 70;
        c.a = (std::uint8_t)((flashAlpha > 1.0f ? 1.0f : flashAlpha) * 120.0f);
        push(c);
    }

    if (widgets)
        for (const auto& wc : *widgets) push(wc);

    std::uint32_t n = (std::uint32_t)cmds.size();
    if (n > marker::kMaxCmds) n = marker::kMaxCmds;
    std::uint32_t s = sh->seq + 1;
    sh->seq = s; MemoryBarrier();                            // odd: mid-write
    sh->fb_w = W; sh->fb_h = H;
    sh->gv_x = (std::int32_t)vpX; sh->gv_y = (std::int32_t)vpY;
    sh->gv_w = (std::int32_t)vpW; sh->gv_h = (std::int32_t)vpH;
    {
        std::uint32_t mflags = 0;
        float rx = -1.f, ry = -1.f, rz = -1.f, ra = 0.f, rb = 0.f;
        float r2x = -1.f, r2y = -1.f, r2z = -1.f;
        if (f && cfg.occlude) {
            mflags |= marker::kFlagDepth;
            // Depth direction: the player against a point farther from the camera.
            float sx, sy;
            if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, f->player_fx, f->player_fy, f->player_z, sx, sy)) {
                rx = sx; ry = sy; rz = ZAt(sx, sy);
                const float p0[3] = { f->player_fx, f->player_fy, f->player_z };
                float farp[3] = { p0[0] + 4096.f, p0[1], p0[2] };
                if (ProjW(f->matrix, farp) < ProjW(f->matrix, p0)) farp[0] = p0[0] - 4096.f;
                float fx2, fy2;
                if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, farp[0], farp[1], farp[2], fx2, fy2)) {
                    float fz = ZAt(fx2, fy2);
                    if (fz >= 0.f && rz >= 0.f && fz < rz) mflags |= marker::kFlagDepthReversed;
                    // Exact depth model from the matrix itself: clip z = -a * clip w + b for every point
                    // (the z row is a multiple of the w row plus a constant), so depth = -a + b / w.
                    // The three coordinate ratios must agree; a disagreement means a non-standard
                    // projection and the constants are withheld.
                    const double m2 = f->matrix[2], m3 = f->matrix[3], m10 = f->matrix[10], m11 = f->matrix[11],
                                 m6 = f->matrix[6], m7 = f->matrix[7], m14 = f->matrix[14], m15 = f->matrix[15];
                    double ratios[3]; int nr = 0;
                    if (std::fabs(m3) > 1e-12) ratios[nr++] = -m2 / m3;
                    if (std::fabs(m11) > 1e-12) ratios[nr++] = -m10 / m11;
                    if (std::fabs(m7) > 1e-12) ratios[nr++] = -m6 / m7;
                    double A = 0.0, spread = 1.0;
                    if (nr) {
                        A = ratios[0]; spread = 0.0;
                        for (int i = 1; i < nr; ++i) spread = std::max(spread, std::fabs(ratios[i] - A) / std::max(1e-9, std::fabs(A)));
                    }
                    const double B = m14 + A * m15;
                    // Logged when the projection changes past its frame-to-frame float noise (the
                    // near and far planes move with the view distance setting; camera zoom does not
                    // touch them), and otherwise every 30 seconds so the running accuracy is on record.
                    static double s_lastA = 1e30, s_lastB = 1e30;
                    static unsigned s_lastTick = 0;
                    static int s_logs = 0;
                    const unsigned tick = GetTickCount();
                    const bool moved = std::fabs(A - s_lastA) > 1e-5 * std::max(1.0, std::fabs(A)) ||
                                       std::fabs(B - s_lastB) > 2e-3 * std::max(1.0, std::fabs(B));
                    const bool due = s_lastTick == 0 || (tick - s_lastTick) > 30000;
                    if ((moved || due) && s_logs < 400) {
                        ++s_logs; s_lastA = A; s_lastB = B; s_lastTick = tick;
                        char lb[220];
                        std::snprintf(lb, sizeof(lb), "[ovl] projection depth model: a=%.9g b=%.9g row-ratio spread %.3g (%d ratios); player w %.0f z %.7f model %.7f",
                                      A, B, spread, nr, (double)ProjW(f->matrix, p0), (double)rz, -A + B / std::max(1e-9, (double)ProjW(f->matrix, p0)));
                        rtx::log::Client(cfg.pid, lb);
                    }
                    if (nr >= 2 && spread < 1e-6 && std::fabs(B) > 0.0) { ra = (float)A; rb = (float)B; }
                }
                // Second calibration point: three tiles diagonal on the grid at the tile-corner height.
                float q[3] = { p0[0] + 3.f * 512.f, p0[1] + 3.f * 512.f, p0[2] };
                if (f->grid_r >= 3 && f->heights.size() == (size_t)(2 * f->grid_r + 1) * (2 * f->grid_r + 1) * 4) {
                    const int T = 2 * f->grid_r + 1, gx = f->grid_r + 3, gy = f->grid_r + 3;
                    const std::int16_t hh = f->heights[((size_t)gx * T + gy) * 4];
                    if (hh != -32768) { q[0] = (float)(f->player_tx + 3) * 512.f; q[1] = (float)(f->player_ty + 3) * 512.f; q[2] = 32.f * (float)hh; }
                }
                float qx, qy;
                if (WorldToScreen(f->matrix, vpX, vpY, vpW, vpH, q[0], q[1], q[2], qx, qy)) { r2x = qx; r2y = qy; r2z = ZAt(qx, qy); }
            }
        }
        sh->flags = mflags; sh->ref_x = rx; sh->ref_y = ry; sh->ref_z = rz; sh->ref_a = ra; sh->ref_b = rb;
        sh->ref2_x = r2x; sh->ref2_y = r2y; sh->ref2_z = r2z;
    }
    for (std::uint32_t i = 0; i < n; ++i) sh->cmds[i] = cmds[i];
    sh->count = n;
    sh->visible = 1;
    MemoryBarrier(); sh->seq = s + 1;                        // even: done
}


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

void WCard(std::vector<marker::Command>& v, float x, float y, float w, float h,
           float rad, int ar, int ag, int ab, float alpha) {
    WRound(v, x, y + 3.0f, w, h, rad, 0, 0, 0, (int)(95.0f * alpha));
    WRound(v, x - 1.2f, y - 1.2f, w + 2.4f, h + 2.4f, rad + 1.2f, ar, ag, ab, (int)(235.0f * alpha));
    WRound(v, x, y, w, h, rad, 20, 21, 28, (int)(240.0f * alpha));
}

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

// Refreshes g_notif_hit. Caller holds g_mu.
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

// Widgets for `pid` plus their input-window hit rects. W/H = game client size. Locks g_mu.
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
    // metronome / XP widgets moved in-game in 2.0.9; keep the hit rects clear
    if (g_metro.pid == pid) g_metro_hit = RECT{0, 0, 0, 0};
    if (g_xp.pid == pid) { g_xp_hit = RECT{0, 0, 0, 0}; g_xp_min = RECT{0, 0, 0, 0}; }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // window coords are physical px; widget rects are game px
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
        if (g_metro.on && !g_metro.locked && PtInRect(&g_metro_hit, pt)) {
            g_metro.dragging = true;
            g_metro.placed   = true;
            g_metro.dragOff.x = pt.x - g_metro.x;
            g_metro.dragOff.y = pt.y - g_metro.y;
            SetCapture(hwnd);
            return 0;
        }
        if (g_xp.on && !g_xp.locked) {
            if (PtInRect(&g_xp_min, pt)) {
                g_xp.minimized = !g_xp.minimized;
                SaveXp();
                return 0;
            }
            if (PtInRect(&g_xp_hit, pt)) {
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
    if (msg == WM_MOUSEWHEEL) {                       // resize
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
        if (g_xp.on && !g_xp.locked && PtInRect(&g_xp_hit, pt)) {
            float d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.08f : -0.08f;
            float s = g_xp.scale + d;
            g_xp.scale = (s < 0.7f ? 0.7f : (s > 1.8f ? 1.8f : s));
            SaveXp();
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
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClass, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);

    Dib dib;
    bool shown = false;
    struct HeldFrame { rtx::reader::OverlayFrame frame; long long at_ms = 0; };
    std::unordered_map<DWORD, HeldFrame> held;
    constexpr long long kHoldMs = 600;
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

        if (xpPid) {
            long long t = now_ms();
            bool due;
            { std::lock_guard<std::mutex> lk(g_mu); due = (t - g_xp.sampleMs) >= 1000; }
            if (due) {
                int cur[29];
                bool ok = rtx::reader::SkillsXp(xpPid, cur);
                std::lock_guard<std::mutex> lk(g_mu);
                g_xp.sampleMs = t;
                bool readable = ok && cur[3] > 0;
                if (readable) for (int i = 0; i < 29 && readable; ++i)
                    if (cur[i] == 0 && g_xp.cur[i] > 0) readable = false;
                if (readable) {
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
                            if (g_xp.cur[i] >= 0 && cur[i] - g_xp.cur[i] > 5000000) g_xp.base[i] += cur[i] - g_xp.cur[i];
                            if (g_xp.firstGain[i] == 0 && g_xp.cur[i] >= 0 && cur[i] > g_xp.base[i])
                                g_xp.firstGain[i] = t;
                            if (g_xp.startMs == 0 && g_xp.firstGain[i] != 0) g_xp.startMs = t;
                            g_xp.cur[i] = cur[i];
                        }
                    }
                }
            }
        }

        std::uint32_t mTick = 0; double mAge = -1.0; bool mHave = false;
        if ((metroOn || metroAudio) && metroPid)
            mHave = rtx::reader::TickState(metroPid, mTick, mAge);
        if (metroAudio && mHave) {
            std::uint32_t prev;
            { std::lock_guard<std::mutex> lk(g_mu); prev = g_metro.lastTick; g_metro.lastTick = mTick; }
            if (prev != 0xFFFFFFFFu && mTick != prev && (mTick % (std::uint32_t)metroInterval) == 0)
                PlayMetroClick();
        }

        long long tnow = now_ms();
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
            std::vector<rtx::reader::GuideSite> gsites;
            bool hasUiHl = false, hasPanelViz = false, hasCenter = false, hasSolverCells = false, hasSkillBars = false;
            { std::lock_guard<std::mutex> lk(g_mu);
              auto git = g_guides.find(cpid);
              if (git != g_guides.end())
                  for (const auto& m : git->second) gsites.push_back({ m.gx, m.gy, m.label, m.snapObj, m.rgb, m.gx2, m.gy2, m.region, m.plane, m.rgb2 });
              auto uit = g_uiHighlights.find(cpid);
              hasUiHl = (uit != g_uiHighlights.end() && !uit->second.empty());
              auto ctit = g_centerTexts.find(cpid);
              hasCenter = false;
              if (ctit != g_centerTexts.end())
                  for (const auto& cb : ctit->second) if (!cb.second.text.empty()) { hasCenter = true; break; }
              auto pit = g_panelViz.find(cpid);
              hasPanelViz = (pit != g_panelViz.end() && !pit->second.empty());
              auto pzit = g_puzzleCells.find(cpid);
              hasSolverCells = (pzit != g_puzzleCells.end() && !pzit->second.empty());
              auto knit = g_knotCells.find(cpid);
              hasSolverCells = hasSolverCells || (knit != g_knotCells.end() && !knit->second.empty());
              auto sbit2 = g_skillBars.find(cpid);
              hasSkillBars = (sbit2 != g_skillBars.end() && !sbit2->second.bars.empty()); }
            bool wantF = ccfg.enabled || ccfg.markers || ccfg.nameplates || !ccfg.highlight.empty() ||
                         !ccfg.outline.empty() || !ccfg.outlineLocs.empty() || !gsites.empty();
            bool wantWidgets = (toasting && g_toast_pid.load() == cpid) ||
                               (anyNotifs && g_notif_pid.load() == cpid) ||
                               (metroOn && metroPid == cpid) ||
                               (xpOn && xpPid == cpid);
            if (!wantF && !hasUiHl && !hasPanelViz && !hasCenter && !hasSolverCells && !hasSkillBars &&
                fa <= 0.0f && !wantWidgets) { PublishMarkers(ccfg, nullptr, 0, 0); continue; }
            HWND gw = FindGameWindow(cpid);
            if (!gw || IsIconic(gw) || !IsWindowVisible(gw)) {
                PublishMarkers(ccfg, nullptr, 0, 0);
                continue;
            }
            RECT rc2; GetClientRect(gw, &rc2);
            double gsf = rtx::launcher::dock::GameSpaceFactor(gw, cpid);
            int W2 = (int)std::lround((rc2.right - rc2.left) * gsf);
            int H2 = (int)std::lround((rc2.bottom - rc2.top) * gsf);
            if (W2 < 16 || H2 < 16) continue;
            {
                static std::map<DWORD, std::tuple<int, int, int>> l_sz;   // pid -> {W2, H2, gsf*1000}
                auto cur = std::make_tuple(W2, H2, (int)std::lround(gsf * 1000.0));
                auto it2 = l_sz.find(cpid);
                if (it2 == l_sz.end() || it2->second != cur) {
                    l_sz[cpid] = cur;
                    rtx::log::Client(cpid,
                        "[ovl] frame " + std::to_string(W2) + "x" + std::to_string(H2) +
                        " (physical " + std::to_string(rc2.right - rc2.left) + "x" +
                        std::to_string(rc2.bottom - rc2.top) + " x gsf " + std::to_string(gsf) + ")");
                }
            }
            rtx::launcher::companion::EnsureLoaded(cpid);   // hosts the present layer
            std::vector<rtx::marker::Command> wcmds;
            if (wantWidgets)
                BuildWidgetCommands(cpid, W2, H2, tnow, mTick, mAge, mHave, wcmds);
            const std::vector<rtx::marker::Command>* wptr = wcmds.empty() ? nullptr : &wcmds;
            rtx::reader::OverlayFrame frame;
            bool ok = false;
            if (wantF)
                ok = rtx::reader::BuildOverlayFrame(
                         cpid,
                         (ccfg.enabled && ccfg.players)  || (ccfg.nameplates && (ccfg.np_players || !ccfg.np_player_uids.empty())),
                         (ccfg.enabled && ccfg.npcs)     || (ccfg.nameplates && ccfg.np_npcs),
                         (ccfg.enabled && ccfg.objects)  || (ccfg.nameplates && ccfg.np_objects),
                         ccfg.enabled && ccfg.specials,
                         (ccfg.enabled && ccfg.grid) ? ccfg.radius : 0,
                         ccfg.interactable, ccfg.enabled && ccfg.true_tile,
                         ccfg.highlight, ccfg.outline, ccfg.outlineLocs, gsites, frame);
            if (!ok && (hasUiHl || hasPanelViz || hasSolverCells || hasSkillBars))
                ok = rtx::reader::ReadViewMetrics(cpid, frame);
            if (ok) {
                PublishMarkers(ccfg, &frame, W2, H2, fa, wptr);
                if (wantF) held[cpid] = { std::move(frame), tnow };
            } else if (wantF) {
                auto hit = held.find(cpid);
                bool hold = hit != held.end() && tnow - hit->second.at_ms < kHoldMs;
                PublishMarkers(ccfg, hold ? &hit->second.frame : nullptr, W2, H2, fa, wptr);
            } else {
                PublishMarkers(ccfg, nullptr, W2, H2, fa, wptr);
            }
            anyFrames = anyFrames || wantF || hasUiHl || hasPanelViz || hasCenter ||
                        hasSolverCells || hasSkillBars || wptr != nullptr;
        }
        for (auto it = held.begin(); it != held.end(); )
            if (cfgs.find(it->first) == cfgs.end()) it = held.erase(it);
            else ++it;
        for (auto it = g_marker_outs.begin(); it != g_marker_outs.end(); ) {
            if (cfgs.find(it->first) == cfgs.end()) { it->second.close(); it = g_marker_outs.erase(it); }
            else ++it;
        }

        bool notifying = anyNotifs, metroLocked, xpLocked;
        { std::lock_guard<std::mutex> lk(g_mu);
          metroLocked = g_metro.locked; xpLocked = g_xp.locked; }
        DWORD np = g_notif_pid.load();
        DWORD pid  = (notifying && np)                     ? np
                   : (metroOn && metroPid && !metroLocked) ? metroPid
                   : (xpOn && xpPid && !xpLocked)          ? xpPid : 0;
        HWND  game = pid ? FindGameWindow(pid) : nullptr;
        bool  visible = game && !IsIconic(game) && IsWindowVisible(game);
        bool  padMetro = metroOn && metroPid && pid == metroPid && !metroLocked;
        bool  padXp    = xpOn && xpPid && pid == xpPid && !xpLocked;
        bool  padNotif = notifying && np && pid == np;

        if (!visible || (!padNotif && !padMetro && !padXp)) {
            if (shown) { ShowWindow(hwnd, SW_HIDE); shown = false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                (metroOn || metroAudio) ? 16 : (anyFrames || anyFlash || toasting) ? 33 : 120));
            continue;
        }

        RECT cr; GetClientRect(game, &cr);
        POINT tl{cr.left, cr.top};
        ClientToScreen(game, &tl);
        int W = cr.right - cr.left, H = cr.bottom - cr.top;   // physical px
        if (W < 16 || H < 16) { std::this_thread::sleep_for(std::chrono::milliseconds(60)); continue; }
        // gsfIn maps physical -> game px (hit rects are game px)
        double gsfIn = rtx::launcher::dock::GameSpaceFactor(game, pid);
        if (gsfIn <= 0.0) gsfIn = 1.0;
        g_inputScale.store(gsfIn);

        static std::vector<long long> fpPrev; static POINT tlPrev{ LONG_MIN, LONG_MIN };
        std::vector<long long> fp;
        fp.reserve(16);
        fp.push_back(W); fp.push_back(H);
        fp.push_back((long long)std::llround(gsfIn * 10000.0));
        fp.push_back((padMetro ? 1 : 0) | (padXp ? 2 : 0) | (padNotif ? 4 : 0));
        auto fpRect = [&](const RECT& r) {
            fp.push_back(((long long)r.left << 32) ^ (unsigned)r.top);
            fp.push_back(((long long)r.right << 32) ^ (unsigned)r.bottom);
        };
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (padMetro) fpRect(g_metro_hit);
            if (padXp)    fpRect(g_xp_hit);
            if (padNotif) for (const auto& hit : g_notif_hit) fpRect(hit.rect);
        }
        const bool fpChanged = fp != fpPrev;
        const bool moved = tl.x != tlPrev.x || tl.y != tlPrev.y;
        if (!fpChanged && !moved && shown) {
        } else if (!fpChanged && shown) {
            POINT dst{ tl.x, tl.y };
            UpdateLayeredWindow(hwnd, nullptr, &dst, nullptr, nullptr, nullptr, 0, nullptr, 0);
            tlPrev = tl;
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else if (dib.ensure(W, H)) {
            fpPrev = std::move(fp); tlPrev = tl;
            dib.clear();
            // premultiplied black at alpha 2 is (0,0,0,2): no premultiply pass needed
            auto pad = [&](const RECT& rcG) {   // rcG: game px
                RECT rc{ (LONG)std::lround(rcG.left  / gsfIn), (LONG)std::lround(rcG.top    / gsfIn),
                         (LONG)std::lround(rcG.right / gsfIn), (LONG)std::lround(rcG.bottom / gsfIn) };
                int x0 = rc.left < 0 ? 0 : rc.left, y0 = rc.top < 0 ? 0 : rc.top;
                int x1 = rc.right > W ? W : rc.right, y1 = rc.bottom > H ? H : rc.bottom;
                auto* px = (std::uint32_t*)dib.bits;
                for (int yy = y0; yy < y1; ++yy)
                    std::fill(px + (size_t)yy * W + x0, px + (size_t)yy * W + x1, 0x02000000u);
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
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else if (shown) {
            ShowWindow(hwnd, SW_HIDE); shown = false;
        }

        bool overUi = false;
        if (shown) {
            POINT cur; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
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
        Config& dst = cfg_slot(c.pid);
        auto hl = dst.highlight;          // these four are owned by their own setters; preserve
        auto ol = dst.outline;
        auto oll = dst.outlineLocs;
        auto npu = dst.np_player_uids;
        dst = c;
        dst.highlight = hl;
        dst.outline = ol;
        dst.outlineLocs = oll;
        dst.np_player_uids = npu;
        need_thread = c.enabled || !dst.highlight.empty() || !dst.outline.empty() || !dst.outlineLocs.empty();
        hisz = dst.highlight.size();
    }
    rtx::log::Launcher("[ovl] Configure pid=" + std::to_string(c.pid) + " en=" + std::to_string((int)c.enabled) + " hi=" + std::to_string(hisz));
    if (need_thread) ensure_thread();   // once started it idles until Stop()
}

void EnableMarkers(std::uint32_t pid) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);
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
        if (locked) g_metro.dragging = false;
        if (visual && !wasVisual) LoadMetro();
        if (!visual) g_metro_hit = RECT{0, 0, 0, 0};
        if (audio && !wasAudio)   g_metro.lastTick = 0xFFFFFFFFu;  // no click on the first detection
        if (pid && visual) {
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
        }
    }
    if (visual || audio) ensure_thread();
}


void XpPanel(std::uint32_t pid, bool visible, bool locked, bool total,
             bool auto_skills, std::uint32_t mask) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        bool wasOn = g_xp.on;
        if (g_xp.pid != (DWORD)pid) ResetXpSession();
        g_xp.on         = visible;
        g_xp.locked     = locked;
        g_xp.showTotal  = total;
        g_xp.autoSkills = auto_skills;
        g_xp.mask       = mask;
        g_xp.pid        = (DWORD)pid;
        if (locked) g_xp.dragging = false;
        if (visible && !wasOn) LoadXp();
        if (!visible) { g_xp_hit = RECT{0, 0, 0, 0}; g_xp_min = RECT{0, 0, 0, 0}; }
        if (pid && visible) {
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
        }
    }
    if (pid) ensure_thread();   // the thread hosts the 1 Hz sampler even while hidden
}

void XpPanelReset(std::uint32_t) {
    std::lock_guard<std::mutex> lk(g_mu);
    ResetXpSession();
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
            Config& dst = cfg_slot((DWORD)pid);
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
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
            g_uiHighlights[(DWORD)pid] = keep;
        }
    }
    ensure_thread();
}

void SetUiLabels(std::uint32_t pid, const std::vector<UiLabel>& labels) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        std::vector<UiLabel> keep;
        for (const auto& l : labels) if (!l.text.empty()) keep.push_back(l);
        if (keep.empty()) g_uiLabels.erase((DWORD)pid);
        else {
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
            g_uiLabels[(DWORD)pid] = keep;
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
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
            g_panelViz[(DWORD)pid] = boxes;
        }
    }
    ensure_thread();
}

void SetCenterText(std::uint32_t pid, const std::string& text, int slot, int rgb) {
    if (!pid) return;
    if (slot < 0 || slot >= kCenterSlots) slot = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (text.empty()) {
            auto it = g_centerTexts.find((DWORD)pid);
            if (it != g_centerTexts.end()) {
                it->second.erase(slot);
                if (it->second.empty()) g_centerTexts.erase(it);
            }
        } else {
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
            g_centerTexts[(DWORD)pid][slot] = CenterBanner{ text, rgb };
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
            Config& dst = cfg_slot((DWORD)pid);
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
            Config& dst = cfg_slot((DWORD)pid);
            dst.pid = pid;
            g_skillBars[(DWORD)pid] = { bars, now_ms() };
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
            Config& dst = cfg_slot((DWORD)pid);
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
        Config& dst = cfg_slot((DWORD)pid);
        dst.pid = pid;
        // The Dungeoneering scene tick and the quest guides re-assert their list every pass (the
        // channel is shared, so the last writer wins). An unchanged list is a no-op: no log, no wake.
        if (dst.highlight == names) return;
        dst.highlight = names;
    }
    rtx::log::Launcher("[ovl] SetHighlight pid=" + std::to_string(pid) + " n=" + std::to_string(names.size()));
    ensure_thread();
}

void SetOutlineLocs(std::uint32_t pid, const std::vector<rtx::reader::OutlineLocReq>& locs) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& dst = cfg_slot((DWORD)pid);
        dst.pid = pid;
        dst.outlineLocs = locs;
    }
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
        Config& dst = cfg_slot((DWORD)pid);
        dst.pid = pid;
        g_flash_until[(DWORD)pid] = now_ms() + kFlashMs;
    }
    ensure_thread();
}

void Toast(std::uint32_t pid, const std::string& text) {
    { std::lock_guard<std::mutex> lk(g_mu);
      g_toast_text = text;
      if (pid) {
          Config& dst = cfg_slot((DWORD)pid);
          dst.pid = pid;
      } }
    g_toast_pid.store((DWORD)pid);
    g_toast_until_ms.store(now_ms() + kToastMs);
    ensure_thread();
}

void Notify(std::uint32_t pid, const std::string& text, long long ttl_ms) {
    { std::lock_guard<std::mutex> lk(g_mu);
      long long exp = (ttl_ms > 0) ? now_ms() + ttl_ms : 0;
      bool found = false;
      for (auto& n : g_notifs) if (n.text == text) { n.expire_ms = exp; found = true; break; }
      if (!found) {
          g_notifs.push_back(Notif{ ++g_notif_seq, text, exp });
          while (g_notifs.size() > 6) g_notifs.erase(g_notifs.begin()); }
      if (pid) {
          Config& dst = cfg_slot((DWORD)pid);
          dst.pid = pid;
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

// Synchronous seqlock write of visible=0 to the pid's section; safe from any thread. Must run
void QuiesceMarkers(std::uint32_t pid) {
    if (!pid) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfgs.erase((DWORD)pid); ++g_cfgsVer;
        g_flash_until.erase((DWORD)pid);
        g_guides.erase((DWORD)pid);
        g_uiHighlights.erase((DWORD)pid);
        g_centerTexts.erase((DWORD)pid);
        g_panelViz.erase((DWORD)pid);
    }
    wchar_t name[64];
    marker::MakeSectionName(pid, name);
    HANDLE map = OpenFileMappingW(FILE_MAP_WRITE, FALSE, name);
    if (!map) return;                       // section never created
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
