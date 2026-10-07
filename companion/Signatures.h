#pragma once
// Code signatures the companion attaches to, shared with the reader's update check so the hooks
// and the check cannot drift. Every byte pattern is used by the companion exactly as written here.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rtx::sig {

constexpr int kAny = -1;

// Varp and varc-int set handlers, the same body up to the bucket load register.
inline constexpr unsigned char kVarpBody[] = {
    0x4C, 0x8B, 0x4A, 0x10, 0x48, 0x8B, 0xDA, 0x44, 0x0F, 0xB7, 0x42, 0x24, 0x33, 0xD2, 0x41, 0x8B,
    0xC0, 0x41, 0x8B, 0x49, 0x60, 0x4D, 0x8B, 0x51, 0x58, 0x48, 0xF7, 0xF1, 0x8B, 0xC2, 0x49, 0x8B,
    0x0C, 0xC2,
};
inline constexpr unsigned char kVarcBody[] = {
    0x4C, 0x8B, 0x4A, 0x10, 0x48, 0x8B, 0xDA, 0x44, 0x0F, 0xB7, 0x42, 0x24, 0x33, 0xD2, 0x41, 0x8B,
    0xC0, 0x41, 0x8B, 0x49, 0x60, 0x4D, 0x8B, 0x51, 0x58, 0x48, 0xF7, 0xF1, 0x8B, 0xC2, 0x49, 0x8B,
    0x04, 0xC2,
};

// cc_if_setdraggable: add dword [r9+0x10A0],-2 ; mov rbp,rcx ; mov eax,[r9+..]
inline constexpr unsigned char kCcDragBody[] = {
    0x41, 0x83, 0x81, 0xA0, 0x10, 0x00, 0x00, 0xFE, 0x48, 0x8B, 0xE9, 0x41, 0x8B, 0x81,
};

// NPC display: mov rax,[rcx] ; mov rdi,r9 ; mov rsi,r8 ; mov rbp,rdx ; mov rbx,rcx ; call [rax+0x110]
inline constexpr unsigned char kNpcDisBody[] = {
    0x48, 0x8B, 0x01, 0x49, 0x8B, 0xF9, 0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xD9, 0xFF,
    0x90, 0x10, 0x01, 0x00, 0x00,
};

// Player visibility: mov rax,[rcx+0x1078] ; mov rbp,r9. The actor field's low byte is open
// (0x1088 on the plugin client).
inline constexpr unsigned char kPlDisBody[] = {
    0x48, 0x8B, 0x81, 0x78, 0x10, 0x00, 0x00, 0x49, 0x8B, 0xE9,
};
inline constexpr unsigned char kPlDisMask[] = {
    1, 1, 1, 0, 1, 1, 1, 1, 1, 1,
};

// Render thread: mov rax,[rcx+8] ; mov r15,rcx ; mov r14,[rip+..]. The scene switch is the Jcc at
// function +0x266. Present in the OpenGL client only.
inline constexpr unsigned char kRenderBody[] = {
    0x48, 0x8B, 0x41, 0x08, 0x4C, 0x8B, 0xF9, 0x4C, 0x8B, 0x35,
};

// Inbound framer prologue; the test rcx,rcx / jz / cmp dword [rcx],2 tail makes it unique.
inline constexpr unsigned char kFramerBody[] = {
    0x40, 0x53, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x33, 0xF6, 0x48, 0x8B, 0xD9,
    0x48, 0x8B, 0x49, 0x08, 0x4C, 0x8B, 0xF2, 0x44, 0x8B, 0xFE, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x00,
    0x00, 0x00, 0x00, 0x83, 0x39, 0x02,
};
inline constexpr unsigned char kFramerMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 1, 1, 1,
};

// Object submit: mov rcx,rdx ; mov r8d,0x47 ; mov r14,rdx ; call ; mov rcx,[rbx+0x140] ; cmp rcx,[rbx+0x148]
inline constexpr unsigned char kObjSubmitBody[] = {
    0x00, 0x00, 0x48, 0x8B, 0xCA, 0x41, 0xB8, 0x47, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF2, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x8B, 0x40, 0x01, 0x00, 0x00, 0x48, 0x3B, 0x8B, 0x48, 0x01, 0x00,
};
inline constexpr unsigned char kObjSubmitMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

// Object delete: prologue and the worker vector compare at +0x140/+0x138.
inline constexpr unsigned char kObjDelBody[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA,
    0x48, 0x8B, 0x89, 0x40, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x87, 0x38, 0x01, 0x00, 0x00, 0x48, 0x3B,
    0xC1,
};

