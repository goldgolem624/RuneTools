#pragma once
//
// Native Windows notifications for alerts. Uses a Shell_NotifyIcon balloon, which Windows 10/11
// render as a modern toast in the Action Center -- no WinRT/AUMID/packaging needed (Shell32 is
// already linked). A single hidden tray icon (the app icon) is created lazily on the first
// notification and serves as the notification source for the session.
//

#include <string>

namespace rtx::winnotify {

// Show a Windows notification with `title` (bold first line) and `body` (detail). Both UTF-8.
// Returns false if the shell rejected it. Safe to call from the UI thread.
bool Show(const std::string& title, const std::string& body);

// Remove the tray icon + hidden window. Call once on launcher shutdown.
void Shutdown();

}  // namespace rtx::winnotify
