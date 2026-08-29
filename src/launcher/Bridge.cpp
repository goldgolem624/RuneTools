#include "Bridge.h"
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
#include "../cache/CacheReader.h"
#include "../cache/Constants.h"     // kIndexSoundEffects / kIndexMusic for the audio bridges
#include "Audio.h"                  // in-process Ogg Vorbis playback
#include "SoundFilter.h"            // in-client sound observation + muting (companion channel)
#include "../../companion/FrameShare.h"   // health check: in-game UI layer heartbeat
#include "../../companion/NetProbeShare.h" // health check: chat packet capture ring
#include "MenuSwap.h"               // right-click menu inspector + reorder (companion channel)
#include "../cache/Achievements.h"
#include "../../companion/HudShare.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"
#include "../shared/MachineFingerprint.h"
#include "Http.h"
#include "Crypto.h"
#include "Zip.h"

#include <Ultralight/Ultralight.h>
#include <JavaScriptCore/JavaScript.h>
#include <Windows.h>
// gdiplus.h references unqualified min/max; pull in the std versions first (the Windows macros are suppressed).
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

namespace {

// Body wrapper for the long-lived detached threads below: an escaping exception
// would std::terminate the whole launcher, so log it and let the thread end.
template <class F>
void guarded(const char* what, F&& f) {
    try { f(); }
    catch (const std::exception& e) { rtx::log::Launcher(std::string(what) + ": thread threw: " + e.what()); }
    catch (...) { rtx::log::Launcher(std::string(what) + ": thread threw (non-std)"); }
}

// Bounded JS number -> int: NaN/Infinity through a plain (int) cast is UB, so clamp instead.
static int js_int(JSContextRef ctx, JSValueRef v, int def = 0, int lo = INT_MIN, int hi = INT_MAX) {
    double d = JSValueToNumber(ctx, v, nullptr);
    if (!std::isfinite(d)) return def;
    if (d < (double)lo) return lo;
    if (d > (double)hi) return hi;
    return (int)d;
}

// ---- Screenshot capture ----------------------------------------------------
// Grabs the game window to a PNG under %USERPROFILE%\RuneToolsX\screenshots, named by local date/time.
// The keybind has no default: 0 = unbound.

std::filesystem::path runetools_dir() {
    // Reuse Log's base: LogDir() = ...\RuneToolsX\logs, so its parent is the RuneToolsX root.
    return std::filesystem::path(rtx::log::LogDir()).parent_path();
}
std::filesystem::path screenshots_dir() { return runetools_dir() / L"screenshots"; }
std::filesystem::path screenshot_cfg()  { return runetools_dir() / L"screenshot.txt"; }
std::filesystem::path hidepanels_cfg()  { return runetools_dir() / L"hidepanels.txt"; }   // Preferences: hide/show all panels key

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

// Samples a grid of the 32bpp bitmap: some GPU-accelerated windows render black to a memory DC.
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

// Capture `hwnd` into a fresh 32bpp top-down DIB via PrintWindow(PW_RENDERFULLCONTENT), falling back
// to a screen BitBlt of the window rect when that comes back blank. Returns the HBITMAP (caller deletes).
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
        // PW_RENDERFULLCONTENT (0x2) captures many GPU-composited windows the plain path renders black.
        if (PrintWindow(hwnd, mem, 0x00000002)) ok = true;
        GdiFlush();   // flush batched GDI so the DIB bits reflect the draw
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

// Filesystem-safe local timestamp path: YYYY-MM-DD_HH-MM-SS[_2] (dedup suffix on collision).
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

std::wstring capture_to_screenshots(HWND hwnd) {
    int w = 0, h = 0;
    HBITMAP dib = capture_window_dib(hwnd, w, h);
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

// Raises the in-game toast too, so the keybind path has feedback (the host consumes the key).
std::wstring capture_for_pid(std::uint32_t pid) {
    HWND hwnd = pid ? (HWND)rtx::launcher::dock::GameWindowHandle(pid) : nullptr;
    if (!hwnd) return {};
    std::wstring path = capture_to_screenshots(hwnd);
    if (path.empty()) {
        // Modern alert card first; the legacy in-frame toast only as the fallback while
        // the UI layer isn't up, so keybind feedback never goes silent.
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

// Keybind state: a single virtual-key code, 0 = unbound. Persisted as one integer.
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
// Hide/show-all-panels hotkey: same one-int file shape as the screenshot key, own file so
// neither clobbers the other.
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

constexpr const char* kAppVersion = "1.0.0";   // fallback; real version read from FILEVERSION (app.rc)

std::filesystem::path vault_key_path() { return runetools_dir() / L"vault.key"; }

// The vault passphrase: a random 32-byte secret held on disk under DPAPI, bound to the WINDOWS
// USER ACCOUNT.
//
// It used to be the machine fingerprint - SHA-256(volume serial | MachineGuid | computer name).
// Every one of those inputs is readable by any process on the box, so the "passphrase" was a
// value an attacker could simply recompute: the vault resisted a stolen FILE COPY and nothing
// else, while holding live JX_ session tokens. Under DPAPI the key material cannot be derived
// from machine facts at all - decrypting it requires running as this Windows user on this
// machine, which is a materially higher bar and the platform's intended answer here.
//
// Empty return means DPAPI was unavailable; the caller falls back to the old secret rather than
// locking the user out of their own accounts.
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
    if (!o) return {};       // a half-written key would orphan the vault on the next run
    return secret;
}

// The saved-accounts vault is sealed at rest; unlocked once at startup.
void unlock_or_create_vault(const std::string& secret) {
    if (accounts::HasVault()) {
        if (accounts::Unlock(secret)) return;
        // A vault from before the DPAPI key was sealed with the machine fingerprint. Migrate it in
        // place: ChangePassphrase re-seals the accounts already decrypted in memory, so nobody
        // loses saved logins over a key-management change they never asked for.
        const std::string& legacy = shared::GetMachineFingerprint();
        if (!legacy.empty() && legacy != secret && accounts::Unlock(legacy)) {
            if (accounts::ChangePassphrase(secret)) {
                rtx::log::Launcher("accounts vault migrated to the per-user (DPAPI) key");
            } else {
                // Still unlocked and usable, just not re-sealed. Retried next launch.
                rtx::log::Launcher("accounts vault re-seal failed; staying on the legacy key");
            }
            return;
        }
        // Sealed by another machine or another Windows user: start fresh, otherwise every
        // account-save would silently no-op. DiscardVault keeps the old file as .bak.
        rtx::log::Launcher("accounts vault not unlockable; starting a fresh vault");
        accounts::DiscardVault();
    }
    accounts::Create(secret);
}

// Unlock the vault exactly once, lazily, on first bridge attach.
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

std::string js_to_utf8(JSContextRef ctx, JSValueRef v) {
    JSStringRef s = JSValueToStringCopy(ctx, v, nullptr);
    if (!s) return {};
    size_t bytes = JSStringGetMaximumUTF8CStringSize(s);
    std::string out(bytes, '\0');
    size_t n = JSStringGetUTF8CString(s, out.data(), bytes);
    JSStringRelease(s);
    if (n) out.resize(n - 1);
    return out;
}

JSValueRef utf8_to_js(JSContextRef ctx, const std::string& s) {
    JSStringRef js = JSStringCreateWithUTF8CString(s.c_str());
    JSValueRef out = JSValueMakeString(ctx, js);
    JSStringRelease(js);
    return out;
}

// Serve a per-pid panel read from the reader's background cache: the synchronous cross-process
// read must never run on the UI thread, which stays free to pump the embedded host's
// activation. `build` runs on the background thread, so capture by value only.
JSValueRef served(JSContextRef ctx, const std::string& key, const char* empty,
                  std::function<std::string()> build) {
    std::string r = rtx::reader::ReadAsync(key, std::move(build));
    return utf8_to_js(ctx, r.empty() ? std::string(empty) : r);
}

// One failure envelope for object-shaped bridge reads: {"ok":false,"why":"<why>"} with why in
// {noclient, noargs, pending, error, unsupported}. bridgeJson() in client.html turns this into
// null and records the why, so a panel can tell "no client attached" from "empty data". Only
// bindings whose SUCCESS shape is a JSON object with named fields use it; array, scalar, string
// and keyed-map bindings keep their old empty default, because an object there would break
// `.length` / string callers and would surface "ok"/"why" as rows to key-iterating panels.
static std::string fail_json(const char* why) {
    return std::string("{\"ok\":false,\"why\":\"") + why + "\"}";
}

// True when the reader currently tracks `pid`. Reads the cached snapshot list only (the same
// string gameSnapshots serves), so this adds no cross-process read.
static bool pid_known(std::uint32_t pid) {
    if (!pid) return false;
    const std::string j = rtx::reader::SamplesJson();
    return j.find("\"pid\":" + std::to_string(pid) + ",") != std::string::npos;
}

// served() for object-shaped bindings: noclient when the pid is not attached, error when the
// reader produced nothing, otherwise the success payload unchanged.
static JSValueRef served_obj(JSContextRef ctx, std::uint32_t pid, const std::string& key,
                             std::function<std::string()> build) {
    if (!pid_known(pid)) return utf8_to_js(ctx, fail_json("noclient"));
    std::string r = rtx::reader::ReadAsync(key, std::move(build));
    return utf8_to_js(ctx, r.empty() ? fail_json("error") : r);
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out; out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string get_string_arg(JSContextRef ctx, size_t argc,
                           const JSValueRef argv[], size_t idx) {
    if (idx >= argc) return {};
    return js_to_utf8(ctx, argv[idx]);
}

// Sanitised, per-account identity used for every per-account file and for vote dedupe.
// Defined up here because the vote handler needs it long before the storage helpers below.
// Mirrors BankCache's sanitize so the same account maps predictably to a file:
// keep [A-Za-z0-9-_], space -> '_', drop everything else.
std::string sanitize_account(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    return out;
}

// Reader::AccountKey first (JX_DISPLAY_NAME when the Jagex Launcher set it, else the in-memory
// character name), with a direct env read as the last resort before the reader has attached.
// The STEAM client sets no JX_ vars, so an env-only lookup resolved to nothing there and every
// per-account store silently did nothing - markers, alerts, goals, plugin storage.
std::string account_key_for(std::uint32_t pid) {
    if (!pid) return {};
    std::string acct = sanitize_account(rtx::reader::AccountKey(pid));
    if (!acct.empty()) return acct;
    auto env = process::ReadJxEnv(pid);
    auto it = env.find("JX_DISPLAY_NAME");
    return (it != env.end()) ? sanitize_account(it->second) : std::string();
}

// ---- Callbacks ----

// Open a URL in the user's default browser. Allowlist runetools.io
// only so a tampered renderer can't fire arbitrary shell handlers.
JSValueRef OpenExternal(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    auto url = get_string_arg(ctx, argc, argv, 0);
    static const char* kAllowed = "https://runetools.io/";
    if (url.rfind(kAllowed, 0) != 0) return JSValueMakeUndefined(ctx);
    int n = MultiByteToWideChar(CP_UTF8, 0, url.data(), (int)url.size(),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.data(), (int)url.size(),
                        w.data(), n);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return JSValueMakeUndefined(ctx);
}

bool account_capture_get();      // defined with the rest of the accounts bridge, below

JSValueRef ScanProcesses(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    auto procs = process::ScanRsClients();
    const bool unlocked = accounts::IsUnlocked();
    const bool capture  = account_capture_get();

    std::ostringstream os;
    os << "[";
    // Ids of the clients running right now, so a suppression can be lifted once its client is
    // gone (see accounts::PruneCaptureSuppressions).
    std::vector<std::string> live_ids;
    for (size_t i = 0; i < procs.size(); ++i) {
        const auto& p = procs[i];
        if (i) os << ",";

        // Peek JX_ env so the row can show the account name, and - only when the user has left
        // capture on - save a running client into the vault while the PEB read is already being
        // paid. The name is shown either way; that is a label, not a credential.
        auto env = process::ReadJxEnv(p.pid);
        std::string display_name, character_id;
        auto it = env.find("JX_DISPLAY_NAME");
        if (it != env.end()) display_name = it->second;
        it = env.find("JX_CHARACTER_ID");
        if (it != env.end()) character_id = it->second;
        const bool has_env = !env.empty();

        if (!character_id.empty()) live_ids.push_back(character_id);

        // An account the user has just removed must not be put straight back by capture while
        // its client happens to still be running. That is what made Remove look like it did
        // nothing: the row disappeared and returned on the next enumeration.
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
    // Lift the suppression on any account whose client has since closed, so removing an account
    // stops capture NOW without blocking a later login from ever being saved again.
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
    // Launcher-side stream checks appended here (they are not reader chains): the
    // companion present/UI pipeline and the sound observation hook, both of which a
    // game update can break independently of every memory read above.
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
            // Chat packet capture: the framer hook + always-on op-0x15 ring. A game
            // update can move the framer signature or the connection-object offsets;
            // either shows up here as hook-missing or a message count stuck at 0.
            wchar_t name[64]; rtx::netprobe::MakeSectionName(pid, name);
            HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
            int ok = 2; std::string d = "inactive (loads with the game client)";
            if (m) {
                auto* sh = reinterpret_cast<const rtx::netprobe::Share*>(
                    MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
                if (sh) {
                    if (sh->magic == rtx::netprobe::kMagic && sh->version >= 3) {
                        if (!(sh->flags & 1)) {
                            ok = 0; d = "framer hook not attached (signature may have moved)";
                        } else if (sh->chatSeen == 0) {
                            // Not a failure by itself: an idle character can genuinely
                            // receive nothing for a while after injection.
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

// Text of a sibling UI asset, so the page can load bulky data ON DEMAND instead of having it
// spliced in at startup (quest_guides.js alone is 1.3MB of the 2.7MB the page parses).
JSValueRef UiAsset(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    return utf8_to_js(ctx, rtx::launcher::dock::ReadUiAsset(js_to_utf8(ctx, argv[0])));
}

// ---- Cache audio (js5-14 effects / js5-40 music) ----
// Defined further down (next to the other per-user paths); declared here because the sound
// exporters below need it and they sit up with the other cache accessors.
std::filesystem::path alerts_user_dir();

// soundList(index, startId, limit) -> [{id,rate,ch,ms,bytes}, ...]
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

// soundExport(index, id) -> absolute path of a written .ogg, or "".
// The client ships no Vorbis decoder (and Windows has none), so a sound is made audible by
// writing the real .ogg and handing it to whatever the user already has - browsers all decode
// Vorbis. soundPlay() below does that in one step.
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

// soundPlay(index, id, volumePct) -> true if it decoded and started. Decoded and mixed IN
// PROCESS (Audio.cpp / stb_vorbis) - nothing is written to disk and no external player opens.
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
    auto chunks = rtx::cache::SoundOggChunks(idx, id);   // every stream, not just the first
    if (chunks.empty()) return JSValueMakeBoolean(ctx, false);
    return JSValueMakeBoolean(ctx, rtx::audio::Play(chunks, vol));
}

// Transport. All return immediately; the decode/mix work lives on Audio.cpp's worker.
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
// soundStatus() -> {state,pos,dur,vol,rate,ch}; the panel polls this to drive its transport.
JSValueRef SoundStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::audio::StatusJson());
}

// ---- in-client sound: observe what the game plays, and mute ids (companion channel) ----
// Distinct from the SoundPlay family above: those play cache audio in the LAUNCHER, these act
// on the game's own playback. Both key on the same js5-14 id space if the companion's observed
// id proves to be the archive id - which "verified" in the status reports.

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

// ids: comma-separated, same convention as the other list-valued bridge args.
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

// The searchable object a clue points at, resolved from the map. Clue items carry the tile but
// never the object's name, so the panel asks for it per target tile and caches the answer.
JSValueRef ClueSearchTarget(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return utf8_to_js(ctx, "{}");
    int x = js_int(ctx, argv[0]);
    int y = js_int(ctx, argv[1]);
    int p = (argc >= 3) ? js_int(ctx, argv[2]) : 0;
    return utf8_to_js(ctx, rtx::cache::ClueSearchTargetJson(x, y, p));
}

// ---- right-click menu (Developer) ----
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
    // A number (rtx::menu::kEnable*), though a bool still works: JSValueToNumber maps it to 1/0,
    // which are exactly kEnablePanel and kEnableOff.
    const double raw = JSValueToNumber(ctx, argv[1], nullptr);
    // NaN (undefined) casts to an unsigned as undefined behaviour, so decide the value here.
    const auto mode = (raw >= 1.0 && raw <= 2.0) ? static_cast<std::uint32_t>(raw) : 0u;
    return JSValueMakeBoolean(ctx, rtx::launcher::menuswap::SetEnabled(pid, mode));
}

// verbs: newline-separated, in the order they should appear at the top of the menu.
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

JSValueRef ItemIcon(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, icons::ItemIconDataUrl(id));
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

// --- HUD reminder (top-center sprite + caption over the game frame) ------------------------
// The launcher owns/creates the per-client HUD section; the in-client module opens it read-only.
// One mapping per pid is kept alive so the section persists for the module to read.
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
    s->seq++;                                  // begin write (odd)
    if (!on) { s->enable = 0; s->seq++; return true; }
    if (spriteId != m->lastSprite) {           // decode only when the sprite changes
        int w = 0, h = 0;
        auto rgba = rtx::cache::SpriteRgba(spriteId, w, h);
        if (!rgba.empty() && w > 0 && h > 0 && w <= rtx::hud::kMaxW && h <= rtx::hud::kMaxH) {
            s->w = w; s->h = h;
            std::memcpy(s->rgba, rgba.data(), (size_t)w * h * 4);
            s->imgSeq++;                       // new image -> module re-uploads
        }
        m->lastSprite = spriteId;
    }
    size_t cn = caption.size(); if (cn > (size_t)rtx::hud::kCaptionMax) cn = rtx::hud::kCaptionMax;
    std::memcpy(s->caption, caption.data(), cn); s->caption[cn] = 0;
    s->enable = 1;
    s->seq++;                                  // end write (even)
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
    // Optional 2nd arg: cap the longest side to `px` (panels pass ~2x their box for a clean 2:1 rescale).
    // Optional 3rd arg: frame index within a multi-frame group (chat <img=N> icons).
    int px = (argc >= 2) ? js_int(ctx, argv[1]) : 0;
    int frame = (argc >= 3) ? js_int(ctx, argv[2]) : 0;
    return utf8_to_js(ctx, (px > 0 || frame > 0) ? rtx::cache::SpriteDataUrlScaled(id, px, frame)
                                                 : rtx::cache::SpriteDataUrl(id));
}
// rtx.spriteByName(name) -> archive id of a named sprite group ("modicons"), -1 if unknown.
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
    // served(): the first decode of a big table (163 = 5,313 rows) walks ~19,560 archive files
    // and must not hitch the UI thread; DbRowsJson memoizes, so the background refresh is free.
    return served(ctx, "dbrows:" + std::to_string(t), "[]", [t] { return rtx::cache::DbRowsJson(t); });
}

JSValueRef StructParams(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    int id = js_int(ctx, argv[0]);
    return utf8_to_js(ctx, rtx::cache::StructParamsJson(id));
}

// Cached/async: walking every struct once must not run on the UI thread.
JSValueRef AbilityConfigs(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "abilityconfigs", "{}", [] { return rtx::cache::AbilityConfigsJson(); });
}
// rtx.buffCatalog() -> every buff/debuff name in the cache with its icon id (see BuffCatalogJson).
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

// Map windows are built OFF the UI thread. A request that is not ready yet answers
// {"pending":1} at once and starts (or joins) a background build; the panel polls the same
// call again shortly and receives the finished window, which is then dropped from the ready
// store (the panel keeps its own decoded cache). Before this the build ran synchronously on
// the UI thread and every pan froze the whole client for the duration of a chunk render.
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
    bool sync = (argc > 6) && JSValueToBoolean(ctx, argv[6]);   // legacy callers (clue scan) may insist on inline
    if (sync) {
        // Same guard as the async path: a cache exception must not unwind into JSC.
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
        // Never let an abandoned pan pile results up: keep the store small.
        if (g_mapwinReady.size() > 96) g_mapwinReady.erase(g_mapwinReady.begin());
    }).detach();
    return utf8_to_js(ctx, "{\"pending\":1}");
}

// World-map AREAS (js5-23) and their composited images: static cache data, served off the UI
// thread and memoised (the surface image is ~1.7 MB of PNG, fetched once per session).
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
// World-map ELEMENT tables. Both walk the whole config archive once, so they are served off the
// UI thread and memoised by key -- the panel asks for them a single time when it opens.
JSValueRef MapLabels(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "maplabels", "{}", [] { return rtx::cache::MapLabelsJson(); });
}
JSValueRef MapCategories(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "mapcategories", "{}", [] { return rtx::cache::MapCategoriesJson(); });
}
// A full region walk: always off the UI thread, and memoised on both sides.
JSValueRef MapSymbols(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "mapsymbols", "{\"n\":0,\"b\":\"\"}", [] { return rtx::cache::MapSymbolsJson(); });
}
// Piggybacks on the same region walk (see MapLocNamesJson), so it is served the same way.
JSValueRef MapLocNames(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    return served(ctx, "maplocnames", "{}", [] { return rtx::cache::MapLocNamesJson(); });
}

