#pragma once
// In-game window UI host: one off-screen transparent Ultralight View per game
// client, rendered on the CPU, published into the companion's FrameShare v2
// section and composited inside the game frame by the present hook. Input
// arrives from the companion's InputShare v2 ring (game-window messages the
// pre-filter consumed over our UI regions) and is replayed into the View.
//
// All entry points must be called on the AppCore main thread unless noted.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::gameui {

// Posted to the HOST window by the input waiter thread when the companion has
// enqueued UI events; the host WndProc dispatches to DrainInput on the main
// thread. wParam = pid. WM_APP range, beside kMsgGameClicked / kMsgEmbedDone.
inline constexpr unsigned kMsgUiInput = 0x8000 + 0x54;

// Host SetTimer id for the UI pump (16 ms while the layer is active/animating;
// killed when idle so an idle client costs nothing beyond the 100 ms keep-alive).
inline constexpr unsigned kUiPumpTimerId = 3;

void Init(ultralight::App* app);

// Create the off-screen View for `pid` and start loading `html` (the spliced
// client.html). Safe before the host window exists; the layer stays invisible
// until Bind. Idempotent.
void Prepare(std::uint32_t pid, const std::string& html, double initialScale);

// Attach the prepared layer to the client's host window: creates the shared
// sections + wake event, starts the input waiter, arms the pump timer.
void Bind(std::uint32_t pid, void* hostHwnd);

// Quiesce (visible=0/active=0), stop the waiter, release the View + mappings.
void Destroy(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);

// Per run-loop-iteration work: resize handshake, dirty-surface publish, pump
// timer management. Call from dock::Tick.
void Tick();

// Drain the companion's event ring into the View (main thread; host WndProc
// handler for kMsgUiInput).
void DrainInput(std::uint32_t pid);

// Host-relayed keyboard while the UI holds keyboard capture. Returns true when
// the key was delivered to the View (caller must not relay it to the game).
bool FireHostKey(std::uint32_t pid, unsigned msg, std::uintptr_t wparam, std::intptr_t lparam);
bool KeyboardCaptured(std::uint32_t pid);

// Monitor scale for the layer's View (from the host's WM_DPICHANGED).
void SetDeviceScale(std::uint32_t pid, double scale);
void TogglePanels(std::uint32_t pid);   // hide/show-all-panels hotkey -> page's wmToggleAll()

// ---- Bridge surface (JS window manager) -------------------------------------
// rects: "x,y,w,h;x,y,w,h;..." in CSS px (scaled to physical px here).
// visible: whether the companion should composite the layer at all.
void SetConsumeRects(std::uint32_t pid, const std::string& rectsCsv, bool visible);
void SetKeyboardCapture(std::uint32_t pid, bool on);
// {"pw","ph" physical client px, "cw","ch" CSS px, "scale", "mod" companion-alive}
std::string ClientInfoJson(std::uint32_t pid);

// Dev hot-reload: replace the layer's HTML (re-spliced client.html).
void ReloadHtml(std::uint32_t pid, const std::string& html);

// Raise an in-game alert card through the UI layer's notification system (uiNotify in
// client.html): the same styled, animated cards every panel alert uses. ttl_ms 0 =
// sticky until dismissed. Returns false when the layer isn't up (caller falls back to
// the legacy in-frame toast so keybind feedback never goes silent). Main thread.
bool Notify(std::uint32_t pid, const std::string& msg, int ttl_ms);

// Open the wiki search palette (dimmed overlay + input) in the UI layer. Main thread.
void OpenWikiPalette(std::uint32_t pid);

}  // namespace rtx::launcher::gameui
