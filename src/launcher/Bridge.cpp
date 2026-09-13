#include "Bridge.h"
#include "BridgeUtil.h"
#include "Update.h"
#include "News.h"
#include "Accounts.h"
#include "Companion.h"
#include "Cs2Browser.h"
#include "Dock.h"
#include "GameUi.h"
#include "WikiBrowser.h"
#include "IconCache.h"
#include "Loader.h"
#include "Overlay.h"
#include "Markers.h"
#include "WinNotify.h"
#include "Process.h"
#include "../../companion/CaptureShare.h"
#include "../cache/CacheReader.h"
#include "../cache/Constants.h"
#include "Audio.h"
#include "SoundFilter.h"
#include "../../companion/FrameShare.h"
#include "../../companion/NetProbeShare.h"
#include "MenuSwap.h"
#include "../cache/Achievements.h"
#include "../../companion/HudShare.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"
#include "../shared/MachineFingerprint.h"
#include "Http.h"
#include "Crypto.h"
#include <shobjidl.h>
#include "Zip.h"
#include "LuaHost.h"

#include <Ultralight/Ultralight.h>
#include <JavaScriptCore/JavaScript.h>
#include <Windows.h>
// gdiplus.h references unqualified min/max.
#ifdef NOMINMAX
#include <algorithm>
using std::max;
using std::min;
#endif
#include <gdiplus.h>
#include <ShellAPI.h>
#include <TlHelp32.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <bcrypt.h>
#include <cctype>
#include <climits>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "gdiplus.lib")

namespace rtx::launcher {

void* g_launcherHwnd = nullptr;   // set once via SetLauncherWindow; owner for modal dialogs

namespace {

static int js_int(JSContextRef ctx, JSValueRef v, int def = 0, int lo = INT_MIN, int hi = INT_MAX) {
    double d = JSValueToNumber(ctx, v, nullptr);
    if (!std::isfinite(d)) return def;
    if (d < (double)lo) return lo;
    if (d > (double)hi) return hi;
    return (int)d;
}


std::filesystem::path runetools_dir() {
    return std::filesystem::path(rtx::log::LogDir()).parent_path();
}
std::filesystem::path screenshots_dir() { return runetools_dir() / L"screenshots"; }
std::filesystem::path screenshot_cfg()  { return runetools_dir() / L"screenshot.txt"; }
std::filesystem::path hidepanels_cfg()  { return runetools_dir() / L"hidepanels.txt"; }

std::once_flag       g_gdip_once;
ULONG_PTR            g_gdip_token = 0;
void ensure_gdiplus() {
    std::call_once(g_gdip_once, [] {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&g_gdip_token, &in, nullptr);
    });
}

bool png_encoder_clsid(CLSID& out) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
    std::vector<std::uint8_t> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { out = codecs[i].Clsid; return true; }
    return false;
}

bool bitmap_mostly_black(const void* bits, int w, int h, int stride) {
    if (!bits || w <= 0 || h <= 0) return true;
    const auto* p = static_cast<const std::uint8_t*>(bits);
    int nonblack = 0, samples = 0;
    for (int y = 0; y < h; y += (h > 40 ? h / 40 : 1)) {
        const auto* row = p + (std::size_t)y * stride;
        for (int x = 0; x < w; x += (w > 40 ? w / 40 : 1)) {
            ++samples;
            const std::uint8_t* px = row + (std::size_t)x * 4;
            if (px[0] > 8 || px[1] > 8 || px[2] > 8) ++nonblack;
        }
    }
    return samples > 0 && nonblack * 100 / samples < 2;   // <2% non-black -> treat as blank
}

HBITMAP capture_window_dib(HWND hwnd, int& outW, int& outH) {
    if (!hwnd || !IsWindow(hwnd)) return nullptr;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return nullptr;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return nullptr;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                 // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib) {
        HGDIOBJ prev = SelectObject(mem, dib);
        // PW_RENDERFULLCONTENT = 0x2
        if (PrintWindow(hwnd, mem, 0x00000002)) ok = true;
        GdiFlush();
        if (!ok || bitmap_mostly_black(bits, w, h, w * 4)) {
            POINT tl{ 0, 0 };
            if (ClientToScreen(hwnd, &tl) &&
                BitBlt(mem, 0, 0, w, h, screen, tl.x, tl.y, SRCCOPY | CAPTUREBLT)) {
                GdiFlush();
                ok = true;
            }
        }
        SelectObject(mem, prev);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!ok) { if (dib) DeleteObject(dib); return nullptr; }
    outW = w; outH = h;
    return dib;
}

std::filesystem::path next_screenshot_path() {
    SYSTEMTIME st{}; GetLocalTime(&st);
    auto pad = [](unsigned v, int w) {
        std::wstring s = std::to_wstring(v);
        while ((int)s.size() < w) s.insert(s.begin(), L'0');
        return s;
    };
    std::wstring base = pad(st.wYear, 4) + L"-" + pad(st.wMonth, 2) + L"-" + pad(st.wDay, 2) +
                        L"_" + pad(st.wHour, 2) + L"-" + pad(st.wMinute, 2) + L"-" + pad(st.wSecond, 2);
    std::filesystem::path dir = screenshots_dir();
    std::filesystem::path p = dir / (base + L".png");
    for (int i = 2; std::filesystem::exists(p) && i < 1000; ++i)
        p = dir / (base + L"_" + std::to_wstring(i) + L".png");
    return p;
}

// Presented-frame capture through the companion (Vulkan clients): exact swapchain pixels, overlay
// included, independent of window occlusion. The section stays mapped so the module can find it.
std::mutex g_capmu;
std::unordered_map<std::uint32_t, std::pair<HANDLE, rtx::capture::Share*>> g_capshares;
HBITMAP capture_companion_dib(std::uint32_t pid, int& outW, int& outH) {
    if (!pid) return nullptr;
    rtx::capture::Share* sh = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_capmu);
        auto it = g_capshares.find(pid);
        if (it == g_capshares.end()) {
            wchar_t name[64];
            rtx::capture::MakeSectionName(pid, name);
            HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(rtx::capture::Share), name);
            if (!map) return nullptr;
            auto* p = reinterpret_cast<rtx::capture::Share*>(MapViewOfFile(map, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::capture::Share)));
            if (!p) { CloseHandle(map); return nullptr; }
            if (p->magic != rtx::capture::kMagic) {
                p->version = rtx::capture::kVersion; p->pid = pid; p->request = 0; p->done = 0;
                p->magic = rtx::capture::kMagic;
            }
            it = g_capshares.emplace(pid, std::make_pair(map, p)).first;
        }
        sh = it->second.second;
    }
    const std::uint32_t serial = sh->request + 1;
    sh->request = serial;
    for (int i = 0; i < 60 && sh->done != serial; ++i) Sleep(10);
    if (sh->done != serial || sh->width == 0 || sh->height == 0) return nullptr;
    const int w = (int)sh->width, h = (int)sh->height;
    if ((std::size_t)sh->stride * h > rtx::capture::kMaxBytes) return nullptr;
    HDC screen = GetDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dib || !bits) { if (dib) DeleteObject(dib); return nullptr; }
    const bool rgba = sh->format == 37 || sh->format == 43;   // R8G8B8A8 swapchains need a swap
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* src = sh->pixels + (std::size_t)y * sh->stride;
        std::uint8_t* dst = static_cast<std::uint8_t*>(bits) + (std::size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            dst[x * 4 + 0] = rgba ? src[x * 4 + 2] : src[x * 4 + 0];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = rgba ? src[x * 4 + 0] : src[x * 4 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
    outW = w; outH = h;
    return dib;
}

std::wstring capture_to_screenshots(HWND hwnd, std::uint32_t pid = 0) {
    int w = 0, h = 0;
    HBITMAP dib = capture_companion_dib(pid, w, h);
    if (!dib) dib = capture_window_dib(hwnd, w, h);
    if (!dib) return {};
    std::wstring result;
    std::error_code ec;
    std::filesystem::create_directories(screenshots_dir(), ec);
    ensure_gdiplus();
    CLSID clsid;
    if (png_encoder_clsid(clsid)) {
        Gdiplus::Bitmap bmp(dib, nullptr);
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            std::filesystem::path out = next_screenshot_path();
            if (bmp.Save(out.c_str(), &clsid, nullptr) == Gdiplus::Ok)
                result = out.wstring();
        }
    }
    DeleteObject(dib);
    return result;
}

std::wstring capture_for_pid(std::uint32_t pid) {
    HWND hwnd = pid ? (HWND)rtx::launcher::dock::GameWindowHandle(pid) : nullptr;
    if (!hwnd) return {};
    std::wstring path = capture_to_screenshots(hwnd, pid);
    if (path.empty()) {
        if (!rtx::launcher::gameui::Notify(pid, "Screenshot failed", 5000))
            rtx::overlay::Toast(pid, "Screenshot failed");
        return {};
    }
    std::wstring name = std::filesystem::path(path).filename().wstring();
    int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u8(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, u8.data(), n, nullptr, nullptr);
    if (!rtx::launcher::gameui::Notify(pid, "Screenshot saved: " + u8, 5000))
        rtx::overlay::Toast(pid, "Screenshot saved: " + u8);
    return path;
}

std::mutex        g_ss_mu;
int               g_ss_vk = 0;
bool              g_ss_loaded = false;
void ss_load_locked() {
    if (g_ss_loaded) return;
    g_ss_loaded = true;
    std::ifstream f(screenshot_cfg());
    int vk = 0;
    if (f && (f >> vk) && vk > 0 && vk < 256) g_ss_vk = vk;
}
void ss_save_locked() {
    std::error_code ec; std::filesystem::create_directories(runetools_dir(), ec);
    std::ofstream f(screenshot_cfg(), std::ios::trunc);
    if (f) f << g_ss_vk;
}
std::mutex        g_hp_mu;
int               g_hp_vk = 0;
bool              g_hp_loaded = false;
void hp_load_locked() {
    if (g_hp_loaded) return;
    g_hp_loaded = true;
    std::ifstream f(hidepanels_cfg());
    int vk = 0;
    if (f && (f >> vk) && vk > 0 && vk < 256) g_hp_vk = vk;
}
void hp_save_locked() {
    std::error_code ec; std::filesystem::create_directories(runetools_dir(), ec);
    std::ofstream f(hidepanels_cfg(), std::ios::trunc);
    if (f) f << g_hp_vk;
}


std::filesystem::path vault_key_path() { return runetools_dir() / L"vault.key"; }

std::string vault_secret() {
    const auto path = vault_key_path();
    {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::stringstream ss; ss << f.rdbuf();
            const auto blob = ss.str();
            if (!blob.empty()) {
                std::vector<std::uint8_t> bytes(blob.begin(), blob.end());
                std::string s = crypto::UnprotectForCurrentUser(bytes);
                if (!s.empty()) return s;
                rtx::log::Launcher("vault key present but not unprotectable by this user");
            }
        }
    }
    std::uint8_t raw[32];
    if (!crypto::RandomBytes(raw, sizeof(raw))) return {};
    static const char* kHex = "0123456789abcdef";
    std::string secret;
    secret.reserve(sizeof(raw) * 2);
    for (std::uint8_t b : raw) { secret.push_back(kHex[b >> 4]); secret.push_back(kHex[b & 0xf]); }
    SecureZeroMemory(raw, sizeof(raw));
    auto sealed = crypto::ProtectForCurrentUser(secret);
    if (sealed.empty()) return {};
    std::error_code ec; std::filesystem::create_directories(runetools_dir(), ec);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return {};
    o.write(reinterpret_cast<const char*>(sealed.data()), (std::streamsize)sealed.size());
    o.flush();
    if (!o) return {};
    return secret;
}

void unlock_or_create_vault(const std::string& secret) {
    if (accounts::HasVault()) {
        if (accounts::Unlock(secret)) return;
        const std::string& legacy = shared::GetMachineFingerprint();
        if (!legacy.empty() && legacy != secret && accounts::Unlock(legacy)) {
            if (accounts::ChangePassphrase(secret)) {
                rtx::log::Launcher("accounts vault migrated to the per-user (DPAPI) key");
            } else {
                rtx::log::Launcher("accounts vault re-seal failed; staying on the legacy key");
            }
            return;
        }
        rtx::log::Launcher("accounts vault not unlockable; starting a fresh vault");
        accounts::DiscardVault();
    }
    accounts::Create(secret);
}

void ensure_vault_unlocked() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::string secret = vault_secret();
        if (secret.empty()) {
            rtx::log::Launcher("DPAPI vault key unavailable; falling back to the machine key");
            secret = shared::GetMachineFingerprint();
        }
        unlock_or_create_vault(secret);
    });
}

// Cross-process reads never run on the UI thread; `build` runs on the reader thread, capture by value only.
JSValueRef served(JSContextRef ctx, const std::string& key, const char* empty,
                  std::function<std::string()> build) {
    std::string r = rtx::reader::ReadAsync(key, std::move(build));
    return utf8_to_js(ctx, r.empty() ? std::string(empty) : r);
}

static std::string fail_json(const char* why) {
    return std::string("{\"ok\":false,\"why\":\"") + why + "\"}";
}

static bool pid_known(std::uint32_t pid) {
    if (!pid) return false;
    const std::string j = rtx::reader::SamplesJson();
    return j.find("\"pid\":" + std::to_string(pid) + ",") != std::string::npos;
}

static JSValueRef served_obj(JSContextRef ctx, std::uint32_t pid, const std::string& key,
                             std::function<std::string()> build) {
    if (!pid_known(pid)) return utf8_to_js(ctx, fail_json("noclient"));
    std::string r = rtx::reader::ReadAsync(key, std::move(build));
    return utf8_to_js(ctx, r.empty() ? fail_json("error") : r);
}

std::string sanitize_account(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    return out;
}

// Reader::AccountKey first (Steam clients set no JX_ vars), env read as the pre-attach fallback.
std::string account_key_for(std::uint32_t pid) {
    if (!pid) return {};
    std::string acct = sanitize_account(rtx::reader::AccountKey(pid));
    if (!acct.empty()) return acct;
    auto env = process::ReadJxEnv(pid);
    auto it = env.find("JX_DISPLAY_NAME");
    return (it != env.end()) ? sanitize_account(it->second) : std::string();
}


JSValueRef OpenExternal(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto url = get_string_arg(ctx, argc, argv, 0);
    static const char* kAllowed[] = {
        "https://runetools.io/", "https://secure.runescape.com/", "https://www.runescape.com/", "https://runescape.com/",
    };
    bool allowed = false;
    for (const char* a : kAllowed) if (url.rfind(a, 0) == 0) { allowed = true; break; }
    if (!allowed) return JSValueMakeUndefined(ctx);
    int n = MultiByteToWideChar(CP_UTF8, 0, url.data(), (int)url.size(),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.data(), (int)url.size(),
                        w.data(), n);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return JSValueMakeUndefined(ctx);
}

bool account_capture_get();

JSValueRef ScanProcesses(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    auto procs = process::ScanRsClients();
    const bool unlocked = accounts::IsUnlocked();
    const bool capture  = account_capture_get();

    std::ostringstream os;
    os << "[";
    std::vector<std::string> live_ids;
    for (size_t i = 0; i < procs.size(); ++i) {
        const auto& p = procs[i];
        if (i) os << ",";

        auto env = process::ReadJxEnv(p.pid);
        std::string display_name, character_id;
        auto it = env.find("JX_DISPLAY_NAME");
        if (it != env.end()) display_name = it->second;
        it = env.find("JX_CHARACTER_ID");
        if (it != env.end()) character_id = it->second;
        const bool has_env = !env.empty();

        if (!character_id.empty()) live_ids.push_back(character_id);

        if (capture && unlocked && has_env &&
            (!display_name.empty() || !character_id.empty()) &&
            !accounts::IsCaptureSuppressed(character_id)) {
            accounts::Upsert(accounts::FromEnv(env));
        }

        os << "{"
           << "\"pid\":"          << p.pid << ","
           << "\"name\":\""       << json_escape(wide_to_utf8(p.name)) << "\","
           << "\"x64\":"          << (p.x64 ? "true" : "false") << ","
           << "\"accessible\":"   << (p.accessible ? "true" : "false") << ","
           << "\"display_name\":\"" << json_escape(display_name) << "\","
           << "\"character_id\":\"" << json_escape(character_id) << "\","
           << "\"has_env\":"      << (has_env ? "true" : "false")
           << "}";
    }
    accounts::PruneCaptureSuppressions(live_ids);
    os << "]";
    return utf8_to_js(ctx, os.str());
}

JSValueRef GameSnapshots(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::reader::SamplesJson());
}

JSValueRef ReaderHealth(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string j = rtx::reader::ReaderHealthJson(pid);
    auto tail = j.rfind("]}");
    if (pid && tail != std::string::npos) {
        std::string extra;
        auto add = [&](const char* k, int ok, const std::string& d) {
            extra += ",{\"k\":\""; extra += k; extra += "\",\"ok\":" + std::to_string(ok) +
                     ",\"d\":\"" + d + "\"}";
        };
        {
            wchar_t name[64]; rtx::frame::MakeSectionName(pid, name);
            HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
            int ok = 2; std::string d = "inactive (loads with the game client)";
            if (m) {
                auto* sh = reinterpret_cast<const rtx::frame::Share*>(
                    MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(rtx::frame::Share)));
                if (sh) {
                    if (sh->magic == rtx::frame::kMagic && sh->module_seq > 0 &&
                        sh->client_w > 0 && sh->client_h > 0) {
                        ok = 1;
                        d = "compositing at " + std::to_string(sh->client_w) + "x" +
                            std::to_string(sh->client_h);
                    } else if (sh->magic == rtx::frame::kMagic) {
                        ok = 0; d = "layer mapped but the game never presented through it";
                    }
                    UnmapViewOfFile((void*)sh);
                }
                CloseHandle(m);
            }
            add("Companion: in-game UI frame", ok, d);
        }
        {
            std::string st = rtx::launcher::soundfilter::StatusJson(pid);
            int ok; std::string d;
            if (st.find("\"hooked\":true") != std::string::npos) { ok = 1; d = "observing playback"; }
            else if (st.find("\"ok\":true") != std::string::npos) {
                ok = 0; d = "play function not found in this game build";
            } else { ok = 2; d = "inactive (loads with the game client)"; }
            add("Companion: sound observation", ok, d);
        }
        {
            // Chat capture: framer hook + op-0x15 ring.
            wchar_t name[64]; rtx::netprobe::MakeSectionName(pid, name);
            HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
            int ok = 2; std::string d = "inactive (loads with the game client)";
            if (m) {
                auto* sh = reinterpret_cast<const rtx::netprobe::Share*>(
                    MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
                MEMORY_BASIC_INFORMATION mbi{};
                if (sh && (VirtualQuery(sh, &mbi, sizeof(mbi)) == 0 || mbi.RegionSize < sizeof(rtx::netprobe::Share))) {
                    UnmapViewOfFile((void*)sh); sh = nullptr;
                }
                if (sh) {
                    if (sh->magic == rtx::netprobe::kMagic && sh->version >= 3) {
                        if (!(sh->flags & 1)) {
                            ok = 0; d = "framer hook not attached (signature may have moved)";
                        } else if (sh->chatSeen == 0) {
                            ok = 2; d = "hooked; no chat messages observed yet";
                        } else {
                            ok = 1;
                            d = std::to_string((unsigned long long)sh->chatSeen) +
                                " messages captured";
                        }
                    } else if (sh->magic == rtx::netprobe::kMagic) {
                        ok = 0; d = "companion predates chat capture (restart the game client)";
                    }
                    UnmapViewOfFile((void*)sh);
                }
                CloseHandle(m);
            }
            add("Companion: chat packet capture", ok, d);
        }
        j.insert(tail, extra);
    }
    return utf8_to_js(ctx, j);
}

JSValueRef UiAsset(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    return utf8_to_js(ctx, rtx::launcher::dock::ReadUiAsset(js_to_utf8(ctx, argv[0])));
}

// ---- Cache audio (js5-14 effects / js5-40 music) ----
std::filesystem::path alerts_user_dir();

JSValueRef SoundList(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "[]");
    int idx   = js_int(ctx, argv[0]);
    int start = (argc >= 2) ? js_int(ctx, argv[1]) : 0;
    int lim   = (argc >= 3) ? js_int(ctx, argv[2]) : 200;
    if (idx != rtx::cache::kIndexSoundEffects && idx != rtx::cache::kIndexMusic)
        return utf8_to_js(ctx, "[]");
    return utf8_to_js(ctx, rtx::cache::SoundListJson(idx, start, lim));
}

JSValueRef SoundExport(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "");
    int idx = js_int(ctx, argv[0]);
    int id  = js_int(ctx, argv[1]);
    if (idx != rtx::cache::kIndexSoundEffects && idx != rtx::cache::kIndexMusic)
        return utf8_to_js(ctx, "");
    auto ogg = rtx::cache::SoundOgg(idx, id);
    if (ogg.empty()) return utf8_to_js(ctx, "");
    auto dir = alerts_user_dir();
    if (dir.empty()) return utf8_to_js(ctx, "");
    dir /= L"sounds-cache";
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    auto path = dir / ((idx == rtx::cache::kIndexMusic ? L"music_" : L"sfx_") +
                       std::to_wstring(id) + L".ogg");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.wstring().c_str(), L"wb") != 0 || !f) return utf8_to_js(ctx, "");
    std::fwrite(ogg.data(), 1, ogg.size(), f);
    std::fclose(f);
    return utf8_to_js(ctx, path.string());
}

JSValueRef SoundPlay(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    int idx = js_int(ctx, argv[0]);
    int id  = js_int(ctx, argv[1]);
    int vol = (argc >= 3) ? js_int(ctx, argv[2]) : 100;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    if (idx != rtx::cache::kIndexSoundEffects && idx != rtx::cache::kIndexMusic)
        return JSValueMakeBoolean(ctx, false);
    auto chunks = rtx::cache::SoundOggChunks(idx, id);
    if (chunks.empty()) return JSValueMakeBoolean(ctx, false);
    return JSValueMakeBoolean(ctx, rtx::audio::Play(chunks, vol));
}

JSValueRef SoundStop(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    rtx::audio::Stop();
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef SoundPause(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t, const JSValueRef[], JSValueRef*) {
    rtx::audio::Pause();
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef SoundResume(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    rtx::audio::Resume();
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef SoundSeek(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc >= 1) rtx::audio::Seek(js_int(ctx, argv[0]));
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef SoundVolume(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc >= 1) rtx::audio::SetVolume(js_int(ctx, argv[0]));
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef SoundStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::audio::StatusJson());
}


JSValueRef SoundFilterStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::launcher::soundfilter::StatusJson(pid));
}

JSValueRef SoundFilterEnable(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool on = JSValueToBoolean(ctx, argv[1]);
    return JSValueMakeBoolean(ctx, rtx::launcher::soundfilter::SetEnabled(pid, on));
}

JSValueRef SoundMute(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string csv = js_to_utf8(ctx, argv[1]);
    std::vector<int> ids;
    for (std::size_t i = 0; i < csv.size();) {
        while (i < csv.size() && (csv[i] < '0' || csv[i] > '9')) ++i;
        int v = 0; bool got = false;
        while (i < csv.size() && csv[i] >= '0' && csv[i] <= '9') { v = v * 10 + (csv[i++] - '0'); got = true; }
        if (got) ids.push_back(v);
    }
    return JSValueMakeBoolean(ctx, rtx::launcher::soundfilter::SetMuted(pid, std::move(ids)));
}

JSValueRef ClueSearchTarget(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    int x = js_int(ctx, argv[0]);
    int y = js_int(ctx, argv[1]);
    int p = (argc >= 3) ? js_int(ctx, argv[2]) : 0;
    return utf8_to_js(ctx, rtx::cache::ClueSearchTargetJson(x, y, p));
}

JSValueRef MenuStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::launcher::menuswap::StatusJson(pid));
}

JSValueRef MenuEnable(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    const double raw = JSValueToNumber(ctx, argv[1], nullptr);
    const auto mode = (raw >= 1.0 && raw <= 2.0) ? static_cast<std::uint32_t>(raw) : 0u;
    return JSValueMakeBoolean(ctx, rtx::launcher::menuswap::SetEnabled(pid, mode));
}

JSValueRef MenuSwapFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return JSValueMakeBoolean(ctx, rtx::launcher::menuswap::SetPins(pid, js_to_utf8(ctx, argv[1])));
}

JSValueRef HostInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::reader::HostJson());
}

JSValueRef IconSource(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "none");
    const int src = icons::IconSource(js_int(ctx, argv[0]));
    return utf8_to_js(ctx, src == 2 ? "rendered" : src == 1 ? "pack" : "none");
}

JSValueRef ItemIcon(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, icons::ItemIconDataUrl(id));
}

JSValueRef IconMisses(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, icons::IconMissesJson());
}

namespace {
std::mutex  g_iconCovMu;
std::string g_iconCovJson;      // "" = not built yet
bool        g_iconCovBusy = false;
}
JSValueRef IconCoverage(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    {
        std::lock_guard<std::mutex> lk(g_iconCovMu);
        if (!g_iconCovJson.empty()) return utf8_to_js(ctx, g_iconCovJson);
        if (g_iconCovBusy) return utf8_to_js(ctx, "{\"pending\":1}");
        g_iconCovBusy = true;
    }
    std::thread([] {
        std::string r;
        try { r = rtx::cache::ItemIconCoverageJson(&icons::IconPackHas); } catch (...) { r = "{}"; }
        std::lock_guard<std::mutex> lk(g_iconCovMu);
        g_iconCovJson = r.empty() ? "{}" : r;
        g_iconCovBusy = false;
    }).detach();
    return utf8_to_js(ctx, "{\"pending\":1}");
}