JSValueRef BankItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields ({"items":[..],..}).
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "bank:" + std::to_string(pid),
                      [pid]{ return rtx::reader::BankJson(pid); });
}

JSValueRef MetalBankItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields.
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "metalbank:" + std::to_string(pid),
                      [pid]{ return rtx::reader::MetalBankJson(pid); });
}

JSValueRef MaterialItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields.
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "materials:" + std::to_string(pid),
                      [pid]{ return rtx::reader::MaterialsJson(pid); });
}

JSValueRef GroupBankItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields.
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "groupbank:" + std::to_string(pid),
                      [pid]{ return rtx::reader::GroupBankJson(pid); });
}

JSValueRef BaitBoxItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields.
    if (argc < 1) return utf8_to_js(ctx, fail_json("noargs"));
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served_obj(ctx, pid, "baitbox:" + std::to_string(pid),
                      [pid]{ return rtx::reader::BaitBoxJson(pid); });
}

JSValueRef WorkbenchItems(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields.
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
    rtx::launcher::companion::EnsureLoaded(pid);   // runtime scene objects (dig-site hotspots etc.)
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

JSValueRef GameTick(JSContextRef ctx, JSObjectRef, JSObjectRef,
                    size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeNumber(ctx, -1);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::uint32_t count = 0; double age_ms = 0;
    if (!rtx::reader::TickState(pid, count, age_ms)) return JSValueMakeNumber(ctx, -1);
    return JSValueMakeNumber(ctx, static_cast<double>(count));
}

// Tick count PLUS the ms since that tick landed, which GameTick throws away. The metronome
// widget needs the age: a 600 ms beat sampled at the UI's poll rate would jitter by more
// than a third of a beat, so the page samples this occasionally and interpolates the phase
// locally from its own clock. age = -1 when the tick time is unknown.
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
    // served(): the container walk is a real cross-process read polled per open panel;
    // it must not run inline on the UI thread (see the served() contract above).
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
    // Optional 4th arg = the SLOT to read. Two stacks of one id hold different instance
    // vars, and without this every read returned the first slot's.
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

JSValueRef VarbitMap(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, rtx::cache::VarbitMapJson());
}

