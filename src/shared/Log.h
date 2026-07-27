#pragma once

#include <cstdint>
#include <string>

// Process logging for the launcher. Two destinations under
// %USERPROFILE%\RuneToolsX\logs\:
//
//   launcher.log        process-wide events (boot, runtime load, spawn, fatals)
//   client-<pid>.log    per game process; created lazily on first write
//
// Per-PID logs from previous sessions are purged on Init() so each run starts
// clean, but a session's own logs are kept until the next launch (a crashed
// client's log survives for inspection). All writers timestamp lines and
// redact the user's profile path.

namespace rtx::log {

// Call once at process start, before any other logging. Creates the logs
// directory, deletes stale per-PID logs from earlier sessions, and opens a
// fresh launcher.log.
void Init();

// Mark that the process is intentionally tearing down. Call at the start of the
// shutdown sequence (normal close AND the updater's self-terminate). After this,
// the crash handler stops writing crash-*.txt/.dmp: background threads (Ultralight
// render/GC, overlay, the game-memory reader) get killed mid-teardown and can fault
// in the race just before the kill lands. Those are benign shutdown crashes, not
// in-use faults -- and the minidump would be empty anyway since the process is
// already going away. Suppressing them means a crash file only ever records a real,
// live-session crash worth investigating.
void BeginShutdown();

// Process-wide event -> launcher.log.
void Launcher(const std::string& msg);

// Per game process event -> client-<pid>.log (created on first call).
void Client(std::uint32_t pid, const std::string& msg);

// Flush and close a per-PID stream when its process goes away. The file is
// left on disk (kept for the rest of this session); a later write for the
// same PID reopens it.
void CloseClient(std::uint32_t pid);

// Replace any occurrence of the user's profile path with %USERPROFILE%.
// Exposed for callers that surface paths outside the log (e.g. message boxes).
std::string Redact(const std::string& msg);

// Absolute path of the logs directory (for "Open log" in the UI).
std::wstring LogDir();

}  // namespace rtx::log
