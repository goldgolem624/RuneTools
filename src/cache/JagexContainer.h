#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::cache {

// NXT "ZL" wrapper: bytes 0-1 "ZL", 4-7 uncompressed size (BE), 8+ zlib stream. Empty on other input.
// `why` (optional) receives the reason an empty result came back ("container type 4", "inflate failed").
std::vector<std::uint8_t> Decompress(const std::vector<std::uint8_t>& raw, std::string* why = nullptr);

// Standard container: [type:1][compressedSize:4][(origSize:4)][payload]. type 0 stored,
// 1 bzip2 ("BZh1" stripped), 2 zlib/gzip, 3 lzma. Used by the sprite index (8) and every archive read.
std::vector<std::uint8_t> DecompressStandard(const std::vector<std::uint8_t>& raw, std::string* why = nullptr);

}  // namespace rtx::cache
