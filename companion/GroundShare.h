#pragma once
//
// Launcher <-> companion contract for GROUND ITEMS (dropped stacks on a tile,
// scene entity TYPE 3; separate section from rtx::scene locs). Companion publishes
// item id + tile; launcher resolves names from the item cache. Plain C-layout POD.

#include <cstdint>

namespace rtx::ground {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXGroundItems_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545847;   // 'RTXG'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxItems = 512;

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

// One ground-item stack. Names resolve launcher-side from the item cache by `id`.
struct Item {
    std::int32_t id;      // item config id
    std::int32_t x;       // world tile X
    std::int32_t y;       // world tile Y
    std::int32_t plane;   // 0..3
};

// Header + inline item array. `seq` goes odd before a write, even after; reader
// retries while odd or if it changed across the copy.
struct Share {
    std::uint32_t magic;      // kMagic once initialised
    std::uint32_t version;    // kVersion
    volatile std::uint32_t seq;   // write seqlock (odd = mid-update)
    std::uint32_t count;      // valid entries in items[0..count)
    std::uint32_t pid;        // target client pid (sanity)
    std::uint32_t flags;      // reserved
    Item          items[kMaxItems];
};

}  // namespace rtx::ground
