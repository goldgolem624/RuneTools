#include "IconCache.h"

#include <Windows.h>
#include <wincrypt.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "crypt32.lib")

// Item icons are served from a single bundled pack (items.pack, staged next to the exe); it
// composites noted items as item-on-note PNGs. Format:
//   "RTIP", u32 version(1), u32 N, then N*(u32 offset, u32 len) indexed by item
//   id (len 0 = absent), then the raw image blobs (gif or png).

namespace rtx::launcher::icons {

namespace {

std::mutex                           g_mu;
std::unordered_map<int, std::string> g_mem_url;     // id -> data URL ("" = known miss)
bool                                 g_tried  = false;
bool                                 g_ready  = false;
std::wstring                         g_pack_path;
std::vector<std::uint32_t>           g_off, g_len;   // per item id

std::wstring pack_path() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return (std::filesystem::path(exe).parent_path() / L"items.pack").wstring();
}

// Load the pack index once. Caller holds g_mu.
void ensure_index() {
    if (g_tried) return;
    g_tried = true;
    g_pack_path = pack_path();
    std::ifstream f(g_pack_path, std::ios::binary);
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
    g_off.resize(N); g_len.resize(N);
    for (std::uint32_t i = 0; i < N; ++i) { g_off[i] = idx[i * 2]; g_len[i] = idx[i * 2 + 1]; }
    g_ready = true;
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

std::string ItemIconDataUrl(int item_id) {
    if (item_id <= 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_mem_url.find(item_id);
    if (it != g_mem_url.end()) return it->second;

    ensure_index();
    if (!g_ready || (std::size_t)item_id >= g_off.size() || g_len[item_id] == 0) {
        g_mem_url[item_id] = "";    // cache the miss so the pack isn't re-checked
        return {};
    }
    std::ifstream f(g_pack_path, std::ios::binary);
    std::string url;
    if (f) {
        f.seekg((std::streamoff)g_off[item_id], std::ios::beg);
        std::vector<unsigned char> bytes(g_len[item_id]);
        f.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)g_len[item_id]);
        if ((std::uint32_t)f.gcount() == g_len[item_id]) {
            std::string b64 = base64_encode(bytes);
            if (!b64.empty()) url = std::string("data:") + sniff_mime(bytes) + ";base64," + b64;
        }
    }
    g_mem_url[item_id] = url;
    return url;
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
