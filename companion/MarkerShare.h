#pragma once
// World-marker draw list, launcher -> module; client pixels, origin top-left.

#include <cstdint>
#include "ShareName.h"

namespace rtx::marker {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXMarker_v1_";
inline constexpr std::uint32_t kMagic   = 0x5254584D;   // 'RTXM'
inline constexpr std::uint32_t kVersion = 29;
inline constexpr std::uint32_t kMaxCmds = 8192;
inline constexpr int kTextMax = 95;                     // kText inline string capacity (chars, excl. NUL; '\n' = panel line break)

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
    kText      = 6,  // label `text`, x1 = glyph px height; glyph = flags: 0 pill centred at (x0,y0),
                     //   bit0 plain text (rgba = colour, y0 = line centre), align (glyph>>1)&3: 0 left 1 centre 2 right
    kRoundFill = 7,  // filled rounded rect: (x0,y0) top-left, (x1,y1) = w,h, thickness = corner radius
};

inline constexpr std::uint32_t kFlagDepth = 1, kFlagDepthReversed = 2;
inline constexpr std::uint32_t kFlagInFrameTrial = 16;  // trial: a square drawn into the game's own frame, under its interface (Vulkan)
inline constexpr std::uint32_t kFlagOccludeHide = 8;   // with kFlagDepth: what the scene is in front of is not drawn at all, instead of faintly
inline constexpr std::uint32_t kFlagEngineHover = 4;   // have the game outline whatever is hovered, see EngineHighlight.h
inline constexpr std::uint16_t kTextPlain = 1;              // kText glyph-field flags
inline constexpr std::uint16_t kTextAlignCentre = 1 << 1;
inline constexpr std::uint16_t kTextAlignRight  = 2 << 1;

// text[0] != 0: a text component instead of a rectangle; font = the game's font id (26 = its 12px text)
struct CcRect { std::int32_t parent, slot, x, y, w, h; std::uint32_t argb; std::int32_t font; char text[48]; };
inline constexpr int kMaxCc = 96;

// World points the launcher wants the game's own answer for. The module projects them in the frame
// hook and writes the screen points back through the frame share, so the launcher can hold them
// against its own projection and then use them.
inline constexpr int kMaxAnchors = 64;
struct Anchor {
    std::int32_t plane;
    float        x, height, y;   // the game's fine units, 512 to a tile
    std::int32_t lift;           // extra height above the point, in the same units
    std::int32_t on_ground;      // 1 = take the height from the terrain instead
    // A character the game knows by its own index: the answer then comes from the game's own
    // position for it, lifted to the height the game puts its own overheads at. 0 = use the point
    // above. The point is still filled in, both as the fallback and as the check that the index
    // found the character we meant.
    std::int32_t entity;
    // What this request is called. The answer comes back carrying it, so an answer is matched to the
    // request it belongs to rather than to whatever now sits at the same place in the list.
    std::uint32_t tag;
};
// Questions about the account the game answers for itself: whether an achievement's requirements
// are met, whether a quest is started or finished. The game weighs these against the live stats,
// the account state and today's date, so the answer is the one the game would give rather than one
// we work out from tables of our own that go stale with every update.
inline constexpr int kMaxAsks = 128;
enum AskKind : std::uint16_t {
    kAskAchievementState = 0,   // how far along the requirement is, as the game grades it
    kAskAchievementPrereqs = 1, // every achievement it depends on is done
    kAskQuestFinished = 2,
    kAskQuestStarted = 3,
};
struct Ask {
    std::uint16_t kind;         // AskKind
    std::uint16_t spare;
    std::int32_t  id;           // achievement or quest id
    std::uint32_t tag;          // echoed with the answer, so an answer is never read against the wrong question
};

inline constexpr int kCcSlotBase = 0xE00;   // dynamic ids from here are ours; the game's scripts stay far below

struct Command {
    std::uint16_t type;        // Type
    std::uint16_t glyph;       // glyph atlas cell index (kGlyph); flags (kText)
    float x0, y0, x1, y1;      // client-pixel coords (meaning per type)
    float x2, y2, x3, y3;      // extra quad corners (kFillQuad only)
    float thickness;           // line/outline width in px; corner radius (kRoundFill)
    float z0, z1, z2, z3;      // clip-space depth per point for occlusion; < 0 = never occluded
    std::uint8_t r, g, b, a;   // straight (non-premultiplied) RGBA
    std::uint8_t top;          // 1 = belongs over the game's interface (it marks a part of it); 0 = part of the world, under it
    char  text[kTextMax + 1];  // kText only: NUL-terminated ASCII label (else unused)
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

struct Share {
    std::uint32_t magic;       // kMagic once initialised
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)
    volatile std::uint32_t seq;    // write seqlock (odd = mid-update)

