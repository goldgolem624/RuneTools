#pragma once
//
// Launcher <-> companion contract for the EVENT CHANNEL: a filtered copy of decoded
// server -> client packets (same capture point as NetProbeShare.h, the inbound framer)
// that panels and plugins subscribe to instead of polling. Always on while the framer
// hook lives; the launcher chooses WHICH opcodes land here through `mask`.
//
// Single writer (the game's network thread), many readers. Each record is its own
// odd/even seqlock (seq odd while the body is being filled, even once published) and
// `written` is the monotonic record count, published after the record. Plain C-layout
// POD; zero allocations on the hook path.

#include <cstdint>

namespace rtx::events {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXEvents_v2_";   // moves with kVersion
inline constexpr std::uint32_t kMagic   = 0x54564552;   // 'REVT'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr int kMaxRecords = 2048;   // ~20 s of the busiest observed rate (106 op-0x52 / 45 s + per-tick ops)
inline constexpr int kPayload    = 1024;    // bytes kept; `length` still reports the true wire size

// Default opcode mask (launcher never wrote one): skill_update 0x04, ge_offer 0x05/0x51,
// container_update 0x2B, runclientscript 0x52, run_energy 0x5C, run_weight 0x00,
// ping_echo 0x8D. 0x15 (message_game) is never recorded here: the chat ring owns it.
inline constexpr std::uint32_t kDefaultMask[8] = {
    (1u << 0x04) | (1u << 0x05) | (1u << 0x00),          // word 0: opcodes 0x00..0x1F
    (1u << (0x2B - 0x20)),                               // word 1: 0x20..0x3F
    (1u << (0x51 - 0x40)) | (1u << (0x52 - 0x40)) | (1u << (0x5C - 0x40)),   // word 2: 0x40..0x5F
    0,                                                   // word 3: 0x60..0x7F
    (1u << (0x8D - 0x80)),                               // word 4: 0x80..0x9F
    0, 0, 0 };

struct Record {
    std::uint32_t seq;        // seqlock: (index+1)*2 when published, that value | 1 while filling
    std::uint32_t tick;       // GetTickCount() at capture (client cycle is not cheaply reachable here)
    std::uint32_t wallMs;     // epoch ms, low 32 bits (wraps every ~49.7 days; pair with tick)
    std::int32_t  opcode;     // deciphered server opcode
    std::int32_t  length;     // TRUE payload length on the wire (may exceed kPayload)
    std::uint8_t  payload[kPayload];   // first min(length, kPayload) bytes
};

struct Share {
    std::uint32_t magic;               // kMagic once initialised
    std::uint32_t version;             // kVersion
    std::uint32_t pid;                 // target client pid (sanity)
    std::uint32_t maskSet;             // launcher wrote `mask` at least once
    std::uint32_t mask[8];             // bit (op & 31) of word (op >> 5): record this opcode
    std::uint32_t flags;               // bit0 = hook feeding this ring
    volatile std::uint64_t written;    // records ever written; published AFTER the record. slot = i % kMaxRecords
    std::uint64_t truncated;           // records whose length exceeded kPayload
    std::uint64_t inbound;             // every framed inbound message seen (any opcode)
    Record recs[kMaxRecords];
};

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

}  // namespace rtx::events