// Type-4 display (graphic highlights), vtable slot 7. Starts mid-function at the flag byte test,
// +0x1BD since 950-1 (+0x1AD before the object header grew).
inline constexpr unsigned char kT4DisplayBody[] = {
    0x80, 0xB9, 0xBD, 0x01, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xD9, 0x49, 0x8B, 0xE8, 0x4C, 0x8B, 0xF2,
    0x48, 0x8B, 0xF9,
};

// Type-13 display (walk and scan markers), vtable slot 7.
inline constexpr unsigned char kT13DisplayBody[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x48, 0x81, 0xEC, 0xE0, 0x00,
    0x00, 0x00,
};

// Menu string-init: the language-index load at MainData+0x19B10 (low byte open: 0x19B30 on the
// plugin client).
inline constexpr unsigned char kMenuInit[] = {
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x8B, 0xF9, 0x48, 0x8D, 0x0D,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x8D, 0x99, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x90, 0x10, 0x9B, 0x01, 0x00, 0x48, 0x63, 0x02, 0x48, 0x8D, 0x14, 0xC5, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0x84, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x48, 0x03, 0xDA, 0x48, 0x89, 0x47,
    0x10, 0x48, 0x8B, 0x03, 0x48, 0x89, 0x47, 0x18, 0x48, 0x8B, 0x84, 0x0A, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x89, 0x47, 0x20, 0x48, 0x8B, 0x84, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x47, 0x28,
    0x48, 0x8B, 0x84, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x47, 0x30, 0x48, 0x8B, 0x84, 0x0A,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x47, 0x38,
};
inline constexpr unsigned char kMenuInitMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1,
};

// Menu clear(), inside its body: mov byte [rcx+0x68],0 ; movzx ebp,dl ; mov rsi,[rcx+0x1388] (end) ;
// mov rdi,rcx ; mov rbx,[rcx+0x1380] (begin) ; cmp rbx,rsi ; je. The hook is the function holding it.
inline constexpr unsigned char kMenuClear[] = {
    0xC6, 0x41, 0x68, 0x00, 0x0F, 0xB6, 0xEA, 0x48, 0x8B, 0xB1, 0x88, 0x13, 0x00, 0x00, 0x48, 0x8B,
    0xF9, 0x48, 0x8B, 0x99, 0x80, 0x13, 0x00, 0x00, 0x48, 0x3B, 0xDE, 0x74,
};
inline constexpr unsigned char kMenuClearMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
};

// Per-tick menu builder.
inline constexpr unsigned char kMenuBuild[] = {
    0x44, 0x88, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53,
    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC8, 0x48,
    0x81, 0xEC, 0x38, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0xE8, 0xE2, 0xBF, 0x5A, 0x00, 0x48, 0x8B,
    0xC8, 0xE8, 0xAA, 0xC5, 0x5A, 0x00, 0x48, 0x8B, 0x47, 0x08, 0x4C, 0x8B, 0x3D, 0x6F, 0x48, 0xBF,
    0x00, 0x4C, 0x8B, 0xA0, 0x98, 0x98, 0x01, 0x00, 0x4C, 0x8B, 0xA8, 0x68, 0x8D, 0x01, 0x00, 0x48,
    0x8B, 0xB0, 0x58, 0x8D, 0x01, 0x00, 0x48, 0x8B, 0x98, 0x80, 0x8D, 0x01, 0x00, 0x4C, 0x89, 0xA5,
    0x80, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x6C, 0x24, 0x30, 0xE8, 0x42, 0xC4, 0x71, 0x00, 0x4C, 0x8B,
    0xF0, 0xE8, 0x1E, 0xC4, 0x71, 0x00, 0x49, 0x81, 0xFE, 0x80, 0x96, 0x98, 0x00,
};
inline constexpr unsigned char kMenuBuildMask[] = {   // the frame size, the rbp bias, MapMgr and the home slot are open
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,
    1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0,
};

// Menu snapshot, inside its body: inc qword [rcx+0x21B0] (counter) ; lea reg,[rcx+0x1A80] (base).
// It copies the top records into the left-click slots. The hook is the function holding it.
inline constexpr unsigned char kMenuSnap[] = {
    0x48, 0xFF, 0x81, 0xB0, 0x21, 0x00, 0x00, 0x48, 0x8D, 0xB1, 0x80, 0x1A, 0x00, 0x00,
};
inline constexpr unsigned char kMenuSnapMask[] = {
    1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,
};

