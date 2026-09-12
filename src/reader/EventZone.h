// Zone-relative server packets, decoded from the 950-1 handlers (docs/fieldmap-950-1.md "Zone
// packets"). The server first sets a zone base (0x60, or the header of a 0x31 zone update, or
// 0x02 which also clears the zone), then sends items whose position byte is (localX << 4 | localY)
// inside that 8x8 zone. The base is given as an offset in zones from the loaded map's origin, which
// the client keeps at [MainData+0x19898]+0x698 (x) / +0x69C (y); EventsJson reads it before decoding.
// Field names marked "raw" below carry the byte the client reads without a meaning that the
// decompilation could pin; everything else is what the handler does with the byte.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace rtx::evzone {

struct Zone { int plane = 0, x = 0, y = 0; bool set = false; };
inline Zone g_zone;                 // last zone base seen in the record stream (records decode in order)
inline int g_mapBaseX = 0, g_mapBaseY = 0;   // [MainData+0x19898]+0x698 / +0x69C, refreshed per EventsJson call

inline std::uint16_t u16be(const std::uint8_t* b) { return (std::uint16_t)((b[0] << 8) | b[1]); }
inline std::uint32_t u32be(const std::uint8_t* b) { return ((std::uint32_t)b[0] << 24) | ((std::uint32_t)b[1] << 16) | ((std::uint32_t)b[2] << 8) | b[3]; }

// World tile of a zone-local position byte on the current zone. false when no zone base was seen yet.
inline bool tile(std::uint8_t pos, int& wx, int& wy, int& plane) {
    if (!g_zone.set) return false;
    wx = g_zone.x + ((pos >> 4) & 7); wy = g_zone.y + (pos & 7); plane = g_zone.plane; return true;
}
inline void tileJson(std::string& o, std::uint8_t pos) {
    int wx, wy, pl; char t[96];
    if (tile(pos, wx, wy, pl)) std::snprintf(t, sizeof(t), "\"x\":%d,\"y\":%d,\"plane\":%d", wx, wy, pl);
    else std::snprintf(t, sizeof(t), "\"x\":null,\"y\":null,\"plane\":null,\"pos\":%u", pos);
    o += t;
}
// Packed world coordinate: plane bits 28-29, x bits 14-27, y bits 0-13; 0xFFFFFFFF = none.
inline void packedJson(std::string& o, std::uint32_t v) {
    char t[96];
    if (v == 0xFFFFFFFFu) std::snprintf(t, sizeof(t), "\"x\":null,\"y\":null,\"plane\":null");
    else std::snprintf(t, sizeof(t), "\"x\":%u,\"y\":%u,\"plane\":%u", (v >> 14) & 0x3FFF, v & 0x3FFF, (v >> 28) & 3);
    o += t;
}
// Entity reference used by the on-actor graphic packets: player index below 0x20000000, NPC
// index below 0x40000000 (both & 0xFFFF into the registries at MainData+0x19950 / +0x19930),
// otherwise a packed world tile.
inline void refJson(std::string& o, std::uint32_t v) {
    char t[96];
    if (v < 0x20000000u) std::snprintf(t, sizeof(t), "\"target\":\"player\",\"index\":%u", v & 0xFFFF);
    else if (v < 0x40000000u) std::snprintf(t, sizeof(t), "\"target\":\"npc\",\"index\":%u", v & 0xFFFF);
    else { o += "\"target\":\"tile\","; packedJson(o, v); return; }
    o += t;
}

// Zone sub-packet ids (the table at exe+0xD96B08) and the top-level opcodes that carry the same body.
// The table pointer at exe+0xD96B08 points at exe+0xD96B30; entry k is sub id k (descriptor: id, length, handler).
enum Sub { kObjAdd = 0x00, kAreaSound2 = 0x01, kSpotAnim2 = 0x02, kSub03 = 0x03, kAreaSound = 0x04,
           kProjectile = 0x05, kObjCount = 0x06, kLocVar1 = 0x07, kLocVar2 = 0x08, kObjAddOwned = 0x09,
           kObjDel = 0x0A, kSpotAnim = 0x0B, kLocDel = 0x0C, kLocAdd = 0x0D, kSub0E = 0x0E,
           kProjectile20 = 0x0F, kProjectile28 = 0x10, kProjectile29 = 0x11 };
