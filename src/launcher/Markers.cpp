#include "Markers.h"

#include "Process.h"
#include "Dock.h"                    // game window handle for cursor mapping
#include "../reader/Reader.h"        // BuildOverlayFrame (live camera matrix)
#include "../cache/CacheReader.h"    // TileHeight (terrain-followed tile corners)

#include <Windows.h>
#include <cmath>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace rtx::markers {

namespace {

namespace process = rtx::launcher::process;

std::mutex g_mu;

struct Account {
    std::vector<TileMarker> markers;
    std::uint64_t           version = 1;
    bool                    loaded  = false;
};
std::map<std::string, Account> g_accounts;   // keyed by sanitized account name (guarded by g_mu)

// ---- keybind config (global, loaded once) ----
Keybinds g_kb;
bool     g_kb_loaded = false;
// Per-pid arm state: the mark/delete keys only place tiles while the client's Markers panel is
// open. Guarded by g_mu. A pid absent from the set is disarmed (the safe default).
std::map<std::uint32_t, bool> g_kb_armed;

std::filesystem::path user_dir() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) == 0) return {};
    return std::filesystem::path(up) / L"RuneToolsX";
}

std::filesystem::path markers_dir() {
    auto d = user_dir();
    if (d.empty()) return {};
    d /= L"markers";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d;
}

// Same identity the bank/alerts caches use: keep [A-Za-z0-9-_], space -> '_', drop the rest.
std::string sanitize_account(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    return out;
}

// pid -> account name, cached: account_for() is called every overlay frame, and ReadJxEnv()
// walks the remote PEB -- far too heavy to repeat at frame rate. The mapping is stable for a
// process's lifetime, so resolve once. Empty is also cached (unresolved account stays empty).
std::mutex                              g_acct_mu;
std::map<std::uint32_t, std::string>    g_pid_acct;

std::string account_for(std::uint32_t pid) {
    if (!pid) return {};
    {
        std::lock_guard<std::mutex> lk(g_acct_mu);
        auto it = g_pid_acct.find(pid);
        if (it != g_pid_acct.end()) return it->second;
    }
    auto env = process::ReadJxEnv(pid);
    auto it = env.find("JX_DISPLAY_NAME");
    std::string acct = (it != env.end()) ? sanitize_account(it->second) : std::string();
    // Only cache a resolved name; a momentary empty (env not ready yet) should be retried.
    if (!acct.empty()) {
        std::lock_guard<std::mutex> lk(g_acct_mu);
        g_pid_acct[pid] = acct;
    }
    return acct;
}

// Labels are stored in a tab-separated line, so a tab or newline would corrupt the file.
std::string clean_label(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (c == '\t' || c == '\n' || c == '\r') out.push_back(' ');
        else if ((unsigned char)c >= 0x20) out.push_back(c);   // keep printable + UTF-8 high bytes
    }
    if (out.size() > 64) out.resize(64);
    return out;
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o.push_back(c);
        }
    }
    return o;
}

void save_locked(const std::string& acct, const Account& a) {
    auto dir = markers_dir();
    if (dir.empty() || acct.empty()) return;
    auto path = dir / (acct + ".tsv");
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    for (const auto& m : a.markers) {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "%d\t%d\t%d\t%d\t%06X\t",
                      m.region, m.lx, m.ly, m.plane, m.color & 0xFFFFFF);
        f << hdr << m.label << '\n';
    }
}

Account& get_or_load(const std::string& acct) {   // caller holds g_mu
    Account& a = g_accounts[acct];
    if (a.loaded || acct.empty()) { a.loaded = true; return a; }
    a.loaded = true;
    auto dir = markers_dir();
    if (dir.empty()) return a;
    std::ifstream f(dir / (acct + ".tsv"), std::ios::binary);
    if (!f) return a;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // region \t lx \t ly \t plane \t RRGGBB \t label(rest)
        std::istringstream ss(line);
        std::string region, lx, ly, plane, color, label;
        if (!std::getline(ss, region, '\t')) continue;
        if (!std::getline(ss, lx, '\t')) continue;
        if (!std::getline(ss, ly, '\t')) continue;
        if (!std::getline(ss, plane, '\t')) continue;
        if (!std::getline(ss, color, '\t')) continue;
        std::getline(ss, label);   // may be empty / contain spaces
        TileMarker m;
        try {
            m.region = std::stoi(region);
            m.lx = std::stoi(lx); m.ly = std::stoi(ly);
            m.plane = std::stoi(plane);
            m.color = (std::uint32_t)std::stoul(color, nullptr, 16) & 0xFFFFFF;
        } catch (...) { continue; }
        m.label = clean_label(label);
        if (m.lx < 0 || m.lx > 63 || m.ly < 0 || m.ly > 63 || m.plane < 0 || m.plane > 3) continue;
        a.markers.push_back(std::move(m));
    }
    return a;
}

