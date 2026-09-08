#pragma once
// Transient render-pass highlights (not in the persistent worldview vector), e.g. scan rings gfx 6841/6842/6843.

#include <cstdint>

namespace rtx::special {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSpecialData_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545848;   // 'RTXH'
inline constexpr std::uint32_t kVersion = 8;
inline constexpr int kMaxHighlights     = 64;
inline constexpr std::uint32_t kStaleMs = 5000;         // highlight TTL (ms)

struct Highlight {
    std::int32_t  gfx;     // graphic id (scan ring 6841 blue / 6842 orange / 6843 red); -1 for type 13
    std::int32_t  x;       // world tile X
    std::int32_t  y;       // world tile Y
    std::int32_t  uid;     // entity uid (dedupe key)
    std::int16_t  plane;   // 0..3
    std::int16_t  type;    // scene entity type: 4 = graphic highlight, 13 = world marker
    std::int16_t  kind;    // kKind*, classified structurally at capture (most type-4s are actor adornments)
    std::int16_t  _pad;
    std::uint32_t stamp;   // GetTickCount64() low32 at capture; launcher evicts after kStaleMs
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
    volatile std::uint32_t enable;   // launcher -> companion; observer is a near-noop while 0
    std::uint32_t flags;          // bit0 = observer installed
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
