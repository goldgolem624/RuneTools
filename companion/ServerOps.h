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
inline constexpr int kVarcInt         = 0x77;   // 6 bytes  : id = ((b1-0x80)&0xFF)|(b0<<8); value = (b2<<24)|(b3<<16)|(b5<<8)|b4     (2/2, high bytes unproven)
inline constexpr int kVarcByte        = 0x7E;   // 3 bytes  : id = b0|(b1<<8); value = (0x80-b2)&0xFF                                  (2/2)
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
};

inline constexpr int kDefaultCaptured[] = {
    kSkillUpdate, kGeOffer, kContainerUpdate, kRunClientScript, kRunEnergy, kRunWeight, kPingEcho,
    kVarpInt, kVarpByte, kVarcInt, kVarcByte,
};
constexpr std::uint32_t DefaultMaskWord(int word) {
    std::uint32_t m = 0;
    for (int op : kDefaultCaptured) if ((op >> 5) == word) m |= (1u << (op & 31));
    return m;
}

}  // namespace rtx::sops
