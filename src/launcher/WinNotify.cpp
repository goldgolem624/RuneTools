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
constexpr UINT kTrayId = 1;
constexpr wchar_t kClass[] = L"RuneToolsXNotifyWnd";

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

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

// Create the hidden window + register the tray icon once. Caller holds g_mu.
bool ensure_icon() {
    if (g_added) return true;
    if (!g_hwnd) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);   // harmless if already registered (returns 0 + ERROR_CLASS_ALREADY_EXISTS)
        g_hwnd = CreateWindowExW(0, kClass, L"RuneTools", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!g_hwnd) return false;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP;
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
    // Use the app icon in the toast (large), not the generic info glyph.
    nid.dwInfoFlags  = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = load_app_icon(32);

    std::wstring wt = widen(title), wb = widen(body);
    wcsncpy_s(nid.szInfoTitle, wt.c_str(), _TRUNCATE);   // 64-wchar cap
    wcsncpy_s(nid.szInfo,      wb.c_str(), _TRUNCATE);    // 256-wchar cap
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
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