JSValueRef ModelIcon(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, icons::ModelIconDataUrl(id));
}

JSValueRef ItemInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::ItemInfoJson(id));
}

// The launcher owns the per-client HUD section (one mapping per pid kept alive); the module reads it.
namespace {
struct HudMap { HANDLE h = nullptr; rtx::hud::Share* s = nullptr; int lastSprite = -1; };
std::unordered_map<std::uint32_t, HudMap> g_hudMaps;
std::mutex g_hudMu;
HudMap* HudFor(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_hudMu);
    auto it = g_hudMaps.find(pid);
    if (it != g_hudMaps.end()) return &it->second;
    wchar_t name[64]; rtx::hud::MakeSectionName(pid, name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::hud::Share), name);
    if (!h) return nullptr;
    auto* s = reinterpret_cast<rtx::hud::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::hud::Share)));
    if (!s) { CloseHandle(h); return nullptr; }
    s->magic = rtx::hud::kMagic; s->version = rtx::hud::kVersion; s->pid = pid;
    s->seq = 0; s->enable = 0; s->imgSeq = 0; s->w = s->h = 0; s->caption[0] = 0;
    auto& m = g_hudMaps[pid]; m.h = h; m.s = s; m.lastSprite = -1;
    return &m;
}
}  // namespace

bool HudWrite(std::uint32_t pid, int spriteId, const std::string& caption, bool on) {
    HudMap* m = HudFor(pid);
    if (!m || !m->s) return false;
    rtx::hud::Share* s = m->s;
    s->seq++;                                  // odd = write in progress
    if (!on) { s->enable = 0; s->seq++; return true; }
    if (spriteId != m->lastSprite) {
        int w = 0, h = 0;
        auto rgba = rtx::cache::SpriteRgba(spriteId, w, h);
        if (!rgba.empty() && w > 0 && h > 0 && w <= rtx::hud::kMaxW && h <= rtx::hud::kMaxH) {
            s->w = w; s->h = h;
            std::memcpy(s->rgba, rgba.data(), (size_t)w * h * 4);
            s->imgSeq++;
        }
        m->lastSprite = spriteId;
    }
    size_t cn = caption.size(); if (cn > (size_t)rtx::hud::kCaptionMax) cn = rtx::hud::kCaptionMax;
    std::memcpy(s->caption, caption.data(), cn); s->caption[cn] = 0;
    s->enable = 1;
    s->seq++;
    return true;
}

JSValueRef HudSprite(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 4) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int sprite = js_int(ctx, argv[1]);
    std::string caption = js_to_utf8(ctx, argv[2]);
    bool on = JSValueToBoolean(ctx, argv[3]);
    return JSValueMakeBoolean(ctx, HudWrite(pid, sprite, caption, on));
}

JSValueRef Sprite(JSContextRef ctx, JSObjectRef, JSObjectRef,
                  size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    int id = js_int(ctx, argv[0]);
    int px = (argc >= 2) ? js_int(ctx, argv[1]) : 0;
    int frame = (argc >= 3) ? js_int(ctx, argv[2]) : 0;
    return utf8_to_js(ctx, (px > 0 || frame > 0) ? rtx::cache::SpriteDataUrlScaled(id, px, frame)
                                                 : rtx::cache::SpriteDataUrl(id));
}
JSValueRef SpriteByName(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeNumber(ctx, -1);
    return JSValueMakeNumber(ctx, rtx::cache::SpriteIdByName(js_to_utf8(ctx, argv[0])));
}

JSValueRef Achievements(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::AchievementsJson());
}

JSValueRef EnumInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::EnumJson(id));
}

JSValueRef ItemParams(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::ItemParamsJson(id));
}

JSValueRef DbRows(JSContextRef ctx, JSObjectRef, JSObjectRef,
                  size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "[]");
    int t = js_int(ctx, argv[0]);
    return served(ctx, "dbrows:" + std::to_string(t), "[]", [t] { return rtx::cache::DbRowsJson(t); });
}

JSValueRef StructParams(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::StructParamsJson(id));
}

JSValueRef AbilityConfigs(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "abilityconfigs", "{}", [] { return rtx::cache::AbilityConfigsJson(); });
}
JSValueRef BuffCatalog(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "buffcatalog", "{}", [] { return rtx::cache::BuffCatalogJson(); });
}

JSValueRef ArchResearch(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::ArchResearchJson());
}
JSValueRef MystPages(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::MystPagesJson());
}

JSValueRef NpcInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::NpcJson(id));
}

JSValueRef ParamDef(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::ParamDefJson(id));
}

JSValueRef CacheIfaceGroup(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int gid = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::IfaceGroupDefsJson(gid));
}

namespace {
std::mutex g_mapwinMu;
std::unordered_map<std::string, std::string> g_mapwinReady;
std::unordered_set<std::string> g_mapwinBusy;
}
JSValueRef MapWindow(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, "{}");
    int cx = (int)JSValueToNumber(ctx, argv[0], nullptr);
    int cy = (int)JSValueToNumber(ctx, argv[1], nullptr);
    int plane = (int)JSValueToNumber(ctx, argv[2], nullptr);
    int half = (argc > 3) ? (int)JSValueToNumber(ctx, argv[3], nullptr) : 0;
    int ts   = (argc > 4) ? (int)JSValueToNumber(ctx, argv[4], nullptr) : 0;
    int want = (argc > 5) ? (int)JSValueToNumber(ctx, argv[5], nullptr) : 15;
    bool sync = (argc > 6) && JSValueToBoolean(ctx, argv[6]);
    if (sync) {
        std::string r;
        try { r = rtx::cache::MapWindowJson(cx, cy, plane, half, ts, want); } catch (...) { r = "{}"; }
        return utf8_to_js(ctx, r.empty() ? "{}" : r);
    }
    const std::string key = std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(plane) + "," +
                            std::to_string(half) + "," + std::to_string(ts) + "," + std::to_string(want);
    {
        std::lock_guard<std::mutex> lk(g_mapwinMu);
        auto it = g_mapwinReady.find(key);
        if (it != g_mapwinReady.end()) { std::string r = std::move(it->second); g_mapwinReady.erase(it); return utf8_to_js(ctx, r); }
        if (g_mapwinBusy.count(key)) return utf8_to_js(ctx, "{\"pending\":1}");
        g_mapwinBusy.insert(key);
    }
    std::thread([key, cx, cy, plane, half, ts, want] {
        std::string r;
        try { r = rtx::cache::MapWindowJson(cx, cy, plane, half, ts, want); } catch (...) { r = "{}"; }
        std::lock_guard<std::mutex> lk(g_mapwinMu);
        g_mapwinReady[key] = r.empty() ? "{}" : r;
        g_mapwinBusy.erase(key);
        if (g_mapwinReady.size() > 96) g_mapwinReady.erase(g_mapwinReady.begin());
    }).detach();
    return utf8_to_js(ctx, "{\"pending\":1}");
}

// World-map areas (js5-23) and their composited images.
JSValueRef MapAreas(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "mapareas", "{}", [] { return rtx::cache::MapAreasJson(); });
}
JSValueRef MapAreaImage(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    int id = js_int(ctx, argv[0]);
    bool thumb = (argc > 1) && JSValueToBoolean(ctx, argv[1]);
    return served(ctx, "mapimg:" + std::to_string(id) + (thumb ? ":t" : ":f"), "", [id, thumb] { return rtx::cache::MapAreaImageDataUrl(id, thumb); });
}
JSValueRef MapLabels(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "maplabels", "{}", [] { return rtx::cache::MapLabelsJson(); });
}
JSValueRef MapCategories(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "mapcategories", "{}", [] { return rtx::cache::MapCategoriesJson(); });
}
JSValueRef MapSymbols(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "mapsymbols", "{\"n\":0,\"b\":\"\"}", [] { return rtx::cache::MapSymbolsJson(); });
}
JSValueRef MapLocNames(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "maplocnames", "{}", [] { return rtx::cache::MapLocNamesJson(); });
}

JSValueRef BankItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "bank:" + std::to_string(pid),
                      [pid]{ return rtx::reader::BankJson(pid); });
}

JSValueRef MetalBankItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "metalbank:" + std::to_string(pid),
                      [pid]{ return rtx::reader::MetalBankJson(pid); });
}

JSValueRef MaterialItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "materials:" + std::to_string(pid),
                      [pid]{ return rtx::reader::MaterialsJson(pid); });
}

JSValueRef GroupBankItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "groupbank:" + std::to_string(pid),
                      [pid]{ return rtx::reader::GroupBankJson(pid); });
}

JSValueRef BaitBoxItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "baitbox:" + std::to_string(pid),
                      [pid]{ return rtx::reader::BaitBoxJson(pid); });
}

JSValueRef NexusItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "nexus:" + std::to_string(pid),
                      [pid]{ return rtx::reader::NexusJson(pid); });
}

JSValueRef WorkbenchItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "workbench:" + std::to_string(pid),
                      [pid]{ return rtx::reader::WorkbenchJson(pid); });
}

JSValueRef SceneEntities(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"npcs\":[],\"count\":0}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int range = (argc >= 2) ? js_int(ctx, argv[1]) : 20;
    rtx::launcher::companion::EnsureLoaded(pid);
    return served(ctx, "scene:" + std::to_string(pid) + ":" + std::to_string(range),
                  "{\"npcs\":[],\"count\":0}",
                  [pid, range]{ return rtx::reader::SceneJson(pid, range); });
}

JSValueRef PlayerInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"in\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "player:" + std::to_string(pid), "{\"in\":false}",
                  [pid]{ return rtx::reader::PlayerInfoJson(pid); });
}

JSValueRef SocialFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"in\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "social:" + std::to_string(pid), "{\"in\":false}",
                  [pid]{ return rtx::reader::SocialJson(pid); });
}

// Combat log events after a sequence number. Served from the launcher's own ring (no client
// reads), so it is not cached; the poller behind it runs at 5 Hz for every logged-in client.
JSValueRef CombatLogFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"seq\":0,\"gap\":false,\"events\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    double since = argc >= 2 ? JSValueToNumber(ctx, argv[1], nullptr) : 0.0;
    if (!(since >= 0)) since = 0;
    int max_events = argc >= 3 ? js_int(ctx, argv[2]) : 500;
    return utf8_to_js(ctx, rtx::reader::CombatLogJson(pid, (std::uint64_t)since, max_events));
}

JSValueRef ClientState(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::ClientStateJson(pid));
}

JSValueRef GameTick(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeNumber(ctx, -1);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::uint32_t count = 0; double age_ms = 0;
    if (!rtx::reader::TickState(pid, count, age_ms)) return JSValueMakeNumber(ctx, -1);
    return JSValueMakeNumber(ctx, static_cast<double>(count));
}

JSValueRef GameTickState(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"count\":-1,\"age\":-1}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::uint32_t count = 0; double age_ms = 0;
    if (!rtx::reader::TickState(pid, count, age_ms))
        return utf8_to_js(ctx, "{\"count\":-1,\"age\":-1}");
    char buf[96];
    std::snprintf(buf, sizeof(buf), "{\"count\":%u,\"age\":%.1f}", count, age_ms);
    return utf8_to_js(ctx, buf);
}

JSValueRef Perks(JSContextRef ctx, JSObjectRef, JSObjectRef,
                 size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"items\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "perks:" + std::to_string(pid), "{\"items\":[]}",
                  [pid]{ return rtx::reader::PerksJson(pid); });
}

JSValueRef Inventory(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "inv:" + std::to_string(pid),
                  "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}",
                  [pid]{ return rtx::reader::InventoryJson(pid); });
}

JSValueRef ContainerItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int id   = js_int(ctx, argv[1]);
    return served(ctx, "cont:" + std::to_string(pid) + ":" + std::to_string(id),
                  "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}",
                  [pid, id]{ return rtx::reader::ContainerItemsJson(pid, id); });
}

JSValueRef OpenContainers(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"containers\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "opencont:" + std::to_string(pid), "{\"containers\":[]}",
                  [pid]{ return rtx::reader::OpenContainersJson(pid); });
}

JSValueRef Pof(JSContextRef ctx, JSObjectRef, JSObjectRef,
               size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"pens\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "pof:" + std::to_string(pid), "{\"pens\":[]}",
                  [pid]{ return rtx::reader::PofJson(pid); });
}

JSValueRef ItemExtraInts(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, "{\"present\":false,\"key\":[],\"pos\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int cid  = js_int(ctx, argv[1]);
    int iid  = js_int(ctx, argv[2]);
    int slot = (argc >= 4) ? js_int(ctx, argv[3]) : -1;
    return utf8_to_js(ctx, rtx::reader::ItemExtraIntsJson(pid, cid, iid, slot));
}

JSValueRef Varps(JSContextRef ctx, JSObjectRef, JSObjectRef,
                 size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string ids = js_to_utf8(ctx, argv[1]);
    return served(ctx, "varps:" + std::to_string(pid) + ":" + ids, "{}",
                  [pid, ids]{ return rtx::reader::VarpsJson(pid, ids); });
}

JSValueRef Varbits(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string ids = js_to_utf8(ctx, argv[1]);
    return served(ctx, "varbits:" + std::to_string(pid) + ":" + ids, "{}",
                  [pid, ids]{ return rtx::reader::VarbitsJson(pid, ids); });
}

JSValueRef ServerOps(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::reader::ServerOpsJson());
}

JSValueRef VarbitMap(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::VarbitMapJson());
}

// Varbit definitions of the non-player domains ({"2": varc bit fields, "5": item instance keys, ...}).
JSValueRef VarbitDomainMap(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::VarbitDomainMapJson());
}

JSValueRef VarbitDomains(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::VarbitDomainsJson());
}

JSValueRef VarDefs(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    int archive = argc > 0 ? (int)JSValueToNumber(ctx, argv[0], nullptr) : 60;
    return utf8_to_js(ctx, rtx::cache::VarDefsJson(archive));
}

// Free-vs-member is engine state (the PLAYERMEMBER op), not a var.
JSValueRef Membership(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "membership:" + std::to_string(pid),
                      [pid]{ return rtx::reader::MembershipJson(pid); });
}

JSValueRef Quests(JSContextRef ctx, JSObjectRef, JSObjectRef,
                  size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::QuestsJson());
}

JSValueRef VarpsDumpAll(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "varpsall:" + std::to_string(pid), "{}",
                  [pid]{ return rtx::reader::VarpsDumpAllJson(pid); });
}

JSValueRef VarcsDumpAll(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "varcsall:" + std::to_string(pid), "{}",
                  [pid]{ return rtx::reader::VarcsDumpAllJson(pid); });
}

JSValueRef VarDomainStores(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "vardomstores:" + std::to_string(pid), "{}",
                  [pid]{ return rtx::reader::VarDomainStoresJson(pid); });
}

JSValueRef VarcInts(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string ids = js_to_utf8(ctx, argv[1]);
    return served(ctx, "varcint:" + std::to_string(pid) + ":" + ids, "{}",
                  [pid, ids]{ return rtx::reader::VarcIntsJson(pid, ids); });
}

JSValueRef VarcLongs(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string ids = js_to_utf8(ctx, argv[1]);
    return served(ctx, "varclong:" + std::to_string(pid) + ":" + ids, "{}",
                  [pid, ids]{ return rtx::reader::VarcLongsJson(pid, ids); });
}

JSValueRef VarpsLong(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string ids = js_to_utf8(ctx, argv[1]);
    return served(ctx, "varplong:" + std::to_string(pid) + ":" + ids, "{}",
                  [pid, ids]{ return rtx::reader::VarpsLongJson(pid, ids); });
}

JSValueRef VarcStrings(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string ids = js_to_utf8(ctx, argv[1]);
    return served(ctx, "varcstr:" + std::to_string(pid) + ":" + ids, "{}",
                  [pid, ids]{ return rtx::reader::VarcStringsJson(pid, ids); });
}

JSValueRef VarcStringsDumpAll(JSContextRef ctx, JSObjectRef, JSObjectRef,
                              size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "varcstrall:" + std::to_string(pid), "{}",
                  [pid]{ return rtx::reader::VarcStringsDumpAllJson(pid); });
}

// Walkability of the (2r+1)^2 tiles around x,y on a plane, from the map cache's collision grid
// (the same data the overlay's tile tint uses). rows[i] is the row y-r+i, one char per x-r+j:
// '0' walkable, '1' fully blocked (scenery footprints, walls as tiles, void).
JSValueRef WalkGridFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 4) return utf8_to_js(ctx, "{}");
    int x = js_int(ctx, argv[0]), y = js_int(ctx, argv[1]), plane = js_int(ctx, argv[2]), r = js_int(ctx, argv[3]);
    if (x <= 0 || y <= 0 || x > 16383 || y > 16383) return utf8_to_js(ctx, "{}");
    if (r < 1) r = 1;
    if (r > 8) r = 8;
    if (plane < 0 || plane > 3) plane = 0;
    std::vector<std::uint8_t> g;
    rtx::cache::RegionBlockedFill(x, y, plane, r, g);
    const int T = 2 * r + 1;
    std::string rows;
    for (int gy = 0; gy < T; ++gy) {
        if (gy) rows += ",";
        rows += "\"";
        for (int gx = 0; gx < T; ++gx) {
            const std::size_t i = (std::size_t)gx * T + gy;
            rows += (i < g.size() && (g[i] & rtx::cache::kTileBlockFull)) ? '1' : '0';
        }
        rows += "\"";
    }
    return utf8_to_js(ctx, "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"plane\":" + std::to_string(plane) +
                           ",\"r\":" + std::to_string(r) + ",\"rows\":[" + rows + "]}");
}

JSValueRef GroundItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "[]");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "grounditems:" + std::to_string(pid), "[]",
                  [pid]{ return rtx::reader::GroundItemsJson(pid); });
}

JSValueRef VarsDump(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "varsdump:" + std::to_string(pid), "{}",
                  [pid]{ return rtx::reader::VarsDumpJson(pid); });
}

JSValueRef VarsWatch(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool on = JSValueToBoolean(ctx, argv[1]);
    return JSValueMakeBoolean(ctx, rtx::reader::VarsWatch(pid, on));
}

// Resolving the opcode table may involve an image scan; keep it off the UI thread.
JSValueRef ServerPackets(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"ok\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "serverpackets:" + std::to_string(pid), "{\"ok\":false}",
                  [pid]{ return rtx::reader::ServerPacketsJson(pid); });
}

// Synchronous on purpose: local ring read, and `since` must reflect this call.
JSValueRef ServerPacketFeed(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"ok\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::uint64_t since = (argc >= 2)
        ? (std::uint64_t)JSValueToNumber(ctx, argv[1], nullptr) : 0;
    return utf8_to_js(ctx, rtx::reader::ServerPacketFeedJson(pid, since));
}

JSValueRef ServerPacketArm(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool on = JSValueToBoolean(ctx, argv[1]);
    return JSValueMakeBoolean(ctx, rtx::reader::ServerPacketFeedEnable(pid, on));
}

JSValueRef Events(JSContextRef ctx, JSObjectRef, JSObjectRef,
                  size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::uint64_t since = (argc >= 2)
        ? (std::uint64_t)JSValueToNumber(ctx, argv[1], nullptr) : 0;
    return served_obj(ctx, pid, "events:" + std::to_string(pid),
                      [pid, since] { return rtx::reader::EventsJson(pid, since); });
}

JSValueRef EventsMask(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string csv = js_to_utf8(ctx, argv[1]);
    std::uint32_t mask[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::size_t i = 0;
    while (i < csv.size()) {
        while (i < csv.size() && !std::isdigit((unsigned char)csv[i])) ++i;
        if (i >= csv.size()) break;
        unsigned v = 0;
        while (i < csv.size() && std::isdigit((unsigned char)csv[i])) { v = v * 10 + (unsigned)(csv[i] - '0'); ++i; }
        if (v < 256) mask[v >> 5] |= 1u << (v & 31);
    }
    return JSValueMakeBoolean(ctx, rtx::reader::EventsMaskSet(pid, mask));
}

// which: 0 hide NPCs, 1 hide other players, 2 hide the whole scene.
JSValueRef RenderToggle(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return JSValueMakeBoolean(ctx, false);
    auto pid   = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int  which = js_int(ctx, argv[1]);
    bool on    = JSValueToBoolean(ctx, argv[2]);
    return JSValueMakeBoolean(ctx, rtx::reader::RenderToggle(pid, which, on));
}

JSValueRef OutlineObject(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 6) return JSValueMakeBoolean(ctx, false);
    auto pid  = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    rtx::reader::OutlineLocReq rq;
    rq.id    = js_int(ctx, argv[1]);
    rq.x     = js_int(ctx, argv[2]);
    rq.y     = js_int(ctx, argv[3]);
    rq.plane = js_int(ctx, argv[4]);
    bool on  = JSValueToBoolean(ctx, argv[5]);
    static std::mutex s_mu;
    static std::map<std::uint32_t, std::vector<rtx::reader::OutlineLocReq>> s_locs;
    std::lock_guard<std::mutex> lk(s_mu);
    auto& v = s_locs[pid];
    v.erase(std::remove_if(v.begin(), v.end(), [&](const rtx::reader::OutlineLocReq& e) {
        return e.id == rq.id && e.x == rq.x && e.y == rq.y && e.plane == rq.plane; }), v.end());
    if (on) v.push_back(rq);
    rtx::launcher::companion::EnsureLoaded(pid);
    rtx::overlay::SetOutlineLocs(pid, v);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef OutlineNpc(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int  uid = js_int(ctx, argv[1]);
    bool on  = JSValueToBoolean(ctx, argv[2]);
    static std::mutex s_mu;
    static std::map<std::uint32_t, std::vector<int>> s_uids;
    std::lock_guard<std::mutex> lk(s_mu);
    auto& v = s_uids[pid];
    v.erase(std::remove(v.begin(), v.end(), uid), v.end());
    if (on) v.push_back(uid);
    rtx::launcher::companion::EnsureLoaded(pid);
    rtx::overlay::SetOutline(pid, v);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef NameplatePlayer(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int  uid = js_int(ctx, argv[1]);
    bool on  = JSValueToBoolean(ctx, argv[2]);
    static std::mutex s_mu;
    static std::map<std::uint32_t, std::vector<int>> s_uids;
    std::lock_guard<std::mutex> lk(s_mu);
    auto& v = s_uids[pid];
    v.erase(std::remove(v.begin(), v.end(), uid), v.end());
    if (on) v.push_back(uid);
    rtx::launcher::companion::EnsureLoaded(pid);
    rtx::overlay::SetNameplatePlayers(pid, v);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef Equipment(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "equip:" + std::to_string(pid),
                  "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}",
                  [pid]{ return rtx::reader::EquipmentJson(pid); });
}

JSValueRef InterfaceGroups(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"groups\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::InterfaceGroupsJson(pid));
}

JSValueRef InterfaceGroup(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{\"widgets\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int gid = js_int(ctx, argv[1]);
    return utf8_to_js(ctx, rtx::reader::InterfaceGroupJson(pid, gid));
}

JSValueRef IfaceCompRects(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 4) return utf8_to_js(ctx, "{\"abs\":0,\"comps\":{}}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int gid = js_int(ctx, argv[1]);
    std::string comps = js_to_utf8(ctx, argv[2]);
    int mount = js_int(ctx, argv[3]);
    return utf8_to_js(ctx, rtx::reader::IfaceCompRectsJson(pid, gid, comps, mount));
}

JSValueRef IfaceSpriteParent(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, "{\"ok\":0}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int gid = js_int(ctx, argv[1]);
    int spr = js_int(ctx, argv[2]);
    return utf8_to_js(ctx, rtx::reader::IfaceSpriteParentRectJson(pid, gid, spr));
}

JSValueRef CompassHeading(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "-1");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, std::to_string(rtx::reader::CompassHeadingValue(pid)));
}

JSValueRef CompassTarget(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::CompassTargetJson(pid));
}


JSValueRef ScanSolution(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"ok\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::ScanSolutionJson(pid));
}

JSValueRef HoverEntity(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"ok\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::HoverEntityJson(pid));
}

JSValueRef PuzzleState(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "[]");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::PuzzleStateJson(pid));
}

JSValueRef PuzzleCellRects(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"abs\":0,\"cells\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::PuzzleCellRectsJson(pid));
}

JSValueRef PuzzleWdTable(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, icons::AssetFileBase64(L"wd_table.bin"));
}
JSValueRef PuzzlePdbTable(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, icons::AssetFileBase64(L"pdb_5554.bin"));
}

JSValueRef InterfaceSizeSearch(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, "{\"matches\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int w = js_int(ctx, argv[1]);
    int hgt = js_int(ctx, argv[2]);
    int tol = (argc >= 4) ? js_int(ctx, argv[3]) : 0;
    return utf8_to_js(ctx, rtx::reader::InterfaceSizeSearchJson(pid, w, hgt, tol));
}