// Menu action executor, inside its body: mov rax,[rcx+8] ; mov r14,r8 ; mov r15,rdx ; mov reg,rcx ;
// cmp dword [rax+status],0x28 ; je. The hook is the function holding it.
inline constexpr unsigned char kMenuExec[] = {
    0x48, 0x8B, 0x41, 0x08, 0x4D, 0x8B, 0xF0, 0x4C, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x83, 0xB8, 0xA0,
    0x9F, 0x01, 0x00, 0x28, 0x0F, 0x84,
};
inline constexpr unsigned char kMenuExecMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0,
    0, 0, 0, 1, 1, 1,
};

// Inside the snapshot, the left-click slot assigns: lea rcx,[reg+slot] ; call assign. The first
// one in the body names the slot the lift hooks (0x13E0, 0x13E8 on the plugin client); a later one
// with a higher slot must call the same routine.
inline constexpr std::uint32_t kMenuSlotLo = 0x13C0, kMenuSlotHi = 0x1420;
inline constexpr std::size_t kMenuSnapSpan = 0x1400;
struct MenuAssign { std::size_t site = 0, check = 0; std::uint32_t slot = 0; std::int64_t target = 0; bool ok = false; };   // offsets into the body; target relative to it
inline MenuAssign FindMenuAssign(const unsigned char* body, std::size_t n) {
    MenuAssign m;
    for (std::size_t i = 0; i + 12 <= n; ++i) {
        if ((body[i] != 0x48 && body[i] != 0x49) || body[i + 1] != 0x8D || (body[i + 2] & 0xF8) != 0x88 || (body[i + 2] & 7) == 4 || body[i + 7] != 0xE8) continue;
        std::uint32_t disp; std::memcpy(&disp, body + i + 3, 4);
        if (disp < kMenuSlotLo || disp > kMenuSlotHi) continue;
        std::int32_t rel; std::memcpy(&rel, body + i + 8, 4);
        const std::int64_t target = (std::int64_t)(i + 7) + 5 + rel;
        if (!m.site) { m.site = i + 7; m.slot = disp; m.target = target; continue; }
        if (target == m.target && disp > m.slot) { m.check = i + 7; m.ok = true; return m; }
    }
    return MenuAssign{};
}

// Hover entry op stub ending in jmp rel32 to the routine that pushes the tooltip strings. Two ops
// share it; every match must name the same target.
inline constexpr int kTooltipStub[] = {
    0x4C, 0x8B, 0x89, kAny, kAny, kAny, kAny, 0x4C, 0x8B, 0xD2, 0xBA, kAny, kAny, kAny, kAny, 0x49,
    0x8B, 0x41, 0x08, 0x4C, 0x8B, 0x80, kAny, kAny, kAny, kAny, 0xB8, kAny, kAny, kAny, kAny, 0x41,
    0x80, 0xB8, kAny, kAny, kAny, kAny, 0x00, 0x4D, 0x8B, 0xC2, 0x0F, 0x44, 0xD0, 0x49, 0x03, 0xD1,
    0xE9,
};

// Loc pass outline opt-out switch: cmp byte [rip+disp],0 is the byte the outline module sets.
inline constexpr int kOutlineSwitch[] = {
    0x80, 0x3D, kAny, kAny, kAny, kAny, 0x00, 0x0F, 0x29, 0x74, 0x24, kAny, 0x75, kAny, 0x80, 0x39,
    0x00, 0x75, kAny, 0x48, 0x85, 0xD2, 0x74, kAny, 0x80, 0xBA, kAny, kAny, 0x00, 0x00, 0x00,
};

// Highlight category mode op: lea rcx,[rip+disp] names the category table.
inline constexpr int kOutlineTable[] = {
    0x83, 0xF8, 0x07, 0x77, kAny, 0x44, 0x8B, 0x81, kAny, kAny, 0x00, 0x00, 0x41, 0x83, 0xF8, 0x03,
    0x77, kAny, 0x48, 0x03, 0xC0, 0x48, 0x8D, 0x0D, kAny, kAny, kAny, kAny, 0x44, 0x88, 0x04, 0xC1,
};