int find_idx(const Account& a, int region, int lx, int ly, int plane) {
    for (int i = 0; i < (int)a.markers.size(); ++i) {
        const auto& m = a.markers[i];
        if (m.region == region && m.lx == lx && m.ly == ly && m.plane == plane) return i;
    }
    return -1;
}

void load_keybinds_locked() {
    if (g_kb_loaded) return;
    g_kb_loaded = true;
    auto dir = markers_dir();
    if (dir.empty()) return;
    std::ifstream f(dir / L"_input.txt");
    if (!f) return;
    // Format: markVk removeVk [unused flag] colorHex. The middle field is read and ignored so
    // existing files still parse.
    int mk = 0, rm = 0, legacy = 0; unsigned col = 0x46E0C0;
    if (f >> mk >> rm >> legacy >> std::hex >> col) {
        if (mk) g_kb.markVk = mk;          // 0 = unbound -> keep the A / D defaults
        if (rm) g_kb.removeVk = rm;
        g_kb.defColor = col & 0xFFFFFF;
    }
}

void save_keybinds_locked() {
    auto dir = markers_dir();
    if (dir.empty()) return;
    std::ofstream f(dir / L"_input.txt", std::ios::trunc);
    if (!f) return;
    char b[64];   // keep the 4-field layout (middle field reserved) for compatibility
    std::snprintf(b, sizeof(b), "%d %d %d %06X", g_kb.markVk, g_kb.removeVk,
                  1, g_kb.defColor & 0xFFFFFF);
    f << b;
}

}  // namespace

std::vector<TileMarker> Snapshot(std::uint32_t pid) {
    std::string acct = account_for(pid);
    if (acct.empty()) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    return get_or_load(acct).markers;
}

std::string GetJson(std::uint32_t pid) {
    std::string acct = account_for(pid);
    if (acct.empty()) return "[]";
    std::lock_guard<std::mutex> lk(g_mu);
    const Account& a = get_or_load(acct);
    std::string out = "[";
    for (size_t i = 0; i < a.markers.size(); ++i) {
        const auto& m = a.markers[i];
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "%s{\"region\":%d,\"lx\":%d,\"ly\":%d,\"plane\":%d,\"color\":\"#%06X\",\"label\":\"",
            i ? "," : "", m.region, m.lx, m.ly, m.plane, m.color & 0xFFFFFF);
        out += buf; out += json_escape(m.label); out += "\"}";
    }
    out += "]";
    return out;
}

std::uint64_t Version(std::uint32_t pid) {
    std::string acct = account_for(pid);
    if (acct.empty()) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    return get_or_load(acct).version;
}

