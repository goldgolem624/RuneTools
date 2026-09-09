#pragma once
// Unknown-opcode probe shared by every cache decoder (CacheProbeUnknownOps()): with g_op = N, g_len = L, decoders treat opcode N as "skip L bytes".
// The harness sweeps L and counts clean-terminating records to find a new opcode's payload size. Off when g_op = -1.
namespace rtx::cache::probe {
inline int g_op   = -1;   // opcode to treat as a fixed-size skip (-1 = off)
inline int g_len  = 0;    // bytes to skip after it
inline int g_tail = -1;   // bytes left unread when the last decode returned (0 = consumed exactly)
inline int g_stop = -1;   // stream offset just after the opcode byte that stopped the last decode
}