// Server arrow message, inside its body: the stream advance, the first byte split into slot (>> 5)
// and kind, then within kArrowLoadSpan mov reg,[rax+disp], the arrow manager inside the root. The
// routine is the function holding it.
inline constexpr int kArrowMessage[] = {
    0x49, 0x8D, 0x40, 0x01, 0x48, 0x89, 0x42, 0x18, 0x48, 0x8B, 0x42, 0x10, kAny, 0x0F, 0xB6, kAny,
    0x00, 0x48, 0x8B, 0x01, kAny, 0x8B, kAny, 0x41, 0xC1, kAny, 0x05,
};
inline constexpr std::size_t kArrowLoadSpan = 0x20;
// The first mov r64,[rax+disp32] in p[0..n) with a MainData-sized disp, else 0.
inline std::uint32_t RootFieldLoad(const unsigned char* p, std::size_t n) {
    for (std::size_t i = 0; i + 7 <= n; ++i) {
        if ((p[i] != 0x48 && p[i] != 0x4C) || p[i + 1] != 0x8B || (p[i + 2] & 0xC7) != 0x80) continue;
        std::uint32_t d; std::memcpy(&d, p + i + 3, 4);
        return d >= 0x18000 && d < 0x60000 ? d : 0;
    }
    return 0;
}

// Server trail message; kTrailManager follows within 0xA0 bytes: add rcx,disp ; mov rcx,[rcx] ; call.
inline constexpr int kTrailMessage[] = {
    0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0x42, 0x18, 0x4C, 0x8B, 0xD1, 0x49, 0x8D, 0x48, 0x01, 0x48,
    0x89, 0x4A, 0x18, 0x48, 0x8B, 0x42, 0x10, 0x80, 0x3C, 0x08, 0x7F, 0x46, 0x0F, 0xB6, 0x1C, 0x00,
};
inline constexpr int kTrailManager[] = {
    0x48, 0x81, 0xC1, kAny, kAny, kAny, kAny, 0x48, 0x8B, 0x09, 0xE8,
};

// Where the game gives the arrow manager its turn every frame: mov rcx,[rcx+arrow] ; call frame ;
// mov rax,[reg+8] ; mov reg,[rax+trail] ; lea rbx,[reg+0x10]. The call names the arrow manager's
// per-frame routine (the hook point); the two loads name both managers inside the root. The lea
// after it bounds the trail manager's eight slots, which gives their size (ArrowSlotSize).
// kArrowRefresh is the node refresh call inside the frame routine.
inline constexpr int kArrowFrame[] = {
    0x48, 0x8B, 0x89, kAny, 0x98, 0x01, 0x00, 0xE8, kAny, kAny, kAny, kAny, kAny, 0x8B, kAny, 0x08,
    0x48, 0x8B, kAny, kAny, kAny, 0x01, 0x00, 0x48, 0x8D, kAny, 0x10,
};
inline constexpr std::size_t kArrowFrameCall = 7, kArrowFrameArrow = 3, kArrowFrameTrail = 19;
// Bytes per manager slot from the two leas that bound them (lea rbx,[mgr+0x10] ; lea rdi,[mgr or
// rbx + disp]); 8 on 950-1, 16 on the plugin client; 0 when the shape is another.
inline std::uint32_t ArrowSlotSize(const unsigned char* site) {
    const unsigned char* a = site + 23;
    if (a[0] != 0x48 || a[1] != 0x8D || (a[2] >> 6) != 1 || (a[2] & 7) == 4 || a[3] != 0x10) return 0;
    const int start = (a[2] >> 3) & 7, mgr = a[2] & 7;
    const unsigned char* b = a + 4;
    if (b[0] != 0x48 || b[1] != 0x8D || (b[2] & 7) == 4) return 0;
    const int mod = b[2] >> 6, base = b[2] & 7;
    std::int32_t disp = 0;
    if (mod == 1) disp = (std::int8_t)b[3];
    else if (mod == 2) std::memcpy(&disp, b + 3, 4);
    else return 0;
    const std::int32_t span = base == mgr ? disp - 0x10 : base == start ? disp : 0;
    return span > 0 && span % 8 == 0 ? (std::uint32_t)span / 8 : 0;
}
inline constexpr std::uint32_t kArrowSlotSize = 8;   // the slot layout the marker code knows
inline constexpr int kArrowRefresh[] = {
    0x41, 0xB8, 0x45, 0x00, 0x00, 0x00,
};

// Trail tile draw method: the pass flags field at +45, then the submit call (kTileSubmit).
inline constexpr int kTileDraw[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x8B, 0x01, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0xFF, 0x90, 0x10, 0x01, 0x00,
    0x00, 0x84, 0xC0, 0x74, kAny, 0x4C, 0x8B, 0x4C, 0x24, 0x60, 0x41, 0x8B, 0x81, kAny, kAny, 0x00,
    0x00, 0xA8, 0x03, 0x75, kAny, 0xA8, 0x04, 0x75, kAny, 0xA8, 0x20, 0x74, kAny,
};
inline constexpr int kTileSubmit[] = {
    0x48, 0x89, 0x44, 0x24, 0x20, 0xE8,
};

