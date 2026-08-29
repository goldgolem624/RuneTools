#include "IconCapture.h"
#include "IconCache.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"
#include "../../companion/IconAtlasShare.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace rtx::launcher::iconcapture {

namespace {

// ---- minimal PNG writer (RGBA8, stored deflate blocks) --------------------------------
// ThirdParty/stb has no stb_image_write.h; icons are 36x32 so compression is irrelevant
// and a stored-block zlib stream keeps this dependency free.
std::uint32_t crc32_of(const std::uint8_t* d, size_t n, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back((std::uint8_t)(x >> 24)); v.push_back((std::uint8_t)(x >> 16));
    v.push_back((std::uint8_t)(x >> 8));  v.push_back((std::uint8_t)x);
}
void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    put32(out, (std::uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32_of(out.data() + start, out.size() - start));
}
std::vector<std::uint8_t> encode_png(const std::uint8_t* rgba, int w, int h, int stride) {
    std::vector<std::uint8_t> raw;                       // filter byte 0 + row, per row
    raw.reserve((size_t)h * (w * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * stride, rgba + (size_t)y * stride + (size_t)w * 4);
    }
    std::vector<std::uint8_t> z;                          // zlib: header, stored blocks, adler32
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    do {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((std::uint8_t)n); z.push_back((std::uint8_t)(n >> 8));
        z.push_back((std::uint8_t)~n); z.push_back((std::uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, (b << 16) | a);

    std::vector<std::uint8_t> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, (std::uint32_t)w); put32(ihdr, (std::uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

// ---- share (launcher owns/creates it, one per pid) --------------------------------------
struct AtlasMap { HANDLE h = nullptr; rtx::iconatlas::Share* s = nullptr; };
std::mutex g_mapMu;
std::unordered_map<std::uint32_t, AtlasMap> g_maps;
rtx::iconatlas::Share* ShareFor(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mapMu);
    auto it = g_maps.find(pid);
    if (it != g_maps.end()) return it->second.s;
    wchar_t name[64]; rtx::iconatlas::MakeSectionName(pid, name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::iconatlas::Share), name);
    if (!h) return nullptr;
    auto* s = reinterpret_cast<rtx::iconatlas::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::iconatlas::Share)));
    if (!s) { CloseHandle(h); return nullptr; }
    s->request = 0; s->seq = 0; s->status = 0; s->width = s->height = 0;
    s->glName = 0; s->glError = 0; s->hintTexOwner = 0; s->hintHit = 0; s->capturedAtMs = 0;
    s->pid = pid; s->version = rtx::iconatlas::kVersion; s->magic = rtx::iconatlas::kMagic;
    g_maps[pid] = { h, s };
    return s;
}

// ---- state ------------------------------------------------------------------------------
std::mutex     g_stMu;
std::uint64_t  g_lastAtMs = 0;        // epoch ms of the last completed capture
std::uint64_t  g_lastStartMs = 0;     // GetTickCount64 of the last capture start (rate limit)
int            g_lastWritten = 0, g_lastCells = 0, g_sessionWritten = 0;
std::string    g_lastStatus = "idle";
bool           g_busy = false;
std::wstring   g_dir;                 // version directory of the last capture (lookup target)

std::mutex     g_lookMu;
std::unordered_map<int, std::wstring> g_lookup;   // id -> path ("" = known absent)

std::wstring icons_root() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return (fs::path(buf) / L"RuneToolsX" / L"icons").wstring();
}

std::wstring sanitize_version(const std::string& v) {
    std::wstring out;
    for (char c : v) out += (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? (wchar_t)c : L'_';
    return out.empty() ? L"unknown" : out;
}

// Newest version directory under the icons root (by name, then mtime) so a launcher
// restart still serves the last capture before any new one runs.
std::wstring newest_dir() {
    std::wstring root = icons_root();
    if (root.empty()) return {};
    std::error_code ec;
    std::wstring best; fs::file_time_type bestT{};
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        auto t = e.last_write_time(ec);
        if (best.empty() || t > bestT) { best = e.path().wstring(); bestT = t; }
    }
    return best;
}

std::uint64_t epoch_ms() {
    return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void set_status(const std::string& st, int written, int cells) {
    std::lock_guard<std::mutex> lk(g_stMu);
    g_lastStatus = st; g_lastWritten = written; g_lastCells = cells;
    g_sessionWritten += written; g_lastAtMs = epoch_ms();
}

}  // namespace

