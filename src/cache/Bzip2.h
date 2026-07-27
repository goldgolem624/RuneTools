#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtx::cache {

// Minimal, self-contained bzip2 decompressor (decompress-only) for the RS3 NXT
// cache. RS3 stores some archives -- notably newer (Necromancy-era) sprites --
// with compression type 1 (bzip2), which zlib can't handle; this fills that gap.
//
// `data`/`len` point at the bzip2 BLOCK stream, i.e. the NXT container body with
// its 4-byte "BZh1" header already stripped (so the stream begins at the first
// block magic, 0x314159265359). `orig_size` is the expected output length, used
// only as a reserve hint. Returns the decompressed bytes, or an empty vector on
// malformed input. Validated against libbzip2.
std::vector<std::uint8_t> Bzip2Decompress(const std::uint8_t* data, std::size_t len,
                                          std::size_t orig_size);

}  // namespace rtx::cache