// Membership tier. Free-vs-member is engine state (the PLAYERMEMBER op), not a var, so this
// cannot be served from varps/varbits like the rest of the Player State panel.
JSValueRef Membership(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    // fail_json envelope: success is an object with named fields (member/premier flags).
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

// Cached/async: resolving the opcode table can involve a fallback image scan, which
// must not run on the UI thread.
JSValueRef ServerPackets(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{\"ok\":false}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return served(ctx, "serverpackets:" + std::to_string(pid), "{\"ok\":false}",
                  [pid]{ return rtx::reader::ServerPacketsJson(pid); });
}

// Synchronous on purpose: a local read of the companion's mapped ring (no cross-process
// RPM), and the `since` cursor must reflect THIS call, not a cached earlier span.
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

// which: 0 hide NPCs, 1 hide other players, 2 hide the whole scene.
JSValueRef RenderToggle(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 3) return JSValueMakeBoolean(ctx, false);
    auto pid   = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    int  which = js_int(ctx, argv[1]);
    bool on    = JSValueToBoolean(ctx, argv[2]);
    return JSValueMakeBoolean(ctx, rtx::reader::RenderToggle(pid, which, on));
}

// rtx.outlineObject(pid, locId, x, y, plane, on): box-outline one placed object, the way
// outlineNpc does for NPCs. Keyed per (id, tile), so two instances of one loc toggle apart.
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
    static std::map<std::uint32_t, std::vector<int>> s_uids;   // outlined uids per client
    std::lock_guard<std::mutex> lk(s_mu);
    auto& v = s_uids[pid];
    v.erase(std::remove(v.begin(), v.end(), uid), v.end());
    if (on) v.push_back(uid);
    rtx::launcher::companion::EnsureLoaded(pid);     // hosts the in-frame marker layer
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
    static std::map<std::uint32_t, std::vector<int>> s_uids;   // nameplated player uids per client
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

// Rect of the layer holding a sprite -- addresses window chrome by sprite rather than by
// component id, which is not stable across layouts.
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

// Solver tables (wd_table.bin, pdb_5554.bin) are staged next to the exe by the build and
// returned as base64.
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
    // fail_json envelope: success is an object with named fields (the open dialog).
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
    // Poll sites re-request the same (group, comps) tuple, so the key is stable and the
    // interface-tree walk stays off the UI thread.
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
    if (argc >= 16) c.np_range   = js_int(ctx, argv[15]);
    rtx::overlay::Configure(c);
    return JSValueMakeBoolean(ctx, true);
}

// ---- Party hiscores (Dungeoneering): official index_lite lookups so the panel can judge
//      rooms by the PARTY's best level per skill. Results are cached ON DISK for 1 HOUR per name;
//      a stale (>1h) or absent name kicks one background fetch, a fresh cache serves with no network. ----
namespace {
std::mutex g_hs_mu;
struct HsEntry { int state = 0; long long epoch = 0; std::string body; bool inflight = false; };  // state 0 none 1 ok 2 error; epoch = unix secs of last COMPLETED fetch
std::map<std::string, HsEntry> g_hiscores;   // key = display name as asked
bool g_hs_loaded = false;
constexpr long long kHsTtlSec = 3600;        // 1 hour: min interval between fetches of one name

std::wstring hs_cache_path() {
    wchar_t buf[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    std::wstring dir = (n && n < MAX_PATH) ? std::wstring(buf) : L".";
    dir += L"\\RuneToolsX";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\hiscores.cache";
}
// on-disk format: one record/line "epoch\tname\tbody" with body newlines -> \x1f, CR dropped
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
        if (kv.second.state != 1) continue;   // persist only successful lookups
        bool bad = false;   // a control char in the key would corrupt the line format
        for (unsigned char c : kv.first) if (c < 0x20) { bad = true; break; }
        if (bad) continue;
        std::string body = kv.second.body;
        for (char& c : body) if (c == '\r') c = '\x1f'; else if (c == '\n') c = '\x1f';
        f << kv.second.epoch << '\t' << kv.first << '\t' << body << '\n';
    }
}

long long hs_now() { return (long long)time(nullptr); }

void hs_fetch(std::string name) {
    // display name -> URL form: spaces (including the NON-BREAKING space RS uses
    // in interface names, UTF-8 C2 A0) to underscores, percent-encode the rest
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
        // names become verbatim cache keys in a tab/newline file: only RS-legal characters
        // (RS renders the space in a name as NBSP, UTF-8 C2 A0; hs_fetch maps it, so it is allowed here)
        if (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-Â ") != std::string::npos) continue;
        auto& e = g_hiscores[name];   // default-constructs a state-0 entry if absent
        bool fresh = (e.state != 0) && (now - e.epoch < kHsTtlSec);   // ok OR errored within the last hour
        if (!fresh && !e.inflight) {                                  // >=1h old or never fetched -> one fetch
            e.inflight = true;
            http::Enqueue([name] { hs_fetch(name); });
        }
        if (!first) out << ',';
        first = false;
        // serve any cached body (fresh or stale-being-refreshed) as ok; else pending/error
        const char* s = (e.state == 1) ? "ok" : (e.inflight || e.state == 0) ? "pending" : "error";
        out << '"' << json_escape(name) << "\":{\"s\":\"" << s << '"';
        if (e.state == 1) out << ",\"b\":\"" << json_escape(e.body) << '"';
        out << '}';
    }
    out << '}';
    return utf8_to_js(ctx, out.str());
}

// The RuneTools server host, shared by the party-sync section below and the self-update /
// VoS / world-event code further down. Declared here because party-sync uses it first.
constexpr wchar_t kUpdateHost[]   = L"runetools.io";

// ---- Leagues-only worlds ---------------------------------------------------------------
// Leagues runs its own copy of the world state, so a leagues world's Voice of Seren, world
// events and so on are INDEPENDENT of the main game's. Crowdsourced data must therefore be
// kept apart: a leagues client reporting the Priffdinas hour into the shared pool would
// publish a voice pair nobody on a normal world can act on, and vice versa.
// Owner-captured from the world list (the trophy-marked worlds).
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

// ---- Dungeoneering party-sync: relay facts (examined door levels, fetched hiscores) between
//      party members' clients across PCs via the RuneTools server, keyed by a shared manual CODE.
//      A background SSE loop streams the party channel into a launcher-side cache; the panel reports
//      facts via partyReport and merges the cache via partyData. The cache is a launcher global, so
//      co-located clients share it. See website routes/party.js + services/party-sync.js. ----
namespace {
std::mutex g_party_mu;
std::string g_party_code;                                   // active sync code ("" = off)
std::map<std::string, std::string> g_party_doors;           // "gx,gy" -> "<skill>|<level>"
std::map<std::string, std::string> g_party_hiscores;        // name -> levels CSV ("99,74,...")
std::map<std::string, int> g_party_noncrit;                 // "gx,gy" -> 1: user-marked non-critical
std::map<int, int> g_party_critkeys;                        // key idx -> 1 promoted / 0 demoted (manual overrides)
std::map<std::string, int> g_party_critrooms;               // "gx,gy" -> 1: user-marked CRITICAL room
std::atomic<bool> g_party_synced{ false };                  // a server snapshot arrived for the CURRENT code
std::atomic<int> g_party_epoch{ 0 };                        // bump on code change -> old SSE loop exits
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
    // keep only [a-z0-9], lowercased, 4..16
    std::string out;
    for (char c : s) { if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c; }
    return out.size() >= 4 && out.size() <= 16 ? out : std::string();
}

