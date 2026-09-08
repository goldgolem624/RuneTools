#include "Update.h"
#include "BridgeUtil.h"
#include "Crypto.h"
#include "Http.h"
#include "../shared/Log.h"

#include <Windows.h>
#include <ShellAPI.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <random>                   // random_device: unpredictable installer temp name
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")

namespace rtx::launcher {

static constexpr wchar_t kDownloadPath[] = L"/api/client/download-update";

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

static std::mutex        g_upd_mu;
static std::string       g_upd_phase = "idle";   // idle|downloading|verifying|launching|error
static int               g_upd_pct   = 0;
static std::string       g_upd_detail;
static std::atomic<bool> g_upd_running{ false };

static void set_upd(const char* phase, int pct, const std::string& detail) {
    std::lock_guard<std::mutex> lk(g_upd_mu);
    g_upd_phase = phase; g_upd_pct = pct; g_upd_detail = detail;
}

static void run_update() {
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

    // Signed manifest: rtx-client-update-v1\n<version>\n<hash>\n<size> under the pinned key.
    {
        std::string size;
        {
            size_t k = man.body.find("\"size\"");
            size_t c = k == std::string::npos ? k : man.body.find(':', k);
            if (c != std::string::npos) { for (size_t i = c + 1; i < man.body.size(); ++i) { char ch = man.body[i]; if (ch == ' ') continue; if (ch >= '0' && ch <= '9') size.push_back(ch); else break; } }
        }
        std::vector<std::uint8_t> sig;
        std::string lhash = hash; for (auto& ch : lhash) ch = (char)tolower((unsigned char)ch);
        std::string msg = "rtx-client-update-v1\n" + ver + "\n" + lhash + "\n" + (size.empty() ? "0" : size);
        if (!plugin_b64_decode(json_str(man.body, "sig"), sig) || sig.size() != 64 ||
            !crypto::VerifyEcdsaP256((const std::uint8_t*)msg.data(), msg.size(), sig.data(), sig.size(), kPluginPubKey, sizeof(kPluginPubKey))) {
            log("abort: manifest signature missing or invalid");
            set_upd("error", 0, "Update manifest failed verification");
            return;
        }
    }

    {
        auto parts = [](const std::string& s) {
            std::vector<long> v; long cur = 0; bool any = false;
            for (char c : s) {
                if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
                else if (c == '.') { v.push_back(any ? cur : 0); cur = 0; any = false; }
            }
            if (any) v.push_back(cur);
            while (v.size() < 4) v.push_back(0);
            return v;
        };
        const auto nv = parts(ver), rv = parts(running_version());
        if (!(nv > rv)) {   // lexicographic on equal-length numeric vectors
            log("abort: manifest version " + ver + " is not newer than running " + running_version());
            set_upd("error", 0, "No update available");
            return;
        }
    }

    wchar_t tmp[MAX_PATH] = {}; GetTempPathW(MAX_PATH, tmp);
    wchar_t rnd[24] = {};
    {
        std::random_device rd;   // MSVC: cryptographic (RtlGenRandom-backed)
        swprintf_s(rnd, L"_%08x%08x", (unsigned)rd(), (unsigned)rd());
    }
    std::wstring dest = std::wstring(tmp) + L"RuneToolsXSetup" + rnd + L".exe";

    set_upd("downloading", 0, "Downloading v" + ver);
    auto dl = http::Download(kUpdateHost, kDownloadPath, hdrs, dest,
        [](long long got, long long total) {
            set_upd("downloading", total > 0 ? (int)((got * 100) / total) : 0, "");
        });
    log("download ok=" + std::to_string(dl.ok) + " status=" + std::to_string(dl.status) + " detail=" + dl.detail);
    if (!dl.ok || dl.status != 200) { log("abort: download failed"); set_upd("error", 0, "Download failed"); return; }

    set_upd("verifying", 100, "Verifying");
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        DeleteFileW(dest.c_str());
        log("abort: manifest hash missing or malformed (len=" + std::to_string(hash.size()) + ")");
        set_upd("error", 0, "Update verification failed");
        return;
    }
    // TOCTOU guard: FILE_SHARE_READ only, held from before hashing until the installer is running.
    HANDLE guard = CreateFileW(dest.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (guard == INVALID_HANDLE_VALUE) {
        DeleteFileW(dest.c_str());
        log("abort: could not lock the downloaded installer for verification");
        set_upd("error", 0, "Update verification failed");
        return;
    }
    std::string got = sha256_hex(dest);
    log("verify expected=" + hash + " got=" + got);
    if (got.empty() || _stricmp(got.c_str(), hash.c_str()) != 0) {
        CloseHandle(guard);
        DeleteFileW(dest.c_str());
        log("abort: hash mismatch");
        set_upd("error", 0, "Update verification failed");
        return;
    }

    set_upd("launching", 100, "Installing v" + ver);

    // Inno postinstall [Run] does not fire under /VERYSILENT; RuneToolsX.iss handles the relaunch.
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
    if (code <= 32) { CloseHandle(guard); log("abort: ShellExecuteW failed (<=32)"); set_upd("error", 0, "Couldn't start the installer"); return; }
    Sleep(1500);          // let the installer spin up before this process releases its files
    CloseHandle(guard);   // the installer is running from the verified bytes now
    // TerminateProcess, not ExitProcess: DLL detach can deadlock with live Ultralight/GPU threads.
    log("exiting now (TerminateProcess) so the installer can replace our files");
    rtx::log::BeginShutdown();
    TerminateProcess(GetCurrentProcess(), 0);
}

JSValueRef Version(JSContextRef ctx, JSObjectRef, JSObjectRef,
                   size_t, const JSValueRef[], JSValueRef*) {
    return utf8_to_js(ctx, running_version());
}

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

}  // namespace rtx::launcher
