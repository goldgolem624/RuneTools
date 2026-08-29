#include "IconCache.h"
#include "../reader/Reader.h"   // client version -> the rendered-icon directory for this build

#include <Windows.h>
#include <wincrypt.h>

#include <cctype>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "crypt32.lib")

// Icons are served from bundled packs staged next to the exe: items.pack (item icons, noted
// items composited as item-on-note PNGs) and modelicons.pack (interface type-6 MODEL comps
// rendered with their def cameras, indexed by MODEL id). Shared format:
//   "RTIP", u32 version(1), u32 N, then N*(u32 offset, u32 len) indexed by
//   id (len 0 = absent), then the raw image blobs (gif or png).

namespace rtx::launcher::icons {

namespace {

// One bundled RTIP pack: lazily indexed, per-id data URLs memoized ("" = known miss).
struct Pack {
    const wchar_t*                       file;
    std::mutex                           mu;
    std::unordered_map<int, std::string> mem_url;
    bool                                 tried = false;
    bool                                 ready = false;
    std::wstring                         path;
    std::vector<std::uint32_t>           off, len;
    std::uint32_t                        present = 0;   // index entries with len > 0
};
// Requested-but-absent item ids (id -> request count); capped so a runaway poll cannot grow it.
std::mutex                   g_miss_mu;
std::unordered_map<int, int> g_misses;
constexpr std::size_t        kMissCap = 10000;
Pack g_items_pack { L"items.pack" };
// Icons rendered offline from the cache for items the bundled pack predates. Ships with
// the app exactly like items.pack, so a user gets them by opening the client: nothing is
// rendered or downloaded on their machine.
Pack g_extra_pack { L"items_extra.pack" };
Pack g_model_pack { L"modelicons.pack" };

// Load a pack's index once. Caller holds p.mu.
void ensure_index(Pack& p) {
    if (p.tried) return;
    p.tried = true;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    p.path = (std::filesystem::path(exe).parent_path() / p.file).wstring();
    std::ifstream f(p.path, std::ios::binary);
    if (!f) return;
    char magic[4] = {};
    f.read(magic, 4);
    if (f.gcount() != 4 || std::memcmp(magic, "RTIP", 4) != 0) return;
    std::uint32_t ver = 0, N = 0;
    f.read(reinterpret_cast<char*>(&ver), 4);
    f.read(reinterpret_cast<char*>(&N), 4);
    if (!f || N == 0 || N > 5000000u) return;
    std::vector<std::uint32_t> idx((std::size_t)N * 2);
    f.read(reinterpret_cast<char*>(idx.data()), (std::streamsize)N * 8);
    if ((std::uint32_t)(f.gcount() / 8) != N) return;
    p.off.resize(N); p.len.resize(N);
    for (std::uint32_t i = 0; i < N; ++i) {
        p.off[i] = idx[i * 2]; p.len[i] = idx[i * 2 + 1];
        if (p.len[i] > 0) ++p.present;
    }
    p.ready = true;
}

std::string base64_encode(const std::vector<unsigned char>& bytes) {
    DWORD outLen = 0;
    if (!CryptBinaryToStringA(bytes.data(), (DWORD)bytes.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen)) return {};
    std::string out((std::size_t)outLen, '\0');
    if (!CryptBinaryToStringA(bytes.data(), (DWORD)bytes.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &outLen)) return {};
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

const char* sniff_mime(const std::vector<unsigned char>& b) {
    if (b.size() >= 4 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return "image/png";        // composited noted icons
    return "image/gif";            // library default (GIF87a/GIF89a)
}

// Does this pack hold an icon for the id? Index only, so it is cheap to ask.
bool PackHas(Pack& p, int item_id) {
    if (item_id <= 0) return false;
    std::lock_guard<std::mutex> lk(p.mu);
    ensure_index(p);
    return p.ready && (std::size_t)item_id < p.len.size() && p.len[item_id] > 0;
}

}  // namespace

namespace {
std::string pack_icon_url(Pack& p, int id) {
    if (id <= 0) return {};
    std::lock_guard<std::mutex> lk(p.mu);
    auto it = p.mem_url.find(id);
    if (it != p.mem_url.end()) return it->second;

    ensure_index(p);
    if (!p.ready || (std::size_t)id >= p.off.size() || p.len[id] == 0) {
        p.mem_url[id] = "";    // cache the miss so the pack isn't re-checked
        return {};
    }
    std::ifstream f(p.path, std::ios::binary);
    std::string url;
    if (f) {
        f.seekg((std::streamoff)p.off[id], std::ios::beg);
        std::vector<unsigned char> bytes(p.len[id]);
        f.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)p.len[id]);
        if ((std::uint32_t)f.gcount() == p.len[id]) {
            std::string b64 = base64_encode(bytes);
            if (!b64.empty()) url = std::string("data:") + sniff_mime(bytes) + ";base64," + b64;
        }
    }
    p.mem_url[id] = url;
    return url;
}
}  // namespace

// ---- Offline-rendered icons ------------------------------------------------------------
// Icons are rendered from the game cache into
//   %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<itemId>.png
// and are served before items.pack, so a build newer than the bundled pack still shows art.

std::wstring icons_root() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return (std::filesystem::path(buf) / L"RuneToolsX" / L"icons").wstring();
}

