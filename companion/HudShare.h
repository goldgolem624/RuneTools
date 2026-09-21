#pragma once
// HUD-reminder transport, launcher -> module: sprite RGBA + caption drawn top-centre.
#include <cstdint>
#include "ShareName.h"

namespace rtx::hud {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXHud_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545848;   // 'RTXH'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxW = 128, kMaxH = 128;          // sprite cap (px)
inline constexpr int kCaptionMax = 95;

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

struct Share {
    std::uint32_t magic;            // kMagic once initialised
    std::uint32_t version;          // kVersion
    std::uint32_t pid;              // target client pid
    volatile std::uint32_t seq;     // seqlock (odd = mid-write)
    std::uint32_t enable;           // 0 = hidden
    std::uint32_t imgSeq;           // bumps when rgba/w/h change -> module re-uploads its texture
    std::int32_t  w, h;             // sprite px (<= kMaxW/H)
    char caption[kCaptionMax + 1];
    std::uint8_t rgba[kMaxW * kMaxH * 4];   // straight (non-premultiplied) RGBA, row-major, top-left
};

}  // namespace rtx::hud
