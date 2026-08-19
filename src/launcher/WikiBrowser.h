#pragma once
// In-client wiki browser: a dedicated Ultralight window parented INTO the game host so it
// reads as an in-game panel, hard-locked to https://runescape.wiki. Enforcement is layered
// and fail-closed:
//   1. The load listener stops ANY main-frame navigation whose host is not runescape.wiki
//      (or a subdomain) before it renders, and returns to the last good page. This layer
//      cannot be disabled and is the guarantee behind "no other websites".
//   2. Every committed page gets a guard script injected: off-wiki link clicks and
//      window.open are neutralized before a request even starts, and Escape closes.
//   3. The network listener allowlists subresource hosts the same way (defense in depth;
//      the runtime gates request-blocking behind its Pro licence, so this layer is a
//      bonus where active, never the load-bearing wall).
// The wiki view is NEVER given the rtx JS bridge: remote content has zero API surface.
//
// All entry points main thread (AppCore) unless noted.

#include <cstdint>
#include <string>

namespace ultralight { class App; }

namespace rtx::launcher::wiki {

void Init(ultralight::App* app);

// Open (or refocus) the wiki panel inside `pid`'s host window. `term` empty = wiki home;
// otherwise a go-search: an exact title match lands directly on that page.
void Open(std::uint32_t pid, const std::string& term);
void Close(std::uint32_t pid);
bool IsOpen(std::uint32_t pid);

// Follow the host (position/size/visibility). Call from dock::Tick.
void Tick();

// Close windows whose host/client went away, and everything at shutdown.
void Shutdown();

// The palette hotkey (VK code, 0 = unbound). Persisted. Thread-safe.
int  KeybindVk();
void KeybindSet(int vk);

}  // namespace rtx::launcher::wiki