JSValueRef Dialog(JSContextRef ctx, JSObjectRef, JSObjectRef,
                  size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "dialog:" + std::to_string(pid),
                      [pid]{ return rtx::reader::DialogJson(pid); });
}


JSValueRef InterfaceComps(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int group = js_int(ctx, argv[1]);
    std::string comps = js_to_utf8(ctx, argv[2]);
    return served(ctx, "ifcomps:" + std::to_string(pid) + ":" + std::to_string(group) + ":" + comps,
                  "{}", [pid, group, comps]{ return rtx::reader::InterfaceCompsJson(pid, group, comps); });
}

JSValueRef IfaceOffset(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return JSValueMakeBoolean(ctx, false);
    int gid = js_int(ctx, argv[0]);
    int dx = js_int(ctx, argv[1]);
    int dy = js_int(ctx, argv[2]);
    rtx::reader::SetIfaceOffset(gid, dx, dy);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef Chat(JSContextRef ctx, JSObjectRef, JSObjectRef,
                size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"lines\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "chat:" + std::to_string(pid), "{\"lines\":[]}",
                  [pid]{ return rtx::reader::ChatJson(pid); });
}

JSValueRef Buffs(JSContextRef ctx, JSObjectRef, JSObjectRef,
                 size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"buffs\":[],\"debuffs\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "buffs:" + std::to_string(pid), "{\"buffs\":[],\"debuffs\":[]}",
                  [pid]{ return rtx::reader::BuffsJson(pid); });
}

JSValueRef Cooldowns(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"cooldowns\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "cd:" + std::to_string(pid), "{\"cooldowns\":[]}",
                  [pid]{ return rtx::reader::AbilityCooldownsJson(pid); });
}

JSValueRef ActionBar(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"bars\":[]}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "ab:" + std::to_string(pid), "{\"bars\":[]}",
                  [pid]{ return rtx::reader::ActionBarJson(pid); });
}

JSValueRef OverlayConfig(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    rtx::overlay::Config c;
    if (argc >= 1) c.pid         = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    if (argc >= 2) c.enabled     = JSValueToBoolean(ctx, argv[1]);
    if (argc >= 3) c.grid        = JSValueToBoolean(ctx, argv[2]);
    if (argc >= 4) c.players     = JSValueToBoolean(ctx, argv[3]);
    if (argc >= 5) c.npcs        = JSValueToBoolean(ctx, argv[4]);
    if (argc >= 6) c.objects     = JSValueToBoolean(ctx, argv[5]);
    if (argc >= 7) c.radius      = js_int(ctx, argv[6]);
    if (argc >= 8) c.walk_only   = JSValueToBoolean(ctx, argv[7]);
    if (argc >= 9) c.interactable = JSValueToBoolean(ctx, argv[8]);
    if (argc >= 10) c.specials   = JSValueToBoolean(ctx, argv[9]);
    if (argc >= 11) c.markers    = JSValueToBoolean(ctx, argv[10]);
    if (argc >= 12) c.nameplates = JSValueToBoolean(ctx, argv[11]);
    if (argc >= 13) c.np_players = JSValueToBoolean(ctx, argv[12]);
    if (argc >= 14) c.np_npcs    = JSValueToBoolean(ctx, argv[13]);
    if (argc >= 15) c.np_objects = JSValueToBoolean(ctx, argv[14]);
    if (argc >= 17) c.occlude    = JSValueToBoolean(ctx, argv[16]);
    if (argc >= 18) c.true_tile  = JSValueToBoolean(ctx, argv[17]);
    if (argc >= 16) c.np_range   = js_int(ctx, argv[15]);
    rtx::overlay::Configure(c);
    return JSValueMakeBoolean(ctx, true);
}

namespace {
std::mutex g_hs_mu;
struct HsEntry { int state = 0; long long epoch = 0; std::string body; bool inflight = false; };  // state 0 none 1 ok 2 error; epoch = unix secs of last completed fetch
std::map<std::string, HsEntry> g_hiscores;
bool g_hs_loaded = false;
constexpr long long kHsTtlSec = 3600;

std::wstring hs_cache_path() {
    wchar_t buf[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    std::wstring dir = (n && n < MAX_PATH) ? std::wstring(buf) : L".";
    dir += L"\\RuneToolsX";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\hiscores.cache";
}
// on-disk format: one record/line "epoch\tname\tbody", body newlines -> \x1f
void hs_load() {   // under g_hs_mu
    if (g_hs_loaded) return;
    g_hs_loaded = true;
    std::ifstream f(hs_cache_path(), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto t1 = line.find('\t'); if (t1 == std::string::npos) continue;
        auto t2 = line.find('\t', t1 + 1); if (t2 == std::string::npos) continue;
        HsEntry e; e.state = 1;
        e.epoch = std::atoll(line.substr(0, t1).c_str());
        std::string name = line.substr(t1 + 1, t2 - t1 - 1);
        std::string body = line.substr(t2 + 1);
        for (char& c : body) if (c == '\x1f') c = '\n';
        e.body = body;
        if (!name.empty()) g_hiscores[name] = e;
    }
}
void hs_save() {   // under g_hs_mu
    std::ofstream f(hs_cache_path(), std::ios::binary | std::ios::trunc);
    for (const auto& kv : g_hiscores) {
        if (kv.second.state != 1) continue;
        bool bad = false;
        for (unsigned char c : kv.first) if (c < 0x20) { bad = true; break; }
        if (bad) continue;
        std::string body = kv.second.body;
        for (char& c : body) if (c == '\r') c = '\x1f'; else if (c == '\n') c = '\x1f';
        f << kv.second.epoch << '\t' << kv.first << '\t' << body << '\n';
    }
}

long long hs_now() { return (long long)time(nullptr); }

void hs_fetch(std::string name) {
    // spaces (including the NBSP RS uses in interface names, UTF-8 C2 A0) -> '_', percent-encode the rest
    std::string q;
    for (std::size_t i = 0; i < name.size(); ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c == ' ') q += '_';
        else if (c == 0xC2 && i + 1 < name.size() && (unsigned char)name[i + 1] == 0xA0) { q += '_'; ++i; }
        else if (std::isalnum(c) || c == '_' || c == '-') q += c;
        else { char b[8]; std::snprintf(b, sizeof(b), "%%%02X", c); q += b; }
    }
    std::wstring path = L"/m=hiscore/index_lite.ws?player=";
    for (unsigned char c : q) path.push_back((wchar_t)c);
    auto r = http::Get(L"secure.runescape.com", path, {});
    std::lock_guard<std::mutex> lk(g_hs_mu);
    auto& e = g_hiscores[name];
    e.inflight = false;
    e.epoch = hs_now();
    if (r.ok && r.status == 200 && !r.body.empty()) { e.state = 1; e.body = r.body; hs_save(); }
    else e.state = 2;
}
}  // namespace

JSValueRef HiscoresJson(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::string csv = (argc >= 1) ? js_to_utf8(ctx, argv[0]) : std::string();
    std::ostringstream out;
    out << '{';
    bool first = true;
    std::size_t pos = 0;
    std::lock_guard<std::mutex> lk(g_hs_mu);
    hs_load();
    long long now = hs_now();
    while (pos < csv.size()) {
        auto sep = csv.find(',', pos);
        std::string name = csv.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
        pos = (sep == std::string::npos) ? csv.size() : sep + 1;
        if (name.empty() || name.size() > 20) continue;
        if (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-Â ") != std::string::npos) continue;
        auto& e = g_hiscores[name];
        bool fresh = (e.state != 0) && (now - e.epoch < kHsTtlSec);
        if (!fresh && !e.inflight) {
            e.inflight = true;
            http::Enqueue([name] { hs_fetch(name); });
        }
        if (!first) out << ',';
        first = false;
        const char* s = (e.state == 1) ? "ok" : (e.inflight || e.state == 0) ? "pending" : "error";
        out << '"' << json_escape(name) << "\":{\"s\":\"" << s << '"';
        if (e.state == 1) out << ",\"b\":\"" << json_escape(e.body) << '"';
        out << '}';
    }
    out << '}';
    return utf8_to_js(ctx, out.str());
}


const int kLeaguesWorlds[] = {
    143, 144, 145, 146, 147,
    172, 173, 174, 175,
    190,
    208, 209,
    220, 221, 222, 223, 224,
    230, 231, 232, 233, 234,
    240, 241, 242, 243, 244,
    248,
    260, 261, 262, 263, 264, 265, 266,
    270, 271, 272, 273, 274, 275, 276, 277,
    279, 280, 281, 282, 283, 284, 285, 286, 287, 288,
    292, 293, 294, 295, 296, 297, 298,
};
bool is_leagues_world(int w) {
    for (int lw : kLeaguesWorlds) if (lw == w) return true;
    return false;
}

namespace {
std::mutex g_party_mu;
std::string g_party_code;                                   // "" = off
std::map<std::string, std::string> g_party_doors;           // "gx,gy" -> "<skill>|<level>"
std::map<std::string, std::string> g_party_hiscores;        // name -> levels CSV ("99,74,...")
std::map<std::string, int> g_party_noncrit;                 // "gx,gy" -> 1
std::map<int, int> g_party_critkeys;                        // key idx -> 1 promoted / 0 demoted
std::map<std::string, int> g_party_critrooms;               // "gx,gy" -> 1
std::atomic<bool> g_party_synced{ false };                  // server snapshot arrived for the current code
std::atomic<int> g_party_epoch{ 0 };                        // bumped on code change; old SSE loop exits
std::atomic<bool> g_party_loop_running{ false };

std::wstring party_code_path() {
    wchar_t buf[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    std::wstring dir = (n && n < MAX_PATH) ? std::wstring(buf) : L".";
    dir += L"\\RuneToolsX"; CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\party_code.txt";
}
void party_code_save(const std::string& code) {
    std::ofstream f(party_code_path(), std::ios::binary | std::ios::trunc);
    f << code;
}
std::string party_code_load() {
    std::ifstream f(party_code_path(), std::ios::binary);
    std::string s; std::getline(f, s);
    std::string out;
    for (char c : s) { if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c; }
    return out.size() >= 4 && out.size() <= 16 ? out : std::string();
}

std::string json_str_field(const std::string& j, const std::string& key) {
    auto p = j.find("\"" + key + "\"");
    if (p == std::string::npos) return "";
    p = j.find(':', p); if (p == std::string::npos) return "";
    p = j.find('"', p); if (p == std::string::npos) return "";
    auto e = j.find('"', p + 1); if (e == std::string::npos) return "";
    return j.substr(p + 1, e - p - 1);
}
long long json_num_field(const std::string& j, const std::string& key) {
    auto p = j.find("\"" + key + "\"");
    if (p == std::string::npos) return 0;
    p = j.find(':', p); if (p == std::string::npos) return 0;
    ++p; while (p < j.size() && (j[p] == ' ' || j[p] == '"')) ++p;
    return std::atoll(j.c_str() + p);
}
std::string json_levels_csv(const std::string& j) {
    auto p = j.find("\"levels\"");
    if (p == std::string::npos) return "";
    p = j.find('[', p); if (p == std::string::npos) return "";
    auto e = j.find(']', p); if (e == std::string::npos) return "";
    std::string inner = j.substr(p + 1, e - p - 1), out;
    std::size_t i = 0;
    while (i < inner.size()) {
        auto c = inner.find(',', i);
        std::string tok = inner.substr(i, c == std::string::npos ? std::string::npos : c - i);
        std::size_t a = tok.find_first_not_of(" \t"); std::size_t b = tok.find_last_not_of(" \t");
        tok = (a == std::string::npos) ? "" : tok.substr(a, b - a + 1);
        if (tok == "null") tok = "";
        out += tok;
        if (c == std::string::npos) break;
        out += ','; i = c + 1;
    }
    return out;
}

void party_apply_door(const std::string& payload) {
    std::string cell = json_str_field(payload, "cell");
    std::string skill = json_str_field(payload, "skill");
    long long level = json_num_field(payload, "level");
    if (cell.empty() || skill.empty() || level < 1 || level > 200) return;
    std::lock_guard<std::mutex> lk(g_party_mu);
    g_party_doors[cell] = skill + "|" + std::to_string(level);
}
void party_apply_hiscore(const std::string& payload) {
    std::string name = json_str_field(payload, "name");
    std::string csv = json_levels_csv(payload);
    if (name.empty() || csv.empty()) return;
    std::lock_guard<std::mutex> lk(g_party_mu);
    g_party_hiscores[name] = csv;
}
void party_apply_noncrit(const std::string& payload) {
    std::string cell = json_str_field(payload, "cell");
    if (cell.empty()) return;
    bool on = payload.find("\"on\":true") != std::string::npos;
    std::lock_guard<std::mutex> lk(g_party_mu);
    if (on) g_party_noncrit[cell] = 1; else g_party_noncrit.erase(cell);
}
void party_apply_critroom(const std::string& payload) {
    std::string cell = json_str_field(payload, "cell");
    if (cell.empty()) return;
    bool on = payload.find("\"on\":true") != std::string::npos;
    std::lock_guard<std::mutex> lk(g_party_mu);
    if (on) g_party_critrooms[cell] = 1; else g_party_critrooms.erase(cell);
}
void party_apply_critkey(const std::string& payload) {
    long long idx = json_num_field(payload, "idx");
    if (idx < 1 || idx > 64) return;
    bool on = payload.find("\"on\":true") != std::string::npos;
    std::lock_guard<std::mutex> lk(g_party_mu);
    g_party_critkeys[(int)idx] = on ? 1 : 0;
}
void party_apply_snapshot(const std::string& j) {
    { std::lock_guard<std::mutex> lk(g_party_mu); g_party_doors.clear(); g_party_hiscores.clear(); g_party_noncrit.clear(); g_party_critkeys.clear(); g_party_critrooms.clear(); }
    auto dseg = j.find("\"doors\"");
    auto hseg = j.find("\"hiscores\"");
    auto nseg = j.find("\"noncrit\"");
    if (dseg != std::string::npos) {
        std::size_t end = (hseg != std::string::npos && hseg > dseg) ? hseg : j.size();
        std::size_t i = dseg;
        while ((i = j.find('{', i)) != std::string::npos && i < end) {
            auto e = j.find('}', i); if (e == std::string::npos || e > end) break;
            party_apply_door(j.substr(i, e - i + 1)); i = e + 1;
        }
    }
    if (hseg != std::string::npos) {
        std::size_t end = (nseg != std::string::npos && nseg > hseg) ? nseg : j.size();
        std::size_t i = hseg;
        while ((i = j.find('{', i)) != std::string::npos && i < end) {
            auto e = j.find('}', i); if (e == std::string::npos || e > end) break;
            party_apply_hiscore(j.substr(i, e - i + 1)); i = e + 1;
        }
    }
    if (nseg != std::string::npos) {
        std::size_t lb = j.find('[', nseg), rb = j.find(']', nseg);
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::lock_guard<std::mutex> lk(g_party_mu);
            std::size_t i = lb;
            while (true) {
                std::size_t q = j.find('"', i); if (q == std::string::npos || q > rb) break;
                std::size_t e = j.find('"', q + 1); if (e == std::string::npos || e > rb) break;
                g_party_noncrit[j.substr(q + 1, e - q - 1)] = 1;
                i = e + 1;
            }
        }
    }
    auto cseg = j.find("\"critkeys\"");
    if (cseg != std::string::npos) {
        std::size_t i = cseg;
        while ((i = j.find('{', i)) != std::string::npos) {
            auto e = j.find('}', i); if (e == std::string::npos) break;
            party_apply_critkey(j.substr(i, e - i + 1)); i = e + 1;
        }
    }
    auto rseg = j.find("\"critrooms\"");
    if (rseg != std::string::npos) {
        std::size_t lb = j.find('[', rseg), rb = j.find(']', rseg);
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::lock_guard<std::mutex> lk(g_party_mu);
            std::size_t i = lb;
            while (true) {
                std::size_t q = j.find('"', i); if (q == std::string::npos || q > rb) break;
                std::size_t e = j.find('"', q + 1); if (e == std::string::npos || e > rb) break;
                g_party_critrooms[j.substr(q + 1, e - q - 1)] = 1;
                i = e + 1;
            }
        }
    }
    g_party_synced.store(true);
}

void party_events_loop(int epoch, std::string code) {
    int backoff = 3000;
    std::wstring path = L"/api/party/events?code=";
    for (unsigned char c : code) path.push_back((wchar_t)c);
    while (g_party_epoch.load() == epoch) {
        std::vector<http::Header> hdrs = { { "Accept", "text/event-stream" } };
        std::string buf, cur_ev;
        auto r = http::Stream(kUpdateHost, path, hdrs,
            [&](const char* d, std::size_t n) -> bool {
                if (g_party_epoch.load() != epoch) return false;
                buf.append(d, n);
                std::size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("event:", 0) == 0) {
                        cur_ev = line.substr(6);
                        if (!cur_ev.empty() && cur_ev.front() == ' ') cur_ev.erase(cur_ev.begin());
                    } else if (line.rfind("data:", 0) == 0) {
                        std::string dat = line.substr(5);
                        if (!dat.empty() && dat.front() == ' ') dat.erase(dat.begin());
                        if (dat.size() < 65536) {
                            if (cur_ev == "pdoor") party_apply_door(dat);
                            else if (cur_ev == "phiscore") party_apply_hiscore(dat);
                            else if (cur_ev == "pnoncrit") party_apply_noncrit(dat);
                            else if (cur_ev == "pcritkey") party_apply_critkey(dat);
                            else if (cur_ev == "pcritroom") party_apply_critroom(dat);
                            else if (cur_ev == "psnapshot") party_apply_snapshot(dat);
                            else if (cur_ev == "preset") {   // floor change; hiscores persist
                                std::lock_guard<std::mutex> lk(g_party_mu);
                                g_party_doors.clear(); g_party_noncrit.clear(); g_party_critkeys.clear(); g_party_critrooms.clear();
                            }
                        }
                    } else if (line.empty()) cur_ev.clear();
                }
                if (buf.size() > 128 * 1024) buf.clear();
                return true;
            },
            [](int status) { return status == 200; });
        if (g_party_epoch.load() != epoch) break;
        backoff = (r.ok && r.status == 200) ? 3000 : (backoff * 2 > 60000 ? 60000 : backoff * 2);
        for (int slept = 0; slept < backoff && g_party_epoch.load() == epoch; slept += 500)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    g_party_loop_running.store(false);
}

void party_start_loop() {
    std::string code;
    { std::lock_guard<std::mutex> lk(g_party_mu); code = g_party_code; }
    int epoch = ++g_party_epoch;
    if (code.empty()) return;
    bool expected = false;
    if (g_party_loop_running.compare_exchange_strong(expected, true))
        std::thread([epoch, code] { guarded("party events", [&] { party_events_loop(epoch, code); }); }).detach();
    else
        std::thread([epoch, code]() { guarded("party restart", [&] {
            for (int i = 0; i < 120 && g_party_loop_running.load() && g_party_epoch.load() == epoch; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (g_party_epoch.load() != epoch) return;
            bool e2 = false;
            if (g_party_loop_running.compare_exchange_strong(e2, true))
                std::thread([epoch, code] { guarded("party events", [&] { party_events_loop(epoch, code); }); }).detach();
        }); }).detach();
}
}  // namespace

JSValueRef PartySetCode(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::string raw = (argc >= 1) ? js_to_utf8(ctx, argv[0]) : std::string();
    std::string code;
    for (char c : raw) { if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) code += c; }
    if (!code.empty() && (code.size() < 4 || code.size() > 16)) return JSValueMakeBoolean(ctx, false);
    {
        std::lock_guard<std::mutex> lk(g_party_mu);
        g_party_code = code; g_party_doors.clear(); g_party_hiscores.clear(); g_party_noncrit.clear(); g_party_critkeys.clear(); g_party_critrooms.clear();
    }
    g_party_synced.store(false);
    party_code_save(code);
    party_start_loop();
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef PartyGetCode(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    std::string code;
    {
        std::lock_guard<std::mutex> lk(g_party_mu);
        if (g_party_code.empty()) { g_party_code = party_code_load(); code = g_party_code; }
        else code = g_party_code;
    }
    if (!code.empty() && !g_party_loop_running.load()) party_start_loop();
    return utf8_to_js(ctx, code);
}
static bool json_value_ok(const std::string& d) {
    if (d.size() > 4096) return false;
    std::size_t i = 0;
    while (i < d.size() && std::isspace((unsigned char)d[i])) ++i;
    if (i >= d.size() || (d[i] != '{' && d[i] != '[')) return false;
    std::vector<char> st; bool str = false;
    for (; i < d.size(); ++i) {
        unsigned char c = (unsigned char)d[i];
        if (str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') str = false;
            continue;
        }
        if (c < 0x20) return false;
        if (c == '"') str = true;
        else if (c == '{' || c == '[') st.push_back((char)c);
        else if (c == '}' || c == ']') {
            if (st.empty() || st.back() != (c == '}' ? '{' : '[')) return false;
            st.pop_back();
        }
    }
    return !str && st.empty();
}
JSValueRef PartyReport(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::string kind = js_to_utf8(ctx, argv[0]);
    std::string data = js_to_utf8(ctx, argv[1]);
    if (kind != "door" && kind != "hiscore" && kind != "noncrit" && kind != "reset" && kind != "critkey" && kind != "critroom") return JSValueMakeBoolean(ctx, false);
    std::string code;
    { std::lock_guard<std::mutex> lk(g_party_mu); code = g_party_code; }
    if (code.empty()) return JSValueMakeBoolean(ctx, false);
    if (!json_value_ok(data)) return JSValueMakeBoolean(ctx, false);
    std::string body = "{\"code\":\"" + code + "\",\"kind\":\"" + kind + "\",\"data\":" + data + "}";
    http::Enqueue([body]() {
        std::vector<http::Header> hdrs = { { "Content-Type", "application/json" } };
        http::PostJson(kUpdateHost, L"/api/party/report", hdrs, body);
    });
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef PartyData(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    std::lock_guard<std::mutex> lk(g_party_mu);
    std::string out = "{\"code\":\"" + json_escape(g_party_code) + "\",\"synced\":"
                    + (g_party_synced.load() ? "true" : "false") + ",\"doors\":{";
    bool first = true;
    for (const auto& kv : g_party_doors) {
        auto bar = kv.second.find('|'); if (bar == std::string::npos) continue;
        std::string skill = kv.second.substr(0, bar), level = kv.second.substr(bar + 1);
        if (!first) out += ',';
        first = false;
        out += "\"" + json_escape(kv.first) + "\":{\"skill\":\"" + json_escape(skill) + "\",\"level\":" + level + "}";
    }
    out += "},\"hiscores\":{";
    first = true;
    for (const auto& kv : g_party_hiscores) {
        if (!first) out += ',';
        first = false;
        std::string arr = "[";
        const std::string& csv = kv.second;
        std::size_t i = 0; bool f2 = true;
        while (i <= csv.size()) {
            std::size_t c = csv.find(',', i);
            std::string tok = csv.substr(i, c == std::string::npos ? std::string::npos : c - i);
            if (!f2) arr += ',';
            f2 = false;
            arr += tok.empty() ? "null" : tok;
            if (c == std::string::npos) break;
            i = c + 1;
        }
        arr += "]";
        out += "\"" + json_escape(kv.first) + "\":" + arr;
    }
    out += "},\"noncrit\":[";
    first = true;
    for (const auto& kv : g_party_noncrit) {
        if (!first) out += ',';
        first = false;
        out += "\"" + json_escape(kv.first) + "\"";
    }
    out += "],\"critkeys\":{";
    first = true;
    for (const auto& kv : g_party_critkeys) {
        if (!first) out += ',';
        first = false;
        out += "\"" + std::to_string(kv.first) + "\":" + (kv.second ? "1" : "0");
    }
    out += "},\"critrooms\":[";
    first = true;
    for (const auto& kv : g_party_critrooms) {
        if (!first) out += ',';
        first = false;
        out += "\"" + json_escape(kv.first) + "\"";
    }
    out += "]}";
    return utf8_to_js(ctx, out);
}

JSValueRef MarkersGet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    return utf8_to_js(ctx, rtx::markers::GetJson(pid));
}
JSValueRef MarkersVersion(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    return JSValueMakeNumber(ctx, (double)rtx::markers::Version(pid));
}
JSValueRef MarkerAdd(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 5) return JSValueMakeBoolean(ctx, false);
    auto pid   = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    int region = js_int(ctx, argv[1]);
    int lx     = js_int(ctx, argv[2]);
    int ly     = js_int(ctx, argv[3]);
    int plane  = js_int(ctx, argv[4]);
    std::uint32_t color = (argc >= 6) ? (std::uint32_t)JSValueToNumber(ctx, argv[5], nullptr) : 0x46E0C0;
    std::string label = (argc >= 7) ? js_to_utf8(ctx, argv[6]) : std::string();
    return JSValueMakeBoolean(ctx, rtx::markers::Add(pid, region, lx, ly, plane, color, label));
}
JSValueRef MarkerRemove(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 5) return JSValueMakeBoolean(ctx, false);
    auto pid   = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    int region = js_int(ctx, argv[1]);
    int lx     = js_int(ctx, argv[2]);
    int ly     = js_int(ctx, argv[3]);
    int plane  = js_int(ctx, argv[4]);
    return JSValueMakeBoolean(ctx, rtx::markers::Remove(pid, region, lx, ly, plane));
}
JSValueRef MarkerSetLabel(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 6) return JSValueMakeBoolean(ctx, false);
    auto pid   = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    int region = js_int(ctx, argv[1]);
    int lx     = js_int(ctx, argv[2]);
    int ly     = js_int(ctx, argv[3]);
    int plane  = js_int(ctx, argv[4]);
    std::string label = js_to_utf8(ctx, argv[5]);
    return JSValueMakeBoolean(ctx, rtx::markers::SetLabel(pid, region, lx, ly, plane, label));
}
JSValueRef MarkerSetColor(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 6) return JSValueMakeBoolean(ctx, false);
    auto pid   = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    int region = js_int(ctx, argv[1]);
    int lx     = js_int(ctx, argv[2]);
    int ly     = js_int(ctx, argv[3]);
    int plane  = js_int(ctx, argv[4]);
    std::uint32_t color = (std::uint32_t)JSValueToNumber(ctx, argv[5], nullptr);
    std::uint32_t color2 = (argc >= 7) ? (std::uint32_t)JSValueToNumber(ctx, argv[6], nullptr) : 0;
    return JSValueMakeBoolean(ctx, rtx::markers::SetColor(pid, region, lx, ly, plane, color, color2));
}
JSValueRef MarkersClear(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    return JSValueMakeBoolean(ctx, rtx::markers::Clear(pid));
}
JSValueRef MarkerKeybindsGet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t, const JSValueRef[], JSValueRef*) {
    auto kb = rtx::markers::GetKeybinds();
    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "{\"mark\":%d,\"remove\":%d,\"color\":%u}",
        kb.markVk, kb.removeVk, kb.defColor & 0xFFFFFFu);
    return utf8_to_js(ctx, buf);
}
JSValueRef MarkerKeybindsSet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    rtx::markers::Keybinds kb;
    if (argc >= 1) kb.markVk   = js_int(ctx, argv[0]);
    if (argc >= 2) kb.removeVk = js_int(ctx, argv[1]);
    if (argc >= 3) kb.defColor = (std::uint32_t)JSValueToNumber(ctx, argv[2], nullptr);
    rtx::markers::SetKeybinds(kb);
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef MarkerKeybindsArm(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool on = JSValueToBoolean(ctx, argv[1]);
    rtx::markers::SetKeybindArmed(pid, on);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef OverlayHighlight(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::vector<std::string> names;
    if (argc >= 2) {
        std::string csv = js_to_utf8(ctx, argv[1]);
        std::size_t start = 0;
        while (start <= csv.size()) {
            std::size_t comma = csv.find(',', start);
            std::string part = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            std::size_t a = part.find_first_not_of(" \t");
            std::size_t b = part.find_last_not_of(" \t");
            if (a != std::string::npos) names.push_back(part.substr(a, b - a + 1));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    rtx::overlay::SetHighlight(pid, names);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef OpenClientWindow(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(
        JSValueToNumber(ctx, argv[0], nullptr));
    dock::EnsureClient(pid);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef CloseClientWindow(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeUndefined(ctx);
    auto pid = static_cast<std::uint32_t>(
        JSValueToNumber(ctx, argv[0], nullptr));
    dock::RemoveClient(pid);
    return JSValueMakeUndefined(ctx);
}

JSValueRef ClientWindowOpen(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(
        JSValueToNumber(ctx, argv[0], nullptr));
    return JSValueMakeBoolean(ctx, dock::IsOpen(pid));
}

JSValueRef DockCollapse(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    return JSValueMakeUndefined(ctx);
}

JSValueRef KeepFocused(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeUndefined(ctx);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool on  = JSValueToBoolean(ctx, argv[1]);
    dock::SetKeepFocused(pid, on);
    return JSValueMakeUndefined(ctx);
}

JSValueRef LocMorphs(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "[]");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::LocMorphsJson(pid, js_to_utf8(ctx, argv[1])));
}

JSValueRef GameFocused(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return JSValueMakeBoolean(ctx, dock::GameFocused(pid));
}

JSValueRef HostFullscreen(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    const bool on = (argc >= 2) ? JSValueToBoolean(ctx, argv[1])
                                : !dock::IsHostFullscreen(pid);
    dock::SetHostFullscreen(pid, on);
    return JSValueMakeBoolean(ctx, dock::IsHostFullscreen(pid));
}

JSValueRef RailTip(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t, const JSValueRef[], JSValueRef*) {
    return JSValueMakeUndefined(ctx);
}

JSValueRef UiRects(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string csv = js_to_utf8(ctx, argv[1]);
    bool visible = (argc >= 3) ? JSValueToBoolean(ctx, argv[2]) : true;
    gameui::SetConsumeRects(pid, csv, visible);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UiKeyboard(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    gameui::SetKeyboardCapture(pid, JSValueToBoolean(ctx, argv[1]));
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UiScale(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    dock::SetUiScaleMultiplier(pid, JSValueToNumber(ctx, argv[1], nullptr));
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UiClientInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, gameui::ClientInfoJson(pid).c_str());
}

JSValueRef MyPid(JSContextRef ctx, JSObjectRef, JSObjectRef,
                 size_t, const JSValueRef[], JSValueRef*) {
    JSObjectRef global = JSContextGetGlobalObject(ctx);
    JSStringRef key = JSStringCreateWithUTF8CString("__rtx_pid");
    JSValueRef v = JSObjectGetProperty(ctx, global, key, nullptr);
    JSStringRelease(key);
    double n = JSValueToNumber(ctx, v, nullptr);
    return JSValueMakeNumber(ctx, n);
}

JSValueRef CloseProcess(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) {
        return utf8_to_js(ctx,
            R"({"success":false,"detail":"missing pid"})");
    }
    auto pid = static_cast<std::uint32_t>(
        JSValueToNumber(ctx, argv[0], nullptr));
    bool tracked = false;
    for (const auto& p : process::ScanRsClients()) if (p.pid == pid) { tracked = true; break; }
    if (!tracked) {
        return utf8_to_js(ctx,
            R"({"success":false,"detail":"not a tracked RuneScape client"})");
    }
    bool ok = process::TerminateByPid(pid);
    std::ostringstream os;
    os << "{\"success\":" << (ok ? "true" : "false")
       << ",\"detail\":\""
       << (ok ? "process terminated"
              : "could not terminate (gone or access denied)")
       << "\"}";
    return utf8_to_js(ctx, os.str());
}

JSValueRef ListAccounts(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    auto list = accounts::List();
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& a = list[i];
        if (i) os << ",";
        os << "{"
           << "\"id\":\""           << json_escape(a.id) << "\","
           << "\"display_name\":\"" << json_escape(a.display_name) << "\","
           << "\"character_id\":\"" << json_escape(a.character_id) << "\","
           << "\"captured_at\":\""  << json_escape(a.captured_at) << "\""
           << "}";
    }
    os << "]";
    return utf8_to_js(ctx, os.str());
}

JSValueRef RemoveAccount(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto id = get_string_arg(ctx, argc, argv, 0);
    bool ok = accounts::Remove(id);
    std::ostringstream os;
    os << "{\"success\":" << (ok ? "true" : "false") << "}";
    return utf8_to_js(ctx, os.str());
}

std::filesystem::path account_capture_cfg() { return runetools_dir() / L"account_capture.txt"; }
std::mutex g_cap_mu;
bool       g_cap_on = true, g_cap_loaded = false;

bool account_capture_get() {
    std::lock_guard<std::mutex> lk(g_cap_mu);
    if (!g_cap_loaded) {
        g_cap_loaded = true;
        std::ifstream f(account_capture_cfg());
        int v = 1;
        if (f && (f >> v)) g_cap_on = (v != 0);
    }
    return g_cap_on;
}

void account_capture_set(bool on) {
    std::lock_guard<std::mutex> lk(g_cap_mu);
    g_cap_loaded = true;
    g_cap_on = on;
    std::error_code ec; std::filesystem::create_directories(runetools_dir(), ec);
    std::ofstream f(account_capture_cfg(), std::ios::trunc);
    if (f) f << (on ? 1 : 0);
}

JSValueRef AccountCapture(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc >= 1) account_capture_set(JSValueToBoolean(ctx, argv[0]));
    return JSValueMakeBoolean(ctx, account_capture_get());
}

JSValueRef LaunchAccount(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto id = get_string_arg(ctx, argc, argv, 0);
    accounts::Account a;
    if (!accounts::Get(id, a)) {
        return utf8_to_js(ctx,
            R"({"success":false,"detail":"unknown account or vault locked"})");
    }
    // rs2client.exe falls back to the rs-launch:// handler on a non-empty JX_ACCESS_TOKEN it can't use, so ACCESS/REFRESH are forced empty.
    auto take = [&](const char* k) -> std::string {
        auto it = a.env.find(k);
        return (it == a.env.end()) ? std::string{} : it->second;
    };
    std::unordered_map<std::string, std::string> env = {
        { "JX_ACCESS_TOKEN",  "" },
        { "JX_REFRESH_TOKEN", "" },
        { "JX_DISPLAY_NAME",  take("JX_DISPLAY_NAME") },
        { "JX_CHARACTER_ID",  take("JX_CHARACTER_ID") },
        { "JX_SESSION_ID",    take("JX_SESSION_ID")   },
    };
    auto res = loader::LaunchClientWithEnv(L"", env);
    std::ostringstream os;
    os << "{\"success\":" << (res.success ? "true" : "false")
       << ",\"pid\":"     << res.pid
       << ",\"detail\":\"" << json_escape(res.detail) << "\"}";
    return utf8_to_js(ctx, os.str());
}

JSValueRef OpenLog(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t, const JSValueRef[], JSValueRef*) {
    std::filesystem::path dir = rtx::log::LogDir();
    std::filesystem::path target;
    if      (std::filesystem::exists(dir)) target = dir;
    else if (std::filesystem::exists(dir.parent_path())) target = dir.parent_path();
    if (!target.empty()) {
        ShellExecuteW(nullptr, L"open", target.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
    return JSValueMakeUndefined(ctx);
}

JSValueRef BridgeStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    bool attached = false;
    const char* reader = "detached";
    std::string build = "0";
    if (pid) {
        const std::string j = rtx::reader::SamplesJson();
        auto at = j.find("\"pid\":" + std::to_string(pid) + ",");
        if (at != std::string::npos) {
            attached = true;
            auto end = j.find("\"ge_slots\"", at);
            const std::string rec = j.substr(at, end == std::string::npos ? std::string::npos : end - at);
            const std::string cv = json_str(rec, "client_version");
            if (!cv.empty()) build = "\"" + json_escape(cv) + "\"";
            reader = json_str(rec, "status_label").empty() ? "stale" : "ok";
        }
    }
    return utf8_to_js(ctx, std::string("{\"ok\":true,\"attached\":") + (attached ? "true" : "false") +
                           ",\"reader\":\"" + reader + "\",\"build\":" + build +
                           ",\"launcher\":\"" + json_escape(running_version()) + "\"}");
}

extern std::mutex  g_latest_mu;
extern std::string g_latest_json;
void refresh_latest();
JSValueRef LatestVersion(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    std::string j;
    { std::lock_guard<std::mutex> lk(g_latest_mu); j = g_latest_json; }
    if (j.empty() || j == "{}") { std::thread(refresh_latest).detach(); j = "{}"; }
    return utf8_to_js(ctx, j);
}

constexpr int     kCheckIntervalMs = 60 * 60'000;
constexpr wchar_t kEventsPath[]    = L"/api/client/events";
std::mutex        g_latest_mu;
std::string       g_latest_json = "{}";
std::atomic<bool> g_checker_started{ false };

void refresh_latest() {
    static std::atomic<long long> last_ms{ 0 };
    long long now = (long long)GetTickCount64();
    long long prev = last_ms.load();
    if (now - prev < 3000) return;
    if (!last_ms.compare_exchange_strong(prev, now)) return;

    auto r = http::Get(kUpdateHost, kLatestPath, {});
    if (r.ok && r.status == 200) {
        std::lock_guard<std::mutex> lk(g_latest_mu);
        g_latest_json = r.body;
    }
}

void update_checker_loop() {
    for (;;) {
        refresh_latest();
        for (int slept = 0; slept < kCheckIntervalMs; slept += 1000)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

constexpr wchar_t kVosPath[]       = L"/api/vos";
constexpr wchar_t kVosReportPath[] = L"/api/vos/report";
std::mutex             g_vos_mu;
std::string            g_vos_json = "{}";
std::atomic<long long> g_vos_fetched_ms{ 0 };   // when data landed; must not move on a failed GET
std::atomic<long long> g_vos_get_ms{ 0 };       // last GET attempt; floors the fallback to one per 5 min
std::atomic<bool>      g_vos_fetching{ false };
std::atomic<long long> g_vos_reported_ms{ 0 };
std::atomic<long long> g_vos_reported_lg_ms{ 0 };   // leagues pool has its own floor
std::atomic<bool>      g_sse_alive{ false };        // while alive the GET fallbacks stay off
std::atomic<long long> g_sse_last_rx_ms{ 0 };       // a dead TCP connection keeps g_sse_alive true; heartbeat is 25s
bool sse_alive_now() {
    return g_sse_alive.load() &&
           ((long long)GetTickCount64() - g_sse_last_rx_ms.load()) < 40'000;
}

// Server keeps each world for a fixed 5-minute window from first sighting; rows carry absolute expiry.
constexpr wchar_t kScarabPath[]       = L"/api/scarabs";
constexpr wchar_t kScarabReportPath[] = L"/api/scarabs/report";
std::mutex             g_scarab_mu;
std::string            g_scarab_json = "{}";
std::atomic<long long> g_scarab_fetched_ms{ 0 };   // when data landed
std::atomic<long long> g_scarab_get_ms{ 0 };       // last GET attempt (fallback floor)
std::atomic<bool>      g_scarab_fetching{ false };

// ---- Menaphos Soul Obelisk tracker (loc 109495): sightings classified by tile against kObeliskSpawns; 7.5-minute window from first sighting ----
constexpr wchar_t kObeliskPath[]       = L"/api/obelisks";
constexpr wchar_t kObeliskReportPath[] = L"/api/obelisks/report";
std::mutex             g_obelisk_mu;
std::string            g_obelisk_json = "{}";
std::atomic<long long> g_obelisk_fetched_ms{ 0 };
std::atomic<long long> g_obelisk_get_ms{ 0 };
std::atomic<bool>      g_obelisk_fetching{ false };

std::string stamp_rx_at(const std::string& json) {
    if (json.size() < 2 || json.front() != '{') return json;
    using namespace std::chrono;
    long long ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return "{\"rxAt\":" + std::to_string(ms) + (json[1] == '}' ? "" : ",") + json.substr(1);
}

struct ObeliskSpawn { int x, y, district; };   // district: 1 Imperial, 2 Worker, 3 Merchant, 4 Port
constexpr ObeliskSpawn kObeliskSpawns[] = {
    { 3191, 2709, 1 },   // Imperial District
    { 3167, 2797, 2 },   // Worker District
    { 3217, 2783, 3 },   // Merchant District
    { 3141, 2644, 4 },   // Port District
};

int vos_json_int(const std::string& j, const char* key) {
    std::string needle = "\"" + std::string(key) + "\":";
    auto p = j.find(needle);
    if (p == std::string::npos) return 0;
    p += needle.size();
    long v = 0; bool any = false;
    while (p < j.size() && j[p] >= '0' && j[p] <= '9') { v = v * 10 + (j[p] - '0'); ++p; any = true; }
    return any ? (int)v : 0;
}

long long json_ll(const std::string& j, const char* key) {
    std::string needle = "\"" + std::string(key) + "\":";
    auto p = j.find(needle);
    if (p == std::string::npos) return 0;
    p += needle.size();
    long long v = 0; bool any = false;
    while (p < j.size() && j[p] >= '0' && j[p] <= '9') { v = v * 10 + (j[p] - '0'); ++p; any = true; }
    return any ? v : 0;
}

std::string vos_json_section(const std::string& j, const char* name) {
    const std::string needle = "\"" + std::string(name) + "\":{";
    auto p = j.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    int depth = 1;
    const auto start = p;
    for (; p < j.size() && depth > 0; ++p) {
        if (j[p] == '{') ++depth;
        else if (j[p] == '}') --depth;
    }
    return depth == 0 ? j.substr(start, p - start - 1) : std::string();
}

bool vos_cache_stale() {
    long long fetched = g_vos_fetched_ms.load();
    return fetched != 0 && ((long long)GetTickCount64() - fetched) > 65 * 60'000;
}

bool vos_cache_has_current_hour() {
    if (vos_cache_stale()) return false;
    SYSTEMTIME st; GetSystemTime(&st);
    std::string needle = "\"h\":" + std::to_string((int)st.wHour);
    std::lock_guard<std::mutex> lk(g_vos_mu);
    bool hourOk = g_vos_json.find(needle + ",") != std::string::npos ||
                  g_vos_json.find(needle + "}") != std::string::npos;
    if (!hourOk) return false;
    // "d" = UTC day number (days since epoch); 0 = no date stamp, hour check alone.
    int d = vos_json_int(g_vos_json, "d");
    if (d == 0) return true;
    long long today = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() / 86400;
    return d == (int)today;
}

bool vos_post_report(int a, int b, bool leagues) {
    if (a < 1 || a > 8 || b < 1 || b > 8 || a == b) return false;
    {
        SYSTEMTIME st; GetSystemTime(&st);
        if (st.wMinute == 59 && st.wSecond >= 57) return false;
    }
    long long now = (long long)GetTickCount64();
    std::atomic<long long>& floorMs = leagues ? g_vos_reported_lg_ms : g_vos_reported_ms;
    long long prev = floorMs.load();
    if (prev != 0 && now - prev < 5 * 60'000) return false;
    if (!floorMs.compare_exchange_strong(prev, now)) return false;
    http::Enqueue([a, b, now, leagues] {
        std::atomic<long long>& floorMs = leagues ? g_vos_reported_lg_ms : g_vos_reported_ms;
        std::string body = "{\"a\":" + std::to_string(a) + ",\"b\":" + std::to_string(b) +
                           (leagues ? ",\"l\":1" : "") + "}";
        std::vector<http::Header> hdrs = { { "Content-Type", "application/json" } };
        auto r = http::PostJson(kUpdateHost, kVosReportPath, hdrs, body);
        if (!r.ok || r.status != 200) {
            rtx::log::Launcher("vos report failed: status " + std::to_string(r.status) + " " + r.detail);
            long long cur = now;
            floorMs.compare_exchange_strong(cur, now - (5 * 60'000 - 30'000));
        }
    });
    return true;
}

// Corrupted Scarab detection: swarm ids 109473/109475/109477 (id match; other "Corrupted Scarab" entities exist).
bool scene_has_scarab(const std::string& scene) {
    static const char* kIds[] = { "\"id\":109473,", "\"id\":109475,", "\"id\":109477," };
    for (const char* n : kIds)
        if (scene.find(n) != std::string::npos) return true;
    return false;
}
// District code when the scene holds loc 109495 at a known spawn tile (+-3 for the 2x2 anchor), else 0.
int scene_obelisk_district(const std::string& scene) {
    std::size_t p = 0;
    while ((p = scene.find("\"id\":109495,\"x\":", p)) != std::string::npos) {
        p += 16;
        int x = atoi(scene.c_str() + p);
        std::size_t py = scene.find("\"y\":", p);
        if (py == std::string::npos) break;
        int y = atoi(scene.c_str() + py + 4);
        for (const auto& sp : kObeliskSpawns)
            if (abs(x - sp.x) <= 3 && abs(y - sp.y) <= 3) return sp.district;
        rtx::log::Launcher("soul obelisk 109495 sighted at UNMAPPED tile " +
                           std::to_string(x) + "," + std::to_string(y));
        p = py;
    }
    return 0;
}

// Row shape: [world, ...extra, expiresAtMs]; server-now is estimated as payload.now + (localNow - rxAt).
bool payload_lists_world(const std::string& json, int world) {
    const std::string key = "\"worlds\":[";
    auto start = json.find(key);
    if (start == std::string::npos) return false;
    start += key.size() - 1;
    int depth = 0;
    std::size_t end = start;
    for (; end < json.size(); ++end) {
        if (json[end] == '[') ++depth;
        else if (json[end] == ']' && --depth == 0) { ++end; break; }
    }
    const std::string needle = "[" + std::to_string(world) + ",";
    auto hit = json.find(needle, start);
    if (hit == std::string::npos || hit >= end) return false;
    auto rowEnd = json.find(']', hit);
    if (rowEnd == std::string::npos || rowEnd > end) return true;
    std::size_t q = rowEnd;
    while (q > hit && json[q - 1] >= '0' && json[q - 1] <= '9') --q;
    if (q == rowEnd) return true;
    long long expiresAt = 0;
    for (std::size_t i = q; i < rowEnd; ++i) expiresAt = expiresAt * 10 + (json[i] - '0');
    long long srvNow = json_ll(json, "now");
    long long rxAt = json_ll(json, "rxAt");
    if (expiresAt <= 0 || srvNow <= 0 || rxAt <= 0) return true;
    using namespace std::chrono;
    long long localNow = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return expiresAt > srvNow + (localNow - rxAt);
}

void scarab_scan_pass() {
    static std::unordered_map<int, long long> last_scarab;    // world -> tick
    static std::unordered_map<int, long long> last_obelisk;
    auto snaps = rtx::reader::SampleAll();
    for (const auto& s : snaps) {
        if (s.status != 30 || s.world <= 0) continue;
        rtx::launcher::companion::EnsureLoaded(s.pid);
        std::string scene = rtx::reader::SceneJson(s.pid, 50);
        long long now = (long long)GetTickCount64();
        if (scene_has_scarab(scene)) {
            bool listed;
            { std::lock_guard<std::mutex> lk(g_scarab_mu); listed = payload_lists_world(g_scarab_json, s.world); }
            auto it = last_scarab.find(s.world);
            if (!listed && (it == last_scarab.end() || now - it->second >= 60'000)) {
                last_scarab[s.world] = now;
                int w = s.world;
                http::Enqueue([w] {
                    std::string body = "{\"w\":" + std::to_string(w) + "}";
                    std::vector<http::Header> hdrs = { { "Content-Type", "application/json" } };
                    auto r = http::PostJson(kUpdateHost, kScarabReportPath, hdrs, body);
                    if (!r.ok || r.status != 200)
                        rtx::log::Launcher("scarab report w" + std::to_string(w) + " failed: status " +
                                           std::to_string(r.status) + " " + r.detail);
                });
            }
        }
        int district = scene_obelisk_district(scene);
        if (district > 0) {
            bool listed;
            { std::lock_guard<std::mutex> lk(g_obelisk_mu); listed = payload_lists_world(g_obelisk_json, s.world); }
            auto it = last_obelisk.find(s.world);
            if (!listed && (it == last_obelisk.end() || now - it->second >= 60'000)) {
                last_obelisk[s.world] = now;
                int w = s.world, d = district;
                http::Enqueue([w, d] {
                    std::string body = "{\"w\":" + std::to_string(w) + ",\"d\":" + std::to_string(d) + "}";
                    std::vector<http::Header> hdrs = { { "Content-Type", "application/json" } };
                    auto r = http::PostJson(kUpdateHost, kObeliskReportPath, hdrs, body);
                    if (!r.ok || r.status != 200)
                        rtx::log::Launcher("obelisk report w" + std::to_string(w) + " failed: status " +
                                           std::to_string(r.status) + " " + r.detail);
                });
            }
        }
    }
}

std::string voter_id() {
    static std::mutex mu;
    static std::string cached;
    std::lock_guard<std::mutex> lk(mu);
    if (!cached.empty()) return cached;

    wchar_t* prof = nullptr; size_t n = 0;
    std::wstring path;
    if (_wdupenv_s(&prof, &n, L"USERPROFILE") == 0 && prof) {
        path = std::wstring(prof) + L"\\RuneToolsX\\voter.txt";
        free(prof);
    }
    if (!path.empty()) {
        std::ifstream in(path);
        std::string v;
        if (in && (in >> v) && v.size() == 32 &&
            v.find_first_not_of("0123456789abcdef") == std::string::npos) {
            cached = v;
            return cached;
        }
    }
    unsigned char bytes[16]{};
    BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    char buf[33]{};
    for (int i = 0; i < 16; ++i) std::snprintf(buf + i * 2, 3, "%02x", bytes[i]);
    cached.assign(buf, 32);
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (out) out << cached;
    }
    return cached;
}

JSValueRef WorldEventVote(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return JSValueMakeBoolean(ctx, false);
    std::string kind = js_to_utf8(ctx, argv[0]);
    int world = js_int(ctx, argv[1]);
    bool yes = JSValueToBoolean(ctx, argv[2]);
    std::uint32_t pid = (argc >= 4) ? (std::uint32_t)JSValueToNumber(ctx, argv[3], nullptr) : 0;
    if ((kind != "scarabs" && kind != "obelisks") || world < 1 || world > 999)
        return JSValueMakeBoolean(ctx, false);
    std::string v = voter_id();
    http::Enqueue([kind, world, yes, v, pid] {
        std::string voter = v;
        if (pid) {                        // may hit the PEB, deliberately off the UI thread
            std::string acct = account_key_for(pid);
            if (!acct.empty()) {
                unsigned long long h = 1469598103934665603ULL;          // FNV-1a 64
                for (unsigned char c : acct) { h ^= c; h *= 1099511628211ULL; }
                char hex[17];
                std::snprintf(hex, sizeof hex, "%016llx", h);
                voter += hex;
            }
        }
        std::wstring path = L"/api/" + std::wstring(kind.begin(), kind.end()) + L"/vote";
        std::string body = "{\"w\":" + std::to_string(world) + ",\"v\":\"" + voter +
                           "\",\"y\":" + (yes ? "true" : "false") + "}";
        std::vector<http::Header> hdrs = { { "Content-Type", "application/json" } };
        http::PostJson(kUpdateHost, path.c_str(), hdrs, body);
    });
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef ObeliskCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    bool allow_refresh = (argc >= 1) && JSValueToBoolean(ctx, argv[0]);
    if (allow_refresh && sse_alive_now()) allow_refresh = false;
    long long now = (long long)GetTickCount64();
    long long prev = g_obelisk_get_ms.load();
    if (allow_refresh && (prev == 0 || now - prev >= 120'000) &&
        g_obelisk_get_ms.compare_exchange_strong(prev, now)) {
        bool expected = false;
        if (g_obelisk_fetching.compare_exchange_strong(expected, true)) {
            http::Enqueue([] {
                auto r = http::Get(kUpdateHost, kObeliskPath, {});
                if (r.ok && r.status == 200 && r.body.size() < 32768 &&
                    !r.body.empty() && r.body.front() == '{') {
                    std::lock_guard<std::mutex> lk(g_obelisk_mu);
                    g_obelisk_json = stamp_rx_at(r.body);
                    g_obelisk_fetched_ms.store((long long)GetTickCount64());
                }
                g_obelisk_fetching.store(false);
            });
        }
    }
    std::lock_guard<std::mutex> lk(g_obelisk_mu);
    return utf8_to_js(ctx, g_obelisk_json);
}

JSValueRef ScarabCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    bool allow_refresh = (argc >= 1) && JSValueToBoolean(ctx, argv[0]);
    if (allow_refresh && sse_alive_now()) allow_refresh = false;
    long long now = (long long)GetTickCount64();
    long long prev = g_scarab_get_ms.load();
    if (allow_refresh && (prev == 0 || now - prev >= 120'000) &&
        g_scarab_get_ms.compare_exchange_strong(prev, now)) {
        bool expected = false;
        if (g_scarab_fetching.compare_exchange_strong(expected, true)) {
            http::Enqueue([] {
                auto r = http::Get(kUpdateHost, kScarabPath, {});
                if (r.ok && r.status == 200 && r.body.size() < 32768 &&
                    !r.body.empty() && r.body.front() == '{') {
                    std::lock_guard<std::mutex> lk(g_scarab_mu);
                    g_scarab_json = stamp_rx_at(r.body);
                    g_scarab_fetched_ms.store((long long)GetTickCount64());
                }
                g_scarab_fetching.store(false);
            });
        }
    }
    std::lock_guard<std::mutex> lk(g_scarab_mu);
    return utf8_to_js(ctx, g_scarab_json);
}

// Feeds the combat log: every logged-in client's actor rings, five times a second. The client
// list refreshes every two seconds; the ring poll itself is a few hundred small reads.
void combat_log_loop() {
    std::vector<std::uint32_t> pids; long long listed = 0;
    for (;;) {
        long long now = (long long)GetTickCount64();
        if (now - listed >= 2000) {
            listed = now; pids.clear();
            for (const auto& s : rtx::reader::SampleAll()) if (s.status == 30) pids.push_back(s.pid);
        }
        for (auto pid : pids) rtx::reader::CombatLogPoll(pid);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void world_event_scan_loop() {
    for (;;) {
        scarab_scan_pass();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

void vos_report_loop() {
    for (;;) {
        SYSTEMTIME st; GetSystemTime(&st);
        int hr = (int)st.wHour;
        int ca = 0, cb = 0, la = 0, lb = 0;
        bool cacheCurrent = vos_cache_has_current_hour();
        if (cacheCurrent) {
            std::lock_guard<std::mutex> lk(g_vos_mu);
            ca = vos_json_int(g_vos_json, "a");
            cb = vos_json_int(g_vos_json, "b");
            const std::string lg = vos_json_section(g_vos_json, "lg");
            if (!lg.empty()) { la = vos_json_int(lg, "a"); lb = vos_json_int(lg, "b"); }
        }
        auto snaps = rtx::reader::SampleAll();
        for (const auto& s : snaps) {
            if (s.status != 30) continue;                 // in-game only
            const bool lgWorld = is_leagues_world(s.world);
            // varp 4783 freezes on leaving Priff and varbit 26416 is hour-only, so gate on the Priff region box (region = tile>>6; X 32-35, Y 51-54)
            int tx = 0, ty = 0, pl = 0;
            if (!rtx::reader::PlayerTile(s.pid, tx, ty, pl)) continue;
            int rx = tx >> 6, ry = ty >> 6;
            if (rx < 32 || rx > 35 || ry < 51 || ry > 54) continue;
            std::string vb = rtx::reader::VarbitsJson(s.pid, "25158,25159,26416");
            int a = vos_json_int(vb, "25158"), b = vos_json_int(vb, "25159");
            int stamp = vos_json_int(vb, "26416");
            if (a >= 1 && a <= 8 && b >= 1 && b <= 8 && a != b && stamp == hr) {
                const int pa = lgWorld ? la : ca, pb = lgWorld ? lb : cb;
                if (!cacheCurrent || a != pa || b != pb) vos_post_report(a, b, lgWorld);
                continue;
            }
        }
        for (int slept = 0; slept < 15'000; slept += 1000)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void update_events_loop() {
    int backoff = 3000;
    std::string buf;
    for (;;) {
        std::vector<http::Header> hdrs = {
            { "Accept", "text/event-stream" },
        };
        buf.clear();
        std::string cur_ev;
        auto r = http::Stream(kUpdateHost, kEventsPath, hdrs,
            [&buf, &cur_ev](const char* d, std::size_t n) -> bool {
                g_sse_alive.store(true);
                g_sse_last_rx_ms.store((long long)GetTickCount64());
                buf.append(d, n);
                std::size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, nl);
                    buf.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("event:", 0) == 0) {
                        std::string ev = line.substr(6);
                        if (!ev.empty() && ev.front() == ' ') ev.erase(ev.begin());
                        if (ev == "update" || ev == "hello") std::thread(refresh_latest).detach();
                        cur_ev = ev;
                    } else if (line.rfind("data:", 0) == 0) {
                        if (cur_ev == "vos" || cur_ev == "scarab" || cur_ev == "obelisk") {
                            std::string dat = line.substr(5);
                            if (!dat.empty() && dat.front() == ' ') dat.erase(dat.begin());
                            // a full tracker payload is ~22KB
                            if (!dat.empty() && dat.size() < (cur_ev == "vos" ? 512u : 32768u) && dat.front() == '{') {
                                if (cur_ev == "vos") {
                                    std::lock_guard<std::mutex> lk(g_vos_mu);
                                    g_vos_json = dat;
                                    g_vos_fetched_ms.store((long long)GetTickCount64());
                                } else if (cur_ev == "scarab") {
                                    std::lock_guard<std::mutex> lk(g_scarab_mu);
                                    g_scarab_json = stamp_rx_at(dat);
                                    g_scarab_fetched_ms.store((long long)GetTickCount64());
                                } else {
                                    std::lock_guard<std::mutex> lk(g_obelisk_mu);
                                    g_obelisk_json = stamp_rx_at(dat);
                                    g_obelisk_fetched_ms.store((long long)GetTickCount64());
                                }
                            }
                        }
                    } else if (line.empty()) {
                        cur_ev.clear();
                    }
                }
                if (buf.size() > 64 * 1024) buf.clear();
                return true;
            },
            [](int status) { return status == 200; });
        g_sse_alive.store(false);
        backoff = (r.ok && r.status == 200) ? 3000 : (backoff * 2 > 60000 ? 60000 : backoff * 2);
        if (r.status != 0 && r.status != 200)
            rtx::log::Launcher("sse stream rejected: status " + std::to_string(r.status) +
                               ", reconnect in " + std::to_string(backoff / 1000) + "s");
        for (int slept = 0; slept < backoff; slept += 500)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

JSValueRef LatestVersionCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t, const JSValueRef[], JSValueRef*) {
    bool expected = false;
    if (g_checker_started.compare_exchange_strong(expected, true)) {
        std::thread([] { guarded("update checker", update_checker_loop); }).detach();
        std::thread([] { guarded("update events", update_events_loop); }).detach();
        std::thread([] { guarded("vos report", vos_report_loop); }).detach();
        std::thread([] { guarded("world event scan", world_event_scan_loop); }).detach();
        std::thread([] { guarded("combat log", combat_log_loop); }).detach();
    }
    std::lock_guard<std::mutex> lk(g_latest_mu);
    return utf8_to_js(ctx, g_latest_json);
}

// Varbits 25158/25159 (pair) + 26416 (hour stamp) only update in/near Prifddinas.
JSValueRef VosCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    bool allow_refresh = (argc >= 1) && JSValueToBoolean(ctx, argv[0]);
    if (allow_refresh && sse_alive_now()) allow_refresh = false;
    if (allow_refresh && vos_cache_has_current_hour()) allow_refresh = false;
    long long now = (long long)GetTickCount64();
    long long prev = g_vos_get_ms.load();
    if (allow_refresh && (prev == 0 || now - prev >= 300'000) &&
        g_vos_get_ms.compare_exchange_strong(prev, now)) {
        bool expected = false;
        if (g_vos_fetching.compare_exchange_strong(expected, true)) {
            http::Enqueue([] {
                auto r = http::Get(kUpdateHost, kVosPath, {});
                if (r.ok && r.status == 200 && r.body.size() < 512 &&
                    !r.body.empty() && r.body.front() == '{') {
                    std::lock_guard<std::mutex> lk(g_vos_mu);
                    g_vos_json = r.body;
                    g_vos_fetched_ms.store((long long)GetTickCount64());
                }
                g_vos_fetching.store(false);
            });
        }
    }
    if (vos_cache_stale()) return utf8_to_js(ctx, "{}");
    std::lock_guard<std::mutex> lk(g_vos_mu);
    return utf8_to_js(ctx, g_vos_json);
}

constexpr wchar_t kPricesLatestPath[]  = L"/api/prices/latest";
constexpr wchar_t kPricesMappingPath[] = L"/api/prices/mapping";
std::mutex             g_prices_mu;
std::string            g_prices_json = "{}";
std::string            g_prices_map_json = "[]";
std::atomic<long long> g_prices_ms{ 0 };          // when data landed
std::atomic<long long> g_prices_get_ms{ 0 };      // last attempt
std::atomic<bool>      g_prices_fetching{ false };
std::atomic<long long> g_prices_map_ms{ 0 };
std::atomic<long long> g_prices_map_get_ms{ 0 };
std::atomic<bool>      g_prices_map_fetching{ false };
std::string            g_prices_etag, g_prices_map_etag;   // relay ETags (guarded by g_prices_mu)

// One relay fetch per TTL, gzip on the wire, and conditional: the relay answers If-None-Match with a 304
// when nothing changed (the mapping changes once a day), which costs a few hundred bytes instead of the body.
void prices_kick(const wchar_t* path, std::size_t cap, std::string* slot, std::string* etag_slot,
                 std::atomic<long long>& data_ms, std::atomic<long long>& get_ms,
                 std::atomic<bool>& fetching, long long ttl_ms, long long floor_ms) {
    long long now = (long long)GetTickCount64();
    long long fresh = data_ms.load();
    if (fresh && now - fresh < ttl_ms) return;
    long long prev = get_ms.load();
    if (prev && now - prev < floor_ms) return;
    if (!get_ms.compare_exchange_strong(prev, now)) return;
    bool expected = false;
    if (!fetching.compare_exchange_strong(expected, true)) return;
    std::wstring p(path);
    std::string etag;
    { std::lock_guard<std::mutex> lk(g_prices_mu); etag = *etag_slot; }
    http::Enqueue([p, cap, slot, etag_slot, etag, &data_ms, &fetching] {
        std::vector<http::Header> hdrs;
        if (!etag.empty()) hdrs.push_back({ "If-None-Match", etag });
        auto r = http::Fetch(kUpdateHost, p, hdrs, cap);
        if (r.status == 304 && !etag.empty()) {
            data_ms.store((long long)GetTickCount64());     // still current: extend the TTL, keep the body
        } else if (r.ok && r.status == 200 && !r.body.empty() &&
                   (r.body.front() == '{' || r.body.front() == '[')) {
            std::lock_guard<std::mutex> lk(g_prices_mu);
            *slot = std::move(r.body);
            *etag_slot = r.header("ETag");
            data_ms.store((long long)GetTickCount64());
        }
        fetching.store(false);
    });
}

JSValueRef PricesCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t, const JSValueRef[], JSValueRef*) {
    prices_kick(kPricesLatestPath, 1u * 1024 * 1024, &g_prices_json, &g_prices_etag,
                g_prices_ms, g_prices_get_ms, g_prices_fetching, 120'000, 30'000);
    std::lock_guard<std::mutex> lk(g_prices_mu);
    return utf8_to_js(ctx, g_prices_json);
}
JSValueRef PricesMapping(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    prices_kick(kPricesMappingPath, 4u * 1024 * 1024, &g_prices_map_json, &g_prices_map_etag,
                g_prices_map_ms, g_prices_map_get_ms, g_prices_map_fetching, 21'600'000, 600'000);
    std::lock_guard<std::mutex> lk(g_prices_mu);
    return utf8_to_js(ctx, g_prices_map_json);
}

struct PricesTsEntry { std::string json = "{}"; long long ms = 0; bool fetching = false; };
std::mutex g_prices_ts_mu;
std::map<std::string, PricesTsEntry> g_prices_ts;

JSValueRef PricesTimeseries(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    int id = (argc >= 1) ? (int)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string lb = get_string_arg(ctx, argc, argv, 1);
    static const char* kLb[] = { "6h", "24h", "7d", "30d", "6m", "1y" };
    bool lbOk = false;
    for (const char* l : kLb) if (lb == l) { lbOk = true; break; }
    if (id <= 0 || id > 10'000'000 || !lbOk) return utf8_to_js(ctx, "{}");
    const std::string key = std::to_string(id) + ":" + lb;
    std::string cached = "{}";
    bool kick = false;
    {
        std::lock_guard<std::mutex> lk(g_prices_ts_mu);
        auto& e = g_prices_ts[key];
        cached = e.json;
        long long now = (long long)GetTickCount64();
        if (!e.fetching && (e.ms == 0 || now - e.ms > 300'000)) { e.fetching = true; kick = true; }
        if (g_prices_ts.size() > 48) {
            auto oldest = g_prices_ts.end();
            for (auto it = g_prices_ts.begin(); it != g_prices_ts.end(); ++it)
                if (!it->second.fetching && (oldest == g_prices_ts.end() || it->second.ms < oldest->second.ms)) oldest = it;
            if (oldest != g_prices_ts.end()) g_prices_ts.erase(oldest);
        }
    }
    if (kick) {
        std::wstring path = L"/api/prices/timeseries?id=" + std::to_wstring(id) + L"&lookback=";
        for (char c : lb) path.push_back((wchar_t)c);
        http::Enqueue([path, key] {
            auto r = http::Fetch(kUpdateHost, path, {}, 1u * 1024 * 1024);
            std::lock_guard<std::mutex> lk(g_prices_ts_mu);
            auto& e = g_prices_ts[key];
            if (r.ok && r.status == 200 && !r.body.empty() && r.body.front() == '{') {
                e.json = std::move(r.body);
                e.ms = (long long)GetTickCount64();
            } else {
                if (e.json == "{}") e.json = "{\"unavailable\":1}";
                e.ms = (long long)GetTickCount64() - 240'000;
            }
            e.fetching = false;
        });
    }
    return utf8_to_js(ctx, cached);
}

JSValueRef IsLeaguesWorld(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    return JSValueMakeBoolean(ctx, is_leagues_world(js_int(ctx, argv[0])));
}

JSValueRef LeaguesWorlds(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    std::string out = "[";
    for (std::size_t i = 0; i < sizeof(kLeaguesWorlds) / sizeof(kLeaguesWorlds[0]); ++i) {
        if (i) out += ',';
        out += std::to_string(kLeaguesWorlds[i]);
    }
    out += "]";
    return utf8_to_js(ctx, out);
}

JSValueRef VosReport(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    int a = (argc >= 1) ? js_int(ctx, argv[0]) : 0;
    int b = (argc >= 2) ? js_int(ctx, argv[1]) : 0;
    bool lg = (argc >= 3) && JSValueToBoolean(ctx, argv[2]);
    return JSValueMakeBoolean(ctx, vos_post_report(a, b, lg));
}

JSValueRef ClientsRunning(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"rs2client.exe") == 0) ++n;
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    return JSValueMakeNumber(ctx, n);
}

JSValueRef PasteClipboard(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    if (!OpenClipboard(nullptr)) return utf8_to_js(ctx, "");
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::string out;
    if (h) {
        if (auto* wide = static_cast<wchar_t*>(GlobalLock(h))) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                        nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                out.assign(static_cast<size_t>(n - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(),
                                    n, nullptr, nullptr);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    while (!out.empty() &&
           (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return utf8_to_js(ctx, out);
}

bool clipboard_set_text(const std::string& text) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wn <= 0 || !OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wn * sizeof(wchar_t))) {
            if (auto* w = static_cast<wchar_t*>(GlobalLock(h))) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, w, wn);
                GlobalUnlock(h);
                ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
                if (!ok) GlobalFree(h);
            } else {
                GlobalFree(h);
            }
        }
    }
    CloseClipboard();
    return ok;
}

JSValueRef CopyClipboard(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    return JSValueMakeBoolean(ctx, clipboard_set_text(get_string_arg(ctx, argc, argv, 0)));
}

JSValueRef CaptureScreenshot(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::wstring path = capture_for_pid(pid);
    if (path.empty()) return utf8_to_js(ctx, "");
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u8(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, u8.data(), n, nullptr, nullptr);
    return utf8_to_js(ctx, u8);
}

JSValueRef WikiOpen(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string term = argc > 1 ? js_to_utf8(ctx, argv[1]) : "";
    if (term.size() > 200) term.resize(200);
    rtx::launcher::wiki::Open(pid, term);
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef WikiClose(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc >= 1)
        rtx::launcher::wiki::Close(static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr)));
    return JSValueMakeBoolean(ctx, true);
}
JSValueRef WikiKeybindGet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    return JSValueMakeNumber(ctx, rtx::launcher::wiki::KeybindVk());
}
JSValueRef WikiKeybindSet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc >= 1)
        rtx::launcher::wiki::KeybindSet(js_int(ctx, argv[0]));
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef ScreenshotKeybindGet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                size_t, const JSValueRef[], JSValueRef*) {
    std::lock_guard<std::mutex> lk(g_ss_mu);
    ss_load_locked();
    return JSValueMakeNumber(ctx, g_ss_vk);
}

JSValueRef ScreenshotKeybindSet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                size_t argc, const JSValueRef argv[], JSValueRef*) {
    int vk = (argc >= 1) ? js_int(ctx, argv[0]) : 0;
    if (vk < 0 || vk > 255) vk = 0;
    std::lock_guard<std::mutex> lk(g_ss_mu);
    g_ss_loaded = true; g_ss_vk = vk; ss_save_locked();
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef HidePanelsKeybindGet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                size_t, const JSValueRef[], JSValueRef*) {
    std::lock_guard<std::mutex> lk(g_hp_mu);
    hp_load_locked();
    return JSValueMakeNumber(ctx, g_hp_vk);
}
JSValueRef HidePanelsKeybindSet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                size_t argc, const JSValueRef argv[], JSValueRef*) {
    int vk = (argc >= 1) ? js_int(ctx, argv[0]) : 0;
    if (vk < 0 || vk > 255) vk = 0;
    std::lock_guard<std::mutex> lk(g_hp_mu);
    g_hp_loaded = true; g_hp_vk = vk; hp_save_locked();
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef OpenScreenshots(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t, const JSValueRef[], JSValueRef*) {
    std::error_code ec; std::filesystem::create_directories(screenshots_dir(), ec);
    ShellExecuteW(nullptr, L"open", screenshots_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return JSValueMakeUndefined(ctx);
}

JSValueRef PlayAlertSound(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    std::string name = js_to_utf8(ctx, argv[0]);
    std::string safe;
    for (char c : name)
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') safe.push_back(c);
    if (safe.empty()) return JSValueMakeBoolean(ctx, false);

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::filesystem::path p = std::filesystem::path(exe).parent_path()
                                / L"sounds" / (safe + ".wav");
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return JSValueMakeBoolean(ctx, false);
    BOOL ok = PlaySoundW(p.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    return JSValueMakeBoolean(ctx, ok ? true : false);
}

std::string game_path_json() {
    const std::wstring custom = loader::CustomRsClientPath();
    const std::wstring autod  = loader::AutoRsClientPath();
    const std::wstring path   = custom.empty() ? autod : custom;
    static std::mutex cache_mu;
    static std::wstring cache_path; static std::uintmax_t cache_size = 0;
    static std::filesystem::file_time_type cache_mtime{};
    static loader::SignerCheck cache_sc{ false, false, {}, {} };
    loader::SignerCheck sc{ false, false, {}, {} };
    if (!path.empty()) {
        std::error_code ec;
        const auto size  = std::filesystem::file_size(path, ec);
        const auto mtime = std::filesystem::last_write_time(path, ec);
        std::lock_guard<std::mutex> lk(cache_mu);
        if (path != cache_path || size != cache_size || mtime != cache_mtime) {
            cache_sc = loader::VerifyGameSigner(path);
            cache_path = path; cache_size = size; cache_mtime = mtime;
        }
        sc = cache_sc;
    }
    std::ostringstream os;
    os << "{\"path\":\""   << json_escape(wide_to_utf8(path))
       << "\",\"auto\":\"" << json_escape(wide_to_utf8(autod))
       << "\",\"custom\":"  << (custom.empty() ? "false" : "true")
       << ",\"found\":"      << (path.empty() ? "false" : "true")
       << ",\"signed\":"     << (sc.ok ? "true" : "false")
       << ",\"signer\":\""  << json_escape(sc.subject)
       << "\",\"reason\":\"" << json_escape(sc.ok ? std::string{} : sc.reason) << "\"}";
    return os.str();
}

JSValueRef GamePath(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, game_path_json());
}

JSValueRef GamePathPick(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::wstring chosen; bool cancelled = true;
    {
        IFileOpenDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
            static const COMDLG_FILTERSPEC kTypes[] = {
                { L"RuneScape.exe", L"RuneScape.exe" },
                { L"Programs (*.exe)", L"*.exe" },
            };
            dlg->SetFileTypes(2, kTypes);
            dlg->SetTitle(L"Locate RuneScape.exe");
            dlg->SetFileName(L"RuneScape.exe");
            DWORD opts = 0; dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT);
            std::wstring cur = loader::DefaultRsClientPath();
            if (!cur.empty()) {
                IShellItem* folder = nullptr;
                std::wstring dir = std::filesystem::path(cur).parent_path().wstring();
                if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                    dlg->SetFolder(folder); folder->Release();
                }
            }
            if (SUCCEEDED(dlg->Show(static_cast<HWND>(g_launcherHwnd)))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item))) {
                    PWSTR psz = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                        chosen = psz; CoTaskMemFree(psz); cancelled = false;
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }
    }
    if (init == S_OK || init == S_FALSE) CoUninitialize();

    if (cancelled) return utf8_to_js(ctx, "{\"cancelled\":true}");
    if (std::string why = loader::SetCustomRsClientPath(chosen); !why.empty())
        return utf8_to_js(ctx, "{\"error\":\"" + json_escape(why) + "\"}");
    return utf8_to_js(ctx, game_path_json());
}

