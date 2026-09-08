#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../reader/Reader.h"   // OutlineLocReq

// External overlay: layered click-through topmost window over the game client, fed by out-of-process reads.

namespace rtx::overlay {

struct Config {
    bool          enabled = false;
    bool          grid     = true;
    bool          players  = false;
    bool          npcs     = false;
    bool          objects  = false;
    bool          specials = false;  // type-4 markers (Time Sprite / Rockertunity)
    bool          walk_only = false; // grid: hide unwalkable tiles (else tint red)
    bool          interactable = false;  // markers: only NPCs/objects with actions
    bool          markers  = false;  // persistent tile markers, independent of the grid
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

// Thread-safe. Lazily starts the render thread on first enable; it idles (window hidden) while disabled.
void Configure(const Config& c);

void EnableMarkers(std::uint32_t pid);

// Fading tint over the game window itself.
void Flash(std::uint32_t pid);

// Fading notification panel near the top of the game window; `text` UTF-8.
void Toast(std::uint32_t pid, const std::string& text);

// Top-right notification card, click to dismiss. ttl_ms 0 = sticky.
void Notify(std::uint32_t pid, const std::string& text, long long ttl_ms = 0);
void ClearNotifs();

// NPC names to highlight (empty clears); independent of the grid toggle.
void SetHighlight(std::uint32_t pid, const std::vector<std::string>& names);

// Player uids (sec+0x88) to nameplate in addition to np_players. Stored apart from Config.
void SetNameplatePlayers(std::uint32_t pid, const std::vector<int>& uids);
// NPC uids (sec+0x88) to box-outline (empty clears); independent of grid/NPC toggles.
void SetOutline(std::uint32_t pid, const std::vector<int>& uids);
void SetOutlineLocs(std::uint32_t pid, const std::vector<rtx::reader::OutlineLocReq>& locs);

// `interval` = beat every N ticks (1..6), `locked` = click-through. Both false disables.
void Metronome(std::uint32_t pid, bool visual, bool audio, int interval, bool locked);

// `auto_skills` adds a row on any gain, else `mask` bit i shows skill i. Switching pid re-baselines.
void XpPanel(std::uint32_t pid, bool visible, bool locked, bool total,
             bool auto_skills, std::uint32_t mask);

void XpPanelReset(std::uint32_t pid);

// {"on":b,"elapsed":MS,"total":{"gained":N,"ph":N},"rows":[{"id":SKILL,"xp":N,"gained":N,"ph":N},..]}
std::string XpPanelStateJson(std::uint32_t pid);

// Transient guide marks at world tile coords; replaces the pid's set each call, empty clears.
// rgb 0 = default colour. gx2/gy2 = area extent. region: marks sharing (label, rgb) merge into one zone.
struct GuideMark { int gx = 0; int gy = 0; int plane = 0; std::string label; bool snapObj = false; int rgb = 0; int gx2 = 0; int gy2 = 0; int region = 0; int rgb2 = 0; };
void SetGuideMarks(std::uint32_t pid, std::vector<GuideMark> marks);

// Screen-centre text pill; empty clears. Slots stack upward so independent callers coexist. rgb -1 = default.
struct CenterBanner { std::string text; int rgb = -1; };
inline constexpr int kCenterSlots = 3;
void SetCenterText(std::uint32_t pid, const std::string& text, int slot = 0, int rgb = -1);

// Screen-space (client pixel) highlight rects; w<=0 / empty clears. Both forms share one set per client.
struct UiHighlight { int x = 0, y = 0, w = 0, h = 0; };
void SetUiHighlight(std::uint32_t pid, UiHighlight hl);
void SetUiHighlights(std::uint32_t pid, const std::vector<UiHighlight>& rects);

// Puzzle-box click order, screen pixels; step 0 = next move.
struct PuzzleCell { int x = 0, y = 0, w = 0, h = 0, step = 0, num = -1; };   // num >= 0 overrides the step+1 label
void SetPuzzleCells(std::uint32_t pid, const std::vector<PuzzleCell>& cells);

// Celtic-knot arrow brackets with remaining click count, screen pixels.
struct KnotCell { int x = 0, y = 0, w = 0, h = 0, count = 0; };
void SetKnotCells(std::uint32_t pid, const std::vector<KnotCell>& cells);

// Skills-panel progress bars. Rect is the skill cell (interface space); pct = 0..1000 (tenths of a percent).
struct SkillBar { int x = 0, y = 0, w = 0, h = 0, pct = 0, rgb = 0; };
void SetSkillBars(std::uint32_t pid, const std::vector<SkillBar>& bars);

// Interfaces-tab panel visualizer: labeled boxes at panel screen rects.
struct PanelBox { int x = 0, y = 0, w = 0, h = 0; std::string label; };
void SetPanelViz(std::uint32_t pid, const std::vector<PanelBox>& boxes);

// Tear down the render thread + window; call once on shutdown.
void Stop();

// Hide a client's in-frame marker channel now; call before detaching an embedded client (GL context teardown).
void QuiesceMarkers(std::uint32_t pid);


}  // namespace rtx::overlay
