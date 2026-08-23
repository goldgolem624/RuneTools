#pragma once
//
// Launcher <-> companion contract for TRANSIENT render-pass highlight graphics
// (submitted per frame, absent from the persistent worldview vector; e.g. the
// clue-scan proximity ring gfx 6841 blue / 6842 orange / 6843 red). The companion
// records type-4 highlights and publishes them here. Plain C-layout POD.

#include <cstdint>

namespace rtx::special {

// Named shared section, per client pid; version in the name isolates layout changes.
inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSpecialData_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545848;   // 'RTXH'
inline constexpr std::uint32_t kVersion = 8;   // 8: added `kind`; 7: _pad repurposed as `type`
inline constexpr int kMaxHighlights     = 64;
inline constexpr std::uint32_t kStaleMs = 5000;         // highlight TTL (ms)

// One transient highlight: gfx id + world tile + plane + capture stamp
// (GetTickCount64 low 32 bits) compared launcher-side for stale eviction.
struct Highlight {
    std::int32_t  gfx;     // graphic id (scan ring 6841 blue / 6842 orange / 6843 red); -1 for type 13
    std::int32_t  x;       // world tile X
    std::int32_t  y;       // world tile Y
    std::int32_t  uid;     // entity uid (dedupe key)
    std::int16_t  plane;   // 0..3
    // Scene entity type this came from: 4 = graphic highlight (Time Sprite, Rockertunity,
    // the scan rings), 13 = world marker (walk destination, scan dig marker). Was a pad
    // byte pair, so the struct size is unchanged; the version bump is for the meaning.
    std::int16_t  type;
    // What this actually IS, decided at capture from structure rather than from a gfx-id
    // blocklist. See kKind* below. The hook sees every rendered type-4, and most of those are
    // per-actor adornments (health bars, hitsplats) rather than world effects -- `kind` is what
    // lets the panel tell those apart instead of showing them all as one anonymous "Special".
    std::int16_t  kind;
    std::int16_t  _pad;
    std::uint32_t stamp;   // GetTickCount64() low32 at capture
};

inline constexpr std::int16_t kKindUnknown  = 0;
inline constexpr std::int16_t kKindScan     = 1;   // type 13, stores its own tile as its destination
inline constexpr std::int16_t kKindDest     = 2;   // type 13, walk destination (no self-destination)
inline constexpr std::int16_t kKindAdorn    = 3;   // type 4 hanging off a player/NPC node
inline constexpr std::int16_t kKindEffect   = 4;   // type 4 standalone world effect

struct Share {
    std::uint32_t magic;          // kMagic once initialised
    std::uint32_t version;        // kVersion
    volatile std::uint32_t seq;   // write seqlock (odd = mid-update)
    std::uint32_t count;          // valid items[0..count)
    std::uint32_t pid;            // target client pid (sanity)
    // launcher -> companion: 1 while the scene/specials reader is polling; the
    // render-thread observer is a near-noop while 0.
    volatile std::uint32_t enable;
    std::uint32_t flags;          // bit0 = observer installed
    // Diagnostics (companion-written, best-effort): [0]=observer fires while armed,
    // [1]=type-4 highlights seen, [2]=last type-4 gfx id, [3]=observer installed,
    // [4]=bitmask of entity types seen, [5]=resolved fn RVA, [6]=entity ptrs in the
    // tracked set, [7]=distinct scene workers walked, [8]=seconds the spawn capture has
    // been installed, [9]=root-resolution method (0 none / 1 byte anchor / 2 structural
    // scan), [10]=containers discovered, [11]=position source (0 none / 1 live / 2 remembered).
    std::uint32_t diag[12];
    Highlight     items[kMaxHighlights];
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

}  // namespace rtx::special
