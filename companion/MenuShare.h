#pragma once
//
// Launcher <-> companion contract for the right-click menu.
//
// The companion has the menu manager hooked (see MenuProbe.cpp) and publishes the live entry
// list here so a panel can show what the game is about to draw, and ask for two entries to be
// swapped.
//
// Entries are published in DISPLAY order. The game stores them reversed - index 0 is the bottom
// line - and every consumer getting that backwards would be a bug per consumer, so the flip
// happens once, here, on publish.

#include <cstdint>

namespace rtx::menu {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXMenu_v1_";
inline constexpr std::uint32_t kMagic   = 0x554E454D;   // 'MENU'
inline constexpr std::uint32_t kVersion = 10;   // + handle, promotion diagnostics, entry type, source

// Where the promoted class came from. STRUCTURAL is the normal path and needs no gesture from
// the user: the promoted twin of a demoted op class is the same C++ type (same vtable) and
// entity ordinal, at the top of the promoted band. LEARNED means the behavioural cross-check
// (the class several different verbs share) disagreed and won - direct evidence beats structure,
// and seeing it here means the structural rule needs revisiting.
inline constexpr std::uint32_t kPromoSrcNone       = 0;
inline constexpr std::uint32_t kPromoSrcLearned    = 1;
inline constexpr std::uint32_t kPromoSrcStructural = 2;

// Entry type, from the action class's ordinal (class + 0x44). Same codes the hover-tooltip
// script switches on. It decides how a RULE MAY BE KEYED: an interface entry's "handle" is the
// packed (group<<16)|comp of the widget, and every backpack slot is a different comp, so keying
// an item rule on the handle silently binds it to the SLOT - the rule vanished when the item
// moved. Items key on their name instead.
inline constexpr std::int32_t kTypeGround    = 0;   // Walk here / Cancel (no target)
inline constexpr std::int32_t kTypeInterface = 1;
inline constexpr std::int32_t kTypeGroundObj = 2;
inline constexpr std::int32_t kTypeLoc       = 3;
inline constexpr std::int32_t kTypeNpc       = 4;
inline constexpr std::int32_t kTypePlayer    = 7;

// Why a demoted row (Drop / Examine) could or could not be given a promoted action class - see
// PromotePinnedEntry in MenuProbe.cpp. Reported in the panel so a refusal explains itself
// instead of needing a log dive: every one of these states was a real dead end during the work.
inline constexpr std::uint32_t kPromoIdle        = 0;   // nothing pinned, or no menu
inline constexpr std::uint32_t kPromoNotNeeded   = 1;   // the pinned row is already promoted
inline constexpr std::uint32_t kPromoApplied     = 2;   // class swapped: left-click should follow
inline constexpr std::uint32_t kPromoOtherClass  = 3;   // demoted, but not the class we have proven
inline constexpr std::uint32_t kPromoNoPartner   = 4;   // derived counterpart failed its fingerprint
inline constexpr std::uint32_t kPromoWriteFailed = 5;

// Share::enable. Both non-zero values publish; they differ only in whether a human is watching,
// which is what the diagnostic dump budget re-arms on. Kept as values of the existing field
// rather than a new one so an older companion still reads a truthy enable and keeps working.
inline constexpr std::uint32_t kEnableOff        = 0;
inline constexpr std::uint32_t kEnablePanel      = 1;   // panel on screen: publish + re-arm dumps
inline constexpr std::uint32_t kEnableBackground = 2;   // rules exist but panel closed: publish only

inline constexpr int kMaxEntries = 32;   // real menus are well under this
inline constexpr int kVerbLen    = 64;
inline constexpr int kTargetLen  = 128;  // targets carry colour tags and can be long
// Sized for every rule x its options, though the panel sends only the hovered entity's rules -
// it is the side that knows the config ids, and two same-named variants' pins in the list at
// once would let the first match win for the wrong one.
inline constexpr int kMaxPins    = 256;

// One reorder rule. `target` is the plain object/NPC/item name with colour tags stripped;
// empty means "any target", i.e. a global rule.
//
// No id here on purpose: the panel sends ONLY the rules for the entity under the cursor, so there
// is nothing for an id to disambiguate. An earlier version plumbed config ids through this
// struct, a handle->id table and a per-target array; it resolved inconsistently and was removed.
struct Pin {
    char verb[kVerbLen];
    char target[kTargetLen];
};

struct Entry {
    char          verb[kVerbLen];       // "Bank", "Load Last Preset from"
    char          target[kTargetLen];   // "<col=00ffff>Bank booth"; empty for Walk here / Cancel
    std::uint32_t refs;                 // action-object refcount, diagnostic
    std::int32_t  slot;                 // index in the game's array (reversed vs display)
    std::int32_t  type;                 // kType* above; -1 unknown
};

// flags (companion -> launcher)
inline constexpr std::uint32_t kFlagHooked = 1u << 0;

struct Share {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pid;
    std::uint32_t flags;

