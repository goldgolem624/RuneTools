// Window-message filter + foreground-query capture for render continuity (see
// InputFilter.h).
//
// keepFocused set: make the client believe it is always the active foreground window
// so it never throttles its frame rate. That needs three things together:
//   1. drop focus-loss notifications (WM_KILLFOCUS / WM_ACTIVATE*),
//   2. answer its foreground/active/focus polls with its own window, and
//   3. run the real click-activation for it -- a client that thinks it is active
//      declines WM_MOUSEACTIVATE, so a background client would never raise.
// Flag clear: messages pass through and the polls return the real answer.
//
// `embedded` also set: the client window is a true child of the host with input queues
// detached (see Dock.cpp), so its queue never sees real keyboard focus and the host
// relays key messages. That adds:
//   4. post kMsgGameClicked to the host on mouse-button-down so it routes keyboard here,
//   5. skip the client's own keyboard translation (relayed WM_CHARs were already
//      translated by the host; translating again doubles characters), and
//   6. mirror relayed key-downs/-ups into this queue's key state for GetKeyState.

#include "InputFilter.h"
#include "RenderShare.h"
#include "InputShare.h"    // in-game window UI consume rects + forwarded-event ring

#include <detours.h>
#include <fstream>
#include <mutex>
#include <string>

namespace rtx::winmsg {
namespace {

WNDPROC             g_orig       = nullptr;
HWND                g_hwnd       = nullptr;
HWND                g_gameWindow = nullptr;   // the client's own window (reported while keep-focused)
rtx::render::Share* g_render     = nullptr;
HANDLE              g_renderMap  = nullptr;

void EnsureRenderMapped() {
    if (g_render) return;
    // Runs on EVERY window message and foreground/active query; retrying
    // OpenFileMapping each call = a syscall per mouse-move -> input lag. Back off to
    // once per second; once mapped this early-returns forever.
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[64];
    rtx::render::MakeSectionName(GetCurrentProcessId(), name);
    // Read + write: this module also PUBLISHES inputWindow (module -> launcher) here.
    g_renderMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!g_renderMap) return;
    g_render = reinterpret_cast<rtx::render::Share*>(
        MapViewOfFile(g_renderMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                      sizeof(rtx::render::Share)));
    if (!g_render) { CloseHandle(g_renderMap); g_renderMap = nullptr; }
}

// ---- in-game window UI: consume rects + forwarded-event ring -----------------
// The launcher publishes the regions its in-game window UI claims; mouse/keyboard
// messages that hit them are copied into the SPSC ring (waking the launcher via
// the named event) and dropped, so the game never reacts to UI interactions.
// Nothing is ever synthesized back into game input.
rtx::input::Share* g_input    = nullptr;
HANDLE             g_inputMap = nullptr;
HANDLE             g_inputEvt = nullptr;

void EnsureInputMapped() {
    // Retry until BOTH handles exist: the wake event can lag the section if the
    // launcher's creation order ever races this open (a missing event would
    // otherwise leave UI input undrained for the whole session).
    if (g_input && g_inputEvt) return;
    // Same backoff rationale as EnsureRenderMapped: this runs on every message.
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[64];
    if (!g_input) {
        rtx::input::MakeSectionName(GetCurrentProcessId(), name);
        g_inputMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!g_inputMap) return;
        g_input = reinterpret_cast<rtx::input::Share*>(
            MapViewOfFile(g_inputMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                          sizeof(rtx::input::Share)));
        if (!g_input) { CloseHandle(g_inputMap); g_inputMap = nullptr; return; }
    }
    if (!g_inputEvt) {
        rtx::input::MakeEventName(GetCurrentProcessId(), name);
        g_inputEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    }
}

bool UiActive();   // fwd (uses g_closing, defined below)

// Local snapshot of the consume rects, refreshed under the writer's seqlock; a
// mid-write read keeps the previous snapshot (one stale frame is fine).
rtx::input::Rect g_uiRects[rtx::input::kMaxRects];
std::uint32_t    g_uiRectCount = 0;

void SnapshotRects() {
    std::uint32_t seq = g_input->rect_seq;
    if (seq & 1u) return;
    std::uint32_t n = g_input->rect_count;
    if (n > rtx::input::kMaxRects) n = rtx::input::kMaxRects;
    rtx::input::Rect tmp[rtx::input::kMaxRects];
    for (std::uint32_t i = 0; i < n; ++i) tmp[i] = g_input->rects[i];
    if (g_input->rect_seq != seq) return;   // torn: keep the old snapshot
    for (std::uint32_t i = 0; i < n; ++i) g_uiRects[i] = tmp[i];
    g_uiRectCount = n;
}

