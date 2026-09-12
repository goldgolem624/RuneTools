#include "SoundFilter.h"
#include "../../companion/SoundShare.h"   // per-pid section name + layout

#include <Windows.h>

#include <algorithm>
#include <string>

namespace rtx::launcher::soundfilter {
namespace {

struct View {
    HANDLE              map = nullptr;
    rtx::sound::Share*  sh  = nullptr;

    explicit View(std::uint32_t pid) {
        if (!pid) return;
        wchar_t name[64];
        rtx::sound::MakeSectionName(pid, name);
        map = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!map) return;
        sh = (rtx::sound::Share*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                                               sizeof(rtx::sound::Share));
        // Uninitialised or wrong-version section: treat as absent.
        if (sh && (sh->magic != rtx::sound::kMagic || sh->version != rtx::sound::kVersion)) {
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

}  // namespace

bool SetEnabled(std::uint32_t pid, bool on) {
    View v(pid);
    if (!v) return false;
    v.sh->enable = on ? 1u : 0u;
    return true;
}

bool SetMuted(std::uint32_t pid, std::vector<int> ids) {
    View v(pid);
    if (!v) return false;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.size() > (std::size_t)rtx::sound::kMaxBlocked) ids.resize(rtx::sound::kMaxBlocked);

    // Seqlock: the audio thread reads lock-free; count is published after the ids.
    const std::uint32_t s = v.sh->blockSeq + 1;
    v.sh->blockSeq = s;                        // odd -> mid-update, reader declines to mute
    MemoryBarrier();
    v.sh->blockCount = 0;                      // no partial list is ever searchable
    MemoryBarrier();
    for (std::size_t i = 0; i < ids.size(); ++i) v.sh->blocked[i] = ids[i];
    MemoryBarrier();
    v.sh->blockCount = (std::uint32_t)ids.size();
    MemoryBarrier();
    v.sh->blockSeq = s + 1;                    // even -> readable again
    return true;
}

std::string StatusJson(std::uint32_t pid) {
    View v(pid);
    if (!v) return "{}";
    const auto* sh = v.sh;
    const std::uint32_t seq   = sh->recentSeq;
    const std::uint32_t valid = seq < (std::uint32_t)rtx::sound::kMaxRecent
                                    ? seq : (std::uint32_t)rtx::sound::kMaxRecent;

    std::string out = "{\"ok\":true";
    out += ",\"hooked\":";   out += (sh->flags & rtx::sound::kFlagHooked) ? "true" : "false";
    out += ",\"enabled\":";  out += sh->enable ? "true" : "false";
    out += ",\"playRva\":" + std::to_string(sh->playRva);
    out += ",\"muted\":" + std::to_string(sh->blockCount);
    out += ",\"seq\":"   + std::to_string(seq);
    out += ",\"recent\":[";
    for (std::uint32_t i = 0; i < valid; ++i) {
        const std::uint32_t abs = seq - valid + i;          // oldest-first
        const auto& e = sh->recent[abs % rtx::sound::kMaxRecent];
        if (i) out += ',';
        // Origin from the return address (950-1 call sites, each +5 = the instruction after the call):
        // 0x949A4/0x94ACF/0x9513F script ops, 0xF0620 server sound (0x2C), 0xF09E0 server world-tile
        // sound (0x5F), 0x115250/0x115510 zone sounds (0xA4 and the zone-update sub-packets),
        // 0x3E7040 actor animation slots, 0x3E8970 engine. Unknown call sites are reported by RVA.
        const char* origin = "other";
        switch (e.caller) {
        case 0x949A4 + 5: case 0x94ACF + 5: case 0x9513F + 5: origin = "script"; break;
        case 0xF0620 + 5: origin = "server"; break;
        case 0xF09E0 + 5: origin = "server_tile"; break;
        case 0x115250 + 5: case 0x115510 + 5: origin = "zone"; break;
        case 0x3E7040 + 5: origin = "actor"; break;
        case 0x3E8970 + 5: origin = "engine"; break;
        default: break;
        }
        out += "{\"n\":" + std::to_string(abs) +
               ",\"id\":" + std::to_string(e.id) +
               ",\"idx\":" + std::to_string(e.idx) +
               ",\"muted\":" + std::to_string(e.muted) +
               ",\"ms\":" + std::to_string(e.ms) +
               ",\"origin\":\"" + origin + "\",\"caller\":" + std::to_string(e.caller) +
               ",\"x\":" + std::to_string(e.x) + ",\"y\":" + std::to_string(e.y) +
               ",\"group\":" + std::to_string(e.group) + ",\"kind\":" + std::to_string(e.kind) + '}';
    }
    out += "],\"diag\":[";
    for (int i = 0; i < 8; ++i) { if (i) out += ','; out += std::to_string(sh->diag[i]); }
    out += "]}";
    return out;
}

}  // namespace rtx::launcher::soundfilter
