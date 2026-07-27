#pragma once
//
// Tile-marker store: persistent, per-account world-tile markers that the overlay draws into the
// game's 3D view. A marker is stored by REGION + LOCAL tile (regionX/Y = global >> 6; local =
// global & 63) so it survives sessions and stays meaningful inside instances whose absolute
// coordinates change. Single source of truth: the UI, the keybind and the overlay all go
// through it. Thread-safe; persists to %USERPROFILE%/RuneToolsX/markers/<account>.tsv.
//

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::markers {

struct TileMarker {
    int           region = 0;   // (regionX << 8) | regionY, regionX = globalX >> 6
    int           lx = 0, ly = 0;   // local tile within the region (0..63)
    int           plane = 0;        // 0..3
    std::uint32_t color = 0;        // 0xRRGGBB
    std::string   label;            // may be empty
};

// Thread-safe copy of a pid's account markers, for the overlay render thread.
std::vector<TileMarker> Snapshot(std::uint32_t pid);

// Marker list as a JSON array for the UI:
//   [{"region":N,"lx":N,"ly":N,"plane":N,"color":"#rrggbb","label":".."},..]
std::string GetJson(std::uint32_t pid);

// Monotonic per-account version; bumps on every change so the UI can detect keybind-added
// markers on its poll without diffing the whole list.
std::uint64_t Version(std::uint32_t pid);

// Granular mutators. A marker is uniquely identified by (region, lx, ly, plane).
bool Add(std::uint32_t pid, int region, int lx, int ly, int plane,
         std::uint32_t color, const std::string& label);
bool Remove(std::uint32_t pid, int region, int lx, int ly, int plane);
bool SetLabel(std::uint32_t pid, int region, int lx, int ly, int plane, const std::string& label);
bool SetColor(std::uint32_t pid, int region, int lx, int ly, int plane, std::uint32_t color);
bool Clear(std::uint32_t pid);

// Keybind handler entry: add (action > 0, idempotent) or remove (action < 0) a marker at the
// tile UNDER THE MOUSE CURSOR (inverse-projected from the live camera). Returns true when the
// store changed. Marking is GATED on the Markers panel being open (see SetKeybindArmed): with
// default keys A/D doubling as movement, a closed panel must never place tiles during play.
bool MarkAtCursor(std::uint32_t pid, int action);

// Arm/disarm the mark & delete keybinds for `pid`. The Markers panel calls this: armed only
// while its tab is the open (non-collapsed) panel. Disarmed -> MarkAtCursor is a no-op and the
// key falls through to the game. Default disarmed (a client with no open panel never marks).
void SetKeybindArmed(std::uint32_t pid, bool armed);
bool KeybindArmed(std::uint32_t pid);

// Global keybind + default-colour config (persisted to markers/_input.txt). VK codes. Marking
// is always active (no enable flag) -- the keys are the sole way to place/remove a tile.
struct Keybinds {
    int           markVk   = 0x41;       // 'A' = add a tile under the cursor
    int           removeVk = 0x44;       // 'D' = delete the tile under the cursor
    std::uint32_t defColor = 0x46E0C0;   // teal accent for new markers
};
Keybinds GetKeybinds();
void     SetKeybinds(const Keybinds& kb);

}  // namespace rtx::markers
