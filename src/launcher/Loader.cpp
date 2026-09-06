#include "Loader.h"
#include "../shared/Log.h"

#include <Windows.h>
#include <SoftPub.h>
#include <wincrypt.h>
#include <wintrust.h>
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace rtx::launcher::loader {

namespace {

std::string last_error_message(DWORD err) {
    LPSTR buf = nullptr;
    DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buf, 0, nullptr);
    std::string out;
    if (n && buf) {
        out.assign(buf, n);
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
            out.pop_back();
    }
    if (buf) LocalFree(buf);
    std::ostringstream os;
    os << "Win32 error " << err;
    if (!out.empty()) os << " (" << out << ")";
    return os.str();
}

}  // namespace

namespace {

std::string w2u(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Read a REG_SZ / REG_EXPAND_SZ value; empty on any failure. `view` is KEY_WOW64_64KEY /
// KEY_WOW64_32KEY (or 0 for HKCU / the default view).
std::wstring reg_str(HKEY root, const wchar_t* subkey, const wchar_t* value, REGSAM view) {
    HKEY h{};
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | view, &h) != ERROR_SUCCESS) return {};
    wchar_t buf[1024]; DWORD cb = sizeof(buf), type = 0;
    LONG rc = RegQueryValueExW(h, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(h);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    std::wstring s(buf, cb / sizeof(wchar_t));
    while (!s.empty() && s.back() == L'\0') s.pop_back();
    return s;
}

// InstallLocation of a program found by DisplayName across the Uninstall keys (HKLM 64/32, HKCU).
std::wstring uninstall_install_location(const wchar_t* displayName) {
    const wchar_t* kUninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    struct Spot { HKEY root; REGSAM view; };
    const Spot spots[] = {
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY },
        { HKEY_CURRENT_USER,  0 },
    };
    for (const auto& sp : spots) {
        HKEY h{};
        if (RegOpenKeyExW(sp.root, kUninstall, 0, KEY_READ | sp.view, &h) != ERROR_SUCCESS) continue;
        std::wstring found;
        wchar_t sub[512]; DWORD idx = 0, len = 512;
        while (RegEnumKeyExW(h, idx++, sub, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            len = 512;
            std::wstring keypath = std::wstring(kUninstall) + L"\\" + sub;
            if (_wcsicmp(reg_str(sp.root, keypath.c_str(), L"DisplayName", sp.view).c_str(), displayName) == 0) {
                found = reg_str(sp.root, keypath.c_str(), L"InstallLocation", sp.view);
                if (!found.empty()) break;
            }
        }
        RegCloseKey(h);
        if (!found.empty()) return found;
    }
    return {};
}

// Steam library roots: the Steam install dir + every "path" listed in libraryfolders.vdf
// (so a game moved to another drive / a custom-named library is still found).
std::vector<std::wstring> steam_libraries() {
    std::vector<std::wstring> libs;
    std::wstring steam = reg_str(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0);
    if (steam.empty())
        steam = reg_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", KEY_WOW64_32KEY);
    if (steam.empty()) return libs;
    libs.push_back(steam);
    std::ifstream f(std::filesystem::path(steam + L"\\steamapps\\libraryfolders.vdf"));
    std::string line;
    while (f && std::getline(f, line)) {
        auto pk = line.find("\"path\"");
        if (pk == std::string::npos) continue;
        auto q1 = line.find('"', pk + 6);
        if (q1 == std::string::npos) continue;
        auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string raw = line.substr(q1 + 1, q2 - q1 - 1), p;
        for (size_t i = 0; i < raw.size(); ++i) {     // collapse each doubled backslash to one
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') { p.push_back('\\'); ++i; }
            else p.push_back(raw[i]);
        }
        int n = MultiByteToWideChar(CP_UTF8, 0, p.data(), (int)p.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p.data(), (int)p.size(), w.data(), n);
        if (!w.empty()) libs.push_back(w);
    }
    return libs;
}

std::vector<std::wstring> fixed_drive_roots() {
    std::vector<std::wstring> out;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        std::wstring root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
        if (GetDriveTypeW(root.c_str()) == DRIVE_FIXED) out.push_back(root);
    }
    return out;
}

}  // namespace

