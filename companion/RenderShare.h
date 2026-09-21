// Render-control shared section, launcher -> module. All toggles default to 0 (off).
#pragma once
#include <cstdint>
#include "ShareName.h"

namespace rtx::render {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXRender_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545852;   // 'RTXR'
inline constexpr std::uint32_t kVersion = 1;

inline constexpr std::uint32_t kMsgGameClicked = 0x8000 + 0x52;   // WM_APP + 'R'

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

struct Share {
    std::uint32_t magic;        // kMagic once initialised
    std::uint32_t version;      // kVersion
    std::uint32_t pid;          // target client pid (sanity)
    volatile std::uint32_t hideNpcs;
    volatile std::uint32_t hidePlayers;
    volatile std::uint32_t hideAll;     // blank the whole scene
    volatile std::uint32_t keepFocused; // suppress focus-loss throttling
    volatile std::uint32_t installed;   // module -> launcher: bit0 npc-skip, bit1 player-skip, bit2 scene-blank
    volatile std::uint32_t embedded;
    volatile std::uint64_t inputWindow;
};

}  // namespace rtx::render
