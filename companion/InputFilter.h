#pragma once
#include <windows.h>
// Window-message filter: subclasses the client WndProc; gated by RenderShare keepFocused, off = pass-through.

namespace rtx::winmsg {

bool Install(HWND hwnd);

void Uninstall();

void DiagLogLine(const char* line);

}  // namespace rtx::winmsg
