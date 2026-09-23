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
    bool        augmented   = false; // "Disassemble" worn option or a destroy message naming "gizmos"
    // Opcodes 203-208, each an item id in the same 24-bit form the noted link uses. These are the
    // links between an item and its other forms, which is how a variant that the market does not
    // know (augmented, charged, degraded) is traced back to the one it does. -1 = absent.
    int         linked[6]   = { -1, -1, -1, -1, -1, -1 };
    // opcode-249 params, kept verbatim.
    std::map<int, int>         params_i;
    std::map<int, std::string> params_s;
    // Right-click options in slot order: opcodes 30-34 ground, 35-39 carried/worn.
    std::string options[5];
    std::string worn_options[5];
    // opcode 132: VAROBJ list in slot order; instance-var key N means varobjs[N]. Augmented gear:
    // 30212 item XP, 30215/30216 gizmo-1 perk-1 id/rank, 30217/30218 perk-2, 30219..30222 gizmo-2.
    std::vector<int> varobjs;
};

// Decodes one item def. stop_op receives the opcode that ended the decode early (0 = clean end).
ItemDef DecodeItem(int id, std::vector<std::uint8_t> file_bytes, int* stop_op = nullptr);

}  // namespace rtx::cache
