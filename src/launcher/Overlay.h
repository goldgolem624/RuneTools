#pragma once

#include <cstdint>
#include <string>
#include <vector>

// External world-grid overlay: a layered, click-through, topmost sibling window drawn over
// the game client. Projects world tiles/entities to client pixels from out-of-process reads.

namespace rtx::overlay {

struct Config {
    bool          enabled = false;
    bool          grid     = true;
    bool          players  = false;
    bool          npcs     = false;
    bool          objects  = false;
    bool          specials = false;  // draw type-4 markers (Time Sprite / Rockertunity)
    bool          walk_only = false; // grid: hide unwalkable tiles (else tint them red)
    bool          interactable = false;  // markers: only NPCs/objects that have actions (mirrors Scene tab)
    bool          markers  = false;  // draw the user's persistent tile markers (independent of the grid)
    // Nameplates: name labels over players/NPCs/objects, drawn in-frame. Independent of `enabled`.
    bool          nameplates = false;
    bool          np_players = true;
    bool          np_npcs    = true;
    bool          np_objects = false;
    int           np_range   = 20;   // nameplate draw range in tiles (Chebyshev, from the player)
    std::vector<int> np_player_uids;  // specific players to nameplate even when np_players is off (owned by SetNameplatePlayers)
    std::uint32_t pid      = 0;      // target client (0 = none)
    int           radius   = 12;     // grid radius in tiles around the player
    std::vector<std::string> highlight;
    std::vector<int>         outline;    // NPC uids (sec+0x88) to box-outline (Scene tab)
};

// Apply a new config. Thread-safe. Lazily spins up the render thread on first
// enable; the thread idles (window hidden) while disabled.
void Configure(const Config& c);

// Turn on tile-marker rendering for `pid` and ensure the render thread is running.
void EnableMarkers(std::uint32_t pid);

// Briefly flash `pid`'s game window (a fading tint over the game itself, not the launcher).
void Flash(std::uint32_t pid);

// Notification panel centred near the top of `pid`'s game window, `text` UTF-8, fading out.
void Toast(std::uint32_t pid, const std::string& text);

// Notification card on `pid`'s game window (top-right), clickable to dismiss: the overlay turns
// interactive only while the cursor is over a card. ttl_ms 0 = sticky, > 0 = auto-dismiss.
void Notify(std::uint32_t pid, const std::string& text, long long ttl_ms = 0);
void ClearNotifs();

// NPC names to highlight on `pid`'s game window (empty clears); independent of the grid toggle.
void SetHighlight(std::uint32_t pid, const std::vector<std::string>& names);

// Player uids (sec+0x88) to nameplate on `pid`, in addition to np_players. Owned separately so
// Configure() calls never clobber it.
void SetNameplatePlayers(std::uint32_t pid, const std::vector<int>& uids);
// NPC uids (sec+0x88) to box-outline on `pid` (empty clears): a 3D box around the live model
// AABB, independent of the grid/NPC toggles.
void SetOutline(std::uint32_t pid, const std::vector<int>& uids);

// Tick metronome on `pid`'s game window. `visual` draws the widget (drag to move, wheel to
// resize; position/size persist), `audio` clicks sounds/metronome.wav on each confirmed tick,
// `interval` = beat every N ticks (1..6), `locked` = click-through. Both false disables it.
void Metronome(std::uint32_t pid, bool visual, bool audio, int interval, bool locked);

// XP-tracker panel on `pid`'s game window: session gains + XP/h per skill and combined.
// `total` shows the all-skills row; `auto_skills` adds a row on any skill gain (else `mask`
// bit i = always show skill i); `locked` = click-through. Switching pid re-baselines.
void XpPanel(std::uint32_t pid, bool visible, bool locked, bool total,
             bool auto_skills, std::uint32_t mask);

// Re-baseline the XP session (gains and rates restart from the next sample).
void XpPanelReset(std::uint32_t pid);

// Live tracker metrics for the panel UI (mirrors the on-game widget):
//   {"on":b,"elapsed":MS,"total":{"gained":N,"ph":N},
//    "rows":[{"id":SKILL,"xp":N,"gained":N,"ph":N},..]}   (rows = all 29 skills)
std::string XpPanelStateJson(std::uint32_t pid);

// Transient guide marks: tile highlights + multi-line labels at world tile coords. Replaces the
// pid's whole set on every call; an empty vector clears it. Not persisted.
// rgb: 0 = default marker colour, else 0xRRGGBB; a coloured mark never takes the direction arrow.
// gx2/gy2: optional area extent -- draws a flat ground rect spanning (gx,gy)..(gx2,gy2).
// region: marks sharing (label, rgb) merge into ONE flat zone (single perimeter outline + label).
struct GuideMark { int gx = 0; int gy = 0; int plane = 0; std::string label; bool snapObj = false; int rgb = 0; int gx2 = 0; int gy2 = 0; int region = 0; int rgb2 = 0; };
void SetGuideMarks(std::uint32_t pid, std::vector<GuideMark> marks);

// Big screen-centre text, drawn as a kText pill at the gameview centre; empty clears.
//
// `slot` lets independent callers hold their own banner at the same time -- they stack upward
// from the original position instead of overwriting each other, so a prayer call and a boss
// mechanic are both readable. `rgb` is 0xRRGGBB, or -1 for the default red accent.
// Slot 0 with the default colour is exactly the original behaviour.
struct CenterBanner { std::string text; int rgb = -1; };
inline constexpr int kCenterSlots = 3;
void SetCenterText(std::uint32_t pid, const std::string& text, int slot = 0, int rgb = -1);

// Transient SCREEN-SPACE highlight rect (already client pixels, NOT world projected): outline +
// soft fill over a game UI element. Replaces the pid's rect on each call; w<=0 clears.
struct UiHighlight { int x = 0, y = 0, w = 0, h = 0; };
void SetUiHighlight(std::uint32_t pid, UiHighlight hl);
// SEVERAL screen-space highlights at once. Replaces the pid's set each call; an empty vector
// clears. SetUiHighlight is the one-rect form and shares this storage, so the two overwrite
// each other - there is one highlight set per client, not one per caller.
void SetUiHighlights(std::uint32_t pid, const std::vector<UiHighlight>& rects);

// Puzzle-box "click these in order" cells: screen-pixel rects, step 0 = the immediate next move.
// Replaces the pid's set each call; empty vector clears.
struct PuzzleCell { int x = 0, y = 0, w = 0, h = 0, step = 0, num = -1; };   // num >= 0 = label to show (e.g. knot click count); else step+1
void SetPuzzleCells(std::uint32_t pid, const std::vector<PuzzleCell>& cells);

// Celtic-knot arrow highlights: a corner-bracket box on each arrow to click, with the remaining
// click count in a pill above it. Screen pixels. Replaces the pid's set each call; empty clears.
struct KnotCell { int x = 0, y = 0, w = 0, h = 0, count = 0; };
void SetKnotCells(std::uint32_t pid, const std::vector<KnotCell>& cells);

// XP progress bars on the in-game Skills panel: one thin bar along the bottom edge of each skill
// cell, filled by progress to the next level. Rect is the CELL (interface space, same as the other
// interface-anchored channels); the bar is inset within it. pct = 0..1000 (tenths of a percent, so
// the channel stays integer). Replaces the pid's set each call; empty clears.
struct SkillBar { int x = 0, y = 0, w = 0, h = 0, pct = 0, rgb = 0; };
void SetSkillBars(std::uint32_t pid, const std::vector<SkillBar>& bars);

// Interfaces-tab panel visualizer: labeled boxes at panels' true screen rects (client pixels).
struct PanelBox { int x = 0, y = 0, w = 0, h = 0; std::string label; };
void SetPanelViz(std::uint32_t pid, const std::vector<PanelBox>& boxes);

// Tear down the render thread + window. Call once on launcher shutdown.
void Stop();

// Force a client's in-frame marker channel not-visible NOW. Call before detaching/closing an
// embedded client so it is inert before the game tears down its GL context.
void QuiesceMarkers(std::uint32_t pid);

}  // namespace rtx::overlay