    std::uint32_t count;       // valid commands in cmds[0..count)
    std::int32_t  fb_w, fb_h;  // client size the coords were projected for
    std::int32_t  gv_x, gv_y, gv_w, gv_h;   // game-view sub-rect (clip region)
    volatile std::uint32_t visible;         // 0 = module skips drawing markers
    std::uint32_t flags;       // kFlagDepth: occlude by the scene depth; kFlagDepthReversed: nearer = larger z
    float ref_x, ref_y, ref_z; // player's projected point, for depth calibration
    float ref_a, ref_b;        // projection constants: depth = -a + b / view distance (b = 0: unknown)
    float ref2_x, ref2_y, ref2_z;   // open-ground point three tiles from the player, second calibration sample
    // The view matrix every point was projected with, and where the game keeps it. The list is a
    // frame or more old by the time it is drawn; the module reads the matrix again at that moment
    // and moves each point to where the camera now puts it. view_addr = 0: not known.
    float         view_m[16];
    std::uint64_t view_addr;
    // Scenery under the cursor that the game should outline this frame: the tile and definition
    // the scene share lists it under. hover_on = 0 when there is none, or it offers nothing to do.
    volatile std::uint32_t hover_on;
    std::int32_t  hover_x, hover_y, hover_id;
    // Text for the game's own tooltip while the cursor is on one interface slot, see TooltipHook.h.
    volatile std::uint32_t tip_on;
    std::int32_t  tip_slot;       // interface slot of an item; tile x of scenery
    std::uint32_t tip_comp;       // interface id << 16 | component; tile y of scenery
    char          tip_text[192];
    std::uint32_t hover_width[8]; // outline width per highlight category; the game uses 4, 0 = leave it
    std::uint32_t hover_rgb[8];   // per highlight category, 0xRRGGBB for the game's outline while kFlagEngineHover is on; 0 = the game's own
    // World markers the game draws itself, see EngineMarkers.h: one marked tile and an arrow over a tile.
    volatile std::uint32_t mark_tile_on;
    std::int32_t  mark_tile_x, mark_tile_y;
    std::uint32_t mark_tile_model;
    volatile std::uint32_t mark_arrow_on;
    std::int32_t  mark_arrow_x, mark_arrow_y, mark_arrow_plane;
    std::uint32_t mark_arrow_style, mark_arrow_height;
    std::int32_t  mark_arrow_pointer;   // the arrow at the player's feet; -1 = none
    std::uint32_t mark_tile_rgb, mark_tile_width;     // the game's outline around each: 0xRRGGBB (0 = none) and width
    std::uint32_t mark_arrow_rgb, mark_arrow_width;   // these two are for the arrow at the player's feet
    std::uint32_t mark_arrow_range;     // tiles from the marked tile within which the arrows show
    std::uint32_t mark_pointer_scale;   // percent
    std::uint32_t mark_pointer_reach;   // how far from the player it sits, 512 = one tile
    std::int32_t  mark_arrow_npc;       // the NPC the arrow sits over instead of the tile, by its index in the game; -1 = the tile
    std::uint32_t mark_tile_steady;     // 0 = leave the game's slow rise and fall on the tile model
    // The way there: the game lays `mark_path_model` on every tile between the two.
    volatile std::uint32_t mark_path_on;
    std::int32_t  mark_path_x0, mark_path_y0, mark_path_x1, mark_path_y1;
    std::uint32_t mark_path_model;

    // Rectangles the game draws itself as components of one of its interfaces, see EngineComponents.h.
    // parent = interface << 16 | component; slot = the dynamic id, ours from kCcSlotBase up; the
    // rectangle in the parent's own coordinates; argb with alpha 255 = opaque.
    // One-shot requests the module carries out through the client's own operations: a sound by id
    // (played through the game's mixer), the camera zoom and field of view. `op_seq` changes with
    // every new request; the module acts once per change on the fields that are set (0 = leave).
    volatile std::uint32_t op_seq;
    std::int32_t op_sound, op_zoom, op_fov;
    std::uint32_t anchor_count;              // entries in anchors[0..anchor_count)
    Anchor        anchors[kMaxAnchors];
    // The questions above, and a counter that changes whenever the list does. The module answers a
    // list once and then leaves it alone, so a list that does not change costs nothing.
    std::uint32_t ask_seq;
    std::uint32_t ask_count;                 // entries in asks[0..ask_count)
    Ask           asks[kMaxAsks];
    std::uint32_t cc_count;
    CcRect        cc[kMaxCc];
    Command cmds[kMaxCmds];
};

}  // namespace rtx::marker
