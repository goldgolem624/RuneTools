#pragma once

#include <cstdint>
#include "ShareName.h"

// Launcher <-> module channel for muting NPC chatter in the chat window. The module owns the
// section; the launcher writes the names and reads the counters.
namespace rtx::chatmute {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXChat_v1_";
inline constexpr std::uint32_t kMagic   = 0x43585452;   // 'RTXC'
inline constexpr std::uint32_t kVersion = 1;

inline constexpr int kMaxNames  = 64;    // muted senders
inline constexpr int kNameChars = 40;    // each, with the terminator
inline constexpr int kMaxRecent = 24;    // senders heard, for the panel to offer

inline constexpr std::uint32_t kFlagHooked = 1u << 0;   // the notify routine is hooked

// mode: 0 = count only, 1 = a muted line is never listed for the chat window (the window still
// finds it in the store), 2 = it is also taken out of the store, as the game drops old lines
inline constexpr std::uint32_t kModeOff = 0, kModeSkip = 1, kModeUnlink = 2;

struct Share {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pid;
    std::uint32_t flags;

    volatile std::uint32_t enable;      // launcher -> module; 0 = pass-through
    volatile std::uint32_t mode;        // kMode*
    std::uint32_t notifyRva;            // the hooked routine, for the log

    // Names in lower case, written under nameSeq (odd = mid-update). "*" mutes every NPC.
    volatile std::uint32_t nameSeq;
    volatile std::uint32_t nameCount;
    char names[kMaxNames][kNameChars];

    // Senders heard, newest at (recentSeq - 1) % kMaxRecent.
    volatile std::uint32_t recentSeq;
    char recent[kMaxRecent][kNameChars];

    std::uint32_t diag[8];              // 0 lines seen, 1 NPC lines, 2 muted, 3 text not readable
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::chatmute
