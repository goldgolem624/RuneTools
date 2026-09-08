#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtx::cache {

// bzip2 decompressor for container compression type 1. `data` is the block stream with the
// 4-byte "BZh1" header stripped (starts at block magic 0x314159265359). Empty on malformed input.
std::vector<std::uint8_t> Bzip2Decompress(const std::uint8_t* data, std::size_t len,
                                          std::size_t orig_size);

}  // namespace rtx::cache
