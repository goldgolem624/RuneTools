#include "IconCache.h"
#include "IconCapture.h"   // live-captured PNGs beat the pack; misses schedule a capture

#include <Windows.h>
#include <wincrypt.h>

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

// Captured PNG (this client build's own render, see IconCapture.h) -> data URL. Memoized
// per id here; IconCapture owns the path memo and drops it for ids a capture just wrote.
std::mutex                          g_cap_mu;
std::unordered_map<int, std::string> g_cap_url;   // id -> data URL ("" = no file at lookup time)
std::string captured_icon_url(int item_id) {
    std::wstring path = iconcapture::FindCapturedPng(item_id);
    if (path.empty()) return {};
    std::lock_guard<std::mutex> lk(g_cap_mu);
    auto it = g_cap_url.find(item_id);
    if (it != g_cap_url.end() && !it->second.empty()) return it->second;
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
    g_cap_url[item_id] = url;
    return url;
}

int IconSource(int item_id) {
    if (item_id <= 0) return 0;
    if (!iconcapture::FindCapturedPng(item_id).empty()) return 2;
    return IconPackHas(item_id) ? 1 : 0;
}

std::string ItemIconDataUrl(int item_id) {
    std::string url = captured_icon_url(item_id);
    if (url.empty()) url = pack_icon_url(g_items_pack, item_id);
    if (url.empty() && item_id > 0) {
        {
            std::lock_guard<std::mutex> lk(g_miss_mu);
            auto it = g_misses.find(item_id);
            if (it != g_misses.end()) ++it->second;
            else if (g_misses.size() < kMissCap) g_misses[item_id] = 1;
        }
        iconcapture::NoteMiss(item_id);   // rate limited inside; no-op without a client
    }
    return url;
}

void ForgetMisses(const std::vector<int>& ids) {
    std::lock_guard<std::mutex> lk(g_miss_mu);
    for (int id : ids) g_misses.erase(id);
}

bool IconPackHas(int item_id) {
    if (item_id <= 0) return false;
    Pack& p = g_items_pack;
    std::lock_guard<std::mutex> lk(p.mu);
    ensure_index(p);
    return p.ready && (std::size_t)item_id < p.len.size() && p.len[item_id] > 0;
}

std::vector<unsigned char> IconPackBytes(int item_id) {
    if (item_id <= 0) return {};
    Pack& p = g_items_pack;
    std::lock_guard<std::mutex> lk(p.mu);
    ensure_index(p);
    if (!p.ready || (std::size_t)item_id >= p.len.size() || p.len[item_id] == 0) return {};
    std::ifstream f(p.path, std::ios::binary);
    if (!f) return {};
    f.seekg((std::streamoff)p.off[item_id], std::ios::beg);
    std::vector<unsigned char> bytes(p.len[item_id]);
    f.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)p.len[item_id]);
    if ((std::uint32_t)f.gcount() != p.len[item_id]) return {};
    return bytes;
}

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
    j += "],\"missCount\":" + std::to_string(m.size()) + "}";
    return j;
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
