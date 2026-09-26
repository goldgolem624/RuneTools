#include "Companion.h"
#include "../shared/Log.h"
#include "../../companion/SceneShare.h"   // per-pid section name + layout
#include "../../companion/ShareName.h"    // session key + handshake blob

#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rtx::launcher::companion {

namespace {

// ---- What is being converged on -------------------------------------------------------------------
// A client is served once two things hold at the same time: the launcher holds a session for it, and
// the module inside it has opened its scene section under that session's name. Everything below is a
// loop that moves a client towards that state and keeps checking it, rather than a sequence of steps
// whose return codes are trusted once. A step that could not be carried out this time (the process
// was still loading, a handle could not be taken, the module did not answer) is retried with backoff.
// Only positive evidence that the module can never take a session ends the attempts for a client.

struct Client {
    std::uint64_t started     = 0;      // the process's start time: what every verdict below is about
    ULONGLONG    nextTryMs    = 0;      // no attempt before this tick: the floor between attempts, raised by backoff
    unsigned     failures     = 0;      // consecutive attempts that did not move the client forward
    std::uint8_t key[16]{};             // the session offered to this client, the same one on every attempt
    bool         haveKey      = false;
    ULONGLONG    adoptedMs    = 0;      // when the module accepted the key and the launcher took it up
    bool         live         = false;  // the scene section has been seen under the session name
    bool         never        = false;  // this module cannot take a session; nothing more until the game restarts
    bool         inFlight     = false;  // one attempt at a time per client
    std::string  state;                 // what stopped the client last, for the log and for Describe
    std::string  logged;                // the last state written to the log, so a reason is written once
};

std::mutex g_mu;
std::unordered_map<std::uint32_t, Client> g_clients;   // under g_mu

// A failed attempt waits longer each time, from the floor up to a few seconds, so a client that is
// stuck costs a snapshot every few seconds and not every tick, and a client that is merely slow to
// start is picked up within a second.
ULONGLONG Backoff(unsigned failures) {
    ULONGLONG ms = 400;
    for (unsigned i = 1; i < failures && ms < 5000; ++i) ms *= 2;
    return ms > 5000 ? 5000 : ms;
}

std::wstring ModulePath() {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return {};
    return (std::filesystem::path(exe).parent_path() / L"rtxscene.dll").wstring();
}

// A pid names a process only until the process goes away and the number is handed out again. What
// is remembered about a client is tied to its start time, so a verdict about one process is never
// applied to the next one that happens to get its number. Zero when the process cannot be asked.
std::uint64_t StartTime(std::uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME c{}, e{}, k{}, u{};
    std::uint64_t t = 0;
    if (GetProcessTimes(h, &c, &e, &k, &u)) t = ((std::uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime;
    CloseHandle(h);
    return t;
}

// The end state's second half: the module's scene section exists under the session name. Without a
// session held here no name is produced and this is false, which is the right answer.
bool SectionLive(std::uint32_t pid) {
    wchar_t name[rtx::ipc::kNameChars];
    rtx::scene::MakeSectionName(pid, name);
    HANDLE s = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!s) return false;
    CloseHandle(s);
    return true;
}

// ---- Looking into the client ----------------------------------------------------------------------

// A module snapshot of a process that is still loading its libraries fails with ERROR_BAD_LENGTH or
// ERROR_PARTIAL_COPY and is meant to be asked again. Anything else is left to the caller.
HANDLE SnapshotModules(std::uint32_t pid, DWORD& err) {
    err = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (s != INVALID_HANDLE_VALUE) return s;
        err = GetLastError();
        if (err != ERROR_BAD_LENGTH && err != ERROR_PARTIAL_COPY) break;
        Sleep(25);
    }
    return INVALID_HANDLE_VALUE;
}

struct RemoteModule {
    std::uintptr_t base = 0;
    DWORD          size = 0;
};

// Whether rtxscene.dll is in the client, and where. Unknown means the process could not be inspected
// this time, which says nothing about the module, and is never taken for a No.
enum class Listed { Unknown, No, Yes };

Listed FindModule(std::uint32_t pid, RemoteModule& out, DWORD& err) {
    out = RemoteModule{};
    HANDLE snap = SnapshotModules(pid, err);
    if (snap == INVALID_HANDLE_VALUE) return Listed::Unknown;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    Listed found = Listed::No;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"rtxscene.dll") == 0) {
                out.base = (std::uintptr_t)me.modBaseAddr;
                out.size = me.modBaseSize;
                found = Listed::Yes;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// ---- The module file next to the launcher ---------------------------------------------------------
// The session entry point is called at base + rva, the rva taken from our own mapping of the same
// file. That is only sound if the client holds this file's image and not an earlier one, so the
// image's link stamp and size are kept as well and checked against the client before any call.
// Re-read whenever the file changes on disk, so a launcher that stays up across an update serves the
// clients that load the new file.

struct LocalImage {
    std::uintptr_t rva   = 0;   // 0: the file exports no session entry point
    DWORD          stamp = 0;
    DWORD          size  = 0;
    std::filesystem::file_time_type mtime{};
    std::uintmax_t bytes = 0;
    bool           known = false;
};

std::mutex g_imageMu;
LocalImage g_image;

// False with a reason when the file could not be read this time; true with rva possibly 0.
bool ReadLocalImage(const std::wstring& dll, LocalImage& out, std::string& why) {
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(dll, ec);
    const auto bytes = std::filesystem::file_size(dll, ec);
    std::lock_guard<std::mutex> lk(g_imageMu);
    if (g_image.known && !ec && g_image.mtime == mtime && g_image.bytes == bytes) { out = g_image; return true; }

    HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) {
        why = "cannot map the module file next to the launcher, err " + std::to_string(GetLastError());
        return false;
    }
    LocalImage img;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(local);
    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(local) + dos->e_lfanew);
    img.stamp = nt->FileHeader.TimeDateStamp;
    img.size  = nt->OptionalHeader.SizeOfImage;
    FARPROC fn = GetProcAddress(local, "RtxSetSession");
    img.rva   = fn ? (std::uintptr_t)fn - (std::uintptr_t)local : 0;
    FreeLibrary(local);
    img.mtime = mtime; img.bytes = bytes; img.known = true;
    g_image = img;
    out = img;
    return true;
}

