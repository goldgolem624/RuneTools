#pragma once

#include "InputStream.h"

#include <array>
#include <string>
#include <vector>

namespace rtx::cache {

// Decoded NPC definition (index 18); unused opcodes are consumed to keep the stream aligned.
struct NpcDef {
    int                      id = -1;
    std::string              name;
    std::array<std::string, 5> options;          // opcodes 30..34 (base right-click actions)
    std::array<std::string, 5> members_options;  // opcodes 150..154 (override on members worlds)
    int                      combat_level = -1;
    int                      size = 1;      // opcode 12: tile footprint (2 = a 2x2 npc)
    int                      wander_range = -1;   // opcode 26 (u16), absent = -1
    int                      max_range = -1;      // opcode 27 (u16), absent = -1
    int                      varp   = -1;
    int                      varbit = -1;
    std::vector<int>         transform_to;  // varbit/varp morph targets
};

NpcDef DecodeNpc(int id, std::vector<std::uint8_t> file_bytes, int* stop_op = nullptr);

}  // namespace rtx::cache
