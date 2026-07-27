#pragma once

#include <cstdint>
#include <string>

// Minimal in-memory ZIP reader (read-only) for plugin bundles. Supports STORED
// (method 0) and DEFLATE (method 8) entries via the vendored zlib. Bounds-checked.

namespace rtx::launcher::zip {

// Extract a single entry by exact name. Returns true and fills `out` on success.
bool ExtractFile(const std::uint8_t* data, std::size_t len,
                 const std::string& name, std::string& out);

}  // namespace rtx::launcher::zip
