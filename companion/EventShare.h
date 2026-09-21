#pragma once
// Event channel (framer packets filtered by `mask`); single writer, per-record odd/even seqlock, `written` published last.

#include <cstdint>
#include "ShareName.h"
#include "ServerOps.h"

namespace rtx::events {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXEvents_v2_";   // moves with kVersion
inline constexpr std::uint32_t kMagic   = 0x54564552;   // 'REVT'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr int kMaxRecords = 2048;   // ~20 s at the busiest observed rate
inline constexpr int kPayload    = 1024;    // bytes kept; `length` still reports the true wire size

inline constexpr std::uint32_t kDefaultMask[8] = {
    rtx::sops::DefaultMaskWord(0), rtx::sops::DefaultMaskWord(1), rtx::sops::DefaultMaskWord(2), rtx::sops::DefaultMaskWord(3),
    rtx::sops::DefaultMaskWord(4), rtx::sops::DefaultMaskWord(5), rtx::sops::DefaultMaskWord(6), rtx::sops::DefaultMaskWord(7) };

struct Record {
    std::uint32_t seq;        // seqlock: (index+1)*2 when published, that value | 1 while filling
    std::uint32_t tick;       // GetTickCount() at capture
    std::uint32_t wallMs;     // epoch ms, low 32 bits
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
    volatile std::uint64_t written;    // records ever written, published after the record; slot = i % kMaxRecords
    std::uint64_t truncated;           // records whose length exceeded kPayload
    std::uint64_t inbound;             // every framed inbound message seen (any opcode)
    Record recs[kMaxRecords];
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::events
