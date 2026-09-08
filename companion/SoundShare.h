#pragma once
// Sound contract; ids are js5 archive ids, muting passes the play function volume 0.

#include <cstdint>

namespace rtx::sound {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSound_v1_";
inline constexpr std::uint32_t kMagic   = 0x53545852;   // 'RTXS'
inline constexpr std::uint32_t kVersion = 1;

inline constexpr int kMaxBlocked = 512;    // muted ids
inline constexpr int kMaxRecent  = 64;     // "recently played" ring

struct RecentEntry {
    std::int32_t  id;
    std::int16_t  idx;      // 14 = sound effects, 40 = music
    std::int16_t  muted;    // 1 if this playback was silenced
    std::uint32_t ms;       // GetTickCount64 truncated; relative ordering only, wrap is harmless
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

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

}  // namespace rtx::sound
