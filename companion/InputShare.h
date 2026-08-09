#pragma once
//
// UI-layer input channel. The launcher publishes the set of screen regions the
// in-game window UI currently claims (window rects + menu bar, in client
// pixels) plus capture flags; the in-client message pre-filter copies
// pointer/keyboard messages that hit those regions into the ring and stops them
// (the game never sees them); outside the regions, messages pass through
// untouched. A consumed button-down starts module-side mouse capture: every
// mouse message is consumed until the matching button-up, so window drags never
// leak into the game.
//
// INVARIANT: this channel only ever drops or passes through a real message and
// forwards a copy to the launcher's off-screen UI view. Nothing here is ever
// turned back into game input.
//
// SPSC ring (producer = module, consumer = launcher); head/tail are the only
// cross-thread mutable fields, updated with release/acquire order. The module
// signals the named wake event after each enqueue so the launcher can sleep.
//
// v2: N z-ordered consume rects (was one sidebar rect) + named wake event.

#include <cstdint>

namespace rtx::input {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXInput_v2_";
inline constexpr wchar_t kEventPrefix[]   = L"Local\\RuneToolsXInputEvt_v2_";
inline constexpr std::uint32_t kMagic   = 0x52545849;   // 'RTXI'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::uint32_t kRingSize = 256;          // power of two
inline constexpr std::uint32_t kMaxRects = 64;

// One forwarded window message (raw Win32 fields; the launcher maps it to an
// Ultralight mouse/scroll/key event).
struct Event {
    std::uint32_t msg;         // WM_* identifier
    std::int32_t  x, y;        // cursor in client pixels
    std::int64_t  wparam;
    std::int64_t  lparam;
    std::uint64_t t;           // GetTickCount64 at capture
};

struct Rect {
    std::int32_t x, y, w, h;   // client pixels; w<=0 marks an empty slot
};

inline void make_name(const wchar_t* prefix, std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = prefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

// Build "Local\RuneToolsXInput_v2_<pid>" into `out` (size >= 64).
inline void MakeSectionName(std::uint32_t pid, wchar_t* out) { make_name(kSectionPrefix, pid, out); }
// Build "Local\RuneToolsXInputEvt_v2_<pid>" into `out` (size >= 64).
inline void MakeEventName(std::uint32_t pid, wchar_t* out) { make_name(kEventPrefix, pid, out); }

struct Share {
    std::uint32_t magic;       // kMagic once initialised
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)

    // launcher -> module: authoritative consume state (client pixels). The rect
    // list is rewritten whole under `rect_seq` (odd = mid-write); the module
    // hit-tests against a snapshot and tolerates one stale frame.
    volatile std::uint32_t rect_seq;
    volatile std::uint32_t rect_count;         // <= kMaxRects
    Rect                   rects[kMaxRects];
    volatile std::uint32_t capture_keyboard;   // 1 = a UI field has focus; consume keys
    volatile std::uint32_t cursor_id;          // ultralight::Cursor to show while over UI
    volatile std::uint32_t active;             // 1 = pre-filter engaged (any UI visible)

    // module -> launcher: SPSC event ring.
    volatile std::uint32_t head;   // next write slot (producer = module)
    volatile std::uint32_t tail;   // next read slot  (consumer = launcher)
    Event events[kRingSize];
};

}  // namespace rtx::input
