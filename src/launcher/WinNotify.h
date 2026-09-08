#pragma once
// Windows notifications (Shell_NotifyIcon balloons, rendered as toasts) plus the tray icon.

#include <string>

namespace rtx::winnotify {

// Show a notification (UTF-8). False if the shell rejected it. Safe from the UI thread.
bool Show(const std::string& title, const std::string& body);

// Register the tray icon and enable minimize-to-tray for `mainHwnd` (an HWND). Call once from
// the main thread: the icon's callback messages arrive on the calling thread's pump.
void EnableTray(void* mainHwnd);

// Call once on shutdown.
void Shutdown();

}  // namespace rtx::winnotify
