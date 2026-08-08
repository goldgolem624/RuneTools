#pragma once
//
// Right-click menu probe: finds the menu manager and dumps its entry array so the 0x2E8 entry
// layout can be read off a live menu instead of guessed from a decompile.
//
// Diagnostic only. It attaches one detour that records a pointer and otherwise changes nothing,
// and it writes nothing into the client. Dumping is gated on RTX_MENU_PROBE=1 so a normal
// session pays for none of it.

namespace rtx::menuprobe {

// Idempotent. Hooks the manager's string-init so the manager pointer can be captured the next
// time the client builds one. False if the pattern could not be found, in which case nothing
// is hooked. Call from the companion worker thread.
bool Install();

// Dump the entry array if a menu is open and the layout has changed since the last dump.
// Cheap no-op when the probe is disabled, the manager is unknown, or the array is empty.
// Call from the worker loop.
void Poll();

void Uninstall();

}  // namespace rtx::menuprobe
