#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtx::cache {

// Raw LZMA1 stream: 5 property bytes (lc/lp/pb packed, then the dictionary size) followed by
// range-coded data, with the decoded size supplied by the caller. Empty on any malformed input.
std::vector<std::uint8_t> LzmaDecompress(const std::uint8_t* data, std::size_t size,
                                         std::size_t decoded_size);

}  // namespace rtx::cache
