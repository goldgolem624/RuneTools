#include "Companion.h"
#include "../shared/Log.h"
#include "../../companion/SceneShare.h"   // per-pid section name + layout
#include "../../companion/ShareName.h"    // session key + handshake blob

#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
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
    wchar_t name[rtx::ipc::kNameChars];
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

// ---- Session handshake ----------------------------------------------
// Each client is handed a random session value and both sides name their
// channels with it, so the names cannot be predicted from the pid alone.
//
// Set RTX_NO_SESSION_KEY=1 to fall back to the previous pid-only naming
// without a rebuild, for narrowing down a channel that fails to bind.
bool SessionHandshakeEnabled() {
    static const bool on = GetEnvironmentVariableW(L"RTX_NO_SESSION_KEY", nullptr, 0) == 0;
    return on;
}

// Each client gets its own random session value. The launcher keeps it, names
// its channels with it, and hands the same value to the module by calling an
// exported entry point in the module's own address space. Both sides then
// build identical names that nothing else can predict.
//
// The handshake is repeated on every attach rather than only after a load, so
// a launcher restart against a client that still has the module loaded simply
// issues a new session and both sides rebind.

// Remote address of the module's RtxSetSession. The RVA is taken from our own
// mapped copy of the same file, so no export parsing is needed.
LPTHREAD_START_ROUTINE RemoteSetSession(std::uint32_t pid, const std::wstring& dll) {
    HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) {
        rtx::log::Launcher("session: cannot map the module locally, err " + std::to_string(GetLastError()));
        return nullptr;
    }
    FARPROC localFn = GetProcAddress(local, "RtxSetSession");
    std::uintptr_t rva = localFn
        ? (std::uintptr_t)localFn - (std::uintptr_t)local
        : 0;
    FreeLibrary(local);
    if (!rva) return nullptr;      // older module without the entry point

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    std::uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"rtxscene.dll") == 0) {
                base = (std::uintptr_t)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    if (!base) {
        rtx::log::Launcher("session: module not listed in pid " + std::to_string(pid));
        return nullptr;
    }

    return (LPTHREAD_START_ROUTINE)(base + rva);
}

// Generates a session for the client, records it locally, and pushes it to the
// module. Returns false when the module could not be told, in which case the
// caller leaves the client on the previous naming so the channels still work.
// Clients whose module has no session entry point. Retrying those costs a
// module snapshot plus a load and free of the DLL, on a path the overlay hits
// every frame, so it is tried once and then left alone.
std::mutex                        g_noHandshakeMu;
std::unordered_set<std::uint32_t> g_noHandshake;

bool CannotHandshake(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_noHandshakeMu);
    return g_noHandshake.count(pid) != 0;
}

