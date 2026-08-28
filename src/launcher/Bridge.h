#pragma once

#include <cstdint>
#include <Ultralight/Ultralight.h>

// JS<->C++ bridge attached on the launcher's main View. Surface at
// `window.rtx.*` (all anonymous; no account or API key required):
//   scanProcesses                       -> JSON [{pid,name,x64,active,display_name,character_id,has_env}]
//   listAccounts                        -> JSON [{id,display_name,character_id,captured_at}]
//   removeAccount(id) / launchAccount(id)
//   openClientWindow(pid,title) / closeClientWindow(pid) / clientWindowOpen(pid)
//   version / openLog / pasteClipboard / copyClipboard(text)

namespace rtx::launcher {

void AttachBridge(ultralight::View* view);

// Per-account unified-window placement (top-left screen position), keyed on JX_DISPLAY_NAME.
// LoadWindowPos returns false when nothing is stored (or the account is unresolved).
void SaveWindowPos(std::uint32_t pid, int x, int y);
bool LoadWindowPos(std::uint32_t pid, int& x, int& y);

// Screenshot: the capture keybind (virtual-key, 0 = unbound) and a capture trigger. The Dock host
// proc fires CaptureScreenshotForPid on a matching key down-edge; PNGs land in RuneToolsX\screenshots.
int  ScreenshotVk();
int  HidePanelsVk();          // Preferences hide/show-all-panels key; 0 when unbound
bool CaptureScreenshotForPid(std::uint32_t pid);

}  // namespace rtx::launcher
