#include "Cs2Browser.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#pragma comment(lib, "version.lib")

namespace fs = std::filesystem;

namespace cs2browser {
namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
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

std::string read_file(const fs::path& p) {
    if (p.empty()) return {};
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::mutex g_mu;
HANDLE g_proc = nullptr;   // running sidecar, owned under g_mu

bool proc_running_locked() {
    if (!g_proc) return false;
    if (WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT) return true;
    CloseHandle(g_proc);
    g_proc = nullptr;
    return false;
}

// Sidecar root: RTX_CS2_SIDECAR env, else <exe dir>\cs2sidecar. Must contain
// dist\cs2export.js. Empty when neither exists; callers report "sidecar not found".
std::wstring sidecar_dir() {
    wchar_t buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"RTX_CS2_SIDECAR", buf, MAX_PATH) && buf[0]) return buf;
    wchar_t mod[MAX_PATH]{};
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    fs::path bundled = fs::path(mod).parent_path() / L"cs2sidecar";
    if (fs::exists(bundled / L"dist" / L"cs2export.js")) return bundled.wstring();
    return std::wstring();
}

// Actual game revision = the installed rs2client.exe's VERSIONINFO (the cache
// itself stores no build number; rsmv's meta.json buildnr is a format cap).
std::string game_client_version() {
    const wchar_t* path = L"C:\\ProgramData\\Jagex\\launcher\\rs2client.exe";
    DWORD ignored = 0, sz = GetFileVersionInfoSizeW(path, &ignored);
    if (!sz) return {};
    std::vector<unsigned char> buf(sz);
    if (!GetFileVersionInfoW(path, 0, sz, buf.data())) return {};
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT n = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &n) || !ffi) return {};
    char v[48];
    std::snprintf(v, sizeof(v), "%u.%u.%u.%u",
                  (unsigned)HIWORD(ffi->dwFileVersionMS), (unsigned)LOWORD(ffi->dwFileVersionMS),
                  (unsigned)HIWORD(ffi->dwFileVersionLS), (unsigned)LOWORD(ffi->dwFileVersionLS));
    return v;
}

int script_id_from_name(const fs::path& p) {
    // clientscript-<id>.ts
    std::wstring st = p.stem().wstring();
    size_t dash = st.rfind(L'-');
    if (dash == std::wstring::npos) return -1;
    int id = 0;
    for (size_t i = dash + 1; i < st.size(); ++i) {
        if (st[i] < L'0' || st[i] > L'9') return -1;
        id = id * 10 + (st[i] - L'0');
    }
    return id;
}

}  // namespace

std::wstring OutDir() {
    wchar_t buf[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    fs::path dir = fs::path(buf) / L"RuneToolsX" / L"cs2";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.wstring();
}

std::string StatusJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    fs::path out = OutDir();
    std::string meta = read_file(out / L"meta.json");
    std::string prog = read_file(out / L"progress.json");
    // Game revision snapshotted when the extraction was STARTED (the panel's
    // "extracted at revision" field; clientVer below is the current install).
    std::string extract_ver = read_file(out / L"client_version.txt");
    bool sidecar = fs::exists(fs::path(sidecar_dir()) / L"dist" / L"cs2export.js");
    std::string clientver = game_client_version();
    std::ostringstream os;
    os << "{\"running\":" << (proc_running_locked() ? "true" : "false")
       << ",\"sidecar\":" << (sidecar ? "true" : "false")
       << ",\"clientVer\":\"" << json_escape(clientver) << "\""
       << ",\"extractVer\":\"" << json_escape(extract_ver) << "\""
       << ",\"meta\":" << (meta.empty() ? "null" : meta)
       << ",\"progress\":" << (prog.empty() ? "null" : prog) << "}";
    return os.str();
}

