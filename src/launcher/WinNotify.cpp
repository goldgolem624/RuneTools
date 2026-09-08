#include "WinNotify.h"

#include <Windows.h>
#include <shellapi.h>

#include <mutex>

#pragma comment(lib, "shell32.lib")

namespace rtx::winnotify {

namespace {

std::mutex g_mu;
HWND       g_hwnd  = nullptr;   // hidden owner window for the tray icon
bool       g_added = false;     // tray icon currently registered
HWND       g_main  = nullptr;   // launcher window parked in the tray on minimize (set once)
WNDPROC    g_mainPrev = nullptr;   // its original wndproc (subclass chain)
constexpr UINT kTrayId  = 1;
constexpr UINT kMsgTray = WM_APP + 1;   // tray icon callback (legacy format: lParam = mouse msg)
constexpr wchar_t kClass[] = L"RuneToolsXNotifyWnd";

bool ensure_icon();

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

HICON load_app_icon(int px) {
    HICON h = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                IMAGE_ICON, px, px, LR_DEFAULTCOLOR);
    return h ? h : LoadIconW(nullptr, IDI_APPLICATION);
}

void restore_main() {
    HWND w = g_main;
    if (!w || !IsWindow(w)) return;
    ShowWindow(w, SW_SHOW);
    if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
    SetForegroundWindow(w);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // Explorer restart kills every tray icon: re-register on the shell's broadcast.
    static const UINT s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (m == s_taskbarCreated) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_added = false;
        ensure_icon();
        return 0;
    }
    // Second-instance "show yourself" broadcast (see main.cpp single-instance guard).
    static const UINT s_showMain = RegisterWindowMessageW(L"RuneToolsX.ShowMain");
    if (m == s_showMain) { restore_main(); return 0; }
    if (m == kMsgTray) {
        if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) { restore_main(); return 0; }
        if (l == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Open RuneTools");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 2, L"Exit");
            POINT pt; GetCursorPos(&pt);
            // Owner must be foreground or the menu never dismisses; WM_NULL kick per MSDN.
            SetForegroundWindow(h);
            UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                            pt.x, pt.y, 0, h, nullptr);
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd == 1) restore_main();
            else if (cmd == 2 && g_main && IsWindow(g_main)) PostMessageW(g_main, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

// Launcher window subclass: minimize completes, then the window hides (tray icon is the way back).
LRESULT CALLBACK MainSubclass(HWND h, UINT m, WPARAM w, LPARAM l) {
    WNDPROC prev = g_mainPrev;
    LRESULT r = prev ? CallWindowProcW(prev, h, m, w, l) : DefWindowProcW(h, m, w, l);
    if (m == WM_SIZE && w == SIZE_MINIMIZED && g_added) ShowWindow(h, SW_HIDE);
    return r;
}

// Caller holds g_mu.
bool ensure_icon() {
    if (g_added) return true;
    if (!g_hwnd) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);   // harmless if already registered
        g_hwnd = CreateWindowExW(0, kClass, L"RuneTools", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!g_hwnd) return false;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;   // NIF_MESSAGE: clicks arrive as kMsgTray
    nid.uCallbackMessage = kMsgTray;
    nid.hIcon  = load_app_icon(GetSystemMetrics(SM_CXSMICON));
    wcscpy_s(nid.szTip, L"RuneTools");
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    g_added = true;
    return true;
}

}  // namespace

bool Show(const std::string& title, const std::string& body) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!ensure_icon()) return false;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags  = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = load_app_icon(32);

    std::wstring wt = widen(title), wb = widen(body);
    wcsncpy_s(nid.szInfoTitle, wt.c_str(), _TRUNCATE);   // 64-wchar cap
    wcsncpy_s(nid.szInfo,      wb.c_str(), _TRUNCATE);    // 256-wchar cap
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
}

void EnableTray(void* mainHwnd) {
    std::lock_guard<std::mutex> lk(g_mu);
    HWND w = reinterpret_cast<HWND>(mainHwnd);
    if (!w || !IsWindow(w) || g_main) return;
    if (!ensure_icon()) return;   // the icon is the only way back: no icon, no hiding
    g_main = w;
    g_mainPrev = (WNDPROC)SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)MainSubclass);
}

void Shutdown() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_added && g_hwnd) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = g_hwnd;
        nid.uID    = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_added = false;
    }
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
}

}  // namespace rtx::winnotify