// naive extractors for the small, flat JSON the server sends (no nested objects beyond
// one level, ASCII values) -- avoids pulling in a JSON lib on the launcher side.
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
// levels array "[65,74,null,...]" -> CSV "65,74,,..."
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
    bool on = payload.find("\"on\":true") != std::string::npos;   // absent/false -> unmark
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
    bool on = payload.find("\"on\":true") != std::string::npos;   // 1 = promoted, 0 = demoted
    std::lock_guard<std::mutex> lk(g_party_mu);
    g_party_critkeys[(int)idx] = on ? 1 : 0;
}
// psnapshot: {"doors":[{cell,skill,level},...],"hiscores":[{name,levels},...]}. Replace
// the whole cache by walking each object (split on '}').
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
    // noncrit = a JSON array of "gx,gy" strings; pull each quoted token in that segment
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
    // critkeys = [{idx,on},...] -> scan its {..} objects (critrooms' strings hold no '{')
    auto cseg = j.find("\"critkeys\"");
    if (cseg != std::string::npos) {
        std::size_t i = cseg;
        while ((i = j.find('{', i)) != std::string::npos) {
            auto e = j.find('}', i); if (e == std::string::npos) break;
            party_apply_critkey(j.substr(i, e - i + 1)); i = e + 1;
        }
    }
    // critrooms = ["gx,gy",...] like noncrit
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
    g_party_synced.store(true);   // the cache now REFLECTS the server -> mirrors may act on it
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
                if (g_party_epoch.load() != epoch) return false;   // code changed -> drop this stream
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
                            else if (cur_ev == "preset") {   // floor change: floor-scoped facts clear, hiscores persist
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
    int epoch = ++g_party_epoch;                    // invalidate any running loop
    if (code.empty()) return;                        // cleared -> just stop
    bool expected = false;
    if (g_party_loop_running.compare_exchange_strong(expected, true))
        std::thread([epoch, code] { guarded("party events", [&] { party_events_loop(epoch, code); }); }).detach();
    else                                             // a loop is winding down; start fresh once it exits
        std::thread([epoch, code]() { guarded("party restart", [&] {
            // the previous stream only notices the epoch bump when DATA arrives (its next heartbeat can be
            // ~25s away), so wait it out rather than giving up at 2s -- epoch-guarded.
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
    g_party_synced.store(false);   // fresh code -> not a server reflection until its snapshot lands
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
    // kick the loop if a persisted code was just loaded and nothing is streaming yet
    if (!code.empty() && !g_party_loop_running.load()) party_start_loop();
    return utf8_to_js(ctx, code);
}
// Cheap shape check for JS-supplied JSON that gets concatenated into a request body:
// bounded size, starts with { or [, balanced brackets, no control chars outside strings.
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
    if (!json_value_ok(data)) return JSValueMakeBoolean(ctx, false);   // data is spliced raw into the body
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
        // CSV "65,74,,90" -> valid JSON array "[65,74,null,90]" (empty token = null)
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

// ---- Tile markers: persistent, per-account world-tile markers drawn by the overlay.
//      src/launcher/Markers.cpp is the single source of truth for the UI, the keybind and the overlay. ----
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
    rtx::markers::Keybinds kb;   // (markVk, removeVk, color); struct defaults are A / D
    if (argc >= 1) kb.markVk   = js_int(ctx, argv[0]);
    if (argc >= 2) kb.removeVk = js_int(ctx, argv[1]);
    if (argc >= 3) kb.defColor = (std::uint32_t)JSValueToNumber(ctx, argv[2], nullptr);
    rtx::markers::SetKeybinds(kb);
    return JSValueMakeBoolean(ctx, true);
}
// Gates the mark/delete keys on the Markers panel being open, so the A/D defaults never
// place tiles during normal play.
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
    // Unified host window + in-game window UI layer for this client.
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

// Legacy no-op: the sidebar dock is gone (panels render in-game). Kept registered so
// any stale JS caller degrades silently instead of throwing.
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

// rtx.gameFocused(pid) -> bool: is the game (or its host frame) the foreground window.
// rtx.locMorphs(pid, "id,id,...") -> live morph resolution per base loc id (see Reader).
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

// Borderless fullscreen for this client's host window. With one arg, TOGGLES; with two, sets.
// Returns the resulting state, so the caller can paint its control without a second call.
JSValueRef HostFullscreen(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    const bool on = (argc >= 2) ? JSValueToBoolean(ctx, argv[1])
                                : !dock::IsHostFullscreen(pid);
    dock::SetHostFullscreen(pid, on);
    return JSValueMakeBoolean(ctx, dock::IsHostFullscreen(pid));
}

// Legacy no-op: tooltips render inside the in-game UI layer now (one DOM = no
// window-bounds limit). Kept registered for stale callers.
JSValueRef RailTip(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t, const JSValueRef[], JSValueRef*) {
    return JSValueMakeUndefined(ctx);
}

// ---- In-game window manager -------------------------------------------------
// rtx.uiRects(pid, "x,y,w,h;..." CSS px, visible): publish the regions the UI
// claims for input + whether the layer composites at all.
JSValueRef UiRects(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string csv = js_to_utf8(ctx, argv[1]);
    bool visible = (argc >= 3) ? JSValueToBoolean(ctx, argv[2]) : true;
    gameui::SetConsumeRects(pid, csv, visible);
    return JSValueMakeBoolean(ctx, true);
}

// rtx.uiKeyboard(pid, on): a UI text field gained/lost focus; capture keys.
JSValueRef UiKeyboard(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    gameui::SetKeyboardCapture(pid, JSValueToBoolean(ctx, argv[1]));
    return JSValueMakeBoolean(ctx, true);
}

// rtx.uiScale(pid, mul): user UI-scale multiplier on top of the DPI-derived device scale.
JSValueRef UiScale(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 2) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    dock::SetUiScaleMultiplier(pid, JSValueToNumber(ctx, argv[1], nullptr));
    return JSValueMakeBoolean(ctx, true);
}

// rtx.uiClientInfo(pid) -> {"pw","ph","cw","ch","scale","mod"}
JSValueRef UiClientInfo(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return utf8_to_js(ctx, "{}");
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    return utf8_to_js(ctx, gameui::ClientInfoJson(pid).c_str());
}

JSValueRef MyPid(JSContextRef ctx, JSObjectRef, JSObjectRef,
                 size_t, const JSValueRef[], JSValueRef*) {
    // Per-client windows define a global __rtx_pid. Main launcher returns 0.
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

// Whether a running client's JX_ credentials are captured into the vault automatically.
//
// This used to happen unconditionally the first time a client was listed, with no prompt and no
// way to switch it off - the launcher took bearer tokens for an account you had merely logged
// into. Capture stays ON by default so nobody's existing multi-client setup silently stops
// working, but it is now the user's call to make.
//
// Its own tiny file, matching the screenshot keybind: one flag does not justify a settings store.
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

// accountCapture()      -> current state
// accountCapture(bool)  -> set, returns the new state
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
    // rs2client.exe falls back to the rs-launch:// URL handler if it sees a
    // non-empty JX_ACCESS_TOKEN it can't use, so forward only the required
    // JX_ vars with ACCESS/REFRESH tokens forced to empty strings.
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
    // Open the logs folder so the launcher log and every client-<pid>.log are
    // visible together. Fall back to the parent if the folder isn't there yet.
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

// ---- one-click self-update ----------------------------------------------
constexpr wchar_t kLatestPath[]   = L"/api/client/latest-version";
constexpr wchar_t kDownloadPath[] = L"/api/client/download-update";

// Running launcher version from the exe's own VERSIONINFO (app.rc FILEVERSION), the single source of truth.
std::string running_version() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        DWORD ignored = 0, sz = GetFileVersionInfoSizeW(path, &ignored);
        if (sz) {
            std::vector<unsigned char> buf(sz);
            if (GetFileVersionInfoW(path, 0, sz, buf.data())) {
                VS_FIXEDFILEINFO* ffi = nullptr; UINT n = 0;
                if (VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &n) && ffi) {
                    char v[32];
                    std::snprintf(v, sizeof(v), "%u.%u.%u",
                                  (unsigned)HIWORD(ffi->dwFileVersionMS),
                                  (unsigned)LOWORD(ffi->dwFileVersionMS),
                                  (unsigned)HIWORD(ffi->dwFileVersionLS));
                    return v;
                }
            }
        }
    }
    return kAppVersion;
}

// Minimal JSON string-field reader (find "key":"value"); the backend response is flat enough for that.
std::string json_str(const std::string& body, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t k = body.find(pat);
    if (k == std::string::npos) return {};
    size_t c = body.find(':', k + pat.size());
    if (c == std::string::npos) return {};
    size_t i = c + 1;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
    if (i >= body.size() || body[i] != '"') return {};
    ++i;
    std::string out;
    while (i < body.size() && body[i] != '"') {
        if (body[i] == '\\' && i + 1 < body.size()) { out.push_back(body[i + 1]); i += 2; }
        else out.push_back(body[i++]);
    }
    return out;
}

// SHA-256 of a file as lowercase hex ("" on failure); verifies a download against the manifest hash.
std::string sha256_hex(const std::wstring& file) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
    std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cb = 0;
        BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
        std::vector<unsigned char> obj(objLen);
        if (BCryptCreateHash(alg, &h, obj.data(), objLen, nullptr, 0, 0) == 0) {
            std::vector<unsigned char> buf(1 << 16); DWORD read = 0;
            while (ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr) && read)
                BCryptHashData(h, buf.data(), read, 0);
            unsigned char dig[32] = {};
            if (BCryptFinishHash(h, dig, sizeof(dig), 0) == 0) {
                static const char* hx = "0123456789abcdef";
                for (unsigned char d : dig) { out.push_back(hx[d >> 4]); out.push_back(hx[d & 0xf]); }
            }
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(f);
    return out;
}

std::mutex        g_upd_mu;
std::string       g_upd_phase = "idle";   // idle|downloading|verifying|launching|error
int               g_upd_pct   = 0;
std::string       g_upd_detail;
std::atomic<bool> g_upd_running{ false };

void set_upd(const char* phase, int pct, const std::string& detail) {
    std::lock_guard<std::mutex> lk(g_upd_mu);
    g_upd_phase = phase; g_upd_pct = pct; g_upd_detail = detail;
}

// Worker: fetch manifest -> download the build -> verify -> run the per-user installer silently ->
// exit so it can replace the launcher's files (the installer relaunches it).
void run_update() {
    struct Done { ~Done() { g_upd_running = false; } } done;
    auto log = [](const std::string& m) { rtx::log::Launcher("[update] " + m); };
    log("worker start (running v" + running_version() + ")");

    std::vector<http::Header> hdrs;   // public update endpoints; no auth headers

    set_upd("downloading", 0, "Checking update");
    auto man = http::Get(kUpdateHost, kLatestPath, hdrs);
    log("manifest GET ok=" + std::to_string(man.ok) + " status=" + std::to_string(man.status));
    if (!man.ok || man.status != 200) { log("abort: manifest unreachable: " + man.detail); set_upd("error", 0, "Couldn't reach the update server"); return; }
    std::string ver  = json_str(man.body, "version");
    std::string hash = json_str(man.body, "hash");
    log("manifest version=" + ver + " hash=" + hash);
    if (ver.empty()) { log("abort: manifest has no version"); set_upd("error", 0, "No update available"); return; }

    wchar_t tmp[MAX_PATH] = {}; GetTempPathW(MAX_PATH, tmp);
    std::wstring dest = std::wstring(tmp) + L"RuneToolsXSetup.exe";

    set_upd("downloading", 0, "Downloading v" + ver);
    auto dl = http::Download(kUpdateHost, kDownloadPath, hdrs, dest,
        [](long long got, long long total) {
            set_upd("downloading", total > 0 ? (int)((got * 100) / total) : 0, "");
        });
    log("download ok=" + std::to_string(dl.ok) + " status=" + std::to_string(dl.status) + " detail=" + dl.detail);
    if (!dl.ok || dl.status != 200) { log("abort: download failed"); set_upd("error", 0, "Download failed"); return; }

    // FAIL CLOSED. A manifest without a usable hash used to skip verification and run the
    // downloaded exe anyway, which made "omit the hash" a way to turn verification off - the one
    // thing an attacker who could serve the manifest would do first. client_versions.hash is NOT
    // NULL server-side, so a missing hash is a broken manifest, never a normal update.
    set_upd("verifying", 100, "Verifying");
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        DeleteFileW(dest.c_str());
        log("abort: manifest hash missing or malformed (len=" + std::to_string(hash.size()) + ")");
        set_upd("error", 0, "Update verification failed");
        return;
    }
    std::string got = sha256_hex(dest);
    log("verify expected=" + hash + " got=" + got);
    if (got.empty() || _stricmp(got.c_str(), hash.c_str()) != 0) {
        DeleteFileW(dest.c_str());
        log("abort: hash mismatch");
        set_upd("error", 0, "Update verification failed");
        return;
    }

    set_upd("launching", 100, "Installing v" + ver);

    // Inno postinstall [Run] entries don't fire under /VERYSILENT; the dual [Run]
    // in RuneToolsX.iss handles the silent relaunch.
    std::wstring instlog = rtx::log::LogDir();
    if (!instlog.empty() && instlog.back() != L'\\' && instlog.back() != L'/') instlog += L'\\';
    instlog += L"installer.log";
    std::wstring args = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=\"" + instlog + L"\"";

    log("launching installer /VERYSILENT (+/LOG=installer.log), then Sleep(1500) + TerminateProcess");
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", dest.c_str(),
                                 args.c_str(),
                                 nullptr, SW_SHOWNORMAL);
    INT_PTR code = reinterpret_cast<INT_PTR>(rc);
    log("ShellExecuteW returned " + std::to_string((long long)code));
    if (code <= 32) { log("abort: ShellExecuteW failed (<=32)"); set_upd("error", 0, "Couldn't start the installer"); return; }
    Sleep(1500);          // let the installer spin up before this process releases its files
    // Must be TerminateProcess, NOT ExitProcess: ExitProcess runs module detach callbacks, which can
    // deadlock with the Ultralight/GPU/SSE threads live -- the process then hangs, keeps the exe locked,
    // and the update silently fails. TerminateProcess exits instantly.
    log("exiting now (TerminateProcess) so the installer can replace our files");
    rtx::log::BeginShutdown();   // suppress crash reports from threads killed by the terminate below
    TerminateProcess(GetCurrentProcess(), 0);
}

JSValueRef Version(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, running_version());
}

// rtx.bridgeStatus(pid): the one-call health summary behind the client's status strip.
//   {"ok":true,"attached":bool,"reader":"ok|detached|stale","build":"<client version>"|0,"launcher":"x.y.z"}
// Built from the cached snapshot list only (no new reads). detached = the pid is not tracked;
// stale = tracked but the reader cannot classify its login state (status byte unverified),
// which is what a game update that moved the offsets looks like from here.
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

