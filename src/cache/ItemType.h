#pragma once

#include "InputStream.h"

#include <map>
#include <string>
#include <vector>

namespace rtx::cache {

struct ItemDef {
    int         id          = -1;
    std::string name;
    int         inv_model_id = -1;
    int         noted_template = -1;
    int         noted_unnoted  = -1;
    bool        stackable   = false;
    bool        tradeable   = false;
    bool        noted       = false;
    int         ge_limit    = -1;
    int         category    = -1;
    long long   value       = -1;   // opcode 181 (gp); drives high/low alch
    bool        augmented   = false; // Invention-augmented: a "Disassemble" worn
                                      // option or a destroy message naming "gizmos"
    // opcode-249 params, kept verbatim. Most items have none; the ones that do carry
    // real gameplay data (e.g. Player-Owned Ports crew: 3080 icon sprite, 3081-3084
    // stats, 3093/3094 + 3095/3096 cost pairs).
    std::map<int, int>         params_i;
    std::map<int, std::string> params_s;
    // Right-click options, kept in slot order (empty slots stay empty): opcodes 30-34 are the
    // ground/floor set, 35-39 the carried/worn set. Needed to author a menu-reorder rule from an
    // item id without hovering the item in game.
    std::string options[5];
    std::string worn_options[5];
};

// Parses the file bytes for one item id. Walks opcode-by-opcode until
// opcode 0; only the fields we care about are kept, the rest are
// consumed correctly to advance the stream. stop_op (optional) receives the
// opcode that terminated the decode early (0 = clean end) -- used by the
// cache parse-health check.
ItemDef DecodeItem(int id, std::vector<std::uint8_t> file_bytes, int* stop_op = nullptr);

}  // namespace rtx::cache
