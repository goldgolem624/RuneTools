#pragma once

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::gameui {

// Posted to the host window by the input waiter thread; wParam = pid. WM_APP range, beside kMsgGameClicked / kMsgEmbedDone.
inline constexpr unsigned kMsgUiInput = 0x8000 + 0x54;

inline constexpr unsigned kUiPumpTimerId = 3;

void Init(ultralight::App* app);

void Prepare(std::uint32_t pid, const std::string& html, double initialScale);

void Bind(std::uint32_t pid, void* hostHwnd);

void Destroy(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);
// The module says it is drawing the launcher's component rectangles through the game (see EngineComponents.h).
bool ModuleDrawsComponents(std::uint32_t pid);
// The game's own screen points for the world points last asked about, newest set only.
struct ModulePoint { std::int32_t x, y, depth, ok; std::uint32_t tag; };
int ModuleAnchors(std::uint32_t pid, ModulePoint* out, int cap);
// The label font's glyph widths as the module measured them, in the pixels of the size it reports.
// False until the module has drawn its first label.
bool ModuleGlyphWidths(std::uint32_t pid, std::uint8_t* adv, int count, int& px);

void Tick();

void DrainInput(std::uint32_t pid);

bool FireHostKey(std::uint32_t pid, unsigned msg, std::uintptr_t wparam, std::intptr_t lparam);
bool KeyboardCaptured(std::uint32_t pid);

void SetDeviceScale(std::uint32_t pid, double scale);
void TogglePanels(std::uint32_t pid);   // hide/show-all-panels hotkey -> page's wmToggleAll()

void SetConsumeRects(std::uint32_t pid, const std::string& rectsCsv, bool visible);
void SetKeyboardCapture(std::uint32_t pid, bool on);
std::string ClientInfoJson(std::uint32_t pid);

void ReloadHtml(std::uint32_t pid, const std::string& html);

bool Notify(std::uint32_t pid, const std::string& msg, int ttl_ms);

void OpenWikiPalette(std::uint32_t pid);

}  // namespace rtx::launcher::gameui