bool HandshakeSession(std::uint32_t pid, const std::wstring& dll) {
    std::uint8_t key[16];
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, key, sizeof(key),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        rtx::log::Launcher("session: RNG unavailable for pid " + std::to_string(pid));
        return false;
    }

    LPTHREAD_START_ROUTINE fn = RemoteSetSession(pid, dll);
    if (!fn) {
        static std::mutex s_mu;
        static std::unordered_set<std::uint32_t> s_logged;
        std::lock_guard<std::mutex> lk(s_mu);
        if (s_logged.insert(pid).second)
            rtx::log::Launcher("session: module in pid " + std::to_string(pid) +
                               " has no session entry point; staying on the previous naming");
        { std::lock_guard<std::mutex> lk2(g_noHandshakeMu); g_noHandshake.insert(pid); }
        return false;
    }

    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) {
        rtx::log::Launcher("session: OpenProcess failed for pid " + std::to_string(pid) +
                           ", err " + std::to_string(GetLastError()));
        return false;
    }

    bool ok = false;
    rtx::ipc::SessionBlob blob{};
    blob.version = rtx::ipc::kSessionBlobVersion;
    blob.pid     = pid;
    for (std::size_t i = 0; i < sizeof(blob.key); ++i) blob.key[i] = key[i];

    LPVOID remote = VirtualAllocEx(h, nullptr, sizeof(blob), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
        rtx::log::Launcher("session: VirtualAllocEx failed for pid " + std::to_string(pid) +
                           ", err " + std::to_string(GetLastError()));
    if (remote) {
        bool wrote = WriteProcessMemory(h, remote, &blob, sizeof(blob), nullptr) != 0;
        if (!wrote)
            rtx::log::Launcher("session: WriteProcessMemory failed for pid " + std::to_string(pid) +
                               ", err " + std::to_string(GetLastError()));
        if (wrote) {
            HANDLE th = CreateRemoteThread(h, nullptr, 0, fn, remote, 0, nullptr);
            if (!th)
                rtx::log::Launcher("session: CreateRemoteThread failed for pid " + std::to_string(pid) +
                                   ", err " + std::to_string(GetLastError()));
            if (th) {
                if (WaitForSingleObject(th, 5000) == WAIT_OBJECT_0) {
                    DWORD rc = 1;
                    GetExitCodeThread(th, &rc);
                    ok = (rc == 0);
                    if (!ok)
                        rtx::log::Launcher("session: module rejected the session for pid " +
                                           std::to_string(pid) + ", code " + std::to_string(rc));
                } else {
                    rtx::log::Launcher("session: module did not answer for pid " + std::to_string(pid));
                }
                CloseHandle(th);
            }
        }
        // Freed only once the module has finished reading it.
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    }
    CloseHandle(h);

    // Adopt the key locally only after the module has it, so the two sides
    // never disagree about which names they are using.
    if (ok) {
        rtx::ipc::SetSessionKey(pid, key, sizeof(key));
        rtx::log::Launcher("session: established for pid " + std::to_string(pid));
    }
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(&blob, sizeof(blob));
    return ok;
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

    // A client we already hold a session for is live and correctly named.
    if ((!SessionHandshakeEnabled() || rtx::ipc::HasSessionKey(pid) || CannotHandshake(pid)) &&
        SectionLive(pid))
        return true;

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
    // Several callers race here on a fresh client; only one performs the load.
    static std::mutex s_loadMu;
    static std::unordered_set<std::uint32_t> s_loading;
    {
        std::lock_guard<std::mutex> lk(s_loadMu);
        if (s_loading.count(pid)) return SectionLive(pid);
        s_loading.insert(pid);
    }
    if (!ModuleListed(pid)) {
        bool loaded = LoadInto(pid, dll);
        rtx::log::Launcher(std::string("scene module ") + (loaded ? "started" : "not listed after load attempt") +
                           " for pid " + std::to_string(pid));
    }

    // Issue a session whether we just loaded the module or found it already
    // there, so a launcher restart against a live client rebinds both sides.
    if (SessionHandshakeEnabled() && !rtx::ipc::HasSessionKey(pid)) {
        // Gated on the module being alive rather than on ModuleListed: the
        // snapshot can come back empty while the loader is still settling, and
        // gating on it made a failed handshake indistinguishable from one that
        // never ran. RemoteSetSession does its own lookup and says so if the
        // module really is absent.
        if (!CannotHandshake(pid) && (SectionLive(pid) || ModuleListed(pid))) {
            static std::mutex s_mu;
            static std::unordered_set<std::uint32_t> s_tried;
            bool first = false;
            { std::lock_guard<std::mutex> lk(s_mu); first = s_tried.insert(pid).second; }
            if (first) rtx::log::Launcher("session: handshaking with pid " + std::to_string(pid));
            HandshakeSession(pid, dll);
        }
    }

    { std::lock_guard<std::mutex> lk(s_loadMu); s_loading.erase(pid); }
    return SectionLive(pid);                        // may need a tick to appear
}

void Forget(std::uint32_t pid) {
    if (!pid) return;
    rtx::ipc::ClearSessionKey(pid);
    { std::lock_guard<std::mutex> lk(g_mu); g_last_try.erase(pid); }
    { std::lock_guard<std::mutex> lk(g_noHandshakeMu); g_noHandshake.erase(pid); }
}

}  // namespace rtx::launcher::companion
