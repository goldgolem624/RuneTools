#pragma once
//
// Native Windows notifications for alerts, plus the tray icon. Uses a Shell_NotifyIcon
// balloon, which Windows 10/11 render as a modern toast in the Action Center -- no
// WinRT/AUMID/packaging needed (Shell32 is already linked). A single tray icon (the app
// icon) serves both roles: EnableTray registers it at startup and wires minimize-to-tray
// for the launcher window; Show reuses it as the notification source.
//

#include <string>

namespace rtx::winnotify {

// Show a Windows notification with `title` (bold first line) and `body` (detail). Both UTF-8.
// Returns false if the shell rejected it. Safe to call from the UI thread.
bool Show(const std::string& title, const std::string& body);

// Register the tray icon now (not lazily) and enable minimize-to-tray for `mainHwnd`
// (an HWND, passed as void* so this header stays Windows-free): minimizing that window
// hides it from the taskbar, a tray-icon click brings it back, and right-click offers
// Open / Exit. Call once from the main thread after the window exists -- the icon's
// callback messages arrive on the calling thread's message pump.
void EnableTray(void* mainHwnd);

// Remove the tray icon + hidden window. Call once on launcher shutdown.
void Shutdown();

}  // namespace rtx::winnotify