JSValueRef GamePathReset(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    loader::SetCustomRsClientPath(L"");
    return utf8_to_js(ctx, game_path_json());
}

// Renderer choice the Jagex launcher applies at the next game start (preferences.cfg next to rs2client).
std::filesystem::path renderer_cfg_path() {
    wchar_t pd[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH) == 0) return {};
    return std::filesystem::path(pd) / L"Jagex" / L"launcher" / L"preferences.cfg";
}

std::string renderer_json() {
    std::string renderer, err;
    auto path = renderer_cfg_path();
    std::ifstream f(path);
    if (!f) err = "preferences.cfg not found";
    std::string line;
    while (f && std::getline(f, line)) {
        if (line.rfind("renderer=", 0) != 0) continue;
        renderer = line.substr(9);
        while (!renderer.empty() && (renderer.back() == '\r' || renderer.back() == ' ')) renderer.pop_back();
        for (auto& c : renderer) c = (char)std::tolower((unsigned char)c);
    }
    bool running = false;
    try {
        for (const auto& info : rtx::launcher::process::ScanRsClients())
            if (_wcsicmp(info.name.c_str(), L"rs2client.exe") == 0) { running = true; break; }
    } catch (...) {}
    return "{\"renderer\":\"" + json_escape(renderer) + "\",\"running\":" + (running ? "true" : "false") +
           ",\"error\":\"" + json_escape(err) + "\"}";
}