extern std::mutex  g_latest_mu;      // defined just below with the checker
extern std::string g_latest_json;
void refresh_latest();
JSValueRef LatestVersion(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t, const JSValueRef[], JSValueRef*) {
    // Never block the JS thread on the network: serve the background cache. An empty cache
    // returns "{}" (checkForUpdate treats a missing version as "up to date"; the periodic
    // cached poll picks the manifest up once the kicked refresh lands).
    std::string j;
    { std::lock_guard<std::mutex> lk(g_latest_mu); j = g_latest_json; }
    if (j.empty() || j == "{}") { std::thread(refresh_latest).detach(); j = "{}"; }
    return utf8_to_js(ctx, j);
}

// Background update checker: worker threads keep g_latest_json fresh so the UI can poll
// latestVersionCached() at zero network/UI cost. Sources: check-on-start + hourly fallback poll
// (update_checker_loop) and an SSE push (update_events_loop). An SSE event is only a TRIGGER to
// refresh; the manifest GET is the source of truth.
constexpr int     kCheckIntervalMs = 60 * 60'000;   // 1 hour fallback poll (SSE does real-time)
constexpr wchar_t kEventsPath[]    = L"/api/client/events";
std::mutex        g_latest_mu;
std::string       g_latest_json = "{}";
std::atomic<bool> g_checker_started{ false };

// Pull the latest-version manifest into the cache (one small public GET). Shared by the
// fallback poll and the SSE trigger.
void refresh_latest() {
    // Debounce (CAS): the check-on-start poll and the SSE 'hello' both fire at
    // startup within the same second; collapse them into one GET.
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
        refresh_latest();   // first pass = check-on-start; thereafter the slow fallback poll
        for (int slept = 0; slept < kCheckIntervalMs; slept += 1000)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

// Voice of Seren shared state (see the crowdsource block below). Declared here because
// the SSE loop writes pushed `vos` events straight into the cache.
constexpr wchar_t kVosPath[]       = L"/api/vos";
constexpr wchar_t kVosReportPath[] = L"/api/vos/report";
std::mutex             g_vos_mu;
std::string            g_vos_json = "{}";
// When VoS DATA last landed (SSE store or a successful GET) -- the freshness clock behind
// vos_cache_stale()/vos_cache_has_current_hour(). Must NOT move on a failed GET.
std::atomic<long long> g_vos_fetched_ms{ 0 };
// Last GET ATTEMPT (success or not) -- floors the fallback to one try per 5 minutes.
std::atomic<long long> g_vos_get_ms{ 0 };
std::atomic<bool>      g_vos_fetching{ false };
std::atomic<long long> g_vos_reported_ms{ 0 };
// Leagues worlds report into their own pool, so they carry their own 5-minute floor.
std::atomic<long long> g_vos_reported_lg_ms{ 0 };
// True while the SSE stream is delivering (only 200 streams reach the data callback;
// heartbeats arrive every ~25s). While alive, the push + connect handshake fully cover
// VoS/tracker delivery, so the GET fallbacks stay off.
std::atomic<bool>      g_sse_alive{ false };
// Last byte received on the stream. A silently dead TCP connection (sleep/NAT drop)
// keeps g_sse_alive true until WinHTTP's 120s receive timeout fires; gating on byte
// recency caps that fallback-suppression window at ~40s (heartbeat is 25s).
std::atomic<long long> g_sse_last_rx_ms{ 0 };
bool sse_alive_now() {
    return g_sse_alive.load() &&
           ((long long)GetTickCount64() - g_sse_last_rx_ms.load()) < 40'000;
}

// ---- Corrupted Scarab world tracker (same architecture as VoS) ----
// Clients that SEE "Corrupted Scarab" in their scene report their world; the server keeps each
// world for a FIXED 5-minute window from its FIRST sighting (re-reports are ignored). Delivery is
// the `scarab` SSE event (+ connect handshake, always sent, even empty); GET is the slow fallback.
// Payload rows carry absolute expiry timestamps; the server also pushes the pruned payload.
constexpr wchar_t kScarabPath[]       = L"/api/scarabs";
constexpr wchar_t kScarabReportPath[] = L"/api/scarabs/report";
std::mutex             g_scarab_mu;
std::string            g_scarab_json = "{}";
std::atomic<long long> g_scarab_fetched_ms{ 0 };   // when DATA landed (SSE or successful GET)
std::atomic<long long> g_scarab_get_ms{ 0 };       // last GET ATTEMPT (floors the fallback; a failed GET must not fake freshness)
std::atomic<bool>      g_scarab_fetching{ false };

// ---- Menaphos Soul Obelisk tracker (loc 109495; same architecture) ----
// The obelisk spawns at FIXED per-district tiles; a sighting is classified by matching the object's
// tile against the table below. Window: 7.5 minutes from first sighting, no refresh (server-enforced).
constexpr wchar_t kObeliskPath[]       = L"/api/obelisks";
constexpr wchar_t kObeliskReportPath[] = L"/api/obelisks/report";
std::mutex             g_obelisk_mu;
std::string            g_obelisk_json = "{}";
std::atomic<long long> g_obelisk_fetched_ms{ 0 };   // when DATA landed
std::atomic<long long> g_obelisk_get_ms{ 0 };       // last GET ATTEMPT (fallback floor)
std::atomic<bool>      g_obelisk_fetching{ false };

// Stamp the LOCAL arrival time (unix ms, same clock as JS Date.now()) into a cached tracker payload:
// {"rxAt":<ms>,...}. The panel derives the server-clock skew as payload.now - payload.rxAt.
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

// Parse one integer field out of a tiny flat JSON object ({"25158":4,..}); 0 when absent.
int vos_json_int(const std::string& j, const char* key) {
    std::string needle = "\"" + std::string(key) + "\":";
    auto p = j.find(needle);
    if (p == std::string::npos) return 0;
    p += needle.size();
    long v = 0; bool any = false;
    while (p < j.size() && j[p] >= '0' && j[p] <= '9') { v = v * 10 + (j[p] - '0'); ++p; any = true; }
    return any ? (int)v : 0;
}

// Same, but 64-bit: unix-ms timestamps ("now", "rxAt", expiresAt) overflow int.
long long json_ll(const std::string& j, const char* key) {
    std::string needle = "\"" + std::string(key) + "\":";
    auto p = j.find(needle);
    if (p == std::string::npos) return 0;
    p += needle.size();
    long long v = 0; bool any = false;
    while (p < j.size() && j[p] >= '0' && j[p] <= '9') { v = v * 10 + (j[p] - '0'); ++p; any = true; }
    return any ? v : 0;
}

// The braced body of a nested object, e.g. section "lg" of {"a":1,...,"lg":{"a":3,"b":5}}.
// The VoS payload serves the main pool at the top level and leagues under "lg", so a plain
// key search would find the main "a" first; callers scope to the section, then read keys
// from it. Empty when absent. Depth-counted, so a nested object inside would not truncate it.
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

// True when the cached value is too old to cover the current hour: `h` is only 0-23, so a day-old
// value whose hour number matches again would otherwise pass the hour check.
bool vos_cache_stale() {
    long long fetched = g_vos_fetched_ms.load();
    return fetched != 0 && ((long long)GetTickCount64() - fetched) > 65 * 60'000;
}

// True when the community cache already holds a value for the CURRENT UTC hour. `h`
// always precedes `n` in the payload, so match the delimited field ("h":1 must not
// match inside "h":18).
bool vos_cache_has_current_hour() {
    if (vos_cache_stale()) return false;
    SYSTEMTIME st; GetSystemTime(&st);
    std::string needle = "\"h\":" + std::to_string((int)st.wHour);
    std::lock_guard<std::mutex> lk(g_vos_mu);
    bool hourOk = g_vos_json.find(needle + ",") != std::string::npos ||
                  g_vos_json.find(needle + "}") != std::string::npos;
    if (!hourOk) return false;
    // The served value carries "d" = UTC day number (days since epoch), so a value from a previous day
    // at the same hour is rejected. d == 0 = a server that sends no date stamp: hour check alone.
    // Matches JS floor(Date.now()/86400000).
    int d = vos_json_int(g_vos_json, "d");
    if (d == 0) return true;
    long long today = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() / 86400;
    return d == (int)today;
}

// Fire-and-forget report under the shared 5-minute floor (also the JS vosReport path).
// `leagues` selects the pool: leagues worlds rotate their own Voice of Seren, so the two
// are reported and served independently. The floor is PER POOL, or a main report would
// silence a leagues one for five minutes.
bool vos_post_report(int a, int b, bool leagues) {
    if (a < 1 || a > 8 || b < 1 || b > 8 || a == b) return false;
    // Hour-boundary guard: the server stamps a report with ITS OWN arrival hour, so a pair read moments
    // before the top of the hour can land just after it and be served as the NEW hour's voice.
    // Skip the last seconds; callers retry with fresh varbits on their next pass.
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
        // re-select inside: the outer reference must not outlive the frame
        std::atomic<long long>& floorMs = leagues ? g_vos_reported_lg_ms : g_vos_reported_ms;
        std::string body = "{\"a\":" + std::to_string(a) + ",\"b\":" + std::to_string(b) +
                           (leagues ? ",\"l\":1" : "") + "}";
        std::vector<http::Header> hdrs = { { "Content-Type", "application/json" } };
        auto r = http::PostJson(kUpdateHost, kVosReportPath, hdrs, body);
        if (!r.ok || r.status != 200) {
            // A FAILED post must not consume the whole 5-minute floor: rewind the stamp so the next pass
            // may retry ~30s after the failed attempt (CAS: only if no newer report claimed the floor).
            rtx::log::Launcher("vos report failed: status " + std::to_string(r.status) + " " + r.detail);
            long long cur = now;
            floorMs.compare_exchange_strong(cur, now - (5 * 60'000 - 30'000));
        }
    });
    return true;
}

// Corrupted Scarab detection + reporting (view-free C++, alongside the VoS loop). Every 2s,
// world_event_scan_loop scans every in-game client's scene for swarm ids 109473/109475/109477
// (id match, not name: other "Corrupted Scarab" entities exist). The substring match below hits
// both npc rows ({"id":N,"uid":..}) and object rows ({"id":N,"x":..}). The server's 5-minute
// window is anchored to the FIRST sighting and never refreshed, so a world already listed is not
// re-reported; an EXPIRED row no longer counts as listed. A 60s local floor bounds retries.
bool scene_has_scarab(const std::string& scene) {
    static const char* kIds[] = { "\"id\":109473,", "\"id\":109475,", "\"id\":109477," };
    for (const char* n : kIds)
        if (scene.find(n) != std::string::npos) return true;
    return false;
}
// Soul Obelisk: returns the district code when the scene holds loc 109495 at a KNOWN
// spawn tile (small tolerance for the 2x2 footprint anchor), else 0. Object rows print
// {"id":N,"x":N,"y":N,...} (npc rows are {"id":N,"uid":..}, so the needle can't cross).
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
        // Unmapped spot: log the tile so the remaining district coordinates can be captured from sightings.
        rtx::log::Launcher("soul obelisk 109495 sighted at UNMAPPED tile " +
                           std::to_string(x) + "," + std::to_string(y));
        p = py;
    }
    return 0;
}

