// Ultralight AppCore launcher. UI in launcher.html; embeds one unified
// window per running rs2client.exe via Dock. Boot phases land
// in %USERPROFILE%\RuneToolsX\logs\launcher.log (see shared/Log.h).

#include "Bridge.h"
#include "Companion.h"
#include "Dock.h"
#include "Http.h"
#include "MonitorFix.h"
#include "Overlay.h"
#include "Process.h"
#include "WinNotify.h"
#include "../shared/Log.h"

#include <AppCore/AppCore.h>
#include <Ultralight/Ultralight.h>

#include <Windows.h>
#include <ShlObj.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <string>
#include <thread>

using namespace ultralight;

namespace {

// Thin wrappers over the shared logger. launcher.log is the process-wide
// destination; per-PID files come from the Reader / window manager.
void boot_log(const std::string& msg) { rtx::log::Launcher(msg); }

void fatal(const std::string& msg) {
    rtx::log::Launcher("FATAL: " + msg);
    MessageBoxA(nullptr, rtx::log::Redact(msg).c_str(), "RuneTools",
                MB_OK | MB_ICONERROR | MB_TOPMOST);
}

std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

std::string read_file_utf8(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Must run before any Ultralight symbol is touched; otherwise
// delay-load probing misses the Ultralight\ subdir and the process
// dies with c0000139.
bool preload_ultralight_dlls(const std::filesystem::path& self) {
    const wchar_t* names[] = {
        L"UltralightCore.dll",
        L"WebCore.dll",
        L"Ultralight.dll",
        L"AppCore.dll",
    };
    auto ul_dir = self / L"Ultralight";
    for (auto name : names) {
        auto full = (ul_dir / name).wstring();
        HMODULE m = LoadLibraryExW(full.c_str(), nullptr,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m) {
            DWORD err = GetLastError();
            int n = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
            std::string p(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                p.data(), n, nullptr, nullptr);
            if (!p.empty() && p.back() == '\0') p.pop_back();
            boot_log("LoadLibrary failed: " + p +
                     " err=" + std::to_string(err));
            return false;
        }
        boot_log("Preloaded: " +
                 std::filesystem::path(name).string());
    }
    return true;
}

// Borderless frame subclass for the launcher window. WS_THICKFRAME keeps live resize and snap,
// but also paints the standard frame (seen as a white sliver above the page's title bar), so
// WM_NCCALCSIZE claims the whole window as client area; WM_NCHITTEST hands the 8px edges back
// to the OS as resize handles. Dragging comes from the page (winCmd HTCAPTION), not from here.
static WNDPROC g_launcherPrevProc = nullptr;
static LRESULT CALLBACK LauncherFrameProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCCALCSIZE:
        if (w) {
            auto* pr = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
            if (IsZoomed(h)) {
                // Maximized: the OS extends the frame past the monitor edge; inset by it or the
                // page's own edges are clipped off-screen.
                const int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                const int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                pr->rgrc[0].left += fx; pr->rgrc[0].right  -= fx;
                pr->rgrc[0].top  += fy; pr->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        const int x = (int)(short)LOWORD(l), y = (int)(short)HIWORD(l);
        RECT r; GetWindowRect(h, &r);
        const int g = 8;
        const bool L = x < r.left + g, R = x >= r.right - g;
        const bool T = y < r.top + g,  B = y >= r.bottom - g;
        if (!IsZoomed(h)) {
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
        }
        break; }
    }
    return CallWindowProcW(g_launcherPrevProc, h, m, w, l);
}

class LauncherApp : public WindowListener, public LoadListener, public AppListener {
public:
    bool ok = false;