bool PtInUi(int x, int y) {
    for (std::uint32_t i = 0; i < g_uiRectCount; ++i) {
        const auto& r = g_uiRects[i];
        if (r.w <= 0 || r.h <= 0) continue;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return true;
    }
    return false;
}

void PushUiEvent(UINT msg, int x, int y, WPARAM wp, LPARAM lp) {
    std::uint32_t head = g_input->head;
    std::uint32_t tail = g_input->tail;
    // Moves/wheel are ~99% of traffic and droppable; hold back headroom so
    // button-ups, key events and the blur sentinel virtually never meet a full
    // ring (a lost button-up = a stuck UI drag).
    bool lowPri = (msg == WM_MOUSEMOVE || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL);
    std::uint32_t cap = lowPri ? (rtx::input::kRingSize - 8) : rtx::input::kRingSize;
    if (head - tail >= cap) return;                     // full: drop (launcher stalled)
    rtx::input::Event& e = g_input->events[head & (rtx::input::kRingSize - 1)];
    e.msg = msg;
    e.x = x; e.y = y;
    e.wparam = (std::int64_t)wp;
    e.lparam = (std::int64_t)lp;
    e.t = GetTickCount64();
    MemoryBarrier();                        // event fields visible before head moves
    g_input->head = head + 1;
    if (g_inputEvt) SetEvent(g_inputEvt);
}

// Buttons currently held that STARTED on the UI (we consume until release) vs on
// the game (we must not consume mid-interaction, e.g. a camera drag crossing a
// window). A button-down joins exactly one mask; WM_MOUSEMOVE's held-button state
// self-heals both masks if a release was missed.
unsigned g_uiButtons   = 0;
unsigned g_gameButtons = 0;
// The previous WM_MOUSEMOVE landed on the UI: used to forward ONE transition
// move as the cursor leaves, so the view clears :hover/tooltips.
bool g_wasOverUi = false;

unsigned ButtonBit(UINT msg, WPARAM wp) {
    switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return 1u;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return 2u;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return 4u;
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            return (HIWORD(wp) == XBUTTON2) ? 16u : 8u;
        default: return 0u;
    }
}

// Ultralight cursor id (Listener.h enum Cursor) -> Win32 IDC_* resource.
LPCWSTR Win32CursorFor(std::uint32_t id) {
    switch (id) {
        case 2:  return IDC_HAND;      // kCursor_Hand
        case 3:  return IDC_IBEAM;     // kCursor_IBeam
        case 4:  return IDC_WAIT;      // kCursor_Wait
        case 5:  return IDC_HELP;      // kCursor_Help
        case 1:  return IDC_CROSS;     // kCursor_Cross
        case 6:  case 13: case 15: case 18: return IDC_SIZEWE;   // E/W/EastWest/Column resize
        case 7:  case 10: case 14: case 19: return IDC_SIZENS;   // N/S/NorthSouth/Row resize
        case 8:  case 12: case 16: return IDC_SIZENESW;  // NE/SW resize
        case 9:  case 11: case 17: return IDC_SIZENWSE;  // NW/SE resize
        case 29: return IDC_SIZEALL;   // kCursor_Move
        case 35: case 38: return IDC_NO;   // NoDrop / NotAllowed
        default: return IDC_ARROW;
    }
}

// Once the client window starts closing, keep-focused must stand down:
// swallowing focus-loss or faking the foreground during shutdown hangs the
// client's close path. Set in FilterProc on WM_CLOSE/WM_DESTROY. Scoped to the
// WINDOW that closed (g_closingHwnd): the game can recreate its render child,
// and a permanent latch would kill keep-focused + all UI input for the new
// window while real shutdown never presents a new window at all.
bool g_closing = false;
HWND g_closingHwnd = nullptr;
HWND g_pnApplied = nullptr;   // window WS_EX_NOPARENTNOTIFY was applied to

bool KeepFocused() {
    if (g_closing) return false;
    EnsureRenderMapped();
    return g_render && g_render->magic == rtx::render::kMagic && g_render->keepFocused;
}

// True-embed mode (child window, detached queues) -- implies keep-focused.
bool EmbeddedActive() {
    if (g_closing) return false;
    EnsureRenderMapped();
    return g_render && g_render->magic == rtx::render::kMagic && g_render->embedded;
}

