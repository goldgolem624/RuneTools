#include "IconCapture.h"
#include "IconCache.h"
#include "IconScore.h"
#include "PngDecode.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"
#include "../../companion/IconAtlasShare.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
    s->wantName = 0; s->candidateCount = 0; std::memset(s->candidates, 0, sizeof(s->candidates));
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
std::uint32_t  g_lastAtlasName = 0;   // GL name of the atlas the last capture used (0 = none)
double         g_lastAtlasScore = 0;  // its pack-similarity score
std::string    g_lastCandidates;      // "name:score,name:score" of the last validation pass

// Validated atlas for this session: the client holds several 1536x1536 textures with
// icon-like content, so a name is only trusted after its pixels scored against the pack.
// Re-validated when the container moves or a later capture's score drops.
std::uint32_t  g_validName = 0;
std::uint64_t  g_validContainer = 0;
bool           g_purged = false;      // wrong-texture leftovers removed once per session

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

// Grid / make_grid / grid_similarity live in IconScore.h (shared with the offline test).

// Reference set: up to 40 item cells (flavour 0x02, 36x32) that the pack also has,
// spread across the map so one bad region cannot dominate.
using iconscore::Grid; using iconscore::make_grid; using iconscore::grid_similarity;
struct Ref { rtx::reader::IconAtlasCell cell; Grid grid; };
std::vector<Ref> pick_refs(const rtx::reader::IconAtlasMap& map) {
    std::vector<const rtx::reader::IconAtlasCell*> eligible;
    for (const auto& c : map.cells)
        if (c.flavour == 0x02 && c.w == 36 && c.h == 32 && c.item > 0 && icons::IconPackHas(c.item))
            eligible.push_back(&c);
    std::vector<Ref> refs;
    if (eligible.empty()) return refs;
    const size_t step = std::max<size_t>(1, eligible.size() / 40);
    for (size_t i = 0; i < eligible.size() && refs.size() < 40; i += step) {
        std::vector<unsigned char> bytes = icons::IconPackBytes(eligible[i]->item);
        png::Image img;
        if (bytes.empty() || !png::Decode(bytes.data(), bytes.size(), img)) continue;
        Grid g = make_grid(img.rgba.data(), img.w, img.h, img.w * 4);
        if (!g.any) continue;
        refs.push_back({ *eligible[i], g });
    }
    return refs;
}

double score_pixels(const std::uint8_t* pixels, const std::vector<Ref>& refs) {
    if (refs.empty()) return 0.0;
    const std::uint32_t W = rtx::iconatlas::kAtlasW;
    double sum = 0;
    for (const auto& r : refs) {
        const std::uint8_t* src = pixels + ((size_t)r.cell.y * W + (size_t)r.cell.x) * 4;
        sum += grid_similarity(make_grid(src, r.cell.w, r.cell.h, (int)W * 4), r.grid);
    }
    return sum / (double)refs.size();
}

// One hook round trip. Returns false on timeout / GL failure (status set by the caller).
bool request_readback(rtx::iconatlas::Share* s, std::uint64_t texOwner, std::uint32_t wantName) {
    std::uint32_t seq0 = s->seq;
    s->hintTexOwner = texOwner;
    s->wantName = wantName;
    s->request = 1;
    const ULONGLONG deadline = GetTickCount64() + 2000;
    while (s->seq == seq0 && GetTickCount64() < deadline) Sleep(20);
    if (s->seq == seq0) { s->request = 0; return false; }
    return true;
}

std::string fmt_score(double v) {
    char b[32]; std::snprintf(b, sizeof(b), "%.3f", v); return b;
}

wchar_t* cell_file_name(const rtx::reader::IconAtlasCell& c, wchar_t* name, size_t n) {
    if (c.flavour == 0x02 && c.w == 36 && c.h == 32) swprintf(name, n, L"%d.png", c.item);
    else swprintf(name, n, L"%d_f%02x.png", c.item, c.flavour);
    return name;
}

