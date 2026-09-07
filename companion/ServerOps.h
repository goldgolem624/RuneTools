#pragma once
// Server -> client opcodes that the reader, the companion and the panels key on.
//
// Jagex reshuffles these with game updates: 949-5 -> 950-1 changed EVERY one of them (message_game
// 0x15 -> 0x21, skill_update 0x04 -> 0x5C, ...). So they live here and nowhere else: the event
// decoders switch on these names, the companion's default capture mask is built from them, and
// the launcher exports the table to the panels as state.serverOps so the JS side never carries
// a number of its own. The health check's "Server opcodes" row reads the client's packet table
// and confirms each opcode still has the wire length recorded here, so a reshuffle shows up as a
// red row instead of a panel quietly decoding the wrong packet.
//
// How each was pinned on 950-1 (repeatable next update, see docs/cs2_opcodes.md):
//   tools/rtx_offsets.py pkt  -> per-opcode handler fingerprint (MainData fields it touches):
//     skill_update / run_energy / run_weight write the skill block (0x19920 -> 0x7618),
//     ge_offer indexes the GE slots (0x19990), server_tick increments the tick counter (0xDBF0).
//   a 60 s capture of the event ring with the mask wide open (offline capture script):
//     container_update starts with container 93 (backpack), runclientscript runs every tick with
//     script args, ping_echo is the 8-byte nonce every ~6 s, message_game carries the chat text.
#include <cstdint>

namespace rtx::sops {

inline constexpr int kMessageGame     = 0x21;   // 0x15 on 949   var-byte : [type smart][u32][flags][sender?][text]
inline constexpr int kSkillUpdate     = 0x5C;   // 0x04 on 949   6 bytes  : [skill -b0][level -b1][xp u32 BE]
inline constexpr int kContainerUpdate = 0x32;   // 0x2B on 949   var-short: [container u16 BE][flags u8] slots...
inline constexpr int kRunClientScript = 0x82;   // 0x52 on 949   var-short
inline constexpr int kGeOffer         = 0x54;   // 0x51 on 949   36 bytes (949 also had a 35-byte 0x05; no 950-1 counterpart)
inline constexpr int kRunEnergy       = 0x15;   // 0x5C on 949   1 byte   : [energy u8] -> skill block +0x18
inline constexpr int kRunWeight       = 0x07;   // 0x00 on 949   2 bytes  : [weight i16 BE] -> skill block +0x1C
inline constexpr int kPingEcho        = 0xBE;   // 0x8D on 949   8 bytes  : two u32 BE nonces, echoed back
inline constexpr int kServerTick      = 0xA0;   // 0xB4 on 949   0 bytes  : tick boundary (INC [MainData+0xDBF0])
inline constexpr int kOpMax           = 0xDE;   // framer bound (`cmp eax,0xDE; ja`); 0xE5 on 949

// Wire length each opcode must carry in the client's packet table (+0x04 of the descriptor):
// >= 0 fixed, -1 var-byte, -2 var-short. The health check compares these against the live table.
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
};

// Opcodes the event ring records when the launcher has not written a mask (message_game is
// left out on purpose: the chat ring owns it).
inline constexpr int kDefaultCaptured[] = {
    kSkillUpdate, kGeOffer, kContainerUpdate, kRunClientScript, kRunEnergy, kRunWeight, kPingEcho,
};
constexpr std::uint32_t DefaultMaskWord(int word) {
    std::uint32_t m = 0;
    for (int op : kDefaultCaptured) if ((op >> 5) == word) m |= (1u << (op & 31));
    return m;
}

}  // namespace rtx::sops