// In-game window UI engaged (any window/menu visible launcher-side).
bool UiActive() {
    if (g_closing) return false;
    EnsureInputMapped();
    return g_input && g_input->magic == rtx::input::kMagic &&
           g_input->version == rtx::input::kVersion && g_input->active;
}

// ---- foreground/active/focus query capture ----------------------------------
typedef HWND (WINAPI* GetWnd_t)();
typedef BOOL (WINAPI* TranslateMsg_t)(const MSG*);
GetWnd_t       g_origGetForeground = nullptr;
GetWnd_t       g_origGetActive     = nullptr;
GetWnd_t       g_origGetFocus      = nullptr;
TranslateMsg_t g_origTranslate     = nullptr;
bool           g_apiHooked         = false;

HWND WINAPI GetForeground_hook() {
    if (g_gameWindow && KeepFocused()) return g_gameWindow;
    return g_origGetForeground ? g_origGetForeground() : nullptr;
}
HWND WINAPI GetActive_hook() {
    if (g_gameWindow && KeepFocused()) return g_gameWindow;
    return g_origGetActive ? g_origGetActive() : nullptr;
}
HWND WINAPI GetFocus_hook() {
    if (g_gameWindow && KeepFocused()) return g_gameWindow;
    return g_origGetFocus ? g_origGetFocus() : nullptr;
}
// Embedded: the host already translated relayed keys and relays the WM_CHARs too;
// translating relayed key-downs again would post a second WM_CHAR per character.
BOOL WINAPI Translate_hook(const MSG* m) {
    if (m && g_gameWindow && m->hwnd == g_gameWindow &&
        (m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN) && EmbeddedActive())
        return FALSE;
    return g_origTranslate ? g_origTranslate(m) : FALSE;
}

void InstallApiHooks() {
    if (g_apiHooked) return;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    g_origGetForeground = reinterpret_cast<GetWnd_t>(GetProcAddress(u, "GetForegroundWindow"));
    g_origGetActive     = reinterpret_cast<GetWnd_t>(GetProcAddress(u, "GetActiveWindow"));
    g_origGetFocus      = reinterpret_cast<GetWnd_t>(GetProcAddress(u, "GetFocus"));
    g_origTranslate     = reinterpret_cast<TranslateMsg_t>(GetProcAddress(u, "TranslateMessage"));
    if (!g_origGetForeground || !g_origGetActive || !g_origGetFocus || !g_origTranslate) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(g_origGetForeground), reinterpret_cast<PVOID>(GetForeground_hook));
    DetourAttach(&reinterpret_cast<PVOID&>(g_origGetActive),     reinterpret_cast<PVOID>(GetActive_hook));
    DetourAttach(&reinterpret_cast<PVOID&>(g_origGetFocus),      reinterpret_cast<PVOID>(GetFocus_hook));
    DetourAttach(&reinterpret_cast<PVOID&>(g_origTranslate),     reinterpret_cast<PVOID>(Translate_hook));
    if (DetourTransactionCommit() == NO_ERROR) g_apiHooked = true;
}

// Embedded: relayed keys never went through this queue, so its key state would stay
// permanently stale. Mirror each relayed key into the queue state; modifiers from
// the async (global) state.
void SyncKeyState(WPARAM vk, bool down) {
    BYTE ks[256];
    if (!GetKeyboardState(ks)) return;
    auto set = [&](int k, bool isDown) {
        ks[k] = isDown ? (BYTE)(ks[k] | 0x80) : (BYTE)(ks[k] & ~0x80);
    };
    if (vk < 256) set((int)vk, down);
    const int mods[] = { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
                         VK_MENU, VK_LMENU, VK_RMENU };
    for (int m : mods) set(m, (GetAsyncKeyState(m) & 0x8000) != 0);
    SetKeyboardState(ks);
}

// Keys the game's queue believes are held. When keep-focused swallows a focus-loss,
// the game skips its "release everything" path and the physical key-up lands in the
// newly focused window, so a held key (Alt during Alt+Tab) sticks forever. On each
// swallow, synthesize the missing key-ups.
bool g_keyDown[256] = {};

void ReleaseHeldKeys(HWND hwnd) {
    for (int vk = 0; vk < 256; ++vk) {
        if (!g_keyDown[vk]) continue;
        g_keyDown[vk] = false;
        UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
        LPARAM lp = (LPARAM)(((sc & 0xff) << 16) | 0xC0000001u);   // transition + prev-down bits = a key-up
        UINT m = (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_F10) ? WM_SYSKEYUP : WM_KEYUP;
        PostMessageW(hwnd, m, (WPARAM)vk, lp);
    }
}

