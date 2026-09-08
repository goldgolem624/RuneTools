#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// Splits one decompressed archive blob into per-file byte ranges. Single-chunk NXT layout:
// byte 0 = chunk count (1), then N+1 big-endian i32 end-exclusive offsets, then the payloads.
// Result is indexed by file_id (largest_file_id + 1 entries); invalid slots left empty.

std::vector<std::vector<std::uint8_t>>
SplitArchive(const std::vector<std::uint8_t>& decompressed,
             const std::vector<int>& valid_file_ids,
             int largest_file_id);

}  // namespace rtx::cache