JSValueRef GpuTiming(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    return utf8_to_js(ctx, rtx::reader::GpuTimingJson(pid));
}

JSValueRef RendererPref(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, renderer_json());
}

JSValueRef RendererSet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::string want = argc >= 1 ? js_to_utf8(ctx, argv[0]) : std::string();
    if (want != "opengl" && want != "vulkan")
        return utf8_to_js(ctx, "{\"error\":\"unknown renderer\"}");
    auto path = renderer_cfg_path();
    std::vector<std::string> lines;
    bool replaced = false;
    {
        std::ifstream f(path);
        std::string line;
        while (f && std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("renderer=", 0) == 0) { line = "renderer=" + want; replaced = true; }
            lines.push_back(line);
        }
    }
    if (!replaced) lines.push_back("renderer=" + want);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return utf8_to_js(ctx, "{\"error\":\"preferences.cfg is not writable\"}");
    for (const auto& l : lines) out << l << '\n';
    out.close();
    return utf8_to_js(ctx, renderer_json());
}


bool clipboard_set_text(const std::string& text);

// ---- Discord webhook alerts ----
// The URL is DPAPI-sealed on disk and never leaves this process: JS and plugins only ever get a
// masked hint. Sends carry allowed_mentions.parse=[] so no message can ping anyone, are
// rate-limited per source and globally, and back off on 429.
namespace discord {

std::mutex   g_mu;
bool         g_loaded = false;
std::wstring g_host, g_path;
std::string  g_hint;
double       g_globalTokens = 5.0; long long g_globalTs = 0;
std::unordered_map<std::string, std::pair<double, long long>> g_srcBuckets;
long long    g_cooldownUntil = 0;

long long now_ms() { return (long long)GetTickCount64(); }

std::filesystem::path store_path() {
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    return dir / L"discord.bin";
}

bool parse_url(const std::string& url, std::wstring& host, std::wstring& path, std::string& hint) {
    // discord.com only: the copy button in Discord produces exactly this shape.
    static const std::regex re("^https://(discord\\.com)(/api/webhooks/[0-9]{17,20}/[A-Za-z0-9_\\-]{60,120})$");
    std::smatch m;
    if (url.size() > 300 || !std::regex_match(url, m, re)) return false;
    std::string h = m[1].str(), p = m[2].str();
    host.assign(h.begin(), h.end());
    path.assign(p.begin(), p.end());
    std::size_t tok = p.rfind('/');
    std::string id = p.substr(14, tok > 14 ? tok - 14 : 0);
    hint = (id.size() > 4 ? id.substr(0, 4) + "\xe2\x80\xa6" : id) + "/\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
    return true;
}

void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    g_host.clear(); g_path.clear(); g_hint.clear();
    std::ifstream f(store_path(), std::ios::binary);
    if (!f) return;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return;
    std::string url = crypto::UnprotectForCurrentUser(bytes);
    std::wstring h, p; std::string hint;
    if (parse_url(url, h, p, hint)) { g_host = h; g_path = p; g_hint = hint; }
}

