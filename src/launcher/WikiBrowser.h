#pragma once
// In-client wiki browser: an Ultralight window glued to the game host, hard-locked to https://runescape.wiki.
// Fail-closed layers: main-frame navigation lock, injected guard script, subresource allowlist (Pro licence gates the last). No rtx JS bridge in the wiki view. Main thread (AppCore) unless noted.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::wiki {

void Init(ultralight::App* app);

// Open (or refocus) the wiki panel for `pid`. Empty `term` = wiki home, else a go-search.
void Open(std::uint32_t pid, const std::string& term);
void Close(std::uint32_t pid);
bool IsOpen(std::uint32_t pid);

// Follow the host. Call from dock::Tick.
void Tick();

void Shutdown();

// The palette hotkey (VK code, 0 = unbound). Persisted. Thread-safe.
int  KeybindVk();
void KeybindSet(int vk);

}  // namespace rtx::launcher::wiki
