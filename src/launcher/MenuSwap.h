#pragma once
// Launcher side of the right-click menu channel (companion/MenuShare.h). The companion owns
// the section; every call no-ops when it is absent, so the panel can poll unconditionally.

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::launcher::menuswap {

// `mode` is rtx::menu::kEnable*: 0 off, 1 panel open, 2 background. Must stay on while any
// rule is active: the panel picks the rule to send from the published menu.
bool SetEnabled(std::uint32_t pid, std::uint32_t mode);

// Ordered reorder rules, one per line as "verb<TAB>target" (empty target = any object). Line i
// draws above i+1. Standing rules, not one-shot swaps: the client rebuilds the entry array every tick.
bool SetPins(std::uint32_t pid, const std::string& rulesNewlineSeparated);

// {"ok":bool,"hooked":bool,"enabled":bool,"seq":n,"reordered":n,"pins":["Bank",...],"entries":[{"verb":"Bank","target":"<col=..>Bank booth","slot":n,"refs":n},...]}
// `entries` is in display order; `slot` is the game's array index, which runs the other way.
std::string StatusJson(std::uint32_t pid);

// A "Hide" row under the rows of each NPC and player. False when the companion cannot add rows.
bool SetHideRows(std::uint32_t pid, bool on);
// The last click on one: its count since the client started, the row kind (4 NPC, 7 player) and
// the actor it was added for. False when there is no channel.
bool HideClick(std::uint32_t pid, std::uint32_t& seq, int& kind, std::uint64_t& actor);
// A "Mark tile" row under "Walk here"; `marked` are the tiles that read "Unmark tile", as (x << 16) | y.
// False when the companion cannot add rows.
bool SetMarkRows(std::uint32_t pid, bool on, const std::vector<std::uint32_t>& marked);

// The row table (companion/MenuShare.h Row): one row per line as
// "label<TAB>labelOn<TAB>types<TAB>minVerbs<TAB>kind<TAB>name|name|...", types a bit mask by
// target type (1 << kType*). `perTarget` caps the rows of ours under one target (1..3).
// False when the companion cannot add rows.
bool SetRows(std::uint32_t pid, const std::string& rowsNewlineSeparated, std::uint32_t perTarget);

struct RowClick {
    std::uint32_t seq = 0, kind = 0, on = 0;
    int           type = -1;
    std::uint64_t actor = 0;
    std::uint32_t ints[3] = { 0, 0, 0 };
    std::string   target;
    std::vector<std::string> verbs;   // the target's own options as the menu showed them, JSON-escaped
};
// The last click on a row of the table. False when there is no channel.
bool LastRowClick(std::uint32_t pid, RowClick& out);

}  // namespace rtx::launcher::menuswap
