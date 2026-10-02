#pragma once
// Muting NPC chatter at the game's own message store (contract in ChatShare.h).

namespace rtx::chatfilter {

bool Install();

// Re-create the share under the current session names. Cheap when unchanged.
void Rebind();

void Uninstall();

}  // namespace rtx::chatfilter
