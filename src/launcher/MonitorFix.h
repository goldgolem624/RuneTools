#pragma once

// Works around a defect in the prebuilt Ultralight AppCore.dll: it caches the primary monitor's
// HMONITOR and calls GetMonitorInfoW on it whenever it sizes a window. An HMONITOR is not stable
// -- launching the game switches the display mode, invalidating the cached handle -- so the next
// window makes AppCore pop a modal "GetMonitorInfo failed" box and size from uninitialized geometry.
//
// InstallMonitorInfoFix() redirects AppCore.dll's import-table entry for
// GetMonitorInfoW to a wrapper that, on failure, re-resolves the live primary
// monitor and retries. Call once after AppCore.dll is loaded, before App::Create().

namespace rtx::launcher {

// Idempotent; silently no-ops if AppCore.dll isn't loaded or the import can't
// be found. Returns true when the redirect was installed.
bool InstallMonitorInfoFix();

}  // namespace rtx::launcher
