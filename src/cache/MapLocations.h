#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// One object placement decoded from a MAPSV2 region (index 5, file 0 = land,
// file 1 = water). Coordinates are local to the 64x64 region; the caller adds
// the region origin (rx*64, ry*64) for world tiles.
struct LocPlacement {
    int id       = -1;
    int plane    = 0;    // 0..3
    int x        = 0;    // 0..63 within region (west tile of multi-tile locs)
    int y        = 0;    // 0..63 within region (south tile)
    int type     = 0;    // shape/type 0..31 (walls 0-3, scenery 10/11, deco 22, ...)
    int rotation = 0;    // 0..3
};

// Decode a MAPSV2 LOCATIONS file (index 5, file 0 or 1) into placements.
// Faithful to rs3cache Location::dump (feature "rs3"): id-delta loop +
// per-id position-delta loop, attribute byte, and the RS3 sub-data extra
// block read when the attribute byte has bit 0x80 set.
std::vector<LocPlacement> DecodeMapLocations(std::vector<std::uint8_t> file_bytes);

}  // namespace rtx::cache
