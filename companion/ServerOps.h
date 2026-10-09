#pragma once
// Server -> client opcodes (re-pin per update: tools/rtx_offsets.py pkt); Expect.len = packet-table descriptor +0x04, -1 var-byte, -2 var-short.
#include <cstdint>

namespace rtx::sops {

inline constexpr int kMessageGame     = 0x21;   // 0x15 on 949   var-byte : [type smart][u32][flags][sender?][text]
inline constexpr int kSkillUpdate     = 0x5C;   // 0x04 on 949   6 bytes  : [skill -b0][level -b1][xp u32 BE]
inline constexpr int kContainerUpdate = 0x32;   // 0x2B on 949   var-short: [container u16 BE][flags u8] slots...
inline constexpr int kRunClientScript = 0x23;   // 0x52 on 949   var-short : [sig][args reversed][script i32]; live 2026-09-12: 278/304 decode
                                                //   (0x82 is NOT runclientscript: it carries tracker cell values for the tracker grid at MainData+0x19850)
inline constexpr int kGeOffer         = 0x54;   // 0x51 on 949   36 bytes (949 also had a 35-byte 0x05; no 950-1 counterpart)
inline constexpr int kRunEnergy       = 0x15;   // 0x5C on 949   1 byte   : [energy u8] -> skill block +0x18
inline constexpr int kRunWeight       = 0x07;   // 0x00 on 949   2 bytes  : [weight i16 BE] -> skill block +0x1C
inline constexpr int kPingEcho        = 0xBE;   // 0x8D on 949   8 bytes  : two u32 BE nonces, echoed back
inline constexpr int kServerTick      = 0xA0;   // 0xB4 on 949   0 bytes  : tick boundary; the handler increments +0xDBF0 of a heap
                                                //   object of its own (the tick counter, allocated at start-up), not of the client root, so a
                                                //   MainData shift never moves it: a change there is a tick counter layout change
// Var set packets, layouts confirmed live against the varp/varc stores on 2026-09-12 (docs/fieldmap-950-1.md):
inline constexpr int kVarpInt         = 0x04;   // 6 bytes  : id = ((b0-0x80)&0xFF)|(b1<<8); value = (b4<<24)|(b5<<16)|(b2<<8)|b3   (26/27 matched)
inline constexpr int kVarpByte        = 0x4F;   // 3 bytes  : value = i8 b0; id = ((b2-0x80)&0xFF)|(b1<<8)                            (10/14)
inline constexpr int kVarcInt         = 0x77;   // 6 bytes  : id = ((b1-0x80)&0xFF)|(b0<<8); value = (b3<<24)|(b2<<16)|(b5<<8)|b4     (handler 0x140141e90)
inline constexpr int kVarcByte        = 0x7E;   // 3 bytes  : id = b0|(b1<<8); value = (int8)(0x80-b2)                                 (handler 0x140141fc0)
inline constexpr int kVarbitVarint    = 0x74;   // var-byte : two LEB128 varints (7 bits per byte, low first, high bit = continue): varbit id, value (handler 0x1401420d0, config slot +0x230)
inline constexpr int kVarpLong        = 0xA5;   // 10 bytes : value = i64 hi=(b1<<24)|(b0<<16)|(b3<<8)|b2, lo=(b5<<24)|(b4<<16)|(b7<<8)|b6; id = (b8<<8)|b9 (handler 0x140142390; op from the live descriptor table, see tools/rtx_pkt_table.py)
inline constexpr int kVarbitByte      = 0x1C;   // 3 bytes  : value = (b0+0x80)&0xFF; varbit id = b1|(b2<<8) (same setter as 0x74)
inline constexpr int kVarbitInt       = 0x52;   // 6 bytes  : value = i32 BE; varbit id = u16 LE at 4
inline constexpr int kVarcLong        = 0xC8;   // 10 bytes : value = i64, hi = u32 LE at 0, lo = u32 LE at 4; varc id = u16 LE at 8
// Trackers: the skill, combat and loot tracker feed (groups of 0x68 at [MainData+0x19850], each a
// grid of i32 cells [row][column]; INT32_MIN = no value). Group, row and column bytes are indexes
// into the groups vector and the group's row and column id lists.
inline constexpr int kTrackerGroup    = 0xB0;   // 5 bytes  : group id = (b2<<24)|(b3<<16)|(b0<<8)|b1, slot = (int8)(b4+0x80)
inline constexpr int kTrackerValues   = 0x82;   // var-short: { group i8 (0xFF ends) { row i8 (0xFF ends) { column i8 (0xFF ends), value i32 BE } } }
inline constexpr int kTrackerRemove   = 0xC2;   // 1 byte   : slot = (int8)(-b0)
inline constexpr int kTrackerClear    = 0xC1;   // 3 bytes  : group b0, column = -b1, row b2; the cell becomes INT32_MIN
inline constexpr int kTrackerColumn   = 0x9D;   // 3 bytes  : group = 0x80-b0, column b1, shown = (b2 == 0x81)
// Inventories: the manager at [MainData+0x199C8] keys its sorted entries by container id * 2 | other-player flag.
inline constexpr int kContainerFull   = 0x09;   // var-short: [container u16 BE][flags u8][count u16 BE] then count x [itemId+1 u24 BE][qty u8 | 0xFF u32 BE][variant if flags&2]; flags&1 = another player's
inline constexpr int kContainerReset  = 0x14;   // 3 bytes  : other = (-b0)&1, container = ((b2-0x80)&0xFF)|(b1<<8)
// Small state packets.
inline constexpr int kSystemUpdate    = 0x1F;   // 2 bytes  : seconds u16 BE (the client keeps ticks: x30, x2.5 in the lobby)
inline constexpr int kCameraTarget    = 0x35;   // 4 bytes  : packed tile (b1<<24)|(b0<<16)|(b3<<8)|b2, 0xFFFFFFFF clears
inline constexpr int kCutscene        = 0xCD;   // 2 bytes  : cutscene id u16 BE
inline constexpr int kFriendsLoaded   = 0x5D;   // 0 bytes  : the friends list is loaded
inline constexpr int kPrivateFilter   = 0x28;   // 1 byte   : private chat filter
inline constexpr int kMinimapState    = 0x20;   // 1 byte   : minimap mode = b0 % 3, shown when b0 < 3
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
inline constexpr int kHintArrow       = 0x62;   // 14 bytes : the game's hint arrow (engine markers)
inline constexpr int kTileTrail       = 0x3A;   // var-short: the game's tile trail (engine markers); its handler is the trail-message signature
inline constexpr int kProjectile20    = 0x72;   // 20 bytes : projectile, zone sub-packet 0x0F body
inline constexpr int kProjectile28    = 0xA9;   // 28 bytes : projectile, zone sub-packet 0x10 body
inline constexpr int kProjectile29    = 0xC4;   // 29 bytes : projectile, zone sub-packet 0x11 body
inline constexpr int kZoneSub3        = 0xB5;   // 5 bytes  : zone sub-packet 0x03 body (layout not decoded)
inline constexpr int kZoneSub14       = 0x90;   // var-byte : zone sub-packet 0x0E body (layout not decoded)
inline constexpr int kOpMax           = 0xDE;   // framer bound (`cmp eax,0xDE; ja`); 0xE5 on 949

// Wire length of every opcode 0..kOpMax as the descriptor initializers give it (-1 var-byte, -2
// var-short), so an update that resizes any packet is named by opcode with the game closed.
inline constexpr int kAllLengths[kOpMax + 1] = {
     4, 19,  3, -2,  6, -2,  1,  2, -1, -2, -2, -1, -1, 25, 11, -2,   // 0x00
     6, 25, -2, 10,  3,  1, -1,  0, 12,  8,  2,  6,  3, -2, -1,  2,   // 0x10
     1, -1, -1, -2, -2, 10,  4,  8,  1,  8, 25, -1,  8,  8,  1,  8,   // 0x20
     3, -2, -2,  6,  6,  4, 10, -1,  4, -2, -2, 10, -1, -2,  2, -2,   // 0x30
    -2,  8, -2,  5, 10,  4,  8, 10,  0,  1, -1,  7,  0, -1, -2,  3,   // 0x40
    -2, -2,  6,  6, 36,  5, -2,  6, -1, -1,  6, 30,  6,  0, 19, 11,   // 0x50
     3, 10, 14, -1, 23,  4, 11, 28, -1,  5,  0,  5,  0,  4, 10, 32,   // 0x60
     0,  0, 20, -2, -1, 12, 10,  6, -1, -1,  8, -1,  0, -2,  3,  8,   // 0x70
     1, -2, -2,  0,  2,  4, -1, -1,  0,  1,  6,  2, -2,  8, -2, 12,   // 0x80
    -1,  3,  2, -1, -1,  2,  9,  2,  5,  4, 21,  4,  1,  3, 14, -1,   // 0x90
     0,  0, -2, -2, 10, 10, -2,  5,  9, 28, -1, 17, -2, 15,  3, -1,   // 0xA0
     5, -2,  1,  4,  3,  5,  1,  0,  1, -2,  3,  8, 14,  3,  8,  6,   // 0xB0
     1,  3,  1,  1, 29, 15, 33, -1, 10,  4,  0,  3, -1,  2,  3,  5,   // 0xC0
    -2, -1, -2,  4,  9,  5,  0,  6, 14,  6,  2,  4, -2,  1, -2,       // 0xD0
};

// The descriptor table is a fixed-capacity vector in the client's data: {begin, end, capacity}
// then the inline buffer at +0x28, one descriptor pointer per opcode in opcode order. A
// descriptor holds its opcode at +0, the wire length at +4 (-1 var-byte, -2 var-short) and at
// +0x10 the vtable whose slot 2 (+0x10) is the handler (often a 9-byte thunk `add rcx,8 ; jmp`).
// The companion checks its lengths against kExpected at attach (boot record line `check: packets`).
inline constexpr std::uint32_t kProtBuffer   = 0x28;   // vector -> inline descriptor buffer
inline constexpr std::uint32_t kDescOp       = 0x00;   // descriptor -> i32 opcode
inline constexpr std::uint32_t kDescLen      = 0x04;   // descriptor -> i32 wire length
inline constexpr std::uint32_t kDescVtbl     = 0x10;   // descriptor -> handler object vtable
inline constexpr std::uint32_t kVtblHandler  = 0x10;   // vtable -> handler (slot 2)
// The inbound connection object the framer is called with, read by the companion's packet feed.
inline constexpr std::uint32_t kConnOp       = 0x2C;   // i32 opcode of the packet just framed, -1 none
inline constexpr std::uint32_t kConnLen      = 0x30;   // i32 payload length
inline constexpr std::uint32_t kConnPayload  = 0x2D0;  // payload pointer
inline constexpr std::uint32_t kConnRx       = 0x2E8;  // u32 cumulative inbound bytes

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
    { kHintArrow,       14, "hint_arrow"       },
    { kProjectile20,    20, "projectile"       },
    { kProjectile28,    28, "projectile"       },
    { kProjectile29,    29, "projectile"       },
    { kZoneSub3,         5, "zone_sub3"        },
    { kZoneSub14,       -1, "zone_sub14"       },
    { kVarbitByte,       3, "varbit_byte"      },
    { kVarbitInt,        6, "varbit_int"       },
    { kVarcLong,        10, "varc_long"        },
    { kTrackerGroup,     5, "tracker_group"    },
    { kTrackerValues,   -2, "tracker_values"   },
    { kTrackerRemove,    1, "tracker_remove"   },
    { kTrackerClear,     3, "tracker_clear"    },
    { kTrackerColumn,    3, "tracker_column"   },
    { kContainerFull,   -2, "container_full"   },
    { kContainerReset,   3, "container_reset"  },
    { kSystemUpdate,     2, "system_update"    },
    { kCameraTarget,     4, "camera_target"    },
    { kCutscene,         2, "cutscene"         },
    { kFriendsLoaded,    0, "friends_loaded"   },
    { kPrivateFilter,    1, "private_filter"   },
    { kMinimapState,     1, "minimap_state"    },
};

inline constexpr int kDefaultCaptured[] = {
    kSkillUpdate, kGeOffer, kContainerUpdate, kRunClientScript, kRunEnergy, kRunWeight, kPingEcho,
    kVarpInt, kVarpByte, kVarcInt, kVarcByte, kVarpLong, kVarbitVarint, kVarbitByte, kVarbitInt, kVarcLong,
    kZoneBase, kZoneClear, kZoneUpdate, kObjAdd, kObjDel, kObjCount, kLocAdd, kLocDel,
    kSpotAnim, kSpotAnim2, kSpotAnimActor, kSpotAnimActor2, kProjectile, kSound, kAreaSound, kAreaSoundAbs, kHintArrow,
    kZoneSub3, kZoneSub14, kContainerFull, kContainerReset,
    kTrackerGroup, kTrackerValues, kTrackerRemove, kTrackerClear, kTrackerColumn,
    kSystemUpdate, kCameraTarget, kCutscene, kFriendsLoaded, kPrivateFilter, kMinimapState,
};
constexpr std::uint32_t DefaultMaskWord(int word) {
    std::uint32_t m = 0;
    for (int op : kDefaultCaptured) if ((op >> 5) == word) m |= (1u << (op & 31));
    return m;
}

}  // namespace rtx::sops
