// Window-message filter + foreground-query capture (see InputFilter.h).

#include "InputFilter.h"
#include "RenderShare.h"
#include "InputShare.h"

#include <detours.h>
#include <fstream>
#include <mutex>
#include <string>

namespace rtx::winmsg {
namespace {

WNDPROC             g_orig       = nullptr;
HWND                g_hwnd       = nullptr;
HWND                g_gameWindow = nullptr;
rtx::render::Share* g_render     = nullptr;
HANDLE              g_renderMap  = nullptr;

void EnsureRenderMapped() {
    static std::uint32_t s_renderGen = 0;
    rtx::ipc::RebindIfStale(s_renderGen, g_render, g_renderMap);
    if (g_render) return;
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[rtx::ipc::kNameChars];
    rtx::render::MakeSectionName(GetCurrentProcessId(), name);
    g_renderMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!g_renderMap) return;
    g_render = reinterpret_cast<rtx::render::Share*>(
        MapViewOfFile(g_renderMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                      sizeof(rtx::render::Share)));
    if (!g_render) { CloseHandle(g_renderMap); g_renderMap = nullptr; }
}

rtx::input::Share* g_input    = nullptr;
HANDLE             g_inputMap = nullptr;
HANDLE             g_inputEvt = nullptr;

void EnsureInputMapped() {
    static std::uint32_t s_inputGen = 0;
    if (rtx::ipc::SessionChanged(s_inputGen)) {
        g_input = nullptr; g_inputMap = nullptr; g_inputEvt = nullptr;
    }
    if (g_input && g_inputEvt) return;
    static ULONGLONG s_nextTry = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;
    wchar_t name[rtx::ipc::kNameChars];
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

bool UiActive();

// Consume-rect snapshot under the writer's seqlock; a torn read keeps the previous one.
rtx::input::Rect g_uiRects[rtx::input::kMaxRects];
std::uint32_t    g_uiRectCount = 0;

void SnapshotRects() {
    std::uint32_t seq = g_input->rect_seq;
    if (seq & 1u) return;
    std::uint32_t n = g_input->rect_count;
    if (n > rtx::input::kMaxRects) n = rtx::input::kMaxRects;
    rtx::input::Rect tmp[rtx::input::kMaxRects];
    for (std::uint32_t i = 0; i < n; ++i) tmp[i] = g_input->rects[i];
    if (g_input->rect_seq != seq) return;
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
    // Moves/wheel are droppable; keep 8 slots of headroom so button-ups and keys never meet a full ring.
    bool lowPri = (msg == WM_MOUSEMOVE || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL);
    std::uint32_t cap = lowPri ? (rtx::input::kRingSize - 8) : rtx::input::kRingSize;
    if (head - tail >= cap) return;
    rtx::input::Event& e = g_input->events[head & (rtx::input::kRingSize - 1)];
    e.msg = msg;
    e.x = x; e.y = y;
    e.wparam = (std::int64_t)wp;
    e.lparam = (std::int64_t)lp;
    e.t = GetTickCount64();
    MemoryBarrier();                        // fields visible before head moves
    g_input->head = head + 1;
    if (g_inputEvt) SetEvent(g_inputEvt);
}

unsigned g_uiButtons   = 0;
unsigned g_gameButtons = 0;
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

// Ultralight cursor id (Listener.h enum Cursor) -> Win32 IDC_*.
LPCWSTR Win32CursorFor(std::uint32_t id) {
    switch (id) {
        case 2:  return IDC_HAND;
        case 3:  return IDC_IBEAM;
        case 4:  return IDC_WAIT;
        case 5:  return IDC_HELP;
        case 1:  return IDC_CROSS;
        case 6:  case 13: case 15: case 18: return IDC_SIZEWE;
        case 7:  case 10: case 14: case 19: return IDC_SIZENS;
        case 8:  case 12: case 16: return IDC_SIZENESW;
        case 9:  case 11: case 17: return IDC_SIZENWSE;
        case 29: return IDC_SIZEALL;
        case 35: case 38: return IDC_NO;
        default: return IDC_ARROW;
    }
}

bool g_closing = false;
HWND g_closingHwnd = nullptr;
HWND g_pnApplied = nullptr;   // window WS_EX_NOPARENTNOTIFY was applied to

bool KeepFocused() {
    if (g_closing) return false;
    EnsureRenderMapped();
    return g_render && g_render->magic == rtx::render::kMagic && g_render->keepFocused;
}

bool EmbeddedActive() {
    if (g_closing) return false;
    EnsureRenderMapped();
    return g_render && g_render->magic == rtx::render::kMagic && g_render->embedded;
}

bool UiActive() {
    if (g_closing) return false;
    EnsureInputMapped();
    return g_input && g_input->magic == rtx::input::kMagic &&
           g_input->version == rtx::input::kVersion && g_input->active;
}

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

bool g_keyDown[256] = {};

void ReleaseHeldKeys(HWND hwnd) {
    for (int vk = 0; vk < 256; ++vk) {
        if (!g_keyDown[vk]) continue;
        g_keyDown[vk] = false;
        UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
        LPARAM lp = (LPARAM)(((sc & 0xff) << 16) | 0xC0000001u);   // transition + prev-down bits
        UINT m = (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_F10) ? WM_SYSKEYUP : WM_KEYUP;
        PostMessageW(hwnd, m, (WPARAM)vk, lp);
    }
}

#define RTX_DIAG 0
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
        g_closing = true;
        g_closingHwnd = hwnd;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) && wparam < 256)
        g_keyDown[wparam] = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    // Embedded: WS_EX_NOPARENTNOTIFY stops per-click synchronous sends to the host. Must be set from the
    // window's own thread; from the present thread it deadlocks during preload.
    if (!g_closing && g_pnApplied != hwnd && EmbeddedActive()) {
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (!(ex & WS_EX_NOPARENTNOTIFY))
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_NOPARENTNOTIFY);
        g_pnApplied = hwnd;
    }
    if (!g_closing && (KeepFocused() || RTX_FORCE_KEEPFOCUS)) {
        bool swallow = (msg == WM_KILLFOCUS) ||
                       (msg == WM_ACTIVATEAPP && wparam == FALSE) ||
                       (msg == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE);
        if (swallow) {
#if RTX_DIAG
            char b[64];
            wsprintfA(b, "[%lu] SWALLOWED msg=0x%x", (unsigned long)GetTickCount64(), msg);
            DiagLogLine(b);
#endif
            ReleaseHeldKeys(hwnd);
            return 0;
        }
        if (msg == WM_MOUSEACTIVATE) return MA_ACTIVATE;
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) {
            HWND root   = GetAncestor(hwnd, GA_ROOT);
            HWND realFg = g_origGetForeground ? g_origGetForeground() : nullptr;
            if (root && realFg && realFg != root && realFg != hwnd) SetForegroundWindow(root);
            if (root && root != hwnd && EmbeddedActive())
                PostMessageW(root, rtx::render::kMsgGameClicked, 0, 0);
        }
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
            if (EmbeddedActive())
                SyncKeyState(wparam, msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        }
    }
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
                if (g_gameButtons) {
                    if (isDown) {
                        g_gameButtons |= bit;
                        if (g_input->capture_keyboard)
                            PushUiEvent(WM_KILLFOCUS, x, y, 0, 0);
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
                if (captured || overUi) {
                    if (msg == WM_MOUSEMOVE) g_wasOverUi = true;
                    PushUiEvent(msg, x, y, wparam, lparam);
                    return 0;
                }
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
    std::ofstream f(path.c_str(), mode);
    if (f) { f << line << "\n"; truncated = true; }
}

bool Install(HWND hwnd) {
    if (!hwnd) return false;
    if (g_closing && g_closingHwnd && hwnd != g_closingHwnd && IsWindow(hwnd)) {
        g_closing = false;
        g_closingHwnd = nullptr;
        g_pnApplied = nullptr;
    }
    g_gameWindow = hwnd;
    InstallApiHooks();
    EnsureRenderMapped();
    if (g_render && g_render->magic == rtx::render::kMagic) {
        g_render->inputWindow = (std::uint64_t)(std::uintptr_t)hwnd;
    }
    if (g_hwnd == hwnd && g_orig) return true;
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
