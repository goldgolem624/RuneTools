#pragma once
// Unified-window host: one host window per game client; the panel UI renders inside the game
// frame via the gameui module. All entry points must be called on the AppCore main thread.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::dock {

// uiDevWatch: watch client_html_path's directory and hot-reload open UI layers on change (dev).
void Init(ultralight::App* app, std::string client_html_path, bool uiDevWatch = false);

// client.html with every panel_*.js spliced in inline (LoadHTML has no base URL).
std::string BuildClientHtml();

// Idempotent; embeds on first Tick once the game window is ready.
void EnsureClient(std::uint32_t pid);

// Destroy the host + UI layer and restore the game to a standalone top-level window.
void RemoveClient(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);

// Embedded game HWND as void* (header stays Windows-free); null when not embedded. Once
// embedded the game is WS_CHILD, so EnumWindows cannot find it.
void* GameWindowHandle(std::uint32_t pid);

// True when the foreground window is the game for `pid` (or its host frame).
bool GameFocused(std::uint32_t pid);

// Scale from this process's physical pixels into the game's own pixel space (1.0 unless the
// game is DPI-virtualized). Thread-safe. Prefer the pid overload: it uses the measured
// companion backbuffer ratio; the hwnd-only form infers from DPI contexts and reads 1.0 once
// the game is an embedded child.
double GameSpaceFactor(void* gameHwnd);
double GameSpaceFactor(void* gameHwnd, std::uint32_t pid);

// Companion-reported client size in the game's pixel space; (0,0) clears. Thread-safe.
void PublishGameClientSize(std::uint32_t pid, int w, int h);

// Borderless fullscreen of the host window (no display-mode change). The game's own
// fullscreen option cannot work once it is a WS_CHILD.
void SetHostFullscreen(std::uint32_t pid, bool on);
void SetUiScaleMultiplier(std::uint32_t pid, double mul);   // Preferences UI scale (1.0 = DPI only)
bool IsHostFullscreen(std::uint32_t pid);

// Keep the client rendering at full rate when unfocused. Off by default.
void SetKeepFocused(std::uint32_t pid, bool on);

// Text of a sibling UI asset (same directory as client.html). `name` must be a bare file
// name; empty string if absent.
std::string ReadUiAsset(const std::string& name);

// Call from AppListener::OnUpdate.
void Tick();

// Restore all game windows + close all hosts. Call once on shutdown.
void Shutdown();

}  // namespace rtx::launcher::dock
