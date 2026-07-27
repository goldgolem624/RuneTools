#include "MapLocations.h"

#include "InputStream.h"

namespace rtx::cache {

namespace {

// id delta = get_smarts: chained unsigned smarts, each 0x7FFF means "add and
// continue" so deltas can exceed a single 15-bit smart.
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
            out.push_back(p);

            // RS3-only extra block: present whenever the attribute byte has
            // bit 0x80 set (effectively every placement). The sub-data byte's
            // bits select trailing u16s we don't need but must consume.
            if (data >= 0x80) {
                int sub = s.ReadUnsignedByte();
                if (sub != 0) {
                    if (sub & 0x01) s.skip(8);   // 4 x u16
                    if (sub & 0x02) s.skip(2);
                    if (sub & 0x04) s.skip(2);
                    if (sub & 0x08) s.skip(2);
                    if (sub & 0x10) {
                        s.skip(2);
                    } else {
                        if (sub & 0x20) s.skip(2);
                        if (sub & 0x40) s.skip(2);
                        if (sub & 0x80) s.skip(2);
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace rtx::cache