// True when `world` appears in the payload's "worlds" array AND its window has not expired.
// Scoped to that array on purpose: the payload also carries a "votes" array whose rows start with
// the same [<world>, prefix, so a whole-document search would report a world as listed while it is
// merely being voted on.
// Row shape: [world, ...extra, expiresAtMs] -- the LAST number in the row is the expiry.
// Server-now is estimated as payload.now + (localNow - rxAt); that slightly UNDERSHOOTS the real
// server clock, so "expired here" implies "already pruned there" and cannot race a still-listed row.
bool payload_lists_world(const std::string& json, int world) {
    const std::string key = "\"worlds\":[";
    auto start = json.find(key);
    if (start == std::string::npos) return false;
    start += key.size() - 1;                              // at the '[' opening the array
    // walk to the matching ']' so the sibling "votes" array is never read
    int depth = 0;
    std::size_t end = start;
    for (; end < json.size(); ++end) {
        if (json[end] == '[') ++depth;
        else if (json[end] == ']' && --depth == 0) { ++end; break; }
    }
    const std::string needle = "[" + std::to_string(world) + ",";
    auto hit = json.find(needle, start);
    if (hit == std::string::npos || hit >= end) return false;
    // Last number in this row = expiresAt. Parse conservatively: on any surprise, treat the row as live.
    auto rowEnd = json.find(']', hit);
    if (rowEnd == std::string::npos || rowEnd > end) return true;
    std::size_t q = rowEnd;
    while (q > hit && json[q - 1] >= '0' && json[q - 1] <= '9') --q;
    if (q == rowEnd) return true;                         // no trailing number: treat as live
    long long expiresAt = 0;
    for (std::size_t i = q; i < rowEnd; ++i) expiresAt = expiresAt * 10 + (json[i] - '0');
    long long srvNow = json_ll(json, "now");
    long long rxAt = json_ll(json, "rxAt");
    if (expiresAt <= 0 || srvNow <= 0 || rxAt <= 0) return true;
    using namespace std::chrono;
    long long localNow = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return expiresAt > srvNow + (localNow - rxAt);
}

// One scene read per client per pass, shared by both trackers.
void scarab_scan_pass() {
    static std::unordered_map<int, long long> last_scarab;    // world -> tick (this thread only)
    static std::unordered_map<int, long long> last_obelisk;
    auto snaps = rtx::reader::SampleAll();
    for (const auto& s : snaps) {
        if (s.status != 30 || s.world <= 0) continue;
        // The Soul Obelisk is a RUNTIME (server-spawned) loc with no static map placement, so it only
        // reaches SceneJson through the companion module's published object list. Load the companion here:
        // this scanner runs from launcher start, long before any panel would trigger it.
        rtx::launcher::companion::EnsureLoaded(s.pid);
        // 50-tile scan radius (SceneJson clamps at 64) so the swarm/obelisk is caught as soon as it renders.
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
                    // Log a failed report; fire-and-forget hides it otherwise. Retry rides the normal 60s per-world floor.
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

// Stable per-install voter id (32 hex chars); WorldEventVote appends a per-account hash so accounts
// vote independently. Random, non-identifying, and only ever sent to the vote endpoint.
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

// `kind` is "scarabs" or "obelisks" (the API path segment). Eligibility is enforced by the
// panel only: the server cannot verify a player's position. The voter id is the per-install
// id plus a hash of that client's account (JX_DISPLAY_NAME), so votes dedupe per ACCOUNT
// rather than per machine. Pure hex and non-identifying on the wire.
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
            // Same identity as every per-account store (see account_key_for): an RSN is unique,
            // so hashing it dedupes one vote per ACCOUNT. Env-only left Steam clients with no
            // account component at all, which deduped them per install instead of per player.
            // The Jagex-launcher value is unchanged - AccountKey prefers JX_DISPLAY_NAME - so
            // existing voter hashes stay stable.
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

// Cached obelisk world list -- same serving rules as scarabCached.
JSValueRef ObeliskCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    bool allow_refresh = (argc >= 1) && JSValueToBoolean(ctx, argv[0]);
    if (allow_refresh && sse_alive_now()) allow_refresh = false;
    // Floor on the ATTEMPT clock, not the data clock: stamping fetched_ms at request time would push the
    // next try a full 120s out after a failed GET and make stale data look fresh.
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
                    g_obelisk_fetched_ms.store((long long)GetTickCount64());   // data landed
                }
                g_obelisk_fetching.store(false);
            });
        }
    }
    std::lock_guard<std::mutex> lk(g_obelisk_mu);
    return utf8_to_js(ctx, g_obelisk_json);
}

// Cached scarab world list -- instant; the SSE push keeps it fresh. The GET fallback
// only runs when the JS asks (allowRefresh), the SSE stream is down, and >=2min passed.
JSValueRef ScarabCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                        size_t argc, const JSValueRef argv[], JSValueRef*) {
    bool allow_refresh = (argc >= 1) && JSValueToBoolean(ctx, argv[0]);
    if (allow_refresh && sse_alive_now()) allow_refresh = false;
    // Floor on the ATTEMPT clock, not the data clock (see ObeliskCached).
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
                    g_scarab_fetched_ms.store((long long)GetTickCount64());   // data landed
                }
                g_scarab_fetching.store(false);
            });
        }
    }
    std::lock_guard<std::mutex> lk(g_scarab_mu);
    return utf8_to_js(ctx, g_scarab_json);
}

// Scene-event scanner (scarabs + soul obelisk). Runs on its own fast cadence and scans IMMEDIATELY on
// start: these are short-lived world events (5 / 7.5 min), so a delayed first pass wastes a meaningful
// slice of the window.
void world_event_scan_loop() {
    for (;;) {
        scarab_scan_pass();                               // work FIRST, then sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

// Always-on Voice of Seren reporter. View-free C++ so it runs with no panel open. Once a minute, when
// the community lacks this hour's value, read the three VoS varbits (all one varp) from every attached
// client and report the first valid pair. The SSE push fills the cache right after a successful
// report; a failed post retries under the 5-minute floor.
void vos_report_loop() {
    for (;;) {
        // WORK FIRST, then sleep: a sleep-first loop delays the first report by a full cadence.
        SYSTEMTIME st; GetSystemTime(&st);
        int hr = (int)st.wHour;
        // The cached community pair for THIS hour (0/0 = none). Report to FILL an empty slot OR to CORRECT
        // a mismatch: a live in-Priff read that DISAGREES with the cache must still post, otherwise a
        // wrong-but-present value silences every client for the hour.
        // Both pools: leagues rotates its own voice, so a leagues client is compared against
        // the leagues consensus (nested under "lg") and a normal one against the main value.
        int ca = 0, cb = 0, la = 0, lb = 0;
        bool cacheCurrent = vos_cache_has_current_hour();
        if (cacheCurrent) {
            std::lock_guard<std::mutex> lk(g_vos_mu);
            ca = vos_json_int(g_vos_json, "a");
            cb = vos_json_int(g_vos_json, "b");
            const std::string lg = vos_json_section(g_vos_json, "lg");
            if (!lg.empty()) { la = vos_json_int(lg, "a"); lb = vos_json_int(lg, "b"); }
        }
        // Hour 0 is fine: the pair+stamp varbits stay STALE (never cleared) on leaving Priff, so the
        // `stamp == hr` check below only passes when the client is genuinely in Priff this hour.
        auto snaps = rtx::reader::SampleAll();
        for (const auto& s : snaps) {
            if (s.status != 30) continue;                 // In-game clients only
            const bool lgWorld = is_leagues_world(s.world);
            // Only a client CURRENTLY in Prifddinas has a live varp 4783: it freezes at the last in-Priff value
            // on leaving, so `stamp == hr` alone would re-report a days-old pair (varbit 26416 is only 0-23, no
            // date). Gate on the player being IN the Priff region box (region = tile>>6; X 32-35, Y 51-54).
            int tx = 0, ty = 0, pl = 0;
            if (!rtx::reader::PlayerTile(s.pid, tx, ty, pl)) continue;
            int rx = tx >> 6, ry = ty >> 6;
            if (rx < 32 || rx > 35 || ry < 51 || ry > 54) continue;
            std::string vb = rtx::reader::VarbitsJson(s.pid, "25158,25159,26416");
            int a = vos_json_int(vb, "25158"), b = vos_json_int(vb, "25159");
            int stamp = vos_json_int(vb, "26416");
            if (a >= 1 && a <= 8 && b >= 1 && b <= 8 && a != b && stamp == hr) {
                const int pa = lgWorld ? la : ca, pb = lgWorld ? lb : cb;
                if (!cacheCurrent || a != pa || b != pb) vos_post_report(a, b, lgWorld);   // fill OR correct
                // Do NOT stop the scan here: with clients on both a normal and a leagues
                // world, breaking after the first would leave the other pool unreported.
                continue;
            }
        }
        for (int slept = 0; slept < 15'000; slept += 1000)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

// Live update push: hold a public SSE stream open and refresh the manifest whenever the server emits
// an event ("hello" on connect, "update" on a new publish). Reconnects with backoff. Non-200
// responses are rejected BEFORE the body: draining a 429/5xx error page through this parser would
// count as a clean stream end and reset the backoff.
void update_events_loop() {
    int backoff = 3000;
    std::string buf;
    for (;;) {
        std::vector<http::Header> hdrs = {
            { "Accept", "text/event-stream" },
        };
        buf.clear();
        std::string cur_ev;                                       // event name the next data: line belongs to
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
                    if (line.rfind("event:", 0) == 0) {           // "event: update" / "event: hello" / "event: vos"
                        std::string ev = line.substr(6);
                        if (!ev.empty() && ev.front() == ' ') ev.erase(ev.begin());
                        // Detached: refresh_latest() is a full HTTP GET; running it synchronously here stalls the parse of
                        // whatever sits behind it in the buffer (on connect, the handshake vos/scarab/obelisk events).
                        if (ev == "update" || ev == "hello") std::thread(refresh_latest).detach();
                        cur_ev = ev;
                    } else if (line.rfind("data:", 0) == 0) {
                        // VoS / scarab push: the payload IS the value -- store it directly,
                        // no follow-up GET. Freshness stamp restarts the slow GET fallback.
                        if (cur_ev == "vos" || cur_ev == "scarab" || cur_ev == "obelisk") {
                            std::string dat = line.substr(5);
                            if (!dat.empty() && dat.front() == ' ') dat.erase(dat.begin());
                            // 32KB tracker cap: a full tracker (500 worlds + votes) is ~22KB. Stays well under the
                            // 64KB line-buffer guard.
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
                        cur_ev.clear();                           // event dispatch boundary
                    }
                    // ':' heartbeat comments need no action.
                }
                if (buf.size() > 64 * 1024) buf.clear();          // guard an unterminated flood
                return true;
            },
            [](int status) { return status == 200; });            // reject 429/5xx before the body
        g_sse_alive.store(false);                                 // stream ended; fallback GETs may resume
        // Clean end of a 200 stream -> quick 3s reconnect. Anything else (transport
        // failure OR a rejected status) -> exponential backoff to 60s.
        backoff = (r.ok && r.status == 200) ? 3000 : (backoff * 2 > 60000 ? 60000 : backoff * 2);
        // Log genuine REJECTIONS only (429/5xx). A 200 stream that died mid-body is a
        // routine network drop and would spam the log on every reconnect cycle.
        if (r.status != 0 && r.status != 200)
            rtx::log::Launcher("sse stream rejected: status " + std::to_string(r.status) +
                               ", reconnect in " + std::to_string(backoff / 1000) + "s");
        for (int slept = 0; slept < backoff; slept += 500)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Instant, no network; the background threads are spun up here on first use.
JSValueRef LatestVersionCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t, const JSValueRef[], JSValueRef*) {
    bool expected = false;
    if (g_checker_started.compare_exchange_strong(expected, true)) {
        std::thread([] { guarded("update checker", update_checker_loop); }).detach();   // check-on-start + fallback poll
        std::thread([] { guarded("update events", update_events_loop); }).detach();     // live SSE push
        std::thread([] { guarded("vos report", vos_report_loop); }).detach();           // always-on Voice of Seren reporter
        std::thread([] { guarded("world event scan", world_event_scan_loop); }).detach();  // scarab + soul-obelisk scene scanner
    }
    std::lock_guard<std::mutex> lk(g_latest_mu);
    return utf8_to_js(ctx, g_latest_json);
}

// ---- Voice of Seren crowdsource ----
// The VoS varbits (25158/25159 pair + 26416 hour stamp) only update while the player is in or
// near Prifddinas; elsewhere the client's copies freeze at the last in-Priff values. A client
// that CAN read them reports the pair; everyone else receives it passively via the `vos` SSE
// push plus the connect handshake. Cached reads are instant; a refresh GET and a report are
// both floored to one per 5 minutes.
JSValueRef VosCached(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    bool allow_refresh = (argc >= 1) && JSValueToBoolean(ctx, argv[0]);
    // While the SSE stream is alive (sse_alive_now caps the silently-dead-socket window at ~40s),
    // delivery is push-only: the connect handshake sent the current value and any change arrives as a
    // `vos` event, so a missing value here just means nobody has reported this hour yet.
    if (allow_refresh && sse_alive_now()) allow_refresh = false;
    // HARD RULE: once the cache holds a value for the CURRENT UTC hour, never hit the
    // network again this hour -- only the SSE push or the hour flip changes anything.
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
                    g_vos_fetched_ms.store((long long)GetTickCount64());   // data landed: freshness clock
                }
                g_vos_fetching.store(false);
            });
        }
    }
    // A value older than 65 minutes cannot be current (h is only 0-23 and would wrap
    // across days) -- serve empty so the panel never displays a day-old pair.
    if (vos_cache_stale()) return utf8_to_js(ctx, "{}");
    std::lock_guard<std::mutex> lk(g_vos_mu);
    return utf8_to_js(ctx, g_vos_json);
}

