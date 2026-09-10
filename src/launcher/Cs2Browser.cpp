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
HANDLE g_log = nullptr;    // sidecar stdout/stderr, owned under g_mu
bool   g_started = false;  // an extraction ran this session
bool   g_cancelled = false;   // the last run was stopped from the panel
std::string g_exit;        // why the last run ended, empty while it is healthy

fs::path log_path() {
    wchar_t buf[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return fs::path(buf) / L"RuneToolsX" / L"logs" / L"cs2export.log";
}

// Last non-empty line of the sidecar log, for the panel to show when a run dies early.
std::string last_log_line() {
    std::string all = read_file(log_path());
    if (all.size() > 8192) all = all.substr(all.size() - 8192);
    std::string best;
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) best = line;
        start = end + 1;
    }
    if (best.size() > 300) best = best.substr(0, 300);
    return best;
}

bool proc_running_locked() {
    if (!g_proc) return false;
    if (WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    GetExitCodeProcess(g_proc, &code);
    CloseHandle(g_proc);
    g_proc = nullptr;
    if (g_log) { CloseHandle(g_log); g_log = nullptr; }
    // The sidecar reports its own failures on stdout and exits 0, so the exit code alone does
    // not say whether the run produced anything.
    std::string tail = last_log_line();
    if (g_cancelled) g_exit = "cancelled";
    else if (!tail.empty() && (tail.rfind("ERR", 0) == 0 || tail.find("Error") != std::string::npos))
        g_exit = tail;
    else if (code != 0) g_exit = "sidecar exited with code " + std::to_string((int)code);
    else if (!fs::exists(fs::path(OutDir()) / L"meta.json")) g_exit = "sidecar exited without writing anything";
    return false;
}

// Sidecar root: RTX_CS2_SIDECAR env, else <exe dir>\cs2sidecar (must contain dist\cs2export.js).
std::wstring sidecar_dir() {
    wchar_t buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"RTX_CS2_SIDECAR", buf, MAX_PATH) && buf[0]) return buf;
    wchar_t mod[MAX_PATH]{};
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    fs::path bundled = fs::path(mod).parent_path() / L"cs2sidecar";
    if (fs::exists(bundled / L"dist" / L"cs2export.js")) return bundled.wstring();
    return std::wstring();
}

// Game revision = rs2client.exe VERSIONINFO (the cache stores no build number).
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
    std::string extract_ver = read_file(out / L"client_version.txt");
    bool sidecar = fs::exists(fs::path(sidecar_dir()) / L"dist" / L"cs2export.js");
    std::string clientver = game_client_version();
    std::ostringstream os;
    const bool running = proc_running_locked();
    os << "{\"running\":" << (running ? "true" : "false")
       << ",\"lastError\":\"" << json_escape(running || !g_started ? std::string() : g_exit) << "\""
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
    std::error_code ec;
    fs::remove(fs::path(OutDir()) / L"progress.json", ec);
    {
        std::ofstream vf(fs::path(OutDir()) / L"client_version.txt",
                         std::ios::binary | std::ios::trunc);
        vf << game_client_version();
    }

    g_exit.clear();
    g_started = true;
    g_cancelled = false;

    // Sidecar output goes to a log so a run that dies on a cache read leaves a reason behind.
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE logf = CreateFileW(log_path().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    fs::path bundled_node = fs::path(sidecar_dir()) / L"node.exe";
    std::wstring node = fs::exists(bundled_node)
                            ? L"\"" + bundled_node.wstring() + L"\""
                            : L"node";
    std::wstring cmd = node + L" --max-old-space-size=8192 \"" + entry.wstring() +
                       L"\" \"" + OutDir() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (logf != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = logf;
        si.hStdError = logf;
        si.hStdInput = nullptr;
    }
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;   // CreateProcessW may write to the buffer
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, sidecar_dir().c_str(), &si, &pi)) {
        if (logf != INVALID_HANDLE_VALUE) CloseHandle(logf);
        g_exit = "failed to start node";
        return "{\"err\":\"failed to start node (is node.exe on PATH?)\"}";
    }
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    g_log = (logf == INVALID_HANDLE_VALUE) ? nullptr : logf;
    return "{\"ok\":true}";
}

std::string Cancel() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!proc_running_locked()) return "{\"ok\":false}";
    TerminateProcess(g_proc, 1);
    CloseHandle(g_proc);
    g_proc = nullptr;
    if (g_log) { CloseHandle(g_log); g_log = nullptr; }
    g_cancelled = true;
    g_exit = "cancelled";
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

// Baked CS2 switch maps (case -> var). "{}" means not extracted yet; consumers keep a fallback.
std::string SwitchesJson() {
    std::string s = read_file(fs::path(OutDir()) / L"switches.json");
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
