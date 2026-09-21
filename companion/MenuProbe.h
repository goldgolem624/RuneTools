#pragma once
// Right-click menu probe; entry dumps gated on RTX_MENU_PROBE=1.

namespace rtx::menuprobe {

bool Install();

void Poll();

// Re-create the share under the current session names. Cheap when unchanged.
void Rebind();

void Uninstall();

}  // namespace rtx::menuprobe
