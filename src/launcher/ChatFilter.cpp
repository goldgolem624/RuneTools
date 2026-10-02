#include "ChatFilter.h"
#include "../../companion/ChatShare.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace rtx::launcher::chatfilter {
namespace {

struct View {
    HANDLE                 map = nullptr;
    rtx::chatmute::Share*  sh  = nullptr;

    explicit View(std::uint32_t pid) {
        if (!pid) return;
        wchar_t name[rtx::ipc::kNameChars];
        rtx::chatmute::MakeSectionName(pid, name);
        map = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!map) return;
        sh = (rtx::chatmute::Share*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(rtx::chatmute::Share));
        if (sh && (sh->magic != rtx::chatmute::kMagic || sh->version != rtx::chatmute::kVersion)) {
            UnmapViewOfFile(sh);
            sh = nullptr;
        }
    }
    ~View() {
        if (sh) UnmapViewOfFile(sh);
        if (map) CloseHandle(map);
    }
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    explicit operator bool() const { return sh != nullptr; }
};

std::string JsonStr(const char* s) {
    std::string o = "\"";
    for (const char* p = s; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20 || c > 0x7E) o += '?';
        else o += (char)c;
    }
    return o + "\"";
}

}  // namespace

bool SetMuted(std::uint32_t pid, std::uint32_t mode, std::vector<std::string> names) {
    View v(pid);
    if (!v) return false;
    std::vector<std::string> keep;
    for (auto& n : names) {
        std::string s;
        for (char c : n) { if (c == '\r' || c == '\n') continue; s += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
        while (!s.empty() && s.back() == ' ') s.pop_back();
        std::size_t b = 0; while (b < s.size() && s[b] == ' ') ++b;
        s = s.substr(b);
        if (s.empty() || s.size() >= (std::size_t)rtx::chatmute::kNameChars) continue;
        if (std::find(keep.begin(), keep.end(), s) == keep.end()) keep.push_back(s);
        if (keep.size() >= (std::size_t)rtx::chatmute::kMaxNames) break;
    }
    const std::uint32_t s = v.sh->nameSeq + 1;
    v.sh->nameSeq = s;                         // odd: mid-update, the game thread declines to mute
    MemoryBarrier();
    v.sh->nameCount = 0;
    MemoryBarrier();
    for (std::size_t i = 0; i < keep.size(); ++i) {
        std::memset(v.sh->names[i], 0, rtx::chatmute::kNameChars);
        std::memcpy(v.sh->names[i], keep[i].data(), keep[i].size());
    }
    MemoryBarrier();
    v.sh->nameCount = (std::uint32_t)keep.size();
    v.sh->mode = mode;
    v.sh->enable = keep.empty() ? 0u : 1u;
    MemoryBarrier();
    v.sh->nameSeq = s + 1;
    return true;
}

std::string StatusJson(std::uint32_t pid) {
    View v(pid);
    if (!v) return "{}";
    const auto* sh = v.sh;
    std::string out = "{\"ok\":true";
    out += ",\"hooked\":";  out += (sh->flags & rtx::chatmute::kFlagHooked) ? "true" : "false";
    out += ",\"enabled\":"; out += sh->enable ? "true" : "false";
    out += ",\"mode\":" + std::to_string(sh->mode);
    out += ",\"muted\":" + std::to_string(sh->nameCount);
    const std::uint32_t seq = sh->recentSeq;
    const std::uint32_t valid = seq < (std::uint32_t)rtx::chatmute::kMaxRecent ? seq : (std::uint32_t)rtx::chatmute::kMaxRecent;
    out += ",\"recent\":[";
    for (std::uint32_t i = 0; i < valid; ++i) {
        const std::uint32_t abs = seq - valid + i;
        char nm[rtx::chatmute::kNameChars];
        std::memcpy(nm, sh->recent[abs % rtx::chatmute::kMaxRecent], sizeof(nm)); nm[sizeof(nm) - 1] = 0;
        if (i) out += ',';
        out += JsonStr(nm);
    }
    out += "],\"diag\":[";
    for (int i = 0; i < 8; ++i) { if (i) out += ','; out += std::to_string(sh->diag[i]); }
    out += "]}";
    return out;
}

}  // namespace rtx::launcher::chatfilter