std::wstring sanitize_version(const std::string& v) {
    std::wstring out;
    for (char c : v) out += (std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? (wchar_t)c : L'_';
    return out;
}

// Newest version directory under the icons root (by mtime), so a launcher started with no
// client attached still serves the last render instead of nothing.
std::wstring newest_dir() {
    std::wstring root = icons_root();
    if (root.empty()) return {};
    std::error_code ec;
    std::wstring best;
    std::filesystem::file_time_type bestT{};
    for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        auto t = e.last_write_time(ec);
        if (best.empty() || t > bestT) { best = e.path().wstring(); bestT = t; }
    }
    return best;
}

// The directory serving icons this session, or "" when there is none to trust.
// A directory is used ONLY when it holds a `.validated` marker: a half-written render, or
// one from the wrong source, would answer every lookup and silently shadow items.pack, and
// because it answers them no miss is ever logged to reveal the bad art.
std::mutex   g_dir_mu;
bool         g_dir_done = false;
std::wstring g_dir;
std::wstring active_dir() {
    std::lock_guard<std::mutex> lk(g_dir_mu);
    if (g_dir_done) return g_dir;
    g_dir_done = true;
    std::wstring root = icons_root();
    std::wstring dir;
    if (!root.empty()) {
        std::string ver;
        try {
            for (const auto& snap : rtx::reader::SampleAll())
                if (snap.pid && !snap.client_version.empty()) { ver = snap.client_version; break; }
        } catch (...) {}
        std::error_code ec;
        std::wstring vdir = sanitize_version(ver);
        if (!vdir.empty()) {
            std::filesystem::path p = std::filesystem::path(root) / vdir;
            if (std::filesystem::is_directory(p, ec)) dir = p.wstring();
        }
        if (dir.empty()) dir = newest_dir();
    }
    std::error_code ec;
    if (!dir.empty() && !std::filesystem::exists(std::filesystem::path(dir) / L".validated", ec)) dir.clear();
    g_dir = dir;
    return g_dir;
}

// Path of a rendered PNG for the id, or "" (memoized per id: one stat per id per session).
std::mutex                            g_ren_mu;
std::unordered_map<int, std::wstring> g_ren_path;
std::wstring rendered_png(int item_id) {
    if (item_id <= 0) return {};
    {
        std::lock_guard<std::mutex> lk(g_ren_mu);
        auto it = g_ren_path.find(item_id);
        if (it != g_ren_path.end()) return it->second;
    }
    std::wstring dir = active_dir();
    std::wstring path;
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::path p = std::filesystem::path(dir) / (std::to_wstring(item_id) + L".png");
        if (std::filesystem::exists(p, ec)) path = p.wstring();
    }
    std::lock_guard<std::mutex> lk(g_ren_mu);
    if (g_ren_path.size() > 20000) g_ren_path.clear();   // bound the memo
    g_ren_path[item_id] = path;
    return path;
}