namespace {

std::filesystem::path game_path_file() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) > 0) {
        auto dir = std::filesystem::path(up) / L"RuneToolsX";
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        return dir / L"game-path.txt";
    }
    return L"runetoolsx-game-path.txt";
}

bool is_runescape_exe(const std::wstring& p) {
    std::wstring name = std::filesystem::path(p).filename().wstring();
    for (auto& c : name) c = (wchar_t)towlower(c);
    return name == L"runescape.exe";
}

// Name of the leaf signer: organisation first, common name as a fallback.
std::string signer_name(PCCERT_CONTEXT cert) {
    if (!cert) return {};
    wchar_t buf[256] = {};
    DWORD n = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, (void*)szOID_ORGANIZATION_NAME, buf, 256);
    if (n <= 1) n = CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, buf, 256);
    return n > 1 ? w2u(buf) : std::string{};
}

}  // namespace

SignerCheck VerifyGameSigner(const std::wstring& path) {
    SignerCheck out{ false, false, {}, {} };
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) { out.reason = "The file no longer exists."; return out; }

    WINTRUST_FILE_INFO fi{}; fi.cbStruct = sizeof(fi); fi.pcwszFilePath = path.c_str();
    WINTRUST_DATA wd{}; wd.cbStruct = sizeof(wd);
    wd.dwUIChoice       = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;          // no network round trip on every launch
    wd.dwUnionChoice    = WTD_CHOICE_FILE;
    wd.pFile            = &fi;
    wd.dwStateAction    = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags      = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;
    // No WTD_LIFETIME_SIGNING_FLAG: a countersigned (timestamped) file stays valid after the
    // certificate's own expiry, which is how an older genuine build keeps launching.
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG st = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);

    if (auto* prov = WTHelperProvDataFromStateData(wd.hWVTStateData)) {
        if (auto* sgnr = WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0)) {
            if (sgnr->csCertChain > 0 && sgnr->pasCertChain) out.subject = signer_name(sgnr->pasCertChain[0].pCert);
        }
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);

    if (st != ERROR_SUCCESS) {
        // A signer name with a failed check means the signature is present but the bytes no
        // longer match it: the file was modified after signing.
        if (!out.subject.empty())
            out.reason = "This file has been modified since \"" + out.subject + "\" signed it, so it cannot be trusted.";
        else if (st == TRUST_E_NOSIGNATURE)
            out.reason = "This file is not digitally signed. The real RuneScape.exe is signed by Jagex Limited.";
        else
            out.reason = "This file's digital signature could not be verified. The real RuneScape.exe is signed by Jagex Limited.";
        return out;
    }
    out.signed_ = true;
    std::string low = out.subject; for (auto& c : low) c = (char)tolower((unsigned char)c);
    if (low.find("jagex") == std::string::npos) {
        out.reason = "This file is signed by \"" + out.subject + "\", not by Jagex. The real RuneScape.exe is signed by Jagex Limited.";
        return out;
    }
    out.ok = true;
    return out;
}

std::wstring CustomRsClientPath() {
    std::ifstream in(game_path_file(), std::ios::binary);
    if (!in) return {};
    std::string u8((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!u8.empty() && (u8.back() == '\r' || u8.back() == '\n' || u8.back() == ' ')) u8.pop_back();
    if (u8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), w.data(), n);
    std::error_code ec;
    if (!is_runescape_exe(w) || !std::filesystem::exists(w, ec)) return {};
    return w;
}

