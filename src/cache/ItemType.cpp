#include "ItemType.h"

namespace rtx::cache {

namespace {

// Opcode dispatcher for the RS3 ItemType definition decode. We only
// keep a handful of fields (name + a few cheap extras for future use);
// every other opcode is consumed to its correct byte width so the stream
// stays aligned. Unknown opcodes terminate the loop -- a misaligned
// stream beyond an unknown opcode produces garbage anyway.
bool ReadOne(InputStream& s, ItemDef& d, int opcode) {
    switch (opcode) {
        case 1:  d.inv_model_id = s.ReadBigSmart();           return true;
        case 2:  d.name         = s.ReadString();             return true;
        case 3:  /* tooltip   */ (void)s.ReadString();        return true;
        case 4: case 5: case 6: case 7: case 8: s.ReadUnsignedShort(); return true;
        case 10: s.ReadUnsignedShort();                       return true;
        case 11: d.stackable = true;                          return true;
        case 12: s.ReadInt();                                 return true;
        case 9: {
            // count x BigSmart (model-id-range values). Misread as a payload-less flag
            // until build ~950: with count 1 the stream stayed aligned by aliasing the
            // list as an op-1 read (which silently overwrote inv_model_id -- unconsumed,
            // so harmless), but counts 2/4 in newer files misparse. Full-index
            // validated 62959/62959 with this shape.
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadBigSmart();
            return true;
        }
        case 13: case 14: s.ReadUnsignedByte();               return true;
        case 15: case 16:                                     return true;
        case 18: s.ReadUnsignedShort();                       return true;
        case 23: case 24: case 25: case 26: s.ReadBigSmart(); return true;
        case 27: s.ReadUnsignedByte();                        return true;
        case 30: case 31: case 32: case 33: case 34:
                 (void)s.ReadString();                        return true;
        case 35: case 36: case 37: case 38: case 39: {
            // Worn (equipped) right-click options. Augmented gear uniquely carries
            // a "Disassemble" worn option (to recover the augmentor); plain worn
            // gear has "Destroy"/"Drop" instead.
            std::string opt = s.ReadString();
            if (opt == "Disassemble") d.augmented = true;
            return true;
        }
        case 40: case 41: {
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) { s.ReadUnsignedShort(); s.ReadUnsignedShort(); }
            return true;
        }
        case 42: {
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) { s.ReadUnsignedByte(); s.ReadByte(); }
            return true;
        }
        case 43: s.ReadInt();                                 return true;
        case 44: case 45: s.ReadUnsignedShort();              return true;
        case 65: d.tradeable = true;                          return true;
        case 69: d.ge_limit = s.ReadInt();                    return true;
        case 78: case 79: case 90: case 91: case 92: case 93:
                 s.ReadBigSmart();                            return true;
        case 94: d.category = s.ReadUnsignedShort();          return true;
        case 95: s.ReadUnsignedShort();                       return true;
        case 96: s.ReadUnsignedByte();                        return true;
        case 97: d.noted_unnoted = s.ReadUnsignedShort();     return true;
        case 98: d.noted_template = s.ReadUnsignedShort(); d.noted = true; return true;
        case 100: case 101: case 102: case 103: case 104:
        case 105: case 106: case 107: case 108: case 109:
                 s.ReadUnsignedShort(); s.ReadUnsignedShort(); return true;
        case 110: case 111: case 112: s.ReadUnsignedShort();  return true;
        case 113: case 114: s.ReadByte();                     return true;
        case 115: s.ReadUnsignedByte();                       return true;
        case 121: case 122: s.ReadUnsignedShort();            return true;
        case 125: case 126: s.ReadByte(); s.ReadByte(); s.ReadByte(); return true;
        case 127: case 128: case 129: case 130: s.ReadUnsignedShort(); return true;
        case 132: {
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadUnsignedShort();
            return true;
        }
        case 134: s.ReadUnsignedByte();                       return true;
        case 139: case 140: s.ReadUnsignedShort();            return true;
        case 142: case 143: case 144: case 145: case 146:
        case 150: case 151: case 152: case 153: case 154:
                 s.ReadUnsignedShort();                       return true;
        case 161: case 162: case 163: s.ReadUnsignedShort();  return true;
        case 164: (void)s.ReadString();                       return true;
        case 157:                                             return true;
        case 165: d.stackable = true;                         return true;
        case 167: case 168: case 178:                         return true;
        case 181: d.value = s.ReadLong();                     return true;
        case 182: s.Read24BitInt();                           return true;   // item-id-sized link (build ~950)
        // Build 949 widened every item-id link field to u24 (ids passed 65535):
        // 190..199 supersede the u16,u16 pairs 100..109; 201/202 supersede the
        // note links 97/98; 203..208 supersede the remaining u16 links. Decoded
        // for alignment (note links kept -- the noted-icon compositing uses them).
        case 190: case 191: case 192: case 193: case 194:
        case 195: case 196: case 197: case 198: case 199:
                 s.Read24BitInt(); s.ReadUnsignedShort();     return true;
        case 201: d.noted_unnoted = s.Read24BitInt();         return true;
        case 202: d.noted_template = s.Read24BitInt(); d.noted = true; return true;
        case 203: case 204: case 205: case 206: case 207: case 208:
                 s.Read24BitInt();                            return true;
        case 242: case 243: case 244: case 245: case 246: case 247: case 248:
                 s.ReadBigSmart();                            return true;
        case 249: {
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) {
                bool is_string = s.ReadUnsignedByte() == 1;
                int  key       = s.Read24BitInt();
                if (is_string) {
                    // The augmented destroy message ("...lose all the item's XP and
                    // gizmos") is a string param -- a reliable augmentation marker
                    // even when the item name has no "Augmented" prefix.
                    std::string v = s.ReadString();
                    if (v.find("gizmo") != std::string::npos) d.augmented = true;
                    d.params_s[key] = std::move(v);
                } else {
                    d.params_i[key] = s.ReadInt();
                }
            }
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

ItemDef DecodeItem(int id, std::vector<std::uint8_t> file_bytes, int* stop_op) {
    ItemDef def;
    def.id = id;
    if (stop_op) *stop_op = 0;
    if (file_bytes.empty()) return def;
    InputStream s(std::move(file_bytes));
    for (;;) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if (!ReadOne(s, def, op)) { if (stop_op) *stop_op = op; break; }
    }
    return def;
}

}  // namespace rtx::cache