    LauncherApp() {
        auto self = exe_dir();
        boot_log("exe dir: " + self.string());
        SetCurrentDirectoryW(self.c_str());

        auto resources = (self / "Ultralight" / "resources").string() + "/";
        for (auto& c : resources) if (c == '\\') c = '/';
        boot_log("resource path: " + resources);

        Settings settings;
        settings.app_name       = String("RuneToolsX");
        settings.developer_name = String("RuneTools");
        // Render the UI on the CPU by DEFAULT, not D3D11: with ~8 game clients saturating VRAM the GPU
        // path exhausts the GPU and the D3D11 device fails, crashing the games too. CPU rendering keeps
        // VRAM free for the games; its cost is scroll-repaint artifacts. RTX_GPU_RENDER=1 switches to
        // D3D11 -- keep it OFF.
        bool useGpu = false;
        {
            char buf[8] = {0};
            DWORD n = GetEnvironmentVariableA("RTX_GPU_RENDER", buf, (DWORD)sizeof(buf));
            if (n > 0 && n < sizeof(buf))
                useGpu = (buf[0] == '1' || buf[0] == 't' || buf[0] == 'T' || buf[0] == 'y' || buf[0] == 'Y');
        }
        settings.force_cpu_renderer = !useGpu;
        boot_log(useGpu ? "renderer: GPU/D3D11 (RTX_GPU_RENDER set) -- watch for D3D11 crashes with many clients"
                        : "renderer: CPU (default)");

        Config config;
        config.resource_path_prefix = String(resources.c_str());
        // Persistent WebCore storage (localStorage / IndexedDB / cookies): without a writable
        // cache_path WebKit's storage is a SILENT NO-OP - every panel's localStorage read
        // returned null, which made "stored" state (ports plan, gear, alert bells, snapshots)
        // evaporate between repaints. %LOCALAPPDATA%\RuneToolsX\webcache, exe-relative fallback.
        {
            std::filesystem::path cacheDir;
            wchar_t* lad = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &lad))) {
                cacheDir = std::filesystem::path(lad) / L"RuneToolsX" / L"webcache";
                CoTaskMemFree(lad);
            } else {
                cacheDir = self / "webcache";
            }
            std::error_code ec;
            std::filesystem::create_directories(cacheDir, ec);
            std::string cp = cacheDir.string();
            for (auto& c : cp) if (c == '\\') c = '/';
            config.cache_path = String(cp.c_str());
            boot_log("webcore cache path: " + cp);
        }

        boot_log("App::Create...");
        app_ = App::Create(settings, config);
        if (!app_) { fatal("App::Create returned null"); return; }

        auto client_html = (self / "client.html").string();
        // Dev override: point the in-game panel UI at a source checkout so the dock hot-reloads
        // open panels on change (no rebuild/restart). Source order: RTX_UI_DIR env var, else the
        // first line of `rtx_ui_dev.txt` next to the exe (survives launch methods that don't
        // inherit fresh user env vars, e.g. a VS debugger opened before setx).
        bool uiDev = false;
        {
            std::string dir;
            char buf[512] = {0};
            DWORD n = GetEnvironmentVariableA("RTX_UI_DIR", buf, (DWORD)sizeof(buf));
            if (n > 0 && n < sizeof(buf)) dir = buf;
            if (dir.empty()) {
                std::ifstream mk(self / "rtx_ui_dev.txt");
                if (mk) { std::getline(mk, dir);
                          while (!dir.empty() && (dir.back() == '\r' || dir.back() == ' ')) dir.pop_back(); }
            }
            if (!dir.empty()) {
                std::error_code ec;
                std::filesystem::path dev(dir);
                if (std::filesystem::exists(dev / "client.html", ec)) {
                    client_html = (dev / "client.html").string();
                    uiDev = true;
                    boot_log("ui dev override: " + client_html);
                } else {
                    boot_log("ui dev dir set but no client.html at '" + dir + "'; using the exe dir");
                }
            }
        }
        rtx::launcher::dock::Init(app_.get(), client_html, uiDev);
        app_->set_listener(this);

        boot_log("Window::Create...");
        // Borderless: the page draws its own title bar (drag region + min/close through the
        // bridge's winCmd), so the OS chrome never breaks the dark theme. Resizable keeps the
        // sizing borders; WS_CAPTION is added below so snap and minimize animations survive.
        window_ = Window::Create(app_->main_monitor(), 460, 640, false,
                                 kWindowFlags_Borderless |
                                 kWindowFlags_Resizable);
        if (!window_) { fatal("Window::Create returned null"); return; }
        window_->SetTitle("RuneTools");
        window_->set_listener(this);

        // Topmost-then-not bypasses anti-focus-stealing.
        HWND hwnd = static_cast<HWND>(window_->native_handle());
        rtx::launcher::SetLauncherWindow(hwnd);
        if (hwnd) {
            // Borderless but still a first-class app window: WS_THICKFRAME keeps resize edges,
            // WS_MINIMIZEBOX + WS_SYSMENU keep taskbar minimize and Alt+Space/Win+Down working.
            // No WS_CAPTION, so no OS title bar paints over the page's own.
            LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
            st |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU;
            SetWindowLongPtrW(hwnd, GWL_STYLE, st);
            g_launcherPrevProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(LauncherFrameProc)));
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            HMODULE hMod = GetModuleHandleW(nullptr);
            HICON hBig = (HICON)LoadImageW(hMod, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                           GetSystemMetrics(SM_CXICON),
                                           GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
            HICON hSm  = (HICON)LoadImageW(hMod, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                           GetSystemMetrics(SM_CXSMICON),
                                           GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
            if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
            if (hSm)  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSm);

            SetWindowPos(hwnd, HWND_TOPMOST,   0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(hwnd);
            BringWindowToTop(hwnd);
            SetActiveWindow(hwnd);

            // Keep the AppCore run loop ticking when idle so OnUpdate (dock::Tick: embed retries,
            // dead-client cleanup) runs without user input. 100 ms lets the single CPU render thread sit
            // idle in GetMessage between ticks, so it can pump an embedded client's host activation
            // immediately on a window switch instead of behind a forced render.
            SetTimer(hwnd, 1, 100, nullptr);

            // Tray icon + minimize-to-tray for the launcher window: minimizing hides it
            // from the taskbar, the tray icon (click or Open) brings it back. Game host
            // windows are untouched -- they minimize normally.
            rtx::winnotify::EnableTray(hwnd);
        }

        overlay_ = Overlay::Create(window_, window_->width(),
                                   window_->height(), 0, 0);
        if (!overlay_) { fatal("Overlay::Create returned null"); return; }
        overlay_->view()->set_load_listener(this);

        auto html_path = self / "launcher.html";
        auto html = read_file_utf8(html_path);
        if (html.empty()) {
            fatal("launcher.html not found or empty:\n" + html_path.string());
            return;
        }
        boot_log("Loading " + std::to_string(html.size()) + " bytes of HTML");
        overlay_->view()->LoadHTML(String(html.c_str()));
        ok = true;
    }

    void Run() {
        if (!ok) return;
        boot_log("Running...");
        app_->Run();
        boot_log("Run returned");
    }

    void OnClose(Window*) override { app_->Quit(); }
    void OnResize(Window*, uint32_t w, uint32_t h) override {
        if (overlay_) overlay_->Resize(w, h);
    }

    // Fired each run-loop iteration. Keep each docked panel positioned/sized to track
    // its game window (on the main thread).
    void OnUpdate() override {
        rtx::launcher::dock::Tick();
    }

    // Attach on both events: window-object-ready can fire only for the
    // initial about:blank context, so DOM-ready is the fallback. Both
    // are idempotent (property overwrite, not leak).
    void OnWindowObjectReady(View* view, uint64_t, bool is_main,
                             const String& url) override {
        if (!is_main) return;
        boot_log(std::string("window object ready (") +
                 url.utf8().data() + "); attaching JS bridge");
        rtx::launcher::AttachBridge(view);
    }
    void OnDOMReady(View* view, uint64_t, bool is_main,
                    const String& url) override {
        if (!is_main) return;
        boot_log(std::string("DOM ready (") +
                 url.utf8().data() + "); re-attaching JS bridge");
        rtx::launcher::AttachBridge(view);
    }

