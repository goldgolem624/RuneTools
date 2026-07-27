#pragma once
//
// Tooling <-> companion contract for RAW INBOUND SOCKET BYTES.
//
// Captures ws2_32 recv/WSARecv as they return, before the client decodes anything, so
// a record holds whatever the wire carried. Opcodes are stream-ciphered at this point
// and are not readable here; use NetProbeShare.h for decoded messages. This channel is
// for volume and timing correlation against in-game events.
//
// Plain C-layout POD. Single writer (the client's network thread), lock-free ring.

#include <cstdint>

namespace rtx::net {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXNetData_v1_";
inline constexpr std::uint32_t kMagic   = 0x454E5452;   // 'RTNE'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxRecords = 2048;   // ring depth
inline constexpr int kSnip       = 512;    // bytes kept per record

// `sock` separates concurrent connections: the client also talks to the lobby and
// JS5 servers, whose traffic must not be mistaken for game traffic.
struct Record {
    std::uint64_t tick;      // GetTickCount64 at return
    std::uint64_t sock;      // SOCKET
    std::int32_t  ret;       // bytes returned; 0 = closed, <0 = error
    std::uint32_t kept;      // min(ret, kSnip)
    std::uint32_t api;       // 1 = recv, 2 = WSARecv
    std::uint32_t _pad;
    std::uint8_t  data[kSnip];
};

struct Share {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pid;
    volatile std::uint32_t enable;    // 0 = hooks pass straight through, nothing recorded
    // Published AFTER the record body is filled, so a reader that observes N may
    // read records [0, N). The oldest surviving record is N - kMaxRecords.
    volatile std::uint64_t written;
    std::uint64_t bytesTotal;         // every inbound byte seen, independent of kSnip
    std::uint32_t flags;              // bit0 = recv hooked, bit1 = WSARecv hooked
    std::uint32_t _pad;
    Record recs[kMaxRecords];
};

}  // namespace rtx::net