// Engine op heads, each checked against the handler its fixed number names.
inline constexpr unsigned char kOpProjectHead[] = {
    0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x8B, 0x82, 0xA0, 0x10, 0x00, 0x00, 0x48,
    0x8B, 0xDA, 0xFF, 0xC8,
};
inline constexpr unsigned char kOpProjectKind3[] = {
    0x80, 0x7E, 0x18, 0x03,
};
inline constexpr unsigned char kOpProjectView[] = {
    0x48, 0x8B, 0x89, 0xD0, 0x99, 0x01, 0x00,
};
inline constexpr unsigned char kOpSelfPosHead[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x81, 0xA8, 0x9F, 0x01, 0x00, 0x48, 0x8B, 0xDA,
    0x48, 0x85, 0xC0,
};
inline constexpr unsigned char kOpBindHead[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0x9A, 0xA0, 0x10, 0x00,
};
inline constexpr unsigned char kOpHeightHead[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x8A, 0xB8, 0xC3, 0x00, 0x00, 0x48, 0x8D, 0x9A,
    0x00, 0x01, 0x00, 0x00,
};
inline constexpr unsigned char kOpEntityScreenHead[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA,
    0x48, 0x8B, 0x8A, 0xB8,
};

// Sound synth op: the call after it is the play routine. The SoundCtx field's low byte is open
// (0x19A50 on the plugin client).
inline constexpr unsigned char kSoundSynth[] = {
    0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x83, 0x82, 0xA0, 0x10, 0x00, 0x00, 0xFD, 0x8B, 0x82,
    0xA0, 0x10, 0x00, 0x00, 0x48, 0x8B, 0x89, 0x30, 0x9A, 0x01, 0x00, 0x4C, 0x8D, 0x04, 0x82, 0x48,
    0x85, 0xC9, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x41, 0x8B, 0x80, 0x04, 0x01, 0x00, 0x00, 0x48,
    0x8D, 0x15, 0x00, 0x00, 0x00, 0x00, 0x45, 0x8B, 0x88, 0x00, 0x01, 0x00, 0x00, 0xC6, 0x44, 0x24,
    0x70, 0x00, 0xC7, 0x44, 0x24, 0x60, 0xFF, 0x00, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x58, 0xFF, 0xFF,
    0xFF, 0xFF, 0x48, 0x89, 0x54, 0x24, 0x50, 0x33, 0xD2, 0x89, 0x54, 0x24, 0x48, 0x89, 0x54, 0x24,
    0x40, 0x48, 0x8B, 0xD1, 0xC7, 0x44, 0x24, 0x38, 0x04, 0x00, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x30,
    0x06, 0x00, 0x00, 0x00, 0x48, 0x89, 0x9C, 0x24, 0x80, 0x00, 0x00, 0x00, 0x41, 0x8B, 0x98, 0x08,
    0x01, 0x00, 0x00, 0x44, 0x0F, 0xB7, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x28, 0xFF,
    0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x20,
};
inline constexpr unsigned char kSoundSynthMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
};

// Chat message store notify routine.
inline constexpr unsigned char kChatNotify[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x50, 0x48, 0x8B, 0x41, 0x30, 0x4C, 0x8D, 0x41, 0x20, 0x4C, 0x8D, 0x4A, 0x18, 0x48, 0x8B,
    0xFA, 0x48, 0x8B, 0xF1, 0x49, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x74, 0x1A, 0x41, 0x8B, 0x09, 0x90,
};

// Component setter wrapper: push rbx ; sub rsp,60 ; lea rax,[vtable] (head), then at +12
// mov [rsp+20],rax ; lea r8,[rsp+20] ; lea rax,[callback] (mid), then at +29 mov [rsp+28],rax ;
// lea rax,[rsp+20] ; mov [rsp+58],rax ; call dispatcher (tail). The dispatcher wanted begins by
// choosing between the state's two component slots, 0xBFC0 and 0xBFE0 (slots, in its first 0x20 bytes).
inline constexpr unsigned char kCcWrapHead[] = { 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8D, 0x05 };
inline constexpr unsigned char kCcWrapMid[]  = { 0x48, 0x89, 0x44, 0x24, 0x20, 0x4C, 0x8D, 0x44, 0x24, 0x20, 0x48, 0x8D, 0x05 };
inline constexpr unsigned char kCcWrapTail[] = { 0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8D, 0x44, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24, 0x58, 0xE8 };
inline constexpr int kCcWrapSlots[] = { 0xB8, 0xC0, 0xBF, 0x00, 0x00, 0x41, 0xB9, 0xE0, 0xBF, 0x00, 0x00 };
inline constexpr std::size_t kCcWrapMidAt = 12, kCcWrapTailAt = 29;