private:
    RefPtr<App>     app_;
    RefPtr<Window>  window_;
    RefPtr<Overlay> overlay_;
};

}  // namespace

// Per-monitor DPI awareness, declared BEFORE any window exists. The dock/overlay
// pipeline mixes coordinates from this process (GetClientRect / ClientToScreen on
// the game window) with pixel data read out of the game itself (gameview viewport,
// panel varcs), and the game renders in physical pixels. Without this declaration
// the process runs DPI-virtualized on scaled displays (125%...): the two spaces
// disagree by the scale factor and overlays land off target -- and the existing
// per-monitor plumbing (SyncUiDpi, WM_DPICHANGED) never fires because
// GetDpiForWindow reports 96 to an unaware process. Resolved dynamically so the
// build works on any SDK; falls back v2 -> per-monitor v1 -> system-aware.
void DeclareDpiAwareness() {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    auto setCtx = reinterpret_cast<SetCtxFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (DPI_AWARENESS_CONTEXT)-4
    if (setCtx && setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) {
        boot_log("dpi: per-monitor v2");
        return;
    }
    using SetAwFn = HRESULT(WINAPI*)(int);
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    auto setAw = shcore ? reinterpret_cast<SetAwFn>(
        GetProcAddress(shcore, "SetProcessDpiAwareness")) : nullptr;
    if (setAw && SUCCEEDED(setAw(2 /* PROCESS_PER_MONITOR_DPI_AWARE */))) {
        boot_log("dpi: per-monitor v1");
        return;
    }
    boot_log(SetProcessDPIAware() ? "dpi: system-aware (legacy fallback)"
                                  : "dpi: awareness NOT set -- overlays may misalign on scaled displays");
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    rtx::log::Init();
    boot_log("=== RuneToolsX starting ===");
    DeclareDpiAwareness();

    // Named mutex matching the installer's AppMutex (RuneToolsX.iss) so a silent
    // in-place update can detect + close this launcher via the Restart Manager.
    // Held for the process lifetime (handle intentionally leaked).
    CreateMutexW(nullptr, FALSE, L"RuneToolsXLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A launcher is already running (possibly minimized). Two launchers publish into the SAME
        // per-client overlay sections and alternate frames, so every highlight/outline blinks.
        // Hand focus to the existing instance and bow out.
        boot_log("another RuneToolsX launcher is already running; asking it to show itself and exiting");
        // The running instance's tray window handles this (WinNotify.cpp) via restore_main(),
        // which un-hides a tray-parked window as well as restoring a minimized one.
        // That tray window is MESSAGE-ONLY, so HWND_BROADCAST never reaches it: look it up by
        // class under HWND_MESSAGE (every instance, in case the stale mutex owner has no tray yet).
        const UINT showMain = RegisterWindowMessageW(L"RuneToolsX.ShowMain");
        AllowSetForegroundWindow(ASFW_ANY);
        HWND tray = nullptr; int sent = 0;
        while ((tray = FindWindowExW(HWND_MESSAGE, tray, L"RuneToolsXNotifyWnd", nullptr)) != nullptr) {
            DWORD_PTR res = 0;
            SendMessageTimeoutW(tray, showMain, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
            ++sent;
        }
        boot_log("single-instance: notified " + std::to_string(sent) + " tray window(s)");
        return 0;
    }

    auto self = exe_dir();
    if (!preload_ultralight_dlls(self)) {
        fatal("Failed to load Ultralight runtime DLLs from "
              "Ultralight\\ subdir next to the exe. Rebuild to ensure "
              "the StageRuntime MSBuild target copied them.");
        return 1;
    }

    // Must run after AppCore.dll is loaded (above) and before App::Create so
    // the very first monitor query is already routed through the fix.
    boot_log(rtx::launcher::InstallMonitorInfoFix()
                 ? "GetMonitorInfoW fix installed in AppCore.dll"
                 : "GetMonitorInfoW fix NOT installed (import not found)");

    // Bring the companion up the INSTANT a client process appears -- before it logs in or renders its
    // first world scene -- so its observers are present from the start (a later setup misses the
    // startup scene pass that records render-pass slots like the clue-scan ring). Idempotent.
    static std::atomic<bool> g_scan_stop{false};
    std::thread([] {
        while (!g_scan_stop.load()) {
            bool any_loaded = false;
            try {
                for (const auto& info : rtx::launcher::process::ScanRsClients()) {
                    if (_wcsicmp(info.name.c_str(), L"rs2client.exe") == 0 &&
                        rtx::launcher::companion::EnsureLoaded(info.pid))
                        any_loaded = true;
                }
            } catch (const std::exception& e) {
                boot_log(std::string("companion scan: ") + e.what());
            } catch (...) {
                boot_log("companion scan: non-std exception");
            }
            // 100 ms while no client is attached (the first world render must not be missed);
            // 500 ms once one is live: the fast path is then just a section-open per tick.
            Sleep(any_loaded ? 500 : 100);
        }
    }).detach();

    try {
        LauncherApp app;
        app.Run();
        // Run() returned = the user closed the window. From here the render/overlay/reader threads
        // get torn down; flag shutdown so a thread that faults in that race isn't logged as a crash.
        rtx::log::BeginShutdown();
        rtx::overlay::Stop();   // join the overlay render thread before teardown
        rtx::launcher::dock::Shutdown();   // restore game windows + close docked panels
        rtx::winnotify::Shutdown();   // remove the notification tray icon
        g_scan_stop.store(true);            // companion scanner loop
        rtx::launcher::http::Shutdown();    // one-shot network worker
    } catch (const std::exception& e) {
        fatal(std::string("Unhandled exception: ") + e.what());
        return 1;
    } catch (...) {
        fatal("Unhandled non-std exception during startup");
        return 1;
    }
    boot_log("=== Clean exit ===");
    // HARD STOP -- same rule as the updater: TerminateProcess, never the CRT exit path. `return 0`
    // runs static destructors and DLL detaches, and Ultralight/WebCore teardown or a worker parked
    // in a synchronous send into a dead client (the embed worker blocks for minutes by design) keeps
    // the PROCESS alive as a windowless corpse with the exe LOCKED. Everything above already shut
    // down in order; the OS reclaims the rest instantly.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;   // unreachable
}