std::string SetCustomRsClientPath(const std::wstring& path) {
    std::error_code ec;
    if (path.empty()) {
        std::filesystem::remove(game_path_file(), ec);
        rtx::log::Launcher("game path override cleared");
        return {};
    }
    if (!std::filesystem::exists(path, ec)) return "That file does not exist.";
    if (!is_runescape_exe(path)) {
        return w2u(std::filesystem::path(path).filename().wstring())
             + " is not RuneScape.exe. Pick the RuneScape.exe inside your game folder.";
    }
    SignerCheck sc = VerifyGameSigner(path);
    rtx::log::Launcher("game path candidate " + w2u(path) + ": signer=\"" + sc.subject + "\" ok=" + (sc.ok ? "1" : "0"));
    if (!sc.ok) return sc.reason;
    std::ofstream out(game_path_file(), std::ios::binary | std::ios::trunc);
    if (!out) return "The choice could not be saved.";
    out << w2u(path);
    rtx::log::Launcher("game path override set: " + w2u(path));
    return {};
}

std::wstring DefaultRsClientPath() {
    if (std::wstring custom = CustomRsClientPath(); !custom.empty()) return custom;
    return AutoRsClientPath();
}

std::wstring AutoRsClientPath() {
    // RuneScape.exe (the Jagex Launcher game wrapper). The wrapper refreshes stale session tokens
    // before spawning the real rs2client.exe child; spawning rs2client directly skips that and lands
    // on the shell "no app for rs-launch" dialog. Resolved via the registry and Steam libraries
    // first, then a per-fixed-drive scan of the standard relative layouts as a backstop.
    std::vector<std::wstring> cands;

    // 1) Jagex Launcher install dir from the registry (covers a custom dir on any drive).
    if (std::wstring jx = uninstall_install_location(L"Jagex Launcher"); !jx.empty()) {
        if (jx.back() != L'\\' && jx.back() != L'/') jx.push_back(L'\\');
        cands.push_back(jx + L"Games\\RuneScape\\RuneScape.exe");
    }

    // 2) Every Steam library (Steam dir + libraryfolders.vdf paths).
    for (std::wstring lib : steam_libraries()) {
        if (!lib.empty() && lib.back() != L'\\' && lib.back() != L'/') lib.push_back(L'\\');
        cands.push_back(lib + L"steamapps\\common\\RuneScape\\bin\\win64\\RuneScape.exe");
    }

    // 3) Per-fixed-drive scan of the standard relative layouts (in-place move to D:/E:/...).
    static const wchar_t* kRel[] = {
        L"Program Files (x86)\\Jagex Launcher\\Games\\RuneScape\\RuneScape.exe",
        L"Program Files\\Jagex Launcher\\Games\\RuneScape\\RuneScape.exe",
        L"Program Files\\Jagex\\RuneScape Launcher\\RuneScape.exe",
        L"ProgramData\\Jagex\\launcher\\RuneScape.exe",   // Jagex Launcher game cache (e.g. C:\ProgramData\Jagex\launcher)
        L"Program Files (x86)\\Steam\\steamapps\\common\\RuneScape\\bin\\win64\\RuneScape.exe",
        L"Steam\\steamapps\\common\\RuneScape\\bin\\win64\\RuneScape.exe",
        L"SteamLibrary\\steamapps\\common\\RuneScape\\bin\\win64\\RuneScape.exe",
    };
    for (const auto& drive : fixed_drive_roots())
        for (const wchar_t* rel : kRel)
            cands.push_back(drive + rel);

    std::error_code ec;
    for (const auto& p : cands) {
        if (std::filesystem::exists(p, ec)) {
            rtx::log::Launcher("RuneScape.exe resolved: " + w2u(p));
            return p;
        }
    }
    rtx::log::Launcher("RuneScape.exe not found (registry, Steam libraries, drive scan all empty)");
    return {};
}

namespace {

// Parent env minus any existing JX_ entries, then overrides appended
// (doubled-NUL terminated wide block). Wiping parent JX_ stops a stale
// launcher-env entry from outranking a saved-account value.
std::wstring build_env_block(
    const std::unordered_map<std::string, std::string>& env_overrides) {

    auto utf8_to_wide = [](const std::string& s) {
        if (s.empty()) return std::wstring{};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                    nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                            w.data(), n);
        return w;
    };

