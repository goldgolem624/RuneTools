#pragma once
// Unknown-opcode probe shared by every cache decoder (CacheProbeUnknownOps()): with g_op = N, g_len = L, decoders treat opcode N as "skip L bytes".
// The harness sweeps L and counts clean-terminating records to find a new opcode's payload size. Off when g_op = -1.
#include <atomic>
#include <string>
namespace rtx::cache::probe {
inline int g_op   = -1;   // opcode to treat as a fixed-size skip (-1 = off)
inline int g_len  = 0;    // bytes to skip after it
inline thread_local int g_tail = -1;   // bytes left unread when the last decode returned (0 = consumed exactly)
inline thread_local int g_stop = -1;   // stream offset just after the opcode byte that stopped the last decode
// Opcode histogram. An opcode a decoder still reads a field from but that has dropped to zero
// across the whole index means the field moved and is now silently empty -- that is how the 950-1
// quest tracker and the npc/loc morph tables went missing. The parse sweep points t_hist at its
// own 512-int table per type; g_hist is the process-wide table the offline tools switch on.
inline bool g_hist_on = false;
inline int  g_hist[512] = {};
inline thread_local int* t_hist = nullptr;
inline void note(int op) {
    if (op <= 0 || op >= 512) return;
    if (t_hist) ++t_hist[op];
    else if (g_hist_on) ++g_hist[op];
}
// Last candidate var id read by an opcode whose payload is only partly understood (npc 189,
// loc 209). The harness checks it resolves in the varbit archive; a wrong slot would not.
inline thread_local int g_cand = -1;
inline void cand(int v) { if (g_hist_on) g_cand = v; }
// Raw payload of the last such opcode (from the opcode byte to where the decoder stopped) plus
// the record tail after it, so the layout can be fitted offline against every live record.
inline std::string g_payload;
inline std::string g_tail_bytes;

// Readers that stop on an opcode they do not know and keep what they have (the varbit map, enums,
// structs, map colours, map scenes, dbrows) count the stop here instead of dropping it silently;
// CacheUnknownOps() reports the counts since the cache opened. Reset on a cache update.
enum UnknownKind { kUnkVarbit, kUnkEnum, kUnkStruct, kUnkColour, kUnkMapscene, kUnkDbrow, kUnkKinds };
inline const char* const kUnknownKindNames[kUnkKinds] = { "varbits", "enums", "structs", "map colours", "map scenes", "dbrows" };
inline std::atomic<int> g_unknown[kUnkKinds][256];
inline void unknown(int kind, int op) {
    if (kind < 0 || kind >= kUnkKinds || op < 0 || op > 255) return;
    g_unknown[kind][op].fetch_add(1, std::memory_order_relaxed);
}
inline void unknown_reset() {
    for (auto& k : g_unknown) for (auto& c : k) c.store(0, std::memory_order_relaxed);
}
}
