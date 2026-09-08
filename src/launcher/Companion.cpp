#include "Companion.h"
#include "../shared/Log.h"
#include "../../companion/SceneShare.h"   // per-pid section name + layout

#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rtx::launcher::companion {

namespace {

std::mutex g_mu;
std::unordered_map<std::uint32_t, ULONGLONG> g_last_try;   // pid -> last attempt tick

std::wstring ModulePath() {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return {};
    return (std::filesystem::path(exe).parent_path() / L"rtxscene.dll").wstring();
}

// Section exists iff the module is loaded and running in the pid (doubles as liveness).
bool SectionLive(std::uint32_t pid) {
    wchar_t name[64];
    rtx::scene::MakeSectionName(pid, name);
    HANDLE s = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!s) return false;
    CloseHandle(s);
    return true;
}

bool ModuleListed(std::uint32_t pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"rtxscene.dll") == 0) { found = true; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool LoadInto(std::uint32_t pid, const std::wstring& dll) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) return false;
    bool ok = false;
    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote) {
        if (WriteProcessMemory(h, remote, dll.c_str(), bytes, nullptr)) {
            HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
            auto fn = k32 ? (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW") : nullptr;
            if (fn) {
                HANDLE th = CreateRemoteThread(h, nullptr, 0, fn, remote, 0, nullptr);
                if (th) {
                    DWORD w = WaitForSingleObject(th, 10000);
                    CloseHandle(th);
                    // Exit code is only the low 32 bits of the HMODULE; 0 is not failure.
                    ok = ModuleListed(pid) || SectionLive(pid);
                    if (!ok && w == WAIT_TIMEOUT)
                        rtx::log::Launcher("companion: pid " + std::to_string(pid) + " LoadLibraryW still running after 10s");
                }
            }
        }
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    }
    CloseHandle(h);
    return ok;
}

}  // namespace

bool EnsureLoaded(std::uint32_t pid) {
    if (!pid) return false;
    if (SectionLive(pid)) return true;             // already live (cheap fast path)

    ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_last_try.find(pid);
        if (it != g_last_try.end() && now - it->second < 400) return false;
        g_last_try[pid] = now;
    }

    {
        HANDLE probe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!probe && GetLastError() == ERROR_ACCESS_DENIED) {
            static std::mutex s_mu;
            static std::unordered_set<std::uint32_t> s_logged;
            std::lock_guard<std::mutex> lk(s_mu);
            if (s_logged.insert(pid).second)
                rtx::log::Launcher("companion: pid " + std::to_string(pid) +
                    " is elevated / out of reach (access denied); run RuneToolsX as administrator to attach");
            return false;
        }
        if (probe) CloseHandle(probe);
    }

    std::wstring dll = ModulePath();
    if (dll.empty() || !std::filesystem::exists(dll)) {
        rtx::log::Launcher("scene module not staged next to the launcher");
        return false;
    }
    if (!ModuleListed(pid)) {
        bool loaded = LoadInto(pid, dll);
        rtx::log::Launcher(std::string("scene module ") + (loaded ? "started" : "not listed after load attempt") +
                           " for pid " + std::to_string(pid));
    }
    return SectionLive(pid);                        // may need a tick to appear
}

}  // namespace rtx::launcher::companion
