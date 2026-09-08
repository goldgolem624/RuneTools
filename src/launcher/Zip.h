#pragma once

#include <cstdint>
#include <string>

// Minimal in-memory ZIP reader: STORED (0) and DEFLATE (8) entries, bounds-checked.

namespace rtx::launcher::zip {

bool ExtractFile(const std::uint8_t* data, std::size_t len,
                 const std::string& name, std::string& out);

}  // namespace rtx::launcher::zip
