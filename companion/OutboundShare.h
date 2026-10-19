#pragma once

#include <cstdint>
#include "ShareName.h"

// Decoded client -> server packets captured at the outbound builder (MakePacket). Single-writer ring,
// `written` published last. The opcode is the plaintext value from the OutPacket (OutPacket+0x00); the
// payload is the bytes after the enciphered opcode byte (buffer.data[1 .. pos-1]). Capture is deferred
// by one: each MakePacket call flushes the previous packet, whose pool block has not been reused yet.
namespace rtx::outbound {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXOutbound_v1_";
inline constexpr std::uint32_t kMagic   = 0x5442554F;   // 'OUTB'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxRecords = 1024;    // ring depth
inline constexpr int kSnip       = 512;     // payload bytes kept

struct Record {
    std::uint64_t tick;      // GetTickCount64 at capture
    std::uint64_t seq;       // monotonic record index (== its slot's written value + 1)
    std::int32_t  opcode;    // plaintext opcode (OutPacket+0x00)
    std::int32_t  length;    // payload bytes on the wire after the opcode (pos - 1)
    std::uint32_t kept;      // min(length, kSnip) copied into data
    std::uint32_t pad;
    std::uint8_t  data[kSnip];
};

struct Share {
    std::uint32_t magic;              // kMagic once initialised
    std::uint32_t version;            // kVersion
    std::uint32_t pid;                // target client pid (sanity)
    volatile std::uint32_t enable;    // launcher sets non-zero to record
    volatile std::uint64_t written;   // published after the record body; slot = (written-1) % kMaxRecords
    std::uint64_t seen;               // every outbound packet captured
    std::uint32_t flags;              // bit0 = builder hook installed
    std::uint32_t builderRva;         // resolved MakePacket RVA (diag; 0 = unresolved)
    std::uint32_t diag[8];
    Record recs[kMaxRecords];
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::outbound
