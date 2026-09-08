#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// Decoded MAPSV2 land tiles (index 5, file 3). Flat 4*64*64 arrays indexed (plane*64 + x)*64 + y.
struct MapTileData {
    std::vector<std::uint8_t> settings;  // 0 default; bit 0x1 = blocked/void, 0x2 = bridge
    std::vector<std::int16_t> heights;    // cache height units; INT16_MIN where the tile carried none
    std::vector<std::int16_t> underlay;   // underlay config id (ground colour), -1 where none
    std::vector<std::int16_t> overlay;    // overlay config id (paths/water/edges), -1 where none
    std::vector<std::int8_t>  shape;      // tile shape (0..47, how overlay splits the tile), -1 where none
};

// leftover: bytes remaining after the tile walk (the unconsumed trailer); -1 = ran short mid-walk.
MapTileData DecodeMapTiles(std::vector<std::uint8_t> file_bytes, int* leftover = nullptr);

}  // namespace rtx::cache
