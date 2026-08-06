#pragma once
//
// Launcher <-> companion contract for in-game sound: which sounds the client plays, and which
// ones to silence.
//
// The companion hooks the engine's shared sound-play function, so the ids here are js5 archive
// ids - exactly the ids the Sounds panel lists and can play back - and muting is done by
// passing that function a volume of 0. Nothing is written into the client's memory.

#include <cstdint>

namespace rtx::sound {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSound_v1_";
inline constexpr std::uint32_t kMagic   = 0x53545852;   // 'RTXS'
inline constexpr std::uint32_t kVersion = 1;

inline constexpr int kMaxBlocked = 512;    // muted ids; far beyond any plausible mute list
inline constexpr int kMaxRecent  = 64;     // "recently played" ring shown in the panel

// One observed playback start, as the engine's own play function saw it. `idx` is the js5
// index the id belongs to (14 effects / 40 music), decoded from the engine's group argument, so
// an effect id and a music id that share a number never collide.
struct RecentEntry {
    std::int32_t  id;
    std::int16_t  idx;      // 14 = sound effects, 40 = music
    std::int16_t  muted;    // 1 if this playback was silenced
    std::uint32_t ms;       // GetTickCount64 truncated; relative ordering only, wrap is harmless
};

// Mute-list key. Ids are well under 2^24 in both indexes, so the index rides in the top byte
// and the list stays a single sorted array the audio path can binary-search.
inline constexpr std::int32_t kIndexEffects = 14;
inline constexpr std::int32_t kIndexMusic   = 40;
inline constexpr std::int32_t MakeKey(std::int32_t idx, std::int32_t id) {
    return (idx << 24) | (id & 0x00FFFFFF);
}

// flags (companion -> launcher)
inline constexpr std::uint32_t kFlagHooked = 1u << 0;   // play-function hook attached
// (kFlagIdUnverified is retired: the id now comes from the engine's play function as a js5
// archive id, the same id the Sounds panel lists. It is no longer an unverified handle.)

struct Share {
    std::uint32_t magic;                    // kMagic once initialised
    std::uint32_t version;                  // kVersion
    std::uint32_t pid;                      // target client pid (sanity)
    std::uint32_t flags;                    // companion -> launcher, kFlag* above

    // Launcher -> companion. 0 makes the detour a near-noop (one load and a branch) so nothing
    // is observed or published while the Sounds panel is closed.
    volatile std::uint32_t enable;

    // RVA of the play function the companion located, 0 if it could not be found. Purely
    // diagnostic - it is what to check first if a game update moves the pattern.
    std::uint32_t playRva;

    // Launcher -> companion: the mute list, sorted ascending so the detour can binary-search
    // it. Written under `blockSeq` (odd = mid-update) because the audio thread reads it
    // concurrently and must never see a half-written list.
    volatile std::uint32_t blockSeq;
    volatile std::uint32_t blockCount;
    std::int32_t           blocked[kMaxBlocked];

    // Companion -> launcher: ring of observed playbacks. `recentSeq` counts every observation
    // ever made (never reset), so slot = (recentSeq - 1) % kMaxRecent is the newest and the
    // launcher can tell how many it missed between polls.
    volatile std::uint32_t recentSeq;
    RecentEntry            recent[kMaxRecent];

    // Diagnostics (companion-written, racy/best-effort): [0] playbacks seen, [1] playbacks
    // silenced, [2] ring entries published.
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
