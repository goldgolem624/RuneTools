#pragma once
// Right-click menu probe; entry dumps gated on RTX_MENU_PROBE=1.
#include <cstddef>

namespace rtx::menuprobe {

bool Install();

void Poll();

// One boot record check line about the menu layout (what the hits named, whether the live records
// decode), for the log; false when there is nothing new.
bool TakeLog(char* out, std::size_t cap);

// Re-create the share under the current session names. Cheap when unchanged.
void Rebind();

void Uninstall();

}  // namespace rtx::menuprobe
