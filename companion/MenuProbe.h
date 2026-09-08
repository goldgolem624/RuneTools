#pragma once
// Right-click menu probe; entry dumps gated on RTX_MENU_PROBE=1.

namespace rtx::menuprobe {

bool Install();

void Poll();

void Uninstall();

}  // namespace rtx::menuprobe
