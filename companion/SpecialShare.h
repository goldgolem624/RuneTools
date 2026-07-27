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
inline constexpr std::uint32_t kVersion = 6;
inline constexpr int kMaxHighlights     = 64;
inline constexpr std::uint32_t kStaleMs = 5000;         // highlight TTL (ms)

// One transient highlight: gfx id + world tile + plane + capture stamp
// (GetTickCount64 low 32 bits) compared launcher-side for stale eviction.
struct Highlight {
    std::int32_t  gfx;     // graphic id (scan ring 6841 blue / 6842 orange / 6843 red)
    std::int32_t  x;       // world tile X
    std::int32_t  y;       // world tile Y
    std::int32_t  uid;     // entity uid (dedupe key)
    std::int16_t  plane;   // 0..3
    std::int16_t  _pad;
    std::uint32_t stamp;   // GetTickCount64() low32 at capture
};

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