// Whether the client's copy of the module is the file next to the launcher, by link stamp and image
// size. Different means an earlier build still loaded in a running game, whose entry point is not
// where ours is, and it is not called into. Unreadable says nothing about the module and is retried.
enum class Image { Same, Different, Unreadable };

Image CompareImage(HANDLE process, const RemoteModule& rm, const LocalImage& li) {
    IMAGE_DOS_HEADER dos{};
    SIZE_T got = 0;
    if (!ReadProcessMemory(process, (LPCVOID)rm.base, &dos, sizeof(dos), &got) || got != sizeof(dos)) return Image::Unreadable;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return Image::Different;
    IMAGE_NT_HEADERS nt{};
    if (!ReadProcessMemory(process, (LPCVOID)(rm.base + (std::uintptr_t)dos.e_lfanew), &nt, sizeof(nt), &got) || got != sizeof(nt)) return Image::Unreadable;
    if (nt.Signature != IMAGE_NT_SIGNATURE) return Image::Different;
    return (nt.FileHeader.TimeDateStamp == li.stamp && nt.OptionalHeader.SizeOfImage == li.size) ? Image::Same : Image::Different;
}

// ---- Steps ----------------------------------------------------------------------------------------

enum class Step {
    Live,       // the end state holds
    Progress,   // something moved; look again after the floor interval
    Stall,      // could not be carried out this time; look again after backoff
    Never       // positive evidence this module cannot take a session
};

bool LoadInto(std::uint32_t pid, const std::wstring& dll, std::string& why) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) { why = "cannot open the client for the module load, err " + std::to_string(GetLastError()); return false; }
    bool ok = false;
    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) why = "cannot allocate in the client for the module load, err " + std::to_string(GetLastError());
    if (remote) {
        if (!WriteProcessMemory(h, remote, dll.c_str(), bytes, nullptr)) {
            why = "cannot write the module path into the client, err " + std::to_string(GetLastError());
        } else {
            HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
            auto fn = k32 ? (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW") : nullptr;
            HANDLE th = fn ? CreateRemoteThread(h, nullptr, 0, fn, remote, 0, nullptr) : nullptr;
            if (!th) {
                why = "cannot start the module load in the client, err " + std::to_string(GetLastError());
            } else {
                DWORD w = WaitForSingleObject(th, 10000);
                CloseHandle(th);
                if (w == WAIT_TIMEOUT) why = "the module load in the client is still running after 10 s";
                else ok = true;   // the exit code is only the low half of the handle; the caller looks for the module instead
            }
        }
        // Freed only once the loader has finished with the path.
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    }
    CloseHandle(h);
    return ok;
}

