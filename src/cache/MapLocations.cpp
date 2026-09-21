#include "MapLocations.h"

#include "InputStream.h"

namespace rtx::cache {

namespace {

// Chained unsigned smarts: 0x7FFF means "add and continue".
int ReadSmarts(InputStream& s) {
    int value = 0;
    for (;;) {
        int off = s.ReadUnsignedSmart();
        if (off == 0x7FFF) { value += 0x7FFF; continue; }
        return value + off;
    }
}

}  // namespace

std::vector<LocPlacement> DecodeMapLocations(std::vector<std::uint8_t> file_bytes) {
    std::vector<LocPlacement> out;
    if (file_bytes.empty()) return out;
    InputStream s(std::move(file_bytes));

    int id = -1;
    while (s.remaining() > 0) {
        int inc = ReadSmarts(s);
        if (inc == 0) break;
        id += inc;

        int pos = 0;
        while (s.remaining() > 0) {
            int pinc = s.ReadUnsignedSmart();
            if (pinc == 0) break;
            pos += pinc - 1;

            int data = s.ReadUnsignedByte();
            LocPlacement p;
            p.id       = id;
            p.plane    = (pos >> 12) & 0x3;
            p.x        = (pos >> 6) & 0x3F;
            p.y        = pos & 0x3F;
            p.type     = (data >> 2) & 0x1F;
            p.rotation = data & 0x3;

            if (data >= 0x80) {
                int sub = s.ReadUnsignedByte();
                p.has_extra = sub != 0;
                if (sub & 0x01) for (int k = 0; k < 4; ++k) p.quat[k] = (short)s.ReadShort();
                if (sub & 0x02) p.tx = (short)s.ReadShort();
                if (sub & 0x04) p.ty = (short)s.ReadShort();
                if (sub & 0x08) p.tz = (short)s.ReadShort();
                if (sub & 0x10) p.scale = s.ReadUnsignedShort();
                if (sub & 0x20) p.sx = s.ReadUnsignedShort();
                if (sub & 0x40) p.sy = s.ReadUnsignedShort();
                if (sub & 0x80) p.sz = s.ReadUnsignedShort();
            }
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace rtx::cache
