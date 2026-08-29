#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Small PNG decoder for the icon-capture validator: it only has to read the icons in
// items.pack (8-bit RGB/RGBA/gray/gray+alpha and 1/2/4/8-bit palette, filters 0-4,
// non-interlaced) so the launcher can compare a live atlas crop with the bundled art.
// Inflate comes from the vendored zlib (CacheVendor.lib). Output is RGBA8, top-down.

namespace rtx::launcher::png {

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;   // w*h*4
};

// Returns false (and leaves `out` empty) on anything it does not understand.
bool Decode(const std::uint8_t* data, std::size_t len, Image& out);

}  // namespace rtx::launcher::png