    LPWCH cur = GetEnvironmentStringsW();
    std::wstring base;
    if (cur) {
        const wchar_t* p = cur;
        while (*p) {
            size_t len = wcslen(p);
            bool is_jx = (len >= 3 && p[0] == L'J' && p[1] == L'X' && p[2] == L'_');
            if (!is_jx) { base.append(p, len); base.push_back(L'\0'); }
            p += len + 1;
        }
        FreeEnvironmentStringsW(cur);
    }
    for (const auto& [k, v] : env_overrides) {
        if (k.empty()) continue;
        if (!(k.size() >= 3 && k[0] == 'J' && k[1] == 'X' && k[2] == '_')) continue;
        base.append(utf8_to_wide(k));
        base.push_back(L'=');
        base.append(utf8_to_wide(v));
        base.push_back(L'\0');
    }
    base.push_back(L'\0');
    return base;
}

LaunchResult launch_impl(
    const std::wstring& rs_in,
    const std::unordered_map<std::string, std::string>* env_overrides) {

    LaunchResult r{ false, {}, 0 };

    std::wstring rs = rs_in.empty() ? DefaultRsClientPath() : rs_in;
    if (rs.empty() || !std::filesystem::exists(rs)) {
        r.detail = "Jagex Launcher (RuneScape.exe) not found";
        rtx::log::Launcher("launch failed: " + r.detail);
        return r;
    }
    // Checked on every launch, not only when chosen: the file at that path can change under
    // us, and the environment below carries the account's session identifiers.
    if (SignerCheck sc = VerifyGameSigner(rs); !sc.ok) {
        r.detail = "Launch refused: " + sc.reason;
        rtx::log::Launcher("launch refused: " + w2u(rs) + " signer=\"" + sc.subject + "\": " + sc.reason);
        return r;
    }

    std::wstring env_block;
    LPVOID env_ptr = nullptr;
    DWORD  flags   = 0;
    if (env_overrides) {
        env_block = build_env_block(*env_overrides);
        env_ptr   = (LPVOID)env_block.data();
        flags    |= CREATE_UNICODE_ENVIRONMENT;
    }

    STARTUPINFOW         si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION  pi{};
    std::wstring cmdline = L"\"" + rs + L"\"";
    auto cwd = std::filesystem::path(rs).parent_path().wstring();

    BOOL ok = CreateProcessW(
        rs.c_str(),
        cmdline.data(),
        nullptr, nullptr,
        FALSE,
        flags,
        env_ptr,
        cwd.c_str(),
        &si, &pi);
    if (!ok) {
        r.detail = "CreateProcessW failed: " +
                   last_error_message(GetLastError());
        rtx::log::Launcher("launch failed: " + r.detail);
        return r;
    }
    r.pid = pi.dwProcessId;
    // Tie every spawned client's lifetime to the launcher via a kill-on-close job object: the whole
    // tree (the bootstrap re-spawns the real client, which inherits job membership) is terminated
    // the moment this process goes away -- normal exit, crash, or task-kill.
    static HANDLE s_job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof(li));
        }
        return j;   // intentionally held for the launcher's lifetime
    }();
    if (s_job) AssignProcessToJobObject(s_job, pi.hProcess);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);

    r.success = true;
    r.detail  = "Launched (PID " + std::to_string(pi.dwProcessId) + ")";
    rtx::log::Launcher("spawned rs2client, PID " + std::to_string(r.pid));
    return r;
}

}  // namespace

LaunchResult LaunchClient(const std::wstring& rs_in) {
    return launch_impl(rs_in, nullptr);
}

LaunchResult LaunchClientWithEnv(
    const std::wstring& rs_in,
    const std::unordered_map<std::string, std::string>& env_overrides) {
    return launch_impl(rs_in, &env_overrides);
}

}  // namespace rtx::launcher::loader
