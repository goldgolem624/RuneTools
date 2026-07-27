// Render-control shared section: launcher writes toggles, module reads them.
// Launcher -> module (opposite direction from SceneShare/VarcShare). All toggles
// default to 0 (off); with nothing enabled the client renders exactly as normal.
#pragma once
#include <cstdint>

namespace rtx::render {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXRender_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545852;   // 'RTXR'
inline constexpr std::uint32_t kVersion = 1;

// Posted (never sent -- the client thread must not block on the launcher) by the in-process
// module to the host top-level window when the user presses a mouse button in the embedded
// client area, so the host can take real keyboard focus and route keys to the client.
inline constexpr std::uint32_t kMsgGameClicked = 0x8000 + 0x52;   // WM_APP + 'R'

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

struct Share {
    std::uint32_t magic;        // kMagic once mapped by the launcher
    std::uint32_t version;      // kVersion
    std::uint32_t pid;          // target client pid (sanity)
    // launcher -> module toggles (0 = render normally, 1 = hide)
    volatile std::uint32_t hideNpcs;
    volatile std::uint32_t hidePlayers;
    volatile std::uint32_t hideAll;     // blank the whole scene
    // launcher -> module: keep the client rendering at full rate when unfocused
    // (suppress focus-loss notifications it would throttle on). 0 = normal.
    volatile std::uint32_t keepFocused;
    // module -> launcher status bits: which capture points resolved/installed.
    // bit0 npc-skip, bit1 player-skip, bit2 scene-blank available.
    volatile std::uint32_t installed;
    // launcher -> module: client window is a true CHILD of the host with input queues
    // detached, so its queue never sees real keyboard focus. Module must then (a) notify
    // the host of clicks (kMsgGameClicked), (b) skip the client's own keyboard
    // translation (the host already translated and forwards WM_CHARs), and (c) mirror
    // forwarded keys into this queue's key state so GetKeyState keeps working.
    // Implies keepFocused.
    volatile std::uint32_t embedded;
    // module -> launcher: the window the client actually takes input on (render-target
    // child inside the client's frame window; same one the message filter sits on).
    // Host must relay keyboard HERE; the top-level frame ignores key messages.
    volatile std::uint64_t inputWindow;
};

}  // namespace rtx::render