std::string status_json() {
    std::lock_guard<std::mutex> lk(g_mu);
    load_locked();
    return std::string("{\"configured\":") + (g_path.empty() ? "false" : "true") +
           ",\"hint\":\"" + json_escape(g_hint) + "\"}";
}

std::string set_url(std::string url) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_loaded = false;
    std::error_code ec;
    if (url.empty()) { std::filesystem::remove(store_path(), ec); load_locked(); return "{\"configured\":false,\"hint\":\"\"}"; }
    // Accept the URL as Discord copies it, a schemeless one, or just "<id>/<token>".
    if (url.rfind("discord.com/", 0) == 0) url = "https://" + url;
    else if (url.rfind("http", 0) != 0)
        url = "https://discord.com/api/webhooks/" + url;
    std::wstring h, p; std::string hint;
    if (!parse_url(url, h, p, hint)) return "{\"error\":\"Only https://discord.com/api/webhooks/<id>/<token> is accepted, exactly as Discord copies it.\"}";
    auto sealed = crypto::ProtectForCurrentUser(url);
    if (sealed.empty()) return "{\"error\":\"Could not seal the URL for this Windows account.\"}";
    auto path = store_path();
    if (path.empty()) return "{\"error\":\"No settings directory.\"}";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return "{\"error\":\"Could not write the settings file.\"}";
    f.write(reinterpret_cast<const char*>(sealed.data()), (std::streamsize)sealed.size());
    f.close();
    load_locked();
    return std::string("{\"configured\":true,\"hint\":\"") + json_escape(g_hint) + "\"}";
}

// Printable text only, no control characters, capped; Discord's hard limit is 2000.
std::string clean_text(const std::string& in, std::size_t cap) {
    std::string out; out.reserve(in.size());
    for (unsigned char c : in) {
        if (c == '\n' || c == '\t') { out.push_back(c == '\t' ? ' ' : '\n'); continue; }
        if (c < 0x20 || c == 0x7f) continue;
        out.push_back((char)c);
    }
    if (out.size() > cap) {
        out.resize(cap);
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back();   // do not cut a UTF-8 sequence
        out += "\xe2\x80\xa6";
    }
    return out;
}

bool take_token(double& tokens, long long& ts, double cap, double perSec, long long now) {
    if (ts) tokens = std::min(cap, tokens + (double)(now - ts) / 1000.0 * perSec);
    ts = now;
    if (tokens < 1.0) return false;
    tokens -= 1.0;
    return true;
}

std::string iso_now() {
    SYSTEMTIME st; GetSystemTime(&st);
    char b[40];
    std::snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

std::string send(const std::string& text, const std::string& source,
                 const std::string& title, const std::string& character, const std::string& world) {
    std::wstring host, path;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        load_locked();
        if (g_path.empty()) return "{\"error\":\"not configured\"}";
        const long long now = now_ms();
        if (now < g_cooldownUntil) return "{\"error\":\"rate limited\"}";
        auto& sb = g_srcBuckets[source];
        if (sb.second == 0) sb.first = 3.0;
        if (!take_token(g_globalTokens, g_globalTs, 5.0, 1.0 / 3.0, now)) return "{\"error\":\"rate limited\"}";
        if (!take_token(sb.first, sb.second, 3.0, 1.0 / 10.0, now)) return "{\"error\":\"rate limited\"}";
        host = g_host; path = g_path;
    }
    // One embed per message: title, the alert text as the body, character and world as fields.
    const bool plugin = source.rfind("plugin:", 0) == 0;
    std::string t = plugin ? "Plugin: " + clean_text(source.substr(7), 40)
                  : !title.empty() ? clean_text(title, 120) : std::string("Alert");
    std::string fields;
    if (!character.empty()) fields += "{\"name\":\"Character\",\"value\":\"" + json_escape(clean_text(character, 40)) + "\",\"inline\":true}";
    if (!world.empty()) fields += std::string(fields.empty() ? "" : ",") + "{\"name\":\"World\",\"value\":\"" + json_escape(clean_text(world, 12)) + "\",\"inline\":true}";
    std::string body = "{\"username\":\"RuneToolsX\",\"allowed_mentions\":{\"parse\":[]},\"embeds\":[{"
                       "\"title\":\"" + json_escape(t) + "\","
                       "\"description\":\"" + json_escape(clean_text(text, 1500)) + "\","
                       "\"color\":13213735,"                                    // brass, matches the launcher accent
                       "\"fields\":[" + fields + "],"
                       "\"footer\":{\"text\":\"RuneToolsX" + std::string(plugin ? " plugin" : "") + "\"},"
                       "\"timestamp\":\"" + iso_now() + "\"}]}";
    http::Enqueue([host, path, body] {
        auto r = http::PostJson(host, path, { { "User-Agent", "RuneToolsX" } }, body);
        if (r.status == 429) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_cooldownUntil = now_ms() + 15000;
            rtx::log::Launcher("discord: rate limited by Discord, pausing 15s");
        } else if (r.status < 200 || r.status >= 300) {
            rtx::log::Launcher("discord: send failed, HTTP " + std::to_string(r.status));
        }
    });
    return "{\"queued\":true}";
}

bool copy_to_clipboard() {
    std::wstring host, path;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        load_locked();
        if (g_path.empty()) return false;
        host = g_host; path = g_path;
    }
    std::string url = "https://" + std::string(host.begin(), host.end()) + std::string(path.begin(), path.end());
    return clipboard_set_text(url);
}

}  // namespace discord

JSValueRef DiscordWebhookCopy(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    return JSValueMakeBoolean(ctx, discord::copy_to_clipboard());
}

JSValueRef DiscordWebhookGet(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, discord::status_json());
}
JSValueRef DiscordWebhookSet(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::string url = argc >= 1 ? js_to_utf8(ctx, argv[0]) : std::string();
    while (!url.empty() && (url.back() == ' ' || url.back() == '\r' || url.back() == '\n')) url.pop_back();
    while (!url.empty() && url.front() == ' ') url.erase(url.begin());
    return utf8_to_js(ctx, discord::set_url(url));
}
JSValueRef DiscordNotify(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::string text   = argc >= 1 ? js_to_utf8(ctx, argv[0]) : std::string();
    std::string source = argc >= 2 ? js_to_utf8(ctx, argv[1]) : std::string("alerts");
    std::string title  = argc >= 3 ? js_to_utf8(ctx, argv[2]) : std::string();
    std::string who    = argc >= 4 ? js_to_utf8(ctx, argv[3]) : std::string();
    std::string world  = argc >= 5 ? js_to_utf8(ctx, argv[4]) : std::string();
    if (source.size() > 80) source.resize(80);
    if (text.empty()) return utf8_to_js(ctx, "{\"error\":\"empty\"}");
    return utf8_to_js(ctx, discord::send(text, source, title, who, world));
}

JSValueRef NotifyWindows(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    std::string title = js_to_utf8(ctx, argv[0]);
    std::string body  = (argc >= 2) ? js_to_utf8(ctx, argv[1]) : std::string();
    return JSValueMakeBoolean(ctx, rtx::winnotify::Show(title, body));
}

std::filesystem::path alerts_user_dir() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) == 0) return {};
    return std::filesystem::path(up) / L"RuneToolsX";
}

std::filesystem::path alerts_cfg_path(std::uint32_t pid) {
    if (!pid) return {};
    std::string acct = account_key_for(pid);
    if (acct.empty()) return {};
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    dir /= L"alerts";
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    return dir / (acct + ".json");
}

std::string alerts_read_file(const std::filesystem::path& p) {
    if (p.empty()) return {};
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::filesystem::path account_store_path(std::uint32_t pid, const wchar_t* sub) {
    if (!pid) return {};
    std::string acct = account_key_for(pid);
    if (acct.empty()) return {};
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    dir /= sub;
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    return dir / (acct + ".json");
}

JSValueRef AlertsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(alerts_cfg_path(pid));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef AlertsSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = alerts_cfg_path(pid);
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef GoalsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"goals"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef GoalsSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"goals");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef LayoutLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"layout"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef LayoutSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"layout");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    auto tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return JSValueMakeBoolean(ctx, false);
        f.write(s.data(), (std::streamsize)s.size());
        if (!f.good()) return JSValueMakeBoolean(ctx, false);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return JSValueMakeBoolean(ctx, !ec);
}

std::filesystem::path cachex_path(const std::string& name) {
    std::string safe;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) safe += c;
    }
    if (safe.empty() || safe.size() > 32) return {};
    auto dir = runetools_dir() / L"cachex";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / (std::wstring(safe.begin(), safe.end()) + L".json");
}

JSValueRef CacheStoreLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    auto p = cachex_path(js_to_utf8(ctx, argv[0]));
    if (p.empty()) return utf8_to_js(ctx, "");
    std::ifstream f(p, std::ios::binary);
    if (!f) return utf8_to_js(ctx, "");
    std::stringstream ss; ss << f.rdbuf();
    return utf8_to_js(ctx, ss.str());
}

JSValueRef CacheStoreSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto p = cachex_path(js_to_utf8(ctx, argv[0]));
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string body = js_to_utf8(ctx, argv[1]);
    if (body.empty()) {
        std::error_code ec; std::filesystem::remove(p, ec);
        return JSValueMakeBoolean(ctx, true);
    }
    auto tmp = p; tmp += L".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f) return JSValueMakeBoolean(ctx, false);
      f.write(body.data(), (std::streamsize)body.size());
      if (!f.good()) return JSValueMakeBoolean(ctx, false); }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return JSValueMakeBoolean(ctx, !ec);
}

JSValueRef NotesLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"notes"));
    return utf8_to_js(ctx, s.empty() ? std::string("[]") : s);
}

JSValueRef PluginGrantsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"plugin-grants"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef PluginGrantsSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"plugin-grants");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef NotesSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"notes");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef NameplatesLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"nameplates"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef NameplatesSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"nameplates");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef MystLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"mysteries"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef MystSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"mysteries");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef QuestLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"questguides"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef QuestSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"questguides");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef Cs2Status(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, cs2browser::StatusJson());
}

JSValueRef Cs2Extract(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, cs2browser::StartExtract());
}

JSValueRef Cs2Cancel(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, cs2browser::Cancel());
}

JSValueRef Cs2Search(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string("{\"err\":\"no query\"}"));
    std::string q = js_to_utf8(ctx, argv[0]);
    int maxr = (argc >= 2) ? js_int(ctx, argv[1]) : 300;
    return utf8_to_js(ctx, cs2browser::SearchJson(q, maxr));
}

JSValueRef Cs2Script(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string("{\"err\":\"no id\"}"));
    int id = js_int(ctx, argv[0]);
    size_t off = (argc >= 2) ? (size_t)JSValueToNumber(ctx, argv[1], nullptr) : 0;
    return utf8_to_js(ctx, cs2browser::ScriptJson(id, off));
}

JSValueRef Cs2Names(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, cs2browser::NamesJson());
}

JSValueRef Cs2Switches(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, cs2browser::SwitchesJson());
}

JSValueRef CounterLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"counter"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef CounterSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"counter");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef SidebarLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"sidebar"));
    return utf8_to_js(ctx, s);
}

JSValueRef KeepFocusedLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"keepfocus"));
    return utf8_to_js(ctx, s);
}
// Borderless fullscreen remembered per account: "1" means every future launch of that
// character opens fullscreen once the client is embedded.
JSValueRef FullscreenPrefLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                              size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"fullscreen"));
    return utf8_to_js(ctx, s);
}
JSValueRef FullscreenPrefSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                              size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    bool on  = JSValueToBoolean(ctx, argv[1]);
    auto p = account_store_path(pid, L"fullscreen");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f) f << (on ? "1" : "0");
    return JSValueMakeBoolean(ctx, f.good());
}
JSValueRef KeepFocusedSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    bool on  = JSValueToBoolean(ctx, argv[1]);
    auto p = account_store_path(pid, L"keepfocus");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f) f << (on ? "1" : "0");
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef SidebarSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    auto p = account_store_path(pid, L"sidebar");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[1]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

std::filesystem::path launcher_cfg_path(const wchar_t* file) {
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    return dir / file;
}
JSValueRef HiddenPanelsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t, const JSValueRef[], JSValueRef*) {
    std::string s = alerts_read_file(launcher_cfg_path(L"panels.json"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}
JSValueRef HiddenPanelsSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto p = launcher_cfg_path(L"panels.json");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[0]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef VarPinsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    std::string s = alerts_read_file(launcher_cfg_path(L"varpins.json"));
    return utf8_to_js(ctx, s.empty() ? std::string("[]") : s);
}
namespace {

JSValueRef LauncherMetaLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t, const JSValueRef[], JSValueRef*) {
    std::string s = alerts_read_file(launcher_cfg_path(L"launcher-meta.json"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef LauncherMetaSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto p = launcher_cfg_path(L"launcher-meta.json");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[0]);
    auto tmp = p; tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return JSValueMakeBoolean(ctx, false);
        f.write(s.data(), (std::streamsize)s.size());
        if (!f.good()) return JSValueMakeBoolean(ctx, false);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) { std::filesystem::remove(p, ec); std::filesystem::rename(tmp, p, ec); }
    return JSValueMakeBoolean(ctx, !ec);
}

JSValueRef WinCmd(JSContextRef ctx, JSObjectRef, JSObjectRef,
                  size_t argc, const JSValueRef argv[], JSValueRef*) {
    HWND h = reinterpret_cast<HWND>(g_launcherHwnd);
    if (!h || !IsWindow(h) || argc < 1) return JSValueMakeBoolean(ctx, false);
    std::string cmd = js_to_utf8(ctx, argv[0]);
    if (cmd == "drag") {
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        POINT pt{};
        if (GetCursorPos(&pt) && ScreenToClient(h, &pt)) {
            if (pt.x < 0) pt.x = 0;
            if (pt.y < 0) pt.y = 0;
            PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM((WORD)pt.x, (WORD)pt.y));
        }
    }
    else if (cmd == "min")  { ShowWindow(h, SW_MINIMIZE); }
    else if (cmd == "close"){ PostMessageW(h, WM_CLOSE, 0, 0); }
    else return JSValueMakeBoolean(ctx, false);
    return JSValueMakeBoolean(ctx, true);
}

}  // namespace

JSValueRef PrefsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    std::string s = alerts_read_file(launcher_cfg_path(L"prefs.json"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}

JSValueRef PrefsSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto p = launcher_cfg_path(L"prefs.json");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[0]);
    auto tmp = p; tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return JSValueMakeBoolean(ctx, false);
        f.write(s.data(), (std::streamsize)s.size());
        if (!f.good()) return JSValueMakeBoolean(ctx, false);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) { std::filesystem::remove(p, ec); std::filesystem::rename(tmp, p, ec); }
    return JSValueMakeBoolean(ctx, !ec);
}

JSValueRef VarPinsSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto p = launcher_cfg_path(L"varpins.json");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[0]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

// kind 0 item / 1 loc / 2 npc.
JSValueRef MenuDef(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    int kind = js_int(ctx, argv[0]);
    int id   = js_int(ctx, argv[1]);
    return utf8_to_js(ctx, rtx::cache::MenuDefJson(kind, id));
}
JSValueRef MenuFind(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{\"ids\":[]}");
    int kind = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::MenuFindJson(kind, js_to_utf8(ctx, argv[1])));
}

JSValueRef MenuSearch(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{\"hits\":[]}");
    int kind  = js_int(ctx, argv[0]);
    int limit = argc > 2 ? js_int(ctx, argv[2]) : 60;
    return utf8_to_js(ctx, rtx::cache::MenuSearchJson(kind, js_to_utf8(ctx, argv[1]), limit));
}

JSValueRef MenuRulesLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    std::string s = alerts_read_file(launcher_cfg_path(L"menurules.json"));
    return utf8_to_js(ctx, s.empty() ? std::string("{}") : s);
}
JSValueRef MenuRulesSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto p = launcher_cfg_path(L"menurules.json");
    if (p.empty()) return JSValueMakeBoolean(ctx, false);
    std::string s = js_to_utf8(ctx, argv[0]);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(s.data(), (std::streamsize)s.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef FlashGame(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    rtx::overlay::Flash(pid);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef Metronome(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool visual = (argc >= 2) && JSValueToBoolean(ctx, argv[1]);
    bool audio  = (argc >= 3) && JSValueToBoolean(ctx, argv[2]);
    int  interval = (argc >= 4) ? js_int(ctx, argv[3]) : 1;
    bool locked = (argc >= 5) && JSValueToBoolean(ctx, argv[4]);
    rtx::overlay::Metronome(pid, visual, audio, interval, locked);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef XpPanelFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    bool visible = (argc >= 2) && JSValueToBoolean(ctx, argv[1]);
    bool locked  = (argc >= 3) && JSValueToBoolean(ctx, argv[2]);
    bool total   = (argc < 4) || JSValueToBoolean(ctx, argv[3]);
    bool autoSk  = (argc < 5) || JSValueToBoolean(ctx, argv[4]);
    auto mask    = (argc >= 6) ? (std::uint32_t)JSValueToNumber(ctx, argv[5], nullptr) : 0u;
    rtx::overlay::XpPanel(pid, visible, locked, total, autoSk, mask);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef XpPanelResetFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    rtx::overlay::XpPanelReset(pid);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef XpPanelStateFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    return utf8_to_js(ctx, rtx::overlay::XpPanelStateJson(pid));
}

// Records separated by \x1e, fields by \x1f: gx, gy, plane, label [, snap, rgb, gx2, gy2, region, rgb2].
// rgb is decimal 0xRRGGBB (0 = default); region 1 merges same-label marks into one zone. Empty clears.
JSValueRef GuideMarksFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string s = js_to_utf8(ctx, argv[1]);
    std::vector<rtx::overlay::GuideMark> v;
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t end = s.find('\x1e', pos);
        if (end == std::string::npos) end = s.size();
        std::string rec = s.substr(pos, end - pos);
        pos = end + 1;
        std::size_t a = rec.find('\x1f');
        std::size_t b = (a == std::string::npos) ? std::string::npos : rec.find('\x1f', a + 1);
        std::size_t c = (b == std::string::npos) ? std::string::npos : rec.find('\x1f', b + 1);
        if (c == std::string::npos) continue;
        std::size_t d = rec.find('\x1f', c + 1);
        rtx::overlay::GuideMark gmk;
        gmk.gx    = std::atoi(rec.substr(0, a).c_str());
        gmk.gy    = std::atoi(rec.substr(a + 1, b - a - 1).c_str());
        gmk.plane = std::atoi(rec.substr(b + 1, c - b - 1).c_str());
        if (d == std::string::npos) {
            gmk.label = rec.substr(c + 1);
        } else {
            gmk.label = rec.substr(c + 1, d - c - 1);
            int extra[6] = { 0, 0, 0, 0, 0, 0 };   // snap, rgb, gx2, gy2, region, rgb2
            std::size_t p2 = d;
            for (int i = 0; i < 6 && p2 != std::string::npos; ++i) {
                std::size_t n2 = rec.find('\x1f', p2 + 1);
                extra[i] = std::atoi(rec.substr(p2 + 1, (n2 == std::string::npos ? rec.size() : n2) - p2 - 1).c_str());
                p2 = n2;
            }
            gmk.snapObj = extra[0] != 0;
            gmk.rgb = extra[1]; gmk.gx2 = extra[2]; gmk.gy2 = extra[3]; gmk.region = extra[4]; gmk.rgb2 = extra[5];
        }
        if (gmk.gx > 0 && gmk.gy > 0) v.push_back(std::move(gmk));
    }
    rtx::overlay::SetGuideMarks(pid, std::move(v));
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef CenterTextFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int slot = (argc >= 3) ? js_int(ctx, argv[2]) : 0;
    int rgb  = (argc >= 4) ? js_int(ctx, argv[3]) : -1;
    rtx::overlay::SetCenterText(pid, js_to_utf8(ctx, argv[1]), slot, rgb);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UiHighlightFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    rtx::overlay::UiHighlight hl{};
    if (argc >= 5) {
        hl.x = js_int(ctx, argv[1]);
        hl.y = js_int(ctx, argv[2]);
        hl.w = js_int(ctx, argv[3]);
        hl.h = js_int(ctx, argv[4]);
    }
    rtx::overlay::SetUiHighlight(pid, hl);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UiLabelsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string s = (argc >= 2) ? js_to_utf8(ctx, argv[1]) : std::string();
    std::vector<rtx::overlay::UiLabel> v;
    std::size_t pos = 0;
    while (pos < s.size() && v.size() < 32) {
        std::size_t end = s.find('\x1e', pos);
        if (end == std::string::npos) end = s.size();
        std::string rec = s.substr(pos, end - pos);
        pos = end + 1;
        std::vector<std::string> f;
        std::size_t at = 0;
        while (f.size() < 6) {
            std::size_t sep = rec.find('\x1f', at);
            if (sep == std::string::npos) { f.push_back(rec.substr(at)); break; }
            f.push_back(rec.substr(at, sep - at)); at = sep + 1;
        }
        if (f.size() < 5 || f[4].empty()) continue;
        rtx::overlay::UiLabel lb;
        lb.x = std::atoi(f[0].c_str()); lb.y = std::atoi(f[1].c_str());
        lb.rgb = std::atoi(f[2].c_str()); lb.px = std::atoi(f[3].c_str());
        lb.style = f.size() >= 6 ? std::atoi(f[5].c_str()) : 0;   // optional sixth field: 1 = pill
        if (lb.px < 8) lb.px = 8; if (lb.px > 40) lb.px = 40;
        lb.text = f[4].substr(0, 90);
        v.push_back(lb);
    }
    rtx::overlay::SetUiLabels(pid, v);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UiHighlightsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string s = (argc >= 2) ? js_to_utf8(ctx, argv[1]) : std::string();
    std::vector<rtx::overlay::UiHighlight> v;
    std::size_t pos = 0;
    while (pos < s.size() && v.size() < 64) {
        std::size_t end = s.find(';', pos);
        if (end == std::string::npos) end = s.size();
        std::string rec = s.substr(pos, end - pos);
        pos = end + 1;
        int f[4] = { 0, 0, 0, 0 };
        std::size_t at = 0;
        int n = 0;
        while (n < 4 && at <= rec.size()) {
            std::size_t comma = rec.find(',', at);
            if (comma == std::string::npos) comma = rec.size();
            f[n++] = std::atoi(rec.substr(at, comma - at).c_str());
            if (comma >= rec.size()) break;
            at = comma + 1;
        }
        if (n < 4 || f[2] <= 0 || f[3] <= 0) continue;
        rtx::overlay::UiHighlight hl{};
        hl.x = f[0]; hl.y = f[1]; hl.w = f[2]; hl.h = f[3];
        v.push_back(hl);
    }
    rtx::overlay::SetUiHighlights(pid, v);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef PanelRectsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "[]");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, rtx::reader::PanelRectsJson(pid));
}

