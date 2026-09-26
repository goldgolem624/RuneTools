#pragma once

#include <cstdint>
#include <string>

// Launcher logging under %USERPROFILE%\RuneToolsX\logs\: launcher.log (process-wide) and
// client-<pid>.log (per game process, lazy). Lines are timestamped and profile-path redacted.

namespace rtx::log {

// Call once at process start. Opens launcher.log for appending and installs the crash handlers.
// Never removes or truncates anything: any process may call it, including a second launcher
// instance or a headless switch, while the running launcher's logs stay intact.
void Init();

// Called by the one launcher instance once it holds the instance mutex: the previous run's
// launcher.log becomes launcher.prev.log, a fresh launcher.log starts, and earlier sessions'
// client logs and stale companion logs are purged.
void StartRun();

// Call at the start of every shutdown path (normal close and updater self-terminate):
// suppresses crash reports for teardown-race faults on background threads.
void BeginShutdown();

void Launcher(const std::string& msg);

void Client(std::uint32_t pid, const std::string& msg);

// Close a per-PID stream; the file stays on disk and a later write reopens it.
void CloseClient(std::uint32_t pid);

// Replace the user's profile path with %USERPROFILE%.
std::string Redact(const std::string& msg);

std::wstring LogDir();

}  // namespace rtx::log