// Diagnostic: log the focus/activation/paint messages the game receives.
#define RTX_DIAG 0
// Diagnostic: force the throttle-message swallow regardless of keepFocused.
#define RTX_FORCE_KEEPFOCUS 0
#if RTX_DIAG
void DiagMsg(UINT msg, WPARAM wp) {
    const char* n = nullptr;
    switch (msg) {
        case WM_MOUSEACTIVATE:     n = "WM_MOUSEACTIVATE"; break;
        case WM_ACTIVATE:          n = "WM_ACTIVATE"; break;
        case WM_ACTIVATEAPP:       n = "WM_ACTIVATEAPP"; break;
        case WM_NCACTIVATE:        n = "WM_NCACTIVATE"; break;
        case WM_SETFOCUS:          n = "WM_SETFOCUS"; break;
        case WM_KILLFOCUS:         n = "WM_KILLFOCUS"; break;
        case WM_WINDOWPOSCHANGING: n = "WM_WINDOWPOSCHANGING"; break;
        case WM_WINDOWPOSCHANGED:  n = "WM_WINDOWPOSCHANGED"; break;
        case WM_SIZE:              n = "WM_SIZE"; break;
        case WM_ERASEBKGND:        n = "WM_ERASEBKGND"; break;
        case WM_PAINT:             n = "WM_PAINT"; break;
        case WM_SYNCPAINT:         n = "WM_SYNCPAINT"; break;
        case WM_ENABLE:            n = "WM_ENABLE"; break;
        case WM_SHOWWINDOW:        n = "WM_SHOWWINDOW"; break;
        default: return;
    }
    char buf[128];
    wsprintfA(buf, "[%lu] gameMsg %s wp=%lu fg=%p",
              (unsigned long)GetTickCount64(), n, (unsigned long)wp, GetForegroundWindow());
    DiagLogLine(buf);
}
#endif

LRESULT CALLBACK FilterProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#if RTX_DIAG
    DiagMsg(msg, wparam);
