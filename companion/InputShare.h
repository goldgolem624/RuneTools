#pragma once
// UI input pre-filter channel. SPSC ring (head/tail release/acquire); messages are only dropped or passed, never re-injected.

#include <cstdint>

namespace rtx::input {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXInput_v2_";
inline constexpr wchar_t kEventPrefix[]   = L"Local\\RuneToolsXInputEvt_v2_";
inline constexpr std::uint32_t kMagic   = 0x52545849;   // 'RTXI'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::uint32_t kRingSize = 256;          // power of two
inline constexpr std::uint32_t kMaxRects = 64;

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

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) { make_name(kSectionPrefix, pid, out); }
inline void MakeEventName(std::uint32_t pid, wchar_t* out) { make_name(kEventPrefix, pid, out); }

struct Share {
    std::uint32_t magic;       // kMagic once initialised
    std::uint32_t version;     // kVersion
    std::uint32_t pid;         // target client pid (sanity)

    volatile std::uint32_t rect_seq;
    volatile std::uint32_t rect_count;         // <= kMaxRects
    Rect                   rects[kMaxRects];
    volatile std::uint32_t capture_keyboard;   // 1 = a UI field has focus; consume keys
    volatile std::uint32_t cursor_id;          // ultralight::Cursor to show while over UI
    volatile std::uint32_t active;             // 1 = pre-filter engaged (any UI visible)

    volatile std::uint32_t head;   // next write slot (producer = module)
    volatile std::uint32_t tail;   // next read slot  (consumer = launcher)
    Event events[kRingSize];
};

}  // namespace rtx::input