JSValueRef InvSlotRectFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int idx = js_int(ctx, argv[1]);
    return utf8_to_js(ctx, rtx::reader::InvSlotRectJson(pid, idx));
}

JSValueRef PanelVizFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::vector<rtx::overlay::PanelBox> boxes;
    if (argc >= 2) {
        std::string s = js_to_utf8(ctx, argv[1]);
        std::size_t pos = 0;
        while (pos < s.size()) {
            std::size_t bar = s.find('|', pos);
            std::string seg = s.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
            pos = (bar == std::string::npos) ? s.size() : bar + 1;
            if (seg.empty()) continue;
            int v[4] = {0,0,0,0}; std::size_t fp = 0; int fi = 0;
            for (; fi < 4; ++fi) {
                std::size_t comma = seg.find(',', fp);
                if (comma == std::string::npos) break;
                v[fi] = std::atoi(seg.substr(fp, comma - fp).c_str());
                fp = comma + 1;
            }
            if (fi < 4) continue;
            rtx::overlay::PanelBox b{}; b.x = v[0]; b.y = v[1]; b.w = v[2]; b.h = v[3];
            b.label = seg.substr(fp);
            boxes.push_back(std::move(b));
        }
    }
    rtx::overlay::SetPanelViz(pid, boxes);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef PuzzleCellsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::vector<rtx::overlay::PuzzleCell> cells;
    if (argc >= 2) {
        std::string s = js_to_utf8(ctx, argv[1]);
        std::size_t pos = 0;
        while (pos < s.size()) {
            std::size_t semi = s.find(';', pos);
            std::string seg = s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            pos = (semi == std::string::npos) ? s.size() : semi + 1;
            if (seg.empty()) continue;
            int v[6] = {0,0,0,0,0,-1}; std::size_t fp = 0; int fi = 0;   // v[5] = optional label number, -1 = step+1
            for (; fi < 6; ++fi) {
                std::size_t comma = seg.find(',', fp);
                std::string tok = seg.substr(fp, comma == std::string::npos ? std::string::npos : comma - fp);
                v[fi] = std::atoi(tok.c_str());
                if (comma == std::string::npos) { ++fi; break; }
                fp = comma + 1;
            }
            if (fi < 5) continue;
            rtx::overlay::PuzzleCell pc{}; pc.x = v[0]; pc.y = v[1]; pc.w = v[2]; pc.h = v[3]; pc.step = v[4]; pc.num = (fi >= 6) ? v[5] : -1;
            cells.push_back(pc);
        }
    }
    rtx::overlay::SetPuzzleCells(pid, cells);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef SkillBarsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::vector<rtx::overlay::SkillBar> bars;
    if (argc >= 2) {
        std::string s = js_to_utf8(ctx, argv[1]);
        std::size_t pos = 0;
        while (pos < s.size() && bars.size() < 64) {
            std::size_t semi = s.find(';', pos);
            std::string seg = s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            pos = (semi == std::string::npos) ? s.size() : semi + 1;
            if (seg.empty()) continue;
            int v[6] = {0,0,0,0,0,0}; std::size_t fp = 0; int fi = 0;
            for (; fi < 6; ++fi) {
                std::size_t comma = seg.find(',', fp);
                std::string tok = seg.substr(fp, comma == std::string::npos ? std::string::npos : comma - fp);
                v[fi] = std::atoi(tok.c_str());
                if (comma == std::string::npos) { ++fi; break; }
                fp = comma + 1;
            }
            if (fi < 5) continue;
            rtx::overlay::SkillBar sb{};
            sb.x = v[0]; sb.y = v[1]; sb.w = v[2]; sb.h = v[3]; sb.pct = v[4]; sb.rgb = v[5];
            bars.push_back(sb);
        }
    }
    rtx::overlay::SetSkillBars(pid, bars);
    return JSValueMakeBoolean(ctx, !bars.empty());
}

JSValueRef KnotCellsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::vector<rtx::overlay::KnotCell> cells;
    if (argc >= 2) {
        std::string s = js_to_utf8(ctx, argv[1]);
        std::size_t pos = 0;
        while (pos < s.size()) {
            std::size_t semi = s.find(';', pos);
            std::string seg = s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            pos = (semi == std::string::npos) ? s.size() : semi + 1;
            if (seg.empty()) continue;
            int v[5] = {0,0,0,0,0}; std::size_t fp = 0; int fi = 0;
            for (; fi < 5; ++fi) {
                std::size_t comma = seg.find(',', fp);
                std::string tok = seg.substr(fp, comma == std::string::npos ? std::string::npos : comma - fp);
                v[fi] = std::atoi(tok.c_str());
                if (comma == std::string::npos) { ++fi; break; }
                fp = comma + 1;
            }
            if (fi < 5) continue;
            rtx::overlay::KnotCell kc{}; kc.x = v[0]; kc.y = v[1]; kc.w = v[2]; kc.h = v[3]; kc.count = v[4];
            cells.push_back(kc);
        }
    }
    rtx::overlay::SetKnotCells(pid, cells);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef OverlayToast(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    rtx::overlay::Toast(pid, js_to_utf8(ctx, argv[1]));
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef OverlayNotify(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    long long ttl = (argc >= 3) ? (long long)JSValueToNumber(ctx, argv[2], nullptr) : 0;
    rtx::overlay::Notify(pid, js_to_utf8(ctx, argv[1]), ttl);
    return JSValueMakeBoolean(ctx, true);
}

void install_fn(JSContextRef ctx, JSObjectRef obj, const char* name,
                JSObjectCallAsFunctionCallback fn) {
    JSStringRef key = JSStringCreateWithUTF8CString(name);
    JSObjectRef f = JSObjectMakeFunctionWithCallback(ctx, key, fn);
    JSObjectSetProperty(ctx, obj, key, f,
                        kJSPropertyAttributeDontDelete | kJSPropertyAttributeDontEnum,
                        nullptr);
    JSStringRelease(key);
}


std::string sanitize_plugin_id(const std::string& id) {
    std::string out;
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') out.push_back(c);
    }
    if (out.empty() || out.front() == '.') return {};
    if (out.find("..") != std::string::npos) return {};
    return out;
}

std::string sanitize_plugin_key(const std::string& key) {
    std::string out;
    for (char c : key) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out.push_back(c);
    }
    return out;
}

std::filesystem::path plugin_store_dir(std::uint32_t pid, const std::string& pluginId) {
    if (!pid) return {};
    std::string id = sanitize_plugin_id(pluginId);
    if (id.empty()) return {};
    std::string acct = account_key_for(pid);
    if (acct.empty()) return {};
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    dir /= L"plugin-data"; dir /= acct; dir /= id;
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path plugin_dev_root() {
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    dir /= L"plugins-dev";
    return dir;
}

constexpr std::size_t kPluginStoreMaxBytes = 256 * 1024;
constexpr std::size_t kPluginEntryMaxBytes = 2 * 1024 * 1024;

JSValueRef PluginStoreLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, std::string());
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    std::string id  = js_to_utf8(ctx, argv[1]);
    std::string key = sanitize_plugin_key(js_to_utf8(ctx, argv[2]));
    if (key.empty()) return utf8_to_js(ctx, std::string());
    auto dir = plugin_store_dir(pid, id);
    if (dir.empty()) return utf8_to_js(ctx, std::string());
    return utf8_to_js(ctx, alerts_read_file(dir / (key + ".json")));
}

JSValueRef PluginStoreSave(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 4) return JSValueMakeBoolean(ctx, false);
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    std::string id  = js_to_utf8(ctx, argv[1]);
    std::string key = sanitize_plugin_key(js_to_utf8(ctx, argv[2]));
    if (key.empty()) return JSValueMakeBoolean(ctx, false);
    std::string val = js_to_utf8(ctx, argv[3]);
    if (val.size() > kPluginStoreMaxBytes) return JSValueMakeBoolean(ctx, false);
    auto dir = plugin_store_dir(pid, id);
    if (dir.empty()) return JSValueMakeBoolean(ctx, false);
    std::ofstream f(dir / (key + ".json"), std::ios::binary | std::ios::trunc);
    if (!f) return JSValueMakeBoolean(ctx, false);
    f.write(val.data(), (std::streamsize)val.size());
    return JSValueMakeBoolean(ctx, f.good());
}

JSValueRef PluginStoreKeys(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, std::string("[]"));
    std::uint32_t pid = (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr);
    std::string id = js_to_utf8(ctx, argv[1]);
    auto dir = plugin_store_dir(pid, id);
    std::string out = "[";
    if (!dir.empty()) {
        std::error_code ec; bool first = true;
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != L".json") continue;
            std::string stem = sanitize_plugin_key(e.path().stem().string());
            if (stem.empty()) continue;
            if (!first) out += ",";
            out += "\""; out += stem; out += "\""; first = false;
        }
    }
    out += "]";
    return utf8_to_js(ctx, out);
}

JSValueRef PluginDevList(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    auto root = plugin_dev_root();
    std::string out = "[";
    if (!root.empty()) {
        std::error_code ec; bool first = true;
        for (auto& e : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            if (!e.is_directory()) continue;
            std::string id = sanitize_plugin_id(e.path().filename().string());
            if (id.empty()) continue;
            std::error_code ec2;
            if (!std::filesystem::exists(e.path() / "manifest.json", ec2)) continue;
            if (!first) out += ",";
            out += "\""; out += id; out += "\""; first = false;
        }
    }
    out += "]";
    return utf8_to_js(ctx, out);
}

JSValueRef PluginDevManifest(JSContextRef ctx, JSObjectRef, JSObjectRef,
                             size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string());
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    auto root = plugin_dev_root();
    if (id.empty() || root.empty()) return utf8_to_js(ctx, std::string());
    return utf8_to_js(ctx, alerts_read_file(root / id / "manifest.json"));
}

JSValueRef PluginDevStamp(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    auto root = plugin_dev_root();
    std::string out;
    if (!root.empty()) {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            if (!e.is_directory()) continue;
            std::string id = sanitize_plugin_id(e.path().filename().string());
            if (id.empty()) continue;
            std::error_code ec2;
            if (!std::filesystem::exists(e.path() / "manifest.json", ec2)) continue;
            long long maxT = 0; int n = 0;
            std::error_code itEc;
            for (auto& f : std::filesystem::recursive_directory_iterator(e.path(), itEc)) {
                if (itEc) break;
                if (++n > 512) break;
                std::error_code fec;
                if (!f.is_regular_file(fec) || fec) continue;
                auto t = std::filesystem::last_write_time(f.path(), fec);
                if (fec) continue;
                long long v = (long long)t.time_since_epoch().count();
                if (v > maxT) maxT = v;
            }
            out += id; out += ':'; out += std::to_string(n); out += ':';
            out += std::to_string(maxT); out += ';';
        }
    }
    return utf8_to_js(ctx, out);
}

JSValueRef PluginDevEntry(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, std::string());
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    std::string entry = js_to_utf8(ctx, argv[1]);
    auto root = plugin_dev_root();
    if (id.empty() || root.empty() || entry.empty()) return utf8_to_js(ctx, std::string());
    if (entry.find('/') != std::string::npos || entry.find('\\') != std::string::npos ||
        entry.find("..") != std::string::npos) return utf8_to_js(ctx, std::string());
    std::string s = alerts_read_file(root / id / entry);
    if (s.size() > kPluginEntryMaxBytes) return utf8_to_js(ctx, std::string());
    return utf8_to_js(ctx, s);
}


constexpr wchar_t kPluginListPath[] = L"/api/plugins/client/list";

}  // namespace
// Pinned ECDSA P-256 public key (raw X||Y); signs plugin bundles and the update manifest.
const unsigned char kPluginPubKey[64] = {
    0x7e, 0x24, 0xd5, 0xaa, 0xd0, 0x72, 0x29, 0xf2, 0x11, 0xbf, 0x5a, 0x75,
    0x3b, 0x5a, 0xf0, 0xe7, 0xe0, 0xd8, 0xdf, 0xb7, 0x7a, 0x8b, 0x19, 0xe4,
    0x17, 0xe8, 0x59, 0x25, 0xdf, 0x44, 0x53, 0x7c, 0x65, 0xa1, 0xe0, 0x0f,
    0x39, 0xf3, 0x67, 0xd8, 0xa5, 0x66, 0x19, 0xb1, 0x43, 0xf4, 0x1d, 0x6d,
    0x4a, 0x03, 0x5a, 0x6f, 0x12, 0x00, 0xa3, 0xf3, 0x3f, 0xfe, 0xa5, 0xb1,
    0x0f, 0x30, 0x09, 0xd5,
};

std::filesystem::path plugin_install_root() {
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    dir /= L"plugins";
    return dir;
}

bool plugin_b64_decode(const std::string& in, std::vector<std::uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((std::uint8_t)((buf >> bits) & 0xff)); }
    }
    return true;
}
namespace {

std::string sha256_hex_buf(const std::uint8_t* d, std::size_t n) {
    std::uint8_t dig[32];
    if (!crypto::Sha256(d, n, dig)) return {};
    static const char* hx = "0123456789abcdef";
    std::string o;
    for (auto b : dig) { o.push_back(hx[b >> 4]); o.push_back(hx[b & 0xf]); }
    return o;
}

// ---- Lua bundles: every allowed file in the zip is written under plugins\<id>\ (the runtime
// reads modules and data files from disk; the JavaScript path only ever needed the entry HTML).
bool lua_bundle_path_ok(const std::string& n) {
    if (n.empty() || n.size() > 200 || n[0] == '/' || n.find("..") != std::string::npos ||
        n.find('\\') != std::string::npos || n.find(':') != std::string::npos) return false;
    int segs = 1;
    for (char c : n) { if ((unsigned char)c < 0x20) return false; if (c == '/') ++segs; }
    if (segs > 8) return false;
    auto dot = n.rfind('.');
    if (dot == std::string::npos || n.rfind('/') != std::string::npos && n.rfind('/') > dot) return false;
    std::string ext = n.substr(dot);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    static const char* kOk[] = {".lua", ".json", ".txt", ".md", ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".wav"};
    for (auto e : kOk) if (ext == e) return true;
    return false;
}

std::string install_lua_bundle(const std::uint8_t* body, std::size_t blen, const std::string& manStr, const std::string& id) {
    std::string mainFile = json_str(manStr, "main");
    if (mainFile.empty()) mainFile = "main.lua";
    if (mainFile.find('/') != std::string::npos || mainFile.find('\\') != std::string::npos ||
        mainFile.find("..") != std::string::npos || mainFile.size() < 5 ||
        mainFile.compare(mainFile.size() - 4, 4, ".lua") != 0) return "Invalid main filename";
    std::vector<std::string> names;
    if (!zip::ListFiles(body, blen, names)) return "Bundle unreadable";
    if (names.size() > 200) return "Bundle has too many files";
    std::vector<std::pair<std::string, std::string>> files;
    std::size_t total = 0; bool haveMain = false, haveManifest = false;
    for (auto& n : names) {
        if (!lua_bundle_path_ok(n)) return "Disallowed file in bundle: " + n;
        std::string data;
        if (!zip::ExtractFile(body, blen, n, data)) return "Bundle entry unreadable: " + n;
        if (data.size() > kPluginEntryMaxBytes) return "Bundle file too large: " + n;
        total += data.size();
        if (total > 5 * 1024 * 1024) return "Bundle too large";
        if (n == mainFile) haveMain = true;
        if (n == "manifest.json") haveManifest = true;
        files.emplace_back(n, std::move(data));
    }
    if (!haveMain) return "Bundle missing " + mainFile;
    if (!haveManifest) return "Bundle missing manifest.json";
    auto root = plugin_install_root();
    if (root.empty()) return "No install location";
    auto dir = root / id;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ec.clear();
    std::filesystem::create_directories(dir, ec);
    for (auto& [n, data] : files) {
        auto path = dir / std::filesystem::path(n);
        std::error_code pec; std::filesystem::create_directories(path.parent_path(), pec);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return "Write failed: " + n;
        f.write(data.data(), (std::streamsize)data.size());
    }
    return {};
}

std::string install_plugin(const std::string& slug) {
    std::string id = sanitize_plugin_id(slug);
    if (id.empty()) return "Invalid plugin id";

    std::wstring path = L"/api/plugins/" + std::wstring(id.begin(), id.end()) + L"/download";
    auto r = http::Fetch(kUpdateHost, path, {});
    if (!r.ok || r.status != 200) return "Download failed (" + std::to_string(r.status) + ")";

    const std::uint8_t* body = (const std::uint8_t*)r.body.data();
    std::size_t blen = r.body.size();

    std::string wantHash = r.header("X-Plugin-Hash");
    std::string gotHash  = sha256_hex_buf(body, blen);
    if (wantHash.empty() || gotHash.empty() || _stricmp(wantHash.c_str(), gotHash.c_str()) != 0)
        return "Integrity check failed";

    std::vector<std::uint8_t> sig;
    if (!plugin_b64_decode(r.header("X-Plugin-Signature"), sig) || sig.size() != 64)
        return "Missing/invalid signature";
    if (!crypto::VerifyEcdsaP256(body, blen, sig.data(), sig.size(), kPluginPubKey, sizeof(kPluginPubKey)))
        return "Signature verification failed";

    std::string manStr;
    if (!zip::ExtractFile(body, blen, "manifest.json", manStr) || manStr.empty())
        return "Bundle missing manifest.json";
    if (sanitize_plugin_id(json_str(manStr, "id")) != id) return "Manifest id mismatch";
    if (json_str(manStr, "runtime") == "lua") return install_lua_bundle(body, blen, manStr, id);
    std::string entryName = json_str(manStr, "entry");
    if (entryName.empty()) entryName = "index.html";
    if (entryName.find('/') != std::string::npos || entryName.find('\\') != std::string::npos ||
        entryName.find("..") != std::string::npos) return "Invalid entry filename";
    std::string entryHtml;
    if (!zip::ExtractFile(body, blen, entryName, entryHtml) || entryHtml.empty())
        return "Bundle missing entry file";

    auto root = plugin_install_root();
    if (root.empty()) return "No install location";
    auto dir = root / id;
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    {
        std::ofstream f(dir / "manifest.json", std::ios::binary | std::ios::trunc);
        if (!f) return "Write failed";
        f.write(manStr.data(), (std::streamsize)manStr.size());
    }
    {
        std::ofstream f(dir / entryName, std::ios::binary | std::ios::trunc);
        if (!f) return "Write failed";
        f.write(entryHtml.data(), (std::streamsize)entryHtml.size());
    }
    return {};
}

std::mutex   g_pluginListMu;
std::string  g_pluginListBody;              // "" = never fetched
std::string  g_pluginListErr;               // "" = none
bool         g_pluginListInFlight = false;
ULONGLONG    g_pluginListAt = 0;            // tick of the last success

JSValueRef PluginMarketList(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t, const JSValueRef[], JSValueRef*) {
    std::string body, err;
    {
        std::lock_guard<std::mutex> lk(g_pluginListMu);
        body = g_pluginListBody;
        err  = g_pluginListErr;
        const ULONGLONG now = GetTickCount64();
        if (!g_pluginListInFlight && (g_pluginListBody.empty() || now - g_pluginListAt > 60000)) {
            g_pluginListInFlight = true;
            http::Enqueue([] {
                auto r = http::Get(kUpdateHost, kPluginListPath, {});
                std::lock_guard<std::mutex> lk(g_pluginListMu);
                if (r.ok && r.status == 200 && !r.body.empty()) {
                    g_pluginListBody = std::move(r.body);
                    g_pluginListAt   = GetTickCount64();
                    g_pluginListErr.clear();
                } else {
                    g_pluginListErr = r.detail.empty()
                        ? ("HTTP " + std::to_string(r.status)) : r.detail;
                }
                g_pluginListInFlight = false;
            });
        }
    }
    if (body.empty() && !err.empty())
        return utf8_to_js(ctx, "{\"error\":\"" + json_escape(err) + "\"}");
    return utf8_to_js(ctx, body.empty() ? std::string("{}") : body);
}

std::mutex   g_installMu;
std::string  g_installResult;               // "" while running, "ok", or error text
bool         g_installInFlight = false;

JSValueRef PluginMarketInstall(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string("Bad request"));
    std::string slug = js_to_utf8(ctx, argv[0]);
    std::lock_guard<std::mutex> lk(g_installMu);
    if (g_installInFlight) return utf8_to_js(ctx, std::string("busy"));
    g_installInFlight = true;
    g_installResult.clear();
    std::thread([slug] {
        std::string err = "install failed";
        guarded("plugin install", [&] { err = install_plugin(slug); });
        std::lock_guard<std::mutex> lk(g_installMu);
        g_installResult = err.empty() ? std::string("ok") : err;
        g_installInFlight = false;
    }).detach();
    return utf8_to_js(ctx, std::string("pending"));
}

JSValueRef PluginInstallStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t, const JSValueRef[], JSValueRef*) {
    std::lock_guard<std::mutex> lk(g_installMu);
    if (g_installInFlight) return utf8_to_js(ctx, std::string("pending"));
    return utf8_to_js(ctx, g_installResult.empty() ? std::string("idle") : g_installResult);
}

JSValueRef PluginInstalledList(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t, const JSValueRef[], JSValueRef*) {
    auto root = plugin_install_root();
    std::string out = "[";
    if (!root.empty()) {
        std::error_code ec; bool first = true;
        for (auto& e : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            if (!e.is_directory()) continue;
            std::string id = sanitize_plugin_id(e.path().filename().string());
            if (id.empty()) continue;
            std::error_code ec2;
            if (!std::filesystem::exists(e.path() / "manifest.json", ec2)) continue;
            if (!first) out += ","; out += "\""; out += id; out += "\""; first = false;
        }
    }
    out += "]";
    return utf8_to_js(ctx, out);
}

JSValueRef PluginInstalledManifest(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string());
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    auto root = plugin_install_root();
    if (id.empty() || root.empty()) return utf8_to_js(ctx, std::string());
    return utf8_to_js(ctx, alerts_read_file(root / id / "manifest.json"));
}

JSValueRef PluginInstalledEntry(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, std::string());
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    std::string entry = js_to_utf8(ctx, argv[1]);
    auto root = plugin_install_root();
    if (id.empty() || root.empty() || entry.empty()) return utf8_to_js(ctx, std::string());
    if (entry.find('/') != std::string::npos || entry.find('\\') != std::string::npos ||
        entry.find("..") != std::string::npos) return utf8_to_js(ctx, std::string());
    std::string s = alerts_read_file(root / id / entry);
    if (s.size() > kPluginEntryMaxBytes) return utf8_to_js(ctx, std::string());
    return utf8_to_js(ctx, s);
}

JSValueRef PluginUninstallLocal(JSContextRef ctx, JSObjectRef, JSObjectRef,
                                size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    auto root = plugin_install_root();
    if (id.empty() || root.empty()) return JSValueMakeBoolean(ctx, false);
    std::error_code ec; std::filesystem::remove_all(root / id, ec);
    return JSValueMakeBoolean(ctx, !ec);
}

// ---- Lua plugins: JavaScriptCore wrappers over LuaHost (core/rtx-plugin-lua.js drives these) ----
std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

JSValueRef LuaLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 4) return utf8_to_js(ctx, std::string("{\"ok\":false,\"error\":\"bad request\",\"failed\":true,\"log\":[]}"));
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    std::string source = js_to_utf8(ctx, argv[1]);
    auto root = (source == "dev") ? plugin_dev_root() : plugin_install_root();
    if (id.empty() || root.empty()) return utf8_to_js(ctx, std::string("{\"ok\":false,\"error\":\"unknown plugin\",\"failed\":true,\"log\":[]}"));
    rtx::launcher::lua::ScopedContext sc(ctx);
    return utf8_to_js(ctx, rtx::launcher::lua::Load(id, root / id, split_csv(js_to_utf8(ctx, argv[2])), js_to_utf8(ctx, argv[3])));
}

JSValueRef LuaUnload(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    if (id.empty()) return JSValueMakeBoolean(ctx, false);
    rtx::launcher::lua::ScopedContext sc(ctx);
    rtx::launcher::lua::Unload(id);
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef LuaTick(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string("{}"));
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    std::string events = argc >= 2 ? js_to_utf8(ctx, argv[1]) : std::string();
    std::string state  = argc >= 3 ? js_to_utf8(ctx, argv[2]) : std::string();
    rtx::launcher::lua::ScopedContext sc(ctx);
    return utf8_to_js(ctx, rtx::launcher::lua::Tick(id, events, state));
}

JSValueRef LuaEvent(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, std::string("{}"));
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    rtx::launcher::lua::ScopedContext sc(ctx);
    return utf8_to_js(ctx, rtx::launcher::lua::Event(id, js_to_utf8(ctx, argv[1]), js_to_utf8(ctx, argv[2])));
}

JSValueRef LuaUiEvent(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return utf8_to_js(ctx, std::string("{}"));
    std::string id = sanitize_plugin_id(js_to_utf8(ctx, argv[0]));
    rtx::launcher::lua::ScopedContext sc(ctx);
    return utf8_to_js(ctx, rtx::launcher::lua::UiEvent(id, js_to_utf8(ctx, argv[1]), js_to_utf8(ctx, argv[2])));
}

