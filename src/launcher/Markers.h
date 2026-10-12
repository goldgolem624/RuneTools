#pragma once
// Per-account world-tile markers, stored by region + local tile (region = global >> 6, local = global & 63) so they survive instances. Thread-safe.

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
    int           style = 0;        // kStyle*: how the tile is drawn
};
inline constexpr int kStyleFilled = 0, kStyleOutline = 1, kStyleCorners = 2;

std::vector<TileMarker> Snapshot(std::uint32_t pid);

std::string GetJson(std::uint32_t pid);

std::uint64_t Version(std::uint32_t pid);

bool Add(std::uint32_t pid, int region, int lx, int ly, int plane,
         std::uint32_t color, const std::string& label);
bool Remove(std::uint32_t pid, int region, int lx, int ly, int plane);
bool SetLabel(std::uint32_t pid, int region, int lx, int ly, int plane, const std::string& label);
bool SetColor(std::uint32_t pid, int region, int lx, int ly, int plane, std::uint32_t color,
              std::uint32_t color2 = 0);
bool SetStyle(std::uint32_t pid, int region, int lx, int ly, int plane, int style);
bool Clear(std::uint32_t pid);

bool MarkAtCursor(std::uint32_t pid, int action);

// A tile as the game shows it now, turned into the tile a marker is kept under: inside an instance
// that is the tile it was copied from, which is the same on every visit. Anywhere else, and for an
// instance tile copied from nowhere, it is the tile itself.
void ToStored(std::uint32_t pid, int& region, int& lx, int& ly, int& plane);

void SetKeybindArmed(std::uint32_t pid, bool armed);
bool KeybindArmed(std::uint32_t pid);

struct Keybinds {
    int           markVk   = 0x41;       // 'A'
    int           removeVk = 0x44;       // 'D'
    std::uint32_t defColor = 0x46E0C0;
    int           defStyle = 0;          // kStyle* a new marker takes
    int           width    = 2;          // outline thickness, 1..5
    int           labelPx  = 14;         // label glyph height, 10..28
};
Keybinds GetKeybinds();
void     SetKeybinds(const Keybinds& kb);

}  // namespace rtx::markers
