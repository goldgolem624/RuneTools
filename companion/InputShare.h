#pragma once
//
// Sidebar input channel. Launcher writes the panel "consume rect" + desired
// cursor; the in-client message pre-filter copies pointer/keyboard messages
// inside that rect into the ring and stops them (game never sees them); outside
// the rect, messages pass through untouched.
//
// INVARIANT: this channel only ever drops or passes through a real message and
// forwards a copy to the launcher's off-screen panel view. Nothing here is ever
// turned back into game input.
//
// SPSC ring (producer = module, consumer = launcher); head/tail are the only
// cross-thread mutable fields, updated with release/acquire order.

#include <cstdint>

namespace rtx::input {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXInput_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545849;   // 'RTXI'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kRingSize = 256;          // power of two

// One forwarded window message (raw Win32 fields; the launcher maps it to an
// Ultralight mouse/scroll/key event).
struct Event {
    std::uint32_t msg;         // WM_* identifier
    std::int32_t  x, y;        // cursor in client pixels (panel-local mapping done launcher-side)
    std::int64_t  wparam;
    std::int64_t  lparam;
    std::uint64_t t;           // GetTickCount64 at capture
};

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
    std::uint32_t magic;       // kMagic once initialised
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)

    // launcher -> module: authoritative consume state (client pixels).
    volatile std::int32_t  sidebar_x, sidebar_y, sidebar_w, sidebar_h;
    volatile std::uint32_t capture_keyboard;   // 1 = a panel field has focus; consume keys
    volatile std::uint32_t cursor_id;          // cursor to show while over the panel
    volatile std::uint32_t active;             // 1 = pre-filter engaged (panel visible)

    // module -> launcher: SPSC event ring.
    volatile std::uint32_t head;   // next write slot (producer = module)
    volatile std::uint32_t tail;   // next read slot  (consumer = launcher)
    Event events[kRingSize];
};

}  // namespace rtx::input