#endif
    if (msg == WM_CLOSE || msg == WM_DESTROY || msg == WM_NCDESTROY || msg == WM_QUIT) {
        g_closing = true;   // stand down -- see KeepFocused(); re-armed by Install()
        g_closingHwnd = hwnd;   // ...but only for THIS window (recreation re-arms)
    }
    // Track held keys so a swallowed focus-loss can release them.
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) && wparam < 256)
        g_keyDown[wparam] = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    // Embedded: silence WM_PARENTNOTIFY at its origin (otherwise every click is a
    // synchronous send into the cross-process host = per-click lag). Must run HERE:
    // FilterProc is on the window's own thread so the style change executes inline;
    // from the present thread it deadlocks during preload (see Install()).
    if (!g_closing && g_pnApplied != hwnd && EmbeddedActive()) {
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (!(ex & WS_EX_NOPARENTNOTIFY))
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_NOPARENTNOTIFY);
        g_pnApplied = hwnd;
    }
    if (!g_closing && (KeepFocused() || RTX_FORCE_KEEPFOCUS)) {
        // Drop the focus-loss notifications the client throttles on (the poll
        // overrides cover the rest).
        bool swallow = (msg == WM_KILLFOCUS) ||
                       (msg == WM_ACTIVATEAPP && wparam == FALSE) ||
                       (msg == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE);
        if (swallow) {
#if RTX_DIAG
            char b[64];
            wsprintfA(b, "[%lu] SWALLOWED msg=0x%x", (unsigned long)GetTickCount64(), msg);
            DiagLogLine(b);
#endif
            ReleaseHeldKeys(hwnd);   // focus loss hidden from the game -> release held keys so none stick
            return 0;
        }
        // Flip side of faking focus: the client thinks it is active, so it declines
        // WM_MOUSEACTIVATE and a background client never comes forward when clicked.
        // Answer the handshake here, which also avoids DefWindowProc's child->parent
        // forward (a synchronous cross-process send when embedded), and raise the root.
        if (msg == WM_MOUSEACTIVATE) return MA_ACTIVATE;
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) {
            HWND root   = GetAncestor(hwnd, GA_ROOT);
            HWND realFg = g_origGetForeground ? g_origGetForeground() : nullptr;
            if (root && realFg && realFg != root && realFg != hwnd) SetForegroundWindow(root);
            // Embedded: notify the host (posted, never sent; don't block this thread
            // on the launcher) so it takes real keyboard focus and relays keys.
            if (root && root != hwnd && EmbeddedActive())
                PostMessageW(root, rtx::render::kMsgGameClicked, 0, 0);
        }
        // Embedded: keep this queue's key state in step with the relayed keyboard stream.
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
            if (EmbeddedActive())
                SyncKeyState(wparam, msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        }
    }
    // ---- in-game window UI consume pre-filter --------------------------------
    // Runs AFTER the keep-focused block so click-raise/kMsgGameClicked still fire
    // (the host must hold real keyboard focus for UI typing to arrive at all).
    if (!g_closing && UiActive()) {
        switch (msg) {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL: {
                int x = (int)(short)LOWORD(lparam);
                int y = (int)(short)HIWORD(lparam);
                if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
                    POINT p{ x, y };            // wheel coords are screen-space
                    ScreenToClient(hwnd, &p);
                    x = p.x; y = p.y;
                }
                SnapshotRects();
                if (msg == WM_MOUSEMOVE) {
                    // Self-heal: drop mask bits for buttons Windows says are no
                    // longer held (a release can be lost to focus loss).
                    unsigned held = ((wparam & MK_LBUTTON)  ? 1u  : 0u) |
                                    ((wparam & MK_RBUTTON)  ? 2u  : 0u) |
                                    ((wparam & MK_MBUTTON)  ? 4u  : 0u) |
                                    ((wparam & MK_XBUTTON1) ? 8u  : 0u) |
                                    ((wparam & MK_XBUTTON2) ? 16u : 0u);
                    g_uiButtons &= held;
                    g_gameButtons &= held;
                }
                unsigned bit = ButtonBit(msg, wparam);
                bool isDown = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                               msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN ||
                               msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK ||
                               msg == WM_MBUTTONDBLCLK || msg == WM_XBUTTONDBLCLK);
                bool isUp   = (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP ||
                               msg == WM_MBUTTONUP || msg == WM_XBUTTONUP);
                // A game interaction in progress (button held that started outside
                // the UI): never consume, whatever the cursor crosses. A further
                // button-down here belongs to the game too -- it must JOIN the mask
                // (every down joins exactly one mask, or the mask empties while a
                // button is still physically held and the UI hijacks the drag).
                if (g_gameButtons) {
                    if (isDown) {
                        g_gameButtons |= bit;
                        if (g_input->capture_keyboard)
                            PushUiEvent(WM_KILLFOCUS, x, y, 0, 0);   // blur the UI field
                    }
                    if (isUp) g_gameButtons &= ~bit;
                    break;
                }
                bool captured = g_uiButtons != 0;
                bool overUi   = PtInUi(x, y);
                if (isDown) {
                    if (captured || overUi) {
                        g_uiButtons |= bit;
                        PushUiEvent(msg, x, y, wparam, lparam);
                        return 0;
                    }
                    g_gameButtons |= bit;
                    // A click that goes to the GAME while the UI holds keyboard
                    // capture: tell the launcher (WM_KILLFOCUS sentinel) so the UI
                    // blurs its field and typing returns to the game.
                    if (g_input->capture_keyboard)
                        PushUiEvent(WM_KILLFOCUS, x, y, 0, 0);
                    break;
                }
                if (isUp) {
                    if (captured && (g_uiButtons & bit)) {
                        g_uiButtons &= ~bit;
                        PushUiEvent(msg, x, y, wparam, lparam);
                        return 0;
                    }
                    break;
                }
                // Moves and wheel: consume while over the UI or mid-UI-drag.
                if (captured || overUi) {
                    if (msg == WM_MOUSEMOVE) g_wasOverUi = true;
                    PushUiEvent(msg, x, y, wparam, lparam);
                    return 0;
                }
                // One transition move as the cursor LEAVES the UI: forwarded to the
                // view (NOT consumed -- the game gets it too) so the page clears
                // :hover state and hides tooltips.
                if (msg == WM_MOUSEMOVE && g_wasOverUi) {
                    g_wasOverUi = false;
                    PushUiEvent(msg, x, y, wparam, lparam);
                }
                break;
            }
            case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
            case WM_CHAR:    case WM_SYSCHAR:
                if (g_input->capture_keyboard) {
                    PushUiEvent(msg, 0, 0, wparam, lparam);
                    // Key-UPS also pass through to the game: the synthesized
                    // stuck-key releases (ReleaseHeldKeys below; the launcher's
                    // ReleaseHeldKeysToGame) arrive as ordinary WM_KEYUPs, and
                    // consuming them would leave the game's key held forever
                    // (auto-walk after Alt+Tab). A key-up for a key the game never
                    // saw down is a no-op.
                    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) break;
                    return 0;
                }
                break;
            case WM_SETCURSOR: {
                POINT p;
                if (GetCursorPos(&p)) {
                    ScreenToClient(hwnd, &p);
                    SnapshotRects();
                    if (g_uiButtons || PtInUi(p.x, p.y)) {
                        SetCursor(LoadCursorW(nullptr, Win32CursorFor(g_input->cursor_id)));
                        return TRUE;
                    }
                }
                break;
            }
            default:
                break;
        }
    } else {
        g_uiButtons = 0;
        g_gameButtons = 0;
        g_wasOverUi = false;
    }
    return CallWindowProcW(g_orig, hwnd, msg, wparam, lparam);
}

}  // namespace

