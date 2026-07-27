#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

class SqliteIndexFile;

// Decodes the sprite at `sprite_id` from the sprites index (one archive
// per sprite, file 0 via the standard container) and re-encodes the first
// frame as a PNG. Returns empty on any failure. If the cache already holds
// the sprite as a PNG/standard image, those bytes pass through unchanged.
std::vector<std::uint8_t> SpriteAsPng(SqliteIndexFile& sprites_index,
                                      int sprite_id);

// As above, but capped to `max_side` (>=8) on the longest side instead of the default
// 64 -- lets a panel request exactly (or 2x) its display box so the browser-side
// rescale stays small and clean.
std::vector<std::uint8_t> SpriteAsPngScaled(SqliteIndexFile& sprites_index,
                                            int sprite_id, int max_side);

// Raw RGBA pixels (w*h*4) of the sprite's first frame, for compositing into the map
// render. Empty (w=h=0) on failure or when the cache stores it as a PNG.
std::vector<std::uint8_t> SpriteRawRgba(SqliteIndexFile& sprites_index,
                                        int sprite_id, int& w, int& h);

}  // namespace rtx::cache
