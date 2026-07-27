#pragma once
//
// Present callback: runs the compositor immediately before the system buffer-swap so
// the launcher-published UI is drawn into the game frame, then calls the original
// swap through.

namespace rtx::present {

// Idempotent. Returns false if the GL entry point could not be located, in which case
// compositing stays off.
bool Install();

// Render-thread safe.
void Uninstall();

}  // namespace rtx::present
