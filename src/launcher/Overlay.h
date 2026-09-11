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

struct PuzzleCell { int x = 0, y = 0, w = 0, h = 0, step = 0, num = -1; };   // num >= 0 overrides the step+1 label
void SetPuzzleCells(std::uint32_t pid, const std::vector<PuzzleCell>& cells);

struct KnotCell { int x = 0, y = 0, w = 0, h = 0, count = 0; };
void SetKnotCells(std::uint32_t pid, const std::vector<KnotCell>& cells);

struct SkillBar { int x = 0, y = 0, w = 0, h = 0, pct = 0, rgb = 0; };
void SetSkillBars(std::uint32_t pid, const std::vector<SkillBar>& bars);

struct PanelBox { int x = 0, y = 0, w = 0, h = 0; std::string label; };
void SetPanelViz(std::uint32_t pid, const std::vector<PanelBox>& boxes);

void Stop();

void QuiesceMarkers(std::uint32_t pid);


}  // namespace rtx::overlay