void DiagLogLine(const char* line) {
    static std::mutex mu;
    static bool truncated = false;
    std::lock_guard<std::mutex> lk(mu);
    wchar_t up[MAX_PATH];
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return;
    std::wstring dir = up; dir += L"\\RuneToolsX";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\stutter-diag.log";
    std::ios::openmode mode = truncated ? std::ios::app : (std::ios::out | std::ios::trunc);
    std::ofstream f(path.c_str(), mode);   // MSVC: ofstream accepts a wide path
    if (f) { f << line << "\n"; truncated = true; }
}

bool Install(HWND hwnd) {
    if (!hwnd) return false;
    // The game recreated its render child: the close latch belonged to the OLD
    // window, so stand back up for the new one (real shutdown never gets here --
    // no new window presents). Also re-apply the per-window style cache.
    if (g_closing && g_closingHwnd && hwnd != g_closingHwnd && IsWindow(hwnd)) {
        g_closing = false;
        g_closingHwnd = nullptr;
        g_pnApplied = nullptr;
    }
    g_gameWindow = hwnd;          // refresh each frame; cheap
    InstallApiHooks();            // idempotent; covers the foreground/active POLL throttle
    EnsureRenderMapped();
    if (g_render && g_render->magic == rtx::render::kMagic) {
        // Publish where keyboard must be relayed: this window (the render-target child
        // the client takes input on), not the top-level frame.
        // WS_EX_NOPARENTNOTIFY is applied in FilterProc, not here: this runs on the
        // present (render) thread and SetWindowLongPtr(GWL_EXSTYLE) is a synchronous
        // WM_STYLECHANGING send to the window thread, which parks on the render
        // pipeline during world preload -- setting it from here deadlocks.
        g_render->inputWindow = (std::uint64_t)(std::uintptr_t)hwnd;
    }
    if (g_hwnd == hwnd && g_orig) return true;            // WndProc already ours on this window
    WNDPROC prev = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    if (!prev) return false;
    if (prev == FilterProc) { g_hwnd = hwnd; return true; }
    g_orig = prev;
    g_hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FilterProc));
#if RTX_DIAG
    {
        LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
        char b[192];
        wsprintfA(b, "WININFO rendered=%p parent=%p owner=%p root=%p style=0x%lx WS_CHILD=%d thr=%lu",
                  hwnd, GetParent(hwnd), GetWindow(hwnd, GW_OWNER), GetAncestor(hwnd, GA_ROOT),
                  (unsigned long)st, (st & WS_CHILD) ? 1 : 0,
                  GetWindowThreadProcessId(hwnd, nullptr));
        DiagLogLine(b);
    }
#endif
    return true;
}

void Uninstall() {
    if (g_hwnd && g_orig)
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_orig));
    g_hwnd = nullptr;
    g_orig = nullptr;
    if (g_apiHooked) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&reinterpret_cast<PVOID&>(g_origGetForeground), reinterpret_cast<PVOID>(GetForeground_hook));
        DetourDetach(&reinterpret_cast<PVOID&>(g_origGetActive),     reinterpret_cast<PVOID>(GetActive_hook));
        DetourDetach(&reinterpret_cast<PVOID&>(g_origGetFocus),      reinterpret_cast<PVOID>(GetFocus_hook));
        DetourDetach(&reinterpret_cast<PVOID&>(g_origTranslate),     reinterpret_cast<PVOID>(Translate_hook));
        DetourTransactionCommit();
        g_apiHooked = false;
    }
}

}  // namespace rtx::winmsg