JSValueRef LuaInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, std::string("{\"loaded\":false}"));
    return utf8_to_js(ctx, rtx::launcher::lua::Info(sanitize_plugin_id(js_to_utf8(ctx, argv[0]))));
}

JSValueRef LuaVersion(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, std::string(rtx::launcher::lua::RuntimeVersion()));
}

// launcher.log tail for the Console panel: {"offset":n,"text":"..."} with the bytes after `offset`
// (at most 256 KB per call, starting on a line boundary when trimmed). A file shorter than the
// offset (new session) starts over. The log is already profile-path redacted by rtx::log.
JSValueRef LauncherLogTail(JSContextRef ctx, JSObjectRef, JSObjectRef,
                           size_t argc, const JSValueRef argv[], JSValueRef*) {
    long long off = (argc >= 1) ? (long long)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    if (off < 0 || off > (1LL << 40)) off = 0;
    std::filesystem::path p = std::filesystem::path(rtx::log::LogDir()) / L"launcher.log";
    std::ifstream f(p, std::ios::binary);
    if (!f) return utf8_to_js(ctx, std::string("{\"offset\":0,\"text\":\"\"}"));
    f.seekg(0, std::ios::end);
    long long size = (long long)f.tellg();
    if (size < 0) size = 0;
    if (off > size) off = 0;
    constexpr long long kMax = 256 * 1024;
    bool trimmed = false;
    long long want = size - off;
    if (want > kMax) { off = size - kMax; want = kMax; trimmed = true; }
    std::string buf((std::size_t)want, '\0');
    f.seekg(off);
    f.read(buf.data(), want);
    buf.resize((std::size_t)f.gcount());
    if (trimmed) { auto nl = buf.find('\n'); if (nl != std::string::npos) buf.erase(0, nl + 1); }
    std::string out = "{\"offset\":" + std::to_string(off + (long long)buf.size()) + ",\"size\":" + std::to_string(size) +
                      ",\"text\":\"" + json_escape(buf) + "\"}";
    return utf8_to_js(ctx, out);
}

}  // namespace

int ScreenshotVk() {
    std::lock_guard<std::mutex> lk(g_ss_mu);
    ss_load_locked();
    return g_ss_vk;
}
int HidePanelsVk() {
    std::lock_guard<std::mutex> lk(g_hp_mu);
    hp_load_locked();
    return g_hp_vk;
}
bool CaptureScreenshotForPid(std::uint32_t pid) {
    return !capture_for_pid(pid).empty();
}

void HudClose(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_hudMu);
    auto it = g_hudMaps.find(pid);
    if (it == g_hudMaps.end()) return;
    if (it->second.s) UnmapViewOfFile(it->second.s);
    if (it->second.h) CloseHandle(it->second.h);
    g_hudMaps.erase(it);
}

void SaveWindowPos(std::uint32_t pid, int x, int y) {
    auto p = account_store_path(pid, L"window");
    if (p.empty()) return;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << x << " " << y;
}
bool LoadWindowPos(std::uint32_t pid, int& x, int& y) {
    std::string s = alerts_read_file(account_store_path(pid, L"window"));
    if (s.empty()) return false;
    std::istringstream ss(s);
    int rx, ry;
    if (!(ss >> rx >> ry)) return false;
    x = rx; y = ry;
    return true;
}

void SetLauncherWindow(void* hwnd) { g_launcherHwnd = hwnd; }

void AttachBridge(ultralight::View* view) {
    if (!view) return;
    auto scoped = view->LockJSContext();
    JSContextRef ctx = scoped->ctx();

    JSObjectRef global = JSContextGetGlobalObject(ctx);

    JSStringRef ns_name = JSStringCreateWithUTF8CString("rtx");
    JSObjectRef ns = JSObjectMake(ctx, nullptr, nullptr);
    JSObjectSetProperty(ctx, global, ns_name, ns,
                        kJSPropertyAttributeDontDelete | kJSPropertyAttributeDontEnum,
                        nullptr);
    JSStringRelease(ns_name);

    ensure_vault_unlocked();

    install_fn(ctx, ns, "openExternal",           OpenExternal);
    install_fn(ctx, ns, "scanProcesses",     ScanProcesses);
    install_fn(ctx, ns, "gameSnapshots",     GameSnapshots);
    install_fn(ctx, ns, "uiAsset",           UiAsset);
    install_fn(ctx, ns, "soundList",         SoundList);
    install_fn(ctx, ns, "soundExport",       SoundExport);
    install_fn(ctx, ns, "soundPlay",         SoundPlay);
    install_fn(ctx, ns, "soundStop",         SoundStop);
    install_fn(ctx, ns, "soundPause",        SoundPause);
    install_fn(ctx, ns, "soundResume",       SoundResume);
    install_fn(ctx, ns, "soundSeek",         SoundSeek);
    install_fn(ctx, ns, "soundVolume",       SoundVolume);
    install_fn(ctx, ns, "soundStatus",       SoundStatus);
    install_fn(ctx, ns, "soundFilterStatus", SoundFilterStatus);
    install_fn(ctx, ns, "soundFilterEnable", SoundFilterEnable);
    install_fn(ctx, ns, "soundMute",         SoundMute);
    install_fn(ctx, ns, "clueSearchTarget", ClueSearchTarget);
    install_fn(ctx, ns, "menuStatus",        MenuStatus);
    install_fn(ctx, ns, "menuEnable",        MenuEnable);
    install_fn(ctx, ns, "menuPins",          MenuSwapFn);
    install_fn(ctx, ns, "hostInfo",          HostInfo);
    install_fn(ctx, ns, "readerHealth",      ReaderHealth);
    install_fn(ctx, ns, "bridgeStatus",      BridgeStatus);
    install_fn(ctx, ns, "itemIcon",          ItemIcon);
    install_fn(ctx, ns, "iconSource",        IconSource);
    install_fn(ctx, ns, "iconMisses",        IconMisses);
    install_fn(ctx, ns, "iconCoverage",      IconCoverage);
    install_fn(ctx, ns, "modelIcon",         ModelIcon);
    install_fn(ctx, ns, "itemInfo",          ItemInfo);
    install_fn(ctx, ns, "sprite",            Sprite);
    install_fn(ctx, ns, "mapAreas",          MapAreas);
    install_fn(ctx, ns, "mapAreaImage",      MapAreaImage);
    install_fn(ctx, ns, "spriteByName",      SpriteByName);
    install_fn(ctx, ns, "hudSprite",         HudSprite);
    install_fn(ctx, ns, "enumInfo",          EnumInfo);
    install_fn(ctx, ns, "structParams",      StructParams);
    install_fn(ctx, ns, "dbRows",            DbRows);
    install_fn(ctx, ns, "itemParams",        ItemParams);
    install_fn(ctx, ns, "abilityConfigs",    AbilityConfigs);
    install_fn(ctx, ns, "buffCatalog",       BuffCatalog);
    install_fn(ctx, ns, "mystPages",         MystPages);
    install_fn(ctx, ns, "archResearch",      ArchResearch);
    install_fn(ctx, ns, "npcInfo",           NpcInfo);
    install_fn(ctx, ns, "paramDef",          ParamDef);
    install_fn(ctx, ns, "cacheIfaceGroup",   CacheIfaceGroup);
    install_fn(ctx, ns, "mapWindow",         MapWindow);
    install_fn(ctx, ns, "mapLabels",         MapLabels);
    install_fn(ctx, ns, "mapCategories",     MapCategories);
    install_fn(ctx, ns, "mapSymbols",        MapSymbols);
    install_fn(ctx, ns, "mapLocNames",       MapLocNames);
    install_fn(ctx, ns, "achievements",      Achievements);
    install_fn(ctx, ns, "bankItems",         BankItems);
    install_fn(ctx, ns, "metalBankItems",    MetalBankItems);
    install_fn(ctx, ns, "materialItems",     MaterialItems);
    install_fn(ctx, ns, "groupBankItems",    GroupBankItems);
    install_fn(ctx, ns, "baitBoxItems",      BaitBoxItems);
    install_fn(ctx, ns, "nexusItems",        NexusItems);
    install_fn(ctx, ns, "workbenchItems",    WorkbenchItems);
    install_fn(ctx, ns, "sceneEntities",     SceneEntities);
    install_fn(ctx, ns, "playerInfo",        PlayerInfo);
    install_fn(ctx, ns, "gameTick",          GameTick);
    install_fn(ctx, ns, "clientState",       ClientState);
    install_fn(ctx, ns, "gameTickState",     GameTickState);
    install_fn(ctx, ns, "perks",             Perks);
    install_fn(ctx, ns, "inventory",         Inventory);
    install_fn(ctx, ns, "containerItems",    ContainerItems);
    install_fn(ctx, ns, "openContainers",    OpenContainers);
    install_fn(ctx, ns, "pof",               Pof);
    install_fn(ctx, ns, "itemExtraInts",     ItemExtraInts);
    install_fn(ctx, ns, "equipment",         Equipment);
    install_fn(ctx, ns, "varps",             Varps);
    install_fn(ctx, ns, "varbits",           Varbits);
    install_fn(ctx, ns, "varbitMap",         VarbitMap);
    install_fn(ctx, ns, "varbitDomainMap",   VarbitDomainMap);
    install_fn(ctx, ns, "varbitDomains",     VarbitDomains);
    install_fn(ctx, ns, "varDefs",           VarDefs);
    install_fn(ctx, ns, "serverOps",         ServerOps);
    install_fn(ctx, ns, "membership",        Membership);
    install_fn(ctx, ns, "quests",            Quests);
    install_fn(ctx, ns, "varpsDumpAll",      VarpsDumpAll);
    install_fn(ctx, ns, "varcsDumpAll",      VarcsDumpAll);
    install_fn(ctx, ns, "varDomainStores",   VarDomainStores);
    install_fn(ctx, ns, "varcStrings",       VarcStrings);
    install_fn(ctx, ns, "varcInts",          VarcInts);
    install_fn(ctx, ns, "varcLongs",         VarcLongs);
    install_fn(ctx, ns, "varpsLong",         VarpsLong);
    install_fn(ctx, ns, "varcStringsDumpAll", VarcStringsDumpAll);
    install_fn(ctx, ns, "groundItems",       GroundItems);
    install_fn(ctx, ns, "varsDump",          VarsDump);
    install_fn(ctx, ns, "varsWatch",         VarsWatch);
    install_fn(ctx, ns, "serverPackets",     ServerPackets);
    install_fn(ctx, ns, "serverPacketFeed",  ServerPacketFeed);
    install_fn(ctx, ns, "serverPacketArm",   ServerPacketArm);
    install_fn(ctx, ns, "events",            Events);
    install_fn(ctx, ns, "eventsMask",        EventsMask);
    install_fn(ctx, ns, "renderToggle",      RenderToggle);
    install_fn(ctx, ns, "outlineNpc",        OutlineNpc);
    install_fn(ctx, ns, "outlineObject",     OutlineObject);
    install_fn(ctx, ns, "nameplatePlayer",   NameplatePlayer);
    install_fn(ctx, ns, "interfaceGroups",   InterfaceGroups);
    install_fn(ctx, ns, "interfaceGroup",     InterfaceGroup);
    install_fn(ctx, ns, "ifaceCompRects",     IfaceCompRects);
    install_fn(ctx, ns, "ifaceSpriteParent",  IfaceSpriteParent);
    install_fn(ctx, ns, "compassHeading",     CompassHeading);
    install_fn(ctx, ns, "compassTarget",      CompassTarget);
    install_fn(ctx, ns, "scanSolution",       ScanSolution);
    install_fn(ctx, ns, "hoverEntity",        HoverEntity);
    install_fn(ctx, ns, "puzzleState",        PuzzleState);
    install_fn(ctx, ns, "puzzleCellRects",    PuzzleCellRects);
    install_fn(ctx, ns, "puzzleWdTable",      PuzzleWdTable);
    install_fn(ctx, ns, "puzzlePdbTable",     PuzzlePdbTable);
    install_fn(ctx, ns, "interfaceSizeSearch", InterfaceSizeSearch);
    install_fn(ctx, ns, "interfaceComps",     InterfaceComps);
    install_fn(ctx, ns, "ifaceOffset",        IfaceOffset);
    install_fn(ctx, ns, "dialog",            Dialog);
    install_fn(ctx, ns, "uiHighlight",       UiHighlightFn);
    install_fn(ctx, ns, "uiHighlights",      UiHighlightsFn);
    install_fn(ctx, ns, "uiLabels",          UiLabelsFn);
    install_fn(ctx, ns, "centerText",        CenterTextFn);
    install_fn(ctx, ns, "panelRects",        PanelRectsFn);
    install_fn(ctx, ns, "panelViz",          PanelVizFn);
    install_fn(ctx, ns, "puzzleCells",        PuzzleCellsFn);
    install_fn(ctx, ns, "knotCells",          KnotCellsFn);
    install_fn(ctx, ns, "skillBars",          SkillBarsFn);
    install_fn(ctx, ns, "invSlotRect",       InvSlotRectFn);
    install_fn(ctx, ns, "chat",              Chat);
    install_fn(ctx, ns, "buffs",             Buffs);
    install_fn(ctx, ns, "cooldowns",         Cooldowns);
    install_fn(ctx, ns, "actionBar",         ActionBar);
    install_fn(ctx, ns, "overlayConfig",     OverlayConfig);
    install_fn(ctx, ns, "overlayHighlight",  OverlayHighlight);
    install_fn(ctx, ns, "hiscores",          HiscoresJson);
    install_fn(ctx, ns, "partySetCode",      PartySetCode);
    install_fn(ctx, ns, "partyGetCode",      PartyGetCode);
    install_fn(ctx, ns, "partyReport",       PartyReport);
    install_fn(ctx, ns, "partyData",         PartyData);
    install_fn(ctx, ns, "markersGet",        MarkersGet);
    install_fn(ctx, ns, "markersVersion",    MarkersVersion);
    install_fn(ctx, ns, "markerAdd",         MarkerAdd);
    install_fn(ctx, ns, "markerRemove",      MarkerRemove);
    install_fn(ctx, ns, "markerSetLabel",    MarkerSetLabel);
    install_fn(ctx, ns, "markerSetColor",    MarkerSetColor);
    install_fn(ctx, ns, "markersClear",      MarkersClear);
    install_fn(ctx, ns, "markerKeybindsGet", MarkerKeybindsGet);
    install_fn(ctx, ns, "markerKeybindsArm", MarkerKeybindsArm);
    install_fn(ctx, ns, "markerKeybindsSet", MarkerKeybindsSet);
    install_fn(ctx, ns, "openClientWindow",  OpenClientWindow);
    install_fn(ctx, ns, "closeClientWindow", CloseClientWindow);
    install_fn(ctx, ns, "clientWindowOpen",  ClientWindowOpen);
    install_fn(ctx, ns, "dockCollapse",      DockCollapse);   // legacy no-op
    install_fn(ctx, ns, "keepFocused",       KeepFocused);
    install_fn(ctx, ns, "gameFocused",       GameFocused);
    install_fn(ctx, ns, "locMorphs",         LocMorphs);
    install_fn(ctx, ns, "hostFullscreen",    HostFullscreen);
    install_fn(ctx, ns, "railTip",           RailTip);        // legacy no-op
    install_fn(ctx, ns, "uiRects",           UiRects);
    install_fn(ctx, ns, "uiKeyboard",        UiKeyboard);
    install_fn(ctx, ns, "uiScale",           UiScale);
    install_fn(ctx, ns, "uiClientInfo",      UiClientInfo);
    install_fn(ctx, ns, "myPid",             MyPid);
    install_fn(ctx, ns, "closeProcess",      CloseProcess);
    install_fn(ctx, ns, "openLog",           OpenLog);
    install_fn(ctx, ns, "pasteClipboard",    PasteClipboard);
    install_fn(ctx, ns, "copyClipboard",     CopyClipboard);
    install_fn(ctx, ns, "captureScreenshot", CaptureScreenshot);
    install_fn(ctx, ns, "wikiOpen",           WikiOpen);
    install_fn(ctx, ns, "wikiClose",          WikiClose);
    install_fn(ctx, ns, "wikiKeybindGet",     WikiKeybindGet);
    install_fn(ctx, ns, "wikiKeybindSet",     WikiKeybindSet);
    install_fn(ctx, ns, "screenshotKeybindGet", ScreenshotKeybindGet);
    install_fn(ctx, ns, "screenshotKeybindSet", ScreenshotKeybindSet);
    install_fn(ctx, ns, "hidePanelsKeybindGet", HidePanelsKeybindGet);
    install_fn(ctx, ns, "hidePanelsKeybindSet", HidePanelsKeybindSet);
    install_fn(ctx, ns, "openScreenshots",    OpenScreenshots);
    install_fn(ctx, ns, "version",           Version);
    install_fn(ctx, ns, "latestVersion",     LatestVersion);
    install_fn(ctx, ns, "latestVersionCached", LatestVersionCached);
    install_fn(ctx, ns, "newsCached",        NewsCached);
    install_fn(ctx, ns, "social",            SocialFn);
    install_fn(ctx, ns, "walkGrid",          WalkGridFn);
    install_fn(ctx, ns, "combatLog",         CombatLogFn);
    install_fn(ctx, ns, "vosCached", VosCached);
    install_fn(ctx, ns, "pricesCached", PricesCached);
    install_fn(ctx, ns, "pricesMapping", PricesMapping);
    install_fn(ctx, ns, "pricesTimeseries", PricesTimeseries);
    install_fn(ctx, ns, "vosReport", VosReport);
    install_fn(ctx, ns, "isLeaguesWorld",     IsLeaguesWorld);
    install_fn(ctx, ns, "leaguesWorlds",      LeaguesWorlds);
    install_fn(ctx, ns, "scarabCached", ScarabCached);
    install_fn(ctx, ns, "obeliskCached", ObeliskCached);
    install_fn(ctx, ns, "worldEventVote", WorldEventVote);
    install_fn(ctx, ns, "startUpdate",       StartUpdate);
    install_fn(ctx, ns, "clientsRunning",    ClientsRunning);
    install_fn(ctx, ns, "updateState",       UpdateState);
    install_fn(ctx, ns, "playSound",         PlayAlertSound);
    install_fn(ctx, ns, "notifyWindows",     NotifyWindows);
    install_fn(ctx, ns, "discordWebhookGet", DiscordWebhookGet);
    install_fn(ctx, ns, "discordWebhookSet", DiscordWebhookSet);
    install_fn(ctx, ns, "discordNotify",     DiscordNotify);
    install_fn(ctx, ns, "discordWebhookCopy", DiscordWebhookCopy);
    install_fn(ctx, ns, "alertsLoad",        AlertsLoad);
    install_fn(ctx, ns, "alertsSave",        AlertsSave);
    install_fn(ctx, ns, "goalsLoad",         GoalsLoad);
    install_fn(ctx, ns, "goalsSave",         GoalsSave);
    install_fn(ctx, ns, "layoutLoad",        LayoutLoad);
    install_fn(ctx, ns, "layoutSave",        LayoutSave);
    install_fn(ctx, ns, "notesLoad",         NotesLoad);
    install_fn(ctx, ns, "notesSave",         NotesSave);
    install_fn(ctx, ns, "pluginGrantsLoad",  PluginGrantsLoad);
    install_fn(ctx, ns, "pluginGrantsSave",  PluginGrantsSave);
    install_fn(ctx, ns, "cacheStoreLoad",    CacheStoreLoad);
    install_fn(ctx, ns, "cacheStoreSave",    CacheStoreSave);
    install_fn(ctx, ns, "nameplatesLoad",    NameplatesLoad);
    install_fn(ctx, ns, "nameplatesSave",    NameplatesSave);
    install_fn(ctx, ns, "mystLoad",          MystLoad);
    install_fn(ctx, ns, "mystSave",          MystSave);
    install_fn(ctx, ns, "questLoad",         QuestLoad);
    install_fn(ctx, ns, "questSave",         QuestSave);
    install_fn(ctx, ns, "counterLoad",       CounterLoad);
    install_fn(ctx, ns, "counterSave",       CounterSave);
    install_fn(ctx, ns, "cs2Status",         Cs2Status);
    install_fn(ctx, ns, "cs2Extract",        Cs2Extract);
    install_fn(ctx, ns, "cs2Cancel",         Cs2Cancel);
    install_fn(ctx, ns, "cs2Search",         Cs2Search);
    install_fn(ctx, ns, "cs2Script",         Cs2Script);
    install_fn(ctx, ns, "cs2Names",          Cs2Names);
    install_fn(ctx, ns, "cs2Switches",       Cs2Switches);
    install_fn(ctx, ns, "sidebarLoad",       SidebarLoad);
    install_fn(ctx, ns, "sidebarSave",       SidebarSave);
    install_fn(ctx, ns, "keepFocusedLoad",   KeepFocusedLoad);
    install_fn(ctx, ns, "keepFocusedSave",   KeepFocusedSave);
    install_fn(ctx, ns, "fullscreenPrefLoad", FullscreenPrefLoad);
    install_fn(ctx, ns, "fullscreenPrefSave", FullscreenPrefSave);
    install_fn(ctx, ns, "hiddenPanelsLoad",  HiddenPanelsLoad);
    install_fn(ctx, ns, "hiddenPanelsSave",  HiddenPanelsSave);
    install_fn(ctx, ns, "varPinsLoad",       VarPinsLoad);
    install_fn(ctx, ns, "varPinsSave",       VarPinsSave);
    install_fn(ctx, ns, "prefsLoad",         PrefsLoad);
    install_fn(ctx, ns, "launcherMetaLoad",  LauncherMetaLoad);
    install_fn(ctx, ns, "launcherMetaSave",  LauncherMetaSave);
    install_fn(ctx, ns, "winCmd",            WinCmd);
    install_fn(ctx, ns, "prefsSave",         PrefsSave);
    install_fn(ctx, ns, "menuRulesLoad",     MenuRulesLoad);
    install_fn(ctx, ns, "menuRulesSave",     MenuRulesSave);
    install_fn(ctx, ns, "menuDef",           MenuDef);
    install_fn(ctx, ns, "menuFind",          MenuFind);
    install_fn(ctx, ns, "menuSearch",        MenuSearch);
    install_fn(ctx, ns, "flashGame",         FlashGame);
    install_fn(ctx, ns, "metronome",         Metronome);
    install_fn(ctx, ns, "guideMarks",        GuideMarksFn);
    install_fn(ctx, ns, "xpPanel",           XpPanelFn);
    install_fn(ctx, ns, "xpPanelReset",      XpPanelResetFn);
    install_fn(ctx, ns, "xpPanelState",      XpPanelStateFn);
    install_fn(ctx, ns, "overlayToast",      OverlayToast);
    install_fn(ctx, ns, "overlayNotify",     OverlayNotify);

    install_fn(ctx, ns, "listAccounts",      ListAccounts);
    install_fn(ctx, ns, "removeAccount",     RemoveAccount);
    install_fn(ctx, ns, "accountCapture",    AccountCapture);
    install_fn(ctx, ns, "launchAccount",     LaunchAccount);
    install_fn(ctx, ns, "gamePath",          GamePath);
    install_fn(ctx, ns, "gamePathPick",      GamePathPick);
    install_fn(ctx, ns, "gamePathReset",     GamePathReset);
    install_fn(ctx, ns, "rendererPref",      RendererPref);
    install_fn(ctx, ns, "gpuTiming",         GpuTiming);
    install_fn(ctx, ns, "rendererSet",       RendererSet);

    install_fn(ctx, ns, "pluginStoreLoad",   PluginStoreLoad);
    install_fn(ctx, ns, "pluginStoreSave",   PluginStoreSave);
    install_fn(ctx, ns, "pluginStoreKeys",   PluginStoreKeys);
    install_fn(ctx, ns, "pluginDevList",     PluginDevList);
    install_fn(ctx, ns, "pluginDevManifest", PluginDevManifest);
    install_fn(ctx, ns, "pluginDevEntry",    PluginDevEntry);
    install_fn(ctx, ns, "pluginDevStamp",    PluginDevStamp);

    install_fn(ctx, ns, "pluginMarketList",        PluginMarketList);
    install_fn(ctx, ns, "pluginMarketInstall",     PluginMarketInstall);
    install_fn(ctx, ns, "pluginInstallStatus",     PluginInstallStatus);
    install_fn(ctx, ns, "pluginInstalledList",     PluginInstalledList);
    install_fn(ctx, ns, "pluginInstalledManifest", PluginInstalledManifest);
    install_fn(ctx, ns, "pluginInstalledEntry",    PluginInstalledEntry);
    install_fn(ctx, ns, "pluginUninstallLocal",    PluginUninstallLocal);

    install_fn(ctx, ns, "luaLoad",                 LuaLoad);
    install_fn(ctx, ns, "luaUnload",               LuaUnload);
    install_fn(ctx, ns, "luaTick",                 LuaTick);
    install_fn(ctx, ns, "luaEvent",                LuaEvent);
    install_fn(ctx, ns, "luaUiEvent",              LuaUiEvent);
    install_fn(ctx, ns, "luaInfo",                 LuaInfo);
    install_fn(ctx, ns, "luaVersion",              LuaVersion);
    install_fn(ctx, ns, "launcherLogTail",         LauncherLogTail);

}

}  // namespace rtx::launcher