// Outline methods on everything the game can outline: slot 0xF8 of the object's method table is
// "hovered this frame", slot 0x100 the plain setter. Both start
// mov rax,[rcx+18h] ; test rax,rax ; je ; mov rax,[rax+130h]
inline constexpr unsigned char kHoverProlog[] = {
    0x48, 0x8B, 0x41, 0x18, 0x48, 0x85, 0xC0, 0x74, 0x1F, 0x48, 0x8B, 0x80, 0x30, 0x01, 0x00, 0x00,
};
inline constexpr std::uint32_t kHoverSlot = 0xF8, kKeepSlot = 0x100;

// Reader anchors: the MainData constructor (sub rax,0xA8 ; ..., 32 bytes in) and the tick counter
// increment (inc dword [rcx+0xDBF0]).
inline constexpr unsigned char kMainAnchor[] = {
    0x48, 0x2D, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x83,
};
inline constexpr unsigned char kTickInc[] = {
    0xFF, 0x81, 0xF0, 0xDB, 0x00, 0x00,
};
// The engine clock (u64 ms) is a global just after the root global: +0x10 on 950-1 Vulkan, +0x8 on
// 950-1 OpenGL, +0x30 on the plugin client. Three routines read it with mov reg,[rip+disp] (the disp
// at +3); each matches once, and those found must name one place. Read by the reader's calibration.
inline constexpr int kClockReadA[] = {   // mov rax,[clock] ; add rax,1Eh ; mov [rdi+48h],rax ; add rsp,20h ; pop rdi ; ret
    0x48, 0x8B, 0x05, kAny, kAny, kAny, kAny, 0x48, 0x83, 0xC0, 0x1E, 0x48, 0x89, 0x47, 0x48, 0x48,
    0x83, 0xC4, 0x20, 0x5F, 0xC3,
};
inline constexpr int kClockReadB[] = {   // mov r9,[clock] ; lea rcx,[rax+..] ; add rdx,40h ; mov r8d,2EEh ; call
    0x4C, 0x8B, 0x0D, kAny, kAny, kAny, kAny, 0x48, 0x8D, 0x88, kAny, kAny, 0x00, 0x00, 0x48, 0x83,
    0xC2, 0x40, 0x41, 0xB8, 0xEE, 0x02, 0x00, 0x00, 0xE8,
};
inline constexpr int kClockReadC[] = {   // mov rax,[clock] ; mov [rcx+48h],rax ; add rsp,78h ; pop r12 ; pop rbx ; ret
    0x48, 0x8B, 0x05, kAny, kAny, kAny, kAny, 0x48, 0x89, 0x41, 0x48, 0x48, 0x83, 0xC4, 0x78, 0x41,
    0x5C, 0x5B, 0xC3,
};
struct ClockRead { const int* pat; std::size_t len; };
inline constexpr ClockRead kClockReads[] = {
    { kClockReadA, sizeof(kClockReadA) / sizeof(int) },
    { kClockReadB, sizeof(kClockReadB) / sizeof(int) },
    { kClockReadC, sizeof(kClockReadC) / sizeof(int) },
};
inline constexpr std::uint32_t kClockDispAt = 3, kClockMax = 0x100;   // the clock lies within this of the root global

