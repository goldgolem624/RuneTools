#pragma once
//
// UI-layer pixel transport: the launcher renders the in-game window UI
// off-screen (CPU-backed transparent Ultralight surface sized to the game's
// client area) and publishes pixels here; the in-client module composites them
// into the game frame, above the marker layer. Launcher -> module, except the
// client_w/client_h/module_seq feedback fields (module -> launcher), which form
// the resize handshake. Plain C-layout POD; live region is width/height/stride,
// changed sub-rect is dirty_* (copied under the seqlock).
//
// v2: full-client-size layer (was a 512px right-anchored sidebar), live client
// size feedback, module liveness counter.

#include <cstdint>

namespace rtx::frame {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXFrame_v2_";
inline constexpr std::uint32_t kMagic   = 0x52545846;   // 'RTXF'
inline constexpr std::uint32_t kVersion = 2;

// Layer cap: a 4K client (3840 * 2160 * 4 = ~33 MB per client). The section is
// pagefile-backed, so pages are committed only as they are first written; a
// 1080p client touches ~8 MB of it.
inline constexpr std::uint32_t kMaxWidth  = 3840;
inline constexpr std::uint32_t kMaxHeight = 2160;
inline constexpr std::uint32_t kMaxBytes  = kMaxWidth * kMaxHeight * 4;

// Build "Local\RuneToolsXFrame_v2_<pid>" into `out` (size >= 64).
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

    // Where to place the layer in client pixels (top-left). The full-window UI
    // layer publishes (0,0); kept for generality.
    std::int32_t  origin_x, origin_y;

    volatile std::uint32_t visible;   // 0 = module skips this layer entirely (gate)
    volatile std::uint32_t cursor;    // desired cursor id while over UI windows (see InputShare)
    std::uint32_t flags;              // reserved

    // Publisher progress code: 0 = never ran, 1 = no game window, 2 = surface/size
    // issue, 3 = surface not painted yet, 4 = painting & publishing.
    volatile std::uint32_t diag;

    // module -> launcher: live GetClientRect size, written every present. The
    // launcher resizes its off-screen view to match (the v1 protocol had no
    // module->launcher resize signal).
    volatile std::int32_t  client_w, client_h;
    // module -> launcher: bumped every present while the module maps this
    // section; lets the launcher detect a live v2 companion.
    volatile std::uint32_t module_seq;

    // BGRA, premultiplied alpha (Ultralight surface format). Row 0 = top.
    std::uint8_t  pixels[kMaxBytes];
};

}  // namespace rtx::frame