    // Launcher -> companion: publish the live entry list. The read walks pointers on the game's
    // thread, so it costs nothing when nobody is looking - but "nobody is looking" is NOT the
    // same as "the panel is closed". The panel picks WHICH stored rule to send from the entities
    // in the published menu, so with publishing off it is choosing from a frozen snapshot and a
    // newly hovered thing never gets its rule pushed - reordering silently stopped whenever the
    // panel was not open. kEnableBackground keeps the feed alive for that, and is distinct from
    // kEnablePanel only so the probe can still tell the two apart (see Poll's dump re-arm).
    volatile std::uint32_t enable;      // kEnable* below

    volatile std::uint32_t seq;         // bumped after a publish completes
    std::uint32_t count;                // valid entries[0..count), DISPLAY order
    Entry entries[kMaxEntries];

    // Launcher -> companion: verbs to pull to the TOP of the menu, in this order.
    //
    // This is a rule rather than a one-shot swap because the client REBUILDS the entry array
    // every tick - a permutation written once is gone on the next frame (observed live). The
    // companion therefore reapplies this ordering from inside the per-tick builder hook, so
    // what the game draws is always the reordered list.
    //
    // An ORDERED rule list: entry i should draw above entry i+1. Anything not listed keeps its
    // existing relative order, below the listed ones. Pinning one verb to the top is just a
    // one-element list, so ordering and pinning are the same mechanism.
    //
    // Rules are SCOPED TO A TARGET by default: {verb "Teleport", target "Event noticeboard"}
    // only touches that object's menu. An empty target means "any", which has to be asked for -
    // matching on the verb alone silently reordered every object that shared it.
    volatile std::uint32_t pinSeq;      // bumped by the launcher after editing the list
    std::uint32_t          pinCount;
    Pin                    pins[kMaxPins];

    // [0] publishes, [1] reorders applied, [2] builder-hook calls, [3] calls with pins set
    std::uint32_t diag[4];
    // Where the reorder stops, so "0 applied" names the stage instead of leaving five
    // candidates: [0] entry vector ok, [1] a verb matched a pin, [2] permutation built,
    // [3] it differed from the current order.
    std::uint32_t stage[4];
    char          lastVerb[kVerbLen];   // the last verb the matcher compared, verbatim
    // Per-lane reorder counts: [0] menu (0x90), [1] targeted (0x6e0), [2] all-but-cancel
    // (0xd30), [3] misc (0x13a0). The left-click action is fed by one of the siblings rather
    // than by the menu list, so these say which lanes a change actually landed in.
    std::uint32_t lane[6];
    // The hovered entity's own handle, from the hover-target block's +0x000: packed (x<<16)|y for
    // a world object, a plain index for an actor. Distinct objects SHARE A NAME - the Shifting
    // tombs entrances are locs 109059 / 109062 / 109065, each with a different option set - so a
    // rule keyed on the name alone gets applied to the wrong one.
    std::uint32_t handle;
    // What the latch currently holds, so a shrinking rebuild of the SAME object cannot replace
    // a fuller capture (the menu decays as the cursor leaves, and a one-option remnant was
    // overwriting the menu being edited).
    char          lastTarget[kTargetLen];
    std::uint32_t lastTargeted;

    // Class-promotion diagnostics, one of kPromo* above plus the numbers behind the verdict:
    // the class priority of the row we wanted on top, and the priority found where the promoted
    // counterpart should be (0 = unreadable). Both are what a refusal needs to be actionable.
    std::uint32_t promoState;
    std::int32_t  promoPrio;
    std::int32_t  promoPartnerPrio;
    std::uint32_t promoSource;          // kPromoSrc*
    char          promoVerb[kVerbLen];
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

}  // namespace rtx::menu
