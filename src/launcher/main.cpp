// Ultralight AppCore launcher. UI in launcher.html; one docked window per rs2client.exe via Dock.

#include "Bridge.h"
#include "Companion.h"
#include "Dock.h"
#include "Http.h"
#include "IconCache.h"
#include "LuaHost.h"
#include "Loader.h"
#include "Loot.h"
#include "Music.h"
#include "MonitorFix.h"
#include "Overlay.h"
#include "Process.h"
#include "../reader/Reader.h"
#include "../cache/CacheReader.h"
#include "WinNotify.h"
#include "../shared/Log.h"

#include <AppCore/AppCore.h>
#include <Ultralight/Ultralight.h>

#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <cwchar>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <string>
#include <vector>
#include <thread>

using namespace ultralight;

namespace {

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

// Must run before any Ultralight symbol is touched (delay-load misses the Ultralight\ subdir, c0000139).
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

static WNDPROC g_launcherPrevProc = nullptr;

// Started again by the installer after an update or a repair. The installer runs unseen, so
// Windows hands the focus back to whatever was in front before it, and the launcher that has
// just come back ends up behind that window: to the user it never reopened. It steps in front
// again a few times while that settles, and flashes its taskbar button if it is still refused.
constexpr UINT_PTR kFrontTimer = 2;
static int g_frontTries = 0;
static void BringLauncherToFront(HWND h) {
    if (GetForegroundWindow() == h) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetWindowPos(h, HWND_TOPMOST,   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(h);
    if (GetForegroundWindow() != h) {
        FLASHWINFO f{ sizeof(f), h, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0 };
        FlashWindowEx(&f);
    }
}

static LRESULT CALLBACK LauncherFrameProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        if (w == kFrontTimer) {
            BringLauncherToFront(h);
            if (++g_frontTries >= 3) KillTimer(h, kFrontTimer);
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, w, (LPARAM)-1);
    case WM_NCCALCSIZE:
        if (w) {
            auto* pr = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
            if (IsZoomed(h)) {
                const int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                const int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                pr->rgrc[0].left += fx; pr->rgrc[0].right  -= fx;
                pr->rgrc[0].top  += fy; pr->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_SIZE:
        // minimised: no music into an empty desktop. It comes back with the window.
        rtx::launcher::music::Minimised(w == SIZE_MINIMIZED);
        break;
    case WM_GETMINMAXINFO: {
        LRESULT r0 = CallWindowProcW(g_launcherPrevProc, h, m, w, l);
        using DpiFn = UINT(WINAPI*)(HWND);
        static DpiFn dpiFn = reinterpret_cast<DpiFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        const double k = (dpiFn && dpiFn(h)) ? dpiFn(h) / 96.0 : 1.0;
        auto* mi = reinterpret_cast<MINMAXINFO*>(l);
        mi->ptMinTrackSize.x = (LONG)(860 * k);
        mi->ptMinTrackSize.y = (LONG)(600 * k);
        return r0; }
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
        // RTX_GPU_RENDER=1 switches to D3D11; the CPU renderer is the default.
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
        // Panel UI dev override: RTX_UI_DIR env var, else the first line of rtx_ui_dev.txt next to the exe.
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
        window_ = Window::Create(app_->main_monitor(), 1060, 700, false,
                                 kWindowFlags_Borderless |
                                 kWindowFlags_Resizable);
        if (!window_) { fatal("Window::Create returned null"); return; }
        window_->SetTitle("RuneTools");
        window_->set_listener(this);

        HWND hwnd = static_cast<HWND>(window_->native_handle());
        rtx::launcher::SetLauncherWindow(hwnd);
        if (hwnd) {
            LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
            st |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU;
            SetWindowLongPtrW(hwnd, GWL_STYLE, st);
            g_launcherPrevProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(LauncherFrameProc)));
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            { COLORREF border = DWMWA_COLOR_NONE;
              DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
              DWORD pref = DWMWCP_DONOTROUND;
              DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref)); }
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