// ---- the table the update check walks ----
// scope: kFirstExec searches the first executable section only (as the scene, menu, sound and chat
// hooks do), kAllExec every executable section. expect: kOnce exactly one hit; kSameTarget one or
// more hits whose trailing rel32 all name one routine; kOpenGlOnce one hit on the OpenGL client and
// none on Vulkan. Names are the ones the companion's boot record uses. hook: where the companion
// attaches, and what the manifest records: kAtHit the hit (byte forms in the first section hook the
// function holding it), kHitInFunction the same with the function shown, kInFunction the function
// holding the hit, kAtCall the routine the call in the hit names.
enum Scope : std::uint8_t { kFirstExec, kAllExec };
enum Expect : std::uint8_t { kOnce, kSameTarget, kOpenGlOnce };
enum Hook : std::uint8_t { kAtHit, kHitInFunction, kInFunction, kAtCall };
struct Sig {
    const char* name;
    const unsigned char* bytes;   // byte form; mask null = every byte fixed, mask[j] 0 = any
    const unsigned char* mask;
    const int* ints;              // int form with kAny, used when bytes is null
    std::size_t len;
    Scope scope;
    Expect expect;
    const char* features;
    Hook hook = kAtHit;
    std::uint8_t callAt = 0;      // kAtCall: the E8 byte's offset in the hit
};
#define RTX_SIG_B(nm, a, sc, ex, ft)    { nm, a, nullptr, nullptr, sizeof(a), sc, ex, ft }
#define RTX_SIG_M(nm, a, m, sc, ex, ft) { nm, a, m, nullptr, sizeof(a), sc, ex, ft }
#define RTX_SIG_I(nm, a, sc, ex, ft)    { nm, nullptr, nullptr, a, sizeof(a) / sizeof(int), sc, ex, ft }
#define RTX_SIG_MH(nm, a, m, sc, ex, hk, ft)   { nm, a, m, nullptr, sizeof(a), sc, ex, ft, hk }
#define RTX_SIG_IH(nm, a, sc, ex, hk, at, ft)  { nm, nullptr, nullptr, a, sizeof(a) / sizeof(int), sc, ex, ft, hk, at }
inline constexpr Sig kTable[] = {
    RTX_SIG_B("main-anchor",     kMainAnchor,     kAllExec,   kOnce,       "Everything that reads the game"),
    RTX_SIG_B("tick-anchor",     kTickInc,        kAllExec,   kOnce,       "Game tick, tick timers"),
    RTX_SIG_B("varp-observer",   kVarpBody,       kFirstExec, kOnce,       "Live variable changes"),
    RTX_SIG_B("varc-observer",   kVarcBody,       kFirstExec, kOnce,       "Live variable changes"),
    RTX_SIG_B("ccdrag-observer", kCcDragBody,     kFirstExec, kOnce,       "Interface drag tracking"),
    RTX_SIG_B("npc-display",     kNpcDisBody,     kFirstExec, kOnce,       "Rendering: hide NPCs"),
    RTX_SIG_MH("player-display", kPlDisBody, kPlDisMask, kFirstExec, kOnce, kHitInFunction, "Rendering: hide players"),
    RTX_SIG_B("scene-blank",     kRenderBody,     kFirstExec, kOpenGlOnce, "Rendering: hide scene"),
    RTX_SIG_M("framer",          kFramerBody, kFramerMask, kFirstExec, kOnce, "Chat capture, event channel, packet feed"),
    RTX_SIG_M("spawn-hook",      kObjSubmitBody, kObjSubmitMask, kFirstExec, kOnce, "Scene objects, specials"),
    RTX_SIG_B("del-hook",        kObjDelBody,     kFirstExec, kOnce,       "Scene objects"),
    RTX_SIG_B("t4-display",      kT4DisplayBody,  kFirstExec, kOnce,       "Specials: graphic highlights, clue scan ring"),
    RTX_SIG_B("t13-display",     kT13DisplayBody, kFirstExec, kOnce,       "Specials: walk and scan markers"),
    RTX_SIG_M("menu-init",       kMenuInit,  kMenuInitMask,  kFirstExec, kOnce, "Menu swaps"),
    RTX_SIG_MH("menu-clear",     kMenuClear, kMenuClearMask, kFirstExec, kOnce, kInFunction, "Menu swaps"),
    RTX_SIG_M("menu-build",      kMenuBuild, kMenuBuildMask, kFirstExec, kOnce, "Menu swaps: reordering"),
    RTX_SIG_MH("menu-snap",      kMenuSnap,  kMenuSnapMask,  kFirstExec, kOnce, kInFunction, "Menu swaps: left-click option"),
    RTX_SIG_MH("menu-exec",      kMenuExec,  kMenuExecMask,  kFirstExec, kOnce, kInFunction, "Menu swaps: action log"),
    RTX_SIG_I("tooltip-stub",    kTooltipStub,    kAllExec,   kSameTarget, "Tooltip text (item prices, levels)"),
    RTX_SIG_I("outline-switch",  kOutlineSwitch,  kAllExec,   kOnce,       "Native outlines"),
    RTX_SIG_I("outline-table",   kOutlineTable,   kAllExec,   kOnce,       "Native outlines: colours and modes"),
    RTX_SIG_IH("arrow-message",  kArrowMessage,   kAllExec,   kOnce, kInFunction, 0, "Engine markers: hint arrows"),
    RTX_SIG_I("trail-message",   kTrailMessage,   kAllExec,   kOnce,       "Engine markers: tile trail"),
    RTX_SIG_IH("arrow-frame",    kArrowFrame,     kAllExec,   kOnce, kAtCall, kArrowFrameCall, "Engine markers|In-frame panels and text|Sounds, camera zoom and FOV|In-frame label anchors|Asks: achievements and quests"),
    RTX_SIG_I("tile-draw",       kTileDraw,       kAllExec,   kOnce,       "Engine markers: tile outline"),
    RTX_SIG_M("sound-synth",     kSoundSynth, kSoundSynthMask, kFirstExec, kOnce, "Sounds panel, sound mute"),
    RTX_SIG_B("chat-notify",     kChatNotify,     kFirstExec, kOnce,       "Chat mute"),
};
#undef RTX_SIG_B
#undef RTX_SIG_M
#undef RTX_SIG_I
#undef RTX_SIG_MH
#undef RTX_SIG_IH