// Hands the client's session to the module by running its entry point on a thread of ours inside the
// client. The blob is read once by the module and not retained.
Step PushSession(std::uint32_t pid, const RemoteModule& rm, const LocalImage& li, const std::uint8_t* key,
                 std::string& why) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) { why = "cannot open the client for the session, err " + std::to_string(GetLastError()); return Step::Stall; }

    Step result = Step::Stall;
    switch (CompareImage(h, rm, li)) {
        case Image::Same: break;
        case Image::Different:
            why = "the module in the client is not the one next to the launcher (an earlier build); restart the game to load the current one";
            CloseHandle(h);
            return Step::Never;
        case Image::Unreadable:
        default:
            why = "cannot read the module's header in the client, err " + std::to_string(GetLastError());
            CloseHandle(h);
            return Step::Stall;
    }

    rtx::ipc::SessionBlob blob{};
    blob.version = rtx::ipc::kSessionBlobVersion;
    blob.pid     = pid;
    for (std::size_t i = 0; i < sizeof(blob.key); ++i) blob.key[i] = key[i];

    LPVOID remote = VirtualAllocEx(h, nullptr, sizeof(blob), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        why = "cannot allocate in the client for the session, err " + std::to_string(GetLastError());
    } else {
        if (!WriteProcessMemory(h, remote, &blob, sizeof(blob), nullptr)) {
            why = "cannot write the session into the client, err " + std::to_string(GetLastError());
        } else {
            auto fn = (LPTHREAD_START_ROUTINE)(rm.base + li.rva);
            HANDLE th = CreateRemoteThread(h, nullptr, 0, fn, remote, 0, nullptr);
            if (!th) {
                why = "cannot start the session call in the client, err " + std::to_string(GetLastError());
            } else {
                if (WaitForSingleObject(th, 5000) != WAIT_OBJECT_0) {
                    why = "the module did not answer the session call within 5 s";
                } else {
                    DWORD rc = 1;
                    GetExitCodeThread(th, &rc);
                    if (rc == 0) {
                        result = Step::Progress;
                    } else if (rc == 3) {
                        why = "the module in the client is from an earlier build; restart the game to load the current one";
                        result = Step::Never;
                    } else {
                        why = "the module rejected the session, code " + std::to_string(rc);
                    }
                }
                CloseHandle(th);
            }
        }
        // Freed only once the module has finished reading it.
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    }
    CloseHandle(h);
    SecureZeroMemory(&blob, sizeof(blob));
    return result;
}

// One pass over a client: load the module if it is not there, hand it the session if it has none,
// then look for the end state. Each pass reports what it saw, and the caller schedules the next.
Step Attempt(std::uint32_t pid, ULONGLONG now, std::string& why) {
    {
        HANDLE probe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!probe && GetLastError() == ERROR_ACCESS_DENIED) {
            why = "the client is elevated / out of reach (access denied); run RuneToolsX as administrator to attach";
            return Step::Stall;
        }
        if (probe) CloseHandle(probe);
    }

    const std::wstring dll = ModulePath();
    if (dll.empty() || !std::filesystem::exists(dll)) {
        why = "scene module not staged next to the launcher";
        return Step::Stall;
    }

    RemoteModule rm; DWORD err = 0;
    Listed listed = FindModule(pid, rm, err);
    if (listed == Listed::Unknown) {
        why = "cannot inspect the client's modules, err " + std::to_string(err);
        return Step::Stall;
    }
    if (listed == Listed::No) {
        if (!LoadInto(pid, dll, why)) return Step::Stall;
        listed = FindModule(pid, rm, err);
        if (listed != Listed::Yes) {
            why = listed == Listed::Unknown
                ? "cannot inspect the client's modules after the load, err " + std::to_string(err)
                : "the module is not listed in the client after a load attempt";
            return Step::Stall;
        }
        rtx::log::Launcher("scene module started for pid " + std::to_string(pid));
    }

    // The module is in the client. Give it the session if the launcher holds none, the same key on
    // every attempt: a module that already has it answers at once and nothing rebinds.
    LocalImage li;
    if (!ReadLocalImage(dll, li, why)) return Step::Stall;
    if (!li.rva) {
        why = "the module file next to the launcher has no session entry point; the installation needs repair";
        return Step::Never;
    }

    std::uint8_t key[16];
    ULONGLONG adoptedMs = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Client& c = g_clients[pid];
        if (!c.haveKey) {
            if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, c.key, sizeof(c.key), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
                why = "RNG unavailable for the session";
                return Step::Stall;
            }
            c.haveKey = true;
            rtx::log::Launcher("session: handshaking with pid " + std::to_string(pid));
        }
        for (std::size_t i = 0; i < sizeof(key); ++i) key[i] = c.key[i];
        adoptedMs = c.adoptedMs;
    }

    if (!rtx::ipc::HasSessionKey(pid)) {
        Step s = PushSession(pid, rm, li, key, why);
        if (s != Step::Progress) { SecureZeroMemory(key, sizeof(key)); return s; }
        // Adopted locally only once the module has it, so the two sides never disagree on the names.
        rtx::ipc::SetSessionKey(pid, key, sizeof(key));
        rtx::log::Launcher("session: established for pid " + std::to_string(pid));
        std::lock_guard<std::mutex> lk(g_mu);
        g_clients[pid].adoptedMs = now;
        adoptedMs = now;
    }
    SecureZeroMemory(key, sizeof(key));

    if (SectionLive(pid)) return Step::Live;

    // The module opens its section within a tick of taking the session. Until then this is progress;
    // past that, the same key is offered again, which a module that still runs accepts at once, and
    // the wait is put in the log rather than assumed away.
    if (now - adoptedMs < 5000) return Step::Progress;
    {
        std::uint8_t again[16];
        { std::lock_guard<std::mutex> lk(g_mu); const Client& c = g_clients[pid]; for (std::size_t i = 0; i < sizeof(again); ++i) again[i] = c.key[i]; }
        std::string pushWhy;
        Step s = PushSession(pid, rm, li, again, pushWhy);
        SecureZeroMemory(again, sizeof(again));
        if (s == Step::Never) { why = pushWhy; return s; }
        why = s == Step::Progress
            ? "the module holds the session but has not opened its scene section after " + std::to_string((now - adoptedMs) / 1000) + " s"
            : "the module took the session earlier but now: " + pushWhy;
    }
    return Step::Stall;
}

}  // namespace

