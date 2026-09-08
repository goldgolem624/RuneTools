#pragma once
// In-game window UI host: one off-screen Ultralight View per game client, CPU-rendered,
// published into the companion's FrameShare v2 section and composited by the present hook.
// Input comes from the companion's InputShare v2 ring. Main thread unless noted.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::gameui {

// Posted to the host window by the input waiter thread; wParam = pid. WM_APP range, beside
// kMsgGameClicked / kMsgEmbedDone.
inline constexpr unsigned kMsgUiInput = 0x8000 + 0x54;

// Host SetTimer id for the UI pump (16 ms while active; killed when idle).
inline constexpr unsigned kUiPumpTimerId = 3;

void Init(ultralight::App* app);

// Create the off-screen View and start loading `html`. Safe before the host exists. Idempotent.
void Prepare(std::uint32_t pid, const std::string& html, double initialScale);

// Attach the prepared layer to the host window: shared sections, wake event, waiter, pump timer.
void Bind(std::uint32_t pid, void* hostHwnd);

void Destroy(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);

// Call from dock::Tick.
void Tick();

// Host WndProc handler for kMsgUiInput.
void DrainInput(std::uint32_t pid);

// Host-relayed keyboard while the UI holds capture. True when delivered to the View (caller
// must not relay it to the game).
bool FireHostKey(std::uint32_t pid, unsigned msg, std::uintptr_t wparam, std::intptr_t lparam);
bool KeyboardCaptured(std::uint32_t pid);

// Monitor scale for the layer's View (from the host's WM_DPICHANGED).
void SetDeviceScale(std::uint32_t pid, double scale);
void TogglePanels(std::uint32_t pid);   // hide/show-all-panels hotkey -> page's wmToggleAll()

// ---- Bridge surface (JS window manager) -------------------------------------
// rects: "x,y,w,h;..." in CSS px. visible: whether the companion composites the layer at all.
void SetConsumeRects(std::uint32_t pid, const std::string& rectsCsv, bool visible);
void SetKeyboardCapture(std::uint32_t pid, bool on);
// {"pw","ph" physical client px, "cw","ch" CSS px, "scale", "mod" companion-alive}
std::string ClientInfoJson(std::uint32_t pid);

// Dev hot-reload.
void ReloadHtml(std::uint32_t pid, const std::string& html);

// In-game alert card via the page's uiNotify. ttl_ms 0 = sticky. False when the layer isn't up
// (caller falls back to the legacy in-frame toast).
bool Notify(std::uint32_t pid, const std::string& msg, int ttl_ms);

// Open the wiki search palette in the UI layer.
void OpenWikiPalette(std::uint32_t pid);

}  // namespace rtx::launcher::gameui
