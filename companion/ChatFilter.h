#pragma once
// Muting NPC chatter at the game's own message store (contract in ChatShare.h).

#include <cstdint>

namespace rtx::chatfilter {

bool Install();

// Re-create the share under the current session names. Cheap when unchanged.
void Rebind();

void Uninstall();

// The store's second index as the notify hook sees it: lines observed, how often the tree size
// at store+0x40 differed from the previous line, its first and latest values.
void TreeStats(std::uint32_t& lines, std::uint32_t& changes, std::int64_t& first, std::int64_t& last);

}  // namespace rtx::chatfilter