inline int subLen(int sub) {
    switch (sub) { case 0x00: return 6; case 0x01: return 11; case 0x02: return 14; case 0x03: return 5; case 0x04: return 10;
                   case 0x05: return 21; case 0x06: return 8; case 0x09: return 8; case 0x0A: return 4; case 0x0B: return 11; case 0x0C: return 2;
                   case 0x0D: return 7; case 0x0F: return 20; case 0x10: return 28; case 0x11: return 29;
                   default: return -1; }
}

// Decode one zone item body (standalone opcode or 0x31 sub-packet). Emits `"kind":...,fields` without
// braces. Returns false when the body is unknown or short.
inline bool subJson(std::string& o, int sub, const std::uint8_t* b, std::uint32_t n) {
    char t[200];
    const int need = subLen(sub);
    if (need < 0 || (int)n < need) return false;
    switch (sub) {
    case kObjAdd: {          // FUN_140140c60: item id 24-bit (b0 low), position 0x80-b3, quantity u16 BE
        const int id = (b[2] << 16) | (b[1] << 8) | b[0];
        o += "\"kind\":\"obj_add\","; tileJson(o, (std::uint8_t)(0x80 - b[3]));
        std::snprintf(t, sizeof(t), ",\"item\":%d,\"qty\":%u", id, u16be(b + 4)); o += t; return true;
    }
    case kObjAddOwned: {     // FUN_140140a10: id 24-bit (b1 hi, b0 mid, b2 low), owner player index u16 LE, qty u16 BE, position b7
        const int id = (b[1] << 16) | (b[0] << 8) | b[2];
        o += "\"kind\":\"obj_add\","; tileJson(o, b[7]);
        std::snprintf(t, sizeof(t), ",\"item\":%d,\"qty\":%u,\"owner\":%u", id, u16be(b + 5), (unsigned)(b[3] | (b[4] << 8))); o += t; return true;
    }
    case kObjDel: {          // FUN_140140b90: position 0x80-b0, id 24-bit (b3 hi, b2 mid, b1 low)
        const int id = (b[3] << 16) | (b[2] << 8) | b[1];
        o += "\"kind\":\"obj_del\","; tileJson(o, (std::uint8_t)(0x80 - b[0]));
        std::snprintf(t, sizeof(t), ",\"item\":%d", id); o += t; return true;
    }
    case kObjCount: {        // FUN_1401409d0: position b0, id 24-bit BE, old qty u16 BE, new qty u16 BE (matched against the tile's stack)
        const int id = (b[1] << 16) | (b[2] << 8) | b[3];
        o += "\"kind\":\"obj_count\","; tileJson(o, b[0]);
        std::snprintf(t, sizeof(t), ",\"item\":%d,\"qty\":%u,\"from\":%u", id, u16be(b + 6), u16be(b + 4)); o += t; return true;
    }
    case kSpotAnim:
    case kSpotAnim2: {       // FUN_140114e00 / FUN_140116250: position b0, graphic u16 BE (0xFFFF removes), height i16 BE, delay u16 BE (bit 15 flag), rotation b7; 14-byte form adds a 24-bit offset pair
        const int gfx = u16be(b + 1) == 0xFFFF ? -1 : u16be(b + 1);
        o += "\"kind\":\"spotanim\","; tileJson(o, b[0]);
        std::snprintf(t, sizeof(t), ",\"gfx\":%d,\"height\":%d,\"delay\":%u,\"flag\":%u,\"rot\":%u", gfx, (std::int16_t)u16be(b + 3), u16be(b + 5) & 0x7FFF, u16be(b + 5) >> 15, b[7] & 7); o += t;
        if (sub == kSpotAnim2) {
            const std::uint32_t v = ((std::uint32_t)b[11] << 16) | ((std::uint32_t)b[12] << 8) | b[13];
            std::snprintf(t, sizeof(t), ",\"ox\":%d,\"oy\":%d,\"oflag\":%u", (int)(v & 0x7FF) - 0x3FF, (int)((v >> 11) & 0x7FF) - 0x3FF, (v & 0xFFC00000u) == 0x400000u ? 1u : 0u); o += t;
        }
        return true;
    }
    case kProjectile: {      // FUN_140140e00 -> FUN_140127940: flags b1, two 24-bit values (b4 b5 b6 and b8 b7 b9), graphic u16 BE at 10 (0xFFFF = none), heights b12 (x4, or x1 when flag bit 1) and b13 (x16), two u16 BE at 14 and 16; bytes 0, 2, 3, 18-20 consumed unread
        const unsigned flags = b[1];
        const std::uint32_t a = ((std::uint32_t)b[4] << 16) | ((std::uint32_t)b[5] << 8) | b[6];
        const std::uint32_t c = ((std::uint32_t)b[8] << 16) | ((std::uint32_t)b[7] << 8) | b[9];
        const int gfx = u16be(b + 10) == 0xFFFF ? -1 : u16be(b + 10);
        const unsigned h0 = (flags & 2) ? (unsigned)b[12] * 4u : (unsigned)b[12] * 16u;
        std::snprintf(t, sizeof(t), "\"kind\":\"projectile\",\"gfx\":%d,\"flags\":%u,\"a\":%u,\"b\":%u,\"h0\":%u,\"h1\":%u,\"t0\":%u,\"t1\":%u,\"raw\":\"%02x%02x%02x%02x%02x%02x\"",
                      gfx, flags, a, c, h0, (unsigned)b[13] * 16u, u16be(b + 14), u16be(b + 16), b[0], b[2], b[3], b[18], b[19], b[20]);
        o += t;
        if (g_zone.set) { std::snprintf(t, sizeof(t), ",\"zx\":%d,\"zy\":%d,\"zplane\":%d", g_zone.x, g_zone.y, g_zone.plane); o += t; }
        return true;
    }
    case kAreaSound:
    case kAreaSound2: {      // FUN_140140990 / FUN_140140950: position b0, sound id u32 BE, b5 (loops & 7, radius >> 4), b6 (raw), b7 (raw), u16 BE at 8; the 11-byte form adds b10 (mode)
        o += "\"kind\":\"area_sound\","; tileJson(o, b[0]);
        std::snprintf(t, sizeof(t), ",\"id\":%u,\"loops\":%u,\"radius\":%u,\"a\":%u,\"b\":%u,\"c\":%u", u32be(b + 1), b[5] & 7, b[5] >> 4, b[6], b[7], u16be(b + 8)); o += t;
        if (sub == kAreaSound2) { std::snprintf(t, sizeof(t), ",\"mode\":%u", b[10]); o += t; }
        return true;
    }
    case kLocAdd: {          // FUN_140140e80: -b0: type bits 2-6, rotation bits 0-1, bit 7 = extra data follows; id = b3 b4 b1 b2; b5 raw; position -b6
        const std::uint8_t k = (std::uint8_t)(0 - b[0]);
        const std::uint32_t id = ((std::uint32_t)b[3] << 24) | ((std::uint32_t)b[4] << 16) | ((std::uint32_t)b[1] << 8) | b[2];
        o += "\"kind\":\"loc_add\","; tileJson(o, (std::uint8_t)(0 - b[6]));
        std::snprintf(t, sizeof(t), ",\"loc\":%u,\"type\":%u,\"rot\":%u,\"extra\":%u,\"a\":%u", id, (k >> 2) & 0x1F, k & 3, k >> 7, (unsigned)(std::uint8_t)(0 - b[5])); o += t; return true;
    }
    case kProjectile29: {    // real handler 0x140115E70 (instruction listing): position b0, flags b1, i8 b2, i8 b3, 24-bit offset pairs at 4 and 7 (11-bit x/y, flag), graphic u16 BE at 10, start height i16 BE at 12 (x4), end height i16 BE at 14 (x4), start u16 at 16, end u16 at 18, b20, u16 at 21 (x4), 24-bit at 23 and 26 (passed shifted left 8; meaning not pinned)
        const auto off = [](const std::uint8_t* q) { const std::uint32_t v = ((std::uint32_t)q[0] << 16) | ((std::uint32_t)q[1] << 8) | q[2]; return std::make_pair((int)(v & 0x7FF) - 0x3FF, (int)((v >> 11) & 0x7FF) - 0x3FF); };
        const auto oa = off(b + 4), ob = off(b + 7);
        const int gfx = u16be(b + 10) == 0xFFFF ? -1 : u16be(b + 10);
        const std::uint32_t e1 = ((std::uint32_t)b[23] << 16) | ((std::uint32_t)b[24] << 8) | b[25], e2 = ((std::uint32_t)b[26] << 16) | ((std::uint32_t)b[27] << 8) | b[28];
        o += "\"kind\":\"projectile\",\"form\":29,"; tileJson(o, b[0]);
        std::snprintf(t, sizeof(t), ",\"gfx\":%d,\"flags\":%u,\"c2\":%d,\"c3\":%d,\"ax\":%d,\"ay\":%d,\"bx\":%d,\"by\":%d,\"h0\":%d,\"h1\":%d,\"t0\":%u,\"t1\":%u,\"r20\":%u,\"r21\":%u,\"e1\":%u,\"e2\":%u",
                      gfx, b[1], (int)(std::int8_t)b[2], (int)(std::int8_t)b[3], oa.first, oa.second, ob.first, ob.second, (int)(std::int16_t)u16be(b + 12) * 4, (int)(std::int16_t)u16be(b + 14) * 4, u16be(b + 16), u16be(b + 18), b[20], u16be(b + 21), e1, e2);
        o += t; return true;
    }
    case kProjectile20:
    case kProjectile28: {    // FUN_1401147E0 / FUN_140115AD0 (thunks 0x140140E40 / 0x140140740): graphic u16 BE at 6; the rest is kept raw until seen live
        const int gfx = u16be(b + 6) == 0xFFFF ? -1 : u16be(b + 6);
        std::snprintf(t, sizeof(t), "\"kind\":\"projectile\",\"form\":%d,\"gfx\":%d,\"hex\":\"", need, gfx); o += t;
        for (int i = 0; i < need; ++i) { std::snprintf(t, sizeof(t), "%02x", b[i]); o += t; }
        o += "\"";
        if (g_zone.set) { std::snprintf(t, sizeof(t), ",\"zx\":%d,\"zy\":%d,\"zplane\":%d", g_zone.x, g_zone.y, g_zone.plane); o += t; }
        return true;
    }
    case kLocDel: {          // FUN_140140ec0: position 0x80-b0, then b1+0x80: type bits 2-6, rotation bits 0-1
        const std::uint8_t k = (std::uint8_t)(b[1] + 0x80);
        o += "\"kind\":\"loc_del\","; tileJson(o, (std::uint8_t)(0x80 - b[0]));
        std::snprintf(t, sizeof(t), ",\"type\":%u,\"rot\":%u", (k >> 2) & 0x1F, k & 3); o += t; return true;
    }
    default: return false;
    }
}

