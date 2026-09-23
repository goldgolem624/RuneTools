#pragma once
// UI pixel transport, launcher -> module; BGRA premultiplied, row 0 = top; diag 0 idle/1 no window/2 size/3 unpainted/4 publishing.

#include <cstdint>
#include "ShareName.h"

namespace rtx::frame {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXFrame_v2_";
inline constexpr std::uint32_t kMagic   = 0x52545846;   // 'RTXF'
inline constexpr std::uint32_t kVersion = 7;

inline constexpr std::uint32_t kMaxWidth  = 3840;
inline constexpr std::uint32_t kMaxHeight = 2160;
inline constexpr std::uint32_t kMaxBytes  = kMaxWidth * kMaxHeight * 4;

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
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
    volatile std::uint32_t module_cc;            // module -> launcher: 1 while the game draws the launcher's component rectangles itself

    // module -> launcher: the game's own screen point for each world point asked about, in the same
    // order. `ok` is 0 when the game could not answer (off screen behind the camera, or the
    // operation is not recognised in this build).
    struct AnchorPoint { std::int32_t x, y, depth, ok; std::uint32_t tag; };
    // module -> launcher: how wide each glyph of the label font is, in atlas pixels, so the
    // launcher can size a label exactly as the module will draw it instead of estimating. Cells run
    // from marker::kGlyphFirst. glyph_px is the size the widths were measured at.
    volatile std::uint32_t glyph_ready;
    std::uint32_t glyph_px;
    std::uint8_t  glyph_adv[128];

    volatile std::uint32_t anchor_seq;           // bumped after a set of answers is written
    std::uint32_t anchor_count;
    AnchorPoint   anchor[64];

    // module -> launcher: the game's own answer to each question the launcher asked, in the order
    // it asked them. `ok` is 0 where the game could not answer: the operation is not in this build,
    // the call faulted, or the id names nothing. `answer_ask_seq` says which list these belong to,
    // and `answer_ready` is set once every question in that list has been answered.
    struct AskAnswer { std::int32_t value, ok; std::uint32_t tag; };
    volatile std::uint32_t answer_seq;           // bumped as answers are written
    std::uint32_t answer_ask_seq;
    volatile std::uint32_t answer_ready;
    std::uint32_t answer_count;
    AskAnswer     answer[128];

    std::uint8_t  pixels[kMaxBytes];
};

}  // namespace rtx::frame
