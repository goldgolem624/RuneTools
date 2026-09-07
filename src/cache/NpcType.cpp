#include "NpcType.h"
#include "Probe.h"

namespace rtx::cache {

namespace {

// Opcode dispatcher for the RS3 NPCType decoder. Returns false
// on an unknown opcode (terminates the loop -- a misaligned stream past an
// unknown opcode is garbage anyway). Only name/options/combat/transforms are
// retained; the rest are consumed to keep the stream aligned.
bool ReadOne(InputStream& s, NpcDef& d, int op) {
    switch (op) {
        case 1: {                                   // model ids
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadBigSmart();
            return true;
        }
        case 2: d.name = s.ReadString();            return true;
        case 12: d.size = s.ReadUnsignedByte();     return true;   // size (tile footprint)
        case 40: case 41: {                          // recolour / retexture
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) { s.ReadShort(); s.ReadShort(); }
            return true;
        }
        case 42: {                                   // recolour palette
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadUnsignedByte();
            return true;
        }
        case 44: case 45: s.skip(2);                return true;
        case 60: {                                   // head models
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadBigSmart();
            return true;
        }
        case 39: s.ReadUnsignedByte();              return true;
        case 93:                                    return true;
        case 95: d.combat_level = s.ReadUnsignedShort(); return true;
        case 97: case 98: s.ReadShort();            return true;   // resize x/y
        case 99:                                    return true;
        case 100: case 101: case 102: s.ReadUnsignedByte(); return true;
        case 103: s.ReadShort();                    return true;
        case 106: case 118: {                        // varbit/varp transform
            // varbit/varp are u16 ids -- read UNSIGNED so large ids (e.g. the 40000+ range) aren't
            // sign-truncated to a negative "no selector"; 0xFFFF = none.
            d.varbit = s.ReadUnsignedShort(); if (d.varbit == 0xFFFF) d.varbit = -1;
            d.varp   = s.ReadUnsignedShort(); if (d.varp   == 0xFFFF) d.varp   = -1;
            int def  = (op == 118) ? s.ReadShort() : -1;
            int n    = s.ReadUnsignedSmart();
            for (int i = 0; i < n + 2; ++i) {
                if (i == n + 1) d.transform_to.push_back(def);
                else            d.transform_to.push_back(s.ReadShort());
            }
            return true;
        }
        case 107: case 109: case 111:               return true;
        case 113: s.ReadShort(); s.ReadShort();     return true;
        case 114: s.ReadUnsignedByte(); s.ReadUnsignedByte(); return true;
        case 119: s.ReadUnsignedByte();             return true;
        case 121: {                                  // model translation
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.skip(4);
            return true;
        }
        case 123: s.ReadShort();                    return true;   // height
        case 125: s.ReadUnsignedByte();             return true;
        case 127: s.ReadShort();                    return true;   // bas id
        case 128: s.ReadUnsignedByte();             return true;
        case 134: s.ReadShort(); s.ReadShort(); s.ReadShort();
                  s.ReadShort(); s.ReadUnsignedByte(); return true;
        case 135: case 136: s.ReadUnsignedByte(); s.ReadShort(); return true;
        case 137: s.ReadShort();                    return true;   // attack cursor
        case 138: case 139: s.ReadBigSmart();       return true;
        case 140: s.ReadUnsignedByte();             return true;
        case 141: case 143:                         return true;
        case 142: s.ReadShort();                    return true;
        case 155: s.skip(4);                        return true;
        case 158: case 159: case 162: case 169:     return true;
        case 160: {                                  // quests
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadShort();
            return true;
        }
        case 163: s.ReadUnsignedByte();             return true;
        case 164: s.ReadShort(); s.ReadShort();     return true;
        case 165: case 168: s.ReadUnsignedByte();   return true;
        case 178: case 182: case 185:               return true;
        case 179: for (int i = 0; i < 6; ++i) s.ReadUnsignedSmart(); return true;
        case 180: s.ReadUnsignedByte();             return true;
        case 181: s.skip(3);                        return true;
        case 183: case 184: s.skip(1);              return true;
        case 186: {                                  // extended morph block
            s.ReadShort();
            d.varbit = s.ReadShort() & 0xFFFF; if (d.varbit == 0xFFFF) d.varbit = -1;
            d.varp   = s.ReadShort() & 0xFFFF; if (d.varp   == 0xFFFF) d.varp   = -1;
            int flags = s.ReadUnsignedByte();
            if (flags & 1) {
                int l1 = s.ReadUnsignedByte();
                for (int i = 0; i < l1; ++i) {
                    s.ReadUnsignedByte();
                    int l2 = s.ReadUnsignedByte();
                    for (int j = 0; j < l2; ++j) {
                        s.ReadShort(); s.ReadShort(); s.ReadBigSmart();
                        int n = s.ReadUnsignedByte();
                        for (int kk = 0; kk < n && kk < 3; ++kk) s.ReadUnsignedByte();
                    }
                }
            }
            if (flags & 2) {
                int l1 = s.ReadUnsignedByte();
                for (int i = 0; i < l1; ++i) {
                    s.ReadUnsignedByte();
                    int l2 = s.ReadUnsignedByte();
                    for (int j = 0; j < l2; ++j) { s.ReadShort(); s.ReadShort(); s.ReadBigSmart(); }
                }
            }
            if (flags & 4) {
                int l1 = s.ReadUnsignedByte();
                for (int i = 0; i < l1; ++i) {
                    s.ReadUnsignedByte();
                    int l2 = s.ReadUnsignedByte();
                    for (int j = 0; j < l2; ++j) { s.ReadShort(); s.ReadShort(); s.ReadShort(); s.ReadShort(); }
                }
            }
            if (flags & 8) {
                int l1 = s.ReadUnsignedByte();
                for (int i = 0; i < l1; ++i) {
                    s.ReadUnsignedByte();
                    int l2 = s.ReadUnsignedByte();
                    for (int j = 0; j < l2; ++j) { s.ReadShort(); s.ReadShort(); s.ReadShort(); s.ReadShort(); }
                }
            }
            if (flags & 16) {
                int l1 = s.ReadUnsignedByte();
                for (int i = 0; i < l1; ++i) {
                    s.ReadUnsignedByte(); s.ReadShort(); s.ReadShort();
                    s.ReadUnsignedByte(); s.ReadUnsignedByte();
                    s.ReadUnsignedByte(); s.ReadUnsignedByte();
                }
            }
            int defTransform = s.ReadShort() & 0xFFFF; if (defTransform == 0xFFFF) defTransform = -1;
            if (d.transform_to.empty()) d.transform_to.push_back(defTransform);
            return true;
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
        case 252: s.ReadShort();                    return true;
        case 253: s.skip(1);                        return true;
        // ---- build 950-1 additions (payloads recovered with the unknown-opcode probe, Probe.h) ----
        case 92: {                                   // u8 x3, string, u8, u8 n, n x bigsmart (11 defs, "Trainee adventurer")
            s.ReadUnsignedByte(); s.ReadUnsignedByte(); s.ReadUnsignedByte();
            (void)s.ReadString(); s.ReadUnsignedByte();
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n; ++i) s.ReadBigSmart();
            return true;
        }
        case 187: {                                  // u8, u16, u16, usmart n, n x bigsmart, bigsmart (same shape as loc 207)
            s.ReadUnsignedByte(); s.ReadUnsignedShort(); s.ReadUnsignedShort();
            int n = s.ReadUnsignedSmart();
            for (int i = 0; i < n; ++i) s.ReadBigSmart();
            s.ReadBigSmart();
            return true;
        }
        case 188: {                                  // u8, u16, u16, bigsmart, usmart n, n x bigsmart, bigsmart (same shape as loc 208)
            s.ReadUnsignedByte(); s.ReadUnsignedShort(); s.ReadUnsignedShort(); s.ReadBigSmart();
            int n = s.ReadUnsignedSmart();
            for (int i = 0; i < n; ++i) s.ReadBigSmart();
            s.ReadBigSmart();
            return true;
        }
        case 189: {                                  // 950-1. Live shapes (191 defs, see docs/cs2_opcodes.md):
            //   u16, u8, u16, u16 (header), u8 mask, then 10-byte sub-records S = {u8 slot, u8 01, u16 0003,
            //   u16 0003, i32 value} with 1-byte glue between them (00 = spacer, 01..03 = a group count),
            //   then a 1-byte tail (02 in every def). The glue/mask semantics were not pinned down
            //   (multi-sub defs contradict every simple count or bitmask reading), so this consumes by
            //   SIGNATURE: a byte that starts an S is an S; otherwise 00 is skipped, a byte followed by an
            //   S is a group count (up to that many contiguous S), and anything else is the tail and ends
            //   the payload. Matches all 191 defs exactly.
            s.ReadUnsignedShort(); s.ReadUnsignedByte(); s.ReadUnsignedShort(); s.ReadUnsignedShort();
            s.ReadUnsignedByte();                                    // mask
            auto subAt = [&](int o) {
                return s.remaining() >= (o - s.offset()) + 10 &&
                       s.peek(o + 1) == 0x01 && s.peek(o + 2) == 0x00 && s.peek(o + 3) == 0x03 &&
                       s.peek(o + 4) == 0x00 && s.peek(o + 5) == 0x03;
            };
            for (int guard = 0; guard < 96 && s.remaining() > 0; ++guard) {
                if (subAt(s.offset())) { s.skip(10); continue; }   // a sub-record starts here
                int c = s.ReadUnsignedByte();                        // glue
                if (c == 0) continue;                                // spacer
                if (!subAt(s.offset())) break;                       // tail consumed -> done
                for (int i = 0; i < c && subAt(s.offset()); ++i) s.skip(10);
            }
            return true;
        }
        default:
            if (op >= 30 && op <= 34) { d.options[op - 30] = s.ReadString(); return true; }
            if (op >= 150 && op <= 154) { d.members_options[op - 150] = s.ReadString(); return true; }
            if (op >= 170 && op <= 175) { s.ReadShort();                     return true; }  // action cursors
            if (op == probe::g_op && probe::g_len <= s.remaining()) { s.skip(probe::g_len); return true; }   // unknown-opcode probe (Probe.h)
            return false;
    }
}

}  // namespace

NpcDef DecodeNpc(int id, std::vector<std::uint8_t> file_bytes, int* stop_op) {
    NpcDef d;
    d.id = id;
    if (stop_op) *stop_op = 0;
    if (file_bytes.empty()) return d;
    InputStream s(std::move(file_bytes));
    for (;;) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if (!ReadOne(s, d, op)) { if (stop_op) *stop_op = op; probe::g_stop = s.offset(); break; }
    }
    probe::g_tail = s.remaining();
    return d;
}

}  // namespace rtx::cache
