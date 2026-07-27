#pragma once
//
// World-marker transport: launcher projects the live scene to screen-space
// primitives and publishes a compact command list; the in-client module draws
// them into the game frame. Launcher -> module. Plain C-layout POD; coords are
// client pixels (origin top-left). Labels reference a module-built glyph atlas.

#include <cstdint>

namespace rtx::marker {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXMarker_v1_";
inline constexpr std::uint32_t kMagic   = 0x5254584D;   // 'RTXM'
inline constexpr std::uint32_t kVersion = 5;
inline constexpr std::uint32_t kMaxCmds = 8192;
inline constexpr int kTextMax = 95;                     // kText inline string capacity (chars, excl. NUL; '\n' = panel line break)

// Glyph atlas layout (shared by both sides): ASCII first..last, one cell each,
// kGlyphCellW x kGlyphCellH px in a kGlyphCols-wide grid. A label at pixel height H
// advances H * (kGlyphCellW / kGlyphCellH) per character (fixed pitch).
inline constexpr std::uint16_t kGlyphFirst = 32;    // space
inline constexpr std::uint16_t kGlyphLast  = 126;   // '~'
inline constexpr int kGlyphCols  = 16;
inline constexpr int kGlyphCellW = 24;
inline constexpr int kGlyphCellH = 34;

enum Type : std::uint16_t {
    kLine      = 1,  // (x0,y0)->(x1,y1), thickness px
    kRect      = 2,  // outline of [x0,y0]-(x1,y1], thickness px
    kFillRect  = 3,  // filled [x0,y0]-(x1,y1]
    kGlyph     = 4,  // atlas cell `glyph` drawn with top-left at (x0,y0), size (x1,y1)
    kFillQuad  = 5,  // filled arbitrary quad (x0,y0)(x1,y1)(x2,y2)(x3,y3), perimeter order
    kText      = 6,  // proportional label `text`; x1 = glyph px height. glyph = flags:
                     //   0 -> legacy pill centred at (x0,y0), rgba = accent (border).
                     //   bit0 -> PLAIN single-line text, no pill; rgba = text colour;
                     //   y0 = the line's vertical centre; align = (glyph>>1)&3:
                     //   0 x0=left edge, 1 x0=centre, 2 x0=right edge.
    kRoundFill = 7,  // filled rounded rect: (x0,y0) top-left, (x1,y1) = w,h,
                     // thickness = corner radius (>= half min extent = capsule/circle)
};

inline constexpr std::uint16_t kTextPlain = 1;              // kText glyph-field flags
inline constexpr std::uint16_t kTextAlignCentre = 1 << 1;
inline constexpr std::uint16_t kTextAlignRight  = 2 << 1;

struct Command {
    std::uint16_t type;        // Type
    std::uint16_t glyph;       // glyph atlas cell index (kGlyph); flags (kText)
    float x0, y0, x1, y1;      // client-pixel coords (meaning per type)
    float x2, y2, x3, y3;      // extra quad corners (kFillQuad only)
    float thickness;           // line/outline width in px; corner radius (kRoundFill)
    std::uint8_t r, g, b, a;   // straight (non-premultiplied) RGBA
    char  text[kTextMax + 1];  // kText only: NUL-terminated ASCII label (else unused)
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

struct Share {
    std::uint32_t magic;       // kMagic once the launcher has initialised it
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)
    volatile std::uint32_t seq;    // write seqlock (odd = mid-update)

    std::uint32_t count;       // valid commands in cmds[0..count)
    std::int32_t  fb_w, fb_h;  // client size the coords were projected for
    std::int32_t  gv_x, gv_y, gv_w, gv_h;   // game-view sub-rect (clip region)
    volatile std::uint32_t visible;         // 0 = module skips drawing markers
    std::uint32_t flags;       // reserved

    Command cmds[kMaxCmds];
};

}  // namespace rtx::marker
