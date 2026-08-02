#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// Decoded MAPSV2 land tiles (index 5, file 3). Both arrays are flat 4*64*64,
// indexed (plane*64 + x)*64 + y. Faithful to the reference decode Tile::dump (feature "rs3").
struct MapTileData {
    std::vector<std::uint8_t> settings;  // 0 default; bit 0x1 = blocked/void, 0x2 = bridge
    std::vector<std::int16_t> heights;    // cache height units; INT16_MIN where the tile carried none
    std::vector<std::int16_t> underlay;   // underlay config id (ground colour), -1 where none
    std::vector<std::int16_t> overlay;    // overlay config id (paths/water/edges), -1 where none
    std::vector<std::int8_t>  shape;      // tile shape (0..47, how overlay splits the tile), -1 where none
};

// leftover (optional): bytes remaining after the full tile walk = the trailing
// non-members bitmask + environment section, which is intentionally not consumed
// (see the note in DecodeMapTiles); -1 = the stream ran SHORT mid-walk, i.e. a
// real misparse. Used by the cache parse-health check.
MapTileData DecodeMapTiles(std::vector<std::uint8_t> file_bytes, int* leftover = nullptr);

}  // namespace rtx::cache
