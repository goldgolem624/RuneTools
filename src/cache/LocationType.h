#pragma once

#include "InputStream.h"

#include <array>
#include <string>
#include <vector>

namespace rtx::cache {

// Decoded location ("object") definition (index 16). We keep the fields the
// Scene panel needs (name, right-click options, footprint); every other opcode
// is consumed to its correct width so the stream stays aligned. Faithful to the
// RS3 LocationConfig opcode table (rs3cache src/definitions/location_configs.rs,
// feature "rs3").
struct LocDef {
    int                        id = -1;
    std::string                name;
    std::array<std::string, 5> options;          // opcodes 30..34 (base right-click actions)
    std::array<std::string, 5> members_options;  // opcodes 150..154 (override on members worlds)
    int                        dim_x = 1;         // opcode 14
    int                        dim_y = 1;         // opcode 15
    int                        mapscene = -1;     // opcode 102: world-map scene icon (-> mapscene def -> sprite)
    int                        mapFunction = -1;  // opcode 107 (0x6B): worldmap maplabel id (-> config 2/arch 36 -> icon sprite + text)
    bool                       members = false;   // opcode 91
    bool                       no_clip = false;   // opcode 17 (unknown_17): tile stays walkable
    std::vector<int>           morph_children;    // opcodes 77/92 configChangeDest -- varbit-selected variant ids (>=0 only)
    // Morph selector: the live variant is morph_variants[value], where value is the
    // varbit (or varp) reading. morph_variants is index-aligned (keeps -1 = "no
    // change"); morph_default is op92's fallback when the value is out of range.
    int                        morph_varbit = -1;  // opcodes 77/92 first field (0xFFFF -> -1)
    int                        morph_varp = -1;    // opcodes 77/92 second field (0xFFFF -> -1)
    int                        morph_default = -1; // opcode 92 default child
    std::vector<int>           morph_variants;     // value-indexed variants (incl -1 placeholders)
};

LocDef DecodeLoc(int id, std::vector<std::uint8_t> file_bytes, int* stop_op = nullptr);

}  // namespace rtx::cache
