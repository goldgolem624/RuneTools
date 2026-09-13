#pragma once
// Rune Caches: one heartbeat a minute to runetools.io listing every character that is in game on
// this PC, sent only while the PC is linked to a RuneTools account. The website credits play time
// and decides when a cache drops; this side only reports and relays the answer to the in-game UI.
#include <cstdint>
#include <string>

namespace rtx::launcher::loot {

void Start();   // starts the heartbeat thread once (cheap no-op while not linked)

// Per-client poll from the in-game UI. Returns JSON:
//   {"linked":bool,"name":"...","seconds":n,"eligible":1200,"guaranteed":3600,"unopened":n,
//    "capped":bool,"drop":bool}
// "drop" is true exactly once per cache earned by that character (cleared by this call).
std::string PollJson(std::uint32_t pid);

}  // namespace rtx::launcher::loot
