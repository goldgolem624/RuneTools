#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// One object placement from a MAPSV2 region (index 5, file 0 = land, 1 = water).
// Coordinates are local to the 64x64 region.
struct LocPlacement {
    int id       = -1;
    int plane    = 0;    // 0..3
    int x        = 0;    // 0..63 within region (west tile of multi-tile locs)
    int y        = 0;    // 0..63 within region (south tile)
    int type     = 0;    // shape/type 0..31 (walls 0-3, scenery 10/11, deco 22, ...)
    int rotation = 0;    // 0..3
};

// Decode a MAPSV2 LOCATIONS file: id-delta loop, per-id position-delta loop, attribute byte,
// extra block when attribute bit 0x80 is set.
std::vector<LocPlacement> DecodeMapLocations(std::vector<std::uint8_t> file_bytes);

}  // namespace rtx::cache
