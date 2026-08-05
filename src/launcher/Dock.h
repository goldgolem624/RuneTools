#pragma once
// Docked sidebar host: one host window per game client, with the Ultralight sidebar as a child
// on the right and the game window filling the rest (embed model documented in Dock.cpp).
//
// All entry points must be called on the AppCore main thread.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::dock {

// uiDevWatch: watch client_html_path's directory and hot-reload open panels on change (dev).
void Init(ultralight::App* app, std::string client_html_path, bool uiDevWatch = false);

// Create the docked panel window for `pid` (reserves space on first Tick). Idempotent.
void EnsureClient(std::uint32_t pid);

// Destroy the panel and restore the game window to its full width.
void RemoveClient(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);

// The embedded game window for `pid` (the reparented child) as an HWND, returned as void* so
// this header stays Windows-free; null when `pid` isn't embedded. Once embedded the game is a
// WS_CHILD window, so EnumWindows (top-level only) can no longer find it.
void* GameWindowHandle(std::uint32_t pid);

// Collapse the sidebar to an icon rail (or expand it), resizing the panel column so the
// game reclaims (or yields) the freed width.
void SetCollapsed(std::uint32_t pid, bool collapsed);

// Keep the client rendering at full rate when its window isn't focused. Loads the companion and
// re-pushes the flag until it takes. Off by default.
void SetKeepFocused(std::uint32_t pid, bool on);

// Rail tooltip: a top-most window drawn over the game, to the LEFT of a rail icon (the
// panel window can't draw tooltips out over the game beside it). clientX/clientY are the
// icon's position inside the panel window. Empty text hides it.
void ShowRailTip(std::uint32_t pid, const std::string& text, int clientX, int clientY);

// Text of a sibling UI asset (same directory as client.html), for data the page loads ON
// DEMAND rather than having spliced in at startup. `name` must be a bare file name - any
// path separator or "." segment is rejected. Empty string if absent or not readable.
std::string ReadUiAsset(const std::string& name);
void HideRailTip();

// Reposition/resize each panel to track its game window. Call from AppListener::OnUpdate.
void Tick();

// Restore all game windows + close all panels. Call once on shutdown.
void Shutdown();

}  // namespace rtx::launcher::dock
