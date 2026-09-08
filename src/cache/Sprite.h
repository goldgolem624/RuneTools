#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

class SqliteIndexFile;

// First frame of sprite `sprite_id` (one archive per sprite, standard container) as PNG.
// Empty on failure; cached PNGs pass through unchanged.
std::vector<std::uint8_t> SpriteAsPng(SqliteIndexFile& sprites_index,
                                      int sprite_id);

// As above, capped to `max_side` (>=8) on the longest side (default 64).
// `frame` selects a frame of a multi-frame group (chat <img=N> icons).
std::vector<std::uint8_t> SpriteAsPngScaled(SqliteIndexFile& sprites_index,
                                            int sprite_id, int max_side, int frame = 0);

// RGBA buffer -> PNG colour type 2 (alpha dropped), UP-filtered, deflate-compressed.
std::vector<std::uint8_t> EncodePngRgb(const std::uint8_t* rgba, int w, int h);

// Raw RGBA (w*h*4) of the first frame. Empty (w=h=0) on failure or when the cache stores a PNG.
std::vector<std::uint8_t> SpriteRawRgba(SqliteIndexFile& sprites_index,
                                        int sprite_id, int& w, int& h);

}  // namespace rtx::cache
