#pragma once
// Server -> client opcodes (re-pin per update: tools/rtx_offsets.py pkt); Expect.len = packet-table descriptor +0x04, -1 var-byte, -2 var-short.
#include <cstdint>

namespace rtx::sops {

inline constexpr int kMessageGame     = 0x21;   // 0x15 on 949   var-byte : [type smart][u32][flags][sender?][text]
inline constexpr int kSkillUpdate     = 0x5C;   // 0x04 on 949   6 bytes  : [skill -b0][level -b1][xp u32 BE]
inline constexpr int kContainerUpdate = 0x32;   // 0x2B on 949   var-short: [container u16 BE][flags u8] slots...
inline constexpr int kRunClientScript = 0x23;   // 0x52 on 949   var-short : [sig][args reversed][script i32]; live 2026-09-12: 278/304 decode
                                                //   (0x82 is NOT runclientscript: its handler reads a script id + one byte into the 5-slot table at MainData+0x19850)
inline constexpr int kGeOffer         = 0x54;   // 0x51 on 949   36 bytes (949 also had a 35-byte 0x05; no 950-1 counterpart)
inline constexpr int kRunEnergy       = 0x15;   // 0x5C on 949   1 byte   : [energy u8] -> skill block +0x18
inline constexpr int kRunWeight       = 0x07;   // 0x00 on 949   2 bytes  : [weight i16 BE] -> skill block +0x1C
inline constexpr int kPingEcho        = 0xBE;   // 0x8D on 949   8 bytes  : two u32 BE nonces, echoed back
inline constexpr int kServerTick      = 0xA0;   // 0xB4 on 949   0 bytes  : tick boundary (INC [MainData+0xDBF0])
// Var set packets, layouts confirmed live against the varp/varc stores on 2026-09-12 (docs/fieldmap-950-1.md):
inline constexpr int kVarpInt         = 0x04;   // 6 bytes  : id = ((b0-0x80)&0xFF)|(b1<<8); value = (b4<<24)|(b5<<16)|(b2<<8)|b3   (26/27 matched)
inline constexpr int kVarpByte        = 0x4F;   // 3 bytes  : value = i8 b0; id = ((b2-0x80)&0xFF)|(b1<<8)                            (10/14)
inline constexpr int kVarcInt         = 0x77;   // 6 bytes  : id = ((b1-0x80)&0xFF)|(b0<<8); value = (b3<<24)|(b2<<16)|(b5<<8)|b4     (Ghidra FUN_140141e90)
inline constexpr int kVarcByte        = 0x7E;   // 3 bytes  : id = b0|(b1<<8); value = (int8)(0x80-b2)                                 (Ghidra FUN_140141fc0)
inline constexpr int kVarbitVarint    = 0x74;   // var-byte : two LEB128 varints (7 bits per byte, low first, high bit = continue): varbit id, value (Ghidra FUN_1401420d0, config slot +0x230)
inline constexpr int kVarpLong        = 0xA5;   // 10 bytes : value = i64 hi=(b1<<24)|(b0<<16)|(b3<<8)|b2, lo=(b5<<24)|(b4<<16)|(b7<<8)|b6; id = (b8<<8)|b9 (Ghidra FUN_140142390; op from the live descriptor table, see tools/rtx_pkt_table.py)
// Zone packets (docs/fieldmap-950-1.md "Zone packets"): a zone base, then items positioned by a
// local (x << 4 | y) byte. Decoded in src/reader/EventZone.h.
inline constexpr int kZoneBase        = 0x60;   // 3 bytes  : y offset, -x offset (zones from the map base), plane+0x80
inline constexpr int kZoneClear       = 0x02;   // 3 bytes  : plane+0x80, x offset, y offset; drops the zone's ground items
inline constexpr int kZoneUpdate      = 0x31;   // var-short: zone base header then [sub id][body] items (table exe+0xD96B08)
inline constexpr int kObjAdd          = 0x33;   // 6 bytes  : ground item added (id 24-bit, position, quantity)
inline constexpr int kObjDel          = 0x6D;   // 4 bytes  : ground item removed
inline constexpr int kObjCount        = 0x46;   // 8 bytes  : ground item quantity changed (old, new)
inline constexpr int kLocAdd          = 0x4B;   // 7 bytes  : map object added or replaced (loc id, type, rotation)
inline constexpr int kLocDel          = 0x1A;   // 2 bytes  : map object removed
inline constexpr int kSpotAnim        = 0x0E;   // 11 bytes : graphic at a tile
inline constexpr int kSpotAnim2       = 0xBC;   // 14 bytes : graphic at a tile with offsets
inline constexpr int kSpotAnimActor   = 0x75;   // 12 bytes : graphic on a player, NPC or tile
inline constexpr int kSpotAnimActor2  = 0xC5;   // 15 bytes : graphic on a player, NPC or tile, with offsets
inline constexpr int kProjectile      = 0x9A;   // 21 bytes : projectile (manager at MainData+0x19960)
inline constexpr int kSound           = 0x2C;   // 8 bytes  : sound effect
inline constexpr int kAreaSound       = 0xA4;   // 10 bytes : sound at a zone tile
inline constexpr int kAreaSoundAbs    = 0x5F;   // 11 bytes : sound at a packed world tile
inline constexpr int kOpMax           = 0xDE;   // framer bound (`cmp eax,0xDE; ja`); 0xE5 on 949

struct Expect { int op; int len; const char* name; };
inline constexpr Expect kExpected[] = {
    { kMessageGame,     -1, "message_game"     },
    { kSkillUpdate,      6, "skill_update"     },
    { kContainerUpdate, -2, "container_update" },
    { kRunClientScript, -2, "runclientscript"  },
    { kGeOffer,         36, "ge_offer"         },
    { kRunEnergy,        1, "run_energy"       },
    { kRunWeight,        2, "run_weight"       },
    { kPingEcho,         8, "ping_echo"        },
    { kServerTick,       0, "server_tick"      },
    { kVarpInt,          6, "varp_int"         },
    { kVarpByte,         3, "varp_byte"        },
    { kVarcInt,          6, "varc_int"         },
    { kVarcByte,         3, "varc_byte"        },
    { kVarpLong,        10, "varp_long"        },
    { kVarbitVarint,    -1, "varbit_set"       },
    { kZoneBase,         3, "zone_base"        },
    { kZoneClear,        3, "zone_clear"       },
    { kZoneUpdate,      -2, "zone_update"      },
    { kObjAdd,           6, "obj_add"          },
    { kObjDel,           4, "obj_del"          },
    { kObjCount,         8, "obj_count"        },
    { kLocAdd,           7, "loc_add"          },
    { kLocDel,           2, "loc_del"          },
    { kSpotAnim,        11, "spotanim"         },
    { kSpotAnim2,       14, "spotanim"         },
    { kSpotAnimActor,   12, "spotanim_actor"   },
    { kSpotAnimActor2,  15, "spotanim_actor"   },
    { kProjectile,      21, "projectile"       },
    { kSound,            8, "sound"            },
    { kAreaSound,       10, "area_sound"       },
    { kAreaSoundAbs,    11, "area_sound"       },
};

inline constexpr int kDefaultCaptured[] = {
    kSkillUpdate, kGeOffer, kContainerUpdate, kRunClientScript, kRunEnergy, kRunWeight, kPingEcho,
    kVarpInt, kVarpByte, kVarcInt, kVarcByte, kVarpLong, kVarbitVarint,
    kZoneBase, kZoneClear, kZoneUpdate, kObjAdd, kObjDel, kObjCount, kLocAdd, kLocDel,
    kSpotAnim, kSpotAnim2, kSpotAnimActor, kSpotAnimActor2, kProjectile, kSound, kAreaSound, kAreaSoundAbs,
};
constexpr std::uint32_t DefaultMaskWord(int word) {
    std::uint32_t m = 0;
    for (int op : kDefaultCaptured) if ((op >> 5) == word) m |= (1u << (op & 31));
    return m;
}

}  // namespace rtx::sops