// Rendered PNG -> data URL, memoized per id alongside the path memo.
std::mutex                           g_ren_url_mu;
std::unordered_map<int, std::string> g_ren_url;
std::string rendered_icon_url(int item_id) {
    std::wstring path = rendered_png(item_id);
    if (path.empty()) return {};
    std::lock_guard<std::mutex> lk(g_ren_url_mu);
    auto it = g_ren_url.find(item_id);
    if (it != g_ren_url.end() && !it->second.empty()) return it->second;
    std::string url;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f) {
        std::streamoff sz = f.tellg();
        if (sz > 0 && sz < 4 * 1024 * 1024) {
            f.seekg(0, std::ios::beg);
            std::vector<unsigned char> bytes((size_t)sz);
            f.read(reinterpret_cast<char*>(bytes.data()), sz);
            if ((std::streamoff)f.gcount() == sz) {
                std::string b64 = base64_encode(bytes);
                if (!b64.empty()) url = "data:image/png;base64," + b64;
            }
        }
    }
    g_ren_url[item_id] = url;
    return url;
}

int IconSource(int item_id) {
    if (item_id <= 0) return 0;
    if (!rendered_png(item_id).empty()) return 2;   // local directory (development override)
    if (PackHas(g_extra_pack, item_id)) return 2;   // shipped render
    return IconPackHas(item_id) ? 1 : 0;
}

std::string ItemIconDataUrl(int item_id) {
    std::string url = rendered_icon_url(item_id);
    if (url.empty()) url = pack_icon_url(g_extra_pack, item_id);
    if (url.empty()) url = pack_icon_url(g_items_pack, item_id);
    if (url.empty() && item_id > 0) {
        {
            std::lock_guard<std::mutex> lk(g_miss_mu);
            auto it = g_misses.find(item_id);
            if (it != g_misses.end()) ++it->second;
            else if (g_misses.size() < kMissCap) g_misses[item_id] = 1;
        }
    }
    return url;
}

bool IconPackHas(int item_id) { return PackHas(g_items_pack, item_id); }

std::string IconMissesJson() {
    std::uint32_t ids = 0, present = 0;
    {
        Pack& p = g_items_pack;
        std::lock_guard<std::mutex> lk(p.mu);
        ensure_index(p);
        if (p.ready) { ids = (std::uint32_t)p.len.size(); present = p.present; }
    }
    std::vector<std::pair<int, int>> m;
    {
        std::lock_guard<std::mutex> lk(g_miss_mu);
        m.assign(g_misses.begin(), g_misses.end());
    }
    std::sort(m.begin(), m.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first; });
    std::string j = "{\"packIds\":" + std::to_string(ids) + ",\"packIcons\":" + std::to_string(present) + ",\"misses\":[";
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (i) j += ',';
        j += '[' + std::to_string(m[i].first) + ',' + std::to_string(m[i].second) + ']';
    }
    j += "],\"missCount\":" + std::to_string(m.size()) + ",\"rendered\":" +
         std::to_string(RenderedIconCount()) + "}";
    return j;
}

int RenderedIconCount() {
    static std::mutex mu;
    static int        cached = -1;      // counted once per process: a directory walk per poll is not free
    std::lock_guard<std::mutex> lk(mu);
    if (cached >= 0) return cached;
    int n = 0;
    {
        Pack& p = g_extra_pack;                 // shipped renders: every user has these
        std::lock_guard<std::mutex> lk(p.mu);
        ensure_index(p);
        if (p.ready) n += (int)p.present;
    }
    std::wstring dir = active_dir();
    if (!dir.empty()) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == L".png") ++n;
        }
    }
    cached = n;
    return cached;
}

std::string ModelIconDataUrl(int model_id) {
    return pack_icon_url(g_model_pack, model_id);
}

std::string AssetFileBase64(const std::wstring& filename) {
    static std::mutex                              mu;
    static std::unordered_map<std::wstring, std::string> cache;   // name -> base64 ("" = miss)
    std::lock_guard<std::mutex> lk(mu);
    auto it = cache.find(filename);
    if (it != cache.end()) return it->second;
    std::string out;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path = (std::filesystem::path(exe).parent_path() / filename).wstring();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f) {
        std::streamoff sz = f.tellg();
        if (sz > 0 && sz <= 64ll * 1024 * 1024) {           // sane bound (tables are ~10-26MB)
            f.seekg(0, std::ios::beg);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(sz));
            f.read(reinterpret_cast<char*>(bytes.data()), sz);
            if (static_cast<std::streamoff>(f.gcount()) == sz) out = base64_encode(bytes);
        }
    }
    cache[filename] = out;
    return out;
}

}  // namespace rtx::launcher::icons
