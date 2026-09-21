#pragma once
// The game's own world markers: the hint arrow that floats over a tile together with the arrow
// at the player's feet that points the way to it, and the trail that lays one model on every
// tile of a path. The game builds, animates and draws all of it; this only asks for it, through
// the same two routines the game runs when the server asks, and on the game's own thread.

#include <cstddef>
#include <cstdint>

namespace rtx::enginemark {

bool Install();
void Uninstall();

struct Want {
    // one marked tile: `tile_model` is laid flat on it, following the ground
    bool          tile_on = false;
    std::int32_t  tile_x = 0, tile_y = 0;
    std::uint32_t tile_model = 0;
    std::uint32_t tile_rgb = 0;         // the game's outline around it, 0xRRGGBB; 0 = none
    std::uint32_t tile_width = 0;       // outline width, the game's own hover outline uses 4
    bool          tile_steady = true;   // hold it at one height; false leaves the game's slow rise and fall, made for upright models
    // a trail on the ground from one tile to another, `path_model` on every tile the game steps onto between them
    bool          path_on = false;
    std::int32_t  path_x0 = 0, path_y0 = 0, path_x1 = 0, path_y1 = 0;
    std::uint32_t path_model = 0;
    // an arrow over a tile, or over an NPC's head when `arrow_npc` names one (its index in the game; -1 = the tile)
    bool          arrow_on = false;
    std::int32_t  arrow_npc = -1;
    std::int32_t  arrow_x = 0, arrow_y = 0, arrow_plane = 0;
    std::uint32_t arrow_style = 0;      // which of the game's arrows
    std::uint32_t arrow_height = 0;     // 0..255, in eighths of the game's height unit, above the ground
    std::uint32_t arrow_range = 0;      // both arrows show only within this many tiles of the tile, 1..90
    // The arrow at the player's feet that turns towards the tile: a model, -1 = none. The arrow
    // over the tile is a picture the game draws flat on the screen, so only this one can carry
    // an outline and a size.
    std::int32_t  arrow_pointer = -1;
    std::uint32_t arrow_rgb = 0;        // its outline, as for the tile
    std::uint32_t arrow_width = 0;
    std::uint32_t pointer_scale = 100;  // its size in percent, 100..400
    std::uint32_t pointer_reach = 0;    // how far out from the player it sits, towards the tile; 512 = one tile
};

// Safe from any thread; the change is carried out on the game's thread at its next frame.
void Update(const Want& want);

// One line about what was last asked of the game, for the log; false when there is nothing new.
bool TakeLog(char* out, std::size_t cap);

}  // namespace rtx::enginemark