            SetTimer(hwnd, 1, 100, nullptr);
            if (std::wcsstr(GetCommandLineW(), L"/relaunched")) SetTimer(hwnd, kFrontTimer, 1200, nullptr);

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

    void OnUpdate() override {
        rtx::launcher::dock::Tick();
    }

    static bool local_url(const String& url) {
        String8 u8 = url.utf8();
        std::string u(u8.data(), u8.length());
        return u.empty() || u.rfind("about:", 0) == 0;
    }
    void OnBeginLoading(View* view, uint64_t, bool is_main, const String& url) override {
        if (is_main && !local_url(url)) {
            view->Stop();
            boot_log("BLOCKED main-frame navigation off the launcher page");
        }
    }
    void OnWindowObjectReady(View* view, uint64_t, bool is_main,
                             const String& url) override {
        if (!is_main || !local_url(url)) return;
        boot_log("window object ready (local); attaching JS bridge");
        rtx::launcher::AttachBridge(view);
    }
    void OnDOMReady(View* view, uint64_t, bool is_main,
                    const String& url) override {
        if (!is_main || !local_url(url)) return;
        boot_log("DOM ready (local); re-attaching JS bridge");
        rtx::launcher::AttachBridge(view);
    }

private:
    RefPtr<App>     app_;
    RefPtr<Window>  window_;
    RefPtr<Overlay> overlay_;
};

}  // namespace

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
    rtx::launcher::loader::LockPageNetwork();   // before anything loads the web engine
    {   // Headless switches that must not touch the running launcher's logs (no rtx::log::Init).
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // --iface-dump <pid> [group[:comps]]: print the open interface groups (with the engine mount and the
        // resolved screen origin of each) or one group's comps to iface-dump.txt and exit. No window.
        // --health <pid>: the reader health rows for a live client to health.txt and exit. No window.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--health") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            rtx::log::Init();
            rtx::reader::SampleAll();
            Sleep(3000);
            rtx::reader::SampleAll();
            std::string out = rtx::reader::ReaderHealthJson(pid);
            { std::ofstream f("health.txt", std::ios::binary | std::ios::trunc); f << out; }
            LocalFree(argv);
            return 0;
        }
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--iface-dump") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            auto snaps = rtx::reader::SampleAll();   // attach + resolve MainData for the live client(s)
            std::string diag = "{\"clients\":[";
            for (size_t i = 0; i < snaps.size(); ++i) diag += (i ? "," : "") + std::to_string(snaps[i].pid) + ":" + (snaps[i].in_world ? "1" : "0");
            diag += "]}\n";
            std::string out;
            if (argc >= 4) {
                std::wstring spec = argv[3];
                size_t colon = spec.find(L':');
                int gid = _wtoi(spec.substr(0, colon).c_str());
                if (spec.rfind(L"cache:", 0) == 0) out = rtx::cache::IfaceGroupDefsJson(_wtoi(spec.c_str() + 6));   // js5 definitions
                else if (colon == std::wstring::npos) out = rtx::reader::InterfaceGroupJson(pid, gid);
                else {
                    std::wstring wc = spec.substr(colon + 1);
                    out = rtx::reader::InterfaceCompsJson(pid, gid, std::string(wc.begin(), wc.end()));
                }
            } else {
                out = rtx::reader::InterfaceGroupsJson(pid);
            }
            { std::ofstream f("iface-dump.txt", std::ios::binary | std::ios::trunc); f << diag << out; }
            LocalFree(argv);
            return 0;
        }
        // --scene-dump <pid> [range]: the scene as plugins receive it, to scene-dump.txt. For
        // checking what the objects around the player actually report before trusting a plugin rule.
        // The channels the companion writes (live objects, ground items, specials) are named per
        // session, so they only resolve for the launcher that owns the client: run from a second
        // process those lists come back empty and the cache-backed half is what this shows.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--scene-dump") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            int range = argc >= 4 ? _wtoi(argv[3]) : 20;
            if (range < 1) range = 1;
            if (range > 64) range = 64;
            rtx::reader::SampleAll();
            std::string out = rtx::reader::SceneJson(pid, range);
            { std::ofstream f("scene-dump.txt", std::ios::binary | std::ios::trunc); f << out; }
            LocalFree(argv);
            return 0;
        }
        // --overhead <pid>: every scene entity with its class pointer and the class entries the
        // game's overhead drawing uses, to overhead.txt. For reading those back in the binary.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--overhead") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            rtx::reader::SampleAll();
            std::string out = rtx::reader::OverheadClassJson(pid) + "\n" + rtx::reader::LiveLocsJson(pid) + "\n" + rtx::reader::OverheadBarsJson(pid);
            { std::ofstream f("overhead.txt", std::ios::binary | std::ios::trunc); f << out; }
            LocalFree(argv);
            return 0;
        }
        // --loc-dump <out.tsv>: every loc definition (name, footprint, actions, models, morphs) for offline tooling.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--loc-dump") {
            std::wstring wp = argv[2];
            int rows = rtx::cache::LocDumpTsv(std::string(wp.begin(), wp.end()));
            { std::ofstream f("loc-dump.txt", std::ios::binary | std::ios::trunc); f << rows; }
            LocalFree(argv);
            return rows >= 0 ? 0 : 1;
        }
        // --item-links <name substring>: every item whose name contains it, with the items its
        // definition links to and their names, to item-links.txt. For tracing a variant the market
        // does not list back to the form it does.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--item-links") {
            std::wstring wp = argv[2];
            const std::string needle(wp.begin(), wp.end());
            std::vector<int> ids(400);
            const int n = rtx::cache::ItemsByName(needle.c_str(), ids.data(), (int)ids.size());
            std::ofstream f("item-links.txt", std::ios::binary | std::ios::trunc);
            f << n << " items matching " << needle << "\n";
            for (int i = 0; i < n; ++i) {
                const int id = ids[i];
                rtx::cache::ItemInfo info = rtx::cache::GetItem(id);
                f << id << "\t" << info.name << "\tvalue=" << info.value
                  << (info.augmented ? "\taugmented" : "");
                int links[6];
                const int ln = rtx::cache::ItemLinkedForms(id, links);
                for (int k = 0; k < ln; ++k)
                    f << "\t-> " << links[k] << " " << rtx::cache::ItemName(links[k]);
                const int traded = rtx::cache::ItemTradeableForm(id);
                if (traded != id) f << "\tTRADED=" << traded << " " << rtx::cache::ItemName(traded);
                f << "\t|" << rtx::cache::ItemRelationsText(id) << "\n";
            }
            LocalFree(argv);
            return 0;
        }
        // --item-extra <pid> <container> <slot>: the per-instance ints of one container slot (varobj keys) to item-extra.txt.
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--item-extra") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            int cont = _wtoi(argv[3]), slot = _wtoi(argv[4]);
            rtx::reader::SampleAll();
            std::string items = rtx::reader::ContainerItemsJson(pid, cont);
            int itemId = -1;
            std::string key = "[" + std::to_string(slot) + ",";
            std::size_t at = items.find(key);
            if (at != std::string::npos) itemId = std::atoi(items.c_str() + at + key.size());
            std::string out = "{\"itemId\":" + std::to_string(itemId) + ",\"extra\":" + rtx::reader::ItemExtraIntsJson(pid, cont, itemId, slot) + "}";
            { std::ofstream f("item-extra.txt", std::ios::binary | std::ios::trunc); f << out; }
            LocalFree(argv);
            return 0;
        }
        // --enum-scan <keyA> <keyB>: every cache enum holding int values at both keys, with the item names those
        // values would be, to enum-scan.txt. Finds the table behind a per-item index (e.g. a stored special attack).
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--enum-scan") {
            const int ka = _wtoi(argv[2]), kb = _wtoi(argv[3]);
            std::ofstream f("enum-scan.txt", std::ios::binary | std::ios::trunc);
            std::string strA, strB;   // string-valued enums report the text instead of an item
            auto valueAt = [&](const std::string& j, int k, long long& out, std::string& str) {
                std::string key = "\"" + std::to_string(k) + "\":";
                std::size_t at = j.find(key); if (at == std::string::npos) return false;
                const char* p = j.c_str() + at + key.size();
                if (*p == '"') { std::size_t e = j.find('"', at + key.size() + 1); str = j.substr(at + key.size() + 1, e == std::string::npos ? 0 : e - (at + key.size() + 1)); out = -1; return true; }
                out = std::atoll(p); return true;
            };
            for (int id = 0; id < 40000; ++id) {
                std::string j = rtx::cache::EnumJson(id);
                if (j.size() < 8) continue;
                long long va = 0, vb = 0; strA.clear(); strB.clear();
                if (!valueAt(j, ka, va, strA) || !valueAt(j, kb, vb, strB)) continue;
                int n = 0; for (char c : j) if (c == ':') ++n;
                if (n < 8 || n > 400) continue;
                std::string na = !strA.empty() ? ("\"" + strA + "\"") : (va > 0 && va < 100000 ? rtx::cache::ItemInfoJson((int)va) : "");
                std::string nb = !strB.empty() ? ("\"" + strB + "\"") : (vb > 0 && vb < 100000 ? rtx::cache::ItemInfoJson((int)vb) : "");
                f << id << "\tentries=" << n << "\t" << ka << "=" << va << " " << na << "\t" << kb << "=" << vb << " " << nb << "\n";
            }
            LocalFree(argv);
            return 0;
        }
        // --bank-dump <pid>: the bank rows as the Bank tab sees them (live or from the cache, with per-item vars)
        // to bank-dump.txt.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--bank-dump") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            rtx::reader::SampleAll();
            std::ofstream f("bank-dump.txt", std::ios::binary | std::ios::trunc);
            f << rtx::reader::BankJson(pid);
            LocalFree(argv);
            return 0;
        }
        // --var-scan <pid> <lo> <hi>: every varp and varbit whose live value lies in [lo, hi], to var-scan.txt.
        // Finds the vars behind a number the game shows (rune counts in a nexus, charges, ...).
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--var-scan") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            const long long lo = _wtoi64(argv[3]), hi = _wtoi64(argv[4]);
            rtx::reader::SampleAll();
            std::ofstream f("var-scan.txt", std::ios::binary | std::ios::trunc);
            auto scan = [&](const char* kind, int maxId, auto&& reader) {
                for (int base = 0; base < maxId; base += 400) {
                    std::string csv;
                    for (int id = base; id < base + 400 && id < maxId; ++id) { if (!csv.empty()) csv += ","; csv += std::to_string(id); }
                    std::string j = reader(csv);
                    std::size_t pos = 1;
                    while (pos < j.size()) {
                        if (j[pos] != '"') { ++pos; continue; }
                        std::size_t e = j.find('"', pos + 1); if (e == std::string::npos) break;
                        int id = std::atoi(j.c_str() + pos + 1);
                        if (e + 1 >= j.size() || j[e + 1] != ':') break;
                        long long v = std::atoll(j.c_str() + e + 2);
                        if (v >= lo && v <= hi) f << kind << "\t" << id << "\t" << v << "\n";
                        pos = j.find_first_of(",}", e + 2); if (pos == std::string::npos) break; ++pos;
                    }
                }
            };
            scan("varp",   20000, [&](const std::string& csv) { return rtx::reader::VarpsJson(pid, csv); });
            scan("varbit", 70000, [&](const std::string& csv) { return rtx::reader::VarbitsJson(pid, csv); });
            LocalFree(argv);
            return 0;
        }
        // --var-block <pid> <lo> <hi>: player varps lo..hi with their cache type code, live int value and live long
        // value (long-typed varps keep a full i64), to var-block.txt. For mapping a family of related vars.
        if (argv && argc >= 5 && std::wstring(argv[1]) == L"--var-block") {
            std::uint32_t pid = (std::uint32_t)_wtoi(argv[2]);
            const int lo = _wtoi(argv[3]), hi = _wtoi(argv[4]);
            rtx::reader::SampleAll();
            std::string csv;
            for (int id = lo; id <= hi; ++id) { if (!csv.empty()) csv += ","; csv += std::to_string(id); }
            std::string defs = rtx::cache::VarDefsJson(60);
            std::string ints = rtx::reader::VarpsJson(pid, csv), longs = rtx::reader::VarpsLongJson(pid, csv);
            auto field = [](const std::string& j, int id) -> std::string {
                std::string key = "\"" + std::to_string(id) + "\":";
                std::size_t at = j.find(key); if (at == std::string::npos) return "";
                std::size_t e = j.find_first_of(",}", at + key.size()); return j.substr(at + key.size(), e == std::string::npos ? std::string::npos : e - at - key.size());
            };
            std::size_t tpos = defs.find("\"types\":{");
            std::string types = tpos == std::string::npos ? "" : defs.substr(tpos + 8);
            std::ofstream f("var-block.txt", std::ios::binary | std::ios::trunc);
            f << "id\ttype\tint\tlong\n";
            for (int id = lo; id <= hi; ++id)
                f << id << "\t" << field(types, id) << "\t" << field(ints, id) << "\t" << field(longs, id) << "\n";
            LocalFree(argv);
            return 0;
        }
        // --enum-dump <id>: one cache enum as JSON to enum-<id>.txt.
        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--enum-dump") {
            const int id = _wtoi(argv[2]);
            std::ofstream f("enum-" + std::to_string(id) + ".txt", std::ios::binary | std::ios::trunc);
            f << rtx::cache::EnumJson(id);
            LocalFree(argv);
            return 0;
        }
        // --enum-rscan <valA> <valB>: enums that map some key to valA and another key to valB (reverse lookup, e.g.
        // weapon item id -> special attack index), with the item names of those keys, to enum-rscan.txt.
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--enum-rscan") {
            const long long va = _wtoi(argv[2]), vb = _wtoi(argv[3]);
            std::ofstream f("enum-rscan.txt", std::ios::binary | std::ios::trunc);
            for (int id = 0; id < 40000; ++id) {
                std::string j = rtx::cache::EnumJson(id);
                if (j.size() < 8) continue;
                long long ka = -1, kb = -1; int n = 0;
                std::size_t pos = 1;
                while (pos < j.size()) {
                    if (j[pos] != '"') { ++pos; continue; }
                    std::size_t e = j.find('"', pos + 1); if (e == std::string::npos) break;
                    long long key = std::atoll(j.c_str() + pos + 1);
                    std::size_t colon = e + 1; if (colon >= j.size() || j[colon] != ':') break;
                    if (j[colon + 1] == '"') { std::size_t e2 = j.find('"', colon + 2); pos = e2 == std::string::npos ? j.size() : e2 + 1; ++n; continue; }
                    long long val = std::atoll(j.c_str() + colon + 1); ++n;
                    if (val == va) ka = key; if (val == vb) kb = key;
                    pos = j.find_first_of(",}", colon + 1); if (pos == std::string::npos) break; ++pos;
                }
                if (ka < 0 || kb < 0) continue;
                std::string na = ka > 0 && ka < 100000 ? rtx::cache::ItemName((int)ka) : "", nb = kb > 0 && kb < 100000 ? rtx::cache::ItemName((int)kb) : "";
                f << id << "\tentries=" << n << "\tkey(" << va << ")=" << ka << " " << na << "\tkey(" << vb << ")=" << kb << " " << nb << "\n";
            }
            LocalFree(argv);
            return 0;
        }
        // --model-icons <ids.txt> <outdir>: write modelicons.pack entries as <outdir>/<modelId>.png (one id per line).
        if (argv && argc >= 4 && std::wstring(argv[1]) == L"--model-icons") {
            std::wstring wi = argv[2], wo = argv[3];
            std::ifstream in{ std::filesystem::path(wi) };
            std::filesystem::create_directories(std::filesystem::path(wo));
            auto b64val = [](char c) -> int {
                if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+') return 62; if (c == '/') return 63; return -1; };
            int written = 0, missing = 0; std::string line;
            while (std::getline(in, line)) {
                int id = std::atoi(line.c_str()); if (id <= 0) continue;
                std::string url = rtx::launcher::icons::ModelIconDataUrl(id);
                std::size_t comma = url.find(',');
                if (url.empty() || comma == std::string::npos) { ++missing; continue; }
                std::string ext = url.rfind("data:image/gif", 0) == 0 ? ".gif" : ".png";
                std::vector<unsigned char> bytes; int acc = 0, bits = 0;
                for (std::size_t i = comma + 1; i < url.size(); ++i) {
                    int v = b64val(url[i]); if (v < 0) continue;
                    acc = (acc << 6) | v; bits += 6;
                    if (bits >= 8) { bits -= 8; bytes.push_back((unsigned char)((acc >> bits) & 0xFF)); }
                }
                std::ofstream o(std::filesystem::path(wo) / (std::to_wstring(id) + std::wstring(ext.begin(), ext.end())), std::ios::binary | std::ios::trunc);
                o.write((const char*)bytes.data(), (std::streamsize)bytes.size());
                ++written;
            }
            { std::ofstream f("model-icons.txt", std::ios::binary | std::ios::trunc); f << written << " written, " << missing << " missing"; }
            LocalFree(argv);
            return 0;
        }
        if (argv) LocalFree(argv);
    }
    rtx::log::Init();
    {   // --lua-selftest [plugin dir]: exercise the Lua plugin host against a stub page, write
        // lua-selftest.txt next to the working directory and exit with 0 on pass. No window, no game.
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2 && std::wstring(argv[1]) == L"--lua-selftest") {
            std::filesystem::path dir = (argc >= 3) ? std::filesystem::path(argv[2]) : std::filesystem::path(L"SampleLuaPluginX");
            std::string report;
            int rc = rtx::launcher::lua::SelfTest(dir, report);
            { std::ofstream f("lua-selftest.txt", std::ios::binary | std::ios::trunc); f << report; }
            boot_log("lua selftest rc=" + std::to_string(rc) + "\n" + report);
            LocalFree(argv);
            return rc;
        }
        if (argv) LocalFree(argv);
    }
    boot_log("=== RuneToolsX starting ===");
    DeclareDpiAwareness();

    // Named mutex matching the installer's AppMutex (RuneToolsX.iss); held for the process lifetime.
    CreateMutexW(nullptr, FALSE, L"RuneToolsXLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        boot_log("another RuneToolsX launcher is already running; asking it to show itself and exiting");
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
        fatal("RuneTools cannot start: some of its files are missing from the Ultralight folder next to "
              "RuneToolsXLauncher.exe.\n\nAntivirus software sometimes removes them. Add the RuneTools "
              "folder to your antivirus exclusions, then install RuneTools again to put the files back.");
        return 1;
    }

    boot_log(rtx::launcher::InstallMonitorInfoFix()
                 ? "GetMonitorInfoW fix installed in AppCore.dll"
                 : "GetMonitorInfoW fix NOT installed (import not found)");

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
            // A process snapshot per pass: 2 Hz while looking for a client, 1 Hz once one is attached.
            Sleep(any_loaded ? 1000 : 500);
        }
    }).detach();

    try {
        LauncherApp app;
        app.Run();
        rtx::launcher::loot::Shutdown();
        rtx::log::BeginShutdown();
        rtx::overlay::Stop();
        rtx::launcher::dock::Shutdown();
        rtx::winnotify::Shutdown();
        g_scan_stop.store(true);
        rtx::launcher::http::Shutdown();
    } catch (const std::exception& e) {
        fatal(std::string("Unhandled exception: ") + e.what());
        return 1;
    } catch (...) {
        fatal("Unhandled non-std exception during startup");
        return 1;
    }
    boot_log("=== Clean exit ===");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;   // unreachable
}
