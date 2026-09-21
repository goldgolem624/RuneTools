#pragma once
// Launcher <-> companion contract for ground items (scene entity type 3).

#include <cstdint>
#include "ShareName.h"

namespace rtx::ground {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXGroundItems_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545847;   // 'RTXG'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxItems = 512;

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

struct Item {
    std::int32_t id;      // item config id
    std::int32_t x;       // world tile X
    std::int32_t y;       // world tile Y
    std::int32_t plane;   // 0..3
};

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
