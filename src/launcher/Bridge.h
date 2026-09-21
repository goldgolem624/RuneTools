#pragma once

#include <cstdint>
#include <Ultralight/Ultralight.h>

// JS<->C++ bridge attached on the launcher's main View, exposed as window.rtx.*.

namespace rtx::launcher {

void AttachBridge(ultralight::View* view);

// Launcher top-level HWND for winCmd (drag/min/close); set once from main.cpp.
void SetLauncherWindow(void* hwnd);

// Per-account unified-window placement, keyed on JX_DISPLAY_NAME.
void SaveWindowPos(std::uint32_t pid, int x, int y);
bool LoadWindowPos(std::uint32_t pid, int& x, int& y);

// Grand Exchange price of an item from the cached price relay: instant buy, else instant sell.
// 0 when the relay has no price for it (or has not answered yet; asking starts the fetch).
long long ItemGePrice(int item_id);

// Screenshot keybind (virtual-key, 0 = unbound); the Dock host proc fires the capture on key down.
int  ScreenshotVk();
int  HidePanelsVk();          // 0 when unbound
bool CaptureScreenshotForPid(std::uint32_t pid);

}  // namespace rtx::launcher