int CaptureIcons(std::uint32_t pid) {
    if (!pid) { set_status("no client", 0, 0); return 0; }
    // 1. cell map from the reader (also proves the pid is attached)
    rtx::reader::IconAtlasMap map = rtx::reader::ReadIconAtlasMap(pid);
    if (map.w <= 0 || map.cells.empty()) { set_status("no map (open the backpack)", 0, 0); return 0; }
    if (map.w != (int)rtx::iconatlas::kAtlasW || map.h != (int)rtx::iconatlas::kAtlasH) {
        set_status("atlas size " + std::to_string(map.w) + "x" + std::to_string(map.h) + " unsupported", 0, (int)map.cells.size());
        return 0;
    }
    // 2. ask the hook for the pixels
    rtx::iconatlas::Share* s = ShareFor(pid);
    if (!s) { set_status("share failed", 0, (int)map.cells.size()); return 0; }
    std::uint32_t seq0 = s->seq;
    s->hintTexOwner = map.texOwner;
    s->request = 1;
    const ULONGLONG deadline = GetTickCount64() + 2000;
    while (s->seq == seq0 && GetTickCount64() < deadline) Sleep(20);
    if (s->seq == seq0) { s->request = 0; set_status("timeout (companion not loaded?)", 0, (int)map.cells.size()); return 0; }
    if (s->status == rtx::iconatlas::kStatusNoTexture) { set_status("no atlas texture found", 0, (int)map.cells.size()); return 0; }
    if (s->status != rtx::iconatlas::kStatusOk) { set_status("gl error " + std::to_string(s->glError), 0, (int)map.cells.size()); return 0; }
    // 3. slice
    std::string ver;
    for (const auto& snap : rtx::reader::SampleAll()) if (snap.pid == pid) ver = snap.client_version;
    std::wstring root = icons_root();
    if (root.empty()) { set_status("no USERPROFILE", 0, (int)map.cells.size()); return 0; }
    fs::path dir = fs::path(root) / sanitize_version(ver);
    std::error_code ec; fs::create_directories(dir, ec);
    const std::uint32_t W = rtx::iconatlas::kAtlasW, H = rtx::iconatlas::kAtlasH;
    int written = 0; std::vector<int> ids;
    for (const auto& c : map.cells) {
        if (c.item <= 0 || c.w <= 0 || c.h <= 0 || c.x < 0 || c.y < 0 ||
            (std::uint32_t)(c.x + c.w) > W || (std::uint32_t)(c.y + c.h) > H) continue;
        wchar_t name[64];
        if (c.flavour == 0x02 && c.w == 36 && c.h == 32) swprintf(name, 64, L"%d.png", c.item);
        else swprintf(name, 64, L"%d_f%02x.png", c.item, c.flavour);
        fs::path file = dir / name;
        if (fs::exists(file, ec)) continue;
        const std::uint8_t* src = s->pixels + ((size_t)c.y * W + (size_t)c.x) * 4;
        std::vector<std::uint8_t> png = encode_png(src, c.w, c.h, (int)W * 4);
        std::ofstream f(file, std::ios::binary);
        if (!f) continue;
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
        ++written; ids.push_back(c.item);
    }
    {
        std::lock_guard<std::mutex> lk(g_stMu);
        g_dir = dir.wstring();
    }
    {
        // captured ids must be re-looked-up: the "absent" memo is now stale for them
        std::lock_guard<std::mutex> lk(g_lookMu);
        for (int id : ids) g_lookup.erase(id);
    }
    icons::ForgetMisses(ids);
    set_status("ok", written, (int)map.cells.size());
    rtx::log::Client(pid, "icon capture: " + std::to_string(written) + " new of " +
                     std::to_string(map.cells.size()) + " cells (hint " + std::to_string(s->hintHit) +
                     ", tex " + std::to_string(s->glName) + ")");
    return written;
}

bool CaptureAsync(std::uint32_t pid) {
    {
        std::lock_guard<std::mutex> lk(g_stMu);
        if (g_busy) return false;
        g_busy = true; g_lastStartMs = GetTickCount64();
    }
    std::thread([pid] {
        try { CaptureIcons(pid); } catch (...) { set_status("exception", 0, 0); }
        std::lock_guard<std::mutex> lk(g_stMu);
        g_busy = false;
    }).detach();
    return true;
}

void NoteMiss(int) {
    {
        std::lock_guard<std::mutex> lk(g_stMu);
        if (g_busy || GetTickCount64() - g_lastStartMs < 10000) return;
        g_busy = true; g_lastStartMs = GetTickCount64();   // claim the slot before the thread exists
    }
    std::thread([] {
        std::uint32_t pid = 0;
        try {
            for (const auto& snap : rtx::reader::SampleAll()) if (snap.pid) { pid = snap.pid; break; }
            if (pid) CaptureIcons(pid);
        } catch (...) { set_status("exception", 0, 0); }
        std::lock_guard<std::mutex> lk(g_stMu);
        g_busy = false;
    }).detach();
}

std::wstring FindCapturedPng(int item_id) {
    if (item_id <= 0) return {};
    {
        std::lock_guard<std::mutex> lk(g_lookMu);
        auto it = g_lookup.find(item_id);
        if (it != g_lookup.end()) return it->second;
    }
    std::wstring dir;
    { std::lock_guard<std::mutex> lk(g_stMu); dir = g_dir; }
    if (dir.empty()) {
        dir = newest_dir();
        std::lock_guard<std::mutex> lk(g_stMu);
        if (g_dir.empty()) g_dir = dir;
    }
    std::wstring path;
    if (!dir.empty()) {
        std::error_code ec;
        fs::path p = fs::path(dir) / (std::to_wstring(item_id) + L".png");
        if (fs::exists(p, ec)) path = p.wstring();
    }
    std::lock_guard<std::mutex> lk(g_lookMu);
    if (g_lookup.size() > 20000) g_lookup.clear();   // bound the memo
    g_lookup[item_id] = path;
    return path;
}

std::string StatusJson() {
    std::lock_guard<std::mutex> lk(g_stMu);
    std::string dir;
    for (wchar_t c : g_dir) { if (c == L'\\') dir += "\\\\"; else if (c < 0x80 && c != L'"') dir += (char)c; }
    return "{\"lastAt\":" + std::to_string(g_lastAtMs) + ",\"written\":" + std::to_string(g_lastWritten) +
           ",\"cells\":" + std::to_string(g_lastCells) + ",\"status\":\"" + g_lastStatus +
           "\",\"sessionWritten\":" + std::to_string(g_sessionWritten) + ",\"busy\":" + (g_busy ? "1" : "0") +
           ",\"dir\":\"" + dir + "\"}";
}

}  // namespace rtx::launcher::iconcapture
