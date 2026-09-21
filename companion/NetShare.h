#pragma once
// Raw inbound socket bytes (ws2_32 recv/WSARecv), still ISAAC-ciphered; single-writer ring, `written` published last.

#include <cstdint>
#include "ShareName.h"

namespace rtx::net {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXNetData_v1_";

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}
inline constexpr std::uint32_t kMagic   = 0x454E5452;   // 'RTNE'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr int kMaxRecords = 2048;   // ring depth
inline constexpr int kSnip       = 512;    // bytes kept per record

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
    volatile std::uint64_t written;   // published after the body; readers may take [N - kMaxRecords, N)
    std::uint64_t bytesTotal;         // every inbound byte seen, independent of kSnip
    std::uint32_t flags;              // bit0 = recv hooked, bit1 = WSARecv hooked
    std::uint32_t _pad;
    Record recs[kMaxRecords];
};

}  // namespace rtx::net
