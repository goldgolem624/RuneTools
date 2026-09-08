#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// NXT "ZL" wrapper: bytes 0-1 "ZL", 4-7 uncompressed size (BE), 8+ zlib stream. Empty on other input.
std::vector<std::uint8_t> Decompress(const std::vector<std::uint8_t>& raw);

// Standard container: [type:1][compressedSize:4][(origSize:4)][payload]. type 0 stored,
// 1 bzip2 ("BZh1" stripped), 2 zlib/gzip; lzma -> empty. Used by the sprite index (8).
std::vector<std::uint8_t> DecompressStandard(const std::vector<std::uint8_t>& raw);

}  // namespace rtx::cache
