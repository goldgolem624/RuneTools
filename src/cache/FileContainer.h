#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

// Splits one decompressed archive blob into per-file byte ranges.
//
// NXT layout (single-chunk; the only one we've ever observed in the
// indexes we read):
//   byte 0          : chunk count, always 1 (skipped)
//   bytes 1..N*4+4  : N+1 big-endian i32 offsets, end-exclusive
//   bytes N*4+5..   : the file payloads concatenated, file i runs
//                     from offsets[i] to offsets[i+1]
//
// Returns a vector indexed by file_id (largest_file_id + 1 entries);
// non-`valid_file_ids` slots are left empty.

std::vector<std::vector<std::uint8_t>>
SplitArchive(const std::vector<std::uint8_t>& decompressed,
             const std::vector<int>& valid_file_ids,
             int largest_file_id);

}  // namespace rtx::cache
