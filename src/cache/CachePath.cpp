#include "CachePath.h"
#include "Constants.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace rtx::cache {
namespace {

namespace fs = std::filesystem;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring reg_str(HKEY root, const wchar_t* key, const wchar_t* value, REGSAM view) {
    HKEY h{};
    if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &h) != ERROR_SUCCESS) return {};
    wchar_t buf[MAX_PATH]{};
    DWORD cb = sizeof(buf), type = 0;
    LSTATUS st = RegQueryValueExW(h, value, nullptr, &type, (LPBYTE)buf, &cb);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    return std::wstring(buf, wcsnlen(buf, MAX_PATH));
}

std::string env_str(const char* name) {
    char buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return (n > 0 && n < sizeof(buf)) ? std::string(buf, n) : std::string();
}

// Steam library roots: the Steam install plus every "path" in libraryfolders.vdf, so a game
// moved to another drive or a custom-named library is still found.
std::vector<std::string> steam_libraries() {
    std::vector<std::string> libs;
    std::wstring steam = reg_str(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0);
    if (steam.empty())
        steam = reg_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                        L"InstallPath", KEY_WOW64_32KEY);
    if (steam.empty()) return libs;
    libs.push_back(narrow(steam));
    std::ifstream f(fs::path(steam + L"\\steamapps\\libraryfolders.vdf"));
    std::string line;
    while (f && std::getline(f, line)) {
        auto pk = line.find("\"path\"");
        if (pk == std::string::npos) continue;
        auto q1 = line.find('"', pk + 6);
        if (q1 == std::string::npos) continue;
        auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string raw = line.substr(q1 + 1, q2 - q1 - 1), p;
        for (std::size_t i = 0; i < raw.size(); ++i) {   // vdf doubles every backslash
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') { p.push_back('\\'); ++i; }
            else p.push_back(raw[i]);
        }
        if (!p.empty()) libs.push_back(p);
    }
    return libs;
}

// `cache_folder=` out of a preferences.cfg. This is the client's own record of where it put the
// cache, so it survives the user moving it - which nothing in the registry does.
std::string cache_folder_from(const fs::path& prefs) {
    std::error_code ec;
    if (!fs::is_regular_file(prefs, ec)) return {};
    std::ifstream f(prefs);
    std::string line;
    while (f && std::getline(f, line)) {
        const char* kKey = "cache_folder=";
        if (line.rfind(kKey, 0) != 0) continue;
        std::string v = line.substr(std::strlen(kKey));
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
        return v;
    }
    return {};
}

void probe(CacheCandidate& c) {
    std::error_code ec;
    if (c.path.empty() || !fs::is_directory(c.path, ec)) return;
    for (const auto& e : fs::directory_iterator(c.path, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        const std::string fn = e.path().filename().string();
        if (fn.rfind("js5-", 0) != 0 || e.path().extension() != ".jcache") continue;
        ++c.archives;
        // Newest js5 write = when this cache was last played. GlobalSettings/Settings are
        // deliberately excluded: they are touched by merely opening a client.
        auto t = fs::last_write_time(e.path(), ec);
        if (!ec) {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                            t.time_since_epoch()).count();
            if (secs > c.newest) c.newest = secs;
        }
    }
    // A partially downloaded cache is still a real cache (NXT fills archives lazily), so the
    // bar is only "enough to be a cache and not a stray folder".
    c.usable = c.archives >= 8;
}

void add(std::vector<CacheCandidate>& out, std::string path, std::string source) {
    if (path.empty()) return;
    std::error_code ec;
    // Canonicalise so the same directory reached two ways is not weighed twice.
    fs::path p = fs::weakly_canonical(fs::path(path), ec);
    std::string s = ec ? path : p.string();
    for (const auto& c : out)
        if (_stricmp(c.path.c_str(), s.c_str()) == 0) return;
    CacheCandidate c;
    c.path = std::move(s);
    c.source = std::move(source);
    probe(c);
    out.push_back(std::move(c));
}