// Is `world` a leagues-only world? One source of truth for the launcher and the panels,
// so the two can never disagree about which pool a report belongs to.
JSValueRef IsLeaguesWorld(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    return JSValueMakeBoolean(ctx, is_leagues_world(js_int(ctx, argv[0])));
}

// The whole list, for panels that want to show or filter by it.
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

// vosReport(a, b [, leagues]) -- the third argument selects the leagues pool. Omitted =
// main, so an older caller keeps its existing behaviour.
JSValueRef VosReport(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    int a = (argc >= 1) ? js_int(ctx, argv[0]) : 0;
    int b = (argc >= 2) ? js_int(ctx, argv[1]) : 0;
    bool lg = (argc >= 3) && JSValueToBoolean(ctx, argv[2]);
    return JSValueMakeBoolean(ctx, vos_post_report(a, b, lg));
}

// Number of running rs2client.exe processes. The update banner checks this BEFORE
// installing: a running client keeps the in-process module mapped, so the installer
// can't replace the launcher's files and the update would half-apply.
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

// Idempotent; the caller polls updateState().
JSValueRef StartUpdate(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    bool expected = false;
    if (g_upd_running.compare_exchange_strong(expected, true)) {
        set_upd("downloading", 0, "Starting");
        std::thread([] { guarded("update", run_update); }).detach();
    }
    return JSValueMakeBoolean(ctx, true);
}

JSValueRef UpdateState(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    std::lock_guard<std::mutex> lk(g_upd_mu);
    std::ostringstream o;
    o << "{\"phase\":\"" << g_upd_phase << "\",\"pct\":" << g_upd_pct
      << ",\"detail\":\"" << json_escape(g_upd_detail) << "\"}";
    return utf8_to_js(ctx, o.str());
}

// Ultralight has no navigator.clipboard; read CF_UNICODETEXT directly.
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

// Ultralight has no clipboard integration at all, so the panel forwards Ctrl+C /
// right-click Copy selections here.
JSValueRef CopyClipboard(JSContextRef ctx, JSObjectRef, JSObjectRef,
                         size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::string text = get_string_arg(ctx, argc, argv, 0);
    int wn = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wn <= 0 || !OpenClipboard(nullptr)) return JSValueMakeBoolean(ctx, false);
    bool ok = false;
    if (EmptyClipboard()) {
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wn * sizeof(wchar_t))) {
            if (auto* w = static_cast<wchar_t*>(GlobalLock(h))) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, w, wn);
                GlobalUnlock(h);
                ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;   // clipboard owns h on success
                if (!ok) GlobalFree(h);
            } else {
                GlobalFree(h);
            }
        }
    }
    CloseClipboard();
    return JSValueMakeBoolean(ctx, ok);
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

// ---- wiki browser (in-client, runescape.wiki only) ----
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

// ---- Alerts: sound playback + config persistence -------------------------
// Ultralight has no HTML5 audio / Web Audio, so the UI calls back here to play
// a bundled WAV (staged in <exe>/sounds by the build). The name is reduced to a
// bare filename so a UI bug can't reach outside the sounds folder.
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
    // SND_ASYNC: return immediately; SND_NODEFAULT: silence (not the system
    // ding) if the WAV can't be opened. A new call pre-empts a still-playing one.
    BOOL ok = PlaySoundW(p.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    return JSValueMakeBoolean(ctx, ok ? true : false);
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

// Per-account config path, keyed on the account identity above (same as the bank cache).
// EMPTY when the account can't be resolved, so no other account's file is read and nothing shared is
// written. A panel always passes its myPid(); pid 0 = no game.
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

// Per-account JSON store for an arbitrary feature folder `sub` (e.g. "goals"),
// keyed on JX_DISPLAY_NAME like the alerts/bank caches. Empty when unresolved.
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

// Per-account JSON blob stores. The UI owns each schema; the blob is persisted verbatim.
// arg0 = pid, so an unresolved account neither reads nor writes anything.
JSValueRef AlertsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                      size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    // Strictly the account's own file: a new/unresolved account starts from UI defaults ("{}").
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

// In-game window-manager layout ({v:1, menu:{...}, windows:{tab:{x,y,w,h,...}}}). Written
// ATOMICALLY (tmp + rename): a drag session rewrites this file often, and a torn write here
// would lose the user's whole window arrangement.
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
    if (ec) {   // cross-volume or locked target: fall back to replace-in-place
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return JSValueMakeBoolean(ctx, !ec);
}

// ---- Cache Explorer snapshots -------------------------------------------------------------
// Scans of the game cache are large and only change when the game does, so they are written
// to %USERPROFILE%\RuneToolsX\cachex\<name>.json rather than kept in the renderer. Machine
// wide on purpose: the cache is not per-account. `name` is reduced to [a-z0-9] so a caller
// cannot walk out of the folder.
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
    // Empty body clears the snapshot, so the panel's Clear button needs no separate call.
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
    std::filesystem::rename(tmp, p, ec);          // atomic swap; a torn file would not parse
    return JSValueMakeBoolean(ctx, !ec);
}

JSValueRef NotesLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                     size_t argc, const JSValueRef argv[], JSValueRef*) {
    std::uint32_t pid = (argc >= 1) ? (std::uint32_t)JSValueToNumber(ctx, argv[0], nullptr) : 0;
    std::string s = alerts_read_file(account_store_path(pid, L"notes"));
    return utf8_to_js(ctx, s.empty() ? std::string("[]") : s);
}

// Plugin permission grants, PER ACCOUNT. They used to live in localStorage under a bare
// plugin id, which is machine-wide: consenting once on one character silently authorised the
// plugin for every character on the machine, including ones the user never opened it on.
// Per-account also means the consent survives the ui-assets re-extract that wipes
// localStorage on every app update, so users are not re-prompted for everything on upgrade.
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
    if (p.empty()) return JSValueMakeBoolean(ctx, false);   // account unresolved: refuse, JS retries
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

// Entity uids change every session, so pinned nameplates persist by player NAME and are
// re-applied to whatever uid a matching player has when they next come into range.
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

// ---- CS2 Scripts panel (Cs2Browser.cpp): sidecar extraction + search/view ----
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

// Launcher-level config, NOT per-account: which accounts skip auto-opening a client
// panel window.
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

// Global (not per-account) Vars-tab pin list, kept on disk so pins survive app updates --
// localStorage is wiped whenever the ui-assets are re-extracted.
JSValueRef VarPinsLoad(JSContextRef ctx, JSObjectRef, JSObjectRef,
                       size_t, const JSValueRef[], JSValueRef*) {
    std::string s = alerts_read_file(launcher_cfg_path(L"varpins.json"));
    return utf8_to_js(ctx, s.empty() ? std::string("[]") : s);
}
// Durable UI preferences, machine-wide. localStorage is NOT durable here: it is wiped
// whenever ui-assets are re-extracted, which happens on every app update, so anything a user
// would resent re-doing (a hand-collected sound mute list, overlay toggles) has to live on
// disk. Var pins and menu rules were moved off localStorage for exactly this reason; this is
// the general store so the next setting does not need its own bridge pair.
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
    // Temp-plus-rename: this is rewritten on every preference change, and a torn write
    // would lose every setting at once rather than the one being changed.
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

// Menu-rule authoring from an id alone (no hovering): name + right-click options for
// kind 0 item / 1 loc / 2 npc, and the reverse name -> ids lookup used to key a hovered
// NPC on its config id instead of its throwaway instance index.
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

// Global (not per-account) right-click menu reorder rules. Same reasoning as the Vars pins:
// the dock's localStorage does not survive a ui-asset re-extract, and on a build without the
// WebCore cache path it does not survive a reload at all, so the rules have to live on disk.
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

// autoSkills = rows appear as skills gain XP; otherwise mask bit i = always show skill i.
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

// Payload format: records separated by '\x1e', fields by '\x1f':
//   gx, gy, plane, label   then OPTIONAL   snap, rgb, gx2, gy2, region
// label may hold '\n' for a multi-line panel; rgb is decimal 0xRRGGBB (0 = default
// colour); gx2/gy2 = flat area extent; region 1 merges same-label marks into one flat
// zone of their loc footprints. Empty payload clears.
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
        // Optional fields after the label: snap-to-live-object flag, rgb, area gx2/gy2.
        // Labels never contain \x1f (callers sanitise it out), so a 4th separator
        // unambiguously starts the optional tail.
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
    // Optional 3rd/4th args: banner slot, and 0xRRGGBB accent (-1 / absent = the default red).
    // Both omitted is the original single-banner behaviour, so existing callers are unchanged.
    int slot = (argc >= 3) ? js_int(ctx, argv[2]) : 0;
    int rgb  = (argc >= 4) ? js_int(ctx, argv[3]) : -1;
    rtx::overlay::SetCenterText(pid, js_to_utf8(ctx, argv[1]), slot, rgb);
    return JSValueMakeBoolean(ctx, true);
}

// Coords are client pixels; w<=0 clears. Drawn directly, with no world projection.
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

