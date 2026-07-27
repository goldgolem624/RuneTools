#include "MapTiles.h"

#include "InputStream.h"

#include <limits>

namespace rtx::cache {

MapTileData DecodeMapTiles(std::vector<std::uint8_t> file_bytes, int* leftover) {
    MapTileData out;
    if (leftover) *leftover = 0;
    out.settings.assign(4 * 64 * 64, 0);
    out.heights.assign(4 * 64 * 64, std::numeric_limits<std::int16_t>::min());
    out.underlay.assign(4 * 64 * 64, -1);
    out.overlay.assign(4 * 64 * 64, -1);
    out.shape.assign(4 * 64 * 64, -1);
    if (file_bytes.empty()) return out;

    // Optional 5-byte "jagx\x01" header (format 936) -> heights become u16.
    bool is936 = file_bytes.size() >= 5 && file_bytes[0] == 'j' && file_bytes[1] == 'a' &&
                 file_bytes[2] == 'g' && file_bytes[3] == 'x' && file_bytes[4] == 0x01;

    InputStream s(std::move(file_bytes));
    if (is936) s.skip(5);

    // rs3cache (4,64,64) C-order: plane, x, y.
    for (int plane = 0; plane < 4; ++plane) {
        for (int x = 0; x < 64; ++x) {
            for (int y = 0; y < 64; ++y) {
                // Ran short mid-walk = a real misparse; -1 tells the parse-health check.
                if (s.remaining() <= 0) { if (leftover) *leftover = -1; return out; }
                int idx = (plane * 64 + x) * 64 + y;
                int flags = s.ReadUnsignedByte();
                if (flags & 0x1) { out.shape[idx] = (std::int8_t)s.ReadUnsignedByte(); out.overlay[idx] = (std::int16_t)s.ReadUnsignedSmart(); }  // shape + overlay id
                if (flags & 0x2) out.settings[idx] = (std::uint8_t)s.ReadUnsignedByte();
                if (flags & 0x4) out.underlay[idx] = (std::int16_t)s.ReadUnsignedSmart();                          // underlay id
                if (flags & 0x8) out.heights[idx] =
                                     (std::int16_t)(is936 ? s.ReadUnsignedShort() : s.ReadUnsignedByte());
            }
        }
    }
    // After the full 4*64*64 walk the file continues with an 8-byte non-members
    // bitmask and an opcode-based ENVIRONMENT section (rsmv mapsquare_tiles.jsonc:
    // ops 0x80 env id, 0x00/0x01 lighting records, 0x02 three floats, 0x81 4x256B
    // blocks...). It is intentionally NOT consumed: op 0x01's record format varies
    // per entry in ways neither rsmv nor empirical brute-forcing resolves, and none
    // of it feeds a feature. The tile walk above was validated complete on all 5120
    // regions. `leftover` reports the trailer size for the health check;
    // -1 above (ran SHORT mid-walk) is the only misparse signal.
    if (leftover) *leftover = (int)s.remaining();
    return out;
}

}  // namespace rtx::cache
