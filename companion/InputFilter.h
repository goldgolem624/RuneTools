#pragma once
#include <windows.h>
//
// Window-message filter for render continuity. Subclasses the client window's message
// procedure to drop focus-loss notifications, so the client keeps rendering at full
// rate when unfocused instead of throttling to its low background rate.
//
// Gated by the launcher's keepFocused flag (RenderShare); off by default, in which case
// every message passes through unchanged. It never produces a message toward the
// client -- it only ever drops a focus-loss notification while the flag is set.

namespace rtx::winmsg {

// Idempotent per window; safe to call each frame.
bool Install(HWND hwnd);

void Uninstall();

// Appends to %USERPROFILE%\RuneToolsX\stutter-diag.log; thread-safe, truncates once
// per process load.
void DiagLogLine(const char* line);

}  // namespace rtx::winmsg