bool EnsureLoaded(std::uint32_t pid) {
    if (!pid) return false;

    // The end state, checked on every call. Cheap enough for the callers that poll from the render path.
    if (rtx::ipc::HasSessionKey(pid) && SectionLive(pid)) {
        std::lock_guard<std::mutex> lk(g_mu);
        Client& c = g_clients[pid];
        if (!c.live) {
            c.live = true; c.failures = 0; c.state.clear(); c.logged.clear();
            rtx::log::Launcher("companion: pid " + std::to_string(pid) + " is live");
        }
        return true;
    }

    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Client& c = g_clients[pid];
        if (c.inFlight || now < c.nextTryMs) return false;
        c.nextTryMs = now + 400;
        // A different process under a remembered number: nothing known applies to it, session included.
        const std::uint64_t started = StartTime(pid);
        if (started && c.started && started != c.started) {
            rtx::log::Launcher("companion: pid " + std::to_string(pid) + " is a new process; starting over");
            rtx::ipc::ClearSessionKey(pid);
            c = Client{};
            c.nextTryMs = now + 400;
        }
        if (!c.started) c.started = started;
        if (c.never) return false;
        c.inFlight = true;
        if (c.live) {
            c.live = false;
            rtx::log::Launcher("companion: pid " + std::to_string(pid) + " is no longer live; bringing it back");
        }
    }

    std::string why;
    Step step = Step::Stall;
    try { step = Attempt(pid, now, why); }
    catch (const std::exception& e) { why = std::string("attempt failed: ") + e.what(); }
    catch (...) { why = "attempt failed"; }

    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_clients.find(pid);
    if (it == g_clients.end()) return false;   // forgotten while the attempt ran: nothing to record
    Client& c = it->second;
    c.inFlight = false;
    const ULONGLONG after = GetTickCount64();
    switch (step) {
        case Step::Live:
            c.live = true; c.failures = 0; c.state.clear(); c.logged.clear();
            rtx::log::Launcher("companion: pid " + std::to_string(pid) + " is live");
            return true;
        case Step::Progress:
            c.failures = 0; c.state.clear();
            c.nextTryMs = after + 400;
            return false;
        case Step::Never:
            c.never = true; c.state = why;
            rtx::log::Launcher("companion: pid " + std::to_string(pid) + ": " + why + " (no further attempts)");
            return false;
        case Step::Stall:
        default:
            ++c.failures;
            c.nextTryMs = after + Backoff(c.failures);
            c.state = why;
            if (c.logged != why) {
                c.logged = why;
                rtx::log::Launcher("companion: pid " + std::to_string(pid) + ": " + why +
                                   " (retrying, next in " + std::to_string(Backoff(c.failures)) + " ms)");
            }
            return false;
    }
}

std::string Describe(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_clients.find(pid);
    if (it == g_clients.end()) return "the launcher has not looked at this client yet";
    const Client& c = it->second;
    if (c.live)  return "the module is live under its session";
    if (c.never) return c.state;
    if (!c.state.empty()) return c.state + (c.failures ? " (attempt " + std::to_string(c.failures) + ")" : "");
    if (c.adoptedMs) return "session established, waiting for the module to open its channels";
    return "bringing the module up";
}

void Forget(std::uint32_t pid) {
    if (!pid) return;
    rtx::ipc::ClearSessionKey(pid);
    std::lock_guard<std::mutex> lk(g_mu);
    g_clients.erase(pid);
}

}  // namespace rtx::launcher::companion
