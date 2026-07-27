#pragma once
//
// HUD-reminder transport: the launcher decodes a sprite to RGBA + a caption and publishes
// them here; the in-client module draws a top-center sprite + caption over the game frame.
// The launcher CREATES + writes this section; the module OPENS it read-only. launcher -> module.
//
#include <cstdint>

namespace rtx::hud {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXHud_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545848;   // 'RTXH'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxW = 128, kMaxH = 128;          // sprite cap (px)
inline constexpr int kCaptionMax = 95;

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
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
