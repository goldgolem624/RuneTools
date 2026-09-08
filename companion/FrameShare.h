#pragma once
// UI pixel transport, launcher -> module; BGRA premultiplied, row 0 = top; diag 0 idle/1 no window/2 size/3 unpainted/4 publishing.

#include <cstdint>

namespace rtx::frame {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXFrame_v2_";
inline constexpr std::uint32_t kMagic   = 0x52545846;   // 'RTXF'
inline constexpr std::uint32_t kVersion = 2;

inline constexpr std::uint32_t kMaxWidth  = 3840;
inline constexpr std::uint32_t kMaxHeight = 2160;
inline constexpr std::uint32_t kMaxBytes  = kMaxWidth * kMaxHeight * 4;

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
    std::uint32_t magic;       // kMagic once initialised
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)
    volatile std::uint32_t seq;    // write seqlock (odd = mid-update)

    std::uint32_t width;       // live pixel width  (<= kMaxWidth)
    std::uint32_t height;      // live pixel height (<= kMaxHeight)
    std::uint32_t stride;      // bytes per row (= width * 4)
    std::uint32_t frame_id;    // bumped whenever pixel content changes (skip upload if equal)

    std::int32_t  dirty_x, dirty_y, dirty_w, dirty_h;   // changed sub-rect since last publish

    std::int32_t  origin_x, origin_y;   // layer top-left in client pixels (currently 0,0)

    volatile std::uint32_t visible;   // 0 = module skips this layer entirely (gate)
    volatile std::uint32_t cursor;    // desired cursor id while over UI windows (see InputShare)
    std::uint32_t flags;              // reserved

    volatile std::uint32_t diag;

    volatile std::int32_t  client_w, client_h;   // module -> launcher: GetClientRect size, every present
    volatile std::uint32_t module_seq;           // module -> launcher: bumped every present (liveness)

    std::uint8_t  pixels[kMaxBytes];
};

}  // namespace rtx::frame
