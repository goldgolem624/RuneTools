#pragma once
// Unified-window host: one host window per game client, with the game filling the entire host
// client area. The panel UI renders INSIDE the game frame via the gameui module (embed model
// documented in Dock.cpp).
//
// All entry points must be called on the AppCore main thread.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::dock {

// uiDevWatch: watch client_html_path's directory and hot-reload open UI layers on change (dev).
void Init(ultralight::App* app, std::string client_html_path, bool uiDevWatch = false);

// client.html with every panel_*.js spliced in as inline <script> blocks (LoadHTML has no base
// URL, so relative <script src> can never resolve). Used for the in-game UI layer + hot reload.
std::string BuildClientHtml();

// Create the unified host + in-game UI layer for `pid` (embeds on first Tick when the game
// window is ready). Idempotent.
void EnsureClient(std::uint32_t pid);

// Destroy the host + UI layer and restore the game window to a standalone top-level window.
void RemoveClient(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);

// The embedded game window for `pid` (the reparented child) as an HWND, returned as void* so
// this header stays Windows-free; null when `pid` isn't embedded. Once embedded the game is a
// WS_CHILD window, so EnumWindows (top-level only) can no longer find it.
void* GameWindowHandle(std::uint32_t pid);

// Keep the client rendering at full rate when its window isn't focused. Loads the companion and
// re-pushes the flag until it takes. Off by default.
void SetKeepFocused(std::uint32_t pid, bool on);

// Text of a sibling UI asset (same directory as client.html), for data the page loads ON
// DEMAND rather than having spliced in at startup. `name` must be a bare file name - any
// path separator or "." segment is rejected. Empty string if absent or not readable.
std::string ReadUiAsset(const std::string& name);

// Embed retries, dead-client cleanup, companion flag pushes, UI-layer publish. Call from
// AppListener::OnUpdate.
void Tick();

// Restore all game windows + close all hosts. Call once on shutdown.
void Shutdown();

}  // namespace rtx::launcher::dock
