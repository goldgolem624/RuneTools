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

// Paths are built from wide strings and handed out as UTF-8, which is what SQLite and the rest of
// the launcher expect. path::string() and path(std::string) go through the ANSI code page instead:
// they misname non-ASCII folders and throw on a name the code page cannot hold.
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// A path written in a text file: UTF-8 when it decodes as such, else the ANSI code page. Never
// throws, so a malformed file cannot take discovery down.
fs::path path_from_text(const std::string& s) {
    if (s.empty()) return {};
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) { cp = CP_ACP; n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0); }
    if (n <= 0) return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), n);
    return fs::path(std::move(w));
}

// Folders named by a file or found on disk must be a drive-letter absolute path. A UNC or device
// path (\\host\share, \\?\, \\.\) would make us contact a network host just by probing it, and a
// relative one would resolve against our working directory.
bool local_drive_path(const fs::path& p) {
    const std::wstring& w = p.native();
    return w.size() >= 3 && ((w[0] >= L'A' && w[0] <= L'Z') || (w[0] >= L'a' && w[0] <= L'z')) &&
           w[1] == L':' && (w[2] == L'\\' || w[2] == L'/');
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

fs::path env_path(const wchar_t* name) {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? fs::path(std::wstring(buf, n)) : fs::path();
}

std::vector<fs::path> steam_libraries() {
    std::vector<fs::path> libs;
    std::wstring steam = reg_str(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0);
    if (steam.empty())
        steam = reg_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                        L"InstallPath", KEY_WOW64_32KEY);
    if (steam.empty()) return libs;
    libs.push_back(fs::path(steam));
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
        if (!p.empty()) libs.push_back(path_from_text(p));
    }
    return libs;
}

fs::path cache_folder_from(const fs::path& prefs) {
    std::error_code ec;
    if (!local_drive_path(prefs) || !fs::is_regular_file(prefs, ec)) return {};
    std::ifstream f(prefs);
    std::string line;
    while (f && std::getline(f, line)) {
        const char* kKey = "cache_folder=";
        if (line.rfind(kKey, 0) != 0) continue;
        std::string v = line.substr(std::strlen(kKey));
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
        return path_from_text(v);
    }
    return {};
}

void probe(CacheCandidate& c, const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    // Explicit error_code increments: the range-for form throws when a step fails.
    fs::directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& e = *it;
        std::error_code fec;
        if (!e.is_regular_file(fec)) continue;
        // Wide compare: any file name at all may sit in a folder we probe.
        const std::wstring fn = e.path().filename().wstring();
        if (fn.rfind(L"js5-", 0) != 0 || e.path().extension() != L".jcache") continue;
        ++c.archives;
        // Newest js5 write = last played. GlobalSettings/Settings are touched by merely opening a client.
        auto t = fs::last_write_time(e.path(), fec);
        if (!fec) {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                            t.time_since_epoch()).count();
            if (secs > c.newest) c.newest = secs;
        }
    }
    c.usable = c.archives >= 8;
}

// rank: see CacheCandidate. Only RTX_CACHE_DIR (rank 0), which the user sets, may name a network
// or relative path.
void add(std::vector<CacheCandidate>& out, const fs::path& path, std::string source, int rank) {
    if (path.empty()) return;
    if (rank > 0 && !local_drive_path(path)) return;
    std::error_code ec;
    fs::path p = fs::weakly_canonical(path, ec);
    if (ec || p.empty()) p = path;
    std::string s = narrow(p.native());
    for (const auto& c : out)
        if (_stricmp(c.path.c_str(), s.c_str()) == 0) return;
    CacheCandidate c;
    c.path = std::move(s);
    c.source = std::move(source);
    c.rank = rank;
    probe(c, p);
    out.push_back(std::move(c));
}

// cache_folder points at the PARENT (cache = <cache_folder>\RuneScape); accept either form.
void add_folder_or_parent(std::vector<CacheCandidate>& out, const fs::path& dir,
                          const std::string& source, int rank) {
    if (dir.empty()) return;
    add(out, dir / L"RuneScape", source, rank);
    add(out, dir, source + " (direct)", rank);
}

}  // namespace

std::vector<CacheCandidate> CacheCandidates() {
    std::vector<CacheCandidate> out;

    add_folder_or_parent(out, env_path(L"RTX_CACHE_DIR"), "RTX_CACHE_DIR", 0);

    add_folder_or_parent(out, cache_folder_from(R"(C:\ProgramData\Jagex\launcher\preferences.cfg)"),
                         "preferences.cfg (Jagex)", 1);
    const auto libs = steam_libraries();
    for (const auto& lib : libs) {
        fs::path rs = lib / "steamapps" / "common" / "RuneScape";
        add_folder_or_parent(out, cache_folder_from(rs / "launcher" / "preferences.cfg"),
                             "preferences.cfg (Steam)", 1);
    }
    {
        std::wstring inst = reg_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Jagex\\JagexLauncher\\RuneScape",
                                    L"InstallLocation", 0);
        if (inst.empty())
            inst = reg_str(HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\WOW6432Node\\Jagex\\JagexLauncher\\RuneScape",
                           L"InstallLocation", KEY_WOW64_32KEY);
        if (!inst.empty())
            add_folder_or_parent(out, cache_folder_from(fs::path(inst) / "preferences.cfg"),
                                 "preferences.cfg (install dir)", 1);
    }

    add(out, fs::path(kDefaultCacheRoot), "default (ProgramData)", 1);
    for (const auto& lib : libs)
        add(out, lib / "steamapps" / "common" / "RuneScape" / "RuneScape", "default (Steam)", 1);

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
            add(out, fs::path(root) / rel, "drive scan", 2);
    }
    return out;
}

const std::string& ResolveCacheRoot() {
    static std::once_flag once;
    static std::string resolved;
    std::call_once(once, [] {
        resolved = kDefaultCacheRoot;
        try {
            const auto cands = CacheCandidates();
            const CacheCandidate* best = nullptr;
            for (const auto& c : cands) {
                if (!c.usable) continue;
                if (c.rank == 0) { best = &c; break; }
                // Trust before recency: any local account can create a folder at a drive root and
                // set its file times, so the drive scan only counts when no install names a cache.
                if (!best || c.rank < best->rank || (c.rank == best->rank && c.newest > best->newest))
                    best = &c;
            }
            if (best) resolved = best->path;
        } catch (...) {
            // Discovery is best effort: keep the default rather than leave the cache unopened.
        }
    });
    return resolved;
}

}  // namespace rtx::cache
