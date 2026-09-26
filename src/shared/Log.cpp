#include "Log.h"

#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <typeinfo>
#include <unordered_map>

namespace rtx::log {

namespace {

// Per file ceiling. Reached only by a runaway caller, so the file is closed off with one
// final line rather than growing without bound.
constexpr std::streamoff                       kFileByteCap = 16 * 1024 * 1024;

std::mutex                                     g_mu;
std::ofstream                                  g_launcher;
std::unordered_map<std::uint32_t, std::ofstream> g_clients;
std::string                                    g_userprofile_lc;   // lowercased, for redaction
bool                                           g_inited = false;
std::atomic<bool>                              g_shutting_down{false};   // suppress teardown-race crash reports

std::string to_lower_copy(const std::string& s) {
    std::string out(s);
    for (auto& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

std::filesystem::path log_dir() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) > 0)
        return std::filesystem::path(up) / L"RuneToolsX" / L"logs";
    return std::filesystem::path(L"RuneToolsX") / L"logs";
}

std::string timestamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    std::snprintf(ts, sizeof(ts), "[%02d:%02d:%02d.%03d] ",
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return ts;
}

// g_mu must be held.
void write_line(std::ofstream& f, const std::string& msg) {
    if (!f.is_open()) return;
    if (f.tellp() > kFileByteCap) {
        f << timestamp() << "log size cap reached, no further lines this session\n";
        f.flush();
        f.close();
        return;
    }
    f << timestamp() << Redact(msg) << "\n";
    f.flush();
}

// g_mu must be held. Opens (append) a per-PID stream on first use.
std::ofstream& client_stream(std::uint32_t pid) {
    auto it = g_clients.find(pid);
    if (it != g_clients.end()) return it->second;

    char name[32];
    std::snprintf(name, sizeof(name), "client-%u.log", pid);
    auto path = log_dir() / name;

    std::ofstream f(path, std::ios::out | std::ios::app);
    auto res = g_clients.emplace(pid, std::move(f));
    std::ofstream& out = res.first->second;
    out << "\n" << timestamp() << "=== attached to PID " << pid << " ===\n";
    out.flush();
    return out;
}

// Crash capture: crash-*.txt plus a minidump. Must not take the logging mutex (the crashing
// thread may hold it).
void write_crash_report(const char* tag, EXCEPTION_POINTERS* ep, const char* detail) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t base[64];
    swprintf(base, 64, L"crash-%02d%02d%02d-tid%lu",
             st.wHour, st.wMinute, st.wSecond, GetCurrentThreadId());
    auto dir = log_dir();
    std::ofstream f(dir / (std::wstring(base) + L".txt"));
    if (f) {
        f << timestamp() << tag << "\n";
        if (detail) f << detail << "\n";
        if (ep && ep->ExceptionRecord) {
            char b[128];
            std::snprintf(b, sizeof(b), "code=0x%08lX addr=%p",
                          (unsigned long)ep->ExceptionRecord->ExceptionCode,
                          ep->ExceptionRecord->ExceptionAddress);
            f << b << "\n";
        }
        f.flush();
    }
    HANDLE hf = CreateFileW((dir / (std::wstring(base) + L".dmp")).c_str(), GENERIC_WRITE, 0,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hf,
                          (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo),
                          ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(hf);
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (g_shutting_down.load(std::memory_order_relaxed)) return EXCEPTION_CONTINUE_SEARCH;
    write_crash_report("unhandled SEH exception", ep, nullptr);
    return EXCEPTION_CONTINUE_SEARCH;   // let WER record it as well
}

void TerminateHandler() {
    if (g_shutting_down.load(std::memory_order_relaxed)) {
        TerminateProcess(GetCurrentProcess(), 0xC0000409u);   // teardown race: exit quietly
        return;
    }
    char detail[512];
    std::snprintf(detail, sizeof(detail), "std::terminate (no active exception)");
    if (auto ex = std::current_exception()) {
        try { std::rethrow_exception(ex); }
        catch (const std::exception& e) {
            std::snprintf(detail, sizeof(detail), "uncaught C++ exception: %s [%s]",
                          e.what(), typeid(e).name());
        }
        catch (...) {
            std::snprintf(detail, sizeof(detail), "uncaught non-std C++ exception");
        }
    }
    write_crash_report("std::terminate", nullptr, detail);
    TerminateProcess(GetCurrentProcess(), 0xC0000409u);
}

}  // namespace

std::string Redact(const std::string& msg) {
    if (g_userprofile_lc.empty() || msg.empty()) return msg;
    auto lc = to_lower_copy(msg);
    if (lc.find(g_userprofile_lc) == std::string::npos) return msg;
    std::string out; out.reserve(msg.size());
    size_t pos = 0;
    while (pos < msg.size()) {
        auto hit = lc.find(g_userprofile_lc, pos);
        if (hit == std::string::npos) { out.append(msg, pos, std::string::npos); break; }
        out.append(msg, pos, hit - pos);
        out.append("%USERPROFILE%");
        pos = hit + g_userprofile_lc.size();
    }
    return out;
}

std::wstring LogDir() { return log_dir().wstring(); }

void BeginShutdown() { g_shutting_down.store(true, std::memory_order_relaxed); }

void Init() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_inited) return;
    g_inited = true;

    char up[MAX_PATH] = {};
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH) > 0) {
        std::string s = up;
        while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
        g_userprofile_lc = to_lower_copy(s);
    }

    auto dir = log_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Appending, never truncating: a second launcher instance or a headless switch runs this too,
    // and the file may be the running launcher's record of what is going wrong right now.
    g_launcher.open(dir / L"launcher.log", std::ios::out | std::ios::app);

    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate(TerminateHandler);
}

void StartRun() {
    std::lock_guard<std::mutex> lk(g_mu);
    auto dir = log_dir();
    std::error_code ec;

    // The previous run's log is kept under one name, so the last run's record survives the next
    // start, and this run's file begins empty. A rename that fails leaves the file appended to.
    if (g_launcher.is_open()) g_launcher.close();
    std::filesystem::rename(dir / L"launcher.log", dir / L"launcher.prev.log", ec);
    g_launcher.open(dir / L"launcher.log", std::ios::out | (ec ? std::ios::app : std::ios::trunc));

    if (std::filesystem::exists(dir, ec)) {
        const auto now = std::filesystem::file_time_type::clock::now();
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const auto fn = e.path().filename().string();
            const bool is_log = fn.size() > 4 && fn.compare(fn.size() - 4, 4, ".log") == 0;
            const auto age = now - e.last_write_time(ec);
            // Previous sessions' client logs go immediately. Companion logs may still belong to a
            // game that outlived the launcher, so they wait an hour. Crash artefacts keep a fortnight.
            bool drop = false;
            if (is_log && fn.rfind("client-", 0) == 0) drop = true;
            else if (is_log && fn.rfind("companion-", 0) == 0) drop = age > std::chrono::hours(1);
            else if (fn.rfind("crash-", 0) == 0) drop = age > std::chrono::hours(24 * 14);
            if (drop) std::filesystem::remove(e.path(), ec);
        }
    }
}

void Launcher(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_mu);
    write_line(g_launcher, msg);
}

void Client(std::uint32_t pid, const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_mu);
    write_line(client_stream(pid), msg);
}

void CloseClient(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_clients.find(pid);
    if (it == g_clients.end()) return;
    if (it->second.is_open()) {
        it->second << timestamp() << "=== PID " << pid << " gone ===\n";
        it->second.flush();
        it->second.close();
    }
    g_clients.erase(it);
}

}  // namespace rtx::log
