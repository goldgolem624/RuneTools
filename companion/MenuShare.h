#pragma once
// Launcher <-> companion contract for the right-click menu; entries published in DISPLAY order.

#include <cstdint>
#include "ShareName.h"

namespace rtx::menu {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXMenu_v1_";
inline constexpr std::uint32_t kMagic   = 0x554E454D;   // 'MENU'
inline constexpr std::uint32_t kVersion = 10;

inline constexpr std::uint32_t kPromoSrcNone       = 0;
inline constexpr std::uint32_t kPromoSrcLearned    = 1;
inline constexpr std::uint32_t kPromoSrcStructural = 2;

// Entry type = action class ordinal (class + 0x44); interface handles are (group<<16)|comp.
inline constexpr std::int32_t kTypeGround    = 0;   // Walk here / Cancel (no target)
inline constexpr std::int32_t kTypeInterface = 1;
inline constexpr std::int32_t kTypeGroundObj = 2;
inline constexpr std::int32_t kTypeLoc       = 3;
inline constexpr std::int32_t kTypeNpc       = 4;
inline constexpr std::int32_t kTypePlayer    = 7;

inline constexpr std::uint32_t kPromoIdle        = 0;
inline constexpr std::uint32_t kPromoNotNeeded   = 1;
inline constexpr std::uint32_t kPromoApplied     = 2;
inline constexpr std::uint32_t kPromoOtherClass  = 3;
inline constexpr std::uint32_t kPromoNoPartner   = 4;
inline constexpr std::uint32_t kPromoWriteFailed = 5;
inline constexpr std::uint32_t kPromoUnverified  = 6;   // the display record did not carry the expected class tag: not written
inline constexpr std::uint32_t kPromoLifted      = 7;   // rule's top row moved to the top after the game's sort; left-click slots refreshed
inline constexpr std::uint32_t kPromoNoAssign    = 8;   // lift wanted but the game's slot-assign helper was not found on this build

inline constexpr std::uint32_t kEnableOff        = 0;
inline constexpr std::uint32_t kEnablePanel      = 1;   // publish + re-arm dumps
inline constexpr std::uint32_t kEnableBackground = 2;   // publish only

inline constexpr int kMaxEntries = 32;
inline constexpr int kVerbLen    = 64;
inline constexpr int kTargetLen  = 128;
inline constexpr int kMaxPins    = 256;

struct Pin {
    char verb[kVerbLen];
    char target[kTargetLen];
};

struct Entry {
    char          verb[kVerbLen];
    char          target[kTargetLen];   // colour-tagged; empty for Walk here / Cancel
    std::uint32_t refs;
    std::int32_t  slot;                 // index in the game's array (reversed vs display)
    std::int32_t  type;                 // kType*; -1 unknown
};

inline constexpr std::uint32_t kFlagHooked = 1u << 0;
// Set when a reorder was withheld because the live records did not pass the structural check
// (every row must decode a verb). Cleared on the next write that passes.
inline constexpr std::uint32_t kFlagUnverified = 1u << 1;

struct Share {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pid;
    std::uint32_t flags;

    volatile std::uint32_t enable;      // kEnable*; must stay non-zero while rules exist

    volatile std::uint32_t seq;         // bumped after a publish completes
    std::uint32_t count;                // valid entries[0..count), DISPLAY order
    Entry entries[kMaxEntries];

    volatile std::uint32_t pinSeq;      // odd/even seqlock, launcher-written
    std::uint32_t          pinCount;
    Pin                    pins[kMaxPins];

    std::uint32_t diag[4];              // [0] publishes, [1] reorders, [2] builder-hook calls, [3] calls with pins
    std::uint32_t stage[4];             // [0] vector ok, [1] verb matched, [2] permutation built, [3] differed
    char          lastVerb[kVerbLen];
    std::uint32_t lane[6];              // reorders: [0] 0x90, [1] 0x6e0, [2] 0xd30, [3] 0x13a0
    std::uint32_t handle;               // hover block +0x000: (x<<16)|y world object, index actor
    char          lastTarget[kTargetLen];
    std::uint32_t lastTargeted;

    std::uint32_t promoState;           // kPromo*
    std::int32_t  promoPrio;
    std::int32_t  promoPartnerPrio;     // 0 = unreadable
    std::uint32_t promoSource;          // kPromoSrc*
    char          promoVerb[kVerbLen];
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::menu
