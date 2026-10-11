#pragma once
// Launcher side of the right-click menu channel (companion/MenuShare.h). The companion owns
// the section; every call no-ops when it is absent, so the panel can poll unconditionally.

#include <cstdint>
#include <string>

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

}  // namespace rtx::launcher::menuswap
