#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../reader/Reader.h"   // OutlineLocReq


namespace rtx::overlay {

struct Config {
    bool          enabled = false;
    bool          grid     = true;
    bool          players  = false;
    bool          npcs     = false;
    bool          objects  = false;
    bool          specials = false;  // type-4 markers (Time Sprite / Rockertunity)
    bool          walk_only = false; // grid: hide unwalkable tiles (else tint red)
    bool          true_tile = false; // outline the server tile (movement route) of the local player, and of moving NPCs/players whose markers are on
    bool          occlude = true;    // markers faded where the scene is in front of them (Vulkan)
    bool          occlude_hide = false;   // with occlude: those parts are not drawn at all
    bool          inframe_trial = false;  // trial of drawing inside the game's frame, under its interface (Vulkan)
    bool          interactable = false;  // markers: only NPCs/objects with actions
    bool          markers  = false;  // persistent tile markers, independent of the grid
    bool          hover_outline = false;  // outline the object under the cursor, independent of `enabled`
    bool          tooltip_values = false; // Grand Exchange price and alch value in the game's item tooltips
    // The game's own entity highlight, per category. -1 leaves the player's setting alone; scale 0
    // is the silhouette that fills the entity, above it a border of that size.
    std::int32_t  hover_scale[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::int32_t  hover_mode[8]  = { -1, -1, -1, -1, -1, -1, -1, -1 };
    // The game's own world markers pointed at a target: its arrow, the chevrons at the player's
    // feet and a marker on the target's tile.
    bool          mark_test = false;
    std::uint32_t mark_model = 0;         // model laid on the tile
    std::uint32_t mark_style = 0;         // which of the game's arrows
    std::uint32_t mark_height = 0;        // arrow height, 0..255
    std::int32_t  mark_pointer = -1;      // the arrow at the player's feet; -1 = none
    std::uint32_t mark_tile_rgb = 0, mark_tile_width = 0;     // the game's outline around each; rgb 0 = none
    std::uint32_t mark_arrow_rgb = 0, mark_arrow_width = 0;   // the arrow at the player's feet
    std::uint32_t mark_range = 90;        // tiles within which the arrows show, 1..90
    std::uint32_t mark_pointer_scale = 100;   // percent, 100..400
    std::uint32_t mark_pointer_reach = 0;     // how far from the player it sits, 512 = one tile
    bool          mark_arrow = true;          // the arrow over the tile; off leaves the marked tile alone
    // where the target is, found by the panel: -1 = nowhere in view, 0 = a tile, 1 = an NPC by its index in the game
    int           mark_kind = -1;
    int           mark_x = 0, mark_y = 0, mark_plane = 0, mark_npc_uid = -1;
    bool          mark_path = true;           // the game's trail of diamonds from the player to the target
    int           mark_from_x = 0, mark_from_y = 0;   // the player's tile, the near end of that trail
    std::uint32_t hover_rgb[8] = {};      // 0xRRGGBB per game highlight category (3 NPCs, 4 attackable, 5 scenery), 0 = the game's own
    bool          nameplates = false;  // in-frame name labels, independent of `enabled`
    bool          np_players = true;
    bool          np_npcs    = true;
    bool          np_objects = false;
    int           np_range   = 20;   // tiles, Chebyshev from the player
    std::vector<int> np_player_uids;  // owned by SetNameplatePlayers
    std::uint32_t pid      = 0;      // target client (0 = none)
    int           radius   = 12;     // grid radius in tiles
    std::vector<std::string> highlight;
    std::vector<int>         outline;    // NPC uids (sec+0x88) to box-outline
    std::vector<rtx::reader::OutlineLocReq> outlineLocs;   // objects to box-outline
};

void Configure(const Config& c);

void EnableMarkers(std::uint32_t pid);

void Flash(std::uint32_t pid);

void Toast(std::uint32_t pid, const std::string& text);

void Notify(std::uint32_t pid, const std::string& text, long long ttl_ms = 0);
void ClearNotifs();

void SetHighlight(std::uint32_t pid, const std::vector<std::string>& names);

// Player uids (sec+0x88) to nameplate in addition to np_players. Stored apart from Config.
void SetNameplatePlayers(std::uint32_t pid, const std::vector<int>& uids);
void SetOutline(std::uint32_t pid, const std::vector<int>& uids);
void SetOutlineLocs(std::uint32_t pid, const std::vector<rtx::reader::OutlineLocReq>& locs);

void Metronome(std::uint32_t pid, bool visual, bool audio, int interval, bool locked);

void XpPanel(std::uint32_t pid, bool visible, bool locked, bool total,
             bool auto_skills, std::uint32_t mask);

void XpPanelReset(std::uint32_t pid);

std::string XpPanelStateJson(std::uint32_t pid);

struct GuideMark { int gx = 0; int gy = 0; int plane = 0; std::string label; bool snapObj = false; int rgb = 0; int gx2 = 0; int gy2 = 0; int region = 0; int rgb2 = 0; };
void SetGuideMarks(std::uint32_t pid, std::vector<GuideMark> marks);

struct CenterBanner { std::string text; int rgb = -1; };
inline constexpr int kCenterSlots = 3;
void SetCenterText(std::uint32_t pid, const std::string& text, int slot = 0, int rgb = -1);

struct UiHighlight { int x = 0, y = 0, w = 0, h = 0; };
void SetUiHighlight(std::uint32_t pid, UiHighlight hl);
void SetUiHighlights(std::uint32_t pid, const std::vector<UiHighlight>& rects);

// Interface-space text labels (same coordinate space as UiHighlight): drawn plain, left aligned, vertically
// centred on y. Used by panels that annotate an open interface, e.g. the bank value readout by its title.
// style 0: bare text, left edge at x, centred on y. style 1: pill (dark rounded box) centred at (x, y).
struct UiLabel { int x = 0, y = 0; int rgb = -1; int px = 13; int style = 0; std::string text; };
void SetUiLabels(std::uint32_t pid, const std::vector<UiLabel>& labels);
// One-shot requests carried out by the game itself: a sound effect by id, camera zoom, field of
// view (0 = leave). Each call is one request.
void RequestEngine(std::uint32_t pid, int sound, int zoom, int fov);
// Questions the game answers about the account: whether an achievement's requirements are met and
// how far along they are, and whether a quest is started or finished. The game weighs them against
// the live stats, the account and today's date, so the answers are its own rather than ours. The
// whole list is asked at once and replaces the list before it; answers come back through
// gameui::ModuleAnswers, matched by a tag of kind and id.
struct AccountAsk {
    int kind = 0; int id = 0; int arg = 0;
    bool operator==(const AccountAsk& o) const { return kind == o.kind && id == o.id && arg == o.arg; }
    bool operator!=(const AccountAsk& o) const { return !(*this == o); }
};
void AskAccount(std::uint32_t pid, const std::vector<AccountAsk>& asks);

struct PuzzleCell { int x = 0, y = 0, w = 0, h = 0, step = 0, num = -1; };   // num >= 0 overrides the step+1 label
void SetPuzzleCells(std::uint32_t pid, const std::vector<PuzzleCell>& cells);

struct KnotCell { int x = 0, y = 0, w = 0, h = 0, count = 0; };
void SetKnotCells(std::uint32_t pid, const std::vector<KnotCell>& cells);

// x,y,w,h: the cell on screen (the overlay draws there); cc_parent > 0: the cell as a component of that
// interface component, with its rectangle in the parent's own coordinates (the game draws there)
struct SkillBar { int x = 0, y = 0, w = 0, h = 0, pct = 0, rgb = 0; int cc_parent = 0, cc_sub = 0, cx = 0, cy = 0, cw = 0, ch = 0; };
void SetSkillBars(std::uint32_t pid, const std::vector<SkillBar>& bars);

struct PanelBox { int x = 0, y = 0, w = 0, h = 0; std::string label; };
void SetPanelViz(std::uint32_t pid, const std::vector<PanelBox>& boxes);

void Stop();

void QuiesceMarkers(std::uint32_t pid);


}  // namespace rtx::overlay
