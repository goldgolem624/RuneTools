#pragma once
// Unknown-opcode probe, shared by every cache decoder (NpcType, LocationType, the quest
// decoder in CacheReader.cpp, Achievements) and driven by CacheProbeUnknownOps() /
// tools/cacheprobe. When a game update adds an opcode the decoders stop at it; this turns
// "break at opcode N" into the opcode's payload size without a decompiler:
//
//   1. g_op = N, g_len = L makes every decoder treat opcode N as "skip L bytes".
//   2. The harness re-decodes every record that stopped at N for L = 0..kMaxLen and counts
//      the records that then reach a clean 0 terminator with nothing left over (g_tail == 0).
//   3. A fixed-size payload shows up as exactly one L with 100 % success; a variable payload
//      (string, counted list) shows up as no consistent L, and the hex dumps in the report
//      are what you read instead.
//
// Off (g_op = -1) in normal operation; the decoders pay one integer compare per unknown op.
namespace rtx::cache::probe {
inline int g_op   = -1;   // opcode to treat as a fixed-size skip (-1 = off)
inline int g_len  = 0;    // bytes to skip after it
inline int g_tail = -1;   // bytes left unread when the last decode returned (0 = consumed exactly)
inline int g_stop = -1;   // stream offset just after the opcode byte that stopped the last decode
}
