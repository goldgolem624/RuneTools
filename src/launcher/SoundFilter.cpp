#include "SoundFilter.h"
#include "../../companion/SoundShare.h"   // per-pid section name + layout

#include <Windows.h>

#include <algorithm>
#include <string>

namespace rtx::launcher::soundfilter {
namespace {

// Open + map the pid's section for the duration of one call. Polls run at panel tick rate, so
// the open/close cost is irrelevant and it avoids holding a view across a client's exit.
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
        // A section that exists but hasn't been initialised (or is a version we don't speak)
        // must not be written through: treat it as absent.
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

    // Seqlock: the audio thread reads this list without locking, so bracket the write with an
    // odd sequence and publish the count only once the ids are in place.
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
        out += "{\"n\":" + std::to_string(abs) +
               ",\"id\":" + std::to_string(e.id) +
               ",\"idx\":" + std::to_string(e.idx) +
               ",\"muted\":" + std::to_string(e.muted) +
               ",\"ms\":" + std::to_string(e.ms) + '}';
    }
    out += "],\"diag\":[";
    for (int i = 0; i < 8; ++i) { if (i) out += ','; out += std::to_string(sh->diag[i]); }
    out += "]}";
    return out;
}

}  // namespace rtx::launcher::soundfilter