bool Add(std::uint32_t pid, int region, int lx, int ly, int plane,
         std::uint32_t color, const std::string& label) {
    if (lx < 0 || lx > 63 || ly < 0 || ly > 63 || plane < 0 || plane > 3) return false;
    std::string acct = account_for(pid);
    if (acct.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    Account& a = get_or_load(acct);
    int idx = find_idx(a, region, lx, ly, plane);
    if (idx >= 0) {   // already a marker here -> update colour/label in place
        a.markers[idx].color = color & 0xFFFFFF;
        a.markers[idx].label = clean_label(label);
    } else {
        TileMarker m; m.region = region; m.lx = lx; m.ly = ly; m.plane = plane;
        m.color = color & 0xFFFFFF; m.label = clean_label(label);
        if (a.markers.size() < 4096) a.markers.push_back(std::move(m)); else return false;
    }
    a.version++;
    save_locked(acct, a);
    return true;
}

bool Remove(std::uint32_t pid, int region, int lx, int ly, int plane) {
    std::string acct = account_for(pid);
    if (acct.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    Account& a = get_or_load(acct);
    int idx = find_idx(a, region, lx, ly, plane);
    if (idx < 0) return false;
    a.markers.erase(a.markers.begin() + idx);
    a.version++;
    save_locked(acct, a);
    return true;
}

bool SetLabel(std::uint32_t pid, int region, int lx, int ly, int plane, const std::string& label) {
    std::string acct = account_for(pid);
    if (acct.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    Account& a = get_or_load(acct);
    int idx = find_idx(a, region, lx, ly, plane);
    if (idx < 0) return false;
    a.markers[idx].label = clean_label(label);
    a.version++;
    save_locked(acct, a);
    return true;
}

bool SetColor(std::uint32_t pid, int region, int lx, int ly, int plane, std::uint32_t color) {
    std::string acct = account_for(pid);
    if (acct.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    Account& a = get_or_load(acct);
    int idx = find_idx(a, region, lx, ly, plane);
    if (idx < 0) return false;
    a.markers[idx].color = color & 0xFFFFFF;
    a.version++;
    save_locked(acct, a);
    return true;
}

bool Clear(std::uint32_t pid) {
    std::string acct = account_for(pid);
    if (acct.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    Account& a = get_or_load(acct);
    if (a.markers.empty()) return false;
    a.markers.clear();
    a.version++;
    save_locked(acct, a);
    return true;
}

namespace {

// RE'd projection (mirrors rtx::overlay::WorldToScreen): world-fine (x,y,z) -> client px.
bool world_to_screen(const float* m, float vpX, float vpY, float vpW, float vpH,
                     float x, float y, float z, float& sx, float& sy) {
    float w = m[3] * x + m[11] * y + m[7] * z + m[15];
    if (w <= 1.0f) return false;
    float nx = (m[0] * x + m[8] * y + m[4] * z + m[12]) / w;
    float ny = (m[1] * x + m[9] * y + m[5] * z + m[13]) / w;
    float cx = vpW / 2.0f, cy = vpH / 2.0f;
    sx = nx * cx - nx * 2.0f + cx + vpX;
    sy = -(ny * cy) + ny + cy + vpY;
    return true;
}

// Is (px,py) inside the convex quad p0..p3 (perimeter order)? Same-sign edge cross products.
bool point_in_quad(float px, float py, const float* qx, const float* qy) {
    auto cr = [](float ax, float ay, float bx, float by, float cx, float cy) {
        return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    };
    float d0 = cr(qx[0], qy[0], qx[1], qy[1], px, py);
    float d1 = cr(qx[1], qy[1], qx[2], qy[2], px, py);
    float d2 = cr(qx[2], qy[2], qx[3], qy[3], px, py);
    float d3 = cr(qx[3], qy[3], qx[0], qy[0], px, py);
    bool neg = (d0 < 0) || (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d0 > 0) || (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

// Resolve the world tile under the mouse cursor for `pid`. Builds a live frame, then finds the
// ground tile whose projected quad contains the cursor (front-most on overlap). false = no hit.
bool cursor_tile(std::uint32_t pid, int& outTx, int& outTy, int& outPlane) {
    HWND hwnd = reinterpret_cast<HWND>(rtx::launcher::dock::GameWindowHandle(pid));
    if (!hwnd || !IsWindow(hwnd)) return false;
    POINT pt;
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) return false;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return false;
    float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);
    if (W < 16 || H < 16) return false;
    if (pt.x < 0 || pt.y < 0 || pt.x > W || pt.y > H) return false;   // cursor outside the game

    rtx::reader::OverlayFrame f;
    std::vector<std::string> none;
    std::vector<int> noOutline;
    std::vector<rtx::reader::GuideSite> noGuides;
    if (!rtx::reader::BuildOverlayFrame(pid, false, false, false, false, 0, false, none, noOutline, noGuides, f) || !f.ok)
        return false;

    float vpW = f.gv_w > 0 ? (float)f.gv_w : W; if (vpW > W) vpW = W;
    float vpH = f.gv_h > 0 ? (float)f.gv_h : H; if (vpH > H) vpH = H;
    float vpX = f.gv_w > 0 ? (float)f.gv_x : 0.f; if (vpX < 0) vpX = 0; if (vpX + vpW > W) vpX = W - vpW;
    float vpY = f.gv_h > 0 ? (float)f.gv_y : 0.f; if (vpY < 0) vpY = 0; if (vpY + vpH > H) vpY = H - vpH;

    const float mx = (float)pt.x, my = (float)pt.y;
    // Cursor isn't over the 3D viewport (e.g. it's on the chat box / a panel) -> not a tile pick,
    // and skip the per-tile search entirely so the key falls through to the game cheaply.
    if (mx < vpX || my < vpY || mx > vpX + vpW || my > vpY + vpH) return false;

    const int R = 24;
    bool found = false; int bx = 0, by = 0; float bestFront = -1e18f;
    for (int tx = f.player_tx - R; tx <= f.player_tx + R; ++tx)
        for (int ty = f.player_ty - R; ty <= f.player_ty + R; ++ty) {
            // per-tile corner heights so picks land on the same quads the overlay draws
            std::int16_t ch[4];
            rtx::cache::TileCornerHeights(tx, ty, f.plane, ch);
            auto cz = [&](int c) { return (ch[c] == -32768) ? f.player_z : 32.0f * (float)ch[c]; };
            float zSW = cz(0), zSE = cz(1), zNE = cz(2), zNW = cz(3);
            float qx[4], qy[4];
            if (!world_to_screen(f.matrix, vpX, vpY, vpW, vpH, tx * 512.f,       ty * 512.f,       zSW, qx[0], qy[0]) ||
                !world_to_screen(f.matrix, vpX, vpY, vpW, vpH, (tx + 1) * 512.f, ty * 512.f,       zSE, qx[1], qy[1]) ||
                !world_to_screen(f.matrix, vpX, vpY, vpW, vpH, (tx + 1) * 512.f, (ty + 1) * 512.f, zNE, qx[2], qy[2]) ||
                !world_to_screen(f.matrix, vpX, vpY, vpW, vpH, tx * 512.f,       (ty + 1) * 512.f, zNW, qx[3], qy[3]))
                continue;
            if (!point_in_quad(mx, my, qx, qy)) continue;
            float frontness = (qy[0] + qy[1] + qy[2] + qy[3]) * 0.25f;   // lower on screen = nearer camera
            if (frontness > bestFront) { bestFront = frontness; bx = tx; by = ty; found = true; }
        }
    if (!found) return false;
    outTx = bx; outTy = by; outPlane = f.plane;
    return true;
}

}  // namespace

void SetKeybindArmed(std::uint32_t pid, bool armed) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (armed) g_kb_armed[pid] = true;
    else g_kb_armed.erase(pid);
}
bool KeybindArmed(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_kb_armed.find(pid);
    return it != g_kb_armed.end() && it->second;
}

bool MarkAtCursor(std::uint32_t pid, int action) {
    if (!KeybindArmed(pid)) return false;             // Markers panel not open -> key falls through to the game
    int tx = 0, ty = 0, plane = 0;
    if (!cursor_tile(pid, tx, ty, plane)) return false;
    int region = ((tx >> 6) << 8) | (ty >> 6);
    int lx = tx & 63, ly = ty & 63;
    std::uint32_t col;
    { std::lock_guard<std::mutex> lk(g_mu); load_keybinds_locked(); col = g_kb.defColor; }
    if (action < 0) return Remove(pid, region, lx, ly, plane);
    return Add(pid, region, lx, ly, plane, col, std::string());
}

Keybinds GetKeybinds() {
    std::lock_guard<std::mutex> lk(g_mu);
    load_keybinds_locked();
    return g_kb;
}

void SetKeybinds(const Keybinds& kb) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_kb_loaded = true;
    g_kb = kb;
    g_kb.defColor &= 0xFFFFFF;
    save_keybinds_locked();
}

}  // namespace rtx::markers
