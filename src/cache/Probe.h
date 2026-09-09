#pragma once
// Unknown-opcode probe shared by every cache decoder (CacheProbeUnknownOps()): with g_op = N, g_len = L, decoders treat opcode N as "skip L bytes".
// The harness sweeps L and counts clean-terminating records to find a new opcode's payload size. Off when g_op = -1.
#include <string>
namespace rtx::cache::probe {
inline int g_op   = -1;   // opcode to treat as a fixed-size skip (-1 = off)
inline int g_len  = 0;    // bytes to skip after it
inline int g_tail = -1;   // bytes left unread when the last decode returned (0 = consumed exactly)
inline int g_stop = -1;   // stream offset just after the opcode byte that stopped the last decode
// Opcode histogram (off by default). An opcode a decoder still reads a field from but that has
// dropped to zero across the whole index means the field moved and is now silently empty --
// that is how the 950-1 quest tracker and the npc/loc morph tables went missing.
inline bool g_hist_on = false;
inline int  g_hist[512] = {};
inline void note(int op) { if (g_hist_on && op >= 0 && op < 512) ++g_hist[op]; }
// Last candidate var id read by an opcode whose payload is only partly understood (npc 189,
// loc 209). The harness checks it resolves in the varbit archive; a wrong slot would not.
inline int g_cand = -1;
inline void cand(int v) { if (g_hist_on) g_cand = v; }
// Raw payload of the last such opcode (from the opcode byte to where the decoder stopped) plus
// the record tail after it, so the layout can be fitted offline against every live record.
inline std::string g_payload;
inline std::string g_tail_bytes;
}