bool file_equals(const fs::path& file, const std::vector<std::uint8_t>& bytes) {
    std::ifstream f(file, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff sz = f.tellg();
    if (sz != (std::streamoff)bytes.size()) return false;
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> cur((size_t)sz);
    f.read(reinterpret_cast<char*>(cur.data()), sz);
    return (std::streamoff)f.gcount() == sz && cur == bytes;
}

// Drop icons written from the wrong texture. Every file that maps to a current cell is
// re-sliced and byte-compared. A directory without the `.validated` marker (written by
// captures before validation existed) additionally loses every file that cannot be
// checked, since nothing in it can be trusted; once marked, unmapped files (an icon from
// a session where the bank was open) are kept.
int purge_dir(const fs::path& dir, const rtx::reader::IconAtlasMap& map, const std::uint8_t* pixels) {
    const std::uint32_t W = rtx::iconatlas::kAtlasW, H = rtx::iconatlas::kAtlasH;
    std::error_code ec;
    const fs::path marker = dir / L".validated";
    const bool trusted = fs::exists(marker, ec);
    std::unordered_map<std::wstring, const rtx::reader::IconAtlasCell*> byName;
    for (const auto& c : map.cells) {
        if (c.item <= 0 || c.w <= 0 || c.h <= 0 || c.x < 0 || c.y < 0 ||
            (std::uint32_t)(c.x + c.w) > W || (std::uint32_t)(c.y + c.h) > H) continue;
        wchar_t name[64]; byName[cell_file_name(c, name, 64)] = &c;
    }
    int removed = 0;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::wstring fn = e.path().filename().wstring();
        if (fn.size() < 5 || fn.compare(fn.size() - 4, 4, L".png") != 0) continue;
        auto it = byName.find(fn);
        bool keep;
        if (it == byName.end()) keep = trusted;
        else {
            const auto& c = *it->second;
            const std::uint8_t* src = pixels + ((size_t)c.y * W + (size_t)c.x) * 4;
            keep = file_equals(e.path(), encode_png(src, c.w, c.h, (int)W * 4));
        }
        if (!keep && fs::remove(e.path(), ec)) ++removed;
    }
    if (!trusted) { std::ofstream m(marker, std::ios::binary); m << "atlas validated by pack similarity\n"; }
    mark_trusted(dir.wstring());
    return removed;
}

}  // namespace

