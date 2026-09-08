#pragma once

// AppCore.dll caches the primary HMONITOR, which a display-mode switch invalidates. This patches
// its IAT entry for GetMonitorInfoW with a retrying wrapper. Call after AppCore.dll loads, before App::Create().

namespace rtx::launcher {

// Idempotent; returns true when the redirect was installed.
bool InstallMonitorInfoFix();

}  // namespace rtx::launcher