// A cache_folder value points at the PARENT; the cache itself is <cache_folder>\RuneScape.
// Accept either form, so an override may name the cache directly.
void add_folder_or_parent(std::vector<CacheCandidate>& out, const std::string& dir,
                          const std::string& source) {
    if (dir.empty()) return;
    add(out, (fs::path(dir) / "RuneScape").string(), source);
    add(out, dir, source + " (direct)");
}

}  // namespace

std::vector<CacheCandidate> CacheCandidates() {
    std::vector<CacheCandidate> out;

    // 1) Explicit override, for a layout nothing else predicts.
    add_folder_or_parent(out, env_str("RTX_CACHE_DIR"), "RTX_CACHE_DIR");

    // 2) The client's own record, per install. This is what makes a MOVED cache findable.
    add_folder_or_parent(out, cache_folder_from(R"(C:\ProgramData\Jagex\launcher\preferences.cfg)"),
                         "preferences.cfg (Jagex)");
    const auto libs = steam_libraries();
    for (const auto& lib : libs) {
        fs::path rs = fs::path(lib) / "steamapps" / "common" / "RuneScape";
        add_folder_or_parent(out, cache_folder_from(rs / "launcher" / "preferences.cfg"),
                             "preferences.cfg (Steam)");
    }
    // The Jagex Launcher's install dir can hold a preferences.cfg too when the game was
    // relocated wholesale.
    {
        std::wstring inst = reg_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Jagex\\JagexLauncher\\RuneScape",
                                    L"InstallLocation", 0);
        if (inst.empty())
            inst = reg_str(HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\WOW6432Node\\Jagex\\JagexLauncher\\RuneScape",
                           L"InstallLocation", KEY_WOW64_32KEY);
        if (!inst.empty())
            add_folder_or_parent(out, cache_folder_from(fs::path(narrow(inst)) / "preferences.cfg"),
                                 "preferences.cfg (install dir)");
    }

    // 3) Known defaults, for when no preferences.cfg has been written yet.
    add(out, kDefaultCacheRoot, "default (ProgramData)");
    for (const auto& lib : libs)
        add(out, (fs::path(lib) / "steamapps" / "common" / "RuneScape" / "RuneScape").string(),
            "default (Steam)");

    // 4) Last resort: the standard layouts on every fixed drive, for a hand-moved install that
    //    left no preferences.cfg behind. Bounded and shallow - no recursive search.
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26 && mask; ++i) {
        if (!(mask & (1u << i))) continue;
        char root[4] = { (char)('A' + i), ':', '\\', 0 };
        if (GetDriveTypeA(root) != DRIVE_FIXED) continue;
        const char* kRel[] = {
            "ProgramData\\Jagex\\RuneScape",
            "Jagex\\RuneScape",
            "Program Files (x86)\\Steam\\steamapps\\common\\RuneScape\\RuneScape",
            "Steam\\steamapps\\common\\RuneScape\\RuneScape",
            "SteamLibrary\\steamapps\\common\\RuneScape\\RuneScape",
        };
        for (const char* rel : kRel)
            add(out, (fs::path(root) / rel).string(), "drive scan");
    }
    return out;
}

const std::string& ResolveCacheRoot() {
    static std::once_flag once;
    static std::string resolved;
    std::call_once(once, [] {
        const auto cands = CacheCandidates();
        const CacheCandidate* best = nullptr;
        for (const auto& c : cands) {
            if (!c.usable) continue;
            // An explicit override is an instruction, not a hint: take it and stop weighing.
            if (c.source.rfind("RTX_CACHE_DIR", 0) == 0) { best = &c; break; }
            if (!best || c.newest > best->newest) best = &c;
        }
        resolved = best ? best->path : std::string(kDefaultCacheRoot);
    });
    return resolved;
}

}  // namespace rtx::cache