// SEVERAL boxes at once: "x,y,w,h" records separated by ';'. An empty string clears.
// guideMarks). An empty string clears. Replaces the whole set, so a caller redraws its own
// rects each update rather than adding to them.
JSValueRef UiHighlightsFn(JSContextRef ctx, JSObjectRef, JSObjectRef,
                          size_t argc, const JSValueRef argv[], JSValueRef*) {
    if (argc < 1) return JSValueMakeBoolean(ctx, false);
    auto pid = static_cast<std::uint32_t>(JSValueToNumber(ctx, argv[0], nullptr));
    std::string s = (argc >= 2) ? js_to_utf8(ctx, argv[1]) : std::string();
    std::vector<rtx::overlay::UiHighlight> v;
    std::size_t pos = 0;
    while (pos < s.size() && v.size() < 64) {          // cap: a plugin cannot flood the frame
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

// Payload "x,y,w,h,Label|..."; empty clears. Re-sent each tick so the boxes track the
// live varcs as panels move.
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
            // parse "x,y,w,h,label" -- first 4 commas are the ints, the remainder is the label.
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

// Payload "x,y,w,h,step;..."; empty clears. step 0 = the immediate next tile.
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
            int v[6] = {0,0,0,0,0,-1}; std::size_t fp = 0; int fi = 0;   // v[5] = optional label number (knot click count); -1 = use step+1
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

// Payload "x,y,w,h,count;..."; empty clears.
// Skills-panel XP bars: "x,y,w,h,pct[,rgb];..." per cell (pct = tenths of a percent, 0..1000).
// Empty payload clears. Same shape as knotCells, one optional field.
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
            if (fi < 5) continue;               // rgb optional
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

// ttl_ms 0 = sticky until clicked; > 0 = auto-dismiss after ttl_ms. Identical text is
// deduped overlay-side.
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

// ===================== Plugin SDK (host side) =====================
// Installed on window.rtx of the MAIN FRAME only; plugin iframes never see
// window.rtx. The trusted in-panel broker (client.html) calls these on the
// plugin's behalf, gated by a method allowlist + manifest scopes. See PLUGIN_SDK.md.

// Plugin id sanitizer (reverse-DNS charset). Empty result = invalid so a bad id
// can never traverse out of the plugin roots.
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

// Storage key sanitizer (alnum, '-', '_'); empty = invalid.
std::string sanitize_plugin_key(const std::string& key) {
    std::string out;
    for (char c : key) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out.push_back(c);
    }
    return out;
}

// Per-(account, plugin) storage dir: %USERPROFILE%/RuneToolsX/plugin-data/<acct>/<id>/.
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

// Dev-sideload root: %USERPROFILE%/RuneToolsX/plugins-dev/.
std::filesystem::path plugin_dev_root() {
    auto dir = alerts_user_dir();
    if (dir.empty()) return {};
    dir /= L"plugins-dev";
    return dir;
}

constexpr std::size_t kPluginStoreMaxBytes = 256 * 1024;       // per-key value cap
constexpr std::size_t kPluginEntryMaxBytes = 2 * 1024 * 1024;  // entry-file cap

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

// Cheap change fingerprint of the dev-sideload root, polled by the UI so a folder
// dropped into plugins-dev (or an edited entry file) shows up without a client
// restart. One token per plugin dir: <id>:<fileCount>:<maxWriteTime>;. Any
// create, edit or delete changes the string; the UI only reacts when it differs.
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
                if (++n > 512) break;              // runaway-dir guard; count change still stamps
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

// entryFile must be a bare filename inside the plugin dir (no separators, no "..").
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

// ===================== Plugin marketplace (in-client install) =====================
// Downloads a signed bundle, verifies SHA-256 + an ECDSA P-256 signature against
// the PINNED public key, then extracts manifest.json + the entry HTML into
// %USERPROFILE%/RuneToolsX/plugins/<id>/ (same shape as a dev-sideload folder, so
// the loader treats it uniformly). A bundle that fails verification is rejected.

constexpr wchar_t kPluginListPath[] = L"/api/plugins/client/list";

// Pinned ECDSA P-256 public key (raw X||Y, 64 bytes) -- the server's signing key.
// Bundles whose signature doesn't verify against this are refused.
static const unsigned char kPluginPubKey[64] = {
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

std::string sha256_hex_buf(const std::uint8_t* d, std::size_t n) {
    std::uint8_t dig[32];
    if (!crypto::Sha256(d, n, dig)) return {};
    static const char* hx = "0123456789abcdef";
    std::string o;
    for (auto b : dig) { o.push_back(hx[b >> 4]); o.push_back(hx[b & 0xf]); }
    return o;
}

std::string install_plugin(const std::string& slug) {
    std::string id = sanitize_plugin_id(slug);
    if (id.empty()) return "Invalid plugin id";

    std::wstring path = L"/api/plugins/" + std::wstring(id.begin(), id.end()) + L"/download";
    auto r = http::Fetch(kUpdateHost, path, {});   // public endpoint; no auth
    if (!r.ok || r.status != 200) return "Download failed (" + std::to_string(r.status) + ")";

    const std::uint8_t* body = (const std::uint8_t*)r.body.data();
    std::size_t blen = r.body.size();

    // 1) integrity: SHA-256 must match the header.
    std::string wantHash = r.header("X-Plugin-Hash");
    std::string gotHash  = sha256_hex_buf(body, blen);
    if (wantHash.empty() || gotHash.empty() || _stricmp(wantHash.c_str(), gotHash.c_str()) != 0)
        return "Integrity check failed";

    // 2) authenticity: ECDSA signature over the exact bytes vs the pinned key.
    std::vector<std::uint8_t> sig;
    if (!plugin_b64_decode(r.header("X-Plugin-Signature"), sig) || sig.size() != 64)
        return "Missing/invalid signature";
    if (!crypto::VerifyEcdsaP256(body, blen, sig.data(), sig.size(), kPluginPubKey, sizeof(kPluginPubKey)))
        return "Signature verification failed";

    // 3) extract manifest + entry.
    std::string manStr;
    if (!zip::ExtractFile(body, blen, "manifest.json", manStr) || manStr.empty())
        return "Bundle missing manifest.json";
    if (sanitize_plugin_id(json_str(manStr, "id")) != id) return "Manifest id mismatch";
    std::string entryName = json_str(manStr, "entry");
    if (entryName.empty()) entryName = "index.html";
    if (entryName.find('/') != std::string::npos || entryName.find('\\') != std::string::npos ||
        entryName.find("..") != std::string::npos) return "Invalid entry filename";
    std::string entryHtml;
    if (!zip::ExtractFile(body, blen, entryName, entryHtml) || entryHtml.empty())
        return "Bundle missing entry file";

    // 4) write to plugins/<id>/.
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

// Marketplace list: fetched on a DETACHED background thread so the HTTP round-trip NEVER
// runs on the Ultralight JS/render thread (a blocking http::Get there froze the panel grey
// and faulted). served()/ReadAsync is NOT usable here: its cold path builds inline, so the
// first call would still block on the network. The bridge call returns the cached body
// immediately ("{}" until the first fetch lands); the JS Browse view polls until it fills.
std::mutex   g_pluginListMu;
std::string  g_pluginListBody;              // last good JSON ("" = never fetched)
std::string  g_pluginListErr;               // last fetch failure ("" = none); shown by the UI
bool         g_pluginListInFlight = false;
ULONGLONG    g_pluginListAt = 0;            // GetTickCount64() of the last success

JSValueRef PluginMarketList(JSContextRef ctx, JSObjectRef, JSObjectRef,
                            size_t, const JSValueRef[], JSValueRef*) {
    std::string body, err;
    {
        std::lock_guard<std::mutex> lk(g_pluginListMu);
        body = g_pluginListBody;
        err  = g_pluginListErr;
        const ULONGLONG now = GetTickCount64();
        // Kick a refresh when nothing is cached or the cache is >60s old, one at a time.
        if (!g_pluginListInFlight && (g_pluginListBody.empty() || now - g_pluginListAt > 60000)) {
            g_pluginListInFlight = true;
            http::Enqueue([] {
                auto r = http::Get(kUpdateHost, kPluginListPath, {});   // public endpoint; no auth
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
    // A stale-but-good body still beats an error: only report failure when we have
    // nothing to show, so a blip during the 60s refresh doesn't blank the panel.
    if (body.empty() && !err.empty())
        return utf8_to_js(ctx, "{\"error\":\"" + json_escape(err) + "\"}");
    return utf8_to_js(ctx, body.empty() ? std::string("{}") : body);
}

// Install downloads + verifies + extracts the bundle -- also an HTTP round-trip, so it runs
// on a DETACHED thread for the same reason as the list fetch (a blocking install froze the
// panel on click). Returns "pending"|"busy" immediately; the JS polls pluginInstallStatus().
std::mutex   g_installMu;
std::string  g_installResult;               // "" while running, "ok" on success, else error text
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
        // long download: its own thread so it cannot starve the 2 pool workers; a throw
        // becomes an error string (and still clears the in-flight flag) instead of a crash
        std::string err = "install failed";
        guarded("plugin install", [&] { err = install_plugin(slug); });   // "" = success
        std::lock_guard<std::mutex> lk(g_installMu);
        g_installResult = err.empty() ? std::string("ok") : err;
        g_installInFlight = false;
    }).detach();
    return utf8_to_js(ctx, std::string("pending"));
}

// Poll the background install: "pending" while running, "ok" on success, "idle" when nothing
// has run, else the error text from the last attempt.
JSValueRef PluginInstallStatus(JSContextRef ctx, JSObjectRef, JSObjectRef,
                               size_t, const JSValueRef[], JSValueRef*) {
    std::lock_guard<std::mutex> lk(g_installMu);
    if (g_installInFlight) return utf8_to_js(ctx, std::string("pending"));
    return utf8_to_js(ctx, g_installResult.empty() ? std::string("idle") : g_installResult);
}

// Installed (signed) plugins live under plugins/<id>/ -- list/read mirror the dev ones.
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

}  // namespace

// Called from the Dock host window proc on a key down-edge. ScreenshotVk() returns 0
// when unbound, so the Dock check is skipped.
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
// Raises the in-game toast on completion. Safe to call off the UI path.
bool CaptureScreenshotForPid(std::uint32_t pid) {
    return !capture_for_pid(pid).empty();
}

// Release a client's HUD section (view + handle) on teardown so the launcher doesn't
// accumulate one per client pid ever launched. External linkage: called from dock Detach.
void HudClose(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_hudMu);
    auto it = g_hudMaps.find(pid);
    if (it == g_hudMaps.end()) return;
    if (it->second.s) UnmapViewOfFile(it->second.s);
    if (it->second.h) CloseHandle(it->second.h);
    g_hudMaps.erase(it);
}

// Per-account unified-window placement, keyed on JX_DISPLAY_NAME; "x y" text file.
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

    // Unlock the saved-accounts vault on first attach (no sign-in required).
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
    install_fn(ctx, ns, "workbenchItems",    WorkbenchItems);
    install_fn(ctx, ns, "sceneEntities",     SceneEntities);
    install_fn(ctx, ns, "playerInfo",        PlayerInfo);
    install_fn(ctx, ns, "gameTick",          GameTick);
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
    install_fn(ctx, ns, "membership",        Membership);
    install_fn(ctx, ns, "quests",            Quests);
    install_fn(ctx, ns, "varpsDumpAll",      VarpsDumpAll);
    install_fn(ctx, ns, "varcsDumpAll",      VarcsDumpAll);
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
    install_fn(ctx, ns, "vosCached", VosCached);
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
    install_fn(ctx, ns, "hiddenPanelsLoad",  HiddenPanelsLoad);
    install_fn(ctx, ns, "hiddenPanelsSave",  HiddenPanelsSave);
    install_fn(ctx, ns, "varPinsLoad",       VarPinsLoad);
    install_fn(ctx, ns, "varPinsSave",       VarPinsSave);
    install_fn(ctx, ns, "prefsLoad",         PrefsLoad);
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

    // Plugin SDK host functions: reachable by the trusted in-panel broker only -- plugin
    // iframes never see window.rtx.
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

}

}  // namespace rtx::launcher
