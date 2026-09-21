#pragma once

#include <cstdint>
#include "ShareName.h"

namespace rtx::netprobe {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXNetProbe_v1_";
inline constexpr std::uint32_t kMagic   = 0x5450524E;   // 'NRPT'
inline constexpr std::uint32_t kVersion = 3;            // v3: dedicated chat ring appended
inline constexpr int kMaxRecords = 1024;    // ring depth
inline constexpr int kSnip       = 1024;    // payload bytes kept; 0x5D rebuild_scene_dyn reaches ~880
// Decoded server -> client packets from the inbound framer; single-writer ring, `written` published last. Chat ring: op 0x15 message_game, not gated by `enable`. Opcodes 0..0xDE on 950-1 (0..0xE5 before).
inline constexpr int kChatRecords = 256;
inline constexpr int kChatSnip    = 512;    // one message: type+u32+flags+sender+text

struct Record {
    std::uint64_t tick;      // GetTickCount64 at capture
    std::uint64_t seq;       // monotonic record index (== its slot's written value + 1)
    std::int32_t  opcode;    // deciphered opcode, or -1 if the framer flagged it invalid
    std::int32_t  length;    // resolved payload length (bytes on the wire after the opcode)
    std::uint32_t kept;      // min(length, kSnip) actually copied into data
    std::uint32_t gtick;     // game tick counter at capture (0 if unavailable)
    std::uint8_t  data[kSnip];
};

struct ChatRecord {
    std::uint64_t tick;      // GetTickCount64 at capture
    std::uint64_t seq;       // monotonic (== its slot's chatWritten value + 1)
    std::int32_t  length;    // wire payload length
    std::uint32_t kept;      // min(length, kChatSnip) copied into data
    std::uint8_t  data[kChatSnip];
};

struct Share {
    std::uint32_t magic;              // kMagic once initialised
    std::uint32_t version;            // kVersion
    std::uint32_t pid;                // target client pid (sanity)
    volatile std::uint32_t enable;
    volatile std::uint64_t written;   // published after the record body; slot = (written-1) % kMaxRecords
    std::uint64_t seen;               // every inbound message the framer completed
    std::uint32_t flags;              // bit0 = framer hook installed
    std::uint32_t framerRva;          // resolved framer RVA (diag; 0 = unresolved)
    std::uint32_t diag[8];
    Record recs[kMaxRecords];
    volatile std::uint64_t chatWritten;   // published after each ChatRecord body
    std::uint64_t chatSeen;               // op-0x15 messages observed
    ChatRecord chat[kChatRecords];
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::netprobe
