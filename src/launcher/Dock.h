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

// True when the foreground window is the game for `pid` (or the host frame it is embedded in).
bool GameFocused(std::uint32_t pid);

// Scale from this process's physical pixels into `game`'s own pixel space -- the space its
// backbuffer, memory-read viewport values and in-frame drawing all use. 1.0 in the normal
// case (the game's DPI context matches its monitor); less when the game runs DPI-virtualized,
// e.g. a DPI-unaware client on a 125% display renders at 96 DPI and DWM upscales it, so its
// space is smaller than the physical client rect this process measures. Thread-safe (any
// thread), unlike the rest of this header.
//
// The hwnd-only form INFERS the factor from DPI contexts. That inference LIES once the game
// is our embedded WS_CHILD: the child adopts the host's per-monitor-v2 context, so both DPIs
// read the monitor's value and the factor collapses to 1.0 while the game process is still
// virtualized and rendering a smaller backbuffer (seen live: every overlay at 2/3 scale on a
// 150% display). Prefer the pid overload wherever a pid is known: it uses the MEASURED ratio
// companion-backbuffer-size / physical-client-size (ground truth, published every present via
// PublishGameClientSize) and only falls back to the DPI inference while the companion is absent.
double GameSpaceFactor(void* gameHwnd);
double GameSpaceFactor(void* gameHwnd, std::uint32_t pid);

// Publish the companion-reported live client size (the game's own GetClientRect, i.e. its
// backbuffer size in ITS pixel space) for cross-thread GameSpaceFactor consumers. Called from
// gameui::Tick while the companion heartbeat is fresh; (0,0) clears. Thread-safe.
void PublishGameClientSize(std::uint32_t pid, int w, int h);

// Borderless fullscreen for the client's host window: it takes the whole monitor it is on, frame
// and taskbar included, and the embedded game (which fills the host client area) comes with it.
// The GAME's own Screen Sizing / Fullscreen option cannot work once embedded -- it is a WS_CHILD,
// clipped to our client area -- so this is the fullscreen for an embedded client. No display-mode
// change, so alt-tab and multi-client are unaffected. Leaving restores the exact previous frame
// and placement.
void SetHostFullscreen(std::uint32_t pid, bool on);
void SetUiScaleMultiplier(std::uint32_t pid, double mul);   // Preferences UI scale (1.0 = DPI only)
bool IsHostFullscreen(std::uint32_t pid);

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
