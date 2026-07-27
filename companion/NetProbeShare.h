#pragma once
//
// Launcher <-> companion contract for DECODED server -> client packets.
//
// Capture point is the game's inbound framer: the function that reads one server
// message, ISAAC-deciphers its opcode, looks it up in the packet table and reads the
// declared-length payload. That yields the plaintext opcode + length + payload the
// client itself sees, unlike NetShare.h which sees raw ciphered ws2_32 bytes.
//
// The framer runs on the game's network/update thread; this is a single-writer
// lock-free ring, published seq-last so a reader that observes `written` may read
// records [written - kMaxRecords, written). Plain C-layout POD.

#include <cstdint>

namespace rtx::netprobe {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXNetProbe_v1_";
inline constexpr std::uint32_t kMagic   = 0x5450524E;   // 'NRPT'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr int kMaxRecords = 1024;    // ring depth (drained by the panel each poll)
// Payload bytes kept per record; sized for the largest packet worth decoding (0x5D
// rebuild_scene_dyn reaches ~880 bytes). Costs shared memory only: Record = 32 + kSnip.
inline constexpr int kSnip       = 1024;

// One decoded inbound message. `opcode` is the deciphered server opcode (0..0xE5);
// `length` is the resolved payload length in bytes; `kept` is how much landed in `data`.
struct Record {
    std::uint64_t tick;      // GetTickCount64 at capture
    std::uint64_t seq;       // monotonic record index (== its slot's written value + 1)
    std::int32_t  opcode;    // deciphered opcode, or -1 if the framer flagged it invalid
    std::int32_t  length;    // resolved payload length (bytes on the wire after the opcode)
    std::uint32_t kept;      // min(length, kSnip) actually copied into data
    std::uint32_t gtick;     // game tick counter at capture (0 if unavailable) -- groups a burst
    std::uint8_t  data[kSnip];
};

struct Share {
    std::uint32_t magic;              // kMagic once initialised
    std::uint32_t version;            // kVersion
    std::uint32_t pid;                // target client pid (sanity)
    // Launcher -> companion arm stamp (GetTickCount64 low 32 bits). The detour records
    // only while this is recent (see companion side); a closed panel leaves it to go
    // stale and the hook becomes a pass-through. 0 = never armed.
    volatile std::uint32_t enable;
    // Published AFTER each record body is filled.
    volatile std::uint64_t written;   // total records ever written; slot = (written-1) % kMaxRecords
    std::uint64_t seen;               // every inbound message the framer completed (incl. dropped-by-ring)
    std::uint32_t flags;              // bit0 = framer hook installed
    std::uint32_t framerRva;          // resolved framer RVA (diag; 0 = unresolved)
    // Diagnostics (companion-written, best-effort): [0]=framer calls, [1]=records written,
    // [2]=opcode-out-of-range skips, [3]=dup skips, [4..7]=spare.
    std::uint32_t diag[8];
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

}  // namespace rtx::netprobe
