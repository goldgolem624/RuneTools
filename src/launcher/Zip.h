#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal in-memory ZIP reader: STORED (0) and DEFLATE (8) entries, bounds-checked. An archive that
// another zip reader could list differently, or with two names for one file on disk, is refused whole,
// by both calls.

namespace rtx::launcher::zip {

bool ExtractFile(const std::uint8_t* data, std::size_t len,
                 const std::string& name, std::string& out);

// Names of every file entry in the central directory (directories excluded), in archive order.
bool ListFiles(const std::uint8_t* data, std::size_t len, std::vector<std::string>& names);

}  // namespace rtx::launcher::zip
