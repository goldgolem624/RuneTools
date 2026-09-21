#pragma once

#include <cstdint>
#include "ShareName.h"

namespace rtx::sound {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSound_v2_";
inline constexpr std::uint32_t kMagic   = 0x53545852;   // 'RTXS'
inline constexpr std::uint32_t kVersion = 2;   // 2: recent entries carry the caller, tile, group and kind

inline constexpr int kMaxBlocked = 512;    // muted ids
inline constexpr int kMaxRecent  = 64;     // "recently played" ring

struct RecentEntry {
    std::int32_t  id;
    std::int16_t  idx;      // 14 = sound effects, 40 = music
    std::int16_t  muted;    // 1 if this playback was silenced
    std::uint32_t ms;       // GetTickCount64 truncated; relative ordering only, wrap is harmless
    std::uint32_t caller;   // return address of the play call, module-relative: which subsystem asked for the sound
    std::int16_t  x, y;     // world tile of a positioned sound (mode != 4), else -1
    std::uint8_t  group;    // volume category the caller passed (6 effects, 7 area, 8 music)
    std::uint8_t  kind;     // low byte of the kind word (7 script synth, 8 server, 9 world-tile sound)
    std::uint16_t pad;
};

inline constexpr std::int32_t kIndexEffects = 14;
inline constexpr std::int32_t kIndexMusic   = 40;
inline constexpr std::int32_t MakeKey(std::int32_t idx, std::int32_t id) {
    return (idx << 24) | (id & 0x00FFFFFF);
}

inline constexpr std::uint32_t kFlagHooked = 1u << 0;   // play-function hook attached

struct Share {
    std::uint32_t magic;                    // kMagic once initialised
    std::uint32_t version;                  // kVersion
    std::uint32_t pid;                      // target client pid (sanity)
    std::uint32_t flags;                    // companion -> launcher, kFlag* above

    volatile std::uint32_t enable;          // launcher -> companion; 0 = detour is a pass-through

    std::uint32_t playRva;                  // resolved play-function RVA, 0 if not found (diag)

    // Mute list sorted ascending, written under blockSeq (odd = mid-update); audio thread binary-searches it.
    volatile std::uint32_t blockSeq;
    volatile std::uint32_t blockCount;
    std::int32_t           blocked[kMaxBlocked];

    volatile std::uint32_t recentSeq;
    RecentEntry            recent[kMaxRecent];

    std::uint32_t diag[8];

};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::sound