std::string StartExtract() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (proc_running_locked()) return "{\"err\":\"extraction already running\"}";
    fs::path entry = fs::path(sidecar_dir()) / L"dist" / L"cs2export.js";
    if (!fs::exists(entry))
        return "{\"err\":\"sidecar not found (set RTX_CS2_SIDECAR to the folder containing dist\\\\cs2export.js)\"}";
    // Stale progress from an earlier run would render as a live bar until the sidecar's
    // first write; drop it before spawning.
    std::error_code ec;
    fs::remove(fs::path(OutDir()) / L"progress.json", ec);
    {
        std::ofstream vf(fs::path(OutDir()) / L"client_version.txt",
                         std::ios::binary | std::ios::trunc);
        vf << game_client_version();
    }

    // Prefer a node.exe shipped next to the sidecar (self-contained installs);
    // fall back to node on PATH (dev setups).
    fs::path bundled_node = fs::path(sidecar_dir()) / L"node.exe";
    std::wstring node = fs::exists(bundled_node)
                            ? L"\"" + bundled_node.wstring() + L"\""
                            : L"node";
    std::wstring cmd = node + L" --max-old-space-size=8192 \"" + entry.wstring() +
                       L"\" \"" + OutDir() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;   // CreateProcessW may write to the buffer
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, sidecar_dir().c_str(), &si, &pi)) {
        return "{\"err\":\"failed to start node (is node.exe on PATH?)\"}";
    }
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    return "{\"ok\":true}";
}

std::string Cancel() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!proc_running_locked()) return "{\"ok\":false}";
    TerminateProcess(g_proc, 1);
    CloseHandle(g_proc);
    g_proc = nullptr;
    return "{\"ok\":true}";
}

std::string SearchJson(const std::string& query, int max_results) {
    if (query.empty()) return "{\"query\":\"\",\"files\":0,\"truncated\":false,\"results\":[]}";
    if (max_results <= 0 || max_results > 1000) max_results = 300;
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    fs::path dir = fs::path(OutDir()) / L"scripts";
    std::ostringstream results;
    int found = 0, files = 0;
    bool truncated = false;
    std::error_code ec;
    std::string body;
    for (auto& de : fs::directory_iterator(dir, ec)) {
        if (found >= max_results) { truncated = true; break; }
        if (de.path().extension() != L".ts") continue;
        int id = script_id_from_name(de.path());
        if (id < 0) continue;
        ++files;
        std::ifstream f(de.path(), std::ios::binary);
        if (!f) continue;
        body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        std::string lower = body;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        size_t pos = 0;
        int in_file = 0;
        while (in_file < 5 && found < max_results &&
               (pos = lower.find(needle, pos)) != std::string::npos) {
            size_t ls = body.rfind('\n', pos);
            ls = (ls == std::string::npos) ? 0 : ls + 1;
            size_t le = body.find('\n', pos);
            if (le == std::string::npos) le = body.size();
            std::string line = body.substr(ls, std::min(le - ls, (size_t)240));
            // line number = newlines before the match
            int lineno = 1 + (int)std::count(body.begin(), body.begin() + (std::ptrdiff_t)ls, '\n');
            if (found) results << ",";
            results << "{\"id\":" << id << ",\"line\":" << lineno
                    << ",\"text\":\"" << json_escape(line) << "\"}";
            ++found;
            ++in_file;
            pos = le;
        }
    }
    std::ostringstream os;
    os << "{\"query\":\"" << json_escape(query) << "\",\"files\":" << files
       << ",\"truncated\":" << (truncated ? "true" : "false")
       << ",\"results\":[" << results.str() << "]}";
    return os.str();
}

std::string NamesJson() {
    std::string s = read_file(fs::path(OutDir()) / L"names.json");
    return s.empty() ? "{}" : s;
}

std::string ScriptJson(int id, size_t offset) {
    fs::path p = fs::path(OutDir()) / L"scripts" /
                 (L"clientscript-" + std::to_wstring(id) + L".ts");
    std::string body = read_file(p);
    if (body.empty()) return "{\"err\":\"script not extracted\"}";
    constexpr size_t kChunk = 200 * 1024;
    if (offset > body.size()) offset = body.size();
    size_t n = std::min(kChunk, body.size() - offset);
    std::ostringstream os;
    os << "{\"id\":" << id << ",\"size\":" << body.size() << ",\"offset\":" << offset
       << ",\"more\":" << ((offset + n < body.size()) ? "true" : "false")
       << ",\"text\":\"" << json_escape(body.substr(offset, n)) << "\"}";
    return os.str();
}

}  // namespace cs2browser