int CaptureIcons(std::uint32_t pid) {
    if (!pid) { set_status("no client", 0, 0); return 0; }
    // 1. cell map from the reader (also proves the pid is attached)
    rtx::reader::IconAtlasMap map = rtx::reader::ReadIconAtlasMap(pid);
    if (map.w <= 0 || map.cells.empty()) { set_status("no map (open the backpack)", 0, 0); return 0; }
    const int cells = (int)map.cells.size();
    if (map.w != (int)rtx::iconatlas::kAtlasW || map.h != (int)rtx::iconatlas::kAtlasH) {
        set_status("atlas size " + std::to_string(map.w) + "x" + std::to_string(map.h) + " unsupported", 0, cells);
        return 0;
    }
    // 2. reference icons from the pack (the validator needs at least a handful)
    std::vector<Ref> refs = pick_refs(map);
    if (refs.size() < 5) { set_status("too few pack references (" + std::to_string(refs.size()) + ")", 0, cells); return 0; }
    // 3. ask the hook: first request lists candidates and reads the remembered name (or
    //    candidates[0]); each further candidate is a separate readback, all scored.
    rtx::iconatlas::Share* s = ShareFor(pid);
    if (!s) { set_status("share failed", 0, cells); return 0; }
    std::uint32_t remembered = 0;
    { std::lock_guard<std::mutex> lk(g_stMu); if (g_validContainer == map.container) remembered = g_validName; }
    if (!request_readback(s, map.texOwner, remembered)) { set_status("timeout (companion not loaded?)", 0, cells); return 0; }
    if (s->status == rtx::iconatlas::kStatusNoTexture) { set_status("no atlas texture found", 0, cells); return 0; }
    if (s->status != rtx::iconatlas::kStatusOk) { set_status("gl error " + std::to_string(s->glError), 0, cells); return 0; }

    const size_t kBytes = (size_t)rtx::iconatlas::kAtlasW * rtx::iconatlas::kAtlasH * 4;
    std::vector<std::uint32_t> candidates(s->candidates, s->candidates + std::min<std::uint32_t>(s->candidateCount, rtx::iconatlas::kMaxCandidates));
    if (std::find(candidates.begin(), candidates.end(), s->glName) == candidates.end()) candidates.insert(candidates.begin(), s->glName);
    const std::uint32_t hintHit = s->hintHit;

    std::vector<std::uint8_t> best;               // pixels of the best-scoring candidate
    std::uint32_t bestName = 0; double bestScore = -1, secondScore = -1;
    std::string scoreList;
    auto consider = [&](std::uint32_t name, double sc) {
        if (!scoreList.empty()) scoreList += ",";
        scoreList += std::to_string(name) + ":" + fmt_score(sc);
        if (sc > bestScore) { secondScore = bestScore; bestScore = sc; bestName = name; best.assign(s->pixels, s->pixels + kBytes); }
        else if (sc > secondScore) secondScore = sc;
    };
    double first = score_pixels(s->pixels, refs);
    consider(s->glName, first);
    // A remembered, still-good atlas skips the other readbacks; a drop re-validates all.
    const bool skipRest = remembered && s->glName == remembered && first >= 0.6;
    if (!skipRest) {
        for (std::uint32_t name : candidates) {
            if (name == s->glName) continue;
            if (!request_readback(s, map.texOwner, name)) { rtx::log::Launcher("icon capture: readback timeout on tex " + std::to_string(name)); continue; }
            if (s->status != rtx::iconatlas::kStatusOk || s->glName != name) {
                rtx::log::Launcher("icon capture: tex " + std::to_string(name) + " readback failed (status " + std::to_string(s->status) + ", gl " + std::to_string(s->glError) + ")");
                continue;
            }
            consider(name, score_pixels(s->pixels, refs));
        }
    }
    rtx::log::Launcher("icon capture: pid " + std::to_string(pid) + " candidates " + std::to_string(candidates.size()) +
                       " (hint " + std::to_string(hintHit) + ") scores " + scoreList + " refs " + std::to_string(refs.size()));
    {
        std::lock_guard<std::mutex> lk(g_stMu);
        g_lastCandidates = scoreList; g_lastAtlasName = bestName; g_lastAtlasScore = bestScore;
    }
    const bool passes = bestScore >= 0.6 && (secondScore < 0 || bestScore - secondScore >= 0.1);
    if (!passes) {
        { std::lock_guard<std::mutex> lk(g_stMu); g_validName = 0; g_validContainer = 0; }
        set_status("no matching atlas", 0, cells);
        rtx::log::Launcher("icon capture: no matching atlas (best " + std::to_string(bestName) + " " + fmt_score(bestScore) + ")");
        return 0;
    }
    {
        std::lock_guard<std::mutex> lk(g_stMu);
        g_validName = bestName; g_validContainer = map.container;
    }
    // 4. slice
    std::string ver;
    for (const auto& snap : rtx::reader::SampleAll()) if (snap.pid == pid) ver = snap.client_version;
    std::wstring root = icons_root();
    if (root.empty()) { set_status("no USERPROFILE", 0, cells); return 0; }
    fs::path dir = fs::path(root) / sanitize_version(ver);
    std::error_code ec; fs::create_directories(dir, ec);
    int removed = 0;
    bool doPurge;
    { std::lock_guard<std::mutex> lk(g_stMu); doPurge = !g_purged; g_purged = true; }
    if (doPurge) removed = purge_dir(dir, map, best.data());
    const std::uint32_t W = rtx::iconatlas::kAtlasW, H = rtx::iconatlas::kAtlasH;
    int written = 0; std::vector<int> ids;
    for (const auto& c : map.cells) {
        if (c.item <= 0 || c.w <= 0 || c.h <= 0 || c.x < 0 || c.y < 0 ||
            (std::uint32_t)(c.x + c.w) > W || (std::uint32_t)(c.y + c.h) > H) continue;
        wchar_t name[64];
        fs::path file = dir / cell_file_name(c, name, 64);
        if (fs::exists(file, ec)) continue;
        const std::uint8_t* src = best.data() + ((size_t)c.y * W + (size_t)c.x) * 4;
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
        if (removed) g_lookup.clear();
        else for (int id : ids) g_lookup.erase(id);
    }
    icons::ForgetMisses(ids);
    set_status("ok", written, cells);
    rtx::log::Client(pid, "icon capture: " + std::to_string(written) + " new of " + std::to_string(cells) +
                     " cells (hint " + std::to_string(hintHit) + ", tex " + std::to_string(bestName) +
                     ", score " + fmt_score(bestScore) + ", removed " + std::to_string(removed) + ")");
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

// A version directory is only trusted once a validated capture wrote its `.validated` marker.
// Files from an unvalidated atlas (an earlier build wrote 1,507 of them from the wrong texture)
// must not be served: they would also satisfy every lookup, so no miss would ever trigger the
// validated capture that replaces them.
static std::mutex g_trustMu;
static std::wstring g_trustDir;
static bool g_trusted = false;
static bool dir_trusted(const std::wstring& dir) {
    std::lock_guard<std::mutex> lk(g_trustMu);
    if (dir != g_trustDir) {
        g_trustDir = dir;
        std::error_code ec;
        g_trusted = !dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / L".validated", ec);
    }
    return g_trusted;
}
static void mark_trusted(const std::wstring& dir) {
    std::lock_guard<std::mutex> lk(g_trustMu);
    g_trustDir = dir; g_trusted = true;
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
    if (!dir_trusted(dir)) return {};
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
           ",\"atlasName\":" + std::to_string(g_lastAtlasName) + ",\"atlasScore\":" + fmt_score(g_lastAtlasScore) +
           ",\"candidates\":\"" + g_lastCandidates + "\"" +
           ",\"dir\":\"" + dir + "\"}";
}

}  // namespace rtx::launcher::iconcapture