// Top-level opcodes that carry a zone item body: opcode -> sub id (0 = none).
inline int subForOpcode(int op) {   // -1 = not a zone item opcode
    switch (op) { case 0x33: return kObjAdd; case 0x6D: return kObjDel; case 0x46: return kObjCount; case 0x0E: return kSpotAnim;
                  case 0xBC: return kSpotAnim2; case 0x9A: return kProjectile; case 0xA4: return kAreaSound; case 0x1A: return kLocDel;
                  case 0x4B: return kLocAdd; case 0x72: return kProjectile20; case 0xA9: return kProjectile28; case 0xC4: return kProjectile29; default: return -1; }
}

// Standalone packets with their own layouts.
inline bool topJson(std::string& o, int op, const std::uint8_t* b, std::uint32_t n) {
    char t[220];
    switch (op) {
    case 0x60: {   // FUN_140141120: zone base. y = mapBase + b0*8, x = mapBase - b1*8, plane = b2 + 0x80
        if (n < 3) return false;
        g_zone.y = g_mapBaseY + (int)(std::int8_t)b[0] * 8; g_zone.x = g_mapBaseX - (int)(std::int8_t)b[1] * 8; g_zone.plane = (b[2] + 0x80) & 0xFF; g_zone.set = true;
        std::snprintf(t, sizeof(t), "\"kind\":\"zone_base\",\"x\":%d,\"y\":%d,\"plane\":%d", g_zone.x, g_zone.y, g_zone.plane); o += t; return true;
    }
    case 0x02: {   // FUN_1401410e0: zone clear (drops the zone's ground items). plane = b0 + 0x80, x = mapBase + b1*8, y = mapBase + b2*8
        if (n < 3) return false;
        g_zone.plane = (b[0] + 0x80) & 0xFF; g_zone.x = g_mapBaseX + (int)(std::int8_t)b[1] * 8; g_zone.y = g_mapBaseY + (int)(std::int8_t)b[2] * 8; g_zone.set = true;
        std::snprintf(t, sizeof(t), "\"kind\":\"zone_clear\",\"x\":%d,\"y\":%d,\"plane\":%d", g_zone.x, g_zone.y, g_zone.plane); o += t; return true;
    }
    case 0x31: {   // FUN_140140f80: zone update. plane = 0x80 - b0, x = mapBase - b1*8, y = mapBase + b2*8, then [sub id][body]... from the 18-entry table
        if (n < 3) return false;
        g_zone.plane = (0x80 - b[0]) & 0xFF; g_zone.x = g_mapBaseX - (int)(std::int8_t)b[1] * 8; g_zone.y = g_mapBaseY + (int)(std::int8_t)b[2] * 8; g_zone.set = true;
        std::snprintf(t, sizeof(t), "\"kind\":\"zone_update\",\"x\":%d,\"y\":%d,\"plane\":%d,\"items\":[", g_zone.x, g_zone.y, g_zone.plane); o += t;
        std::uint32_t p = 3; bool first = true, partial = false;
        while (p < n) {
            const int sub = b[p++]; const int len = subLen(sub);
            if (len < 0 || p + (std::uint32_t)len > n) { partial = true; break; }   // variable-length loc items end the walk
            o += first ? "{" : ",{"; first = false;
            if (!subJson(o, sub, b + p, (std::uint32_t)len)) { std::snprintf(t, sizeof(t), "\"kind\":\"zone_sub\",\"sub\":%d,\"hex\":\"", sub); o += t; for (int i = 0; i < len; ++i) { std::snprintf(t, sizeof(t), "%02x", b[p + i]); o += t; } o += "\""; }
            o += "}"; p += (std::uint32_t)len;
        }
        o += "],\"partial\":"; o += partial ? "true" : "false"; return true;
    }
    case 0x75: {   // FUN_14010b2c0: graphic on an entity or tile. height i16 (b0-0x80 low, b1 high), delay u16 (b3 high, b2 low; bit 15 flag), -b4: rotation & 7, bit 7 flag, ref u32 BE at 5, graphic (b9-0x80 low, b10 high; 0xFFFF removes), slot b11-0x80
        if (n < 12) return false;
        const int height = (std::int16_t)(((b[0] - 0x80) & 0xFF) | (b[1] << 8));
        const unsigned d = (unsigned)((b[3] << 8) | b[2]);
        const std::uint8_t r = (std::uint8_t)(0 - b[4]);
        const unsigned g = (unsigned)(((b[9] - 0x80) & 0xFF) | (b[10] << 8));
        o += "\"kind\":\"spotanim_actor\","; refJson(o, u32be(b + 5));
        std::snprintf(t, sizeof(t), ",\"gfx\":%d,\"height\":%d,\"delay\":%u,\"flag\":%u,\"rot\":%u,\"rflag\":%u,\"slot\":%d", g == 0xFFFF ? -1 : (int)g, height, d & 0x7FFF, d >> 15, r & 7, r >> 7, (int)(std::uint8_t)(b[11] - 0x80)); o += t; return true;
    }
    case 0xC5: {   // FUN_14010ae50: graphic on an entity or tile with offsets. ref = b2 b3 b0 b1; b4: rotation & 7, bit 7 flag; offsets 24-bit (b6 b5 b7); slot b8+0x80; height (b10-0x80 low, b9 high); graphic (b11-0x80 low, b12 high); delay (b13-0x80 low, b14 high; bit 15 flag)
        if (n < 15) return false;
        const std::uint32_t ref = ((std::uint32_t)b[2] << 24) | ((std::uint32_t)b[3] << 16) | ((std::uint32_t)b[0] << 8) | b[1];
        const std::uint32_t v = ((std::uint32_t)b[6] << 16) | ((std::uint32_t)b[5] << 8) | b[7];
        const int height = (std::int16_t)(((b[10] - 0x80) & 0xFF) | (b[9] << 8));
        const unsigned g = (unsigned)(((b[11] - 0x80) & 0xFF) | (b[12] << 8));
        const unsigned d = (unsigned)(((b[13] - 0x80) & 0xFF) | (b[14] << 8));
        o += "\"kind\":\"spotanim_actor\","; refJson(o, ref);
        std::snprintf(t, sizeof(t), ",\"gfx\":%d,\"height\":%d,\"delay\":%u,\"flag\":%u,\"rot\":%u,\"rflag\":%u,\"slot\":%d,\"ox\":%d,\"oy\":%d,\"oflag\":%u",
                      g == 0xFFFF ? -1 : (int)g, height, d & 0x7FFF, d >> 15, b[4] & 7, b[4] >> 7, (int)(std::uint8_t)(b[8] + 0x80), (int)(v & 0x7FF) - 0x3FF, (int)((v >> 11) & 0x7FF) - 0x3FF, (v & 0xFFC00000u) == 0x400000u ? 1u : 0u); o += t; return true;
    }
    case 0x2C: {   // FUN_14010ac80: sound. id u32 BE, b4 raw, u16 BE at 5 (stored at +0xB4 when nonzero: a countdown), b7 raw
        if (n < 8) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"sound\",\"id\":%u,\"a\":%u,\"b\":%u,\"c\":%u", u32be(b), b[4], u16be(b + 5), b[7]); o += t; return true;
    }
    case 0x5F: {   // FUN_14010a560: sound at a world tile. id = b2 b1 b3 b0, b4 raw, packed tile = b7 b8 b5 b6, volume = -128 - (int8)b9, b10 raw
        if (n < 11) return false;
        const std::uint32_t id = ((std::uint32_t)b[2] << 24) | ((std::uint32_t)b[1] << 16) | ((std::uint32_t)b[3] << 8) | b[0];
        const std::uint32_t pk = ((std::uint32_t)b[7] << 24) | ((std::uint32_t)b[8] << 16) | ((std::uint32_t)b[5] << 8) | b[6];
        o += "\"kind\":\"area_sound\","; packedJson(o, pk);
        std::snprintf(t, sizeof(t), ",\"id\":%u,\"a\":%u,\"volume\":%d,\"b\":%u", id, b[4], (int)(-128 - (int)(std::int8_t)b[9]), b[10]); o += t; return true;
    }
    default: return false;
    }
}

}  // namespace rtx::evzone
