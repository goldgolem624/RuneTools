#include "Process.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <winternl.h>
#include <algorithm>
#include <vector>

namespace rtx::launcher::process {

namespace {

bool ieq(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// rs2client.exe is the only target -- the RuneScape.exe / rs3client.exe
// shells just spawn rs2client and exit, so attaching to those wouldn't
// be useful even when they show up in the process list.
bool is_rs_client(const std::wstring& exe) {
    return ieq(exe, L"rs2client.exe");
}

std::wstring proc_path(HANDLE h) {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
        return std::wstring(buf, len);
    }
    return {};
}

bool is_x64(HANDLE h) {
    BOOL wow = FALSE;
    if (!IsWow64Process(h, &wow)) return true;
    return !wow;
}

}  // namespace

std::vector<Info> ScanRsClients() {
    std::vector<Info> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring exe = pe.szExeFile;
            if (!is_rs_client(exe)) continue;
            Info pi;
            pi.pid = pe.th32ProcessID;
            pi.name = exe;
            HANDLE h = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                FALSE, pe.th32ProcessID);
            if (h) {
                pi.path = proc_path(h);
                pi.x64  = is_x64(h);
                CloseHandle(h);
            } else {
                // No read handle. An access denial means the client is out of reach -- almost always
                // launched elevated (Jagex Launcher as administrator) against a medium-integrity
                // launcher. QUERY_LIMITED_INFORMATION still works, so the row can still be listed.
                if (GetLastError() == ERROR_ACCESS_DENIED) pi.accessible = false;
                pi.x64 = true;
                HANDLE h2 = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    pe.th32ProcessID);
                if (h2) {
                    pi.path = proc_path(h2);
                    CloseHandle(h2);
                }
            }
            out.push_back(std::move(pi));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::sort(out.begin(), out.end(),
              [](const Info& a, const Info& b) { return a.pid < b.pid; });
    return out;
}

bool TerminateByPid(std::uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok != 0;
}

// Hardcoded x64 PEB offsets -- avoids the WDK dep for full layouts.

namespace {

using NtQueryInformationProcess_t = NTSTATUS (NTAPI*)(
    HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

NtQueryInformationProcess_t get_nt_query() {
    static NtQueryInformationProcess_t fn = []() -> NtQueryInformationProcess_t {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) return nullptr;
        return reinterpret_cast<NtQueryInformationProcess_t>(
            GetProcAddress(ntdll, "NtQueryInformationProcess"));
    }();
    return fn;
}

constexpr SIZE_T kPebProcessParametersOffset = 0x20;
constexpr SIZE_T kRupEnvironmentOffset       = 0x80;
constexpr SIZE_T kRupEnvironmentSizeOffset   = 0x3F0;

std::string wide_to_utf8(const wchar_t* w, size_t wlen) {
    if (!w || !wlen) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen,
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen,
                        out.data(), n, nullptr, nullptr);
    return out;
}

template <typename T>
bool read_at(HANDLE h, void* remote, T& out) {
    SIZE_T got = 0;
    return ReadProcessMemory(h, remote, &out, sizeof(T), &got) &&
           got == sizeof(T);
}

}  // namespace

std::unordered_map<std::string, std::string> ReadJxEnv(std::uint32_t pid) {
    std::unordered_map<std::string, std::string> out;

    auto nt_query = get_nt_query();
    if (!nt_query) return out;

    HANDLE proc = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
        FALSE, pid);
    if (!proc) return out;

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG written = 0;
    NTSTATUS st = nt_query(proc, ProcessBasicInformation,
                           &pbi, sizeof(pbi), &written);
    if (st != 0 || !pbi.PebBaseAddress) {
        CloseHandle(proc);
        return out;
    }

    void* params = nullptr;
    if (!read_at(proc, (char*)pbi.PebBaseAddress + kPebProcessParametersOffset,
                 params) || !params) {
        CloseHandle(proc);
        return out;
    }

    void*  env_ptr  = nullptr;
    ULONG_PTR env_size = 0;
    if (!read_at(proc, (char*)params + kRupEnvironmentOffset,     env_ptr) ||
        !read_at(proc, (char*)params + kRupEnvironmentSizeOffset, env_size) ||
        !env_ptr || env_size == 0) {
        CloseHandle(proc);
        return out;
    }

    // 8 MB cap against corrupt size reads.
    constexpr ULONG_PTR kMaxEnvBytes = 8 * 1024 * 1024;
    if (env_size > kMaxEnvBytes) { CloseHandle(proc); return out; }

    std::vector<wchar_t> buf(env_size / sizeof(wchar_t));
    SIZE_T got = 0;
    if (!ReadProcessMemory(proc, env_ptr, buf.data(), env_size, &got) ||
        got != env_size) {
        CloseHandle(proc);
        return out;
    }
    CloseHandle(proc);

    // NAME=VALUE\0 entries; filter to JX_ to skip ambient env.
    const wchar_t* p   = buf.data();
    const wchar_t* end = buf.data() + buf.size();
    while (p < end && *p) {
        size_t len = 0;
        while (p + len < end && p[len] != L'\0') ++len;
        if (len == 0) break;

        const wchar_t* eq = nullptr;
        for (size_t i = 0; i < len; ++i) {
            // Skip leading '=' (per-drive cwd entries like "=C:").
            if (i > 0 && p[i] == L'=') { eq = p + i; break; }
        }
        if (eq) {
            size_t name_len  = (size_t)(eq - p);
            size_t value_len = len - name_len - 1;
            if (name_len >= 3 && p[0] == L'J' && p[1] == L'X' && p[2] == L'_') {
                auto name  = wide_to_utf8(p, name_len);
                auto value = wide_to_utf8(eq + 1, value_len);
                if (!name.empty()) out.emplace(std::move(name), std::move(value));
            }
        }
        p += len + 1;
    }
    return out;
}

}  // namespace rtx::launcher::process
