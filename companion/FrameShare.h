#pragma once
//
// Sidebar pixel transport: launcher renders the panel off-screen (CPU-backed
// Ultralight surface) and publishes pixels here; the in-client module composites
// them into the game frame. Launcher -> module. Plain C-layout POD; live region
// is width/height/stride, changed sub-rect is dirty_* (copied under the seqlock).

#include <cstdint>

namespace rtx::frame {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXFrame_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545846;   // 'RTXF'
inline constexpr std::uint32_t kVersion = 1;

// Sidebar cap: ~512 px wide by a 4K-tall client (512 * 2160 * 4 = ~4.4 MB per client).
inline constexpr std::uint32_t kMaxWidth  = 512;
inline constexpr std::uint32_t kMaxHeight = 2160;
inline constexpr std::uint32_t kMaxBytes  = kMaxWidth * kMaxHeight * 4;

// Build "Local\RuneToolsXFrame_v1_<pid>" into `out` (size >= 64).
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
    std::uint32_t magic;       // kMagic once the launcher has initialised it
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)
    volatile std::uint32_t seq;    // write seqlock (odd = mid-update)

    std::uint32_t width;       // live pixel width  (<= kMaxWidth)
    std::uint32_t height;      // live pixel height (<= kMaxHeight)
    std::uint32_t stride;      // bytes per row (= width * 4)
    std::uint32_t frame_id;    // bumped whenever pixel content changes (skip upload if equal)

    // Changed sub-rect since the last publish (module uploads only this region).
    std::int32_t  dirty_x, dirty_y, dirty_w, dirty_h;

    // Where to place the panel in client pixels (top-left). The sidebar is
    // right-anchored, so the launcher sets origin_x = clientWidth - width.
    std::int32_t  origin_x, origin_y;

    volatile std::uint32_t visible;   // 0 = module skips this layer entirely (gate)
    volatile std::uint32_t cursor;    // desired cursor id while over the panel (see InputShare)
    std::uint32_t flags;              // reserved
    // Publisher progress code: 0 = never ran, 1 = no game window, 2 = surface/size
    // issue, 3 = surface not painted yet, 4 = painting & publishing.
    volatile std::uint32_t diag;

    // BGRA, premultiplied alpha (Ultralight surface format). Row 0 = top.
    std::uint8_t  pixels[kMaxBytes];
};

}  // namespace rtx::frame
