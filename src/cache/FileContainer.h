#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::cache {

// Splits one decompressed archive blob into per-file byte ranges. Single-chunk NXT: byte 0 = chunk count (1), then N+1 big-endian i32 end-exclusive offsets, then the payloads.
// Result is indexed by file_id (largest_file_id + 1 entries); invalid slots left empty.
// The offset table must start the first file right after itself and end the last one inside the
// blob; otherwise the result is empty and `why` (optional) says "file table layout changed".

std::vector<std::vector<std::uint8_t>>
SplitArchive(const std::vector<std::uint8_t>& decompressed,
             const std::vector<int>& valid_file_ids,
             int largest_file_id,
             std::string* why = nullptr);

}  // namespace rtx::cache