// Engine ops called by their fixed number; the head must be that handler's first bytes.
struct OpHead { const char* name; std::uint32_t op; const unsigned char* head; std::size_t len; const char* features; };
inline constexpr std::uint32_t kOpProject = 1738, kOpSelfPos = 1227, kOpBindEntity = 958, kOpOverlayHeight = 1002, kOpEntityScreen = 1668;
inline constexpr OpHead kOpHeads[] = {
    { "op-project",   kOpProject,       kOpProjectHead,      sizeof(kOpProjectHead),      "In-frame labels, engine projection" },
    { "op-selfpos",   kOpSelfPos,       kOpSelfPosHead,      sizeof(kOpSelfPosHead),      "In-frame labels: own position" },
    { "op-bind",      kOpBindEntity,    kOpBindHead,         sizeof(kOpBindHead),         "Overhead anchors" },
    { "op-height",    kOpOverlayHeight, kOpHeightHead,       sizeof(kOpHeightHead),       "Overhead anchors" },
    { "op-entscreen", kOpEntityScreen,  kOpEntityScreenHead, sizeof(kOpEntityScreenHead), "Overhead anchors" },
};

// Component setters, found among the op handlers by a run of bytes each alone contains ("??" = any).
// text: the handler names itself in an error string instead.
struct CcOp { const char* name; const char* text; const char* fingerprint; const char* fingerprint2; };
inline constexpr CcOp kCcOps[] = {
    { "cc_create",        "_cc_create", nullptr,                                  nullptr },
    { "cc_delete",        "_cc_delete", nullptr,                                  nullptr },
    { "cc_setposition",   nullptr,      "33 C9 49 83 C3 40",                      nullptr },   // four ints
    { "cc_setsize",       nullptr,      "41 B9 04 00 00 00 44 39 4A 08",          nullptr },   // four ints, modes clamped to 4
    { "cc_setcolour",     nullptr,      "44 89 88 88 00 00 00",                   nullptr },   // one int into the component at +0x88
    { "cc_setfill",       nullptr,      "40 88 B8 88 01 00 00",                   nullptr },   // one int, as a byte at +0x188
    { "cc_settrans",      nullptr,      "F6 D1 88 8A 8C 00 00 00",                nullptr },   // one int, inverted, as a byte at +0x8C
    { "cc_sethide",       nullptr,      "41 0F 94 C1 E8",                         nullptr },   // one int, compared with 1, handed on
    { "cc_find",          nullptr,      "B8 C0 BF 00 00 45 8B 82 00 01 00 00 48 83 C2 38", nullptr },   // (component, slot) -> the slot made active
    { "cc_settext",       nullptr,      "41 FF 89 A8 8D 00 00 49 81 C1 A8 10 00 00", "3C 0C 75" },   // one string, from the string stack; cmp al,0Ch ; jne (a component kind test) tells it from the plugin client's second string setter
    { "cc_settextfont",   nullptr,      "89 70 20 41 B9 FF FF 00 00",             nullptr },   // one int into the text object at +0x20
    { "cc_settextshadow", nullptr,      "41 FF 89 A0 10 00 00 33 D2 41 8B 81 A0 10 00 00 41 8B 9C 81 00 01 00 00", "0F BA E9 01 88 48 28" },   // sets bit 1 of the text object at +0x28
};
inline constexpr const char* kCcFeatures = "In-game panels drawn by the engine";

// Sound play call sites the launcher labels by origin: the return address of each call to the play
// routine (the call after kSoundSynth) in the Vulkan 950-1 client. Other exes report the raw RVA.
struct SoundSite { std::uint32_t ret; const char* origin; };
inline constexpr SoundSite kSoundSites[] = {
    { 0x949CC, "script" }, { 0x94AEE, "script" }, { 0x9515E, "script" }, { 0xF0718, "server" }, { 0xF0B71, "server_tile" },
    { 0x115478, "zone" }, { 0x115750, "zone" }, { 0x3E70F0, "actor" }, { 0x3E89EB, "engine" },
};

}  // namespace rtx::sig
