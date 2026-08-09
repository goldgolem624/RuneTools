#pragma once
//
// Launcher side of the right-click menu channel (companion/MenuShare.h).
//
// The companion owns the section; this only opens it. Every call no-ops when the companion
// isn't in that client, so the panel can poll unconditionally.

#include <cstdint>
#include <string>

namespace rtx::launcher::menuswap {

// Publish the live entry list. `mode` is rtx::menu::kEnable*: 0 off, 1 panel open, 2 background.
// The companion's read walks pointers on the game's thread, so it stays off when there is neither
// a panel to draw nor a rule to apply - but it must stay ON for the latter, because the panel
// chooses which stored rule to send from the entities in the published menu.
bool SetEnabled(std::uint32_t pid, std::uint32_t mode);

// Ordered reorder rules, one per line as "verb<TAB>target". Line i draws above line i+1;
// anything unlisted keeps its existing relative order below them. An empty target applies to
// any object - rules are otherwise scoped, so reordering one thing's menu leaves others alone.
//
// A standing rule rather than a one-shot swap because the client REBUILDS the entry array every
// tick: a permutation written once is gone on the next frame (observed live). The companion
// reapplies this from inside the per-tick builder hook.
bool SetPins(std::uint32_t pid, const std::string& rulesNewlineSeparated);

// {"ok":bool,"hooked":bool,"enabled":bool,"seq":n,"reordered":n,"pins":["Bank",...],
//  "entries":[{"verb":"Bank","target":"<col=..>Bank booth","slot":n,"refs":n},...]}
// `entries` is in DISPLAY order (top line first); `slot` is the game's own array index, which
// runs the other way. "{}" when unavailable.
std::string StatusJson(std::uint32_t pid);

}  // namespace rtx::launcher::menuswap
