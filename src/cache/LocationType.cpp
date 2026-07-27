#include "LocationType.h"

namespace rtx::cache {

namespace {

// Opcode dispatcher for the RS3 LocationConfig format. Returns false
// on an unknown opcode (terminates the loop -- a misaligned stream past an
// unknown opcode is garbage anyway). Only name/options/footprint/members are
// retained; the rest are consumed to keep the stream aligned.
bool ReadOne(InputStream& s, LocDef& d, int op) {
    switch (op) {
        case 1: {                                    // models: count x (type, sub-count x smart32)
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) {
                s.ReadByte();                        // model type
                int sub = s.ReadUnsignedByte();
                for (int j = 0; j < sub; ++j) s.ReadBigSmart();
            }
            return true;
        }
        case 2: d.name = s.ReadString();             return true;
        case 14: d.dim_x = s.ReadUnsignedByte();     return true;
        case 15: d.dim_y = s.ReadUnsignedByte();     return true;
        case 17: d.no_clip = true;                   return true;   // unknown_17: does not block walking
        case 18: case 21: case 22: case 23:          return true;   // flags, no payload
        case 19: s.ReadUnsignedByte();               return true;
        case 24: s.ReadBigSmart();                   return true;   // smart32 (animation)
        case 27:                                     return true;
        case 28: case 29: s.ReadByte();              return true;
        case 39: s.ReadByte();                       return true;   // contrast (i8)
        case 40: case 41: {                          // recolour / retexture
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) { s.ReadUnsignedShort(); s.ReadUnsignedShort(); }
            return true;
        }
        case 44: case 45: s.skip(2);                 return true;
        case 62: case 64:                            return true;
        case 65: case 66: case 67: s.ReadUnsignedShort(); return true;   // scale x/y/z
        case 69: s.ReadUnsignedByte();               return true;
        case 70: case 71: case 72: s.ReadUnsignedShort(); return true;   // translate x/y/z
        case 73: case 74:                            return true;
        case 75: s.ReadUnsignedByte();               return true;
        case 77: {                                   // morph table (varbit/varp -> child ids)
            int vb = s.ReadUnsignedShort(); int vp = s.ReadUnsignedShort();
            d.morph_varbit = (vb == 0xFFFF) ? -1 : vb;
            d.morph_varp   = (vp == 0xFFFF) ? -1 : vp;
            int n = s.ReadUnsignedSmart();
            for (int i = 0; i <= n; ++i) {
                int c = s.ReadBigSmart();
                d.morph_variants.push_back(c);                // index-aligned (keeps -1)
                if (c >= 0) d.morph_children.push_back(c);
            }
            return true;
        }
        case 78: s.ReadUnsignedShort(); s.ReadUnsignedByte(); return true;
        case 79: {
            s.ReadUnsignedShort(); s.ReadUnsignedShort(); s.ReadUnsignedByte();
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadUnsignedShort();
            return true;
        }
        case 81: s.ReadUnsignedByte();               return true;
        case 82:                                     return true;
        case 88: case 89:                            return true;
        case 91: d.members = true;                   return true;
        case 92: {                                   // extended morph table (+ default child)
            int vb = s.ReadUnsignedShort(); int vp = s.ReadUnsignedShort();
            d.morph_varbit = (vb == 0xFFFF) ? -1 : vb;
            d.morph_varp   = (vp == 0xFFFF) ? -1 : vp;
            int def_child = s.ReadBigSmart();
            d.morph_default = def_child;
            if (def_child >= 0) d.morph_children.push_back(def_child);
            int n = s.ReadUnsignedSmart();
            for (int i = 0; i <= n; ++i) {
                int c = s.ReadBigSmart();
                d.morph_variants.push_back(c);                // index-aligned (keeps -1)
                if (c >= 0) d.morph_children.push_back(c);
            }
            return true;
        }
        case 93: s.ReadUnsignedShort();              return true;
        case 94:                                     return true;
        case 95: s.ReadUnsignedShort();              return true;
        case 97: case 98:                            return true;
        case 102: d.mapscene = s.ReadUnsignedShort(); return true;   // mapscene icon id (0x66)
        case 103:                                    return true;
        case 104: s.ReadUnsignedByte();              return true;
        case 106: {                                  // head models
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) { s.ReadBigSmart(); s.ReadUnsignedByte(); }
            return true;
        }
        case 107: d.mapFunction = s.ReadUnsignedShort(); return true;   // worldmap maplabel id (0x6B) -> config 2/arch 36 icon+text
        // Build ~950 additions, all derived from live data and full-index validated
        // (139143/139143 clean; the alternatives break real files):
        case 108: case 109: case 110:                return true;   // payload-less flags
        case 159:                                    return true;
        case 166: s.ReadShort();                     return true;   // i16 (observed +-200)
        case 160: {                                  // unknown_160
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadUnsignedShort();
            return true;
        }
        case 162: s.ReadInt();                       return true;
        case 163: s.skip(4);                         return true;
        case 164: case 165: case 167: s.ReadUnsignedShort(); return true;
        case 170: case 171: s.ReadUnsignedSmart();   return true;
        case 173: s.ReadUnsignedShort(); s.ReadUnsignedShort(); return true;
        case 177:                                    return true;
        case 178: s.ReadUnsignedByte();              return true;
        case 186: s.ReadUnsignedByte();              return true;
        case 188: case 189:                          return true;
        case 196: case 197: s.ReadUnsignedByte();    return true;
        case 198: case 199:                          return true;
        case 201: for (int i = 0; i < 6; ++i) s.ReadUnsignedSmart(); return true;
        case 202: s.ReadUnsignedSmart();             return true;
        case 203:                                    return true;
        case 204: {                                  // unknown_204 list (27 bytes each)
            int n = s.ReadUnsignedSmart();
            for (int i = 0; i < n; ++i) {
                s.ReadUnsignedShort(); s.ReadUnsignedByte();
                for (int j = 0; j < 6; ++j) s.ReadInt();
            }
            return true;
        }
        // Ops 205/206 (build ~950) carry a u16 payload-length prefix, but we parse the
        // fields rather than skip: the boundary check then doubles as drift detection
        // (a mismatch surfaces as an unknown-op stop instead of silently misreading).
        // Both structures full-index validated 139143/139143 with exact boundary
        // landings.
        case 205: {                                  // sparse morph table: value ranges -> child
            int len = s.ReadUnsignedShort();
            int end = s.offset() + len;
            int vb = s.ReadUnsignedShort(); int vp = s.ReadUnsignedShort();
            d.morph_varbit = (vb == 0xFFFF) ? -1 : vb;
            d.morph_varp   = (vp == 0xFFFF) ? -1 : vp;
            s.ReadUnsignedByte(); s.ReadUnsignedByte();   // always 2, 1 in live data
            s.ReadUnsignedByte();                         // 1..3 in live data (unknown)
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) {
                int lo = s.ReadUnsignedShort();           // value range (lo == hi in all
                int hi = s.ReadUnsignedShort();           // live data so far)
                int c  = s.ReadBigSmart();
                if (hi < lo || hi > 1024) return false;   // hostile/drifted range
                // Keep the value-indexed morph_variants contract (variants[value] =
                // child): expand the range, padding unmapped values with -1.
                while ((int)d.morph_variants.size() < lo) d.morph_variants.push_back(-1);
                for (int v = lo; v <= hi; ++v) {
                    if ((int)d.morph_variants.size() <= v) d.morph_variants.push_back(c);
                    else d.morph_variants[v] = c;
                }
                if (c >= 0) d.morph_children.push_back(c);
            }
            s.ReadUnsignedShort();       // equals firstLo - 1 in live data (default value?)
            return s.offset() == end;
        }
        case 206: {                                  // attached light-like records
            int len = s.ReadUnsignedShort();
            int end = s.offset() + len;
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) {            // 37 bytes each:
                s.ReadUnsignedByte();                //   u8
                s.ReadInt(); s.ReadInt(); s.ReadInt(); //  f32 offset x/y/z
                s.ReadUnsignedByte();                //   u8 (1..7 in live data)
                s.ReadInt();                         //   f32 (radius/strength?)
                s.ReadInt();                         //   u32 rgba colour
                s.ReadUnsignedByte();                //   u8
                s.ReadUnsignedShort();               //   u16
                s.ReadInt(); s.ReadInt(); s.ReadInt(); //  f32 x3 (1.0 / 0.0 / 1.0 typical)
            }
            return s.offset() == end;
        }
        case 249: {                                  // client-script params
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) {
                bool is_str = s.ReadUnsignedByte() == 1;
                s.Read24BitInt();
                if (is_str) (void)s.ReadString();
                else        s.ReadInt();
            }
            return true;
        }
        default:
            if (op >= 30 && op <= 34)   { d.options[op - 30] = s.ReadString();         return true; }
            if (op >= 136 && op <= 140) { s.ReadUnsignedByte();                        return true; }
            if (op >= 150 && op <= 154) { d.members_options[op - 150] = s.ReadString(); return true; }
            if (op >= 190 && op <= 195) { s.ReadUnsignedShort();                       return true; }  // action cursors
            return false;
    }
}

}  // namespace

LocDef DecodeLoc(int id, std::vector<std::uint8_t> file_bytes, int* stop_op) {
    LocDef d;
    d.id = id;
    if (stop_op) *stop_op = 0;
    if (file_bytes.empty()) return d;
    InputStream s(std::move(file_bytes));
    for (;;) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if (!ReadOne(s, d, op)) { if (stop_op) *stop_op = op; break; }
    }
    return d;
}

}  // namespace rtx::cache
