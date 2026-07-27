#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// NXT "ZL" wrapper (how NXT writes its js5-* archive blobs): bytes 0-1 = "ZL"
// magic, 4-7 = uncompressed size (BE), 8+ = zlib stream. Returns the
// file-container payload (the bytes SplitArchive walks); empty on any other
// input.
std::vector<std::uint8_t> Decompress(const std::vector<std::uint8_t>& raw);

// Standard RS3 container: [type:1][compressedSize:4][ (origSize:4) ][payload].
// type 0 = stored, 1 = bzip2 ("BZh1" header stripped), 2 = zlib/gzip
// (auto-detected); lzma unsupported -> empty. Used by the sprite index (8),
// which doesn't use the "ZL" wrapper.
std::vector<std::uint8_t> DecompressStandard(const std::vector<std::uint8_t>& raw);

}  // namespace rtx::cache
