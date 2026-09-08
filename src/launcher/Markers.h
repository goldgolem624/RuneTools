#pragma once
// Per-account world-tile markers drawn by the overlay. Stored by region + local tile (region =
// global >> 6, local = global & 63) so they survive instances. Thread-safe; persists to
// %USERPROFILE%/RuneToolsX/markers/<account>.tsv.

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::markers {

struct TileMarker {
    int           region = 0;   // (regionX << 8) | regionY, regionX = globalX >> 6
    int           lx = 0, ly = 0;   // local tile within the region (0..63)
    int           plane = 0;        // 0..3
    std::uint32_t color = 0;        // 0xRRGGBB
    std::uint32_t color2 = 0;       // 0 = single-colour; else 0xRRGGBB second tone (two-tone tile)
    std::string   label;            // may be empty
};

// For the overlay render thread.
std::vector<TileMarker> Snapshot(std::uint32_t pid);

// Marker list as a JSON array for the UI:
//   [{"region":N,"lx":N,"ly":N,"plane":N,"color":"#rrggbb","label":".."},..]
std::string GetJson(std::uint32_t pid);

// Monotonic per-account version, bumped on every change.
std::uint64_t Version(std::uint32_t pid);

// A marker is uniquely identified by (region, lx, ly, plane).
bool Add(std::uint32_t pid, int region, int lx, int ly, int plane,
         std::uint32_t color, const std::string& label);
bool Remove(std::uint32_t pid, int region, int lx, int ly, int plane);
bool SetLabel(std::uint32_t pid, int region, int lx, int ly, int plane, const std::string& label);
bool SetColor(std::uint32_t pid, int region, int lx, int ly, int plane, std::uint32_t color,
              std::uint32_t color2 = 0);
bool Clear(std::uint32_t pid);

// Keybind entry: add (action > 0) or remove (action < 0) the tile under the cursor. Returns true
// when the store changed. No-op unless armed (SetKeybindArmed): default keys A/D double as movement.
bool MarkAtCursor(std::uint32_t pid, int action);

// Armed only while the Markers panel is open. Default disarmed.
void SetKeybindArmed(std::uint32_t pid, bool armed);
bool KeybindArmed(std::uint32_t pid);

// Global keybind + default-colour config, persisted to markers/_input.txt. VK codes.
struct Keybinds {
    int           markVk   = 0x41;       // 'A'
    int           removeVk = 0x44;       // 'D'
    std::uint32_t defColor = 0x46E0C0;
};
Keybinds GetKeybinds();
void     SetKeybinds(const Keybinds& kb);

}  // namespace rtx::markers
