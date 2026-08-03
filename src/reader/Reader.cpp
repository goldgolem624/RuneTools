#include "Reader.h"
#include "BankCache.h"
#include "../cache/CacheReader.h"
#include "../shared/Log.h"
#include "../../companion/SceneOffsets.h" // client memory offsets shared with the companion
#include "../../companion/SceneShare.h"   // shared layout for runtime scene objects
#include "../../companion/GroundShare.h"  // shared layout for dropped ground items (type 3)
#include "../../companion/VarcShare.h"    // shared layout for live client-var values
#include "../../companion/RenderShare.h"  // launcher->companion render toggles
#include "../../companion/SpecialShare.h" // shared layout for transient render-pass highlights
#include "../../companion/NetProbeShare.h" // decoded server->client packet feed (companion framer hook)

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <winternl.h>

#include <algorithm>
#include <atomic>
#include <tuple>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "winmm.lib")

namespace rtx::reader {

namespace {

// ---- Patterns + offsets baked from static analysis of rs2client.exe ----

// MainData chain (used for world + status). Pattern anchors a function near
// the top of MainData ctor that does `mov [rip+disp32], rax` to publish
// the MainData root pointer to a global. adjust = -32 walks back to the
// start of that function.
const std::uint8_t  kMainAnchorBytes[] =
    { 0x48, 0x2D, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x83 };
const std::size_t   kMainAnchorLen     = sizeof(kMainAnchorBytes);
constexpr int       kMainAnchorAdjust  = -32;
constexpr std::uint32_t kOffWorld   = 0x19970;   // root + this -> ptr -> +0x20 -> +0x8 = world(int)
constexpr std::uint32_t kOffStatus  = 0x19F60;   // root + this = status(int8)
constexpr std::uint32_t kOffStats   = 0x198E0;   // root + this -> stats container
constexpr std::uint32_t kStatsInner = 0x7618;    // stats container + this -> skill block
constexpr std::uint32_t kOffGE      = 0x19950;   // root + this -> ptr -> +0x10 = ge slot array
constexpr std::uint32_t kGEArrayPad = 0x10;      // bytes from slot-container start to slot 0
constexpr std::uint32_t kGESlotSize = 0x28;      // bytes per slot
constexpr int           kGESlotCount = 8;        // members get 8, non-members 3 (rest read as empty)

// Interface-container manager. root + this -> mgr -> { array start@+0x8,
// end@+0x10 }; entries stride 0x48 with id@+0x10 and the item array at
// start@+0x18 / end@+0x20 (stride 0x8: item_id@+0, stack@+4). Bank = id 95;
// it is only present while the bank window is open.
constexpr std::uint32_t kOffInvData      = 0x19988;
constexpr std::uint32_t kContainerStride = 0x48;
constexpr int           kBankContainerId = 95;
constexpr int           kMetalBankContainerId = 858;   // Mining&Smithing metal bank (ores + bars)
constexpr int           kMaterialsContainerId = 885;   // Archaeology material storage (the CS2 interface group 1313 is NOT the container id)
// Group Ironman shared bank, container 963. Note 964 is the bank
// INVENTORY, not the shared storage.
constexpr int           kGroupBankContainerId = 963;
constexpr int           kBaitBoxContainerId = 867;   // Anachronia Big Game Hunter bait box
constexpr int           kWorkbenchContainerId = 1008; // Archaeologist's workbench damaged-artefact storage
constexpr int           kMaxContainers   = 64;
constexpr int           kMaxBankSlots    = 8192;

// Player varp (var-player) hashmap, embedded in MainData at +0x36040. Layout:
//   root+0x8  = bucketArray (ptr)        root+0x10 = divisor (i32, bucket count)
//   node = bucketArray[varpId % divisor]; chain node.next@+0x28; node id@+0,
//   value@+0x8 (low 32 = the int value), type tag@+0x20 (0/1 = int).
// An unset varp has no node and defaults to 0. Varbits are just a bit-slice of a
// varp (def: varp id + [lsb,msb]); the slicing is done by the caller/JS.
constexpr std::uint32_t kOffVarpHash     = 0x36040;

// Tick chain. The tick function loads an owner from a rip-relative global
// and increments a counter at +0xDBF0 of that owner. The exact byte
// sequence for the increment is:
//   FF 81 F0 DB 00 00     INC dword [RCX+0xDBF0]
// preceded (within the same prologue) by:
//   48 8B 0D <disp32>     MOV RCX, [rip+disp32]   ; the owner global
// Anchor on the increment -- specific enough to be unique -- then walk back
// ~16 bytes to find the MOV that loaded RCX.
const std::uint8_t  kTickIncBytes[] =
    { 0xFF, 0x81, 0xF0, 0xDB, 0x00, 0x00 };
const std::size_t   kTickIncLen     = sizeof(kTickIncBytes);
constexpr std::uint32_t kTickCounterOff = 0xDBF0;  // u32 at owner + this

// ---- Win32 helpers ----

std::string last_error_msg(DWORD err) {
    char* buf = nullptr;
    DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buf, 0, nullptr);
    std::string out;
    if (buf && n) {
        out.assign(buf, n);
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
            out.pop_back();
    }
    if (buf) LocalFree(buf);
    return out;
}

DWORD find_pid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD out = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { out = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

struct ModuleRange { std::uint64_t base = 0; std::uint64_t size = 0; };

ModuleRange main_module_range(HANDLE h, const wchar_t* name) {
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_64BIT))
        return {};
    DWORD n = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < n; ++i) {
        wchar_t base[260] = {};
        GetModuleBaseNameW(h, mods[i], base, 260);
        if (_wcsicmp(base, name) != 0) continue;
        MODULEINFO mi{};
        if (!GetModuleInformation(h, mods[i], &mi, sizeof(mi))) return {};
        return { (std::uint64_t)mi.lpBaseOfDll, (std::uint64_t)mi.SizeOfImage };
    }
    return {};
}

bool rpm_bytes(HANDLE h, std::uint64_t addr, void* out, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(h, (LPCVOID)addr, out, n, &got) && got == n;
}
template <typename T>
std::optional<T> rpm(HANDLE h, std::uint64_t addr) {
    T v{};
    if (!rpm_bytes(h, addr, &v, sizeof(T))) return std::nullopt;
    return v;
}

// Search the target's .text for a literal byte pattern. Returns RVA (offset
// from module base) of every match.
std::vector<std::uint64_t> scan_text(HANDLE h, std::uint64_t mod_base,
                                     std::uint64_t mod_size,
                                     const std::uint8_t* needle, std::size_t n) {
    std::vector<std::uint64_t> hits;
    constexpr std::size_t chunk = 4 * 1024 * 1024;
    std::vector<std::uint8_t> buf(chunk + n);
    std::uint64_t off = 0;
    std::size_t   carry = 0;
    while (off < mod_size) {
        std::size_t want = (std::size_t)((std::min)((std::uint64_t)chunk,
                                                    mod_size - off));
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (LPCVOID)(mod_base + off),
                               buf.data() + carry, want, &got) || got == 0) {
            off += want;
            carry = 0;
            continue;
        }
        std::size_t total = carry + got;
        if (total >= n) {
            for (std::size_t i = 0; i + n <= total; ++i) {
                if (std::memcmp(buf.data() + i, needle, n) == 0) {
                    hits.push_back(off + i - carry);
                }
            }
            carry = n - 1;
            std::memmove(buf.data(), buf.data() + total - carry, carry);
        } else {
            carry = total;
        }
        off += got;
    }
    return hits;
}

// Walk forward from `start_rva` looking for `48 89 05 <rel32>` -- the
// `mov [rip+rel32], rax` that publishes a 64-bit value to a global. Returns
// the absolute global address. Scans up to `scan_max` bytes.
std::optional<std::uint64_t> decode_publish_global(
        HANDLE h, std::uint64_t mod_base, std::uint64_t start_rva,
        std::size_t scan_max = 0x200) {
    std::vector<std::uint8_t> body(scan_max);
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (LPCVOID)(mod_base + start_rva),
                           body.data(), scan_max, &got)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i + 7 <= got; ++i) {
        if (body[i] == 0x48 && body[i+1] == 0x89 && body[i+2] == 0x05) {
            std::int32_t disp = *reinterpret_cast<std::int32_t*>(&body[i+3]);
            return mod_base + start_rva + i + 7 + disp;
        }
    }
    return std::nullopt;
}

// Given the RVA of the `INC dword [RCX+0xDBF0]` increment, walk back up to
// `look_back` bytes for the most recent `48 8B 0D <rel32>` (MOV RCX,
// [rip+rel32]). Returns the absolute address of the global it loads.
std::optional<std::uint64_t> decode_preceding_mov_rcx_rip(
        HANDLE h, std::uint64_t mod_base, std::uint64_t inc_rva,
        std::size_t look_back = 0x40) {
    std::uint64_t scan_start = (inc_rva > look_back) ? inc_rva - look_back : 0;
    std::size_t   scan_len   = (std::size_t)(inc_rva - scan_start);
    std::vector<std::uint8_t> body(scan_len);
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (LPCVOID)(mod_base + scan_start),
                           body.data(), scan_len, &got)) {
        return std::nullopt;
    }
    // Walk back from the end to reach the MOV nearest the increment.
    for (std::ptrdiff_t i = (std::ptrdiff_t)got - 7; i >= 0; --i) {
        if (body[i] == 0x48 && body[i+1] == 0x8B && body[i+2] == 0x0D) {
            std::int32_t disp = *reinterpret_cast<std::int32_t*>(&body[i+3]);
            return mod_base + scan_start + i + 7 + disp;
        }
    }
    return std::nullopt;
}

// Read VS_VERSIONINFO ProductVersion from the target's exe file path.
std::string read_client_version(HANDLE h) {
    wchar_t path[MAX_PATH] = {};
    DWORD n = MAX_PATH;
    if (!QueryFullProcessImageNameW(h, 0, path, &n)) return {};
    DWORD handle = 0;
    DWORD sz = GetFileVersionInfoSizeW(path, &handle);
    if (!sz) return {};
    std::vector<char> buf(sz);
    if (!GetFileVersionInfoW(path, handle, sz, buf.data())) return {};
    struct LangCp { WORD lang, cp; };
    LangCp* langs = nullptr; UINT lang_n = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                        (LPVOID*)&langs, &lang_n) || lang_n < sizeof(LangCp)) {
        return {};
    }
    wchar_t sub[64];
    auto query = [&](const wchar_t* key) -> std::string {
        std::swprintf(sub, 64, L"\\StringFileInfo\\%04x%04x\\%s",
                      langs[0].lang, langs[0].cp, key);
        wchar_t* val = nullptr; UINT vn = 0;
        if (!VerQueryValueW(buf.data(), sub, (LPVOID*)&val, &vn) || !val) return {};
        int b = WideCharToMultiByte(CP_UTF8, 0, val, -1, nullptr, 0, nullptr, nullptr);
        if (b <= 1) return {};
        std::string out(b - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, val, -1, out.data(), b, nullptr, nullptr);
        return out;
    };
    auto v = query(L"ProductVersion");
    if (v.empty()) v = query(L"FileVersion");
    return v;
}

// Read the JX_DISPLAY_NAME variable from the target's PEB environment.
// Returns empty if unavailable (legacy accounts and pre-init both look
// the same here).
std::string read_target_env(HANDLE h, const wchar_t* var) {
    using NtQIP_t = NTSTATUS (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static auto NtQIP = reinterpret_cast<NtQIP_t>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!NtQIP) return {};

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG ret = 0;
    if (NtQIP(h, ProcessBasicInformation, &pbi, sizeof(pbi), &ret) != 0)
        return {};
    if (!pbi.PebBaseAddress) return {};

    // PEB->ProcessParameters @ offset 0x20 on x64.
    PVOID proc_params = nullptr;
    if (!rpm_bytes(h, (std::uint64_t)pbi.PebBaseAddress + 0x20,
                   &proc_params, sizeof(proc_params)) || !proc_params)
        return {};
    // RTL_USER_PROCESS_PARAMETERS->Environment @ 0x80, EnvironmentSize @ 0x3F0.
    PVOID env_addr = nullptr;
    if (!rpm_bytes(h, (std::uint64_t)proc_params + 0x80,
                   &env_addr, sizeof(env_addr)) || !env_addr)
        return {};
    SIZE_T env_size = 0;
    if (!rpm_bytes(h, (std::uint64_t)proc_params + 0x3F0,
                   &env_size, sizeof(env_size)) ||
        env_size == 0 || env_size > 1 * 1024 * 1024)
        return {};

    std::vector<wchar_t> blob(env_size / sizeof(wchar_t) + 2, L'\0');
    if (!rpm_bytes(h, (std::uint64_t)env_addr, blob.data(), env_size))
        return {};

    size_t var_len = std::wcslen(var);
    const wchar_t* p = blob.data();
    const wchar_t* end = blob.data() + blob.size();
    while (p < end && *p) {
        size_t len = std::wcslen(p);
        if (len > var_len + 1 &&
            _wcsnicmp(p, var, var_len) == 0 && p[var_len] == L'=') {
            const wchar_t* val = p + var_len + 1;
            int b = WideCharToMultiByte(CP_UTF8, 0, val, -1, nullptr, 0, nullptr, nullptr);
            if (b <= 1) return {};
            std::string out(b - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, val, -1, out.data(), b, nullptr, nullptr);
            return out;
        }
        p += len + 1;
    }
    return {};
}

const char* status_label(int s) {
    switch (s) {
        case 10: return "Logging in";
        case 20: return "Lobby";
        case 30: return "In-game";
        case 37: return "Changing worlds";
        case 40: return "Logging out";
        default: return "";
    }
}

// Number of logical processors -- used to normalise per-process CPU%.
int logical_cpu_count() {
    static int n = []{
        SYSTEM_INFO si; GetSystemInfo(&si);
        int v = (int)si.dwNumberOfProcessors;
        return v > 0 ? v : 1;
    }();
    return n;
}

ULONGLONG ft_to_100ns(const FILETIME& ft) {
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

HostInfo read_host_info() {
    static std::string s_cpu;
    static std::string s_gpu;
    static long long   s_total_mb = 0;
    static bool        s_cached   = false;
    if (!s_cached) {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                0, KEY_READ, &hk) == ERROR_SUCCESS) {
            wchar_t buf[256] = {}; DWORD sz = sizeof(buf);
            if (RegQueryValueExW(hk, L"ProcessorNameString", nullptr,
                                 nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
                int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) {
                    s_cpu.assign((size_t)(n - 1), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, buf, -1, s_cpu.data(), n, nullptr, nullptr);
                }
            }
            RegCloseKey(hk);
        }
        while (!s_cpu.empty() && (s_cpu.back() == ' ' || s_cpu.back() == '\t'))
            s_cpu.pop_back();

        // GPU: walk display adapters, take the first non-Microsoft Basic
        // one (those are the software fallback or RDP adapter).
        DISPLAY_DEVICEW dd{}; dd.cb = sizeof(dd);
        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
            std::wstring name = dd.DeviceString;
            if (name.find(L"Microsoft Basic") != std::wstring::npos) continue;
            if (name.find(L"Remote Display") != std::wstring::npos) continue;
            int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                s_gpu.assign((size_t)(n - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, s_gpu.data(), n, nullptr, nullptr);
            }
            break;
        }
        if (s_gpu.empty()) s_gpu = "Unknown";
        if (s_cpu.empty()) s_cpu = "Unknown";

        MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            s_total_mb = (long long)(ms.ullTotalPhys / (1024 * 1024));
        }
        s_cached = true;
    }

    HostInfo hi;
    hi.cpu_name          = s_cpu;
    hi.cpu_logical_cores = logical_cpu_count();
    hi.gpu_name          = s_gpu;
    hi.ram_total_mb      = s_total_mb;

    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        hi.ram_used_mb = (long long)((ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024));
    }
    return hi;
}

// ---- Per-PID cached attachment state ----

struct State {
    DWORD          pid              = 0;
    HANDLE         proc             = nullptr;
    std::uint64_t  mod_base         = 0;
    std::uint64_t  mod_size         = 0;
    std::string    client_version;
    std::string    display_name;

    // Resolved global addresses (absolute VAs in the target).
    std::uint64_t  main_global_va   = 0;
    std::uint64_t  tick_owner_va    = 0;

    // Tick tracking. The fast-poll thread refreshes these at ~1 ms
    // cadence using QueryPerformanceCounter, so the reported inter-tick delta
    // is accurate to a millisecond rather than quantised to the
    // UI's polling rate.
    std::uint32_t  last_tick_value     = 0;
    double         last_tick_qpc_ms    = 0.0;
    double         last_dt_ms          = 0.0;
    std::uint32_t  current_tick        = 0;

    // CPU-usage sampling. Kernel + user time are 100-ns ticks, diffed against
    // wall-clock and divided by core count to normalise to the 0..100% range
    // matching Task Manager. Multiple client windows poll concurrently; without
    // a min-dt guard the sample window becomes too small and idle processes
    // round to 0%.
    ULONGLONG      cpu_sample_wall_ms     = 0;
    ULONGLONG      cpu_sample_total_100ns = 0;
    double         cached_cpu_pct         = 0.0;

    // In-game character name (inline ASCII at data ptr +0x68). Fallback
    // bank-cache key for legacy logins where JX_DISPLAY_NAME is empty.
    std::string    character;

    // Bank (container 95). Held live while the bank is open; persisted to
    // disk on change so BankJson can serve it when closed.
    std::vector<BankSlot> bank_slots;       // full array; slot = index
    bool                  bank_open      = false;
    long long             bank_cached_at = 0;   // unix seconds of last disk write
    std::uint64_t         bank_hash      = 0;   // FNV-1a of slots, change detect

    // Metal bank (container 858): ores + bars, loaded only at a forge/furnace/anvil.
    std::vector<BankSlot> metalbank_slots;
    bool                  metalbank_open      = false;
    long long             metalbank_cached_at = 0;
    std::uint64_t         metalbank_hash      = 0;

    // Archaeology material storage (container 885): loaded only while the storage UI is open.
    std::vector<BankSlot> materials_slots;
    bool                  materials_open      = false;
    long long             materials_cached_at = 0;
    std::uint64_t         materials_hash      = 0;

    // Group Ironman shared bank (container 963): loaded only while the group storage UI is open.
    std::vector<BankSlot> groupbank_slots;
    bool                  groupbank_open      = false;
    long long             groupbank_cached_at = 0;
    std::uint64_t         groupbank_hash      = 0;

    // Anachronia bait box (container 867): loaded only while the bait-box UI is open.
    std::vector<BankSlot> baitbox_slots;
    bool                  baitbox_open      = false;
    long long             baitbox_cached_at = 0;
    std::uint64_t         baitbox_hash      = 0;
    // Archaeologist's workbench damaged-artefact storage (container 1008): loaded only while the
    // workbench UI is open.
    std::vector<BankSlot> workbench_slots;
    bool                  workbench_open      = false;
    long long             workbench_cached_at = 0;
    std::uint64_t         workbench_hash      = 0;
};

std::mutex                              g_mu;
std::unordered_map<DWORD, State>        g_states;
// Info-tab state: previous local-player pos per pid (movement detection). g_pinfo_mu guards it --
// PlayerInfoJson runs on BOTH the panel_loop thread and the ReadAsync cold-path (UI thread), and the
// dead-client eviction in SampleAll also touches it, so it must never be accessed unsynchronized.
std::unordered_map<DWORD, std::pair<float, float>> g_pinfo_prevpos;
std::mutex                                         g_pinfo_mu;

void release(State& s) {
    if (s.proc) CloseHandle(s.proc);
    s = State{};
}

// Snapshot a client's read targets under a BRIEF g_mu hold and DUPLICATE its process handle, so a
// per-tab builder can do its cross-process reads WITHOUT holding g_mu for the whole walk --
// holding it through a builder's RPM serialises every other reader, the 5 ms tick and the overlay.
// The duplicated handle stays valid even if the client exits and SampleAll closes the original
// mid-read (SampleAll is the ONLY closer), so a read on a dead handle just fails. The dup is
// closed on scope exit. Use:  auto ps = snap_proc(pid); if (!ps) return EMPTY; rpm(ps.h, ps.mgva)...
struct ProcSnap {
    HANDLE        h = nullptr;       // DUPLICATED handle (independently valid; closed in the dtor)
    std::uint64_t mgva = 0;          // main_global_va: address of the MainData root pointer
    std::uint64_t mod_base = 0;      // rs2client.exe image base (for module-relative reads)
    ProcSnap() = default;
    ProcSnap(const ProcSnap&) = delete;
    ProcSnap& operator=(const ProcSnap&) = delete;
    ProcSnap(ProcSnap&& o) noexcept : h(o.h), mgva(o.mgva), mod_base(o.mod_base) { o.h = nullptr; }
    ~ProcSnap() { if (h) CloseHandle(h); }
    explicit operator bool() const { return h != nullptr && mgva != 0; }
};
static ProcSnap snap_proc(std::uint32_t pid) {
    ProcSnap s;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_states.find((DWORD)pid);
    if (it != g_states.end() && it->second.proc && it->second.main_global_va &&
        DuplicateHandle(GetCurrentProcess(), it->second.proc, GetCurrentProcess(),
                        &s.h, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        s.mgva     = it->second.main_global_va;
        s.mod_base = it->second.mod_base;
    }
    return s;
}

bool process_alive(HANDLE h) {
    if (!h) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code)) return false;
    return code == STILL_ACTIVE;
}

std::vector<DWORD> find_all_pids(const wchar_t* name) {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0)
                out.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

// Static analysis: scan .text for the MainData publish global. Returns 0
// if the byte pattern isn't present (process not fully image-mapped yet).
std::uint64_t resolve_main_global(HANDLE h, std::uint64_t base, std::uint64_t size) {
    auto anchors = scan_text(h, base, size, kMainAnchorBytes, kMainAnchorLen);
    for (auto rva : anchors) {
        std::uint64_t fn_rva = (std::uint64_t)((std::int64_t)rva + kMainAnchorAdjust);
        if (fn_rva >= size) continue;
        auto g = decode_publish_global(h, base, fn_rva);
        if (g) return *g;
    }
    return 0;
}

// Static analysis: scan .text for the tick-counter `INC dword [RCX+0xDBF0]`
// and decode the immediately preceding `MOV RCX, [rip+disp32]` to get the
// tick-owner global address. The increment byte sequence is unique in .text
// so the first hit is the right one; the global is not checked for being
// populated (it is BSS-initialised before engine init).
std::uint64_t resolve_tick_owner_global(HANDLE h,
                                        std::uint64_t base, std::uint64_t size) {
    auto incs = scan_text(h, base, size, kTickIncBytes, kTickIncLen);
    for (auto inc_rva : incs) {
        if (inc_rva >= size) continue;
        auto g = decode_preceding_mov_rcx_rip(h, base, inc_rva);
        if (g) return *g;
    }
    return 0;
}

// Open the process + capture immutable bits (mod range, version, env).
// Globals are resolved here too but may stay 0 if the engine isn't yet
// initialised; sample_one() retries each frame until they're set.
bool attach_state(State& s, DWORD pid) {
    HANDLE h = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
        FALSE, pid);
    if (!h) {
        rtx::log::Client(pid, "attach failed: OpenProcess err=" +
                              std::to_string(GetLastError()));
        return false;
    }
    s.pid  = pid;
    s.proc = h;

    auto range = main_module_range(h, L"rs2client.exe");
    if (!range.base) {
        rtx::log::Client(pid, "attach failed: rs2client.exe module not mapped yet");
        release(s);
        return false;
    }
    s.mod_base = range.base;
    s.mod_size = range.size;

    s.client_version = read_client_version(h);
    s.display_name   = read_target_env(h, L"JX_DISPLAY_NAME");

    s.main_global_va = resolve_main_global(h, range.base, range.size);
    s.tick_owner_va  = resolve_tick_owner_global(h, range.base, range.size);

    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "attached: version=%s base=0x%llx main_global=%s tick_owner=%s name=%s",
        s.client_version.empty() ? "?" : s.client_version.c_str(),
        (unsigned long long)s.mod_base,
        s.main_global_va ? "ok" : "pending",
        s.tick_owner_va  ? "ok" : "pending",
        s.display_name.empty() ? "(none yet)" : s.display_name.c_str());
    rtx::log::Client(pid, buf);
    return true;
}

// ---- 1 ms tick poller ----

LARGE_INTEGER     g_qpc_freq{};
std::atomic<bool> g_fast_started{ false };

double qpc_now_ms() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)g_qpc_freq.QuadPart;
}

void fast_tick_loop() {
    // Raise the system timer resolution so the short Sleep below is honoured
    // (default granularity is ~15 ms). Process-wide; left raised for the
    // lifetime of the launcher.
    timeBeginPeriod(1);

    // Per-process tick probes. The read plan is snapshotted under g_mu, the (slow)
    // cross-process ReadProcessMemory runs OUTSIDE the lock so a sweep never stalls
    // the UI-thread SampleAll callers, then results are written back under the lock.
    // Reused across iterations to avoid per-loop allocation.
    struct Probe  { DWORD pid; HANDLE proc; std::uint64_t owner_va;
                    std::uint32_t last_val; double last_qpc_ms; };
    struct Result { DWORD pid; HANDLE proc; std::uint32_t tick;
                    double now_ms; bool reset; bool advanced; double dt; };
    std::vector<Probe>  probes;
    std::vector<Result> results;

    while (true) {
        // 1) Snapshot the read plan under the lock (cheap; no RPM here).
        probes.clear();
        {
            std::lock_guard<std::mutex> lk(g_mu);
            probes.reserve(g_states.size());
            for (auto& [pid, s] : g_states)
                if (s.proc && s.tick_owner_va)
                    probes.push_back({pid, s.proc, s.tick_owner_va,
                                      s.last_tick_value, s.last_tick_qpc_ms});
        }

        // 2) Read each process's tick counter WITHOUT holding g_mu. Only this
        //    thread writes the tick fields, so the snapshot's last_* values are
        //    still authoritative when the change is classified below.
        results.clear();
        results.reserve(probes.size());
        for (const auto& p : probes) {
            auto owner = rpm<std::uint64_t>(p.proc, p.owner_va);
            if (!owner || !*owner) continue;
            auto t = rpm<std::uint32_t>(p.proc, *owner + kTickCounterOff);
            if (!t) continue;
            Result r{p.pid, p.proc, *t, qpc_now_ms(), false, false, 0.0};
            if (p.last_qpc_ms == 0.0)      r.reset = true;          // first observation
            else if (*t < p.last_val)      r.reset = true;          // counter reset (relogin)
            else if (*t != p.last_val) {   r.advanced = true; r.dt = r.now_ms - p.last_qpc_ms; }
            results.push_back(r);
        }

        // 3) Write back under the lock, skipping any process that was evicted or
        //    re-attached (different handle) during the read.
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (const auto& r : results) {
                auto it = g_states.find(r.pid);
                if (it == g_states.end() || it->second.proc != r.proc) continue;
                State& s = it->second;
                s.current_tick = r.tick;
                if (r.reset) {
                    s.last_tick_value  = r.tick;
                    s.last_tick_qpc_ms = r.now_ms;
                    s.last_dt_ms       = 0.0;
                } else if (r.advanced) {
                    s.last_dt_ms       = r.dt;
                    s.last_tick_value  = r.tick;
                    s.last_tick_qpc_ms = r.now_ms;
                }
            }
        }

        // 5 ms is ample for a 600 ms game tick and keeps this thread's syscall
        // volume low.
        Sleep(5);
    }
}

void ensure_fast_thread_started() {
    bool expected = false;
    if (!g_fast_started.compare_exchange_strong(expected, true)) return;
    QueryPerformanceFrequency(&g_qpc_freq);
    std::thread(fast_tick_loop).detach();
}

// Fill a Snapshot from the live State. Caller holds g_mu.
Snapshot sample_one(State& s) {
    Snapshot snap;
    snap.pid            = s.pid;
    snap.client_version = s.client_version;
    snap.display_name   = s.display_name;

    if (s.main_global_va) {
        auto root = rpm<std::uint64_t>(s.proc, s.main_global_va);
        if (root && *root) {
            auto p1 = rpm<std::uint64_t>(s.proc, *root + kOffWorld);
            if (p1 && *p1) {
                auto p2 = rpm<std::uint64_t>(s.proc, *p1 + 0x20);
                if (p2 && *p2) {
                    auto w = rpm<std::int32_t>(s.proc, *p2 + 0x8);
                    if (w) snap.world = *w;
                }
            }
            auto st = rpm<std::int8_t>(s.proc, *root + kOffStatus);
            if (st) snap.status = (int)*st;

            // Skill records: root -> stats container -> +0x7618 -> { count@+8,
            // entries@+0x10 }. Each record is stride 0x18; per the engine's
            // decompiled update_xp + skill-data packet handler the fields are:
            //   record+0x0C = experience, +0x10 = current level, +0x14 = boosted.
            // Covers all 29 skills, Necromancy included.
            auto stats = rpm<std::uint64_t>(s.proc, *root + kOffStats);
            if (stats && *stats > 0x10000) {
                auto block = rpm<std::uint64_t>(s.proc, *stats + kStatsInner);
                if (block && *block > 0x10000) {
                    auto count = rpm<std::uint8_t>(s.proc, *block + 0x8);
                    auto arr   = rpm<std::uint64_t>(s.proc, *block + 0x10);
                    if (count && arr && *arr > 0x10000) {
                        int n = (*count > 29) ? 29 : *count;
                        // One block read instead of 3 RPMs per skill: the records
                        // are contiguous (stride 0x18) at *arr; per record the
                        // fields are +0x0C XP, +0x10 level, +0x14 boosted.
                        std::vector<std::uint8_t> sb((std::size_t)n * 0x18);
                        if (n > 0 && rpm_bytes(s.proc, *arr, sb.data(), sb.size())) {
                            snap.skills.reserve(n);
                            for (int i = 0; i < n; ++i) {
                                const std::uint8_t* e = sb.data() + (std::size_t)i * 0x18;
                                SkillLevel sl;
                                sl.real    = *(const std::int32_t*)(e + 0x10);
                                sl.boosted = *(const std::int32_t*)(e + 0x14);
                                std::int32_t xp = *(const std::int32_t*)(e + 0x0C);
                                if (xp >= 0 && xp <= 200000000) sl.xp = xp;
                                snap.skills.push_back(sl);
                            }
                        }
                    }
                }
            }

            auto ge_box = rpm<std::uint64_t>(s.proc, *root + kOffGE);
            if (ge_box && *ge_box) {
                // One block read for all 8 slots (stride 0x28) instead of 7 RPMs
                // each. Per slot: +0x00 status, +0x04 type, +0x08 item,
                // +0x10 price(i64), +0x18 qty, +0x1C filled, +0x20 fill_value(i64).
                alignas(8) std::uint8_t gb[kGESlotCount * kGESlotSize];
                if (rpm_bytes(s.proc, *ge_box + kGEArrayPad, gb, sizeof(gb))) {
                    snap.ge_slots.reserve(kGESlotCount);
                    for (int i = 0; i < kGESlotCount; ++i) {
                        const std::uint8_t* b = gb + (std::size_t)i * kGESlotSize;
                        GeSlot g; g.slot = i;
                        g.status       = *(const std::int32_t*)(b + 0x00);
                        g.type         = *(const std::int32_t*)(b + 0x04);
                        g.item_id      = *(const std::int32_t*)(b + 0x08);
                        g.price        = *(const std::int64_t*)(b + 0x10);
                        g.quantity     = *(const std::int32_t*)(b + 0x18);
                        g.filled       = *(const std::int32_t*)(b + 0x1C);
                        g.filled_value = *(const std::int64_t*)(b + 0x20);
                        snap.ge_slots.push_back(g);
                    }
                }
            }

            // In-game character name (inline ASCII at data ptr +0x68), kept
            // only as a legacy-login fallback for the bank-cache key.
            auto dptr = rpm<std::uint64_t>(s.proc, *root + (kOffStatus + 0x8));
            if (dptr && *dptr > 0x10000) {
                char nm[16] = {0};
                if (rpm_bytes(s.proc, *dptr + 0x68, nm, 15)) {
                    std::string name;
                    for (int ci = 0; ci < 15 && nm[ci]; ++ci)
                        if ((unsigned char)nm[ci] >= 32) name.push_back(nm[ci]);
                    if (!name.empty()) s.character = name;
                }
            }
            // Prefer JX_DISPLAY_NAME: it's stable and set before the bank can
            // open, whereas the memory name populates lazily and would split
            // the write and read across two filenames.
            const std::string& bank_key =
                !s.display_name.empty() ? s.display_name : s.character;

            // Bank (container 95) + Metal bank (container 858). Both exist in memory only
            // while their window is open; snapshot whichever are present and persist to disk
            // when contents change. One container-manager walk feeds both.
            bool found_bank = false, found_metal = false, found_mats = false, found_group = false, found_bait = false,
                 found_wb = false;
            auto cmgr = rpm<std::uint64_t>(s.proc, *root + kOffInvData);
            if (cmgr && *cmgr > 0x10000) {
                auto cstart = rpm<std::uint64_t>(s.proc, *cmgr + 0x8);
                auto cend   = rpm<std::uint64_t>(s.proc, *cmgr + 0x10);
                if (cstart && cend && *cend > *cstart) {
                    int n = (int)((*cend - *cstart) / kContainerStride);
                    if (n > kMaxContainers) n = kMaxContainers;
                    std::vector<std::uint8_t> cbuf((std::size_t)n * kContainerStride);
                    if (n > 0 && rpm_bytes(s.proc, *cstart, cbuf.data(), cbuf.size())) {
                        auto capture = [&](const std::uint8_t* e, std::vector<BankSlot>& dst,
                                           std::uint64_t& hashRef, long long& cachedAtRef,
                                           const char* kind) {
                            std::uint64_t istart = *(const std::uint64_t*)(e + 0x18);
                            std::uint64_t iend   = *(const std::uint64_t*)(e + 0x20);
                            if (istart <= 0x10000 || iend <= istart) return;
                            int slots = (int)((iend - istart) / 0x8);
                            if (slots > kMaxBankSlots) slots = kMaxBankSlots;
                            if (slots <= 0) return;
                            std::vector<BankSlot> items((std::size_t)slots);
                            if (!rpm_bytes(s.proc, istart, items.data(),
                                           items.size() * sizeof(BankSlot))) return;
                            std::uint64_t hsh = 1469598103934665603ull;  // FNV-1a
                            const std::uint8_t* p = (const std::uint8_t*)items.data();
                            for (std::size_t bi = 0; bi < items.size() * sizeof(BankSlot); ++bi) {
                                hsh ^= p[bi]; hsh *= 1099511628211ull;
                            }
                            dst = std::move(items);
                            if (hsh != hashRef && !bank_key.empty() &&
                                WriteContainerCache(kind, bank_key, dst)) {
                                hashRef     = hsh;
                                cachedAtRef = (long long)std::time(nullptr);
                                rtx::log::Client(s.pid, std::string(kind) + " changed; cached " +
                                                 std::to_string(slots) + " slots for " + bank_key);
                            }
                        };
                        for (int c = 0; c < n && !(found_bank && found_metal && found_mats && found_group && found_bait && found_wb); ++c) {
                            const std::uint8_t* e = cbuf.data() + (std::size_t)c * kContainerStride;
                            int cid = *(const std::int32_t*)(e + 0x10);
                            if (cid == kBankContainerId && !found_bank) {
                                found_bank = true;
                                capture(e, s.bank_slots, s.bank_hash, s.bank_cached_at, "bank");
                            } else if (cid == kMetalBankContainerId && !found_metal) {
                                found_metal = true;
                                capture(e, s.metalbank_slots, s.metalbank_hash, s.metalbank_cached_at, "metalbank");
                            } else if (cid == kMaterialsContainerId && !found_mats) {
                                found_mats = true;
                                capture(e, s.materials_slots, s.materials_hash, s.materials_cached_at, "materials");
                            } else if (cid == kGroupBankContainerId && !found_group) {
                                found_group = true;
                                capture(e, s.groupbank_slots, s.groupbank_hash, s.groupbank_cached_at, "groupbank");
                            } else if (cid == kBaitBoxContainerId && !found_bait) {
                                found_bait = true;
                                capture(e, s.baitbox_slots, s.baitbox_hash, s.baitbox_cached_at, "baitbox");
                            } else if (cid == kWorkbenchContainerId && !found_wb) {
                                found_wb = true;
                                capture(e, s.workbench_slots, s.workbench_hash, s.workbench_cached_at, "workbench");
                            }
                        }
                    }
                }
            }
            s.bank_open = found_bank;
            snap.bank_open = found_bank;
            if (found_bank) {
                int filled = 0;
                for (const auto& it : s.bank_slots) if (it.item_id > 0) ++filled;
                snap.bank_count     = filled;
                snap.bank_cached_at = s.bank_cached_at;
            }
            s.metalbank_open = found_metal;
            snap.metalbank_open = found_metal;
            if (found_metal) {
                int filled = 0;
                for (const auto& it : s.metalbank_slots) if (it.item_id > 0) ++filled;
                snap.metalbank_count     = filled;
                snap.metalbank_cached_at = s.metalbank_cached_at;
            }
            s.materials_open = found_mats;
            snap.materials_open = found_mats;
            if (found_mats) {
                int filled = 0;
                for (const auto& it : s.materials_slots) if (it.item_id > 0) ++filled;
                snap.materials_count     = filled;
                snap.materials_cached_at = s.materials_cached_at;
            }
            s.groupbank_open = found_group;
            snap.groupbank_open = found_group;
            if (found_group) {
                int filled = 0;
                for (const auto& it : s.groupbank_slots) if (it.item_id > 0) ++filled;
                snap.groupbank_count     = filled;
                snap.groupbank_cached_at = s.groupbank_cached_at;
            }
            s.baitbox_open = found_bait;
            snap.baitbox_open = found_bait;
            if (found_bait) {
                int filled = 0;
                for (const auto& it : s.baitbox_slots) if (it.item_id > 0) ++filled;
                snap.baitbox_count     = filled;
                snap.baitbox_cached_at = s.baitbox_cached_at;
            }
            s.workbench_open = found_wb;
            snap.workbench_open = found_wb;
            if (found_wb) {
                int filled = 0;
                for (const auto& it : s.workbench_slots) if (it.item_id > 0) ++filled;
                snap.workbench_count     = filled;
                snap.workbench_cached_at = s.workbench_cached_at;
            }
        }
    }
    snap.status_label = status_label(snap.status);

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(s.proc,
            (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        snap.working_set_mb = (long long)(pmc.WorkingSetSize    / (1024 * 1024));
        snap.priv_bytes_mb  = (long long)(pmc.PrivateUsage      / (1024 * 1024));
    }

    // Per-process CPU %, normalised across all logical cores. Recompute
    // only when at least 500 ms have passed since the last baseline.
    FILETIME ftC, ftE, ftK, ftU;
    if (GetProcessTimes(s.proc, &ftC, &ftE, &ftK, &ftU)) {
        ULONGLONG total = ft_to_100ns(ftK) + ft_to_100ns(ftU);
        ULONGLONG now   = GetTickCount64();
        if (s.cpu_sample_wall_ms == 0) {
            s.cpu_sample_total_100ns = total;
            s.cpu_sample_wall_ms     = now;
        } else {
            ULONGLONG dt_ms = now - s.cpu_sample_wall_ms;
            if (dt_ms >= 500) {
                ULONGLONG dt_100ns = dt_ms * 10000ull;
                ULONGLONG d_cpu    = (total >= s.cpu_sample_total_100ns)
                                       ? total - s.cpu_sample_total_100ns : 0;
                double pct = 100.0 * (double)d_cpu
                             / ((double)dt_100ns * (double)logical_cpu_count());
                if (pct < 0)   pct = 0;
                if (pct > 100) pct = 100;
                s.cached_cpu_pct         = pct;
                s.cpu_sample_total_100ns = total;
                s.cpu_sample_wall_ms     = now;
            }
        }
        snap.cpu_pct = s.cached_cpu_pct;
    }

    // Tick fields are kept hot by the fast-poll thread; just read them.
    snap.tick_count   = s.current_tick;
    snap.last_tick_ms = s.last_dt_ms;

    snap.in_world = snap.world > 0;
    return snap;
}

}  // namespace

std::vector<Snapshot> SampleAll() {
    ensure_fast_thread_started();

    // Enumerate the process table BEFORE taking g_mu: CreateToolhelp32Snapshot can take several ms
    // on a busy box and touches no shared state, so there is no reason to hold the lock across it.
    auto live_pids = find_all_pids(L"rs2client.exe");
    std::unordered_set<DWORD> live(live_pids.begin(), live_pids.end());

    std::lock_guard<std::mutex> lk(g_mu);

    for (auto it = g_states.begin(); it != g_states.end(); ) {
        if (!live.count(it->first) || !process_alive(it->second.proc)) {
            DWORD code = 0;
            if (it->second.proc) GetExitCodeProcess(it->second.proc, &code);
            rtx::log::Client(it->first,
                "process exited (code " + std::to_string(code) + "); detaching");
            rtx::log::CloseClient(it->first);
            release(it->second);
            { std::lock_guard<std::mutex> lk2(g_pinfo_mu); g_pinfo_prevpos.erase(it->first); }
            it = g_states.erase(it);
        } else ++it;
    }

    // Retry per-sample for anything that wasn't ready at attach time:
    // JX_DISPLAY_NAME (filled when the user picks a character), and the
    // two static globals (rare, but if attach happened before the image
    // was fully mapped they'd be 0). Cheap once cached. Log each the first
    // time it resolves so the per-PID log shows when the client became fully
    // readable.
    for (auto& [pid, s] : g_states) {
        if (!s.proc) continue;
        if (s.display_name.empty()) {
            s.display_name = read_target_env(s.proc, L"JX_DISPLAY_NAME");
            if (!s.display_name.empty())
                rtx::log::Client(pid, "resolved display name: " + s.display_name);
        }
        if (s.main_global_va == 0 && s.mod_base) {
            s.main_global_va = resolve_main_global(s.proc, s.mod_base, s.mod_size);
            if (s.main_global_va) rtx::log::Client(pid, "resolved MainData global");
        }
        if (s.tick_owner_va == 0 && s.mod_base) {
            s.tick_owner_va = resolve_tick_owner_global(s.proc, s.mod_base, s.mod_size);
            if (s.tick_owner_va) rtx::log::Client(pid, "resolved tick-owner global");
        }
    }

    for (DWORD pid : live_pids) {
        if (g_states.count(pid)) continue;
        State s;
        if (attach_state(s, pid)) {
            g_states.emplace(pid, std::move(s));
        }
    }

    std::vector<Snapshot> out;
    out.reserve(g_states.size());
    for (auto& [pid, s] : g_states) out.push_back(sample_one(s));
    std::sort(out.begin(), out.end(),
              [](const Snapshot& a, const Snapshot& b){ return a.pid < b.pid; });
    return out;
}

namespace {
std::string json_escape(const std::string& v) {
    std::string o; o.reserve(v.size() + 4);
    for (char c : v) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8]; std::snprintf(buf, 8, "\\u%04x", (unsigned char)c);
                    o += buf;
                } else {
                    o.push_back(c);
                }
        }
    }
    return o;
}
}  // namespace

namespace {
// The samples JSON (the always-on gameSnapshots poll, hit several times a second by every
// client window AND the launcher) is built on a BACKGROUND thread and served from this cache,
// so the UI thread never builds it. That matters beyond throughput: when a game client is
// embedded, the launcher's UI thread must stay free to pump the host's activation messages, or
// the game's cross-process focus handshake -- and therefore its render -- stalls on every
// window switch. SampleAll (process-table snapshot + attach + an N-process RPM storm + an
// occasional bank-cache disk write) is exactly that multi-hundred-ms block, so it runs here.
std::mutex        s_samples_mu;
std::string       s_samples_json;
std::atomic<bool> s_sampler_started{false};

std::string BuildSamplesJson() {
    auto snaps = SampleAll();
    std::string out = "[";
    // Sized for the whole per-client record: every storage box adds ~60 chars of field names plus
    // its values, and snprintf would silently TRUNCATE into malformed JSON rather than overflow.
    char buf[1536];
    for (size_t i = 0; i < snaps.size(); ++i) {
        const auto& s = snaps[i];
        if (i) out.push_back(',');
        std::snprintf(buf, sizeof(buf),
            "{\"pid\":%u,\"client_version\":\"%s\","
             "\"in_world\":%s,\"world\":%d,\"status\":%d,\"status_label\":\"%s\","
             "\"display_name\":\"%s\","
             "\"tick_count\":%llu,\"last_tick_ms\":%.1f,"
             "\"working_set_mb\":%lld,\"priv_bytes_mb\":%lld,\"cpu_pct\":%.1f,"
             "\"bank_open\":%s,\"bank_count\":%d,\"bank_cached_at\":%lld,"
             "\"metalbank_open\":%s,\"metalbank_count\":%d,\"metalbank_cached_at\":%lld,"
             "\"materials_open\":%s,\"materials_count\":%d,\"materials_cached_at\":%lld,"
             "\"groupbank_open\":%s,\"groupbank_count\":%d,\"groupbank_cached_at\":%lld,"
             "\"baitbox_open\":%s,\"baitbox_count\":%d,\"baitbox_cached_at\":%lld,"
             "\"workbench_open\":%s,\"workbench_count\":%d,\"workbench_cached_at\":%lld,"
             "\"ge_slots\":[",
            s.pid,
            json_escape(s.client_version).c_str(),
            s.in_world ? "true" : "false",
            s.world, s.status, json_escape(s.status_label).c_str(),
            json_escape(s.display_name).c_str(),
            (unsigned long long)s.tick_count,
            s.last_tick_ms,
            s.working_set_mb, s.priv_bytes_mb, s.cpu_pct,
            s.bank_open ? "true" : "false", s.bank_count, s.bank_cached_at,
            s.metalbank_open ? "true" : "false", s.metalbank_count, s.metalbank_cached_at,
            s.materials_open ? "true" : "false", s.materials_count, s.materials_cached_at,
            s.groupbank_open ? "true" : "false", s.groupbank_count, s.groupbank_cached_at,
            s.baitbox_open ? "true" : "false", s.baitbox_count, s.baitbox_cached_at,
            s.workbench_open ? "true" : "false", s.workbench_count, s.workbench_cached_at);
        out += buf;
        for (size_t k = 0; k < s.ge_slots.size(); ++k) {
            const auto& g = s.ge_slots[k];
            if (k) out.push_back(',');
            std::snprintf(buf, sizeof(buf),
                "{\"slot\":%d,\"status\":%d,\"type\":%d,\"item_id\":%d,"
                 "\"price\":%lld,\"quantity\":%d,\"filled\":%d,\"filled_value\":%lld}",
                g.slot, g.status, g.type, g.item_id,
                g.price, g.quantity, g.filled, g.filled_value);
            out += buf;
        }
        out += "],\"skills\":[";
        for (size_t k = 0; k < s.skills.size(); ++k) {
            if (k) out.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%d,%d,%d]",
                          s.skills[k].real, s.skills[k].boosted, s.skills[k].xp);
            out += buf;
        }
        out += "]}";
    }
    out.push_back(']');
    return out;
}

void sample_loop() {
    // ~150 ms keeps the snapshot as fresh as the 250 ms UI refresh needs without spinning.
    while (true) {
        std::string j = BuildSamplesJson();
        { std::lock_guard<std::mutex> lk(s_samples_mu); s_samples_json.swap(j); }
        Sleep(150);
    }
}

// ---- generic off-thread per-panel reads ------------------------------------
// Each per-tab bridge read (scene/bank/inventory/perks/...) would otherwise do a synchronous
// cross-process RPM on the UI thread when its tab is open -- a smaller cousin of the SampleAll
// block above, but still enough to add a frame or two to the activation handshake on a window
// switch. They're served the same way: the UI thread registers a (key -> builder) and gets the
// last cached value instantly; this background thread keeps every actively-polled key fresh.
// Entries not polled for ~2 s (tab closed) are dropped.
struct AsyncEntry {
    std::function<std::string()>          build;
    std::string                           value;
    bool                                  has_value = false;
    std::chrono::steady_clock::time_point last_used;
};
std::mutex                                  s_async_mu;
std::unordered_map<std::string, AsyncEntry> s_async;

void panel_loop() {
    using namespace std::chrono;
    std::vector<std::pair<std::string, std::function<std::string()>>> jobs;
    while (true) {
        jobs.clear();
        {
            std::lock_guard<std::mutex> lk(s_async_mu);
            auto now = steady_clock::now();
            for (auto it = s_async.begin(); it != s_async.end(); ) {
                // 5 s, comfortably above the slowest panel poll throttle (Pets/Bosses self-throttle
                // to ~2 s), so an actively-shown tab's entry is never evicted between its polls --
                // which would otherwise make it go cold and flash the empty default every cycle.
                if (now - it->second.last_used > seconds(5)) { it = s_async.erase(it); continue; }
                jobs.emplace_back(it->first, it->second.build);
                ++it;
            }
        }
        for (auto& [key, build] : jobs) {
            std::string v = build();          // heavy RPM, OFF the UI thread (builder takes g_mu)
            std::lock_guard<std::mutex> lk(s_async_mu);
            auto it = s_async.find(key);
            if (it != s_async.end()) { it->second.value = std::move(v); it->second.has_value = true; }
        }
        Sleep(jobs.empty() ? 60 : 100);       // refresh active reads ~10x/s; idle-poll otherwise
    }
}

void ensure_sampler_started() {
    bool expected = false;
    if (s_sampler_started.compare_exchange_strong(expected, true)) {
        std::thread(sample_loop).detach();
        std::thread(panel_loop).detach();
    }
}
}  // namespace

// Serve a per-panel read from the background cache: returns the last cached value instantly (or
// "" before the first background build) and registers the builder so panel_loop keeps it fresh.
// Keeps cross-process RPM off the UI thread so it stays free to pump the embedded host's
// activation. `build` must be self-contained (it runs on the background thread).
std::string ReadAsync(const std::string& key, std::function<std::string()> build) {
    ensure_sampler_started();
    {
        std::lock_guard<std::mutex> lk(s_async_mu);
        auto& e = s_async[key];
        e.build = build;                                  // keep the latest builder
        e.last_used = std::chrono::steady_clock::now();
        if (e.has_value) return e.value;                  // warm: serve the background-refreshed cache
    }
    // Cold (first poll, or just after an eviction): build ONCE inline so the caller gets real
    // data immediately instead of the empty default -- otherwise a panel that reads "no data" as
    // a real state (e.g. Pets -> every pet "locked") flashes wrong until the next poll. One-time
    // per key; steady-state stays off the UI thread.
    std::string v = build();
    std::lock_guard<std::mutex> lk(s_async_mu);
    auto& e = s_async[key];
    e.value = v; e.has_value = true;
    e.last_used = std::chrono::steady_clock::now();
    return v;
}

std::string SamplesJson() {
    ensure_sampler_started();
    {
        std::lock_guard<std::mutex> lk(s_samples_mu);
        if (!s_samples_json.empty()) return s_samples_json;   // steady state: just serve the cache
    }
    // First call before the background thread has produced anything: build once so the UI isn't
    // blank at startup. One-time only -- not on the steady-state hot path.
    std::string j = BuildSamplesJson();
    std::lock_guard<std::mutex> lk(s_samples_mu);
    if (s_samples_json.empty()) s_samples_json = j;
    return s_samples_json;
}

std::string BankJson(std::uint32_t pid) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;          // copy of the in-memory cache (if any)
    long long             mem_cached_at = 0;

    // Copy the cached slots out under a BRIEF lock, then build the JSON -- and read the disk
    // cache -- without holding g_mu, so a refresh never stalls the tick/overlay.
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            character = !it->second.display_name.empty()
                          ? it->second.display_name : it->second.character;
            if (!it->second.bank_slots.empty()) {
                mem_slots     = it->second.bank_slots;
                mem_cached_at = it->second.bank_cached_at;
                open          = it->second.bank_open;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    std::vector<BankSlot> from_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
    }
    if (!items) {
        BankCacheData d = ReadBankCache(character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            cached_at = d.cached_at;
            items     = &from_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;   // in-memory fallback
        }
    }

    std::string out = "{\"open\":";
    out += open ? "true" : "false";
    out += ",\"character\":\"" + json_escape(character) + "\"";
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), ",\"cached_at\":%lld,\"items\":[", cached_at);
    out += hdr;

    int count = 0;
    if (items) {
        char buf[96];
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            if (count) out.push_back(',');
            // [slot, item_id, stack, "name"] -- name resolved here (memoized)
            // so the UI can search by name without per-item bridge calls.
            std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, bs.item_id, bs.stack);
            out += buf;
            out += json_escape(rtx::cache::ItemName(bs.item_id));
            out += "\"]";
            ++count;
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

// Metal bank (container 858) -- mirrors BankJson: live slots while open, else the encrypted
// per-character disk cache, else the in-memory fallback. Item names resolved here (memoized).
std::string MetalBankJson(std::uint32_t pid) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;          // copy of the in-memory cache (if any)
    long long             mem_cached_at = 0;

    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            character = !it->second.display_name.empty()
                          ? it->second.display_name : it->second.character;
            if (!it->second.metalbank_slots.empty()) {
                mem_slots     = it->second.metalbank_slots;
                mem_cached_at = it->second.metalbank_cached_at;
                open          = it->second.metalbank_open;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    std::vector<BankSlot> from_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
    }
    if (!items) {
        BankCacheData d = ReadContainerCache("metalbank", character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            cached_at = d.cached_at;
            items     = &from_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;
        }
    }

    std::string out = "{\"open\":";
    out += open ? "true" : "false";
    out += ",\"character\":\"" + json_escape(character) + "\"";
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), ",\"cached_at\":%lld,\"items\":[", cached_at);
    out += hdr;
    int count = 0;
    if (items) {
        char buf[96];
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            if (count) out.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, bs.item_id, bs.stack);
            out += buf;
            out += json_escape(rtx::cache::ItemName(bs.item_id));
            out += "\"]";
            ++count;
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

// Archaeology material storage (container 885) -- mirrors MetalBankJson: live slots while the
// storage UI is open, else the encrypted per-character disk cache, else the in-memory fallback.
std::string MaterialsJson(std::uint32_t pid) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;          // copy of the in-memory cache (if any)
    long long             mem_cached_at = 0;

    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            character = !it->second.display_name.empty()
                          ? it->second.display_name : it->second.character;
            if (!it->second.materials_slots.empty()) {
                mem_slots     = it->second.materials_slots;
                mem_cached_at = it->second.materials_cached_at;
                open          = it->second.materials_open;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    std::vector<BankSlot> from_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
    }
    if (!items) {
        BankCacheData d = ReadContainerCache("materials", character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            cached_at = d.cached_at;
            items     = &from_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;
        }
    }

    std::string out = "{\"open\":";
    out += open ? "true" : "false";
    out += ",\"character\":\"" + json_escape(character) + "\"";
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), ",\"cached_at\":%lld,\"items\":[", cached_at);
    out += hdr;
    int count = 0;
    if (items) {
        char buf[96];
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            if (count) out.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, bs.item_id, bs.stack);
            out += buf;
            out += json_escape(rtx::cache::ItemName(bs.item_id));
            out += "\"]";
            ++count;
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

// Anachronia bait box (container 867) -- mirrors MetalBankJson: live slots while the bait-box UI
// is open, else the encrypted per-character disk cache, else the in-memory fallback.
std::string BaitBoxJson(std::uint32_t pid) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;          // copy of the in-memory cache (if any)
    long long             mem_cached_at = 0;

    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            character = !it->second.display_name.empty()
                          ? it->second.display_name : it->second.character;
            if (!it->second.baitbox_slots.empty()) {
                mem_slots     = it->second.baitbox_slots;
                mem_cached_at = it->second.baitbox_cached_at;
                open          = it->second.baitbox_open;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    std::vector<BankSlot> from_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
    }
    if (!items) {
        BankCacheData d = ReadContainerCache("baitbox", character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            cached_at = d.cached_at;
            items     = &from_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;
        }
    }

    std::string out = "{\"open\":";
    out += open ? "true" : "false";
    out += ",\"character\":\"" + json_escape(character) + "\"";
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), ",\"cached_at\":%lld,\"items\":[", cached_at);
    out += hdr;
    int count = 0;
    if (items) {
        char buf[96];
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            if (count) out.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, bs.item_id, bs.stack);
            out += buf;
            out += json_escape(rtx::cache::ItemName(bs.item_id));
            out += "\"]";
            ++count;
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

// Archaeologist's workbench damaged-artefact storage (container 1008) -- mirrors BaitBoxJson:
// live slots while the workbench UI is open, else the encrypted per-character disk cache, else
// the in-memory fallback. Empty is a legitimate state: the container holds nothing until the
// first guild-shop workbench upgrade is bought (varbit 61463).
std::string WorkbenchJson(std::uint32_t pid) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;          // copy of the in-memory cache (if any)
    long long             mem_cached_at = 0;

    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            character = !it->second.display_name.empty()
                          ? it->second.display_name : it->second.character;
            if (!it->second.workbench_slots.empty()) {
                mem_slots     = it->second.workbench_slots;
                mem_cached_at = it->second.workbench_cached_at;
                open          = it->second.workbench_open;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    std::vector<BankSlot> from_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
    }
    if (!items) {
        BankCacheData d = ReadContainerCache("workbench", character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            cached_at = d.cached_at;
            items     = &from_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;
        }
    }

    std::string out = "{\"open\":";
    out += open ? "true" : "false";
    out += ",\"character\":\"" + json_escape(character) + "\"";
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), ",\"cached_at\":%lld,\"items\":[", cached_at);
    out += hdr;
    int count = 0;
    if (items) {
        char buf[96];
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            if (count) out.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, bs.item_id, bs.stack);
            out += buf;
            out += json_escape(rtx::cache::ItemName(bs.item_id));
            out += "\"]";
            ++count;
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

// Group Ironman shared bank (container 963) -- mirrors MetalBankJson: live slots while the group
// storage UI is open, else the encrypted per-character disk cache, else the in-memory fallback.
std::string GroupBankJson(std::uint32_t pid) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;          // copy of the in-memory cache (if any)
    long long             mem_cached_at = 0;

    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            character = !it->second.display_name.empty()
                          ? it->second.display_name : it->second.character;
            if (!it->second.groupbank_slots.empty()) {
                mem_slots     = it->second.groupbank_slots;
                mem_cached_at = it->second.groupbank_cached_at;
                open          = it->second.groupbank_open;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    std::vector<BankSlot> from_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
    }
    if (!items) {
        BankCacheData d = ReadContainerCache("groupbank", character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            cached_at = d.cached_at;
            items     = &from_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;
        }
    }

    std::string out = "{\"open\":";
    out += open ? "true" : "false";
    out += ",\"character\":\"" + json_escape(character) + "\"";
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), ",\"cached_at\":%lld,\"items\":[", cached_at);
    out += hdr;
    int count = 0;
    if (items) {
        char buf[96];
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            if (count) out.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, bs.item_id, bs.stack);
            out += buf;
            out += json_escape(rtx::cache::ItemName(bs.item_id));
            out += "\"]";
            ++count;
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

// Walk the interface-container manager for `container_id` and emit
//   {"present":bool,"count":N,"cap":N,"items":[[slot,item_id,stack,"name"],..]}
// Same chain the bank/perks readers use: root + kOffInvData -> mgr; the entry
// array [+0x8,+0x10) has stride 0x48 with id@+0x10 and the slot array at
// [+0x18,+0x20) (stride 0x8: item_id@+0, stack@+4). Filled slots only; the
// slot number is the array index. `h`/`root` are pre-resolved by the caller (no lock held).
static std::string container_items_json(HANDLE h, std::uint64_t root, int container_id) {
    const char* kAbsent = "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}";
    auto cmgr = rpm<std::uint64_t>(h, root + kOffInvData);
    if (!cmgr || *cmgr <= 0x10000) return kAbsent;
    auto cstart = rpm<std::uint64_t>(h, *cmgr + 0x8);
    auto cend   = rpm<std::uint64_t>(h, *cmgr + 0x10);
    if (!cstart || !cend || *cstart <= 0x10000 || *cend <= *cstart) return kAbsent;
    int ncont = (int)((*cend - *cstart) / kContainerStride);
    if (ncont > kMaxContainers) ncont = kMaxContainers;
    for (int c = 0; c < ncont; ++c) {
        std::uint64_t e = *cstart + (std::uint64_t)c * kContainerStride;
        if (rpm<std::int32_t>(h, e + 0x10).value_or(-1) != container_id) continue;
        auto istart = rpm<std::uint64_t>(h, e + 0x18);
        auto iend   = rpm<std::uint64_t>(h, e + 0x20);
        if (!istart || !iend || *istart <= 0x10000 || *iend < *istart)
            return "{\"present\":true,\"count\":0,\"cap\":0,\"items\":[]}";
        int cap = (int)((*iend - *istart) / 0x8);
        if (cap > kMaxBankSlots) cap = kMaxBankSlots;
        std::string items; int count = 0; char buf[96];
        for (int s = 0; s < cap; ++s) {
            std::uint64_t slotAddr = *istart + (std::uint64_t)s * 0x8;
            int iid   = rpm<std::int32_t>(h, slotAddr).value_or(0);
            if (iid <= 0) continue;
            int stack = rpm<std::int32_t>(h, slotAddr + 0x4).value_or(0);
            if (count) items.push_back(',');
            std::snprintf(buf, sizeof(buf), "[%d,%d,%d,\"", s, iid, stack);
            items += buf;
            items += json_escape(rtx::cache::ItemName(iid));
            items += "\"]";
            ++count;
        }
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "{\"present\":true,\"count\":%d,\"cap\":%d,\"items\":[", count, cap);
        std::string out = hdr; out += items; out += "]}";
        return out;
    }
    return kAbsent;
}

static std::string container_json_for(std::uint32_t pid, int container_id) {
    const char* kAbsent = "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kAbsent;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kAbsent;
    return container_items_json(h, *root, container_id);
}

std::string InventoryJson(std::uint32_t pid) { return container_json_for(pid, 93); }
std::string ContainerItemsJson(std::uint32_t pid, int container_id) { return container_json_for(pid, container_id); }

// Enumerate EVERY container live in the interface-container manager (most exist in memory only while
// their window is open). Returns each one's id + filled count + capacity; the Containers panel maps
// known ids to their dedicated tab and shows unknown ones inline. Same walk as container_items_json,
// lock-free via snap_proc. Counts via one block read per container (cheap; only this tab polls it).
std::string OpenContainersJson(std::uint32_t pid) {
    const char* kEmpty = "{\"containers\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto cmgr = rpm<std::uint64_t>(h, *root + kOffInvData);
    if (!cmgr || *cmgr <= 0x10000) return kEmpty;
    auto cstart = rpm<std::uint64_t>(h, *cmgr + 0x8);
    auto cend   = rpm<std::uint64_t>(h, *cmgr + 0x10);
    if (!cstart || !cend || *cstart <= 0x10000 || *cend <= *cstart) return kEmpty;
    int ncont = (int)((*cend - *cstart) / kContainerStride);
    if (ncont > kMaxContainers) ncont = kMaxContainers;
    std::string out = "{\"containers\":[";
    int emitted = 0;
    for (int c = 0; c < ncont; ++c) {
        std::uint64_t e = *cstart + (std::uint64_t)c * kContainerStride;
        int id = rpm<std::int32_t>(h, e + 0x10).value_or(-1);
        if (id < 0) continue;
        auto istart = rpm<std::uint64_t>(h, e + 0x18);
        auto iend   = rpm<std::uint64_t>(h, e + 0x20);
        int cap = 0, count = 0;
        if (istart && iend && *istart > 0x10000 && *iend >= *istart) {
            cap = (int)((*iend - *istart) / 0x8);
            if (cap > kMaxBankSlots) cap = kMaxBankSlots;
            if (cap > 0) {
                std::vector<std::uint8_t> sb((std::size_t)cap * 0x8);
                if (rpm_bytes(h, *istart, sb.data(), sb.size())) {
                    for (int s = 0; s < cap; ++s)
                        if (*(const std::int32_t*)(sb.data() + (std::size_t)s * 0x8) > 0) ++count;
                }
            }
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s{\"id\":%d,\"count\":%d,\"cap\":%d}",
                      emitted ? "," : "", id, count, cap);
        out += buf;
        ++emitted;
    }
    out += "]}";
    return out;
}

// Player-Owned Farm pens (containers 851-857). Each slot is an animal: item id = species (+checked
// status via the item name), and the per-slot inv-var at INDEX 2 holds the primary trait in its low 5
// bits, matching the in-game trait panel (var2 & 0x1F -> trait enum 14340: 4 Sullen,
// 11 Stingy, 15 Jovial, 16 Robust, 22 Producer, 27 Regular, ...). Pens are in the container manager
// only while their UI is open, so -- exactly like the bank -- each pen is snapshotted while open
// persist it per-account to disk (kind "pen<id>", change-guarded), reading the cache back when closed.
//   {"pens":[{"id":N,"open":bool,"cached_at":sec,"animals":[[item,trait,"species"],..]},..]}
std::string PofJson(std::uint32_t pid) {
    const char* kEmpty = "{\"pens\":[]}";
    // account key for the per-pen disk cache (brief lock; PofJson is only polled while its tab is open)
    std::string key;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end())
            key = !it->second.display_name.empty() ? it->second.display_name : it->second.character;
    }
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto rootp = rpm<std::uint64_t>(h, ps.mgva);
    std::uint64_t root = (rootp && *rootp > 0x10000) ? *rootp : 0;
    std::uint64_t cstart = 0, cend = 0; int ncont = 0;
    if (root) {
        auto cmgr = rpm<std::uint64_t>(h, root + kOffInvData);
        if (cmgr && *cmgr > 0x10000) {
            auto a = rpm<std::uint64_t>(h, *cmgr + 0x8);
            auto b = rpm<std::uint64_t>(h, *cmgr + 0x10);
            if (a && b && *a > 0x10000 && *b > *a) {
                cstart = *a; cend = *b;
                ncont = (int)((cend - cstart) / kContainerStride);
                if (ncont > kMaxContainers) ncont = kMaxContainers;
            }
        }
    }
    static std::mutex s_penHashMu;
    static std::unordered_map<std::uint64_t, std::uint64_t> s_penHash;   // (pid<<16|pen) -> last written hash
    static const int kPens[] = { 851, 852, 853, 854, 855, 856, 857 };
    std::string out = "{\"pens\":["; bool firstPen = true;
    for (int pen : kPens) {
        std::vector<BankSlot> slots; bool open = false; long long cached_at = 0;
        std::uint64_t e = 0;
        for (int c = 0; c < ncont; ++c) {
            std::uint64_t ee = cstart + (std::uint64_t)c * kContainerStride;
            if (rpm<std::int32_t>(h, ee + 0x10).value_or(-1) == pen) { e = ee; break; }
        }
        if (e) {
            auto istart = rpm<std::uint64_t>(h, e + 0x18);
            auto iend   = rpm<std::uint64_t>(h, e + 0x20);
            auto xbase  = rpm<std::uint64_t>(h, e + 0x30);
            if (istart && iend && *istart > 0x10000 && *iend >= *istart) {
                open = true;
                int ns = (int)((*iend - *istart) / 0x8);
                if (ns > 64) ns = 64;                                   // pens are tiny
                for (int s = 0; s < ns; ++s) {
                    int item = rpm<std::int32_t>(h, *istart + (std::uint64_t)s * 8).value_or(0);
                    if (item <= 0) continue;
                    int trait = -1;
                    if (xbase && *xbase > 0x10000) {
                        std::uint64_t X = *xbase + (std::uint64_t)s * 0x38;
                        auto parr = rpm<std::uint64_t>(h, X + 0x10);
                        int cnt = rpm<std::int32_t>(h, X + 0x18).value_or(0);
                        if (parr && *parr > 0x10000 && cnt > 0 && cnt <= 32) {
                            for (int j = 0; j < cnt; ++j) {
                                auto p = rpm<std::uint64_t>(h, *parr + (std::uint64_t)j * 8);
                                if (!p || *p <= 0x10000) continue;
                                if (rpm<std::int32_t>(h, *p).value_or(-1) == 2) {           // inv-var index 2
                                    trait = rpm<std::int32_t>(h, *p + 8).value_or(0) & 0x1F; // low 5 bits = trait
                                    break;
                                }
                            }
                        }
                    }
                    BankSlot bs; bs.item_id = item; bs.stack = trait; slots.push_back(bs);
                }
            }
        }
        std::string kind = "pen" + std::to_string(pen);
        if (open) {
            cached_at = (long long)std::time(nullptr);
            if (!key.empty()) {
                std::uint64_t hsh = 1469598103934665603ull;
                for (auto& bs : slots) {
                    hsh = (hsh ^ (std::uint64_t)(std::uint32_t)bs.item_id) * 1099511628211ull;
                    hsh = (hsh ^ (std::uint64_t)(std::uint32_t)bs.stack)   * 1099511628211ull;
                }
                std::uint64_t hk = ((std::uint64_t)pid << 16) | (std::uint32_t)pen; bool changed;
                { std::lock_guard<std::mutex> lk(s_penHashMu); changed = (s_penHash[hk] != hsh); if (changed) s_penHash[hk] = hsh; }
                if (changed) WriteContainerCache(kind, key, slots);
            }
        } else if (!key.empty()) {
            BankCacheData d = ReadContainerCache(kind, key);
            slots = std::move(d.slots); cached_at = d.cached_at;
        }
        if (!firstPen) out.push_back(','); firstPen = false;
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "{\"id\":%d,\"open\":%s,\"cached_at\":%lld,\"animals\":[",
                      pen, open ? "true" : "false", cached_at);
        out += hdr;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i) out.push_back(',');
            char b[64];
            std::snprintf(b, sizeof(b), "[%d,%d,\"", slots[i].item_id, slots[i].stack);
            out += b; out += json_escape(rtx::cache::ItemName(slots[i].item_id)); out += "\"]";
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

// Read the per-slot Extra_ints of the first `item_id` in `container_id` (same ext-data layout the
// perks reader uses: entry+0x30 -> slot*0x38 -> {ptrArr@+0x10, count@+0x18}; each ptr = key@+0,
// value@+8). Returns {"present":bool,"key":[v@key0..15],"pos":[v in pointer order]} so callers can use
// whichever indexing is correct (base gem bag = Extra_ints[1] = packed sapphire/em/ruby/diamond).
std::string ItemExtraIntsJson(std::uint32_t pid, int container_id, int item_id) {
    const char* kAbsent = "{\"present\":false,\"key\":[],\"pos\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kAbsent;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kAbsent;
    auto cmgr = rpm<std::uint64_t>(h, *root + kOffInvData);
    if (!cmgr || *cmgr <= 0x10000) return kAbsent;
    auto cstart = rpm<std::uint64_t>(h, *cmgr + 0x8);
    auto cend   = rpm<std::uint64_t>(h, *cmgr + 0x10);
    if (!cstart || !cend || *cstart <= 0x10000 || *cend <= *cstart) return kAbsent;
    int ncont = (int)((*cend - *cstart) / kContainerStride);
    if (ncont > kMaxContainers) ncont = kMaxContainers;
    for (int c = 0; c < ncont; ++c) {
        std::uint64_t e = *cstart + (std::uint64_t)c * kContainerStride;
        if (rpm<std::int32_t>(h, e + 0x10).value_or(-1) != container_id) continue;
        auto istart = rpm<std::uint64_t>(h, e + 0x18);
        auto iend   = rpm<std::uint64_t>(h, e + 0x20);
        auto xstart = rpm<std::uint64_t>(h, e + 0x30);
        if (!istart || !iend || *istart <= 0x10000 || *iend <= *istart || !xstart || *xstart <= 0x10000) return kAbsent;
        int nslot = (int)((*iend - *istart) / 0x8);
        if (nslot > kMaxBankSlots) nslot = kMaxBankSlots;
        for (int s = 0; s < nslot; ++s) {
            if (rpm<std::int32_t>(h, *istart + (std::uint64_t)s * 0x8).value_or(0) != item_id) continue;
            std::uint64_t X = *xstart + (std::uint64_t)s * 0x38;
            int count = rpm<std::int32_t>(h, X + 0x18).value_or(0);
            auto ptrArr = rpm<std::uint64_t>(h, X + 0x10);
            int byKey[16] = {0}; std::string pos; int np = 0; char buf[16];
            if (count > 0 && count <= 32 && ptrArr && *ptrArr > 0x10000) {
                for (int j = 0; j < count; ++j) {
                    auto p = rpm<std::uint64_t>(h, *ptrArr + (std::uint64_t)j * 0x8);
                    if (!p || *p <= 0x10000) continue;
                    int key = rpm<std::int32_t>(h, *p).value_or(-1);
                    int v   = rpm<std::int32_t>(h, *p + 0x8).value_or(0);
                    if (key >= 0 && key < 16) byKey[key] = v;
                    std::snprintf(buf, sizeof(buf), "%s%d", np ? "," : "", v); pos += buf; ++np;
                }
            }
            std::string out = "{\"present\":true,\"key\":[";
            for (int k = 0; k < 16; ++k) { std::snprintf(buf, sizeof(buf), "%s%d", k ? "," : "", byKey[k]); out += buf; }
            out += "],\"pos\":["; out += pos; out += "]}";
            return out;
        }
        return kAbsent;   // container present but item not held
    }
    return kAbsent;
}
std::string EquipmentJson(std::uint32_t pid) { return container_json_for(pid, 94); }

// Read one player varp by id from the MainData+0x36040 hashmap. Returns 0 for an
// unset varp (no node) -- which is exactly the engine's default, so callers can
// treat "absent" and 0 identically. `h`/`root` pre-resolved + g_mu held.
static int read_varp(HANDLE h, std::uint64_t root, int varp_id) {
    if (varp_id < 0) return 0;
    std::uint64_t hashRoot = root + kOffVarpHash;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 2000000) return 0;   // upper bound: a stale offset reads garbage as the bucket count
    std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varp_id % div) * 8).value_or(0);
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (rpm<std::int32_t>(h, node).value_or(-1) == varp_id)
            return rpm<std::int32_t>(h, node + 0x8).value_or(0);   // tag 0/1 -> int at +0x8
        node = rpm<std::uint64_t>(h, node + 0x28).value_or(0);
    }
    return 0;
}

static constexpr std::size_t kVarcStrCap = 1024;   // longest live tooltips/examines seen ~300 B; bound every string read

// Read an EASTL basic_string living at `strbase` out of the target process into `out` (raw bytes,
// capped at kVarcStrCap). Returns false unless the object parses as a sane string. EASTL packs the
// heap/SSO discriminator into the top byte of the object (strbase+0x17): its high bit set => heap
// layout {char* @+0, size @+8, capacity @+0x10}; clear => SSO with the 23-byte inline buffer at
// strbase+0 and (0x17 - flag) chars used. Same string class the companion reads off
// the CS2 VM string stack.
static bool read_eastl_string(HANDLE h, std::uint64_t strbase, std::string& out) {
    out.clear();
    std::uint8_t flag = rpm<std::uint8_t>(h, strbase + 0x17).value_or(0);
    std::size_t size; std::uint64_t src;
    if (flag & 0x80) {                                        // heap
        src  = rpm<std::uint64_t>(h, strbase + 0x0).value_or(0);
        std::uint64_t sz = rpm<std::uint64_t>(h, strbase + 0x8).value_or(0);
        if (src <= 0x10000 || src > 0x00007FFFFFFFFFFFull || sz == 0 || sz > 0x2000) return false;
        size = (std::size_t)std::min<std::uint64_t>(sz, kVarcStrCap);
    } else {                                                  // SSO (short string optimisation)
        // stored byte = 0x17 - length. Accept length 1..22; reject 0 (empty) and the flag==0 case
        // (a full 23-char inline string) -- flag==0 is indistinguishable from a zeroed int node, so
        // treating it as absent keeps the whole-map dump clean.
        if (flag == 0 || flag > 0x16) return false;
        size = (std::size_t)(0x17 - flag);
        src  = strbase;
    }
    std::string tmp; tmp.resize(size);
    if (!rpm_bytes(h, src, tmp.data(), size)) return false;
    out.swap(tmp);
    return true;
}

// Read one varc-int (VarClientInt) by id from the GLOBAL client-var hashmap. Unlike varps (a MainData-
// embedded map) and unlike the script-VM-context path the companion observes, varc-ints live in a global
// store reachable purely externally (varc 1323 = the compass dig tile):
//   client  = *ps.mgva (MainData);  store = *(client + 0x198E0);  hashmap = store + 0x7630
//   buckets = *(hashmap + 0x8);     count = *(hashmap + 0x10)
//   node = buckets[id % count]; chain @+0x28; id (i32) @+0; value (i32) @+8.  Absent/unset -> 0 (engine default).
static int read_varc(HANDLE h, std::uint64_t client, int varc_id) {
    if (varc_id < 0 || client <= 0x10000) return 0;
    std::uint64_t store = rpm<std::uint64_t>(h, client + 0x198E0).value_or(0);
    if (store <= 0x10000) return 0;
    std::uint64_t hm = store + 0x7630;
    std::uint64_t ba = rpm<std::uint64_t>(h, hm + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hm + 0x10).value_or(0);
    if (ba <= 0x10000 || ba > 0x00007FFFFFFFFFFFull || div <= 0 || div > 2000000) return 0;   // sanity: stale offset -> 0
    std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varc_id % div) * 8).value_or(0);
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (rpm<std::int32_t>(h, node).value_or(-1) == varc_id)
            return rpm<std::int32_t>(h, node + 0x8).value_or(0);
        node = rpm<std::uint64_t>(h, node + 0x28).value_or(0);
    }
    return 0;
}

// Like read_varc but reports whether the id was actually PRESENT in the hashmap, so a legitimate value of 0
// (e.g. a panel dragged hard to the left/top edge -> position varc = 0) is distinguishable from "absent".
// The panel-origin resolver needs this: keying "is the var set?" off (value != 0) drops a panel sitting at 0.
static bool read_varc_found(HANDLE h, std::uint64_t client, int varc_id, int& out_val) {
    out_val = 0;
    if (varc_id < 0 || client <= 0x10000) return false;
    std::uint64_t store = rpm<std::uint64_t>(h, client + 0x198E0).value_or(0);
    if (store <= 0x10000) return false;
    std::uint64_t hm = store + 0x7630;
    std::uint64_t ba = rpm<std::uint64_t>(h, hm + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hm + 0x10).value_or(0);
    if (ba <= 0x10000 || ba > 0x00007FFFFFFFFFFFull || div <= 0 || div > 2000000) return false;
    std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varc_id % div) * 8).value_or(0);
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (rpm<std::int32_t>(h, node).value_or(-1) == varc_id) { out_val = rpm<std::int32_t>(h, node + 0x8).value_or(0); return true; }
        node = rpm<std::uint64_t>(h, node + 0x28).value_or(0);
    }
    return false;
}

// Read one varc-STRING (VarClientString) by id from the SAME global client-var hashmap the varc-ints
// live in (store+0x7630) -- int and string varcs share one map; the type is a property of the var
// DEFINITION, not of the node (there is NO type tag in the node -- the +0x04 dword holds per-var data,
// often float bits). The value union at node+8 is an EASTL basic_string (identical class to the one the
// companion reads off the VM string stack): the flag byte at strbase+0x17 selects heap vs SSO --
//   flag & 0x80  -> HEAP : ptr = *(u64)(strbase+0),  size = *(u64)(strbase+8);  read `size` bytes @ptr
//   else         -> SSO  : size = 0x17 - flag;       chars inline at strbase (max 22)
// Returns true + fills `out` (raw CP-1252 bytes, capped) when the id is present AND parses as a sane
// string; false for absent / int-typed / garbage. Example: varc 2251 = the interface-1177 hover
// tooltip ("A magical barrier is blocking your path.<br>This door requires level 94 Magic ...").
static bool read_varc_str(HANDLE h, std::uint64_t client, int varc_id, std::string& out) {
    out.clear();
    if (varc_id < 0 || client <= 0x10000) return false;
    std::uint64_t store = rpm<std::uint64_t>(h, client + 0x198E0).value_or(0);
    if (store <= 0x10000) return false;
    std::uint64_t hm = store + 0x7630;
    std::uint64_t ba = rpm<std::uint64_t>(h, hm + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hm + 0x10).value_or(0);
    if (ba <= 0x10000 || ba > 0x00007FFFFFFFFFFFull || div <= 0 || div > 2000000) return false;
    std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varc_id % div) * 8).value_or(0);
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (rpm<std::int32_t>(h, node).value_or(-1) == varc_id)
            return read_eastl_string(h, node + 0x8, out);
        node = rpm<std::uint64_t>(h, node + 0x28).value_or(0);
    }
    return false;
}

// Read a comma-separated list of varp ids and return {"<id>":value,..} as JSON.
// The reusable building block for varp/varbit-driven panels (farming, etc.): the
// caller passes the varp ids it needs and slices varbit bits itself. Empty {} when
// not in-world / chain unreadable. Unset varps come back as 0.
std::string VarpsJson(std::uint32_t pid, const std::string& ids_csv) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";

    std::string out = "{"; bool first = true; char buf[48];
    std::size_t i = 0, n = ids_csv.size();
    while (i < n) {
        while (i < n && (ids_csv[i] < '0' || ids_csv[i] > '9')) ++i;
        int id = 0; bool any = false;
        while (i < n && ids_csv[i] >= '0' && ids_csv[i] <= '9') {
            id = id * 10 + (ids_csv[i] - '0'); any = true; ++i;
            if (id > 1000000) { id = 0; any = false; break; }   // overflow guard
        }
        if (!any) continue;
        int v = read_varp(h, *root, id);
        std::snprintf(buf, sizeof(buf), "%s\"%d\":%d", first ? "" : ",", id, v);
        out += buf; first = false;
    }
    out += "}";
    return out;
}

// Read a comma-separated list of VARBIT ids -> {"<id>":value,..}. Each varbit resolves (cache def, idx2/arch69)
// to a backing varp + bit range [lsb,msb]; the varp is read and the bits sliced. Unset -> 0.
std::string VarbitsJson(std::uint32_t pid, const std::string& ids_csv) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::string out = "{"; bool first = true; char buf[48];
    std::size_t i = 0, n = ids_csv.size();
    while (i < n) {
        while (i < n && (ids_csv[i] < '0' || ids_csv[i] > '9')) ++i;
        int id = 0; bool any = false;
        while (i < n && ids_csv[i] >= '0' && ids_csv[i] <= '9') { id = id * 10 + (ids_csv[i] - '0'); any = true; ++i;
            if (id > 1000000) { id = 0; any = false; break; } }
        if (!any) continue;
        int val = 0, wvp = -1, lsb = -1, msb = -1;
        if (rtx::cache::GetVarbit(id, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
            int raw = read_varp(h, *root, wvp);
            unsigned mask = (msb - lsb + 1 >= 32) ? 0xFFFFFFFFu : ((1u << (msb - lsb + 1)) - 1);
            val = (int)(((unsigned)raw >> lsb) & mask);
        }
        std::snprintf(buf, sizeof(buf), "%s\"%d\":%d", first ? "" : ",", id, val);
        out += buf; first = false;
    }
    out += "}";
    return out;
}

// Dump EVERY currently-set player varp by polling the MainData+0x36040 hashmap directly
// (every bucket -> chain), keyed "4:<id>" (scope 4 = varp) to match the Vars tab. A varp the
// SERVER updates but no CS2 script re-reads (ore/wood/soil/gem box counts) is never re-pushed,
// so the companion push-observer shows it stale while a direct poll stays current.
// External-only; needs no companion. One block read for the bucket array, then 0x30 bytes
// per node (id@+0, value@+8, next@+0x28).
std::string VarpsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t hashRoot = *root + kOffVarpHash;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";   // real bucket counts are a few thousand; cap a stale-offset alloc at ~1 MB, not 16 MB

    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return "{}";

    std::string out = "{"; out.reserve(1u << 16); bool first = true; char buf[64];
    for (int b = 0; b < div; ++b) {
        std::uint64_t node = buckets[(std::size_t)b];
        for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
            std::uint8_t nb[0x30];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
            int id  = *reinterpret_cast<const std::int32_t*>(nb + 0x00);
            int val = *reinterpret_cast<const std::int32_t*>(nb + 0x08);
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + 0x28);
            if (id >= 0 && id < 100000) {
                std::snprintf(buf, sizeof(buf), "%s\"4:%d\":%d", first ? "" : ",", id, val);
                out += buf; first = false;
            }
            node = next;
        }
    }
    out += "}";
    return out;
}

// Dump EVERY currently-set varc-int by polling the GLOBAL client-var hashmap directly (store+0x7630, where
// store = *(MainData+0x198E0)), keyed "5:<id>" (scope 5 = varc-int) to match the Vars tab. External-only --
// the companion's in-process varc capture is disabled, so this direct poll is how varcs reach the panel.
// Same node layout as the varp map (id@+0, value@+8, next@+0x28). Offsets build-specific (re-verify on update).
std::string VarcsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t store = rpm<std::uint64_t>(h, *root + 0x198E0).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + 0x7630;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";   // real bucket counts are a few thousand; cap a stale-offset alloc at ~1 MB, not 16 MB

    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return "{}";

    std::string out = "{"; out.reserve(1u << 16); bool first = true; char buf[64];
    for (int b = 0; b < div; ++b) {
        std::uint64_t node = buckets[(std::size_t)b];
        for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
            std::uint8_t nb[0x30];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
            int id  = *reinterpret_cast<const std::int32_t*>(nb + 0x00);
            int val = *reinterpret_cast<const std::int32_t*>(nb + 0x08);
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + 0x28);
            if (id >= 0 && id < 100000) {
                std::snprintf(buf, sizeof(buf), "%s\"5:%d\":%d", first ? "" : ",", id, val);
                out += buf; first = false;
            }
            node = next;
        }
    }
    out += "}";
    return out;
}

// Read specific varcs as 64-bit values ("varc longs"), external-only. Same global client-var
// hashmap as VarcsDumpAllJson (store+0x7630); the node's value union at +0x08 is read as i64
// instead of i32, so long-typed varcs keep their full width -- e.g. the death interface's
// per-slot item prices (varcs 4829-4875 / 7109-7111, CS2 script11472) and the at-risk
// aggregate varc 4876. Values are emitted as JSON STRINGS: they can exceed the double-exact
// integer range and must survive the JS round-trip.
std::string VarcLongsJson(std::uint32_t pid, const std::string& ids_csv) {
    std::unordered_set<int> want;
    std::size_t i = 0;
    while (i < ids_csv.size()) {
        std::size_t j = ids_csv.find(',', i);
        if (j == std::string::npos) j = ids_csv.size();
        int id = std::atoi(ids_csv.substr(i, j - i).c_str());
        if (id >= 0 && id < 100000) want.insert(id);
        i = j + 1;
    }
    if (want.empty()) return "{}";
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t store = rpm<std::uint64_t>(h, *root + 0x198E0).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + 0x7630;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";

    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return "{}";

    std::string out = "{"; bool first = true; char buf[80];
    for (int b = 0; b < div; ++b) {
        std::uint64_t node = buckets[(std::size_t)b];
        for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
            std::uint8_t nb[0x30];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
            int id = *reinterpret_cast<const std::int32_t*>(nb + 0x00);
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + 0x28);
            if (want.count(id)) {
                auto val = *reinterpret_cast<const std::int64_t*>(nb + 0x08);
                std::snprintf(buf, sizeof(buf), "%s\"%d\":\"%lld\"",
                              first ? "" : ",", id, (long long)val);
                out += buf; first = false;
            }
            node = next;
        }
    }
    out += "}";
    return out;
}

// 64-BIT VARPS: some varps are long-typed (GE offer price vp137, market price vp140,
// recent trading price vp13483, snapshot vp9458). The varp hashmap node has the same
// shape as the varc node -- value union at +0x8 -- and read_varp only takes the low 4
// bytes; this reads the full i64. Values emit as JSON STRINGS (may exceed the
// double-exact range). Unvalidated ids simply read the same low bits widened, so this
// is safe to call on any varp.
std::string VarpsLongJson(std::uint32_t pid, const std::string& ids_csv) {
    std::unordered_set<int> want;
    std::size_t i = 0;
    while (i < ids_csv.size()) {
        std::size_t j = ids_csv.find(',', i);
        if (j == std::string::npos) j = ids_csv.size();
        int id = std::atoi(ids_csv.substr(i, j - i).c_str());
        if (id >= 0 && id < 100000) want.insert(id);
        i = j + 1;
    }
    if (want.empty()) return "{}";
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t hashRoot = *root + kOffVarpHash;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 2000000) return "{}";
    std::string out = "{"; bool first = true; char buf[80];
    for (int id : want) {
        std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)(id % div) * 8).value_or(0);
        for (int s2 = 0; s2 < 128 && node > 0x10000; ++s2) {
            if (rpm<std::int32_t>(h, node).value_or(-1) == id) {
                auto val = rpm<std::int64_t>(h, node + 0x8).value_or(0);
                std::snprintf(buf, sizeof(buf), "%s\"%d\":\"%lld\"",
                              first ? "" : ",", id, (long long)val);
                out += buf; first = false;
                break;
            }
            node = rpm<std::uint64_t>(h, node + 0x28).value_or(0);
        }
    }
    out += "}";
    return out;
}

// Append `s` to `out` as a JSON string body (no surrounding quotes): keep printable ASCII, escape
// " and \, map everything else (CP-1252 punctuation, control bytes, embedded NULs) to a space so the
// result is always valid UTF-8 JSON -- the same rule VarsDumpJson uses for companion string vars, and
// the guard against the JSStringCreateWithUTF8CString whole-JSON-null trap on a stray high byte.
static void append_json_str(std::string& out, const std::string& s) {
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += (char)ch; }
        else if (ch >= 0x20 && ch <= 0x7e) out += (char)ch;
        else out += ' ';
    }
}

// Read specific varc-STRINGS by id, external-only. `ids_csv` = comma-separated ids; returns
// {"<id>":"<string>",..} for the ids that are present and parse as a sane EASTL string (absent /
// int-typed ids are omitted). The targeted read path for a known string var -- e.g. varc 2251 = the
// interface-1177 hover tooltip, 1691 = mouseover text. Works without the companion.
std::string VarcStringsJson(std::uint32_t pid, const std::string& ids_csv) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::string out = "{"; bool first = true; std::string val;
    std::size_t i = 0;
    while (i < ids_csv.size()) {
        std::size_t j = ids_csv.find(',', i);
        if (j == std::string::npos) j = ids_csv.size();
        int id = std::atoi(ids_csv.substr(i, j - i).c_str());
        if (id >= 0 && read_varc_str(h, *root, id, val)) {
            out += first ? "\"" : ",\""; first = false;
            out += std::to_string(id); out += "\":\"";
            append_json_str(out, val);
            out += "\"";
        }
        i = j + 1;
    }
    out += "}";
    return out;
}

// Dump EVERY currently-populated varc-STRING by polling the global client-var hashmap (store+0x7630,
// the same map the varc-ints live in). Keyed "2:<id>" (scope 2 = varc-string) to match the Vars tab and
// the companion's string entries, so the tab can show string vars (tooltip 2251, player names, examines,
// ...) with NO companion loaded. Because the node carries no int/string TYPE tag, this parses every
// node's value union as an EASTL string and keeps only those that validate (heap ptr sane + non-empty
// bounded size, or a valid SSO length) AND look textual -- an int var's value union almost never passes,
// though a rare binary/opaque string var can slip in (acceptable for a diagnostic dump). Same node layout
// and stale-offset guards as VarcsDumpAllJson. Offsets build-specific (re-verify on update).
std::string VarcStringsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t store = rpm<std::uint64_t>(h, *root + 0x198E0).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + 0x7630;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";

    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return "{}";

    std::string out = "{"; out.reserve(1u << 14); bool first = true; std::string val;
    for (int b = 0; b < div; ++b) {
        std::uint64_t node = buckets[(std::size_t)b];
        for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
            int id = rpm<std::int32_t>(h, node).value_or(-1);
            std::uint64_t next = rpm<std::uint64_t>(h, node + 0x28).value_or(0);
            if (id >= 0 && id < 100000 && read_eastl_string(h, node + 0x8, val) && !val.empty()) {
                // require mostly-printable content so int nodes whose union happens to parse are dropped
                std::size_t printable = 0;
                for (unsigned char c : val) if (c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c <= 0x7e)) ++printable;
                if (printable * 100 >= val.size() * 80) {
                    out += first ? "\"2:" : ",\"2:"; first = false;
                    out += std::to_string(id); out += "\":\"";
                    append_json_str(out, val);
                    out += "\"";
                }
            }
            node = next;
        }
    }
    out += "}";
    return out;
}

// Live every-scope var snapshot from the companion's var section (varp/varbit/varc/
// string-int, each resolved in-process via the var-push observer). Keyed by
// "<scope>:<id>" since the scopes are separate id-spaces. {} when the companion isn't
// loaded or the watcher hasn't been enabled. Diff successive dumps for the watcher.
std::string VarsDumpJson(std::uint32_t pid) {
    wchar_t name[64];
    rtx::varc::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return "{}";
    auto* sh = reinterpret_cast<const rtx::varc::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::varc::Share)));
    if (!sh) { CloseHandle(h); return "{}"; }
    std::string out = "{";
    if (sh->magic == rtx::varc::kMagic && sh->version == rtx::varc::kVersion) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::uint32_t s1 = sh->seq;
            if (s1 & 1u) continue;                       // mid-write, retry
            std::uint32_t cnt = sh->count;
            if (cnt > (std::uint32_t)rtx::varc::kMaxVars) cnt = rtx::varc::kMaxVars;
            std::uint32_t scnt = sh->strCount;
            if (scnt > (std::uint32_t)rtx::varc::kMaxStrVars) scnt = rtx::varc::kMaxStrVars;
            std::string body; body.reserve((std::size_t)cnt * 16 + (std::size_t)scnt * 48);
            bool first = true;
            char buf[48];
            for (std::uint32_t i = 0; i < cnt; ++i) {    // int vars (varp 4 / varc-int 5)
                const auto& e = sh->entries[i];
                std::snprintf(buf, sizeof(buf), "%s\"%u:%u\":%d", (first ? "" : ","),
                              (unsigned)e.scope, (unsigned)e.id, (int)e.value);
                body += buf; first = false;
            }
            for (std::uint32_t i = 0; i < scnt; ++i) {   // string vars (varc-string, scope 2)
                const auto& se = sh->strEntries[i];
                std::snprintf(buf, sizeof(buf), "%s\"2:%u\":\"", (first ? "" : ","), (unsigned)se.id);
                body += buf; first = false;
                for (int j = 0; j < rtx::varc::kStrLen && se.text[j]; ++j) {
                    unsigned char ch = (unsigned char)se.text[j];
                    if (ch == '"' || ch == '\\') { body += '\\'; body += (char)ch; }
                    else if (ch >= 0x20 && ch <= 0x7e) body += (char)ch;
                    else body += ' ';                    // drop control/non-ASCII -> keep JSON valid UTF-8
                }
                body += '"';
            }
            std::uint32_t s2 = sh->seq;
            if (s1 == s2 && !(s2 & 1u)) { out += body; break; }   // torn-free
        }
    }
    out += "}";
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return out;
}

// Turn the in-process var observer on/off (it only does work while on). Set true when
// the Vars watcher opens, false when it closes. Returns false if the companion section
// isn't present. Writes the launcher->companion enable flag in the shared section.
bool VarsWatch(std::uint32_t pid, bool on) {
    wchar_t name[64];
    rtx::varc::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::varc::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::varc::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = (sh->magic == rtx::varc::kMagic && sh->version == rtx::varc::kVersion);
    if (ok) sh->enable = on ? 1u : 0u;
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

// ---- server -> client packet protocol -----------------------------------------
// The client keeps a per-opcode descriptor table for inbound (server->client) messages.
// The inbound framer indexes it by the deciphered opcode (valid 0x00..0xE5); each entry
// is a pointer to a 0x50-byte descriptor: {+0x00 int opcode, +0x04 int length, +0x10 ptr
// decode/handle vtable (handler fn at vtable+0x10)}. length >=0 = fixed, -1 = 1-byte size
// prefix (var-byte), -2 = 2-byte size prefix (var-short). The table base is held at a
// module global (rs2client+0xC6A158 on the current build) and populated during init.
//
// Reading it here (cross-process, no hook) enumerates the WHOLE protocol the client
// understands, live from the running build. Resolution is RVA-first, then a structural
// fallback that proves a candidate by the descriptor invariant (desc[op].opcode == op),
// so a game update that moves the table is self-correcting rather than silently wrong.
namespace {
constexpr std::uint64_t kOpTableRva   = 0xC6A158;   // module global holding the table base
constexpr int           kOpMax        = 0xE5;       // highest valid opcode (framer bails above)
constexpr int           kOpCount      = kOpMax + 1; // 230
constexpr std::uint64_t kDescOpcodeOff = 0x00;
constexpr std::uint64_t kDescLenOff    = 0x04;
constexpr std::uint64_t kDescVtblOff   = 0x10;
constexpr std::uint64_t kVtblHandlerOff = 0x10;

// True if `tbl` walks like the opcode table: every entry a pointer whose descriptor's
// stored opcode equals its index. Sampled at a spread of opcodes to stay cheap.
bool optable_valid(HANDLE h, std::uint64_t tbl) {
    if (!tbl) return false;
    static const int probe[] = {0, 1, 2, 0x2A, 0x5C, 0x83, 0xC0, kOpMax};
    for (int op : probe) {
        auto desc = rpm<std::uint64_t>(h, tbl + (std::uint64_t)op * 8);
        if (!desc || !*desc) return false;
        auto stored = rpm<std::int32_t>(h, *desc + kDescOpcodeOff);
        if (!stored || *stored != op) return false;
    }
    return true;
}

// Resolve the opcode-table base: the RVA global first, then a structural sweep of the
// image for any in-module qword that satisfies optable_valid.
std::uint64_t resolve_optable(HANDLE h, std::uint64_t base, std::uint64_t size) {
    if (auto t = rpm<std::uint64_t>(h, base + kOpTableRva); t && optable_valid(h, *t))
        return *t;
    // Fallback: scan the image in chunks for a stored in-module pointer that validates.
    constexpr std::size_t chunk = 4 * 1024 * 1024;
    std::vector<std::uint8_t> buf(chunk);
    for (std::uint64_t off = 0; off < size; off += chunk) {
        std::size_t want = (std::size_t)((std::min)((std::uint64_t)chunk, size - off));
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (LPCVOID)(base + off), buf.data(), want, &got) || got < 8)
            continue;
        for (std::size_t i = 0; i + 8 <= got; i += 8) {
            std::uint64_t v; std::memcpy(&v, buf.data() + i, 8);
            if (v < base || v >= base + size) continue;      // only in-module candidates
            if (optable_valid(h, v)) return v;
        }
    }
    return 0;
}
}  // namespace

// Full server->client opcode enumeration for `pid`, read live from the running client.
// {} when the process isn't attached; {"ok":false,...} when the table can't be resolved
// (e.g. a game update moved it before init finished). Never touches the game via a hook.
std::string ServerPacketsJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"ok\":false,\"reason\":\"not attached\"}";
    HANDLE h = ps.h;
    // snap_proc only exposes main_global_va + mod_base; the image size lives on State.
    std::uint64_t base = ps.mod_base, size = 0;
    { std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_states.find((DWORD)pid);
      if (it != g_states.end()) size = it->second.mod_size; }
    if (!base || !size) return "{\"ok\":false,\"reason\":\"module not mapped\"}";

    std::uint64_t tbl = resolve_optable(h, base, size);
    if (!tbl) return "{\"ok\":false,\"reason\":\"opcode table not found (build may have moved it)\"}";

    std::string out = "{\"ok\":true,\"tableRva\":\"0x";
    { char b[24]; std::snprintf(b, sizeof(b), "%llx", (unsigned long long)(tbl - base)); out += b; }
    out += "\",\"min\":0,\"max\":"; out += std::to_string(kOpMax);
    out += ",\"packets\":[";
    bool first = true;
    for (int op = 0; op < kOpCount; ++op) {
        auto desc = rpm<std::uint64_t>(h, tbl + (std::uint64_t)op * 8);
        if (!desc || !*desc) continue;
        auto len = rpm<std::int32_t>(h, *desc + kDescLenOff);
        if (!len) continue;
        const char* kind = (*len == -1) ? "var_byte" : (*len == -2) ? "var_short"
                          : (*len < 0)  ? "var"      : "fixed";
        // handler fn = *(*(desc+0x10) + 0x10), reported as an RVA -- a stable per-packet id.
        std::uint64_t hrva = 0;
        if (auto vt = rpm<std::uint64_t>(h, *desc + kDescVtblOff); vt && *vt)
            if (auto fn = rpm<std::uint64_t>(h, *vt + kVtblHandlerOff);
                fn && *fn >= base && *fn < base + size)
                hrva = *fn - base;
        char b[128];
        std::snprintf(b, sizeof(b),
            "%s{\"op\":%d,\"len\":%d,\"kind\":\"%s\",\"handler\":\"0x%llx\"}",
            first ? "" : ",", op, (int)*len, kind, (unsigned long long)hrva);
        out += b; first = false;
    }
    out += "]}";
    return out;
}

// Live decoded-packet feed from the companion's netprobe ring. Returns records with
// seq > `since` (0 = everything currently in the ring), newest-inclusive, plus meta so
// the panel can show capture health. {"ok":false,...} when the companion isn't loaded or
// the panel hasn't been built into this client yet. Best-effort: a very fast burst can
// lap the ring between polls (meta.seen vs the returned span shows any gap).
std::string ServerPacketFeedJson(std::uint32_t pid, std::uint64_t since) {
    wchar_t name[64];
    rtx::netprobe::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return "{\"ok\":false,\"reason\":\"companion not loaded / panel not built in\"}";
    auto* sh = reinterpret_cast<const rtx::netprobe::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::netprobe::Share)));
    if (!sh) { CloseHandle(h); return "{\"ok\":false,\"reason\":\"map failed\"}"; }
    std::string out;
    if (sh->magic != rtx::netprobe::kMagic || sh->version != rtx::netprobe::kVersion) {
        out = "{\"ok\":false,\"reason\":\"share magic/version mismatch\"}";
    } else {
        const std::uint64_t written = sh->written;
        const std::uint64_t oldest  = written > rtx::netprobe::kMaxRecords
                                     ? written - rtx::netprobe::kMaxRecords : 0;
        std::uint64_t from = since > oldest ? since : oldest;   // clamp to what still lives
        // Cap the per-call span so a long-idle panel re-open doesn't serialise the whole ring.
        constexpr std::uint64_t kMaxPerCall = 512;
        if (written - from > kMaxPerCall) from = written - kMaxPerCall;
        char meta[256];
        std::snprintf(meta, sizeof(meta),
            "{\"ok\":true,\"written\":%llu,\"seen\":%llu,\"from\":%llu,"
            "\"enable\":%u,\"flags\":%u,\"framerRva\":\"0x%x\","
            "\"diag\":[%u,%u,%u,%u],\"packets\":[",
            (unsigned long long)written, (unsigned long long)sh->seen,
            (unsigned long long)from, (unsigned)sh->enable, (unsigned)sh->flags,
            (unsigned)sh->framerRva, sh->diag[0], sh->diag[1], sh->diag[2], sh->diag[3]);
        out = meta;
        bool first = true;
        static const char* hexd = "0123456789abcdef";
        for (std::uint64_t s = from; s < written; ++s) {
            const rtx::netprobe::Record& r = sh->recs[s % rtx::netprobe::kMaxRecords];
            if (r.seq != s + 1) continue;                       // slot was lapped mid-read: skip
            std::uint32_t kept = r.kept; if (kept > rtx::netprobe::kSnip) kept = rtx::netprobe::kSnip;
            std::string hex; hex.reserve((std::size_t)kept * 2);
            for (std::uint32_t i = 0; i < kept; ++i) {
                hex += hexd[r.data[i] >> 4]; hex += hexd[r.data[i] & 0xF];
            }
            char b[96];
            std::snprintf(b, sizeof(b),
                "%s{\"seq\":%llu,\"t\":%llu,\"op\":%d,\"len\":%d,\"kept\":%u,\"hex\":\"",
                first ? "" : ",", (unsigned long long)r.seq, (unsigned long long)r.tick,
                (int)r.opcode, (int)r.length, kept);
            out += b; out += hex; out += "\"}";
            first = false;
        }
        out += "]}";
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return out;
}

// Arm / disarm the companion's framer capture. The companion records only while `enable`
// is a recent GetTickCount64 stamp, so the panel re-arms each poll and a closed/crashed
// panel auto-disarms within ~3s. Returns false if the companion section isn't present.
bool ServerPacketFeedEnable(std::uint32_t pid, bool on) {
    wchar_t name[64];
    rtx::netprobe::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::netprobe::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::netprobe::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = (sh->magic == rtx::netprobe::kMagic && sh->version == rtx::netprobe::kVersion);
    if (ok) sh->enable = on ? (std::uint32_t)GetTickCount64() : 0u;
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

// Render toggles (launcher->companion). which: 0 = hide NPCs, 1 = hide other players,
// 2 = hide the whole scene, 3 = keep-focused, 4 = true-embed (client is a
// child of the host with detached input queues). Writes the flag in the render shared
// section; returns false if the companion isn't loaded / the section isn't present.
bool RenderToggle(std::uint32_t pid, int which, bool on) {
    wchar_t name[64];
    rtx::render::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::render::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::render::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = (sh->magic == rtx::render::kMagic && sh->version == rtx::render::kVersion);
    if (ok) {
        std::uint32_t v = on ? 1u : 0u;
        if (which == 0) sh->hideNpcs = v;
        else if (which == 1) sh->hidePlayers = v;
        else if (which == 2) sh->hideAll = v;
        else if (which == 3) sh->keepFocused = v;
        else if (which == 4) sh->embedded = v;
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

// The keyboard-input window the companion published (see Reader.h). 0 if the companion isn't
// loaded, the section isn't present, or no frame has been presented yet.
std::uint64_t RenderInputWindow(std::uint32_t pid) {
    wchar_t name[64];
    rtx::render::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return 0;
    auto* sh = reinterpret_cast<rtx::render::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::render::Share)));
    if (!sh) { CloseHandle(h); return 0; }
    std::uint64_t w = (sh->magic == rtx::render::kMagic && sh->version == rtx::render::kVersion) ? sh->inputWindow : 0;
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return w;
}

// Live tick state for `pid`: the server-tick count and ms since the last tick
// (precise, from the fast-poll QPC timestamp; age = -1 if unknown). Backs the
// overlay metronome: count drives the beat (every N ticks), age the smooth phase.
// Returns false if the client isn't tracked. Queried each render frame.
bool TickState(std::uint32_t pid, std::uint32_t& count, double& age_ms) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_states.find((DWORD)pid);
    if (it == g_states.end()) return false;
    count  = it->second.current_tick;
    double t = it->second.last_tick_qpc_ms;
    age_ms = (t > 0.0) ? (qpc_now_ms() - t) : -1.0;
    return true;
}

bool SkillsXp(std::uint32_t pid, int out[29]) {
    auto ps = snap_proc(pid);
    if (!ps) return false;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return false;
    auto stats = rpm<std::uint64_t>(h, *root + kOffStats);
    if (!stats || *stats <= 0x10000) return false;
    auto block = rpm<std::uint64_t>(h, *stats + kStatsInner);
    if (!block || *block <= 0x10000) return false;
    auto count = rpm<std::uint8_t>(h, *block + 0x8);
    auto arr   = rpm<std::uint64_t>(h, *block + 0x10);
    if (!count || !arr || *arr <= 0x10000) return false;
    int n = (*count > 29) ? 29 : (int)*count;
    if (n <= 0) return false;
    std::vector<std::uint8_t> sb((std::size_t)n * 0x18);
    if (!rpm_bytes(h, *arr, sb.data(), sb.size())) return false;
    for (int i = 0; i < 29; ++i) out[i] = -1;
    for (int i = 0; i < n; ++i) {
        std::int32_t xp = *(const std::int32_t*)(sb.data() + (std::size_t)i * 0x18 + 0x0C);
        if (xp >= 0 && xp <= 200000000) out[i] = xp;   // the game's per-skill cap
    }
    return true;
}

// ---- Scene entities (players + NPCs) ----
//
// Layout for the current build. All scene entities -- players, NPCs,
// scenery, ground items -- live in one vector owned by the worldView's "scene worker":
//
//   container = *(root + 0x19990)
//   idx       = *(int)(container + 0x70)        active worldView index
//   worldView = *( *(container + 0x58) + idx*0x10 + 8 )
//   worker    = *(worldView + 0x10170)
//   vector:   begin = *(worker + 0x138), end = *(worker + 0x140)   (Entity* each)
//
//   per entity:  sec  = *(entity + 0x1A0)        object-data
//                type = *(u8)(sec + 0x10)        1 = NPC, 2 = player
//   NPC/player:  name(asciiz)@sec+0xB8, uid(world index)@sec+0x88,
//                NPC configId@sec+0x1080, posX(float)@sec+0x270,
//                posY(float)@sec+0x278  (tile = pos / 512)
//
// NPC right-click actions come from the cache NPC def (index 18), keyed by the
// config id. All offsets are version-specific and break on a game update.
//
// Objects are NOT read from the live vector (the scene-render object nodes
// carry no externally-walkable loc config id; the live loc render buffer is
// volatile + false-positive-prone). Instead the reader decodes
// the MAPSV2 cache (index 5) for the regions overlapping `obj_range` tiles
// around the local player and resolve names/actions from loc configs (index
// 16) -- see the objects pass below.
namespace {

// One runtime scene object surfaced by the in-client companion module.
struct RuntimeObj {
    int config_id, x, y, plane, kind;
    float bmin[3], bmax[3];   // live model world AABB (east,north,up); bmax.x==bmin.x = none
};

// Read the per-pid companion section (if present) and take a torn-free snapshot of
// the runtime scene objects -- the dynamically placed locs (e.g. Archaeology
// dig-site hotspots) that aren't in the static map. Returns false when no section
// exists for the pid (the module isn't loaded). Strictly read-only.
bool ReadRuntimeObjects(std::uint32_t pid, std::vector<RuntimeObj>& out) {
    out.clear();
    wchar_t name[64];
    rtx::scene::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<const rtx::scene::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::scene::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = false;
    if (sh->magic == rtx::scene::kMagic && sh->version == rtx::scene::kVersion) {
        for (int attempt = 0; attempt < 8 && !ok; ++attempt) {
            std::uint32_t s1 = sh->seq;
            if (s1 & 1u) continue;                       // mid-write, retry
            std::uint32_t cnt = sh->count;
            if (cnt > (std::uint32_t)rtx::scene::kMaxObjects) cnt = rtx::scene::kMaxObjects;
            out.clear(); out.reserve(cnt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto& o = sh->objects[i];
                RuntimeObj r{ o.config_id, o.x, o.y, o.plane, o.kind,
                              { o.bmin[0], o.bmin[1], o.bmin[2] },
                              { o.bmax[0], o.bmax[1], o.bmax[2] } };
                out.push_back(r);
            }
            std::uint32_t s2 = sh->seq;
            ok = (s1 == s2 && !(s2 & 1u));               // unchanged across the copy
            if (!ok) out.clear();
        }
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

// Dropped ground items (scene entity type 3), published by the companion into its own section.
// id = item config id (resolve name from the item cache); x/y/plane = the tile the stack sits on.
struct GroundItem { int id, x, y, plane; };
bool ReadGroundItems(std::uint32_t pid, std::vector<GroundItem>& out) {
    out.clear();
    wchar_t name[64];
    rtx::ground::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<const rtx::ground::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::ground::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = false;
    if (sh->magic == rtx::ground::kMagic && sh->version == rtx::ground::kVersion) {
        for (int attempt = 0; attempt < 8 && !ok; ++attempt) {
            std::uint32_t s1 = sh->seq;
            if (s1 & 1u) continue;                       // mid-write, retry
            std::uint32_t cnt = sh->count;
            if (cnt > (std::uint32_t)rtx::ground::kMaxItems) cnt = rtx::ground::kMaxItems;
            out.clear(); out.reserve(cnt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto& it = sh->items[i];
                out.push_back({ it.id, it.x, it.y, it.plane });
            }
            std::uint32_t s2 = sh->seq;
            ok = (s1 == s2 && !(s2 & 1u));               // unchanged across the copy
            if (!ok) out.clear();
        }
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

// Read the per-pid companion "special" section: transient render-pass highlights (the clue-scan
// ring) the launcher can't see on its own. RW-maps so it can set enable=1, which arms the
// companion's render-thread observer ONLY while the launcher polls (cost ~zero otherwise).
// Strictly a launcher<->companion handshake; no game state is touched. False if no section.
struct RuntimeHi { int gfx, x, y, uid, plane; std::uint32_t stamp; };
bool ReadRuntimeHighlights(std::uint32_t pid, std::vector<RuntimeHi>& out, std::uint32_t* diag = nullptr) {
    out.clear();
    wchar_t name[64];
    rtx::special::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::special::Share*>(
        MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(rtx::special::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = false;
    if (sh->magic == rtx::special::kMagic && sh->version == rtx::special::kVersion) {
        sh->enable = (std::uint32_t)GetTickCount64();    // refresh the arm stamp (companion decays it)
        if (diag) for (int k = 0; k < 12; ++k) diag[k] = sh->diag[k];   // +gfxhits/t4hits/MB/sub
        for (int attempt = 0; attempt < 8 && !ok; ++attempt) {
            std::uint32_t s1 = sh->seq;
            if (s1 & 1u) continue;                       // mid-write, retry
            std::uint32_t cnt = sh->count;
            if (cnt > (std::uint32_t)rtx::special::kMaxHighlights) cnt = rtx::special::kMaxHighlights;
            out.clear(); out.reserve(cnt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto& it = sh->items[i];
                out.push_back(RuntimeHi{ it.gfx, it.x, it.y, it.uid, it.plane, it.stamp });
            }
            std::uint32_t s2 = sh->seq;
            ok = (s1 == s2 && !(s2 & 1u));               // unchanged across the copy
            if (!ok) out.clear();
        }
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

// Resolve a runtime loc's name/actions, honouring a varbit/varp morph. A placed
// archaeology hotspot carries a morph BASE id (e.g. 117366) whose on-screen variant
// (117367 "Crucible stands debris" vs 117363 "Earthen clay") is chosen live by a
// varbit. That varbit is read in-process and resolved to the exact child the game is
// drawing; with no morph (or an unreadable selector) it falls back to GetLoc(base).
rtx::cache::LocMeta resolve_loc(HANDLE h, std::uint64_t root, int base_id) {
    int vb = -1, vp = -1, defc = -1;
    std::vector<int> variants;
    if (h && root && rtx::cache::GetLocMorph(base_id, vb, vp, defc, variants) && !variants.empty()) {
        int value = -1;
        if (vb >= 0) {                                            // varbit takes priority (engine order)
            int wvp = -1, lsb = -1, msb = -1;                     // varbit -> backing varp + bit range
            if (rtx::cache::GetVarbit(vb, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
                int raw = read_varp(h, root, wvp);
                int width = msb - lsb + 1;
                unsigned mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
                value = (int)(((unsigned)raw >> lsb) & mask);
            }
        } else if (vp >= 0) {
            value = read_varp(h, root, vp);                       // selector is a varp directly
        }
        if (value >= 0) {
            // Selector read OK -> resolve to the exact live variant (engine semantics:
            // in-range index picks the child; out-of-range uses the morph default).
            int child = (value < (int)variants.size()) ? variants[value] : defc;
            if (child < 0) return {};            // variant is "none" -> hidden, no marker
            return rtx::cache::GetLoc(child);    // live variant (empty name -> caller filters it)
        }
        // Selector unreadable (not in-world / chain miss): fall through to a static guess.
    }
    return rtx::cache::GetLoc(base_id);
}

// Resolve an NPC's live name/actions honouring a varbit/varp morph: read the selector in-process and
// return the EXACT variant the game is drawing right now. A morph entity that is currently a DIFFERENT
// NPC (or hidden) returns that child's meta (or empty) -- so a same-named but inactive morph no longer
// matches a highlight, even when the real NPC is out of walk range. No morph / unreadable
// selector -> GetNpc(base). out_hidden (optional) is set true ONLY when this entity IS a morph
// whose live variant is hidden (child == -1) or whose selector is out of range -- i.e. the game
// is NOT drawing it here. Callers use that to SKIP the entity instead of falling back to its
// stale in-memory name, which would resurrect a relocated NPC at its old tile.
rtx::cache::NpcMeta resolve_npc(HANDLE h, std::uint64_t root, int base_id, bool* out_hidden = nullptr) {
    if (out_hidden) *out_hidden = false;
    int vb = -1, vp = -1, defc = -1;
    std::vector<int> variants;
    if (h && root && rtx::cache::GetNpcMorph(base_id, vb, vp, defc, variants) && !variants.empty()) {
        int value = -1;
        if (vb >= 0) {                                            // varbit takes priority (engine order)
            int wvp = -1, lsb = -1, msb = -1;
            if (rtx::cache::GetVarbit(vb, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
                int raw = read_varp(h, root, wvp);
                int width = msb - lsb + 1;
                unsigned mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
                value = (int)(((unsigned)raw >> lsb) & mask);
            }
        } else if (vp >= 0) {
            value = read_varp(h, root, vp);                       // selector is a varp directly
        }
        // Resolve to the EXACT in-range variant, or to NOTHING -- never the default child or the base.
        // Out-of-range or unreadable means the morph's live identity is unknown here; guessing paints a
        // phantom (a leftover actor mis-named). {} makes callers fall back to the quest tile.
        (void)defc;
        if (value < 0 || value >= (int)variants.size()) { if (out_hidden) *out_hidden = true; return {}; }
        int child = variants[value];
        if (child < 0) { if (out_hidden) *out_hidden = true; return {}; }   // variant "none" -> hidden, no NPC here
        return rtx::cache::GetNpc(child);        // the live variant the game is actually showing
    }
    return rtx::cache::GetNpc(base_id);          // not a morph -> itself
}

}  // namespace

// JSON [{id,x,y,plane},..] of the dropped ground items in the client's scene. Empty array when
// the companion isn't loaded / no items. Names are left to the caller (item cache / JS side).
std::string GroundItemsJson(std::uint32_t pid) {
    std::vector<GroundItem> items;
    ReadGroundItems(pid, items);
    std::string out = "[";
    char buf[96];
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%s{\"id\":%d,\"x\":%d,\"y\":%d,\"plane\":%d}",
                      i ? "," : "", items[i].id, items[i].x, items[i].y, items[i].plane);
        out += buf;
    }
    out += "]";
    return out;
}

namespace {
// ---- update-resilient worldView fields ---------------------------------------
// The scene-worker pointer and the view-projection matrix are plain offsets into
// the worldView object and MOVE whenever a game update grows the class. Both are
// resolved at RUNTIME: the cached offset is validated on every use (a handful of
// reads) and the worldView block is rescanned when it stops validating, so a game
// update costs one throttled rescan instead of a broken feature.
std::atomic<std::uint32_t>       g_workerOff{ rtx::scn::kWorkerOffDefault };
std::atomic<std::uint32_t>       g_matrixOff{ rtx::scn::kMatrixOffDefault };
std::atomic<std::int32_t>        g_camPosRel{ rtx::scn::kCamPosRelDefault };   // stored camera position, relative to the matrix
std::atomic<unsigned long long>  g_workerScanAt{ 0 };   // rescan throttles (GetTickCount64)
std::atomic<unsigned long long>  g_matrixScanAt{ 0 };
constexpr std::uint32_t          kWvSpan = 0x14000;     // worldView bytes covered by a rescan

// Live entity FINE positions (east, north, up) used to functionally validate a
// candidate view matrix. SEVERAL are collected because any single entity can
// legitimately be off-screen (behind the camera, outside a zoomed-in frustum).
struct CamProbes { int n = 0; float p[8][3]; };

// A real scene worker walks: +0x138/+0x140 bound a sane entity vector and the
// sampled entries carry secs (+0x1A0) with known type bytes. `strict` (rescan
// candidates) demands >=4 typed entities in a >=4-slot vector; the non-strict
// form (the already-proven cached offset) accepts a sparse or mid-reload scene
// -- one typed entity, or even a momentarily empty vector -- so a region load
// or a lonely area doesn't read as "offset broke" and drop the frame.
bool validate_scene_worker(HANDLE h, std::uint64_t worker, CamProbes* probes, bool strict) {
    if (worker <= 0x10000 || worker > 0x7FFFFFFFFFFFull) return false;
    auto b = rpm<std::uint64_t>(h, worker + 0x138);
    auto e = rpm<std::uint64_t>(h, worker + 0x140);
    if (!b || !e || *b <= 0x10000 || *e < *b || (*e - *b) % 8) return false;
    std::uint64_t n = (*e - *b) / 8;
    if (n > 100000) return false;
    const int need = strict ? 4 : 1;
    if (n < (std::uint64_t)need) return !strict && n == 0;
    int typed = 0;
    for (std::uint64_t i = 0; i < n && i < 48; ++i) {
        auto ep = rpm<std::uint64_t>(h, *b + i * 8);
        if (!ep || *ep <= 0x10000) continue;
        auto sec = rpm<std::uint64_t>(h, *ep + 0x1A0);
        if (!sec || *sec <= 0x10000) continue;
        int t = rpm<std::uint8_t>(h, *sec + 0x10).value_or(0xFF);
        if (!(t <= 5 || (t >= 10 && t <= 13))) continue;
        ++typed;
        if (probes && probes->n < 8 && (t == 1 || t == 2)) {
            float x = rpm<float>(h, *sec + 0x270).value_or(0);
            float z = rpm<float>(h, *sec + 0x274).value_or(0);
            float y = rpm<float>(h, *sec + 0x278).value_or(0);
            if (x > 0 && y > 0) {
                probes->p[probes->n][0] = x;
                probes->p[probes->n][1] = y;
                probes->p[probes->n][2] = z;
                ++probes->n;
            }
        }
        if (typed >= need && (!probes || probes->n >= 8)) return true;
    }
    return typed >= need;
}

// Scene worker for a worldView: cached offset first (lenient), else rescan
// (throttled, strict -- a random heap block must not be adopted off a sparse
// coincidence).
std::optional<std::uint64_t> scene_worker(HANDLE h, std::uint32_t pid,
                                          std::uint64_t wv, CamProbes* probes) {
    std::uint32_t off = g_workerOff.load(std::memory_order_relaxed);
    auto w = rpm<std::uint64_t>(h, wv + off);
    if (w && validate_scene_worker(h, *w, probes, false)) return *w;
    unsigned long long now = GetTickCount64(), last = g_workerScanAt.load();
    if (now - last < 3000 || !g_workerScanAt.compare_exchange_strong(last, now))
        return std::nullopt;
    for (std::uint32_t o = 0; o + 8 <= kWvSpan; o += 8) {
        auto cand = rpm<std::uint64_t>(h, wv + o);
        if (!cand || *cand <= 0x10000) continue;
        if (probes) probes->n = 0;
        if (validate_scene_worker(h, *cand, probes, true)) {
            g_workerOff.store(o, std::memory_order_relaxed);
            char msg[80];
            std::snprintf(msg, sizeof(msg), "scene worker offset moved: worldView+0x%X", o);
            rtx::log::Client(pid, msg);
            return *cand;
        }
    }
    return std::nullopt;
}

bool finite_nonzero_16(const float* m) {
    bool nz = false;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) return false;
        if (m[i] != 0.f) nz = true;
    }
    return nz;
}

// Matrix selection. The worldView holds SEVERAL matrix blocks that all project the
// scene plausibly: the render camera (0x13090 plus byte-identical copies) and impostor
// cameras -- notably a player-tracking, north-locked, straight-down minimap block
// (0x13970) that passes every entity-based heuristic. Heuristics cannot separate them;
// the discriminator is the engine's own camera POSITION: the camera centre solved from
// a view-projection matrix (the point where the x', y' and w rows all vanish) must
// EQUAL the position the engine stores on the camera block at matrix+0x80 as
// (east, up, north) floats -- agreement is ~0.2 fine units for the real camera and
// ~1.7M for the impostor. An exact self-consistency test that holds at every camera
// pose, so validation never fails just because entities are off-screen.

// Camera centre from a view-projection matrix: solve the 3x3 system
// x'(C)=y'(C)=w(C)=0 (world order east, north, up -- the WorldToScreen input).
bool solve_cam_centre(const float* m, double out[3]) {
    double A[3][4] = {
        { m[0], m[8],  m[4], -(double)m[12] },
        { m[1], m[9],  m[5], -(double)m[13] },
        { m[3], m[11], m[7], -(double)m[15] },
    };
    for (int i = 0; i < 3; ++i) {
        int p = i;
        for (int r = i + 1; r < 3; ++r)
            if (std::fabs(A[r][i]) > std::fabs(A[p][i])) p = r;
        if (std::fabs(A[p][i]) < 1e-12) return false;
        if (p != i)
            for (int c = 0; c < 4; ++c) std::swap(A[i][c], A[p][c]);
        for (int r = i + 1; r < 3; ++r) {
            double f = A[r][i] / A[i][i];
            for (int c = i; c < 4; ++c) A[r][c] -= f * A[i][c];
        }
    }
    for (int i = 2; i >= 0; --i) {
        double s = A[i][3];
        for (int c = i + 1; c < 3; ++c) s -= A[i][c] * out[c];
        out[i] = s / A[i][i];
    }
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

// Full matrix validation, all intrinsic -- selects exactly the render camera among the
// centre-consistent lookalikes in the camera region:
// 1. true-perspective STRUCTURE -- the z'(depth) row gradient is parallel to
//    the w row gradient (depth maps as a*z+b over z; view/rotation blocks have
//    orthogonal axes there) and the optical axis is centred (a point straight
//    ahead projects to ndc 0,0 -- shifted/mixed float windows fail);
// 2. the solved centre is a REAL world position (positive east/north; zeroed
//    or identity camera blocks solve to the origin and self-match trivially);
// 3. the stored (east, up, north) camera position at wv+off+rel equals the
//    solved centre. Tolerance is generous vs the measured 0.21-unit agreement
//    to ride out a torn read, yet 5 orders under any wrong-block mismatch.
bool validate_view_matrix(HANDLE h, std::uint64_t wv, std::uint32_t off,
                          std::int32_t rel, const float* m) {
    if (!finite_nonzero_16(m)) return false;
    const double fe = m[3], fn = m[11], fu = m[7];        // w-row gradient (forward)
    const double wd = fe * fe + fn * fn + fu * fu;
    if (!(wd > 0.0)) return false;
    const double ze = m[2], zn = m[10], zu = m[6];        // z'-row gradient
    const double zd = ze * ze + zn * zn + zu * zu;
    if (!(zd > 0.0)) return false;
    const double cx = zn * fu - zu * fn, cy = zu * fe - ze * fu, cz = ze * fn - zn * fe;
    if (cx * cx + cy * cy + cz * cz > 1e-4 * zd * wd) return false;
    if (std::fabs(m[0] * fe + m[8] * fn + m[4] * fu) > 0.01 * wd) return false;
    if (std::fabs(m[1] * fe + m[9] * fn + m[5] * fu) > 0.01 * wd) return false;
    double C[3];
    if (!solve_cam_centre(m, C)) return false;
    if (C[0] < 512.0 || C[1] < 512.0) return false;       // >= 1 tile east/north
    float p[3];
    if (!rpm_bytes(h, wv + off + rel, p, sizeof(p))) return false;
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return false;
    constexpr double kTol = 64.0;   // fine units (1/8 tile)
    return std::fabs(C[0] - p[0]) < kTol &&   // east
           std::fabs(C[2] - p[1]) < kTol &&   // up
           std::fabs(C[1] - p[2]) < kTol;     // north
}

// Secondary sanity for rescan fallback: the matrix projects SOME live entity
// on-screen with real perspective (w varies with position -- rejects the
// orthographic helper cameras) and 6-tile offsets separate on screen.
bool probes_project(const float* m, const CamProbes& probes) {
    auto ndc = [&](float x, float y, float z, float& nx, float& ny) {
        float w = m[3] * x + m[11] * y + m[7] * z + m[15];
        if (w <= 1.0f) return false;
        nx = (m[0] * x + m[8] * y + m[4] * z + m[12]) / w;
        ny = (m[1] * x + m[9] * y + m[5] * z + m[13]) / w;
        return true;
    };
    constexpr float kOff = 6 * 512.f;
    for (int i = 0; i < probes.n; ++i) {
        const float* p = probes.p[i];
        float ax, ay;
        if (!ndc(p[0], p[1], p[2], ax, ay)) continue;
        if (ax < -1.3f || ax > 1.3f || ay < -1.3f || ay > 1.3f) continue;
        float wp = m[3] * p[0] + m[11] * p[1] + m[7] * p[2] + m[15];
        float dw = std::fabs(m[3]) + std::fabs(m[11]) + std::fabs(m[7]);
        if (dw * kOff < 0.005f * std::fabs(wp)) continue;    // orthographic: w constant
        auto separates = [&](float dx, float dy) {
            float bx, by;
            if (ndc(p[0] + dx, p[1] + dy, p[2], bx, by) &&
                (std::fabs(bx - ax) + std::fabs(by - ay)) > 0.005f) return true;
            if (ndc(p[0] - dx, p[1] - dy, p[2], bx, by) &&
                (std::fabs(bx - ax) + std::fabs(by - ay)) > 0.005f) return true;
            return false;
        };
        if (separates(kOff, 0.f) && separates(0.f, kOff)) return true;
    }
    return false;
}

// Per-client camera-read state. `frozen` guards the one hole centre-match
// leaves: a block that STOPPED updating still matches its own (equally stale)
// stored centre. Camera varcs 5115 (yaw) / 5114 (pitch) / 1971 (zoom) are the
// engine's CAM2 orbit state (cs2: CAM2_SETPOSITIONENTITY_PLAYER(0,0,zoom,
// 5114,5115,...)), so "varcs changed but matrix bytes are bit-identical for
// ~8 reads" means the block is no longer the live camera -> rescan, skipping
// the stale offset when an alternative exists.
struct CamTrust {
    bool  validated = false;   // centre-match passed at least once at g_matrixOff
    bool  haveLastM = false, haveVarc = false;
    float lastM[16] = {};
    int   varc[3] = {};        // yaw / pitch / zoom at the previous read
    int   frozen  = 0;         // consecutive camera-moved-but-matrix-frozen reads
    int   unmatched = 0;       // consecutive centre-match failures (torn reads pass through)
    std::uint32_t staleOff = 0xFFFFFFFF;   // offset dropped by the frozen detector
};
std::mutex g_camTrustMu;
std::unordered_map<std::uint32_t, CamTrust> g_camTrust;

// View-projection matrix for a worldView. The cached offset is accepted on
// centre-match (exact, pose-independent); a short trust window rides out torn
// reads. Rescan (throttled) when the offset never matched for this client or
// stopped matching: pass 1 adopts the block whose solved centre equals its own
// stored position at the canonical +0x80; pass 2 (layout drifted) finds a
// probe-validated matrix and searches +-0x400 around it for its stored centre,
// re-learning the relative offset. `root` (MainData, 0 = unknown) feeds the
// varc camera signature for the frozen detector.
bool read_view_matrix(HANDLE h, std::uint32_t pid, std::uint64_t root,
                      std::uint64_t wv, const CamProbes& probes, float* out) {
    std::uint32_t off = g_matrixOff.load(std::memory_order_relaxed);
    std::int32_t  rel = g_camPosRel.load(std::memory_order_relaxed);
    bool finiteNz = rpm_bytes(h, wv + off, out, 16 * sizeof(float)) &&
                    finite_nonzero_16(out);
    bool matched = finiteNz && validate_view_matrix(h, wv, off, rel, out);

    bool usable;
    {
        std::lock_guard<std::mutex> lk(g_camTrustMu);
        if (g_camTrust.size() > 64) g_camTrust.clear();   // bound: dead pids accumulate
        CamTrust& tr = g_camTrust[pid];
        bool camMoved = false;
        if (root > 0x10000) {
            int v[3]; bool f0, f1, f2;
            f0 = read_varc_found(h, root, 5115, v[0]);
            f1 = read_varc_found(h, root, 5114, v[1]);
            f2 = read_varc_found(h, root, 1971, v[2]);
            if (f0 && f1 && f2) {
                camMoved = tr.haveVarc && (v[0] != tr.varc[0] || v[1] != tr.varc[1] ||
                                           v[2] != tr.varc[2]);
                tr.varc[0] = v[0]; tr.varc[1] = v[1]; tr.varc[2] = v[2];
                tr.haveVarc = true;
            }
        }
        if (finiteNz) {
            bool same = tr.haveLastM && std::memcmp(tr.lastM, out, 16 * sizeof(float)) == 0;
            if (camMoved && same) {
                if (++tr.frozen >= 8 && tr.validated) {   // stale block self-matches; force the rescan
                    tr.validated = false;
                    tr.staleOff = off;
                    matched = false;
                }
            } else if (!same) {
                tr.frozen = 0;
            }
            std::memcpy(tr.lastM, out, 16 * sizeof(float));
            tr.haveLastM = true;
        }
        if (matched) {
            tr.validated = true;
            tr.unmatched = 0;
            if (tr.staleOff == off) tr.staleOff = 0xFFFFFFFF;
        } else if (finiteNz && tr.validated) {
            if (++tr.unmatched >= 8) tr.validated = false;   // stopped matching for real
        }
        usable = matched || (finiteNz && tr.validated);
    }
    if (usable) return true;

    unsigned long long now = GetTickCount64(), last = g_matrixScanAt.load();
    if (now - last < 3000 || !g_matrixScanAt.compare_exchange_strong(last, now))
        return false;
    std::uint32_t staleOff;
    {
        std::lock_guard<std::mutex> lk(g_camTrustMu);
        staleOff = g_camTrust[pid].staleOff;
    }
    auto adopt = [&](std::uint32_t o, std::int32_t r, const float* m) {
        g_matrixOff.store(o, std::memory_order_relaxed);
        g_camPosRel.store(r, std::memory_order_relaxed);
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "view matrix resolved: worldView+0x%X (campos %+d)", o, r);
        rtx::log::Client(pid, msg);
        std::memcpy(out, m, 16 * sizeof(float));
        std::lock_guard<std::mutex> lk(g_camTrustMu);
        CamTrust& tr = g_camTrust[pid];
        tr.validated = true;
        tr.frozen = 0;
        tr.unmatched = 0;
        std::memcpy(tr.lastM, out, 16 * sizeof(float));
        tr.haveLastM = true;
    };
    // Pass 1: canonical layout -- centre stored at matrix+0x80. Several blocks
    // matching at once is possible (e.g. a cutscene camera block), so prefer a
    // match that also projects the live scene; the stale offset (frozen
    // detector) only wins if nothing else matches.
    struct Hit { std::uint32_t off; float m[16]; };
    Hit hits[8]; int nh = 0;
    Hit stale{}; bool haveStale = false;
    for (std::uint32_t o = 0; o + 64 <= kWvSpan && nh < 8; o += 0x10) {
        float m[16];
        if (!rpm_bytes(h, wv + o, m, sizeof(m))) continue;
        if (!validate_view_matrix(h, wv, o, 0x80, m)) continue;
        if (o == staleOff) { stale.off = o; std::memcpy(stale.m, m, sizeof(m)); haveStale = true; continue; }
        hits[nh].off = o; std::memcpy(hits[nh].m, m, sizeof(m)); ++nh;
    }
    if (nh > 0) {
        int pick = 0;
        if (probes.n > 0)
            for (int i = 0; i < nh; ++i)
                if (probes_project(hits[i].m, probes)) { pick = i; break; }
        adopt(hits[pick].off, 0x80, hits[pick].m);
        return true;
    }
    if (haveStale) { adopt(stale.off, 0x80, stale.m); return true; }
    // Pass 2: the centre field moved relative to the matrix. Find a matrix that
    // projects the live scene, solve its centre, and search around it for the
    // stored (east, up, north) copy to re-learn the relative offset.
    if (probes.n > 0) {
        for (std::uint32_t o = 0; o + 64 <= kWvSpan; o += 0x10) {
            float m[16];
            if (!rpm_bytes(h, wv + o, m, sizeof(m)) || !finite_nonzero_16(m)) continue;
            if (!probes_project(m, probes)) continue;
            double C[3];
            if (!solve_cam_centre(m, C)) continue;
            if (C[0] < 512.0 || C[1] < 512.0) continue;   // >= 1 tile east/north
            constexpr std::int32_t kWin = 0x400;
            std::int32_t lo = (o > (std::uint32_t)kWin) ? -(std::int32_t)kWin : -(std::int32_t)o;
            float buf[(2 * kWin) / 4 + 3];
            if (!rpm_bytes(h, wv + o + lo, buf, sizeof(buf))) continue;
            for (std::int32_t r = 0; r + 3 <= (std::int32_t)(sizeof(buf) / 4); ++r) {
                if (std::fabs((double)buf[r]     - C[0]) < 64.0 &&
                    std::fabs((double)buf[r + 1] - C[2]) < 64.0 &&
                    std::fabs((double)buf[r + 2] - C[1]) < 64.0) {
                    adopt(o, lo + r * 4, m);
                    return true;
                }
            }
        }
    }
    return false;
}
}  // namespace

std::string SceneJson(std::uint32_t pid, int obj_range) {
    constexpr std::uint64_t kContainer = rtx::scn::kContainer;   // all from SceneOffsets.h --
    constexpr std::uint64_t kActiveIdx = rtx::scn::kActiveIdx;   // shared with the companion
    constexpr std::uint64_t kEntryArr  = rtx::scn::kEntryArr;
    constexpr std::uint64_t kEntryWv   = rtx::scn::kEntryWv;
    constexpr std::uint64_t kVecBegin  = rtx::scn::kVecBegin;
    constexpr std::uint64_t kVecEnd    = rtx::scn::kVecEnd;
    constexpr std::uint64_t kSecPtr    = rtx::scn::kSecPtr;
    constexpr std::uint64_t kType      = rtx::scn::kType;
    constexpr std::uint64_t kName      = rtx::scn::kName;
    constexpr std::uint64_t kUid       = rtx::scn::kUid;
    constexpr std::uint64_t kConfig    = rtx::scn::kConfig;
    constexpr std::uint64_t kCombat    = rtx::scn::kCombat;
    constexpr std::uint64_t kPosX      = rtx::scn::kPosX;
    constexpr std::uint64_t kPosY      = rtx::scn::kPosY;
    constexpr std::uint64_t kPlayerData = rtx::scn::kPlayerData;
    constexpr std::uint64_t kLocalUid   = rtx::scn::kLocalUid;

    std::string players, npcs, specials;
    int vt4 = 0, vgfx4 = -1;   // diag: type-4 entities seen in the worldview vector + last gfx
    int pc = 0, nc = 0, sc4 = 0;
    int player_x = -1, player_y = -1;  // local player tile (anchors the object range)
    int player_plane = 0;              // local player plane (sec + 0x40)

    // Snapshot a DUPLICATE of the client's process handle under a brief g_mu hold, then run the
    // whole entity + object walk WITHOUT the lock. That walk can take tens of ms on a dense scene;
    // holding g_mu through it blocks the 5 ms tick, the 60 fps overlay and every other panel read.
    // The dup stays valid even if the client exits and SampleAll closes the original mid-walk
    // (SampleAll is the ONLY closer), so there is no use-after-close -- a read on a dead handle
    // just fails.
    HANDLE h = nullptr; std::uint64_t mgva = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end() && it->second.proc && it->second.main_global_va &&
            DuplicateHandle(GetCurrentProcess(), it->second.proc, GetCurrentProcess(),
                            &h, 0, FALSE, DUPLICATE_SAME_ACCESS))
            mgva = it->second.main_global_va;
    }
    struct DupGuard { HANDLE h; ~DupGuard() { if (h) CloseHandle(h); } } _dup{ h };
    if (h && mgva) {
        auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off)
            -> std::optional<std::uint64_t> {
            if (!p || *p <= 0x10000) return std::nullopt;
            return rpm<std::uint64_t>(h, *p + off);
        };
        auto root = rpm<std::uint64_t>(h, mgva);
        auto pdata = deref(root, kPlayerData);
        int local_uid = (pdata && *pdata > 0x10000)
                          ? rpm<std::int32_t>(h, *pdata + kLocalUid).value_or(-1) : -1;
        auto cont = deref(root, kContainer);
        auto idx  = (cont && *cont > 0x10000)
                      ? rpm<std::int32_t>(h, *cont + kActiveIdx) : std::nullopt;
        auto arr  = deref(cont, kEntryArr);
        std::optional<std::uint64_t> wv, worker, vb, ve;
        if (idx && *idx >= 0 && arr && *arr > 0x10000) {
            wv     = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + kEntryWv);
            if (wv && *wv > 0x10000) worker = scene_worker(h, pid, *wv, nullptr);
            vb     = deref(worker, kVecBegin);
            ve     = deref(worker, kVecEnd);
        }
        if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
            std::uint64_t n = (*ve - *vb) / 8;
            if (n > 20000) n = 20000;
            for (std::uint64_t i = 0; i < n; ++i) {
                auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
                if (!ep || *ep <= 0x10000) continue;
                auto sec = rpm<std::uint64_t>(h, *ep + kSecPtr);
                if (!sec || *sec <= 0x10000) continue;
                int type = rpm<std::uint8_t>(h, *sec + kType).value_or(0xff);
                if (type == 4) {                             // type-4 world entity (Time Sprite etc.)
                    // gfx at sec+0x74; position on the entity (ep+0x30/0x38). vt4/vgfx4 count ALL type-4
                    // for diagnostics; the scan ring is NOT a vector entity (it arrives via the
                    // companion highlight merge below), so keep the position gate to avoid (0,0) clutter.
                    int gfx  = rpm<std::int32_t>(h, *sec + 0x74).value_or(-1);
                    auto ex = rpm<float>(h, *ep + 0x30);
                    auto ey = rpm<float>(h, *ep + 0x38);
                    int sx4 = ex ? (int)(*ex / 512.f) : 0;
                    int sy4 = ey ? (int)(*ey / 512.f) : 0;
                    ++vt4; vgfx4 = gfx;
                    if (sx4 > 0 && sy4 > 0) {
                        int uid4 = rpm<std::int32_t>(h, *sec + 0x88).value_or(0);
                        int pl4  = rpm<std::int32_t>(h, *sec + 0x40).value_or(0);   // plane (sec+0x40)
                        if (pl4 < 0 || pl4 > 3) pl4 = 0;
                        if (sc4) specials.push_back(',');
                        char sbuf[160];
                        // "w":1 marks a REAL world position (vector type-4: Time Sprite,
                        // rockertunity, ...). The companion-merged scan ring below has no world
                        // tile, so the panel must not treat every special the same way.
                        std::snprintf(sbuf, sizeof(sbuf),
                            "{\"x\":%d,\"y\":%d,\"p\":%d,\"gfx\":%d,\"uid\":%d,\"w\":1}", sx4, sy4, pl4, gfx, uid4);
                        specials += sbuf; ++sc4;
                    }
                    continue;
                }
                if (type != 1 && type != 2) continue;        // players + NPCs only

                auto fx = rpm<float>(h, *sec + kPosX);
                auto fy = rpm<float>(h, *sec + kPosY);
                int tx = fx ? (int)(*fx / 512.f) : 0;
                int ty = fy ? (int)(*fy / 512.f) : 0;
                if (tx <= 0 || ty <= 0) continue;

                char nm[40] = {0};
                rpm_bytes(h, *sec + kName, nm, sizeof(nm) - 1);
                std::string name;
                for (int j = 0; j < (int)sizeof(nm) && nm[j]; ++j) {
                    unsigned char c = (unsigned char)nm[j];
                    if (c >= 0x20 && c <= 0x7e) name.push_back((char)c);
                    else if (c == 0xA0) name.push_back(' ');   // CP-1252 NBSP: display names use it ("Aurni x") -- dropping it corrupted the name
                }
                int uid = rpm<std::int32_t>(h, *sec + kUid).value_or(0);

                char buf[160];
                if (type == 2) {                              // player
                    if (name.empty()) continue;               // players always carry a live name
                    bool self = (uid == local_uid);
                    if (self) { player_x = tx; player_y = ty;
                                player_plane = rpm<std::int32_t>(h, *sec + 0x40).value_or(0); }
                    int combat = rpm<std::int32_t>(h, *sec + kCombat).value_or(-1);
                    if (combat < 0 || combat > 5000) combat = -1;
                    if (pc) players.push_back(',');
                    std::snprintf(buf, sizeof(buf),
                        "{\"uid\":%d,\"x\":%d,\"y\":%d,\"combat\":%d,\"self\":%s,\"name\":\"",
                        uid, tx, ty, combat, self ? "true" : "false");
                    players += buf; players += json_escape(name); players += "\"}";
                    ++pc;
                } else {                                      // NPC
                    int cfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                    int anim = rpm<std::int32_t>(h, *sec + 0xA90).value_or(-1);  // shared actor anim
                    // Resolve the LIVE morph variant. A morph base (e.g. the tutorial guildmaster,
                    // base 26929, varbit 46463) statically resolves to its FIRST child; resolve_npc
                    // reads the varbit/varp in-process and returns the variant the game is actually
                    // drawing. It returns GetNpc(base) for non-morphs and unreadable selectors, and
                    // returns EMPTY only when the live variant is HIDDEN (child == -1). Do NOT fall
                    // back to the static GetNpc on empty: that resurrects a hidden variant as a
                    // phantom at its old tile. An empty result means "skip".
                    bool morphHidden = false;
                    auto meta = resolve_npc(h, root.value_or(0), cfg, &morphHidden);
                    // Prefer the cache def name: the live name at sec+0xB8 can be an internal/dev
                    // name for some NPCs (e.g. summoned conjures showing "combatv2_necromancer...").
                    // Fall back to the live name only when the cache has none AND this is not a
                    // hidden morph -- a hidden morph keeps a stale in-memory name, so falling back
                    // would resurrect the relocated NPC at its old tile.
                    std::string npcName = !meta.name.empty() ? meta.name : (morphHidden ? std::string() : name);
                    if (npcName.empty()) continue;            // hidden morph variant / nameless decoration
                    std::string acts;
                    for (const auto& a : meta.actions) {
                        if (!acts.empty()) acts.push_back(',');
                        acts += '"'; acts += json_escape(a); acts += '"';
                    }
                    if (nc) npcs.push_back(',');
                    // Report the RESOLVED model id (the live morph child, e.g. 26930), not the morph
                    // base cfg (26929) -- that is the NPC actually being drawn at the current varbit.
                    int reportId = (meta.id >= 0) ? meta.id : cfg;
                    // FACING, as a heading in degrees, or -1 when unreadable. The actor carries a
                    // YAW QUATERNION: sec+0x1E0 = w, sec+0x1E8 = y, both snapping to cos/sin of
                    // 22.5-degree steps.
                    //   heading = 2 * atan2(-y, w)
                    // The negation is required: the game's yaw runs OPPOSITE to compass bearing.
                    int face = -1;
                    {
                        // 0x1E0/0x1E8, NOT 0x1D0/0x1D8. Both are yaw quaternions sitting exactly
                        // 45 degrees apart, and 0x1D0 LAGS the live facing by one step.
                        auto qw = rpm<float>(h, *sec + 0x1E0);
                        auto qy = rpm<float>(h, *sec + 0x1E8);
                        if (qw && qy && (*qw != 0.0f || *qy != 0.0f)) {
                            double deg = 2.0 * std::atan2(-static_cast<double>(*qy),
                                                          static_cast<double>(*qw))
                                         * 180.0 / 3.14159265358979323846;
                            deg = std::fmod(deg, 360.0);
                            if (deg < 0) deg += 360.0;
                            face = static_cast<int>(deg + 0.5) % 360;
                        }
                    }
                    std::snprintf(buf, sizeof(buf),
                        "{\"id\":%d,\"uid\":%d,\"x\":%d,\"y\":%d,\"combat\":%d,\"anim\":%d,\"face\":%d,\"size\":%d,\"name\":\"",
                        reportId, uid, tx, ty, meta.combat_level, anim, face, meta.size);
                    npcs += buf; npcs += json_escape(npcName);
                    npcs += "\",\"actions\":["; npcs += acts; npcs += "]}";
                    ++nc;
                }
            }
        }
    }
    // Objects pass: named static map scenery within `obj_range` tiles (Chebyshev)
    // of the local player. Purely cache-driven (MAPSV2 index 5 + loc configs
    // index 16) -- the live render nodes carry no externally-walkable loc id and
    // the engine keeps no flat runtime scenery table out of process, so the cache
    // is the complete + exact source. Anchored on the player; the UI range slider
    // sets how far to look. Most placements are unnamed walls/floors, so only named
    // ones are kept, deduped by (id, world tile). Empty when the player tile wasn't
    // resolved.
    // `vis` = this loc currently has a RENDERED MODEL. A cleared/solved loc can stay
    // in the worldview with its action intact but stop being drawn (Daemonheim
    // pedestal rooms leave mined Rubble still advertising "Mine"). The companion
    // publishes each runtime object's live model AABB, and a degenerate one
    // (bmax.x <= bmin.x) is the engine's own statement that nothing is drawn.
    // Static map locs have no runtime AABB, so they report vis = true.
    struct Obj { int id, x, y, plane, type, dist; std::string name, acts; bool rt; bool vis; };
    std::vector<Obj> objs;
    if (player_x >= 0) {
        if (obj_range < 1)   obj_range = 1;
        if (obj_range > 128) obj_range = 128;   // instances load floor-wide; let a full 8x8 floor's objects through
        constexpr int kCollectCap = 4000;   // safety bound before sorting
        std::unordered_set<long long> seen;
        // Process handle/root for live varbit-aware morph resolution, used for BOTH the static-map
        // pass and the runtime pass so morph locs (Archaeology hotspots, doors, tunnels) show the
        // variant the game is actually rendering. Reuse the dup'd handle (the entity-walk `root` is
        // scoped to the block above) and resolve root again from it -- no second g_states lookup.
        HANDLE rh = h; std::uint64_t rroot = 0;
        if (h) { auto rv = rpm<std::uint64_t>(h, mgva); if (rv) rroot = *rv; }
        int rx0 = (player_x - obj_range) / 64, rx1 = (player_x + obj_range) / 64;
        int ry0 = (player_y - obj_range) / 64, ry1 = (player_y + obj_range) / 64;
        for (int rx = rx0; rx <= rx1 && (int)objs.size() < kCollectCap; ++rx) {
            for (int ry = ry0; ry <= ry1 && (int)objs.size() < kCollectCap; ++ry) {
                if (rx < 0 || ry < 0) continue;
                for (const auto& p : rtx::cache::RegionLocations(rx, ry)) {
                    if ((int)objs.size() >= kCollectCap) break;
                    int wx = rx * 64 + p.x, wy = ry * 64 + p.y;
                    int ax = wx - player_x, ay = wy - player_y;
                    if (ax < 0) ax = -ax;
                    if (ay < 0) ay = -ay;
                    int dist = ax > ay ? ax : ay;        // Chebyshev (tiles away)
                    if (dist > obj_range) continue;       // range filter
                    if (p.plane != player_plane) continue;  // only the player's plane
                    auto meta = resolve_loc(rh, rroot, p.id);   // varbit-aware (live morph state)
                    if (meta.name.empty()) continue;
                    long long dk = ((long long)p.id << 40) | ((long long)wx << 20) | (unsigned)wy;
                    if (!seen.insert(dk).second) continue;   // same id+tile (other plane/type)
                    std::string acts;
                    for (const auto& a : meta.actions) {
                        if (!acts.empty()) acts.push_back(',');
                        acts += '"'; acts += json_escape(a); acts += '"';
                    }
                    objs.push_back({ p.id, wx, wy, p.plane, p.type, dist, meta.name, std::move(acts), false, true });
                }
            }
        }
        // Merge runtime-placed scenery published by the in-client companion: the
        // dynamic locs (Archaeology dig-site hotspots etc.) the static map can't
        // carry. Same range/plane/named-only/dedupe rules so the two sources combine
        // seamlessly; `seen` (filled by the cache pass) stops anything already in
        // the map from doubling. `rt` flags the live-sourced ones for the UI.
        std::vector<RuntimeObj> runtime;
        if (ReadRuntimeObjects(pid, runtime)) {
            for (const auto& r : runtime) {
                if ((int)objs.size() >= kCollectCap) break;
                if (r.config_id <= 0 || r.plane != player_plane) continue;
                int ax = r.x - player_x, ay = r.y - player_y;
                if (ax < 0) ax = -ax;
                if (ay < 0) ay = -ay;
                int dist = ax > ay ? ax : ay;
                if (dist > obj_range) continue;
                auto meta = resolve_loc(rh, rroot, r.config_id);
                if (meta.name.empty()) continue;
                long long dk = ((long long)r.config_id << 40) | ((long long)r.x << 20) | (unsigned)r.y;
                if (!seen.insert(dk).second) continue;       // already have it from the cache (exact tile)
                // Footprint dedupe: a loc's static-map tile (its SW anchor) differs
                // from its live render origin (the footprint centre) by at most
                // dim-1, while two DISTINCT instances of the same loc never overlap
                // and so sit >= dim apart. So same id + plane within max(dim)-1 tiles
                // is the SAME physical object (e.g. a 5x1 tunnel: anchor vs centre = 2).
                int tol = std::max(meta.dim_x, meta.dim_y) - 1; if (tol < 0) tol = 0;
                bool dup = false;
                for (const auto& o : objs)
                    if (!o.rt && o.id == r.config_id && o.plane == r.plane &&
                        std::abs(o.x - r.x) <= tol && std::abs(o.y - r.y) <= tol) { dup = true; break; }
                if (dup) continue;
                std::string acts;
                for (const auto& a : meta.actions) {
                    if (!acts.empty()) acts.push_back(',');
                    acts += '"'; acts += json_escape(a); acts += '"';
                }
                objs.push_back({ r.config_id, r.x, r.y, r.plane, -1, dist, meta.name, std::move(acts), true,
                                 r.bmax[0] > r.bmin[0] });   // degenerate AABB = not rendered
            }
        }
        std::sort(objs.begin(), objs.end(), [](const Obj& a, const Obj& b) {
            return a.dist != b.dist ? a.dist < b.dist : a.name < b.name;
        });
    }
    constexpr int kMaxObjects = 800;
    std::string objects;
    int oc = 0;
    for (const auto& o : objs) {
        if (oc >= kMaxObjects) break;
        char buf[208];
        if (oc) objects.push_back(',');
        std::snprintf(buf, sizeof(buf),
            "{\"id\":%d,\"x\":%d,\"y\":%d,\"plane\":%d,\"type\":%d,\"dist\":%d,\"rt\":%s,\"vis\":%s,\"name\":\"",
            o.id, o.x, o.y, o.plane, o.type, o.dist, o.rt ? "true" : "false",
            o.vis ? "true" : "false");
        objects += buf; objects += json_escape(o.name);
        objects += "\",\"actions\":["; objects += o.acts; objects += "]}";
        ++oc;
    }

    // Merge companion-observed render-pass highlights (the clue-scan ring etc.): transient type-4
    // graphics the game submits each frame but never keeps in the worldview vector, so the external
    // walk above cannot see them. The companion captures only what is rendered (player-local) and
    // dedupes by uid, so stale entries are dropped by the capture stamp; the panel filters by gfx.
    // No range filter here, so a ring whose position reads oddly still surfaces.
    std::uint32_t sdiag[12] = { 0,0,0,0,0,0,0,0,0,0,0,0 };   // ...+gfxhits/t4hits/MB/sub
    {
        std::vector<RuntimeHi> highs;
        if (ReadRuntimeHighlights(pid, highs, sdiag)) {
            std::uint32_t now = (std::uint32_t)GetTickCount64();
            for (const auto& hi : highs) {
                if ((std::uint32_t)(now - hi.stamp) > rtx::special::kStaleMs) continue;   // stale
                if (sc4) specials.push_back(',');
                char sbuf[160];
                std::snprintf(sbuf, sizeof(sbuf), "{\"x\":%d,\"y\":%d,\"gfx\":%d,\"uid\":%d}", hi.x, hi.y, hi.gfx, hi.uid);
                specials += sbuf; ++sc4;
            }
        }
    }
    char sdbuf[192];
    std::snprintf(sdbuf, sizeof(sdbuf), "[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]", sdiag[0], sdiag[1], sdiag[2], sdiag[3], sdiag[4], sdiag[5], sdiag[6], sdiag[7], sdiag[8], sdiag[9], sdiag[10], sdiag[11]);
    char vdbuf[48];
    std::snprintf(vdbuf, sizeof(vdbuf), "[%d,%d]", vt4, vgfx4);   // worldview-vector type-4 count + last gfx

    return "{\"players\":[" + players + "],\"npcs\":[" + npcs +
           "],\"objects\":[" + objects + "],\"specials\":[" + specials + "],\"sdiag\":" + sdbuf + ",\"vdiag\":" + vdbuf + "}";
}

// Live view-projection matrix: worldView + g_matrixOff, resolved at runtime by
// read_view_matrix (the offset drifts across builds: 0x13030 -> 0x13070 ->
// 0x13970 on 949). See project_runetoolsx_world_to_screen.

// Gameview (3D viewport) rect from the interface tree. Static chain:
// mainData+0x198C0 -> owner; container =
// owner+0x30; group array at container+0x58..+0x60 (stride 0x10, entry+8 =
// group obj, interface id @+0). Group 1477 = the game frame; child 27 -> child
// 28 = the 3D viewport. Widget node: ids i16 @+0x28/+0x2A/+0x2C, rect x@+0x70
// y@+0x74 w@+0x78 h@+0x7C. Returns false (caller leaves gv_* = 0 -> use window)
// unless the full chain validates and yields a positive rect.
static bool read_gameview_rect(HANDLE h, std::uint64_t mainData,
                               int& gx, int& gy, int& gw, int& gh) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t owner = r64(mainData + 0x198C0);
    if (owner <= 0x10000) return false;
    std::uint64_t cont = owner + 0x30;
    std::uint64_t gs = r64(cont + 0x58), ge = r64(cont + 0x60);
    if (gs <= 0x10000 || ge <= gs || (ge - gs) % 0x10 || (ge - gs) > 0x200000) return false; // UI-container gate
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != 1477) continue;            // group 1477 only
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) return false;
        for (std::uint64_t wn = a; wn + 0x18 <= b; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd <= 0x10000) continue;
            if (r16(nd + 0x28) != 1477 || r16(nd + 0x2a) != 27 || r16(nd + 0x2c) != -1) continue;  // 1477:27
            int px = r32(nd + 0x70), py = r32(nd + 0x74);             // parent (accumulates into child)
            const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };      // child-array offsets
            for (int k = 0; k < 3; ++k) {
                std::uint64_t cs = r64(nd + co[k]), ce = r64(nd + co[k] + 8);
                std::uint64_t ca = cs + 8, cb = ce + 8;
                if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                    std::uint64_t ch = r64(c);
                    if (ch <= 0x10000) continue;
                    std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;                         // child lives in a separate alloc
                    if (r16(ch + 0x28) != 1477 || r16(ch + 0x2a) != 28 || r16(ch + 0x2c) != -1) continue;  // 1477:28
                    int cw = r32(ch + 0x78), chh = r32(ch + 0x7c);
                    if (cw <= 0 || chh <= 0) return false;
                    gx = px + r32(ch + 0x70); gy = py + r32(ch + 0x74); gw = cw; gh = chh;
                    return true;
                }
            }
            return false;                                             // found 1477:27 but no child 28
        }
        return false;                                                 // group present, no 27 node
    }
    return false;                                                     // group 1477 absent (not in-world / login)
}

// Read a widget's display text: *(node+0x90) -> char* (legacy I_textP). The live
// string is usually already UTF-8 (player names use U+00A0 = 0xC2 0xA0); valid UTF-8
// passes through byte-for-byte and only genuinely non-UTF-8 (legacy CP-1252) bytes
// are widened, then JSON-escaped -- always emitting valid UTF-8 so a stray byte
// can't null the whole JSON in Ultralight. Empty when the widget has no text. RS3
// colour tags ("<col=ffffff>") are left in; the UI strips them.
// JSON-escape + UTF-8-normalise raw widget-text bytes (shared by the pointer
// and SSO text forms).
static std::string iface_encode_text(const char* buf, int len) {
    // Is the whole string already well-formed UTF-8? If so keep the bytes as-is;
    // double-encoding them corrupts the text. Otherwise it is legacy CP-1252 and
    // each high byte gets widened below. Both paths emit valid UTF-8.
    auto valid_utf8 = [&]() -> bool {
        for (int i = 0; i < len; ) {
            unsigned char c = (unsigned char)buf[i];
            int n;
            if (c < 0x80)                             n = 0;
            else if ((c & 0xE0) == 0xC0 && c >= 0xC2) n = 1;   // reject overlong C0/C1
            else if ((c & 0xF0) == 0xE0)              n = 2;
            else if ((c & 0xF8) == 0xF0 && c <= 0xF4) n = 3;
            else                                      return false;
            if (i + 1 + n > len) return false;
            for (int k = 1; k <= n; ++k)
                if (((unsigned char)buf[i + k] & 0xC0) != 0x80) return false;
            i += 1 + n;
        }
        return true;
    };
    bool keep = valid_utf8();

    std::string out;
    for (int i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)buf[i];
        if (ch == '"')              out += "\\\"";
        else if (ch == '\\')        out += "\\\\";
        else if (ch < 0x20)         out += ' ';                                       // control -> space
        else if (keep || ch < 0x80) out += (char)ch;                                  // already UTF-8 (or ASCII)
        else {
            // CP-1252 -> UTF-8. 0x80-0x9F are punctuation in CP-1252 (euro, curly quotes,
            // dashes, ...), NOT the Latin-1 C1 controls; map them through the table.
            // >= 0xA0 matches Latin-1.
            static const unsigned short kCp1252Hi[32] = {
                0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
                0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
                0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
                0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178 };
            unsigned int cp = (ch < 0xA0) ? kCp1252Hi[ch - 0x80] : ch;
            if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        }
    }
    return out;
}

static std::string iface_text_at(HANDLE h, std::uint64_t node, std::uint64_t field_off, int cap) {
    std::uint64_t p = rpm<std::uint64_t>(h, node + field_off).value_or(0);
    if (p <= 0x10000) return {};
    char buf[512] = {};
    if (cap > (int)sizeof(buf) - 1) cap = (int)sizeof(buf) - 1;
    if (!rpm_bytes(h, p, buf, cap)) return {};
    int len = 0;
    while (len < (int)sizeof(buf) - 1 && buf[len]) ++len;
    return iface_encode_text(buf, len);
}

// Display text at the usual field (*(node+0x90), legacy I_textP).
static std::string iface_text(HANDLE h, std::uint64_t node) {
    return iface_text_at(h, node, 0x90, 127);
}

// SECOND text member at node+0x180: a 24-byte FBString-style SSO string. Flag
// byte at +0x197: 0x80 = heap form {ptr @+0x180, size @+0x188, cap @+0x190};
// else inline chars at +0x180 with size = 23 - flag. Examples from the dialogue
// option-select group 1188: "Yes." flag 0x13 = 23-4; "No - do nothing." flag
// 0x07 = 23-16; heap "Yes - and don't remind me." size 26.
// This member holds the dialogue question/options, ability cooldowns, chat
// lines -- text the +0x90 field doesn't carry.
static std::string iface_sso_text(HANDLE h, std::uint64_t node) {
    std::uint8_t raw[0x18];
    if (!rpm_bytes(h, node + 0x180, raw, sizeof(raw))) return {};
    std::uint8_t flag = raw[0x17];
    char buf[512] = {};
    int len = 0;
    if (flag & 0x80) {
        std::uint64_t p = 0, sz = 0;
        std::memcpy(&p, raw, 8);
        std::memcpy(&sz, raw + 8, 8);
        if (p <= 0x10000 || sz == 0 || sz > sizeof(buf) - 1) return {};
        if (!rpm_bytes(h, p, buf, (int)sz)) return {};
        len = (int)sz;
    } else {
        if (flag > 23) return {};                 // not a live SSO string
        len = 23 - (int)flag;
        if (len <= 0) return {};
        std::memcpy(buf, raw, (size_t)len);
    }
    // sanity: a REAL string runs exactly its declared length (inline: 23-flag; heap: the size
    // field) with no interior NUL, all printable. +0x180 is a per-class UNION: containers keep a
    // child-vector here and GRAPHIC nodes keep a scalar (live: bytes 0xbc/0x80 with flag 0 ->
    // declared len 23 but a NUL at index 1). A NUL before the declared end = union junk,
    // so reject it.
    // 0xFF is the empty/unset jstring sentinel -- still treated as NON-text so empty widgets
    // show nothing instead of a stray "ÿ".
    int printable = 0;
    for (int i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0) return {};                    // shorter than declared -> not a string
        if (c >= 0x20 && c != 0xFF) ++printable;
    }
    if (len < 1 || printable < len) return {};
    return iface_encode_text(buf, len);
}

// Walk one widget node + its children depth-first, appending each as a JSON
// object to `out`. Shares the node layout used by read_gameview_rect.
// baseX/baseY = the accumulated absolute screen origin of this node's PARENT
// (seed with the panel origin so emitted rects are absolute screen coords, not
// group-relative). Each node's +0x70/+0x74 is relative to its parent, so the
// node's absolute position is base + (x,y) and its children recurse from there.
// Emits "r"=[relX,relY,w,h] (unchanged) AND "a"=[absX,absY] when an origin is known.
static void iface_walk(HANDLE h, int group, std::uint64_t node, int depth,
                       std::string& out, int& count, bool& first, int baseX, int baseY, bool haveAbs) {
    if (count >= 6000 || depth > 12) return;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    int i1 = r16(node+0x28), i2 = r16(node+0x2a), i3 = r16(node+0x2c);
    int x = r32(node+0x70), y = r32(node+0x74), w = r32(node+0x78), hh = r32(node+0x7c);
    int ax = baseX + x, ay = baseY + y;        // absolute screen position of this node
    std::string txt = iface_text(h, node);     // *(node+0x90) display text (chat, labels, ...)
    if (txt.empty()) txt = iface_sso_text(h, node);   // +0x180 SSO member (dialogue options, inline labels)
    int item = r32(node + 0x1a0);              // item id on an item slot; a packed ARGB colour on text/graphic
    int amt  = r32(node + 0x1a8);              // item stack / quantity (currency amounts, item counts)
    // node+0x188 holds EITHER a small cache sprite id (zero-extended into 8 bytes) OR a pointer to a
    // dynamic-graphic content object -- but ONLY on the graphic classes. On TEXT nodes the whole
    // +0x180..0x197 span IS the SSO string object, so +0x188 reads the heap-string SIZE (live: GE /
    // chat rows showed "spr 44" for a 44-char line, "spr 64" for a 64-char warning) or leftover
    // inline characters. Discriminate by the +0x181..0x187 bytes: graphic nodes keep zeros / small
    // scalars there (live: ff/bc/80 + 00s), text nodes keep string data -- any PRINTABLE ASCII marks
    // string storage, not a sprite. A node with resolved text is never sprite-bearing either: icon +
    // text are SIBLING nodes (buff slot 19:0 sprite vs 19:1 countdown).
    std::uint64_t sprRaw = r64(node + 0x188);
    bool sprUnset = sprRaw == ~0ull;               // all-FF = the unset sentinel: NO graphic content (not "dynamic")
    bool sprOk = txt.empty();
    if (sprOk) {
        std::uint8_t sb[8] = {};
        if (rpm_bytes(h, node + 0x180, sb, 8))
            for (int i = 1; i < 8 && sprOk; ++i)
                if (sb[i] >= 0x20 && sb[i] < 0x7f) sprOk = false;
    }
    // Bit-62-flagged +0x188 = new-style OBJ-ICON graphic (POH-rework grids, e.g. 1512):
    // 0x4000000000000000 | flavour<<24 | itemId in the low 24 bits (0x02 flavour = 36x32
    // category icon, 0x12 = 63x56 grid cell; +0x1a0 duplicates the plain item id). Real
    // heap pointers never reach bit 62 (user-mode VA < 2^48), so this cannot collide with
    // the dynamic-content-pointer case. Folded into the existing 131072+item encoding the
    // Interfaces tab renders via the item icon pack. Bound the unpacked id to real item-id
    // range: SSO string bytes can read as a bit-62 value, and legit ids stay < 200000.
    bool sprIsItem = sprOk && !sprUnset && (sprRaw >> 62) == 1 && (sprRaw & 0xFFFFFF) < 200000;
    bool sprIsObj = sprOk && !sprUnset && !sprIsItem && sprRaw > 0xFFFFFFFFull;   // 64-bit heap pointer -> dynamic graphic
    int  spr      = (sprOk && !sprIsObj && !sprUnset && sprRaw > 0 && sprRaw < 0x100000) ? (int)sprRaw : 0;
    if (sprIsItem) spr = 131072 + (int)(sprRaw & 0xFFFFFF);
    bool vis = ((std::uint32_t)r32(node + 0x50) & 0x01010000u) == 0x01010000u;
    // Component TYPE, inferred from the node's payload (build-stable; the true discriminator is the C++
    // vtable at node+0x0, but those RVAs change every build). Verified against CS2-proven nodes: comp 14
    // (CC_DELETEALL) -> has children -> layer; comp 15 (IF_SETTEXT) -> text; item icons carry the item id
    // at +0x1a0 + the amount at +0x1a8 (live: Heartments 52860 x2475) AND a matching +0x188 key (see
    // realItem below) -- +0x1a0 alone is ambiguous because its meaning is per-class (colour on text,
    // scalar 2 on the footer hover class). Item ranks before text (an icon has both id and name).
    auto hasChild = [&](std::uint64_t off) -> bool {
        std::uint64_t cs = r64(node + off), ce = r64(node + off + 8), ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) return false;
        return r64(ca) > 0x10000;          // at least one real child pointer
    };
    bool hasKids   = hasChild(0x198) || hasChild(0x1c8) || (txt.empty() && hasChild(0x180));
    // +0x1a0 is CLASS-dependent (per the node's widget class): item id on item-icon
    // graphics, a packed colour on text, small scalars / pointer fragments elsewhere.
    // Range alone therefore false-positives -- the equipment/backpack footer's hover class
    // keeps 2 there. A REAL item icon also carries a matching key at +0x188: the slot
    // flavour packs 0x60000+item (equipment 15:*, backpack 5:*), the currency-pouch
    // flavour (1473 comp 20:*) leaves the unset sentinel. Require one of the two flavours.
    bool realItem  = (item > 0 && item < 200000) &&
                     (sprUnset || sprRaw == 0x60000ull + (std::uint64_t)item);
    // A rendered graphic always has a size; a ZERO-SIZED node with a small +0x188 scalar is a
    // different widget class whose per-class field passed the byte gates (the quest list's 0x0
    // rows carry an enum, not sprite ids). Size-gate the graphic call.
    bool sized     = (w > 0 && hh > 0);
    bool okSpr     = (spr > 0) && sized;   // spr already gated to a plausible sprite-id range above
    // A pointer-backed (dynamic) graphic is still a graphic -- label it as such, just without a (wrong) sprite id.
    const char* ty = hasKids ? "layer" : realItem ? "item" : !txt.empty() ? "text" : (okSpr || (sprIsObj && sized)) ? "graphic" : "rect";
    out += first ? "" : ",";
    out += "{\"g\":" + std::to_string(group) + ",\"t\":[" + std::to_string(i1) + "," +
           std::to_string(i2) + "," + std::to_string(i3) + "],\"d\":" + std::to_string(depth) +
           ",\"ty\":\"" + ty + "\",\"r\":[" + std::to_string(x) + "," + std::to_string(y) + "," +
           std::to_string(w) + "," + std::to_string(hh) + "]";
    if (haveAbs) out += ",\"a\":[" + std::to_string(ax) + "," + std::to_string(ay) + "]";
    if (!txt.empty())          out += ",\"x\":\"" + txt + "\"";
    if (realItem)              out += ",\"it\":" + std::to_string(item);
    if (realItem && amt > 0)   out += ",\"n\":" + std::to_string(amt);          // stack / amount (e.g. currency)
    if (okSpr && !realItem)    out += ",\"s\":" + std::to_string(spr);          // a graphic's sprite (not an item echo)
    if (vis)                   out += ",\"v\":1";
    out += "}";
    first = false; ++count;
    const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };   // child-array offsets
    for (int k = 0; k < 3; ++k) {
        std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
        std::uint64_t ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
        for (std::uint64_t c = ca; c + 0x18 <= cb && count < 6000; c += 0x18) {
            std::uint64_t ch = r64(c);
            if (ch <= 0x10000) continue;
            std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
            if (d <= 0x3000) continue;                     // child in a separate alloc
            iface_walk(h, group, ch, depth + 1, out, count, first, ax, ay, haveAbs);
        }
    }
}

static void iface_groups_range(HANDLE h, std::uint64_t mainData,
                               std::uint64_t& gs, std::uint64_t& ge) {
    gs = ge = 0;
    std::uint64_t owner = rpm<std::uint64_t>(h, mainData + 0x198C0).value_or(0);
    if (owner <= 0x10000) return;
    std::uint64_t cont = owner + 0x30;
    std::uint64_t s = rpm<std::uint64_t>(h, cont + 0x58).value_or(0);
    std::uint64_t e = rpm<std::uint64_t>(h, cont + 0x60).value_or(0);
    if (s <= 0x10000 || e <= s || (e - s) % 0x10 || (e - s) > 0x200000) return;
    gs = s; ge = e;
}

// Read one companion-published var value by (scope,id). scope 4 = varp/varbit,
// 5 = varc-int. RS3 movable panels store their on-screen X/Y in varc-ints, which
// the launcher can't resolve externally (getStorage is an in-process vtable call),
// so the companion publishes them; this reads the torn-free snapshot. false when
// the companion isn't loaded / the var isn't resolved yet.
static bool read_companion_var(std::uint32_t pid, int scope, int id, int& out) {
    wchar_t name[64];
    rtx::varc::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<const rtx::varc::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::varc::Share)));
    if (!sh) { CloseHandle(h); return false; }
    bool found = false;
    if (sh->magic == rtx::varc::kMagic && sh->version == rtx::varc::kVersion) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::uint32_t s1 = sh->seq;
            if (s1 & 1u) continue;
            std::uint32_t cnt = sh->count;
            if (cnt > (std::uint32_t)rtx::varc::kMaxVars) cnt = rtx::varc::kMaxVars;
            int val = 0; bool hit = false;
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto& e = sh->entries[i];
                if ((int)e.scope == scope && (int)e.id == id) { val = (int)e.value; hit = true; break; }
            }
            std::uint32_t s2 = sh->seq;
            if (s1 == s2 && !(s2 & 1u)) { out = val; found = hit; break; }
        }
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return found;
}

// RS3 movable-panel position table (the draggable-panel group set).
// Each open interface GROUP's on-screen origin = its X/Y
// varc-ints (scope 5). All dialogue groups share 9102/9103. Only the anchored origin
// (top-left) is needed to make a group's widgets absolute; widget sizes
// come from the widgets themselves. CENTERED_IN_FRAME panels (teleports/tool)
// are omitted here -- their origin needs the widget size to centre, handled
// only if a feature needs them.
// mount_comp: when the position var is unset (panel never dragged), use the absolute screen position of
// this component in the game frame (group 1477) as the origin -- that's the central-interface slot the
// sub-interface renders into. 0 = no fallback (origin fails when the var is absent).
// is_varc: the position id is a VARC-int (global var hashmap), NOT a varp. Read it via read_varc_found ONLY --
// otherwise the companion varp-observer / varp hashmap can shadow it (a varp with the same id, value 0, wins
// and pins the origin to 0 -> "only correct at panel 0,0"). The celtic knot's 6310/6311 are varcs.
// req_group: this spec only applies while that group is ALSO open -- variant routing for groups
// that render inside different window frames with different position vars (0 = always applies).
// A group may have several specs; iface_panel_origin tries them in table order.
struct PanelOriginSpec { int group; int var_x; int var_y; int off_left; int off_top; int mount_comp; bool is_varc; int req_group; };
static const PanelOriginSpec kPanelOrigins[] = {
    { 1188, 3082, 3083, 0, 0, 0, true },   // Choose dialog (option select) -- centered; position via live VARC 3082/3083
    { 1184, 3082, 3083, 0, 0, 0, true },   // NPC dialog
    { 1191, 3082, 3083, 0, 0, 0, true },   // Player dialog
    { 1189, 3082, 3083, 0, 0, 0, true },   // Clue continue
    { 1186, 3082, 3083, 0, 0, 0, true },   // Server message dialog
    { 1603, 9102, 9103, 0, 0 },   // Input text
    { 1370, 3089, 3090, 0, -9, 0, true },  // Item Production content: ALWAYS renders inside the 1371 window frame
                                  // (there is no standalone 1370 variant), so its origin is the frame's varcs
                                  // 3089/3090.
    { 13,   3089, 3090, 0, 31, 0, true },  // Bank pin -- window-frame varcs 3089/3090
    { 1224, 3089, 3090, 0, 0, 0, true },   // Ritual selection (Necromancy! communion ritual) -- window-frame varcs 3089/3090
    { 1286, 3089, 3090, 0, 0, 0, true },   // Discovered Ratings (Fish Flingers) -- window-frame varcs 3089/3090.
                                  // 2026-08-02: anchoring it to the central-overlay frame 1477:728 (centred) was
                                  // tried and REVERTED -- the box did not track the live window. Keep the varcs.
    // House Controls (POH rework): group 1665 is a sub-overlay that mounts into a resizable
    // window frame whose SLOT COMP VARIES with docking (comp 376 when floating, 116/171/...
    // when docked into a tabbed slot) -- so a fixed mount comp is wrong the moment it docks.
    // Like the inventory (3040/3041), key off 1665's own position varc 3096/3097, which the
    // game keeps pointed at its window frame wherever it lives. Base inset = frame + (4,16)
    // (border + modern title). Mode adjustments are applied in iface_panel_origin (classic -12
    // / tabbed +28) -- see there. HARDCODED offsets on purpose: robust at any window position
    // including screen 0,0, where walking the 1477 tree to the frame mis-anchors on the root.
    { 1665, 3096, 3097, -4, -16, 0, true },
    { 1223, 3096, 3097, 0, 0, 0, true },   // Active ritual (Necromancy!) -- position varcs 3096/3097
    { 923,  3096, 3097, 0, 0, 0, true },   // Fish Flingers competition results -- position varcs 3096/3097
    { 919,  3047, 3048, 0, 0, 0, true },   // Fish Flingers live scoreboard (heaviest / total fish /
                                  // total weight / assists, habitat depletion bar) -- varcs 3047/3048
    { 1222, 6463, 6464, 0, 0, 0, true },   // Well of Souls talent tree (Necromancy!) -- varcs 6463/6464 (central-overlay family)
    { 660,  9121, 9122, 0, -9 },  // Material storage -- UNVERIFIED varp path (flip to its varcs once live-checked)
    { 656,  6310, 6311, 0, 0, 728, true }, // Museum / Training Weapons donation -- varcs 6310/6311 (the knot family). Mount
                                  // slot 1477:728 stays the fallback at the never-dragged default.
    { 691,  6463, 6464, 0, 0, 0, true },   // Relic power -- varcs 6463/6464 (central-overlay family)
    { 1594, 6463, 6464, 0, 0, 0, true },   // Shop (e.g. Ezreal's) -- varcs 6463/6464
    { 517,  5632, 5633, 0, 0, 0, true },   // Bank -- varcs 5632/5633
    { 190,  5846, 5847, 0, -16, 0, true }, // Quest -- varcs 5846/5847; off_top -16 nudges widgets down 16px
    { 1092, 6463, 6464, 0, 0, 0, true },   // Lodestone network -- varcs 6463/6464 (recalibrate via the Interfaces tab if
                                  // anything sits off)
    { 584,  10071, 10072, 0, 0 }, // DXP timer -- UNVERIFIED varp path (flip to its varcs once live-checked)
    { 1473, 3040, 3041, 0, 0, 0, true },   // Inventory (backpack) -- LIVE varc 3040 (x) / 3041 (y), is_varc=true so the
                                           // origin is read fresh from the varc hashmap each frame instead of the companion
                                           // observer snapshot, which lags unless something keeps pumping vars.
    // Celtic-knot puzzle layouts: positioned by VARC 6310 (x) / 6311 (y) = live panel origin.
    // is_varc=true so the varp/companion path can't shadow it with a 0.
    // 1477 mount slot 728 stays a last-ditch fallback (full-frame layer at 0,0, origin-only-ok).
    { 394, 6310, 6311, 0, 0, 728, true }, { 519, 6310, 6311, 0, 0, 728, true }, { 525, 6310, 6311, 0, 0, 728, true },
    { 526, 6310, 6311, 0, 0, 728, true }, { 529, 6310, 6311, 0, 0, 728, true }, { 1000, 6310, 6311, 0, 0, 728, true },
    { 1001, 6310, 6311, 0, 0, 728, true }, { 1002, 6310, 6311, 0, 0, 728, true }, { 1003, 6310, 6311, 0, 0, 728, true },
    { 1934, 6463, 6464, 0, 0, 0, true },  // Towers (Skyscrapers, master clue). Positioned by VARC 6463 (x) / 6464 (y).
    { 518,  6463, 6464, 0, 0, 0, true },  // Deduction notes (Secrets of Amberfell). Same position varcs as the towers panel.
    // Player-Owned Ports composite screens: their trees are SLOT-RELATIVE to the 800x600
    // central-LARGE slot (1477 comp 724), whose live position is varcs 6463/6464 - proven by
    // matching the varcs against comp 724's accumulated rect at two window positions
    // (2026-07-29). Same varc family as the towers/lodestone panels above.
    { 916,  6463, 6464, 0, 0, 0, true },  // Ports ship view (shipyard / crew window / voyages)
    { 1276, 6463, 6464, 0, 0, 0, true },  // Ports Crew Roster
    { 1933, 3089, 3090, 0, -8, 0, true },  // Lockbox (master clue). Positioned by VARC 3089 (x) / 3090 (y) --
                                  // the live panel origin. is_varc=true so the varp path can't shadow it with a 0.
                                  // No mount fallback (the position varc is reliably published when the box is open).
    { 1371, 3089, 3090, 0, -9, 0, true },  // Make-x / potion crafting window (opens on using the pestle etc.).
                                  // Shares the movable-window position VARCs 3089 (x) / 3090 (y) with the lockbox.
                                  // off_top -9 nudges every widget DOWN 9px (oy = var_y - off_top).
    { 720,  0, 0, 0, 0, 735, false },      // Option-select window (reused for the Murder on the Border "identify the
                                  // murderer" list, farming tree-select, etc.). 720 is a "Central Interface": it is NOT
                                  // nested under 1477 in the widget tree, but its content is laid out for the 512x334
                                  // central-interface content slot -- comp 1477:735. The window (comp 2, 300x286) sits at
                                  // group-rel (106,24) = PRE-CENTRED for 512x334 ((512-300)/2=106, (334-286)/2=24). So the
                                  // origin is simply comp 735's live top-left; NO extra centring (that would double it).
                                  // read_iface_mount_origin(735) tracks the slot live as the client resizes.
    { 743,  6423, 6424, 0, 0, 0, true },  // Extra action button. Positioned by VARC 6423 (x) / 6424 (y). Guides box
                                  // component 7 (the 38x38 button layer) via ifaceCompRects -- the root layer 0
                                  // is 150x56 and includes the hotkey label text above the button.
    { 1512, 0, 0, 0, 0, 722 },    // Build-mode furniture placement sidebar (POH rework, 800x600 canvas; panel node 7
                                  // at -3,93 255x614 + collapse-tab node 24 at 248,93). ALWAYS open (gate on 1514).
                                  // VIEWPORT-anchored: the canvas maps 1:1 to the screen, so the origin is (0,0) --
                                  // expressed as 1477 mount comp 722, the fullscreen layer at 0,0 (no position varc;
                                  // nudge via the Interfaces tab if a live test disagrees).
                                  // Item grid = comp 16 subs (:261...): NO text; graphic field = ITEM id (61880 =
                                  // crude wooden chair) -- match cells via InterfaceCompsJson's "spr".
    { 1030, 6463, 6464, 0, 0, 0, true },    // "Link a clue" investigation board (Fort Forinthry mysteries, e.g.
                                  // Murder on the Border). Positioned by VARC 6463 (x) / 6464 (y) -- same pair
                                  // as the towers/deduction/furniture windows.
                                  // No mount fallback (the position varc is published while it's open).
    { 1518, 6463, 6464, 0, 0, 726, true },  // Furniture Storage (POH rework, 800x600 canvas, window node 5 at 88,30
                                  // 624x540). Same position varcs as 1516 below.
                                  // CAVEAT: the group appears ALWAYS OPEN (its comp tree persists while the window
                                  // is hidden), so group presence is NOT an open signal -- gate on 1514, the
                                  // fullscreen frame that opens/closes with these windows. Slot layers: comp 18 =
                                  // empty slot frames (sprite 18266), comp 19 subs = contents (text = colored item
                                  // name, graphic field = the ITEM id, e.g. 61880 crude wooden chair).
    { 1516, 6463, 6464, 0, 0, 726, true },  // Furniture Construction (POH rework workbench window, 800x600 canvas).
                                  // Positioned by VARC 6463 (x) / 6464 (y) -- same pair as the towers/deduction
                                  // panels above. Fallback when the varc is unset:
                                  // 1477 central slot cluster 722-727 (724 = container, 727 = invisible chrome,
                                  // 726 = the childless mount comp; live dump had all three at the same abs).
                                  // Guides box comp 23 -- the 252x27 CONSTRUCT-button SLOT (the button itself is
                                  // new-style UI content mounted into it; it has NO classic text/child nodes, so
                                  // the slot rect is the only boxable thing).
    { 1931, 0, 0, 0, -12, 732 },  // Puzzle box (sliding). off_top -12 (oy = mount_y - off_top, so this nudges DOWN 12).
                                  // It's a CENTERED central interface (NOT a draggable panel) --
                                  // it renders into the 1477 frame's mount slot, component 732. So there
                                  // is no position varc (3089/3090 don't track it; the highlight didn't follow a move);
                                  // the origin = that mount component's LIVE screen position, which tracks automatically
                                  // wherever the interface draws. Same mechanism as the donation window 656 / slot 728.
                                  // Position varcs live in the global varc hashmap (read_varc) -- NOT varps, NOT
                                  // companion-captured -- so iface_panel_origin falls back to read_varc.
};
// Live per-group calibration nudge, ADDED on top of the kPanelOrigins offset. Set from the Interfaces
// tab so a panel's screen offset can be dialed in visually without a rebuild; bake the dialed value into
// kPanelOrigins afterwards. Keyed by group; applies to every client (the panel layout is shared).
static std::mutex g_ifaceOffMu;
static std::unordered_map<int, std::pair<int,int>> g_ifaceOff;
void SetIfaceOffset(int gid, int dx, int dy) {
    std::lock_guard<std::mutex> lk(g_ifaceOffMu);
    if (dx == 0 && dy == 0) g_ifaceOff.erase(gid);
    else g_ifaceOff[gid] = { dx, dy };
}

// Absolute screen position of a component in the game frame (group 1477) -- the accumulated +0x70/74
// down the widget tree. Used to anchor a central-interface sub-interface (e.g. the donation window 656)
// to the slot it renders into. false when group 1477 / the component isn't found.
static bool read_iface_mount_origin(HANDLE h, std::uint64_t main_data, int mount_comp, int& ox, int& oy) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, main_data, gs, ge);
    if (!gs) return false;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != 1477) continue;            // game frame
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a) return false;
        bool found = false; int fx = 0, fy = 0;
        std::function<void(std::uint64_t,int,int,int)> walk =
            [&](std::uint64_t node, int bx, int by, int depth) {
            if (found || depth > 14) return;
            int ax = bx + r32(node + 0x70), ay = by + r32(node + 0x74);
            if (r16(node + 0x2a) == mount_comp && r32(node + 0x78) > 0 && r32(node + 0x7c) > 0) {
                fx = ax; fy = ay; found = true; return;
            }
            const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
            for (int k = 0; k < 3 && !found; ++k) {
                std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
                std::uint64_t ca = cs + 8, cb = ce + 8;
                if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                for (std::uint64_t c = ca; c + 0x18 <= cb && !found; c += 0x18) {
                    std::uint64_t ch = r64(c);
                    if (ch <= 0x10000) continue;
                    std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;
                    walk(ch, ax, ay, depth + 1);
                }
            }
        };
        for (std::uint64_t wn = a; wn + 0x18 <= b && !found; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd > 0x10000) walk(nd, 0, 0, 0);
        }
        if (found) { ox = fx; oy = fy; return true; }
        return false;
    }
    return false;
}

// One position var, multi-source with the RIGHT precedence. The companion observer is the
// livest source, but it publishes a transient 0 through a just-died storage pointer
// (ReadIntSafe faults -> 0 until the ~2s eviction pass), and a script-scope capture can
// shadow a real id with 0. Accepting that 0 pins panels to the "var = 0" position while
// the true var is nonzero, so a companion ZERO never beats a direct read: companion
// nonzero > varp nonzero > varc found (a stale last-known position still beats 0) >
// companion 0 (only when no direct source exists).
static bool read_panel_pos_var(HANDLE h, std::uint64_t main_data, std::uint32_t pid,
                               int id, bool is_varc, int& out) {
    int cv = 0;
    bool cok = is_varc ? read_companion_var(pid, 5, id, cv)
                       : (read_companion_var(pid, 4, id, cv) || read_companion_var(pid, 5, id, cv));
    int vv = 0; bool vok = false;
    if (is_varc) vok = read_varc_found(h, main_data, id, vv);
    else {
        int pv = read_varp(h, main_data, id);
        if (pv != 0) { vv = pv; vok = true; }
        else vok = read_varc_found(h, main_data, id, vv);
    }
    // NONZERO beats zero; at equal class the DIRECT hashmap beats the companion observer --
    // observer entries are keyed by capture handler, not registry identity, so a script-scope
    // var can impersonate a position id. Companion values only fill genuine hashmap misses.
    if (vok && vv != 0) { out = vv; return true; }
    if (cok && cv != 0) { out = cv; return true; }
    if (vok) { out = vv; return true; }
    if (cok) { out = cv; return true; }
    return false;
}

// Is an interface group currently open (present in the group list with a sane widget list)?
// Same open test DialogJson uses. Cheap: one pass over the group directory.
static bool iface_group_open(HANDLE h, std::uint64_t main_data, int gid) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, main_data, gs, ge);
    if (!gs) return false;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap = r64(g + 8);
        if (ap <= 0x10000 || r32(ap) != gid) continue;
        std::uint64_t ws = r64(ap + 0x20), we = r64(ap + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        return ws && we && a > 0x10000 && b > a && (b - a) <= 0x100000;
    }
    return false;
}

// True if any node in the game frame (group 1477) renders sprite `sprite_id`. Used to detect
// House Controls' window tab bar: its tab icon (18788) is present ONLY when the window is
// tabbed (2+ panels docked). A plain header strip exists even when solo, so geometry alone
// can't tell -- but this icon can. Cheap enough for the niche House Controls origin.
static bool iface_has_sprite(HANDLE h, std::uint64_t main_data, int sprite_id) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto spr = [&](std::uint64_t a){ return (int)rpm<std::uint16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, main_data, gs, ge);
    if (!gs) return false;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != 1477) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28), a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a) return false;
        bool found = false;
        std::function<void(std::uint64_t,int)> walk = [&](std::uint64_t node, int depth) {
            if (found || depth > 16) return;
            if (spr(node + 0x188) == sprite_id) { found = true; return; }
            const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
            for (int k = 0; k < 3 && !found; ++k) {
                std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8), ca = cs + 8, cb = ce + 8;
                if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                for (std::uint64_t c = ca; c + 0x18 <= cb && !found; c += 0x18) {
                    std::uint64_t ch = r64(c);
                    if (ch <= 0x10000) continue;
                    std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;
                    walk(ch, depth + 1);
                }
            }
        };
        for (std::uint64_t w = a; w + 0x18 <= b && !found; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) walk(nd, 0);
        }
        return found;
    }
    return false;
}

// Resolve a group's absolute screen origin from its position vars. Returns false (origin unchanged)
// when the group has no spec / can't be located -- callers then fall back to group-relative coords.
// A group may have SEVERAL specs (variant routing via req_group); each is tried in table order and
// the first that resolves wins, so a variant whose vars are absent falls through to the next spec.
static bool iface_panel_origin(HANDLE h, std::uint64_t main_data, std::uint32_t pid, int gid, int& ox, int& oy) {
    for (const auto& s : kPanelOrigins) {
        if (s.group != gid) continue;
        if (s.req_group && !iface_group_open(h, main_data, s.req_group)) continue;   // variant gate
        // Position vars: a movable panel only WRITES these once dragged; at its default spot the
        // var is absent from every source. For such a panel with a mount_comp (donation window
        // 656), the default is the central-interface slot it renders into. Source precedence
        // lives in read_panel_pos_var: the companion observer tracks a moving panel live, but
        // its transient zeros must never beat a direct varp/varc read.
        int vx = 0, vy = 0; bool okx = false, oky = false;
        if (s.var_x < 0 && s.var_y < 0) {
            okx = oky = true;   // identity spec: the group's tree is already screen-absolute
        } else {
            if (s.var_x > 0) okx = read_panel_pos_var(h, main_data, pid, s.var_x, s.is_varc, vx);
            if (s.var_y > 0) oky = read_panel_pos_var(h, main_data, pid, s.var_y, s.is_varc, vy);
        }
        if (okx && oky) {
            ox = vx - s.off_left; oy = vy - s.off_top;
        } else if (s.mount_comp) {
            int cx, cy;
            if (!read_iface_mount_origin(h, main_data, s.mount_comp, cx, cy)) continue;   // next spec
            ox = cx - s.off_left; oy = cy - s.off_top;
        } else {
            continue;   // this spec can't resolve -> try the group's next spec
        }
        // House Controls (1665): base origin = frame + (4,16) [modern-solo title inset]. Two
        // mode adjustments (hardcoded: solo +16, tabbed +44, legacy +4):
        //  - CLASSIC/legacy interface (varbit 27169 = varp 3680 bit 21): smaller chrome -> -12.
        //  - MODERN tabbed: a tab bar drops the content 28px -- keyed on the tab icon sprite
        //    18788, which appears only when House Controls is docked/tabbed with another panel.
        if (gid == 1665) {
            if ((read_varp(h, main_data, 3680) >> 21) & 1) oy -= 12;
            else if (iface_has_sprite(h, main_data, 18788)) oy += 28;
        }
        // Option-select 720 needs no special-casing: its kPanelOrigins mount comp 735 (the 512x334
        // central-interface content slot) already yields the correct origin, because 720's content is
        // pre-centred for that slot: comp 735 TL=(432,112) and the window lands centred with
        // no extra math.
        { std::lock_guard<std::mutex> lk(g_ifaceOffMu);   // live calibration nudge from the Interfaces tab
          auto ov = g_ifaceOff.find(gid);
          if (ov != g_ifaceOff.end()) { ox += ov->second.first; oy += ov->second.second; } }
        return true;
    }
    // DEV EXPERIMENT (2026-08-02, dev branch only) -- cache-driven panel mounts. Flip
    // kCachePanelMounts to false to revert to table-only origins; no other change needed.
    // The HUD panel registry (enum 7716, js5-17) maps each content-slot id to a panel struct;
    // params 3514-3517 pack the panel's CONTENT interface comps as (group<<16)|sub (Skills
    // 320:0, Prayers 1457:2, Backpack 1474:0 -- cache-verified 2026-08-02) and param 3503
    // packs its mount comp under 1477 the same way, so the registry yields hosted-group ->
    // 1477 mount sub with no kPanelOrigins entry. Runs ONLY when the table above missed, so
    // every hand-tuned spec keeps priority. Position still comes from the live tree
    // (read_iface_mount_origin accumulation); the cache supplies nothing but the linking.
    static constexpr bool kCachePanelMounts = true;
    if (kCachePanelMounts) {
        int mc = rtx::cache::PanelMountComp(gid);
        int cx = 0, cy = 0;
        if (mc > 0 && read_iface_mount_origin(h, main_data, mc, cx, cy)) {
            ox = cx; oy = cy;
            { std::lock_guard<std::mutex> lk(g_ifaceOffMu);
              auto ov = g_ifaceOff.find(gid);
              if (ov != g_ifaceOff.end()) { ox += ov->second.first; oy += ov->second.second; } }
            return true;
        }
    }
    return false;
}

// Re-anchor a movable window's varc origin to its LIVE rendered frame. The engine CLAMPS a
// window inside the viewport at screen edges while the drag varcs keep the UNCLAMPED drag
// point, so a window pushed into a corner drifts off its varc by the clamp delta -- every
// rect derived from the varc then misses by that delta. Group 1477 (the HUD root) hosts the
// rendered frames in ABSOLUTE screen coords: pick the node nearest (px,py) whose size fits
// [wLo..wHi]x[hLo..hHi] within a 120px anchor window (covers any realistic clamp), nearest
// first, smallest area on ties. False = no match; callers keep the varc origin (fail-safe).
static bool iface_live_frame_origin(HANDLE h, std::uint64_t gs, std::uint64_t ge,
                                    int px, int py, int wLo, int wHi, int hLo, int hHi,
                                    int& outX, int& outY, int* outW = nullptr, int* outH = nullptr) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    long long bestD = -1, bestArea = 0;
    // Unique-size census: a game-window RESIZE re-lays-out the window instantly while the
    // position varc lags by SECONDS, so the frame can land beyond any sane anchor tolerance
    // in practice. When exactly ONE distinct
    // size-matching frame exists on screen, adopt it at ANY distance; the anchor window only
    // arbitrates between multiple candidates (parked layout variants). Co-located render
    // layers (same rect, stacked nodes) count as ONE candidate.
    int anyCount = 0, anyX = 0, anyY = 0, anyW = 0, anyH = 0;
    std::function<void(std::uint64_t,int,int,int)> fwalk =
        [&](std::uint64_t node, int bx, int by, int depth) {
        if (depth > 14) return;
        int ax = bx + r32(node + 0x70), ay = by + r32(node + 0x74);
        int w2 = r32(node + 0x78), h2 = r32(node + 0x7c);
        if (w2 >= wLo && w2 <= wHi && h2 >= hLo && h2 <= hHi) {
            if (anyCount == 0) { anyCount = 1; anyX = ax; anyY = ay; anyW = w2; anyH = h2; }
            else if (std::abs(ax - anyX) > 4 || std::abs(ay - anyY) > 4) anyCount = 2;   // second DISTINCT position -> ambiguous
            if (std::abs(ax - px) <= 120 && std::abs(ay - py) <= 120) {
                long long dd = (long long)(ax - px) * (ax - px) + (long long)(ay - py) * (ay - py);
                long long area = (long long)w2 * h2;
                if (bestD < 0 || dd < bestD || (dd == bestD && area < bestArea)) {
                    bestD = dd; bestArea = area; outX = ax; outY = ay;
                    if (outW) *outW = w2; if (outH) *outH = h2;
                }
            }
        }
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3; ++k) {
            std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
            std::uint64_t ca = cs + 8, cb = ce + 8;
            if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
            for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                std::uint64_t ch = r64(c);
                if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                fwalk(ch, ax, ay, depth + 1);
            }
        }
    };
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap = r64(g + 8);
        if (ap <= 0x10000 || r32(ap) != 1477) continue;
        std::uint64_t ws = r64(ap + 0x20), we = r64(ap + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) fwalk(nd, 0, 0, 0);
        }
        break;
    }
    if (bestD < 0 && anyCount == 1) {   // sole size-match on screen -> adopt it at any distance (post-resize varc lag)
        outX = anyX; outY = anyY;
        if (outW) *outW = anyW; if (outH) *outH = anyH;
        return true;
    }
    return bestD >= 0;
}

// Named movable-panel table for the Interfaces-tab VISUALIZER (mirrors the companion kPanelGroups +
// the draggable-panel group names). Each panel's true screen rect = (var_x - off_left, var_y -
// off_top, var_w, var_h). W/H = 0 -> the panel has no size varc (origin-only).
struct PanelVizSpec { const char* name; int var_x; int var_y; int var_w; int var_h; int off_left; int off_top; };
static const PanelVizSpec kPanelViz[] = {
    { "Dialogue",          9102, 9103, 9104, 9105, 0, 0 },
    { "Bank pin / Tool",   9121, 9122, 0,    0,    0, 0 },
    { "Bank",              9596, 9597, 9598, 9599, 0, 0 },
    { "Quest",             10128,10129,10130,10131,0, 0 },
    { "Lodestone",         10394,10395,0,    0,    112,0 },
    { "DXP timer",         10071,10072,0,    0,    0, 0 },
    { "All chat",          8969, 8970, 8971, 8972, 0, 0 },
    { "Emote",             9292, 9293, 9294, 9295, 0, 0 },
    { "Notes",             9387, 9388, 9389, 9390, 0, 0 },
    { "Music",             9368, 9369, 9370, 9371, 0, 0 },
    { "Inventory",         8988, 8989, 8990, 8991, 0, 0 },
    { "Equipment",         9083, 9084, 9085, 9086, 0, 0 },
    { "Skills",            9311, 9312, 9313, 9314, 0, 0 },
    { "Achievement paths", 10318,10319,10320,10321,0, 0 },
    { "Activity tracker",  10166,10167,10168,10169,0, 0 },
    { "Minimap",           8931, 8932, 8933, 8934, 0, 0 },
    { "Friends chat list", 9539, 9540, 9541, 9542, 0, 0 },
    { "Private chat",      9026, 9027, 9028, 9029, 0, 0 },
    { "Friends list",      9406, 9407, 9408, 9409, 0, 0 },
    { "Friends chat",      9045, 9046, 9047, 9048, 0, 0 },
    { "Clan chat",         9064, 9065, 9066, 9067, 0, 0 },
    { "Clan chat list",    9425, 9426, 9427, 9428, 0, 0 },
    { "Guest clan chat",   9349, 9350, 9351, 9352, 0, 0 },
    { "Requests",          9653, 9654, 9655, 9656, 0, 0 },
    { "Group chat",        9748, 9749, 9750, 9751, 0, 0 },
    { "Group chat list",   9786, 9787, 9788, 9789, 0, 0 },
    { "Loot",              9900, 9901, 9902, 9903, 0, 0 },
    { "Buff bar",          9444, 9445, 9446, 9447, 0, 0 },
    { "Debuff bar",        10147,10148,10149,10150,0, 0 },
};

// Every panel whose position varc has resolved -> its computed true screen rect. The Interfaces tab
// lists these with visibility toggles and draws the toggled ones via the overlay.
//   [{"name":..,"x":..,"y":..,"w":..,"h":..}, ..]
std::string PanelRectsJson(std::uint32_t pid) {
    std::string out = "["; bool first = true;
    for (const auto& s : kPanelViz) {
        int X, Y;
        if (!read_companion_var(pid, 4, s.var_x, X) && !read_companion_var(pid, 5, s.var_x, X)) continue;
        if (!read_companion_var(pid, 4, s.var_y, Y) && !read_companion_var(pid, 5, s.var_y, Y)) continue;
        int W = 0, H = 0;
        if (s.var_w) { if (!read_companion_var(pid, 4, s.var_w, W)) W = 0; }
        if (s.var_h) { if (!read_companion_var(pid, 4, s.var_h, H)) H = 0; }
        int x = X - s.off_left, y = Y - s.off_top;
        if (!first) out += ","; first = false;
        out += "{\"name\":\""; out += s.name; out += "\",\"x\":" + std::to_string(x) +
               ",\"y\":" + std::to_string(y) + ",\"w\":" + std::to_string(W) + ",\"h\":" + std::to_string(H) + "}";
    }
    out += "]";
    return out;
}

// Cheap: just the group list (id + top-level widget count), NO recursion. The
// inspector loads this on open; per-group widgets are fetched on expand, so the
// whole tree (thousands of nodes) is never walked unless asked for.
//   {"groups":[{"id":G,"n":topLevelCount},..]}
std::string InterfaceGroupsJson(std::uint32_t pid) {
    const char* kEmpty = "{\"groups\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    std::string out = "{\"groups\":["; bool first = true;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000) continue;
        int gid = r32(ap2);
        if (gid <= 0 || gid > 70000) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        int n = (ws && we && we > ws + 8 && (we - ws) < 0x100000) ? (int)((we - ws) / 0x18) : 0;
        out += first ? "" : ","; first = false;
        out += "{\"id\":" + std::to_string(gid) + ",\"n\":" + std::to_string(n) + "}";
    }
    out += "]}";
    return out;
}

// Widgets (recursive tree) for ONE group, fetched on demand when expanded.
//   {"widgets":[{"g":G,"t":[id1,id2,id3],"d":depth,"r":[x,y,w,h]},..]}
// Compass-clue needle heading. Interface group 996 (the compass), component id 5 (the needle): its
// rotation is the int32 at node+0x180 (see project_runetoolsx_compass). Returns the raw rotation
// (0..~2092), or -1 if group 996 isn't open / the needle isn't found. The solver (client.html) converts
// it to a world bearing: bearing_from_north_CW = raw / 5.8127. Only the raw value is exposed; the
// elimination + calibration live in JS so they can be tuned without a rebuild.
int CompassHeadingValue(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return -1;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return -1;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return -1;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != 996) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) return -1;
        for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd <= 0x10000) continue;
            if (r16(nd + 0x2a) == 5)            // component id 5 = the needle
                return r32(nd + 0x180);          // its rotation
        }
        return -1;   // group open but needle component not found
    }
    return -1;       // group 996 not open
}

// Compass-clue dig tile straight from the engine: varc 1323 holds the target packed as
// (plane<<28)|(x<<14)|y (the same coord packing clue scrolls use), so the EXACT spot reads with no
// needle triangulation. Returns "x,y,plane", or "" when no compass clue is active (varc 0) or the value is
// out of range. The JS side still cross-checks the tile against the known compass-spot list, so a stale
// post-update offset (which would return garbage) is rejected there and the solver falls back to the needle.
std::string CompassTargetJson(std::uint32_t pid) {
    auto ps = snap_proc(pid); if (!ps) return "";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "";
    int packed = read_varc(h, *root, 1323);
    if (packed <= 0) return "";                                    // 0 = no compass-clue target set
    int x = (packed >> 14) & 0x3FFF, y = packed & 0x3FFF, z = (packed >> 28) & 0x3;
    if (x <= 0 || y <= 0 || x > 16383 || y > 16383) return "";
    char buf[48]; std::snprintf(buf, sizeof(buf), "%d,%d,%d", x, y, z);
    return buf;
}

// LIVE read of the scan orb's ring graphic from the scene-graphic REGISTRY, the second read
// path recorded when op83 was reverse-engineered: op83's handler stores its 0x2c-byte record
// at MainData+0x198b0 (+0x90 + slot*0x2c) with the coordinate converted to a FINE world
// position (0x100 + tile*512) as floats. Reading the registry beats reading the packet: the
// record PERSISTS for as long as the ring is drawn, while the packet only exists on the tick
// it arrived (so a 2.5s freshness window either misses it or goes stale mid-scan).
// The registry base may be the struct itself or a pointer to it (both forms are probed), and
// op83 is generic, so every plausible tile found is returned for the caller to validate
// against the clue's own candidate spots.
static void ScanRingTilesFromMemory(HANDLE h, std::uint64_t root, std::vector<std::pair<int,int>>& out) {
    constexpr std::uint64_t kOffRegistry = 0x198b0, kRecBase = 0x90, kRecSize = 0x2c, kSlots = 8;
    // A fine coordinate is 0x100 + tile*512, so a valid one is positive, under the world edge,
    // and lands on a tile centre.
    auto fine_to_tile = [](float f) -> int {
        if (!(f > 0.0f) || f > 16383.0f * 512.0f + 512.0f) return -1;
        double t = ((double)f - 256.0) / 512.0;
        int ti = (int)(t + 0.5);
        if (ti <= 0 || ti > 16383) return -1;
        if (t - (double)ti > 0.02 || (double)ti - t > 0.02) return -1;   // must sit ON a tile centre
        return ti;
    };
    std::uint64_t bases[2] = { root + kOffRegistry, 0 };
    if (auto pv = rpm<std::uint64_t>(h, root + kOffRegistry); pv && *pv > 0x10000) bases[1] = *pv;
    for (std::uint64_t bi = 0; bi < 2; ++bi) {
        if (!bases[bi]) continue;
        std::uint8_t blk[kRecSize * kSlots];
        if (!rpm_bytes(h, bases[bi] + kRecBase, blk, (int)sizeof(blk))) continue;
        for (std::uint64_t slot = 0; slot < kSlots; ++slot) {
            const std::uint8_t* rec = blk + slot * kRecSize;
            // Fine x and y sit in the record as floats; their exact offsets are not pinned, so
            // take the first aligned pair (allowing one field between them for height) that
            // decodes to two on-centre tiles.
            for (std::size_t o = 0; o + 8 <= kRecSize; o += 4) {
                float fx; std::memcpy(&fx, rec + o, 4);
                int tx = fine_to_tile(fx);
                if (tx < 0) continue;
                for (std::size_t g = 4; g <= 8 && o + g + 4 <= kRecSize; g += 4) {
                    float fy; std::memcpy(&fy, rec + o + g, 4);
                    int ty = fine_to_tile(fy);
                    if (ty < 0) continue;
                    bool dup = false;
                    for (auto& q : out) if (q.first == tx && q.second == ty) { dup = true; break; }
                    if (!dup) out.emplace_back(tx, ty);
                    break;
                }
            }
        }
    }
}

std::string ScanSolutionJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"ok\":false}";
    auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
    if (!root || *root <= 0x10000) return "{\"ok\":false}";
    std::vector<std::pair<int,int>> tiles;
    ScanRingTilesFromMemory(ps.h, *root, tiles);
    if (tiles.empty()) return "{\"ok\":false}";
    std::string js = "{\"ok\":true,\"src\":\"scene\",\"x\":" + std::to_string(tiles[0].first)
                   + ",\"y\":" + std::to_string(tiles[0].second) + ",\"cands\":[";
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (i) js += ',';
        js += '[' + std::to_string(tiles[i].first) + ',' + std::to_string(tiles[i].second) + ']';
    }
    js += "]}";
    return js;
}

// Entity under the mouse, from the engine's stable hover-target slot: input_proc = *(root+0x198E8),
// slot = *(input_proc+0x13F8) (the input drainer re-publishes the topmost cursor target's action_obj
// there each tick, so it never goes stale mid-read -- same slot the legacy RuneTools hover API used).
// The action_obj carries two EASTL strings -- target display name @+0x00 ("<col=..>Bank chest",
// ptr-or-SSO) and verb @+0x18 ("Use") -- plus a target reference @+0x48 whose meaning is
// per-category:
//   loc/scenery: +0x48 = loc id, +0x4C/+0x50 = world tile x/y INLINE (locs are not in the live
//                entity vector, so the slot itself is the only source -- and it's sufficient);
//   npc/player : +0x48 = scene uid (+0x4C/+0x50 zero) -> resolve against the live entity vector
//                for the config id (sec+0x1080), tile (fine floats /512) and plane.
// Target-less verbs (Walk here / Cancel / Continue) leave STALE bytes in the name/ref fields from
// the slot's previous occupant, so those return the verb alone.
std::string HoverEntityJson(std::uint32_t pid) {
    const char* kNone = "{\"ok\":false}";
    constexpr std::uint64_t kOffInputProc = 0x198E8, kHoverSlot = 0x13F8;
    auto ps = snap_proc(pid);
    if (!ps) return kNone;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kNone;
    auto ip = rpm<std::uint64_t>(h, *root + kOffInputProc);
    if (!ip || *ip <= 0x10000) return kNone;
    auto ao = rpm<std::uint64_t>(h, *ip + kHoverSlot);
    if (!ao || *ao <= 0x10000) return kNone;

    // EASTL string read that ACCEPTS the flag==0 form (a full 23-char inline SSO string).
    // read_eastl_string rejects flag==0 because in the varc map that byte pattern is
    // indistinguishable from a zeroed int node -- but the hover slot's fields are known
    // strings, and real 23-char names exist ("<col=ffff00>Lamp trader" is exactly 23).
    auto hover_str = [&](std::uint64_t sb, std::string& out2) -> bool {
        out2.clear();
        std::uint8_t rawb[0x18];
        if (!rpm_bytes(h, sb, rawb, sizeof(rawb))) return false;
        std::uint8_t flag = rawb[0x17];
        if (flag & 0x80) {                                    // heap: ptr @+0, size @+8
            std::uint64_t p = 0, sz = 0;
            std::memcpy(&p, rawb, 8); std::memcpy(&sz, rawb + 8, 8);
            if (p <= 0x10000 || p > 0x00007FFFFFFFFFFFull || sz == 0 || sz > 0x200) return false;
            std::string tmp((std::size_t)sz, '\0');
            if (!rpm_bytes(h, p, tmp.data(), (int)sz)) return false;
            out2.swap(tmp);
            return true;
        }
        if (flag > 0x17) return false;                        // not a live SSO string
        std::size_t len = (std::size_t)(0x17 - flag);         // flag 0 = full 23-char inline
        if (len == 0) return false;
        out2.assign((const char*)rawb, len);
        return true;
    };
    std::string verb;
    if (!hover_str(*ao + 0x18, verb) || verb.empty()) return kNone;
    for (char& c : verb) if ((unsigned char)c < 0x20) c = ' ';

    // Drop Jagex markup (<col=..>..</col>) and map the CP-1252 NBSP RS3 uses in names.
    // Both fields need this: ability-bar verbs carry colour tags too, e.g.
    // "Activate <col=FF00>Deflect Ranged</col>".
    auto strip_markup = [](const std::string& s) {
        std::string o; bool intag = false;
        for (unsigned char c : s) {
            if (c == '<') { intag = true; continue; }
            if (c == '>') { intag = false; continue; }
            if (intag) continue;
            if (c == 0xA0) o.push_back(' ');
            else if (c >= 0x20) o.push_back((char)c);
        }
        while (!o.empty() && o.back() == ' ') o.pop_back();
        return o;
    };
    verb = strip_markup(verb);
    if (verb.empty()) return kNone;
    std::string raw;
    hover_str(*ao + 0x00, raw);
    // The display name is always colour-tagged; anything else is slot-reuse garbage.
    std::string name = (raw.compare(0, 5, "<col=") == 0) ? strip_markup(raw) : std::string();
    std::string out = "{\"ok\":true,\"verb\":\"" + json_escape(verb) + "\"";

    int ref = rpm<std::int32_t>(h, *ao + 0x48).value_or(0);
    int tx  = rpm<std::int32_t>(h, *ao + 0x4C).value_or(0);
    int ty  = rpm<std::int32_t>(h, *ao + 0x50).value_or(0);
    char buf[96];
    // ITEM hover: +0x44 = item id (-1 on every other category, so it is the
    // discriminator), +0x4C = slot index, +0x50 = packed (container component u16,
    // interface u16) -- constant 5 / 1473 for the backpack, i.e. WHERE the grid is,
    // not which cell. Checked FIRST because a low slot index in +0x4C paired with the
    // component in +0x50 would otherwise satisfy the loc-tile test.
    int itemId = rpm<std::int32_t>(h, *ao + 0x44).value_or(-1);
    if (itemId >= 0) {
        int slot = rpm<std::int32_t>(h, *ao + 0x4C).value_or(-1);
        int comp = rpm<std::uint16_t>(h, *ao + 0x50).value_or(0);
        int ifid = rpm<std::uint16_t>(h, *ao + 0x52).value_or(0);
        std::snprintf(buf, sizeof(buf), ",\"kind\":\"item\",\"id\":%d,\"slot\":%d,\"iface\":%d,\"comp\":%d",
                      itemId, slot, ifid, comp);
        return out + buf + ",\"name\":\"" + json_escape(name) + "\"}";
    }
    if (tx > 0 && tx < 16384 && ty > 0 && ty < 16384) {          // loc: id + tile inline
        std::snprintf(buf, sizeof(buf), ",\"kind\":\"loc\",\"id\":%d,\"x\":%d,\"y\":%d", ref, tx, ty);
        return out + buf + ",\"name\":\"" + json_escape(name) + "\"}";
    }
    // INTERFACE-action hover: no item (+0x44 == -1), no tile, +0x40 == -1, and +0x50 =
    // packed (component u16, interface u16) with a second pair at +0x5C. Runs BEFORE
    // the target-less check: the name field holds STALE bytes from the slot's previous
    // occupant here, which the <col= filter blanks, and a blank name must not
    // short-circuit this branch. The name is never emitted for pure-interface hovers.
    {
        int comp  = rpm<std::uint16_t>(h, *ao + 0x50).value_or(0);
        int ifid  = rpm<std::uint16_t>(h, *ao + 0x52).value_or(0);
        if (ifid > 0 && ifid < 4096) {
            int comp2 = rpm<std::uint16_t>(h, *ao + 0x5C).value_or(0);
            std::snprintf(buf, sizeof(buf), ",\"kind\":\"iface\",\"iface\":%d,\"comp\":%d,\"comp2\":%d",
                          ifid, comp, comp2);
            return out + buf + "}";
        }
    }
    bool targetless = verb == "Walk here" || verb == "Cancel" || verb == "Continue" || name.empty();
    if (targetless) return out + "}";
    // NPC/player: resolve the scene uid in the live entity vector (same chain as PlayerInfoJson).
    if (ref > 0) {
        constexpr std::uint64_t kContainer = 0x19990, kActiveIdx = 0x70, kEntryArr = 0x58,
                                kEntryWv = 0x8, kVecBegin = 0x138, kVecEnd = 0x140,
                                kSecPtr = 0x1A0, kType = 0x10, kUid = 0x88,
                                kPosX = 0x270, kPosY = 0x278, kNpcCfg = 0x1080;
        auto cont = rpm<std::uint64_t>(h, *root + kContainer);
        auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + kActiveIdx) : std::nullopt;
        auto arr  = (cont && *cont > 0x10000) ? rpm<std::uint64_t>(h, *cont + kEntryArr) : std::nullopt;
        if (idx && *idx >= 0 && arr && *arr > 0x10000) {
            auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + kEntryWv);
            auto worker = (wv && *wv > 0x10000) ? scene_worker(h, pid, *wv, nullptr)
                                                : std::optional<std::uint64_t>{};
            auto vb = worker ? rpm<std::uint64_t>(h, *worker + kVecBegin) : std::nullopt;
            auto ve = worker ? rpm<std::uint64_t>(h, *worker + kVecEnd)   : std::nullopt;
            if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
                std::uint64_t n = (*ve - *vb) / 8; if (n > 20000) n = 20000;
                for (std::uint64_t i = 0; i < n; ++i) {
                    auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
                    if (!ep || *ep <= 0x10000) continue;
                    auto sec = rpm<std::uint64_t>(h, *ep + kSecPtr);
                    if (!sec || *sec <= 0x10000) continue;
                    if (rpm<std::int32_t>(h, *sec + kUid).value_or(0) != ref) continue;
                    int t = rpm<std::uint8_t>(h, *sec + kType).value_or(0xFF);
                    if (t != 1 && t != 2) continue;
                    float fx = rpm<float>(h, *sec + kPosX).value_or(0);
                    float fy = rpm<float>(h, *sec + kPosY).value_or(0);
                    int plane = rpm<std::int32_t>(h, *sec + 0x40).value_or(0);
                    if (plane < 0 || plane > 3) plane = 0;
                    int cfg = (t == 1) ? rpm<std::int32_t>(h, *sec + kNpcCfg).value_or(-1) : -1;
                    char enm[40] = {0};
                    rpm_bytes(h, *sec + 0xB8, enm, sizeof(enm) - 1);
                    std::string sname;
                    for (int j = 0; j < (int)sizeof(enm) && enm[j]; ++j) {
                        unsigned char c = (unsigned char)enm[j];
                        if (c >= 0x20 && c <= 0x7e) sname.push_back((char)c);
                        else if (c == 0xA0) sname.push_back(' ');
                    }
                    std::snprintf(buf, sizeof(buf),
                                  ",\"kind\":\"%s\",\"id\":%d,\"uid\":%d,\"x\":%d,\"y\":%d,\"p\":%d",
                                  t == 1 ? "npc" : "player", cfg, ref,
                                  (int)(fx / 512.f), (int)(fy / 512.f), plane);
                    return out + buf + ",\"name\":\"" + json_escape(sname.empty() ? name : sname) + "\"}";
                }
            }
        }
    }
    // Unresolvable reference (mid-despawn, or an unmapped category): still report what
    // the slot itself says.
    return out + ",\"name\":\"" + json_escape(name) + "\"}";
}

// Puzzle scroll box board, from interface 1931, component 18 (the live tile grid). Its 25 child cells each
// carry a "number" sprite (u16 @ +0x188): consecutive ids for the 24 tiles, 65535 for the gap. Returns the
// 25 raw sprites in row-major (sub) order as a JSON array, or "[]" if 1931 isn't open. The JS solver maps
// sprite -> tile id (tile = sprite - min) so it's robust to whatever the sprite base is.
std::string PuzzleStateJson(std::uint32_t pid) {
    const char* kEmpty = "[]";
    auto ps = snap_proc(pid); if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64  = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32  = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16u = [&](std::uint64_t a){ return (int)rpm<std::uint16_t>(h, a).value_or(0); };
    auto r16s = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    std::uint64_t ap2 = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t a = r64(g + 8);
        if (a > 0x10000 && r32(a) == 1931) { ap2 = a; break; }
    }
    if (!ap2) return kEmpty;
    auto gather = [&](std::uint64_t node, std::uint64_t* buf, int& n, int cap) {
        const std::uint64_t offs[3] = { 0x198, 0x180, 0x1c8 };
        for (int oi = 0; oi < 3; ++oi) {
            std::uint64_t cs = r64(node + offs[oi]), ce = r64(node + offs[oi] + 8);
            std::uint64_t a = cs + 8, b = ce + 8;
            if (!cs || !ce || a <= 0x10000 || b <= a || (b - a) > 0x100000) continue;
            for (std::uint64_t w = a; w + 0x18 <= b && n < cap; w += 0x18) {
                std::uint64_t c = r64(w);
                if (c > 0x10000) buf[n++] = c;
            }
        }
    };
    std::uint64_t stack[4096]; int sp = 0;
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
    for (std::uint64_t w = ws + 8; w + 0x18 <= we + 8 && sp < 4096; w += 0x18) { std::uint64_t v = r64(w); if (v > 0x10000) stack[sp++] = v; }
    std::uint64_t grid = 0; int guard = 0;
    while (sp > 0 && guard++ < 8000) {
        std::uint64_t n = stack[--sp];
        if (r16s(n + 0x2a) == 18 && r16s(n + 0x2c) == -1) { grid = n; break; }
        gather(n, stack, sp, 4096);
    }
    if (!grid) return kEmpty;
    std::uint64_t cells[64]; int cn = 0; gather(grid, cells, cn, 64);
    int board[25]; for (int i = 0; i < 25; i++) board[i] = -1;
    int filled = 0;
    for (int i = 0; i < cn; i++) {
        int s = r16s(cells[i] + 0x2c);
        if (s < 0 || s > 24 || board[s] != -1) continue;
        board[s] = r16u(cells[i] + 0x188);
        filled++;
    }
    if (filled < 25) return kEmpty;
    std::string out = "[";
    for (int i = 0; i < 25; i++) { if (i) out += ","; out += std::to_string(board[i]); }
    out += "]";
    return out;
}

// Absolute screen rects of the 25 puzzle-box cells (interface 1931 / comp 18 children), in sub (row-major)
// order, so the panel can highlight the next tile to click on the live interface. Returns
//   {"abs":1,"cells":[[x,y,w,h] | null, ... x25]}
// "abs":1 means the panel origin resolved so the rects are screen pixels; "abs":0 means group-relative (the
// caller should not draw). {"abs":0,"cells":[]} when 1931 isn't open. 1931's panel-origin entry
// (the central-interface-overlay varcs) is approximate; calibrate via the Interfaces tab.
std::string PuzzleCellRectsJson(std::uint32_t pid) {
    const char* kEmpty = "{\"abs\":0,\"cells\":[]}";
    auto ps = snap_proc(pid); if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64  = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32  = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16s = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    std::uint64_t ap2 = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) { std::uint64_t a = r64(g + 8); if (a > 0x10000 && r32(a) == 1931) { ap2 = a; break; } }
    if (!ap2) return kEmpty;
    int ox = 0, oy = 0;
    bool haveAbs = iface_panel_origin(h, *root, pid, 1931, ox, oy);
    // Find the comp-18 grid, accumulating the absolute screen origin down the tree (+0x70/74 per node).
    std::uint64_t grid = 0; int gx = 0, gy = 0;
    std::function<void(std::uint64_t,int,int,int)> find =
        [&](std::uint64_t node, int bx, int by, int depth) {
        if (grid || depth > 14) return;
        int ax = bx + r32(node + 0x70), ay = by + r32(node + 0x74);
        if (r16s(node + 0x2a) == 18 && r16s(node + 0x2c) == -1) { grid = node; gx = ax; gy = ay; return; }
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3 && !grid; ++k) {
            std::uint64_t cs = r64(node + co[k]), ce = r64(node + co[k] + 8), ca = cs + 8, cb = ce + 8;
            if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
            for (std::uint64_t c = ca; c + 0x18 <= cb && !grid; c += 0x18) {
                std::uint64_t ch = r64(c); if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                find(ch, ax, ay, depth + 1);
            }
        }
    };
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
    for (std::uint64_t w = ws + 8; w + 0x18 <= we + 8 && !grid; w += 0x18) { std::uint64_t nd = r64(w); if (nd > 0x10000) find(nd, ox, oy, 0); }
    if (!grid) return kEmpty;
    // Gather the grid's 25 direct children (the cells), placed by sub (= row-major grid index).
    int cx[25], cy[25], cw[25] = {0}, ch_[25];
    const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
    for (int k = 0; k < 3; ++k) {
        std::uint64_t cs = r64(grid + co[k]), ce = r64(grid + co[k] + 8), ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
        for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
            std::uint64_t cell = r64(c); if (cell <= 0x10000) continue;
            int s = r16s(cell + 0x2c); if (s < 0 || s > 24 || cw[s] > 0) continue;
            cx[s] = gx + r32(cell + 0x70); cy[s] = gy + r32(cell + 0x74);
            cw[s] = r32(cell + 0x78); ch_[s] = r32(cell + 0x7c);
        }
    }
    std::string out = "{\"abs\":"; out += haveAbs ? "1" : "0"; out += ",\"cells\":[";
    for (int i = 0; i < 25; i++) {
        if (i) out += ",";
        if (cw[i] > 0) out += "[" + std::to_string(cx[i]) + "," + std::to_string(cy[i]) + "," + std::to_string(cw[i]) + "," + std::to_string(ch_[i]) + "]";
        else out += "null";
    }
    out += "]}";
    return out;
}

// Screen rects of arbitrary components in any group, anchored to a 1477 central-interface MOUNT slot. Used by
// the celtic-knot solver to box the exact arrow to click. compsCsv = component ids; mountComp = the 1477 slot
// the group renders into (knot = 728). Returns {"abs":1,"comps":{"<id>":[x,y,w,h],..}} ("abs":0 = origin
// unresolved -> don't draw). Walks the group tree accumulating +0x70/74 per node.
std::string IfaceCompRectsJson(std::uint32_t pid, int group, const std::string& compsCsv, int mountComp) {
    const char* kEmpty = "{\"abs\":0,\"comps\":{}}";
    auto ps = snap_proc(pid); if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    std::vector<int> want;
    { int id = 0; bool any = false; for (char ch : compsCsv) { if (ch >= '0' && ch <= '9') { id = id * 10 + (ch - '0'); any = true; } else if (any) { want.push_back(id); id = 0; any = false; } } if (any) want.push_back(id); }
    if (want.empty()) return kEmpty;
    auto r64  = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32  = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16s = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    std::uint64_t ap2 = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) { std::uint64_t a = r64(g + 8); if (a > 0x10000 && r32(a) == group) { ap2 = a; break; } }
    if (!ap2) return kEmpty;
    int ox = 0, oy = 0;
    // Prefer the group's kPanelOrigins entry (varc 6310/6311 for knots, with mount fallback baked in); only if
    // the group isn't registered there, use the caller-supplied mount slot directly.
    bool haveAbs = iface_panel_origin(h, *root, pid, group, ox, oy);
    if (!haveAbs && mountComp > 0) haveAbs = read_iface_mount_origin(h, *root, mountComp, ox, oy);
    std::vector<int> seen; std::string comps;
    std::function<void(std::uint64_t,int,int,int)> walk =
        [&](std::uint64_t node, int bx, int by, int depth) {
        if (depth > 16) return;
        int ax = bx + r32(node + 0x70), ay = by + r32(node + 0x74);
        int comp = r16s(node + 0x2a);
        if (r16s(node + 0x2c) == -1 && std::find(want.begin(), want.end(), comp) != want.end()
            && std::find(seen.begin(), seen.end(), comp) == seen.end()) {
            seen.push_back(comp);
            comps += (comps.empty() ? "" : ",");
            // ax/ay already include the origin (the walk is seeded with ox,oy below) -- do NOT add ox/oy again.
            comps += "\"" + std::to_string(comp) + "\":[" + std::to_string(ax) + "," + std::to_string(ay)
                   + "," + std::to_string(r32(node + 0x78)) + "," + std::to_string(r32(node + 0x7c)) + "]";
        }
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3; ++k) {
            std::uint64_t cs = r64(node + co[k]), ce = r64(node + co[k] + 8), ca = cs + 8, cb = ce + 8;
            if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
            for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                std::uint64_t ch = r64(c); if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                walk(ch, ax, ay, depth + 1);
            }
        }
    };
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
    for (std::uint64_t w = ws + 8; w + 0x18 <= we + 8; w += 0x18) { std::uint64_t nd = r64(w); if (nd > 0x10000) walk(nd, ox, oy, 0); }
    std::string out = "{\"abs\":"; out += haveAbs ? "1" : "0"; out += ",\"comps\":{" + comps + "}}";
    return out;
}

std::string InterfaceGroupJson(std::uint32_t pid, int groupId) {
    const char* kEmpty = "{\"widgets\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    // Absolute screen origin for movable panels (dialogues etc.): seed the walk
    // so emitted widgets carry "a":[absX,absY]. Falls back to relative when the
    // group has no panel spec or its position varcs aren't published.
    int ox = 0, oy = 0;
    bool haveAbs = iface_panel_origin(h, *root, pid, groupId, ox, oy);
    std::string out = "{\"widgets\":["; int count = 0; bool first = true;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != groupId) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        for (std::uint64_t w = a; w + 0x18 <= b && count < 4000; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd <= 0x10000) continue;
            iface_walk(h, groupId, nd, 0, out, count, first, ox, oy, haveAbs);
        }
        break;   // only the matching group
    }
    out += "]}";
    return out;
}

// Search EVERY open group's widget tree for components of a given size (w x h, within +-tol). Lets the
// Interfaces tab pair a panel to its game-frame mount slot by size (e.g. the donation window 656's
// 512x352 root <-> group 1477's 512x352 central-interface slots). Walks all groups once, on demand.
//   {"matches":[{"g":G,"c":comp,"s":sub,"w":w,"h":h,"ax":absX,"ay":absY},..]}  (ax/ay only when the
//   group's origin resolves, so the row can highlight in-game).
std::string InterfaceSizeSearchJson(std::uint32_t pid, int tw, int th, int tol) {
    const char* kEmpty = "{\"matches\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    if (tw <= 0 || th <= 0) return kEmpty;
    if (tol < 0) tol = 0; if (tol > 256) tol = 256;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    auto adiff = [](int x, int y){ return x > y ? x - y : y - x; };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    std::string out = "{\"matches\":["; bool first = true; int total = 0, matched = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge && matched < 600; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000) continue;
        int gid = r32(ap2);
        if (gid <= 0 || gid > 70000) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) continue;
        int ox = 0, oy = 0;
        bool haveAbs = iface_panel_origin(h, *root, pid, gid, ox, oy);
        std::function<void(std::uint64_t,int,int,int)> walk =
            [&](std::uint64_t node, int bx, int by, int depth) {
            if (depth > 14 || total > 120000 || matched >= 600) return;
            ++total;
            int x = r32(node+0x70), y = r32(node+0x74), w = r32(node+0x78), hh = r32(node+0x7c);
            int ax = bx + x, ay = by + y;
            if (adiff(w, tw) <= tol && adiff(hh, th) <= tol) {
                out += first ? "" : ","; first = false;
                out += "{\"g\":" + std::to_string(gid) + ",\"c\":" + std::to_string(r16(node+0x2a))
                     + ",\"s\":" + std::to_string(r16(node+0x2c)) + ",\"w\":" + std::to_string(w)
                     + ",\"h\":" + std::to_string(hh);
                if (haveAbs) out += ",\"ax\":" + std::to_string(ax) + ",\"ay\":" + std::to_string(ay);
                out += "}";
                ++matched;
            }
            const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
            for (int k = 0; k < 3; ++k) {
                std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
                std::uint64_t ca = cs + 8, cb = ce + 8;
                if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                for (std::uint64_t c = ca; c + 0x18 <= cb && matched < 600; c += 0x18) {
                    std::uint64_t ch = r64(c);
                    if (ch <= 0x10000) continue;
                    std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;
                    walk(ch, ax, ay, depth + 1);
                }
            }
        };
        for (std::uint64_t wn = a; wn + 0x18 <= b && matched < 600; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd > 0x10000) walk(nd, ox, oy, 0);
        }
    }
    out += "]}";
    return out;
}

// Live dialogue state for the quest auto-highlight: which dialogue group is open
// (1188 option-select / 1184 NPC / 1191 player / 1186 server) and, for the option
// box, each option's exact text + ABSOLUTE screen rect (panel-position varc +
// within-group offset). The UI matches the focused quest step's expected chat
// option against these and highlights the right box. {} when no dialogue is open.
//   {"group":1188,"options":[{"n":1,"comp":6,"text":"..","x":..,"y":..,"w":..,"h":..},..],
//    "header":"..","headerComp":3}
std::string DialogJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return "{}";

    // option-select comps: comp 3 = header/question; static option texts
    // at comps 6/33/35/37/39 (+0x178 type tag 01). But a quest can ADD an option at ANOTHER
    // comp dynamically (e.g. a "(Continue Quest)" choice), so options are captured by SHAPE
    // (any visible non-header text) below -- a fixed comp whitelist drops the dynamic one.
    // Stale text lingers in unused comps, so the consumer must match the EXPECTED option text.
    const int kGroup = 1188;   // option-select is the one the quest highlight uses
    int ox = 0, oy = 0;
    bool haveAbs = iface_panel_origin(h, *root, pid, kGroup, ox, oy);

    std::function<void(std::uint64_t,int,int,int)> walk;
    std::string opts; bool firstOpt = true; std::string header; int headerComp = -1; int optN = 0;
    int found = 0;
    auto isNumText = [](const std::string& t) {   // text made only of digits / '.' / ' ' (e.g. "1." or "38.")
        if (t.empty() || t.size() > 4) return false;
        for (char c : t) if (!(c >= '0' && c <= '9') && c != '.' && c != ' ') return false;
        return true;
    };
    walk = [&](std::uint64_t node, int bx, int by, int depth) {
        if (depth > 16 || found > 600) return;   // a dynamically-added option can sit past many graphic layers -> generous caps
        int comp = r16(node + 0x2a);
        int tag  = r32(node + 0x178);
        int x = r32(node + 0x70), y = r32(node + 0x74), w = r32(node + 0x78), hh = r32(node + 0x7c);
        int ax = bx + x, ay = by + y;
        ++found;
        std::string txt = iface_sso_text(h, node);
        if (comp == 3 && tag == 2 && !txt.empty() && header.empty()) { header = txt; headerComp = comp; }
        // An option is any VISIBLE (w>0) non-empty text node that isn't the header -- captures the static
        // slots AND a dynamically-added option (e.g. "(Continue Quest)") at a non-standard comp.
        // The "1."/"2." option-index labels are ALSO numeric text, so they can't be dropped by content
        // alone (Dron's Making History quiz has numeric ANSWERS like "38."/"36."). They differ by WIDTH:
        // an index label is narrow (~25px), a real option row is full width (~97px). So skip a numeric
        // node only when it's narrow -- a wide numeric node ("38.") is a genuine answer.
        else if (!txt.empty() && w > 0 && !(comp == 3 && tag == 2) && !(isNumText(txt) && w < 48)) {
            opts += firstOpt ? "" : ","; firstOpt = false; ++optN;
            opts += "{\"n\":" + std::to_string(optN) + ",\"comp\":" + std::to_string(comp) +
                    ",\"tag\":" + std::to_string(tag) + ",\"text\":\"" + txt + "\"";
            if (haveAbs) opts += ",\"x\":" + std::to_string(ax) + ",\"y\":" + std::to_string(ay) +
                                 ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(hh);
            opts += "}";
        }
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3; ++k) {
            std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
            std::uint64_t ca = cs + 8, cb = ce + 8;
            if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
            for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                std::uint64_t ch = r64(c);
                if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                walk(ch, ax, ay, depth + 1);
            }
        }
    };
    bool open = false;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != kGroup) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        open = true;
        // Re-anchor to the live rendered frame (same size as the group root): the drag varc
        // keeps the unclamped point when the window is pushed against a screen edge. ONLY for
        // varc-positioned specs -- mount/viewport-anchored groups (var_x == 0, e.g. the 1512
        // build sidebar) must NOT re-anchor: the same-size search hijacks their origin to any
        // matching frame elsewhere (1512's 800x600 canvas matched the empty central window
        // slot and dragged the box onto it).
        bool varcPositioned = false;
        for (const auto& s : kPanelOrigins) if (s.group == kGroup) { varcPositioned = s.var_x != 0; break; }
        if (haveAbs && varcPositioned) {
            std::uint64_t c0 = r64(a);
            int rw = (c0 > 0x10000) ? r32(c0 + 0x78) : 0, rh = (c0 > 0x10000) ? r32(c0 + 0x7c) : 0;
            int fx = 0, fy = 0;
            if (rw > 0 && rh > 0 && iface_live_frame_origin(h, gs, ge, ox, oy, rw - 2, rw + 2, rh - 2, rh + 2, fx, fy)) { ox = fx; oy = fy; }
        }
        for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) walk(nd, ox, oy, 0);
        }
        break;
    }
    if (!open) return "{}";
    std::string out = "{\"group\":" + std::to_string(kGroup);
    if (!header.empty()) out += ",\"header\":\"" + header + "\",\"headerComp\":" + std::to_string(headerComp);
    out += ",\"hasAbs\":" + std::string(haveAbs ? "true" : "false");
    out += ",\"options\":[" + opts + "]}";
    return out;
}

// Read specific COMPONENTS of an open interface GROUP: for each requested comp id, its live text and
// absolute screen rect (when the group's movable-panel origin is known). A general building block for
// plugins that track a panel. The NPC chat box is group 1184: comp 4 = NPC name, comp 10 = message,
// 11 = the VISIBLE continue button -- highlight THIS one. NB comp
// 15 is the CS2 space-key target (script7800) but lives in a layout variant at the wrong absolute
// position, so it is NOT the rect to draw. compsCsv is a comma-separated id list (e.g. "4,10,11").
//   {"group":G,"open":bool,"hasAbs":bool,"comps":[{"comp":C,"text":"..","x":..,"y":..,"w":..,"h":..},..]}
std::string InterfaceCompsJson(std::uint32_t pid, int group, const std::string& compsCsv) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return "{}";
    std::unordered_set<int> want;
    { int v = 0; bool any = false;
      for (char c : compsCsv) { if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); any = true; }
                                else if (any) { want.insert(v); v = 0; any = false; } }
      if (any) want.insert(v); }
    if (want.empty() || want.size() > 64) return "{}";
    int ox = 0, oy = 0;
    bool haveAbs = iface_panel_origin(h, *root, pid, group, ox, oy);

    std::string comps; bool firstC = true; bool open = false; int found = 0;
    std::function<void(std::uint64_t,int,int,int)> walk;
    walk = [&](std::uint64_t node, int bx, int by, int depth) {
        if (depth > 12 || found > 4000) return;
        ++found;
        int comp = r16(node + 0x2a);
        int x = r32(node + 0x70), y = r32(node + 0x74), w = r32(node + 0x78), hh = r32(node + 0x7c);
        int vflags = r32(node + 0x50);   // raw visibility/state flags (e.g. bit 0x1800 = panel hidden on swap-in-place dialogs)
        int ax = bx + x, ay = by + y;
        if (want.count(comp)) {
            std::string txt = iface_text(h, node);          // +0x90 display text (labels, progress counters)
            if (txt.empty()) txt = iface_sso_text(h, node); // +0x180 SSO (dialogue messages/options) -- matches the inspector
            // +0x188 u64 graphic field: small values = classic cache sprite id ("spr"); bit-62-
            // flagged values = new-style OBJ-icon cells (POH grids 1512 comp 16 / 1518 comp 19):
            // 0x4000000000000000 | flavour<<24 | itemId in the low 24 bits, +0x1a0 duplicating
            // the plain id. "obj" = that unpacked item id so callers
            // can match text-less grid cells by item (e.g. 61880 crude wooden chair).
            std::uint64_t sprRaw = rpm<std::uint64_t>(h, node + 0x188).value_or(0);
            int sprv = (sprRaw > 0 && sprRaw < 0x100000) ? (int)sprRaw : 0;
            int objv = ((sprRaw >> 62) == 1 && (sprRaw & 0xFFFFFF) < 200000) ? (int)(sprRaw & 0xFFFFFF) : 0;
            int subv = r16(node + 0x2c);   // entry index within a templated grid (e.g. :261...) -- lets callers pair sibling layers (icon comp 16 <-> cell bg comp 15)
            comps += firstC ? "" : ","; firstC = false;
            comps += "{\"comp\":" + std::to_string(comp) + ",\"sub\":" + std::to_string(subv) +
                     ",\"text\":\"" + json_escape(txt) + "\"" +
                     ",\"vis\":" + std::to_string(vflags) + ",\"spr\":" + std::to_string(sprv) +
                     ",\"obj\":" + std::to_string(objv);
            if (haveAbs) comps += ",\"x\":" + std::to_string(ax) + ",\"y\":" + std::to_string(ay) +
                                  ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(hh);
            comps += "}";
        }
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3; ++k) {
            std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
            std::uint64_t ca = cs + 8, cb = ce + 8;
            if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
            for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                std::uint64_t ch = r64(c);
                if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                walk(ch, ax, ay, depth + 1);
            }
        }
    };
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != group) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        open = true;
        // Re-anchor to the live rendered frame (same size as the group root): the drag varc
        // keeps the unclamped point when the window is pushed against a screen edge. ONLY for
        // varc-positioned specs -- mount/viewport-anchored groups (var_x == 0, e.g. the 1512
        // build sidebar) must NOT re-anchor: the same-size search hijacks their origin to any
        // matching frame elsewhere (1512's 800x600 canvas matched the empty central window
        // slot and dragged the box onto it).
        bool varcPositioned = false;
        for (const auto& s : kPanelOrigins) if (s.group == group) { varcPositioned = s.var_x != 0; break; }
        if (haveAbs && varcPositioned) {
            std::uint64_t c0 = r64(a);
            int rw = (c0 > 0x10000) ? r32(c0 + 0x78) : 0, rh = (c0 > 0x10000) ? r32(c0 + 0x7c) : 0;
            int fx = 0, fy = 0;
            if (rw > 0 && rh > 0 && iface_live_frame_origin(h, gs, ge, ox, oy, rw - 2, rw + 2, rh - 2, rh + 2, fx, fy)) { ox = fx; oy = fy; }
        }
        for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) walk(nd, ox, oy, 0);
        }
        break;
    }
    return "{\"group\":" + std::to_string(group) + ",\"open\":" + (open ? "true" : "false") +
           ",\"hasAbs\":" + (haveAbs ? "true" : "false") + ",\"comps\":[" + comps + "]}";
}

// Live inventory slot rect -- positioned the SAME way as dialogue options: walk group 1473's widget
// tree, accumulate each widget's within-group offset, and add the panel-origin varc (3040/3041; size 8990/8991). NO
// hardcoded grid: the slot CELLS are found by size (the repeated ~40x36 backpack cells), co-located
// render layers are deduped, and the cells are sorted into reading order. Returns the requested
// slot's true screen rect, so it tracks the live (responsive) layout automatically.
//   {"x":..,"y":..,"w":..,"h":..} or {} if the backpack isn't open / panel varc unresolved / index OOB.
std::string InvSlotRectJson(std::uint32_t pid, int slotIndex) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return "{}";
    const int kGroup = 1473;
    int ox = 0, oy = 0;
    bool originOk = iface_panel_origin(h, *root, pid, kGroup, ox, oy);
    if (!originOk) return "{}";   // panel position varc not resolved yet
    int px = ox, py = oy;   // outer panel frame origin (position varc via iface_panel_origin), before the chrome inset below; re-anchored to the live 1477 frame when found

    // Visible content viewport (absolute screen rect). The interface tree carries slot cells from
    // INACTIVE layout variants (positioned off to the side) and scrolled-out rows, so without this
    // clip the Nth cell in reading order can be one that isn't actually on screen -- the box then
    // floats outside the panel. Resolved from the panel size below; stays unset (no clip) if the
    // size vars aren't available, so it never regresses to drawing nothing.
    int vpx0 = 0, vpy0 = 0, vpx1 = 0, vpy1 = 0; bool haveVp = false;

    struct Cell { int x, y, w, h; std::uint64_t parent; std::uint64_t node; };
    std::vector<Cell> cells;
    std::function<void(std::uint64_t,std::uint64_t,int,int,int)> walk;
    walk = [&](std::uint64_t node, std::uint64_t parent, int bx, int by, int depth) {
        if (depth > 12 || cells.size() > 4000) return;
        int x = r32(node + 0x70), y = r32(node + 0x74), w = r32(node + 0x78), hh = r32(node + 0x7c);
        int ax = bx + x, ay = by + y;
        if (w >= 28 && w <= 60 && hh >= 26 && hh <= 52) cells.push_back({ ax, ay, w, hh, parent, node }); // a backpack cell (size-gated; tolerates UI scale)
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3; ++k) {
            std::uint64_t cs = r64(node+co[k]), ce = r64(node+co[k]+8);
            std::uint64_t ca = cs + 8, cb = ce + 8;
            if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
            for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                std::uint64_t ch = r64(c);
                if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                walk(ch, node, ax, ay, depth + 1);   // `node` is the parent of `ch`
            }
        }
    };
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != kGroup) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        // CHROME INSET: the panel origin (ox,oy / varc 3040/3041; size 8990/8991) is the OUTER panel frame; the group content
        // (the first top-level widget = group root) is inset inside the frame's border + top header bar.
        // Within-group coords are relative to the content, so shift the origin by the chrome. Derived
        // live from sizes (no hardcoded constant): border = (panelW - contentW)/2, header = remaining
        // vertical minus the matching bottom border.
        std::uint64_t c0 = r64(a);
        int contentW = (c0 > 0x10000) ? r32(c0 + 0x78) : 0;
        int contentH = (c0 > 0x10000) ? r32(c0 + 0x7c) : 0;
        // CHROME INSET (frame border + title bar) between the window origin (varc 3040/3041) and the grid.
        // Read the WINDOW frame size LIVE from group 1477's root widget (the movable window that holds the
        // grid). The tree is scale-aware and present whenever the backpack is open -- unlike the panel-size
        // varcs (8990/8991), which are absent at the never-dragged default and left the inset at 0 (box
        // parked in the title). Header = (windowH - contentH) - border, so the slim-headers setting is
        // handled automatically by the live content height (grid 212x333 full / 212x353 slim, window 220x401).
        int panelW = 0, panelH = 0;
        // The frame that OWNS the grid: the group-1477 node nearest the varc, strictly larger
        // than the content (border + title bar; Classic mode's fixed dock slot matches too).
        // Its LIVE position is adopted as the true origin -- the varc is only the search hint
        // (stale by the clamp delta when the window sits against a screen edge).
        int frameX = 0, frameY = 0;
        if (contentW > 0 && contentH > 0 &&
            iface_live_frame_origin(h, gs, ge, px, py, contentW + 1, contentW + 119,
                                    contentH + 1, contentH + 299, frameX, frameY, &panelW, &panelH)) {
            ox = frameX; oy = frameY; px = frameX; py = frameY;
        }
        // No anchored frame -> fall back to group 1477's first root widget (the modern
        // movable window), sanity-gated.
        if (panelW == 0) for (std::uint64_t g2 = gs; g2 + 0x10 <= ge; g2 += 0x10) {
            std::uint64_t ap = r64(g2 + 8);
            if (ap <= 0x10000 || r32(ap) != 1477) continue;
            std::uint64_t ws2 = r64(ap + 0x20), a2 = ws2 + 8;
            if (ws2 > 0x10000 && a2 > 0x10000) {
                std::uint64_t w0 = r64(a2);
                if (w0 > 0x10000) {
                    int pw = r32(w0 + 0x78), ph = r32(w0 + 0x7c);
                    // Sanity: the frame is only slightly bigger than the grid (border + title bar). Reject a
                    // garbage read (e.g. a huge value that would push the header off-screen -> no box, and
                    // would break the viewport clip below) so it falls back to the varbit inset instead.
                    if (contentW > 0 && contentH > 0 && pw > contentW && pw < contentW + 120 && ph > contentH && ph < contentH + 300) { panelW = pw; panelH = ph; }
                }
            }
            break;
        }
        int border, header;
        if (panelW > 0 && panelH > 0) {
            border = (panelW - contentW) / 2;
            header = (panelH - contentH) - border;
        } else {
            // Frame size not read -> derive from the "slim headers" varbit 19924 (varp 3814, bit 0): the
            // grid mounts at (x 4, y 44) slim / (x 4, y 64) full at 100% UI scale.
            border = 4; header = (read_varp(h, *root, 3814) & 1) ? 44 : 64;
        }
        ox += border; oy += header;
        // Visible region: prefer the content area (excludes the title bar + borders); fall back to
        // the outer panel frame. c0's size is the VIEWPORT (the scrollable content is a child), so a
        // row scrolled out of view falls outside this rect and is correctly treated as not visible.
        if (contentW > 0 && contentH > 0)      { vpx0 = ox; vpy0 = oy; vpx1 = ox + contentW; vpy1 = oy + contentH; haveVp = true; }
        else if (panelW > 0 && panelH > 0) { vpx0 = px; vpy0 = py; vpx1 = px + panelW; vpy1 = py + panelH; haveVp = true; }
        // The visible area can never exceed the outer panel frame, even if the scrollable content
        // child reports a height taller than the panel -- clamp so scrolled-past rows are excluded.
        if (haveVp && panelW > 0 && panelH > 0) {
            if (vpx0 < px) vpx0 = px;            if (vpy0 < py) vpy0 = py;
            if (vpx1 > px + panelW) vpx1 = px + panelW;  if (vpy1 > py + panelH) vpy1 = py + panelH;
        }
        for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) walk(nd, c0, ox, oy, 0);   // c0 (group root) is the parent of each top-level widget
        }
        break;
    }
    std::size_t rawCells = cells.size();
    auto visible = [&](const Cell& c) {
        if (!haveVp) return true;
        int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        return cx >= vpx0 && cx <= vpx1 && cy >= vpy0 && cy <= vpy1;
    };
    // Identify the active slot CONTAINER: the parent component holding the most VISIBLE slot-sized
    // children. The real inventory slots are that container's direct children; stray chrome (scrollbar/
    // arrows) sits under other parents, and an inactive layout variant's container is off-screen (0
    // visible cells) -- so voting by visible children picks the right one.
    std::unordered_map<std::uint64_t,int> votes;
    for (const auto& c : cells) if (visible(c)) votes[c.parent]++;
    std::uint64_t cont = 0; int best = 0;
    for (const auto& kv : votes) if (kv.second > best) { best = kv.second; cont = kv.first; }

    // The container's slot children: dedupe co-located render layers, then sort row-major (== slot
    // order). One entry per slot, no cross-layout/chrome contamination, so slots[N] is slot N+1.
    std::vector<Cell> slots;
    for (const auto& c : cells) if (c.parent == cont) {
        bool dup = false;
        for (const auto& u : slots) if (std::abs(u.x - c.x) < 18 && std::abs(u.y - c.y) < 16) { dup = true; break; }
        if (!dup) slots.push_back(c);
    }
    std::sort(slots.begin(), slots.end(), [](const Cell& a, const Cell& b){ return a.y != b.y ? a.y < b.y : a.x < b.x; });
    // Diagnostic (throttled 1s): the chosen container plus a flag probe of the top competing
    // containers. The interface holds overlapping slot-layout variants (only one is rendered),
    // so a geometric vote can pick a hidden one; the probe dumps candidate ints per container
    // node to identify which offset marks the rendered layout.
    { static ULONGLONG t = 0; ULONGLONG n = GetTickCount64(); if (n - t >= 1000) { t = n;
        char hdr[200];
        std::snprintf(hdr, sizeof(hdr), "INV raw=%zu cont=%04llx slots=%zu pick=%d vp=[%d,%d,%d,%d] org=%d,%d",
            rawCells, (unsigned long long)(cont & 0xffff), slots.size(), slotIndex, vpx0, vpy0, vpx1, vpy1, px, py);
        rtx::log::Client(pid, hdr);
        std::unordered_map<std::uint64_t, std::vector<const Cell*>> byp;
        for (const auto& c : cells) byp[c.parent].push_back(&c);
        std::vector<std::pair<std::uint64_t,std::size_t>> order;
        for (auto& kv : byp) order.push_back({ kv.first, kv.second.size() });
        std::sort(order.begin(), order.end(), [](const std::pair<std::uint64_t,std::size_t>& a, const std::pair<std::uint64_t,std::size_t>& b){ return a.second > b.second; });
        static const int kOff[] = { 0x40,0x44,0x48,0x4c,0x50,0x54,0x68,0x6c,0x80,0x84,0x88,0x8c,0x90,0xa0,0xb0,0xb8 };
        for (std::size_t pi = 0; pi < order.size() && pi < 4; ++pi) {
            std::uint64_t pn = order[pi].first;
            const Cell* rep = byp[pn].front();
            int vis = 0; for (auto* c : byp[pn]) if (visible(*c)) ++vis;
            char line[460];
            int o = std::snprintf(line, sizeof(line), "  P=%04llx n=%zu vis=%d cell@%d,%d pflags:",
                (unsigned long long)(pn & 0xffff), order[pi].second, vis, rep->x, rep->y);
            for (int oi = 0; oi < (int)(sizeof(kOff)/sizeof(kOff[0])) && o < (int)sizeof(line) - 24; ++oi)
                o += std::snprintf(line + o, sizeof(line) - o, " %x=%d", kOff[oi], r32(pn + kOff[oi]));
            rtx::log::Client(pid, line);
        }
        std::string d = "  slots:";
        for (std::size_t i = 0; i < slots.size() && i < 40; ++i) { char b[40]; std::snprintf(b, sizeof(b), " [%zu:%d,%d]", i, slots[i].x, slots[i].y); d += b; }
        rtx::log::Client(pid, d); } }
    if (slotIndex < 0 || slotIndex >= (int)slots.size()) return "{}";   // fewer slots than asked -> not present
    const Cell& s = slots[slotIndex];
    if (!visible(s)) return "{}";   // slot exists but is scrolled out of / off the visible viewport -> don't project
    return "{\"x\":" + std::to_string(s.x) + ",\"y\":" + std::to_string(s.y) +
           ",\"w\":" + std::to_string(s.w) + ",\"h\":" + std::to_string(s.h) + ",\"n\":" + std::to_string((int)slots.size()) + "}";
}

// Chat log lines from the chatbox interface (group 137). Each visible line is a
// component-86 widget whose message text is the char* at +0x180 (legacy
// I_itemids3); it already embeds the colored "[HH:MM:SS]" timestamp and the RS3
// markup (<col=..>, <img=N>, names). The RAW message strings are returned (markup
// intact, UTF-8-safe via iface_text_at, including the 0xC2 0xA0 spaces RS3 uses in
// names) in chatbox order (newest first), each with its base text colour (the
// per-channel colour </col> resets to). The UI parses the time, renders the
// colours, and accumulates a deduped searchable log. Lines are read live from the
// widget tree. Empty when the chatbox isn't open / not in-world.
//   {"lines":[{"raw":"<raw markup>","base":RRGGBB},..]}
std::string ChatJson(std::uint32_t pid) {
    const char* kEmpty = "{\"lines\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::uint16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;

    std::uint64_t top = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 > 0x10000 && r32(ap2) == 137) {
            std::uint64_t ws = r64(ap2 + 0x20);
            top = ws > 0x10000 ? r64(ws + 8) : 0;
            break;
        }
    }
    if (!top) return kEmpty;

    auto kids = [&](std::uint64_t node, std::vector<std::uint64_t>& dst) {
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int kk = 0; kk < 3; ++kk) {
            std::uint64_t cs = r64(node + co[kk]), ce = r64(node + co[kk] + 8);
            std::uint64_t a = cs + 8, b = ce + 8;
            if (!cs || !ce || a <= 0x10000 || b <= a || (b - a) > 0x100000) continue;
            for (std::uint64_t c = a; c + 0x18 <= b && dst.size() < 4000; c += 0x18) {
                std::uint64_t ch = r64(c);
                if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;
                dst.push_back(ch);
            }
        }
    };
    // DFS collecting comp-86 line widgets, in tree order. Each line carries its
    // RAW markup (+0x180) AND its base text colour (u24 RGB at +0x80): RS3's
    // </col> resets to this per-channel base (white for system lines, the channel
    // colour for clan/public/etc.), NOT to white -- so the UI must know it to
    // colour message bodies that have no inline <col=..>.
    std::string out = "{\"lines\":["; bool first = true; int emitted = 0;
    std::vector<std::pair<std::uint64_t, int>> stk{ { top, 0 } };
    int guard = 0;
    while (!stk.empty() && guard++ < 20000 && emitted < 500) {
        auto cur = stk.back(); stk.pop_back();
        if (cur.first <= 0x10000 || cur.second > 12) continue;
        if (r16(cur.first + 0x2a) == 86) {
            std::string msg = iface_text_at(h, cur.first, 0x180, 480);
            if (!msg.empty()) {
                std::uint32_t basecol = (std::uint32_t)r32(cur.first + 0x80) & 0xFFFFFF;
                // +0x90 display text = the sender's name on player messages, empty
                // on system/game lines -- the UI uses it (with the [CC]/[FC]/[GC]
                // labels in the text) to attribute the channel.
                std::string nm = iface_text_at(h, cur.first, 0x90, 96);
                out += first ? "" : ","; first = false;
                out += "{\"raw\":\"" + msg + "\",\"base\":" + std::to_string(basecol) +
                       ",\"name\":\"" + nm + "\"}";
                ++emitted;
            }
        }
        std::vector<std::uint64_t> cs; kids(cur.first, cs);
        for (auto c : cs) stk.push_back({ c, cur.second + 1 });
    }
    out += "]}";
    return out;
}

// Parse a buff-bar timer string to seconds. RS3 shows bare seconds ("58"),
// "m:ss" / "h:mm:ss", or an "Nh"/"Nm" suffix for long buffs.
static int parse_buff_secs(const std::string& t) {
    if (t.empty()) return 0;
    if (t.find(':') != std::string::npos) {
        int parts[3] = { 0, 0, 0 }, np = 0, cur = 0; bool any = false;
        for (char c : t) {
            if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
            else if (c == ':') { if (np < 3) parts[np++] = cur; cur = 0; }
        }
        if (np < 3) parts[np++] = cur;
        if (np == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
        if (np == 2) return parts[0] * 60 + parts[1];
        return any ? parts[0] : 0;
    }
    int n = 0; bool any = false;
    for (char c : t) if (c >= '0' && c <= '9') { n = n * 10 + (c - '0'); any = true; }
    if (!any) return 0;
    if (t.find('h') != std::string::npos) return n * 3600;
    if (t.find('m') != std::string::npos) return n * 60;
    return n;   // bare seconds (or trailing 's')
}

// Active buffs (group 284) + debuffs (group 291) from the buff-bar widgets.
std::string BuffsJson(std::uint32_t pid) {
    const char* kEmpty = "{\"buffs\":[],\"debuffs\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::uint16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;

    auto kids = [&](std::uint64_t node, std::vector<std::uint64_t>& dst) {
        const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
        for (int k = 0; k < 3; ++k) {
            std::uint64_t cs = r64(node + co[k]), ce = r64(node + co[k] + 8);
            std::uint64_t a = cs + 8, b = ce + 8;
            if (!cs || !ce || a <= 0x10000 || b <= a || (b - a) > 0x100000) continue;
            for (std::uint64_t c = a; c + 0x18 <= b && dst.size() < 256; c += 0x18) {
                std::uint64_t ch = r64(c);
                if (ch <= 0x10000) continue;
                std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                if (d <= 0x3000) continue;            // child in a separate alloc
                dst.push_back(ch);
            }
        }
    };
    auto group_top = [&](int gid) -> std::uint64_t {
        for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
            std::uint64_t ap2 = r64(g + 8);
            if (ap2 <= 0x10000 || r32(ap2) != gid) continue;
            std::uint64_t ws = r64(ap2 + 0x20);
            return ws > 0x10000 ? r64(ws + 8) : 0;
        }
        return 0;
    };
    // Descendant whose component id (+0x2a) == want (bounded DFS).
    auto find_comp = [&](std::uint64_t top, int want) -> std::uint64_t {
        std::vector<std::pair<std::uint64_t, int>> stk{ { top, 0 } };
        int guard = 0;
        while (!stk.empty() && guard++ < 4000) {
            auto cur = stk.back(); stk.pop_back();
            if (cur.first <= 0x10000 || cur.second > 4) continue;
            std::vector<std::uint64_t> cs; kids(cur.first, cs);
            for (auto c : cs) if (r16(c + 0x2a) == want) return c;
            for (auto c : cs) stk.push_back({ c, cur.second + 1 });
        }
        return 0;
    };

    auto bar_json = [&](int gid, int containerComp, bool debuff) -> std::string {
        std::uint64_t top = group_top(gid);
        if (!top) return "";
        std::uint64_t cont = find_comp(top, containerComp);
        if (!cont) return "";
        std::vector<std::uint64_t> slots; kids(cont, slots);
        std::string out; bool first = true;
        std::vector<unsigned> seen;   // dedup icon ids within this bar (see below)
        for (auto slot : slots) {
            std::uint64_t slotArr = r64(slot + 0x1c8);
            if (slotArr <= 0x10000) continue;
            std::uint64_t icon = r64(slotArr + 0x8), textw = r64(slotArr + 0x20);
            if (icon <= 0x10000) continue;
            int sprite = r16(icon + 0x188);
            int item   = r32(icon + 0x1a0);
            std::string timer;
            if (textw > 0x10000) {
                char tb[8] = {};
                if (rpm_bytes(h, textw + 0x180, tb, sizeof(tb) - 1)) {
                    for (int i = 0; i < (int)sizeof(tb) - 1 && tb[i]; ++i) {
                        unsigned char c = (unsigned char)tb[i];
                        if (c < 0x20 || c > 0x7e) break;
                        timer += (char)c;
                    }
                }
            }
            bool itemBased = (item > 0 && item < 200000);
            bool spriteOk  = (sprite > 0 && sprite < 0xFFFF);
            // The buff bar stores the icon id at the sprite offset even when that
            // id is actually an ITEM (e.g. Bone Shield = 30099). The struct cache
            // knows which: reclassify so item-typed icons render from the item
            // pack instead of failing to resolve as a sprite archive.
            if (!itemBased && spriteOk && rtx::cache::GetBuffIconIsItem(sprite)) {
                item = sprite; sprite = 0; itemBased = true; spriteOk = false;
            }
            if (!itemBased && !spriteOk) continue;            // no real icon -> not a buff
            // EXPIRED-BUFF PHANTOM FILTER. The buff bar is a fixed pool of 50 slot widgets; when a buff
            // ends the engine TEARS DOWN its slot (slot+0xC reverts 9->1) and RESETS its icon widget --
            // but the slot->icon pointer is left dangling, so the stale icon still reads a valid
            // sprite/item (and even a leftover timer). Neither the slot rect, icon+0x50, the timer text
            // nor the resolved name can tell a live buff from a just-expired one. The reliable signal is
            // the icon's content count at icon+0x8: a genuine on-screen buff reads 1, a reset/torn-down
            // icon reads 0 (its content list is emptied -- icon+0x18 also nulls and the vtable swaps to
            // a blank graphic).
            if (r32(icon + 0x8) == 0) continue;
            // Secondary visible-state guard for other pooled-leftover kinds (stale UI sprites, "Sell"
            // labels): a shown icon has icon+0x50 & 0x01010000 == 0x01010000.
            if ((((std::uint32_t)r32(icon + 0x50)) & 0x01010000u) != 0x01010000u) continue;
            int id = itemBased ? item : sprite;
            // Dedup: the buff bar reuses a pool of slot widgets, so while it
            // reshuffles (e.g. refreshing an overhead prayer) the same icon can
            // transiently pass the visible-state filter in TWO slots -> a momentary
            // double (Deflect Magic/Ranged). Each active buff has a unique icon, so
            // drop an id already emitted in this bar.
            unsigned dkey = itemBased ? (0x80000000u | (unsigned)item) : (unsigned)sprite;
            bool dup = false; for (unsigned k : seen) if (k == dkey) { dup = true; break; }
            if (dup) continue; seen.push_back(dkey);
            std::string name = debuff ? rtx::cache::GetDebuffName(id)
                                      : rtx::cache::GetBuffName(id);
            if (name.empty() && itemBased) name = rtx::cache::ItemName(item);
            // Phantom guard: a slot that resolves to NO name AND carries no
            // timer/count text is a pooled leftover that slipped past the
            // visible-state filter (e.g. the spurious "sprite 18290"). Real
            // buffs always resolve a name or carry timer/count text.
            if (name.empty() && timer.empty()) continue;
            // Kind tells whether the +0x180 text is a countdown timer vs a
            // static count/percentage. secs is only
            // meaningful for timers (or when kind is unknown).
            int knd = rtx::cache::GetBuffKind(id);
            const char* kindStr = (knd & 1) ? "timer" : (knd & 4) ? "pct" : (knd & 2) ? "count" : "";
            int sc = (kindStr[0] == '\0' || (knd & 1)) ? parse_buff_secs(timer) : 0;
            out += first ? "" : ","; first = false;
            out += "{\"sprite\":" + std::to_string(itemBased ? 0 : sprite) +
                   ",\"item\":"   + std::to_string(itemBased ? item : 0) +
                   ",\"name\":\"" + json_escape(name) + "\"" +
                   ",\"timer\":\"" + json_escape(timer) + "\"" +
                   ",\"kind\":\"" + std::string(kindStr) + "\"" +
                   ",\"secs\":"   + std::to_string(sc) + "}";
        }
        return out;
    };

    std::string out = "{\"buffs\":[";
    out += bar_json(284, 18, false);
    out += "],\"debuffs\":[";
    out += bar_json(291, 1, true);
    out += "]}";
    return out;
}

// Walk the 5 action bars (1430 main + 1670-1673) collecting (ability/item id ->
// label). Each named slot's label is at *(node+0x90) (with <col> tags) and its
// id at +0x188, which is the SAME id space as the cooldown-registry key
// (e.g. Surge = 14233). Used to name active cooldowns; only called
// when something is on cooldown (the full bar walk isn't free).
static void collect_ability_names(HANDLE h, std::uint64_t root,
                                  std::vector<std::pair<int, std::string>>& roster) {
    std::uint64_t gs, ge; iface_groups_range(h, root, gs, ge);
    if (!gs) return;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::uint16_t>(h, a).value_or(0); };
    const int bars[5] = { 1430, 1670, 1671, 1672, 1673 };
    for (int bi = 0; bi < 5; ++bi) {
        for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
            std::uint64_t ap2 = r64(g + 8);
            if (ap2 <= 0x10000 || r32(ap2) != bars[bi]) continue;
            std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
            std::uint64_t a = ws + 8, b = we + 8;
            if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
            std::vector<std::uint64_t> stack;
            for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
                std::uint64_t nd = r64(w);
                if (nd > 0x10000) stack.push_back(nd);
            }
            int guard = 0;
            while (!stack.empty() && guard < 8000) {
                ++guard;
                std::uint64_t node = stack.back(); stack.pop_back();
                int id = r16(node + 0x188);
                if (id > 0 && id < 0xFFFF) {
                    std::string nm = iface_text(h, node);   // *(node+0x90), escaped UTF-8
                    if (!nm.empty()) {
                        std::string clean; bool intag = false;   // strip <col=..>/</col> tags
                        for (char ch : nm) {
                            if (ch == '<') intag = true;
                            else if (ch == '>') intag = false;
                            else if (!intag) clean += ch;
                        }
                        if (!clean.empty()) {
                            bool seen = false;
                            for (auto& pr : roster) if (pr.first == id) { seen = true; break; }
                            if (!seen) roster.push_back({ id, clean });
                        }
                    }
                }
                const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
                for (int kk = 0; kk < 3; ++kk) {
                    std::uint64_t cs = r64(node + co[kk]), ce = r64(node + co[kk] + 8);
                    std::uint64_t ca = cs + 8, cb = ce + 8;
                    if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                    for (std::uint64_t c = ca; c + 0x18 <= cb && stack.size() < 6000; c += 0x18) {
                        std::uint64_t ch = r64(c);
                        if (ch <= 0x10000) continue;
                        std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                        if (d <= 0x3000) continue;
                        stack.push_back(ch);
                    }
                }
            }
            break;
        }
    }
}

// One bound action-bar ability: id, optional item id (item slots store the item at +0x1a0,
// abilities leave it 0), name, bound hotkey + modifier, whether it is currently castable
// (the engine's enabled byte) and the REAL per-ability cooldown text the game prints on the icon
// (`cd`, exact seconds, empty when ready -- see box_keybind). The GCD swirl is not read;
// the printed `cd` is the only cooldown surfaced.
struct AbarSlot { int id; int item; std::string name; std::string key; int mod; int en;
                  std::string cd; };

// Sanity guard on the cooldown widget's text (the widget itself is located structurally, by
// component id): digits with an optional M:SS colon ("44", "1:23"), a sub-10s decimal ("4.8"),
// or a minutes form ("2m", "2m 15s"). Rejects pooled leftovers that aren't a timer.
static bool is_cd_timer(const std::string& t) {
    if (t.empty()) return false;
    bool digit = false;
    for (char c : t) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c != ':' && c != '.' && c != 'm' && c != 's' && c != ' ') return false;
    }
    return digit;
}

// Read a widget's INLINE label at +0x180 (legacy I_itemids3: short text stored in-place, NOT a
// pointer -- the keybind label / cooldown number live here). Empty if it isn't printable text.
static std::string iface_inline(HANDLE h, std::uint64_t node) {
    char buf[16] = {};
    if (!rpm_bytes(h, node + 0x180, buf, sizeof(buf) - 1)) return {};
    std::string s;
    for (int i = 0; i < 15 && buf[i]; ++i) { unsigned char c = (unsigned char)buf[i]; if (c < 0x20 || c >= 0x7f) return {}; s += (char)c; }
    return s;
}

// Read the slot box's keybind label AND the REAL per-ability cooldown text. Each slot is a
// 13-component block whose box has direct children at FIXED component offsets
// (groups 1430 and 1673):
//   box+4  = ability name node, box+5 = GCD swirl sprite,
//   box+11 = KEYBIND inline text, box+12 = COOLDOWN inline text (+0x180 in-place).
// The cooldown text is the exact seconds the game prints on the icon ("59", "1:23"; empty
// when ready) -- a real per-ability timer, distinct from the swirl that spins on every cast.
// Match children by component id (u16 @ +0x2a) relative to the box, NOT by text shape, so a
// keybind '5' is never confused with a cooldown '5'. Do NOT filter on +0x188==0: live
// cooldown widgets carry junk there (observed 24936/28265/8308/70) -- that filter is why the
// old read missed cooldowns. mod: 0 none, 1 shift, 2 ctrl, 3 alt.
static std::string box_keybind(HANDLE h, std::uint64_t box, int& mod, std::string& cd) {
    mod = 0; cd.clear();
    std::string keyb;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto alnum = [](char c){ return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    int boxComp = (int)rpm<std::uint16_t>(h, box + 0x2a).value_or(0xFFFF);
    if (boxComp == 0xFFFF) return keyb;
    const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
    for (int kk = 0; kk < 3; ++kk) {
        std::uint64_t cs = r64(box + co[kk]), ce = r64(box + co[kk] + 8);
        std::uint64_t ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
        for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
            std::uint64_t ch = r64(c);
            if (ch <= 0x10000) continue;
            std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
            if (d <= 0x3000) continue;
            if (rpm<std::uint16_t>(h, ch + 0x2c).value_or(0) != 0xFFFF) continue;   // direct component, not a sub-array entry
            int rel = (int)rpm<std::uint16_t>(h, ch + 0x2a).value_or(0) - boxComp;
            if (rel == 11 && keyb.empty()) {                       // keybind label
                std::string t = iface_inline(h, ch);
                if (t.size() == 1 && alnum(t[0])) keyb = t;
                else if (t.size() >= 2 && t.size() <= 3 && alnum(t.back())) {   // <a/c/s>-<key>
                    char m = (char)(t[0] | 0x20);
                    int mm = (m == 's') ? 1 : (m == 'c') ? 2 : (m == 'a') ? 3 : 0;
                    if (mm) { mod = mm; keyb = std::string(1, t.back()); }
                }
            } else if (rel == 12 && cd.empty()) {                  // per-ability cooldown text
                std::string t = iface_inline(h, ch);
                if (is_cd_timer(t)) cd = t;
            }
        }
    }
    return keyb;
}


// Depth-first collect every bound ability under `node`. A bound cell carries the ability NAME
// as display text (*(node+0x90)) plus a real ability id (u16 @ +0x188). The slot's box (the name
// node's grandparent) holds the keybind + printed cooldown text, read via box_keybind. Empty /
// icon-only nodes (no letters) are skipped; deduped.
static void abar_collect(HANDLE h, std::uint64_t node, std::uint64_t parent, std::uint64_t gp,
                         int depth, std::vector<AbarSlot>& dst) {
    if (depth > 14 || dst.size() >= 64) return;
    int id = (int)rpm<std::uint16_t>(h, node + 0x188).value_or(0);
    if (id > 0 && id < 0xFFFF) {
        std::string nm = iface_text(h, node);             // *(node+0x90), escaped UTF-8
        if (!nm.empty()) {
            std::string clean; bool intag = false;        // strip <col=..>/</col> tags
            for (char ch : nm) { if (ch == '<') intag = true; else if (ch == '>') intag = false; else if (!intag) clean += ch; }
            bool letter = false;
            for (char ch : clean) if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) { letter = true; break; }
            if (letter) {
                bool seen = false;
                for (auto& s : dst) if (s.id == id) { seen = true; break; }
                if (!seen) {
                    int mod = 0; std::string cd; std::string key = gp ? box_keybind(h, gp, mod, cd) : std::string();
                    int item = rpm<std::int32_t>(h, node + 0x1a0).value_or(0); if (item < 0 || item > 200000) item = 0;   // item slots store the item here
                    // I_AbilityEnabled (+0x80): 255 = fully lit/castable; lower values (e.g. 51)
                    // = greyed. Export the RAW byte so the UI can show it (provenance) and so the
                    // castable threshold can be tuned without re-reversing if other values appear.
                    int en = (int)rpm<std::uint8_t>(h, node + 0x80).value_or(0);
                    dst.push_back({ id, item, clean, key, mod, en, cd });
                }
            }
        }
    }
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    const std::uint64_t co[3] = { 0x198, 0x180, 0x1c8 };
    for (int kk = 0; kk < 3; ++kk) {
        std::uint64_t cs = r64(node + co[kk]), ce = r64(node + co[kk] + 8);
        std::uint64_t ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
        for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
            std::uint64_t ch = r64(c);
            if (ch <= 0x10000) continue;
            std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
            if (d <= 0x3000) continue;
            abar_collect(h, ch, node, parent, depth + 1, dst);   // child: parent = node, grandparent = this node's parent
        }
    }
}

// Bound abilities on every action bar: main (group 1430) + secondaries (1670..1673). The UI
// gates the secondaries on varbits 29138..29141 (>0 = visible; value = displayed preset). Each
// slot's live state is read straight from the interface tree (build-stable, matches what the
// game draws): "cd" = the REAL per-ability cooldown text the game prints on the icon (exact
// seconds / M:SS, empty when ready -- the box+12 component, see box_keybind); "castable" = the
// engine's enabled byte (greyed/uncastable when false). Pure read. mod: 0/1/2/3.
//   {"clock":N,"bars":[{"bar":0,"group":1430,"slots":[{"slot":N,"id":K,..,"cd":"","castable":true},..]},..]}
std::string ActionBarJson(std::uint32_t pid) {
    const char* kEmpty = "{\"clock\":0,\"bars\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto rootv = rpm<std::uint64_t>(h, ps.mgva);
    if (!rootv || *rootv <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *rootv, gs, ge);
    if (!gs) return kEmpty;
    // Engine wall-clock in milliseconds (ticks +1000/sec), emitted as "clock".
    long long clock = (long long)(ps.mod_base
        ? rpm<std::uint64_t>(h, ps.mod_base + 0xED2FF8).value_or(0) : 0);
    const int bars[5] = { 1430, 1670, 1671, 1672, 1673 };
    std::string out = "{\"clock\":" + std::to_string(clock) + ",\"bars\":["; bool firstBar = true;
    for (int bi = 0; bi < 5; ++bi) {
        for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
            std::uint64_t ap2 = r64(g + 8);
            if (ap2 <= 0x10000 || r32(ap2) != bars[bi]) continue;
            std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
            std::uint64_t a = ws + 8, b = we + 8;
            if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
            std::vector<AbarSlot> abis;
            for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
                std::uint64_t nd = r64(w);
                if (nd > 0x10000) abar_collect(h, nd, 0, 0, 0, abis);
            }
            out += firstBar ? "" : ","; firstBar = false;
            out += "{\"bar\":" + std::to_string(bi) + ",\"group\":" + std::to_string(bars[bi]) + ",\"slots\":[";
            for (size_t i = 0; i < abis.size(); ++i) {
                out += i ? "," : "";
                out += "{\"slot\":" + std::to_string(i + 1) + ",\"id\":" + std::to_string(abis[i].id) +
                       ",\"item\":" + std::to_string(abis[i].item) +
                       ",\"name\":\"" + abis[i].name + "\",\"key\":\"" + abis[i].key + "\",\"mod\":" + std::to_string(abis[i].mod) +
                       ",\"castable\":" + (abis[i].en == 255 ? "true" : "false") +
                       ",\"en\":" + std::to_string(abis[i].en) + ",\"cd\":\"" + abis[i].cd + "\"}";
            }
            out += "]}";
            break;
        }
    }
    out += "]}";
    return out;
}

// Cooldown hashmap at root+0x19F78 with layout (buckets ptr @mgr+0x38178, cap @+0x38180;
// node {key i32@0, expiry i64@+8 in CLIENTCLOCK ms = qword at base+0xED2FF8, flag u8@+0x10,
// next@+0x18}). It does NOT hold ability cooldowns -- it stays empty through continuous
// casting. There is no externally-joinable ability_id->expiry registry; the printed
// per-ability cooldown text on each action-bar slot (see box_keybind) is the cooldown
// source. This walk is kept for diagnostics only and is NOT wired into the UI.
//   {"clock":N,"cooldowns":[{"id":K,"remaining":MS,"flag":F},..]}
std::string AbilityCooldownsJson(std::uint32_t pid) {
    const char* kEmpty = "{\"cooldowns\":[]}";
    auto ps = snap_proc(pid);
    if (!ps || !ps.mod_base)
        return kEmpty;
    HANDLE h = ps.h;
    auto rootv = rpm<std::uint64_t>(h, ps.mgva);
    if (!rootv || *rootv <= 0x10000) return kEmpty;
    std::uint64_t mgr     = *rootv + 0x19f78;
    std::uint64_t clock   = rpm<std::uint64_t>(h, ps.mod_base + 0xED2FF8).value_or(0);
    std::uint64_t buckets = rpm<std::uint64_t>(h, mgr + 0x38178).value_or(0);
    std::uint32_t cap     = rpm<std::uint32_t>(h, mgr + 0x38180).value_or(0);
    if (buckets <= 0x10000 || cap == 0 || cap > 100000) return kEmpty;

    struct CD { int id; long long rem; int flag; };
    std::vector<CD> cds;
    for (std::uint32_t i = 0; i < cap && cds.size() < 256; ++i) {
        std::uint64_t node = rpm<std::uint64_t>(h, buckets + (std::uint64_t)i * 8).value_or(0);
        int guard = 0;
        while (node > 0x10000 && guard < 64) {
            int           key    = rpm<std::int32_t>(h, node).value_or(0);
            std::uint64_t expiry = rpm<std::uint64_t>(h, node + 0x08).value_or(0);
            int           flag   = (int)rpm<std::uint8_t>(h, node + 0x10).value_or(0);
            long long     rem    = (long long)expiry - (long long)clock;
            if (key > 0 && rem > 0) cds.push_back({ key, rem, flag });
            node = rpm<std::uint64_t>(h, node + 0x18).value_or(0);
            ++guard;
        }
    }

    // Resolve names from the action bars only when something is on cooldown
    // (ability_id +0x188 == the registry key, so this join is exact).
    std::vector<std::pair<int, std::string>> roster;
    if (!cds.empty()) collect_ability_names(h, *rootv, roster);

    std::string out = "{\"clock\":" + std::to_string((long long)clock) + ",\"cooldowns\":[";
    bool first = true;
    for (const auto& c : cds) {
        out += first ? "" : ","; first = false;
        out += "{\"id\":" + std::to_string(c.id) +
               ",\"remaining\":" + std::to_string(c.rem) +
               ",\"flag\":" + std::to_string(c.flag);
        for (const auto& pr : roster)
            if (pr.first == c.id && !pr.second.empty()) { out += ",\"name\":\"" + pr.second + "\""; break; }
        out += "}";
    }
    out += "]}";
    return out;
}

bool BuildOverlayFrame(std::uint32_t pid, bool want_players, bool want_npcs,
                       bool want_objects, bool want_specials, int grid_radius, bool interactable,
                       const std::vector<std::string>& highlight_names,
                       const std::vector<int>& outline_uids,
                       const std::vector<GuideSite>& guide_sites,
                       OverlayFrame& out) {
    // All from SceneOffsets.h -- shared with the companion's SceneData.cpp walk.
    constexpr std::uint64_t kContainer = rtx::scn::kContainer, kActiveIdx = rtx::scn::kActiveIdx,
                            kEntryArr = rtx::scn::kEntryArr, kEntryWv = rtx::scn::kEntryWv,
                            kVecBegin = rtx::scn::kVecBegin, kVecEnd = rtx::scn::kVecEnd,
                            kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType,
                            kName = rtx::scn::kName, kUid = rtx::scn::kUid,
                            kConfig = rtx::scn::kConfig, kPosX = rtx::scn::kPosX,
                            kPosZ = rtx::scn::kPosZ, kPosY = rtx::scn::kPosY,
                            kPlayerData = rtx::scn::kPlayerData, kLocalUid = rtx::scn::kLocalUid;

    // Same off-lock optimization as SceneJson: dup the client's handle under a brief g_mu hold, then
    // build the entire overlay frame WITHOUT the lock. This runs every overlay frame, so holding
    // g_mu through its dense-scene walk stutters the 60 fps present and starves the tick and
    // every other reader. The dup outlives a concurrent SampleAll close (the only closer), so
    // there is no use-after-close. DupGuard closes the dup on every return path.
    HANDLE h = nullptr; std::uint64_t mgva = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end() && it->second.proc && it->second.main_global_va &&
            DuplicateHandle(GetCurrentProcess(), it->second.proc, GetCurrentProcess(),
                            &h, 0, FALSE, DUPLICATE_SAME_ACCESS))
            mgva = it->second.main_global_va;
    }
    struct DupGuard { HANDLE h; ~DupGuard() { if (h) CloseHandle(h); } } _dup{ h };
    if (!h || !mgva) return false;
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off)
        -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto root = rpm<std::uint64_t>(h, mgva);
    auto pdata = deref(root, kPlayerData);
    int local_uid = (pdata && *pdata > 0x10000)
                      ? rpm<std::int32_t>(h, *pdata + kLocalUid).value_or(-1) : -1;
    auto cont = deref(root, kContainer);
    auto idx  = (cont && *cont > 0x10000)
                  ? rpm<std::int32_t>(h, *cont + kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, kEntryArr);
    if (!idx || *idx < 0 || !arr || *arr <= 0x10000) return false;
    auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + kEntryWv);
    if (!wv || *wv <= 0x10000) return false;

    // Resolve the worker FIRST: its walk collects live entity positions that the
    // matrix rescan uses as tie-breaks (and as the pass-2 fallback if the camera
    // block layout ever drifts).
    CamProbes mprobes;
    auto worker = scene_worker(h, pid, *wv, &mprobes);
    if (!worker) return false;
    std::uint64_t rootv = (root && *root > 0x10000) ? *root : 0;
    if (!read_view_matrix(h, pid, rootv, *wv, mprobes, out.matrix)) return false;

    // Gameview viewport rect (the camera renders into this sub-rect, not the
    // whole client). The rect only changes on resize/layout, so cache it and
    // re-read at most ~2x/sec rather than walking the group list every overlay
    // frame. Left at 0 when unresolved -> the overlay falls back to the window.
    {
        static std::uint32_t s_pid = 0; static unsigned long long s_ms = 0;
        static int s_x = 0, s_y = 0, s_w = 0, s_h = 0;
        unsigned long long nowms = GetTickCount64();
        if (s_pid != pid || nowms - s_ms > 500) {
            int gx, gy, gw, gh;
            if (root && read_gameview_rect(h, *root, gx, gy, gw, gh)) { s_x = gx; s_y = gy; s_w = gw; s_h = gh; }
            else { s_w = 0; s_h = 0; }
            s_pid = pid; s_ms = nowms;
        }
        out.gv_x = s_x; out.gv_y = s_y; out.gv_w = s_w; out.gv_h = s_h;
    }

    auto vb = deref(worker, kVecBegin);
    auto ve = deref(worker, kVecEnd);
    bool have_player = false;
    std::unordered_set<int> regions;
    // Highlight entries: "needle", "needle|label", or "needle|label|tx,ty". The needle matches NPC
    // names (lowercased substring); the optional label replaces the NPC name on the drawn pill; the
    // optional tile is the WANTED instance's location -- when several NPCs share the name, the one
    // nearest THIS tile is boxed instead of the one nearest the player.
    // A '*' prefix on the needle means match-ALL: box EVERY live NPC it matches (e.g.
    // "*mysterious shade|Kill" boxes each shade), not just the best one. The tile part
    // accepts "x;y[;r]" as well as the legacy "x,y" -- the overlayHighlight bridge is
    // comma-separated, so only the ';' form survives it -- and the optional r bounds a
    // match-ALL needle to candidates within r tiles (Chebyshev) of the anchor tile
    // (e.g. shades belong to the monolith within +-6 of it, not ones a room over).
    struct HiNeedle { std::string needle, label; int tx = -1, ty = -1; int id = -1; bool all = false; int rad = -1; };
    std::vector<HiNeedle> hlow;
    for (const auto& s : highlight_names) {
        std::string t = s, lbl; int wtx = -1, wty = -1, wid = -1, wrad = -1; bool wall = false;
        auto bar = t.find('|');
        if (bar != std::string::npos) { lbl = t.substr(bar + 1); t = t.substr(0, bar); }
        auto bar2 = lbl.find('|');
        if (bar2 != std::string::npos) {
            std::string tile = lbl.substr(bar2 + 1); lbl = lbl.substr(0, bar2);
            auto sep = tile.find(',');                              // legacy "x,y"
            if (sep == std::string::npos) sep = tile.find(';');     // "x;y[;r]" (csv-safe)
            if (sep != std::string::npos) {
                wtx = std::atoi(tile.substr(0, sep).c_str());
                std::string rest = tile.substr(sep + 1);
                wty = std::atoi(rest.c_str());
                auto sep2 = rest.find(';');
                if (sep2 != std::string::npos) wrad = std::atoi(rest.substr(sep2 + 1).c_str());
            }
        }
        if (!t.empty() && t[0] == '*') { wall = true; t.erase(0, 1); }
        if (t.size() > 1 && t[0] == '#') wid = std::atoi(t.c_str() + 1);   // "#<id>" -> match the live NPC model id, not the name (disambiguates same-named NPCs)
        for (auto& ch : t) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        if (!t.empty()) hlow.push_back({ t, lbl, wtx, wty, wid, wall, wrad });
    }
    // Exactly ONE NPC is boxed per needle so a same-named but non-active morph variant never draws
    // a phantom second box. The ACTIVE entity is the one with a live in-memory name (mem=true) --
    // exactly how SceneJson decides what to show; an INACTIVE morph carries no in-memory name, so
    // the highlight only matches it via the static cache (mem=false). Preference: mem, then
    // interactable, then nearest. Candidates are resolved after the walk, once the tile is known.
    std::uint64_t root_raw = (root && *root > 0x10000) ? *root : 0;   // for live varbit-aware NPC morphs
    struct HiCand { OverlayPoint hp; int tx, ty, ndl; bool inter, mem; };
    std::vector<HiCand> hcands;
    if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
        std::uint64_t n = (*ve - *vb) / 8;
        if (n > 20000) n = 20000;
        for (std::uint64_t i = 0; i < n; ++i) {
            auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
            if (!ep || *ep <= 0x10000) continue;
            auto sec = rpm<std::uint64_t>(h, *ep + kSecPtr);
            if (!sec || *sec <= 0x10000) continue;
            int type = rpm<std::uint8_t>(h, *sec + kType).value_or(0xff);
            if (type == 4) {                          // type-4 (Time Sprite / Rockertunity)
                if (!want_specials) continue;
                auto ex = rpm<float>(h, *ep + 0x30);  // type-4 position is on the entity
                auto ez = rpm<float>(h, *ep + 0x34);  // height (fine z)
                auto ey = rpm<float>(h, *ep + 0x38);
                if (!ex || !ey) continue;
                if ((int)(*ex / 512.f) <= 0 || (int)(*ey / 512.f) <= 0) continue;
                int gfx = rpm<std::int32_t>(h, *sec + 0x74).value_or(-1);
                OverlayPoint p;
                p.wx = *ex; p.wy = *ey; p.wz = ez.value_or(0);
                p.kind = 3;
                p.label = (gfx == 7307) ? std::string("Time sprite")
                        : (gfx == 7164) ? std::string("Rockertunity")
                        : (gfx == 8447) ? std::string("Lumberjack's Intuition")
                        : ("Special (gfx " + std::to_string(gfx) + ")");   // show unknowns by id
                out.points.push_back(p);
                continue;
            }
            if (type != 1 && type != 2) continue;
            auto fx = rpm<float>(h, *sec + kPosX);
            auto fz = rpm<float>(h, *sec + kPosZ);
            auto fy = rpm<float>(h, *sec + kPosY);
            if (!fx || !fy) continue;
            int tx = (int)(*fx / 512.f), ty = (int)(*fy / 512.f);
            if (tx <= 0 || ty <= 0) continue;
            regions.insert(((tx / 64) << 8) | (ty / 64));

            int uid = rpm<std::int32_t>(h, *sec + kUid).value_or(0);
            if (type == 2 && uid == local_uid) {
                have_player = true;
                out.player_tx = tx; out.player_ty = ty; out.player_z = fz.value_or(0);
                out.player_fx = *fx; out.player_fy = *fy;   // smooth sub-tile position for the ground indicator
                out.plane = rpm<std::int32_t>(h, *sec + 0x40).value_or(0);  // live plane
            }

            char nm[40] = {0};
            rpm_bytes(h, *sec + kName, nm, sizeof(nm) - 1);
            std::string name;
            for (int j = 0; j < (int)sizeof(nm) && nm[j]; ++j) {
                unsigned char c = (unsigned char)nm[j];
                if (c >= 0x20 && c <= 0x7e) name.push_back((char)c);
                else if (c == 0xA0) name.push_back(' ');   // CP-1252 NBSP -> space (matches needles typed with a plain space)
            }

            // NPCs carry a live model world AABB (min @ ent+0x40, max @ ent+0x50,
            // interleaved east/up/north). Read it once here: the highlight ring centres
            // on the box, and the marker draws the true 3D box from the same read.
            bool  have_box = false;
            float bmnE = 0, bmnU = 0, bmnN = 0, bmxE = 0, bmxU = 0, bmxN = 0;
            float cE = *fx, cN = *fy, cU = fz.value_or(0);   // fallback: feet/tile centre
            if (type == 1 || type == 2) {   // players are actors too -> read their AABB so a nameplate can sit above the head
                float ab[8] = {0};   // ent+0x40 .. +0x5c
                if (rpm_bytes(h, *ep + 0x40, ab, sizeof(ab))) {
                    float mnx = ab[0], mny = ab[1], mnz = ab[2];   // min east, up, north
                    float mxx = ab[4], mxy = ab[5], mxz = ab[6];   // max east, up, north
                    if (std::isfinite(mnx) && std::isfinite(mnz) && std::isfinite(mxy) &&
                        mxx > mnx && mxz > mnz && mxy >= mny &&
                        (mxx - mnx) < 60.f * 512.f && (mxz - mnz) < 60.f * 512.f && (mxy - mny) < 60.f * 512.f) {
                        have_box = true;
                        bmnE = mnx; bmnU = mny; bmnN = mnz;
                        bmxE = mxx; bmxU = mxy; bmxN = mxz;
                        cE = (mnx + mxx) * 0.5f;   // box centre east
                        cN = (mnz + mxz) * 0.5f;   // box centre north
                        cU = (mny + mxy) * 0.5f;   // box centre up (height) -> ring sits mid-NPC
                    }
                }
            }

            // Highlight match (NPCs only) -- always, regardless of want_npcs. Resolve
            // the name via the cache (def_id) when the in-memory name is empty: morph
            // NPCs like "Catalyst of alteration" carry no name on the entity (it lives
            // on a transform_to child in the cache). SceneJson resolves the same way.
            if (type == 1 && !hlow.empty()) {
                int hcfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                bool fromMem = !name.empty();
                // Resolve the LIVE morph variant for the name match, ALWAYS -- identical to SceneJson. A
                // morph base can ALSO carry a live in-memory name, so keying off that name picks the STATIC
                // default child and a name-based highlight never matches the live variant. resolve_npc
                // returns GetNpc(base) for non-morphs and the ACTIVE child for morphs (EMPTY only when the
                // variant is hidden, which then correctly fails to match). fromMem still tiebreaks the
                // active entity ahead of any static-cache morph match below.
                bool morphHidden = false;
                rtx::cache::NpcMeta hmeta = resolve_npc(h, root_raw, hcfg, &morphHidden);
                // Hidden morph -> do NOT fall back to the entity's in-memory name (it can linger after the
                // game stops drawing this variant); skip it so a relocated/inactive morph never boxes at its
                // old position.
                const std::string hname = !hmeta.name.empty() ? hmeta.name : (morphHidden ? std::string() : name);
                int hresolvedId = (hmeta.id >= 0) ? hmeta.id : hcfg;   // live model id (== SceneJson's reported id)
                std::string lower = hname;
                for (auto& ch : lower) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                for (std::size_t ni = 0; ni < hlow.size(); ++ni) {
                    // "#<id>" needle -> match the live model id; otherwise a name substring (needs a resolved name).
                    bool match = (hlow[ni].id > 0) ? (hresolvedId == hlow[ni].id)
                                                   : (!hname.empty() && lower.find(hlow[ni].needle) != std::string::npos);
                    if (!match) continue;
                    OverlayPoint hp;
                    hp.wx = cE; hp.wy = cN; hp.wz = cU;   // centre of the model box
                    hp.kind = 1; hp.label = hlow[ni].label.empty() ? hname : hlow[ni].label;
                    hp.tile_x = tx; hp.tile_y = ty;       // tile under the NPC
                    hp.head_z = have_box ? bmxU : cU;     // label floats above the head
                    if (have_box) {                       // model bounding box too
                        hp.has_box3d = true;
                        hp.bmin[0] = bmnE; hp.bmin[1] = bmnN; hp.bmin[2] = bmnU;
                        hp.bmax[0] = bmxE; hp.bmax[1] = bmxN; hp.bmax[2] = bmxU;
                    }
                    bool inter = (hcfg >= 0) && !hmeta.actions.empty();
                    hcands.push_back({ hp, tx, ty, (int)ni, inter, fromMem });
                    // NO break: an NPC must become a candidate for EVERY needle it
                    // matches. Several identical-id needles with different tile
                    // (three "Arm" statues sharing one config id) each need this entity
                    // in their pool.
                }
            }

            // Scene-tab per-NPC outline (by uid) -- always, regardless of want_npcs.
            // Carries the live model AABB so the marker draws a tight 3D box.
            if (type == 1 && have_box && !outline_uids.empty()) {
                int uid = rpm<std::int32_t>(h, *sec + kUid).value_or(0);
                if (std::find(outline_uids.begin(), outline_uids.end(), uid) != outline_uids.end()) {
                    OverlayPoint op;
                    op.wx = cE; op.wy = cN; op.wz = cU; op.kind = 1;
                    op.has_box3d = true;
                    op.bmin[0] = bmnE; op.bmin[1] = bmnN; op.bmin[2] = bmnU;
                    op.bmax[0] = bmxE; op.bmax[1] = bmxN; op.bmax[2] = bmxU;
                    out.highlights.push_back(op);
                }
            }

            bool want = (type == 2) ? want_players : want_npcs;
            if (!want) continue;
            // Interactable filter (mirrors the Scene tab): NPCs without right-click
            // actions are dropped; players are unaffected.
            if (interactable && type == 1) {
                int cfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                if (rtx::cache::GetNpc(cfg).actions.empty()) continue;
            }
            OverlayPoint p;
            p.wx = *fx; p.wy = *fy; p.wz = fz.value_or(0);
            p.kind = (type == 2) ? 2 : 1;
            // Resolve NPC names through the cache so nameplates match the Scene tab: GetNpc applies the
            // friendly-name overrides + prettify (e.g. the necromancy conjures whose live sec+0xB8 name
            // is the internal "combatv2_necromancy_..."), and resolve_npc picks the live morph variant.
            // Players keep their live name.
            std::string label = name;
            if (type == 1) {
                int cfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                bool mh = false;
                auto m = resolve_npc(h, root_raw, cfg, &mh);
                label = !m.name.empty() ? m.name : (mh ? std::string() : name);
                if (label.empty()) continue;   // hidden morph variant -> skip (mirror SceneEntitiesJson)
            }
            p.label = label;
            p.uid = uid;
            p.is_self = (type == 2 && uid == local_uid);   // don't nameplate yourself
            if (have_box) {
                p.head_z = bmxU;   // model top -> a nameplate floats above the head (players AND NPCs)
                if (type == 1) {   // 3D box outline is NPC-only (don't box players)
                    p.has_box3d = true;
                    p.bmin[0] = bmnE; p.bmin[1] = bmnN; p.bmin[2] = bmnU;   // east, north, up
                    p.bmax[0] = bmxE; p.bmax[1] = bmxN; p.bmax[2] = bmxU;
                }
            }
            out.points.push_back(p);
        }
    }

    // Resolve one highlight per needle. Priority (each tier's penalty dwarfs the next): a live-named
    // ACTIVE entity beats a static-cache (possibly inactive morph) match; an interactable beats a
    // non-interactable; then the nearest to the player wins.
    if (!hcands.empty()) {
        std::vector<int> best(hlow.size(), -1);
        std::vector<long long> bestScore(hlow.size(), 0);
        for (std::size_t i = 0; i < hcands.size(); ++i) {
            const HiCand& c = hcands[i];
            const HiNeedle& nd = hlow[c.ndl];
            if (nd.all) {
                // match-ALL: every candidate boxes -- bounded to the anchor radius when
                // the needle carried one (still gated to ACTIVE entities so an inactive
                // morph variant can't draw a phantom box next to the real one).
                if (!c.mem) continue;
                if (nd.tx > 0 && nd.rad > 0 &&
                    (std::abs(c.tx - nd.tx) > nd.rad || std::abs(c.ty - nd.ty) > nd.rad)) continue;
                out.highlights.push_back(c.hp);
                continue;
            }
            // Distance to the WANTED tile when the needle carried one, else to the player.
            int anchorX = (nd.tx > 0) ? nd.tx : out.player_tx;
            int anchorY = (nd.ty > 0) ? nd.ty : out.player_ty;
            long long dx = (long long)c.tx - anchorX, dy = (long long)c.ty - anchorY;
            long long score = (c.mem ? 0LL : (1LL << 50)) + (c.inter ? 0LL : (1LL << 40)) + dx * dx + dy * dy;
            if (best[c.ndl] < 0 || score < bestScore[c.ndl]) { best[c.ndl] = (int)i; bestScore[c.ndl] = score; }
        }
        for (int bi : best) if (bi >= 0) out.highlights.push_back(hcands[bi].hp);
    }

    // Heights are ABSOLUTE: live fine-z = 32 * cache surface height (TileHeight
    // returns the cumulative bridge-aware surface). No player anchor: anchoring
    // breaks when the player stands on a deck loc above the terrain (boardwalks).
    // player_z remains the fallback for tiles with no height data.
    constexpr std::int16_t kNoH = -32768;
    constexpr float kHScale = 32.0f;
    out.anchor_h = have_player
        ? rtx::cache::TileHeight(out.player_tx, out.player_ty, out.plane) : kNoH;

    // Terrain-followed fine z at a tile corner (same formula as the grid/objects).
    auto cornerZ = [&](int tx, int ty) -> float {
        std::int16_t hh = rtx::cache::TileHeight(tx, ty, out.plane);
        return (hh == kNoH) ? out.player_z : kHScale * (float)hh;
    };
    // Fill an object's ground footprint box (SW corner tile + W x H tiles), corners
    // ordered SW,SE,NE,NW in world-fine coords. The base is LEVEL at the lowest
    // corner of the footprint (one bridge/layer decision at the centre column):
    // terrain-following the corners shears prisms on slopes and at deck seams.
    auto fillBox = [&](OverlayPoint& op, int swx, int swy, int W, int H) {
        if (W < 1) W = 1;
        if (H < 1) H = 1;
        const int ep = rtx::cache::TileEffPlane(swx + W / 2, swy + H / 2, out.plane);
        const int cx[4] = { swx, swx + W, swx + W, swx };
        const int cy[4] = { swy, swy, swy + H, swy + H };
        float base = out.player_z, top = out.player_z; bool haveBase = false;
        for (int i = 0; i < 4; ++i) {
            std::int16_t hh = rtx::cache::TileHeightAtPlane(cx[i], cy[i], ep);
            if (hh == kNoH) continue;
            float z = kHScale * (float)hh;
            if (!haveBase) { base = top = z; haveBase = true; }
            else { if (z < base) base = z; if (z > top) top = z; }
        }
        for (int i = 0; i < 4; ++i) {
            op.box[i * 3 + 0] = cx[i] * 512.f;
            op.box[i * 3 + 1] = cy[i] * 512.f;
            op.box[i * 3 + 2] = base;
        }
        op.has_box = true;
        // Static locs carry no model height -- extrude by a footprint-scaled stand-in
        // (bigger scenery reads taller), plus the corner spread so a sloped
        // footprint's upslope side stays enclosed.
        int m = (W > H ? W : H);
        if (m > 4) m = 4;
        op.box_h = 280.f + 120.f * (float)(m - 1) + (top - base);
    };

    // Guide sites (focused-mystery guidance): independent of the object toggle.
    // The label's first line names the target; when a cache loc with that name
    // sits within ~8 tiles, the point gets its real footprint prism (so e.g. the
    // Fort entrance wraps the structure instead of a flat tile on the wall top).
    if (have_player && !guide_sites.empty()) {
        std::uint64_t groot = (root && *root > 0x10000) ? *root : 0;
        std::vector<RuntimeObj> gobjs;                    // live placements: true model AABBs
        ReadRuntimeObjects(pid, gobjs);
        // --- REGION marks: coloured sites sharing (label, rgb) merge into ONE flat zone.
        //     Each site resolves to its loc's covered tiles (live AABB span, else the anchor
        //     tile); the union draws floor-level, per tile, with only the PERIMETER outlined
        //     (shared edges masked off) and a single label at the centroid -- so a grass
        //     patch reads as one combined shape covering exactly its own tiles. ---
        {
            auto tkey = [](int tx, int ty) -> long long { return ((long long)tx << 20) | (long long)(ty & 0xFFFFF); };
            std::unordered_map<std::string, std::vector<const GuideSite*>> rgroups;
            for (const auto& gs : guide_sites)
                if (gs.region) rgroups[gs.label + '\x01' + std::to_string(gs.rgb)].push_back(&gs);
            // resolve_loc is not free and this block runs EVERY overlay frame: memoize per
            // config id (all tufts of a patch share one config, so this collapses the lookups).
            std::unordered_map<int, rtx::cache::LocMeta> lmemo;
            auto locOf = [&](int cfg) -> const rtx::cache::LocMeta& {
                auto it = lmemo.find(cfg);
                if (it == lmemo.end()) it = lmemo.emplace(cfg, resolve_loc(h, groot, cfg)).first;
                return it->second;
            };
            for (auto& rg : rgroups) {
                const auto& sites = rg.second;
                const GuideSite* first = sites[0];
                std::string oname = first->label.substr(0, first->label.find('\n'));
                // Leading '-' = match-only first line (see the per-site loop below): resolve
                // footprints by that name but display only the remaining lines (e.g. the
                // burnt-grass countdown), or no pill at all.
                const bool nameHidden = !oname.empty() && oname[0] == '-';
                if (nameHidden) oname.erase(0, 1);
                std::string disp = first->label;
                if (nameHidden) {
                    std::size_t nl = first->label.find('\n');
                    disp = (nl == std::string::npos) ? std::string() : first->label.substr(nl + 1);
                }
                std::unordered_set<long long> tiles;
                std::vector<std::pair<int,int>> tlist;
                auto addTile = [&](int tx, int ty) { if (tiles.insert(tkey(tx, ty)).second) tlist.push_back({ tx, ty }); };
                for (const GuideSite* s : sites) {
                    // Live object first (BGH pens are instanced; spawns are companion-published).
                    // The covered tiles come from the loc CONFIG's footprint dims centred on the
                    // model -- NOT the raw AABB rounded outward: foliage models overhang their
                    // true tiles, which inflated every zone by a tile per side. The AABB only
                    // picks the orientation (which axis carries the longer dim) and the centre.
                    int bestD2 = 6 * 6 + 1; const RuntimeObj* bestR = nullptr;
                    for (const auto& r : gobjs) {
                        if (r.config_id <= 0 || r.plane != out.plane) continue;
                        if (!(r.bmax[0] > r.bmin[0])) continue;
                        int dx = r.x - s->gx, dy = r.y - s->gy;
                        int d2 = dx * dx + dy * dy;
                        if (d2 >= bestD2) continue;
                        if (!oname.empty() && locOf(r.config_id).name != oname) continue;
                        bestD2 = d2; bestR = &r;
                    }
                    if (bestR) {
                        const auto& meta = locOf(bestR->config_id);
                        int dA = meta.dim_x > meta.dim_y ? meta.dim_x : meta.dim_y;
                        int dB = meta.dim_x > meta.dim_y ? meta.dim_y : meta.dim_x;
                        if (dA < 1) dA = 1;
                        if (dB < 1) dB = 1;
                        float spanX = bestR->bmax[0] - bestR->bmin[0], spanY = bestR->bmax[1] - bestR->bmin[1];
                        const int wantX = spanX >= spanY ? dA : dB, wantY = spanX >= spanY ? dB : dA;
                        const int ctx = (int)std::floor((bestR->bmin[0] + bestR->bmax[0]) * 0.5f / 512.f);
                        const int cty = (int)std::floor((bestR->bmin[1] + bestR->bmax[1]) * 0.5f / 512.f);
                        const int tx0 = ctx - (wantX - 1) / 2, ty0 = cty - (wantY - 1) / 2;
                        for (int tx = tx0; tx < tx0 + wantX; ++tx)
                            for (int ty = ty0; ty < ty0 + wantY; ++ty) addTile(tx, ty);
                    } else {
                        addTile(s->gx, s->gy);
                    }
                }
                if (tlist.empty()) continue;
                // Connected components (8-neighbour): one label can cover several SEPARATE
                // patches -- each patch gets its own perimeter and its own pill, so callers
                // send the same plain label for all of them (no artificial "#2" suffixes).
                std::unordered_map<long long, int> comp;
                int ncomp = 0;
                for (const auto& seed : tlist) {
                    if (comp.count(tkey(seed.first, seed.second))) continue;
                    const int cid = ncomp++;
                    std::vector<std::pair<int,int>> stack{ seed };
                    comp[tkey(seed.first, seed.second)] = cid;
                    while (!stack.empty()) {
                        auto cur = stack.back(); stack.pop_back();
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dy = -1; dy <= 1; ++dy) {
                                if (!dx && !dy) continue;
                                long long k2 = tkey(cur.first + dx, cur.second + dy);
                                if (!tiles.count(k2) || comp.count(k2)) continue;
                                comp[k2] = cid;
                                stack.push_back({ cur.first + dx, cur.second + dy });
                            }
                    }
                }
                // Per-component centroid -> the tile that carries that patch's label pill.
                std::vector<long long> csx(ncomp, 0), csy(ncomp, 0), cn(ncomp, 0);
                for (const auto& t : tlist) { int c = comp[tkey(t.first, t.second)]; csx[c] += t.first; csy[c] += t.second; ++cn[c]; }
                std::vector<std::pair<int,int>> labTile(ncomp, { 0, 0 });
                std::vector<long long> labD(ncomp, -1);
                for (const auto& t : tlist) {
                    const int c = comp[tkey(t.first, t.second)];
                    long long dx = t.first - csx[c] / cn[c], dy = t.second - csy[c] / cn[c], d = dx * dx + dy * dy;
                    if (labD[c] < 0 || d < labD[c]) { labD[c] = d; labTile[c] = t; }
                }
                for (const auto& t : tlist) {
                    OverlayPoint op;
                    op.kind = 4;
                    op.rgb = first->rgb;
                    op.wx = t.first * 512.f + 256.f; op.wy = t.second * 512.f + 256.f;
                    op.wz = cornerZ(t.first, t.second);
                    fillBox(op, t.first, t.second, 1, 1);
                    op.box_h = 0.f;                               // floor-level only
                    int mask = 0;
                    if (!tiles.count(tkey(t.first, t.second - 1))) mask |= 1;   // south
                    if (!tiles.count(tkey(t.first + 1, t.second))) mask |= 2;   // east
                    if (!tiles.count(tkey(t.first, t.second + 1))) mask |= 4;   // north
                    if (!tiles.count(tkey(t.first - 1, t.second))) mask |= 8;   // west
                    op.edge_mask = mask;
                    if (!disp.empty() && t == labTile[comp[tkey(t.first, t.second)]]) op.label = disp;   // one pill per patch
                    out.guides.push_back(std::move(op));
                }
            }
        }
        for (const auto& gs : guide_sites) {
            if (gs.region) continue;                     // merged into a zone above
            OverlayPoint op;
            op.kind = 4;
            op.label = gs.label;
            // Leading '-' on the first line = MATCH-ONLY name: it still resolves the loc
            // footprint below but is not displayed; any remaining lines still show.
            const bool nameHidden = !gs.label.empty() && gs.label[0] == '-';
            if (nameHidden) {
                std::size_t nl = gs.label.find('\n');
                op.label = (nl == std::string::npos) ? std::string() : gs.label.substr(nl + 1);
            }
            op.rgb = gs.rgb;
            op.wx = gs.gx * 512.f + 256.f; op.wy = gs.gy * 512.f + 256.f;
            op.wz = cornerZ(gs.gx, gs.gy);
            // AREA mark: a flat ground rect spanning (gx,gy)..(gx2,gy2) -- e.g. the BGH
            // tall/burnt-grass region. No loc matching, no prism (flat reads as a zone decal).
            if (gs.gx2 >= gs.gx && gs.gy2 >= gs.gy && gs.gx2 > 0 && gs.gy2 > 0) {
                fillBox(op, gs.gx, gs.gy, gs.gx2 - gs.gx + 1, gs.gy2 - gs.gy + 1);
                op.box_h = 0.f;
                op.wx = (gs.gx + gs.gx2 + 1) * 0.5f * 512.f;
                op.wy = (gs.gy + gs.gy2 + 1) * 0.5f * 512.f;
                out.guides.push_back(std::move(op));
                continue;
            }
            std::string oname = gs.label.substr(0, gs.label.find('\n'));
            if (nameHidden) oname.erase(0, 1);
            bool found = false;
            if (gs.snap_obj) {
                // Snap to the nearest LIVE object's model AABB so the box sits at the true
                // rendered height -- correct even in an unmapped instance where the cache
                // heightmap is wrong. No live object near the tile -> skip the mark (never draw
                // it at the bogus cache height). Matched by tile, not name (the morph name varies).
                int bestD2 = 5 * 5 + 1; const RuntimeObj* bestR = nullptr;
                for (const auto& r : gobjs) {
                    if (r.config_id <= 0 || r.plane != out.plane) continue;
                    if (!(r.bmax[0] > r.bmin[0])) continue;       // need a real AABB
                    int dx = r.x - gs.gx, dy = r.y - gs.gy;
                    int d2 = dx * dx + dy * dy;
                    if (d2 >= bestD2) continue;
                    bestD2 = d2; bestR = &r;
                }
                if (!bestR) continue;
                op.has_box3d = true;
                for (int j = 0; j < 3; ++j) { op.bmin[j] = bestR->bmin[j]; op.bmax[j] = bestR->bmax[j]; }
                op.wx = (bestR->bmin[0] + bestR->bmax[0]) * 0.5f;
                op.wy = (bestR->bmin[1] + bestR->bmax[1]) * 0.5f;
                op.wz = (bestR->bmin[2] + bestR->bmax[2]) * 0.5f;
                out.guides.push_back(std::move(op));
                continue;
            }
            if (!oname.empty()) {
                // Live runtime object first: its model AABB wraps the actual visual.
                // (The static map footprint can anchor elsewhere, e.g. atop a wall,
                // and its stand-in height is a guess.)
                int bestD2 = 8 * 8 + 1; const RuntimeObj* bestR = nullptr;
                for (const auto& r : gobjs) {
                    if (r.config_id <= 0 || r.plane != out.plane) continue;
                    if (!(r.bmax[0] > r.bmin[0])) continue;       // need a real AABB
                    int dx = r.x - gs.gx, dy = r.y - gs.gy;
                    int d2 = dx * dx + dy * dy;
                    if (d2 >= bestD2) continue;
                    if (resolve_loc(h, groot, r.config_id).name != oname) continue;
                    bestD2 = d2; bestR = &r;
                }
                if (bestR) {
                    op.has_box3d = true;
                    for (int j = 0; j < 3; ++j) { op.bmin[j] = bestR->bmin[j]; op.bmax[j] = bestR->bmax[j]; }
                    op.wx = (bestR->bmin[0] + bestR->bmax[0]) * 0.5f;
                    op.wy = (bestR->bmin[1] + bestR->bmax[1]) * 0.5f;
                    op.wz = (bestR->bmin[2] + bestR->bmax[2]) * 0.5f;
                    found = true;
                }
            }
            if (!found && !oname.empty()) {
                // search the regions around the site (a footprint can be anchored
                // across a region boundary)
                std::unordered_set<int> rkeys;
                for (int dx = -3; dx <= 3; dx += 6)
                    for (int dy = -3; dy <= 3; dy += 6)
                        rkeys.insert((((gs.gx + dx) >> 6) << 8) | ((gs.gy + dy) >> 6));
                int bestD = 0x7fffffff, bswx = 0, bswy = 0, bW = 1, bH = 1;
                for (int key : rkeys) {
                    int rx = (key >> 8) & 0xff, ry = key & 0xff;
                    for (const auto& p : rtx::cache::RegionLocations(rx, ry)) {
                        if (p.plane != out.plane) continue;
                        auto meta = resolve_loc(h, groot, p.id);
                        if (meta.name != oname) continue;
                        int wx = rx * 64 + p.x, wy = ry * 64 + p.y;
                        int W2 = meta.dim_x, H2 = meta.dim_y;
                        if (p.rotation == 1 || p.rotation == 3) std::swap(W2, H2);
                        int dx = wx + W2 / 2 - gs.gx, dy = wy + H2 / 2 - gs.gy;
                        int d2 = dx * dx + dy * dy;
                        if (d2 < bestD) { bestD = d2; bswx = wx; bswy = wy; bW = W2; bH = H2; }
                    }
                }
                if (bestD <= 8 * 8) {
                    fillBox(op, bswx, bswy, bW, bH);
                    op.wx = (bswx + bW * 0.5f) * 512.f;
                    op.wy = (bswy + bH * 0.5f) * 512.f;
                    found = true;
                }
            }
            if (!found) { fillBox(op, gs.gx, gs.gy, 1, 1); op.box_h = 0.f; }   // flat tile fallback
            out.guides.push_back(op);
        }

    }

    // Direction arrow target: the objective tile to point at. The first guide site, or -- when
    // guiding to an NPC -- the active highlighted NPC's live tile. Only DIRECTION is shown (a
    // ground arrow at the player's feet), never the walked path, so no pathfinding is done.
    {
        int tgx = 0, tgy = 0; bool haveTarget = false;
        // A labelled NPC highlight that resolved to a live scene tile AT/near a guide objective (a talk-to
        // clue's target NPC, now in range) WINS: aim the arrow at the NPC and DROP the static destination
        // tile, so only the NPC is marked (box + label + tile-under-it follow it). The proximity gate keeps
        // an unrelated highlight (e.g. a random event) from stealing the arrow / hiding the clue tile.
        // Otherwise fall back to the first guide site -- the static objective tile.
        bool npcAtObjective = false;
        for (const auto& hp : out.highlights) {
            if (hp.label.empty() || hp.tile_x <= 0 || hp.tile_y <= 0) continue;
            bool atObjective = guide_sites.empty();
            for (const auto& gs : guide_sites) { int dx = hp.tile_x - gs.gx, dy = hp.tile_y - gs.gy; if (dx * dx + dy * dy <= 24 * 24) { atObjective = true; break; } }
            if (atObjective) { tgx = hp.tile_x; tgy = hp.tile_y; haveTarget = true; npcAtObjective = !guide_sites.empty(); break; }
        }
        if (npcAtObjective)                                       // the clue's NPC is in scene -> hide the separate ground tile
                                                                  // marker; coloured hazard/area marks are annotations and stay
            out.guides.erase(std::remove_if(out.guides.begin(), out.guides.end(),
                                            [](const OverlayPoint& p){ return p.rgb == 0; }),
                             out.guides.end());
        else if (!haveTarget) {                                   // first NAVIGATIONAL site (snap object-boxes and coloured
                                                                  // hazard/area marks never request an arrow)
            for (const auto& gs : guide_sites) { if (!gs.snap_obj && gs.rgb == 0) { tgx = gs.gx; tgy = gs.gy; haveTarget = true; break; } }
        }
        // Never aim the arrow across the y=6400 surface/dungeon coordinate boundary: dungeons are stacked at
        // y+6400 in map space, so a straight player->target chevron between the two bands points a meaningless
        // direction. Drop the arrow when the player and target are on opposite sides of the line.
        bool crossBand = haveTarget && ((out.player_ty < 6400) != (tgy < 6400));
        if (have_player && haveTarget && !crossBand) { out.has_arrow = true; out.arrow_tx = tgx; out.arrow_ty = tgy; }
    }

    if (want_objects && have_player) {
        constexpr int kMaxRegions = 9, kMaxObjects = 600;
        std::unordered_set<long long> seen;
        std::uint64_t rroot = (root && *root > 0x10000) ? *root : 0;   // for varbit-aware morphs
        struct RO { int id, x, y; };
        std::vector<RO> robjs;                                          // runtime placements (dedupe static)
        int oc = 0;

        // Runtime objects FIRST: they carry the live model AABB (true visual size +
        // height), so they take priority over the static map's flat footprint.
        std::vector<RuntimeObj> runtime;
        if (ReadRuntimeObjects(pid, runtime)) {
            for (const auto& r : runtime) {
                if (oc >= kMaxObjects) break;
                if (r.config_id <= 0 || r.plane != out.plane) continue;
                auto meta = resolve_loc(h, rroot, r.config_id);
                if (meta.name.empty()) continue;
                if (interactable && meta.actions.empty()) continue;
                long long dk = ((long long)r.config_id << 40) | ((long long)r.x << 20) | (unsigned)r.y;
                if (!seen.insert(dk).second) continue;
                robjs.push_back({ r.config_id, r.x, r.y });
                OverlayPoint op;
                op.kind = 0; op.label = meta.name;
                if (r.bmax[0] > r.bmin[0]) {            // valid live AABB -> true 3D box
                    op.has_box3d = true;
                    for (int j = 0; j < 3; ++j) { op.bmin[j] = r.bmin[j]; op.bmax[j] = r.bmax[j]; }
                    op.wx = (r.bmin[0] + r.bmax[0]) * 0.5f;   // label anchored on the box centre
                    op.wy = (r.bmin[1] + r.bmax[1]) * 0.5f;
                    op.wz = (r.bmin[2] + r.bmax[2]) * 0.5f;
                    op.head_z = r.bmax[2];   // box top -> a nameplate floats above the object
                } else {                                      // no box -> centred tile footprint
                    op.wx = r.x * 512.f + 256.f; op.wy = r.y * 512.f + 256.f;
                    std::int16_t objH = rtx::cache::TileHeight(r.x, r.y, out.plane);
                    op.wz = (objH == kNoH) ? out.player_z : kHScale * (float)objH;
                    int W = meta.dim_x, H = meta.dim_y;
                    fillBox(op, r.x - (W - 1) / 2, r.y - (H - 1) / 2, W, H);
                    op.head_z = op.wz + op.box_h;
                }
                out.points.push_back(op);
                ++oc;
            }
        }

        // Static map scenery SECOND: covers what the runtime didn't (beyond the
        // companion's range / not captured). Skipped where a runtime object of the
        // same loc already covers the tile (footprint tolerance). Flat footprint box.
        int rn = 0;
        for (int key : regions) {
            if (rn++ >= kMaxRegions || oc >= kMaxObjects) break;
            int rx = (key >> 8) & 0xff, ry = key & 0xff;
            for (const auto& p : rtx::cache::RegionLocations(rx, ry)) {
                if (oc >= kMaxObjects) break;
                if (p.plane != out.plane) continue;
                auto meta = resolve_loc(h, rroot, p.id);              // varbit-aware (live morph state)
                if (meta.name.empty()) continue;
                if (interactable && meta.actions.empty()) continue;   // mirror Scene tab filter
                int wx = rx * 64 + p.x, wy = ry * 64 + p.y;
                long long dk = ((long long)p.id << 40) | ((long long)wx << 20) | (unsigned)wy;
                if (!seen.insert(dk).second) continue;
                int tol = std::max(meta.dim_x, meta.dim_y) - 1; if (tol < 0) tol = 0;
                bool dup = false;
                for (const auto& r : robjs)
                    if (r.id == p.id && std::abs(r.x - wx) <= tol && std::abs(r.y - wy) <= tol) { dup = true; break; }
                if (dup) continue;
                OverlayPoint op;
                op.wx = wx * 512.f + 256.f; op.wy = wy * 512.f + 256.f;
                std::int16_t objH = rtx::cache::TileHeight(wx, wy, out.plane);
                op.wz = (objH == kNoH) ? out.player_z : kHScale * (float)objH;   // sit on the terrain
                op.kind = 0; op.label = meta.name;
                int W = meta.dim_x, H = meta.dim_y;
                if (p.rotation == 1 || p.rotation == 3) std::swap(W, H);
                fillBox(op, wx, wy, W, H);
                op.head_z = op.wz + op.box_h;   // footprint top -> a nameplate floats above it
                out.points.push_back(op);
                ++oc;
            }
        }
    }

    if (have_player && grid_radius > 0) {
        out.grid_r = grid_radius;
        rtx::cache::RegionBlockedFill(out.player_tx, out.player_ty, out.plane,
                                      grid_radius, out.blocked);
        rtx::cache::RegionHeightsFill(out.player_tx, out.player_ty, out.plane,
                                      grid_radius, out.heights);
    }

    // Drawable when the player was found (needed for the grid + markers) OR a highlight
    // matched (random-event ring). Highlights project off the view matrix alone, so they
    // must draw even with the overlay/grid disabled and no player tile.
    out.ok = have_player || !out.highlights.empty();
    return out.ok;
}

// Info-tab state g_pinfo_prevpos / g_pinfo_mu are declared up top (next to g_states), since the
// dead-client eviction in SampleAll references them before this point.

std::string PlayerInfoJson(std::uint32_t pid) {
    constexpr std::uint64_t kContainer = 0x19990, kActiveIdx = 0x70, kEntryArr = 0x58,
                            kEntryWv = 0x8, kVecBegin = 0x138,
                            kVecEnd = 0x140, kSecPtr = 0x1A0, kType = 0x10, kUid = 0x88,
                            kPosX = 0x270, kPosY = 0x278, kAnim = 0xA90,
                            kPlayerData = 0x19F68, kLocalUid = 0x48;
    auto ps = snap_proc(pid);
    if (!ps)
        return "{\"in\":false}";
    HANDLE h = ps.h;
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off)
        -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    auto pdata = deref(root, kPlayerData);
    int local_uid = (pdata && *pdata > 0x10000)
                      ? rpm<std::int32_t>(h, *pdata + kLocalUid).value_or(-1) : -1;
    auto cont = deref(root, kContainer);
    auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, kEntryArr);
    if (!idx || *idx < 0 || !arr || *arr <= 0x10000) return "{\"in\":false}";
    auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + kEntryWv);
    auto worker = (wv && *wv > 0x10000) ? scene_worker(h, pid, *wv, nullptr)
                                        : std::optional<std::uint64_t>{};
    auto vb = deref(worker, kVecBegin), ve = deref(worker, kVecEnd);
    std::uint64_t psec = 0;
    if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
        std::uint64_t n = (*ve - *vb) / 8; if (n > 20000) n = 20000;
        for (std::uint64_t i = 0; i < n; ++i) {
            auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
            if (!ep || *ep <= 0x10000) continue;
            auto sec = rpm<std::uint64_t>(h, *ep + kSecPtr);
            if (!sec || *sec <= 0x10000) continue;
            if (rpm<std::uint8_t>(h, *sec + kType).value_or(0xff) != 2) continue;
            if (rpm<std::int32_t>(h, *sec + kUid).value_or(0) == local_uid) { psec = *sec; break; }
        }
    }
    if (!psec) return "{\"in\":false}";

    float fx = rpm<float>(h, psec + kPosX).value_or(0), fy = rpm<float>(h, psec + kPosY).value_or(0);
    int tx = (int)(fx / 512.f), ty = (int)(fy / 512.f);
    int plane = rpm<std::int32_t>(h, psec + 0x40).value_or(0);   // live plane (sec + 0x40)
    if (plane < 0 || plane > 3) plane = 0;
    int anim = rpm<std::int32_t>(h, psec + kAnim).value_or(-1);
    float px, py;
    {
        std::lock_guard<std::mutex> lk(g_pinfo_mu);
        auto& prev = g_pinfo_prevpos[(DWORD)pid];
        px = prev.first; py = prev.second;
        prev = {fx, fy};
    }
    bool moving = (px != 0 || py != 0) &&
                  (std::abs(fx - px) > 1.f || std::abs(fy - py) > 1.f);

    // LP interacting: the local player's interaction target. Read the target
    // entity pointer (sec+0x218) and its uid (sec+0x1b4), then resolve against
    // the live entity vector (NPCs + players). Scenery/objects (e.g. an energy
    // rift) are not in the live vector, so they resolve as null here.
    std::string interactJson = "null";
    if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
        auto rawp = rpm<std::uint64_t>(h, psec + 0x218);
        std::uint64_t t1 = (rawp && *rawp > 0x10000) ? *rawp : 0;
        std::uint64_t t2 = t1 ? t1 + 0x20 : 0;
        int tuid = rpm<std::int32_t>(h, psec + 0x1b4).value_or(-1);
        if (t1 || tuid > 0) {
            std::uint64_t n = (*ve - *vb) / 8; if (n > 20000) n = 20000;
            for (std::uint64_t i = 0; i < n; ++i) {
                auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
                if (!ep || *ep <= 0x10000) continue;
                auto sec = rpm<std::uint64_t>(h, *ep + kSecPtr);
                if (!sec || *sec <= 0x10000) continue;
                int ety = rpm<std::uint8_t>(h, *sec + kType).value_or(0xff);
                if (ety != 1 && ety != 2) continue;
                int euid = rpm<std::int32_t>(h, *sec + kUid).value_or(0);
                bool match = (tuid > 0 && euid == tuid) ||
                             *ep == t1 || *ep == t2 || *sec == t1 || *sec == t2;
                if (!match) continue;
                char enm[40] = {0};
                rpm_bytes(h, *sec + 0xB8, enm, sizeof(enm) - 1);
                std::string name2;
                for (int j = 0; j < (int)sizeof(enm) && enm[j]; ++j) {
                    unsigned char c = (unsigned char)enm[j];
                    if (c >= 0x20 && c <= 0x7e) name2.push_back((char)c);
                    else if (c == 0xA0) name2.push_back(' ');   // CP-1252 NBSP -> space
                }
                int ecfg = -1;
                if (ety == 1) {                                   // NPC: prefer cache name
                    ecfg = rpm<std::int32_t>(h, *sec + 0x1080).value_or(-1);
                    auto m = rtx::cache::GetNpc(ecfg);
                    if (!m.name.empty()) name2 = m.name;
                }
                char ib[160];
                std::snprintf(ib, sizeof(ib),
                    "{\"type\":%d,\"id\":%d,\"uid\":%d,\"name\":\"", ety, ecfg, euid);
                interactJson = ib; interactJson += json_escape(name2); interactJson += "\"}";
                break;
            }
        }
    }

    // Hovering progress bar over the player (the action bar: clue scan / search / harvest, etc.). The chain:
    // psec + (stance 0xF48 - 0x40 = 0xF08) -> +0x28 = the head-bar LIST,
    // each bar a ptr with fill byte @ +0x34 (0-255). The list order is not fixed (+0x10 holds the HEALTH
    // bar), so the list is walked and the action bar reported as the first bar whose fill isn't the
    // player's HP% (preferring a partially-filled one). -1 = no action bar.
    int progress = -1;
    {
        auto p1 = rpm<std::uint64_t>(h, psec + 0xF08);
        auto p2 = (p1 && *p1 > 0x10000) ? rpm<std::uint64_t>(h, *p1 + 0x28) : std::nullopt;
        if (p2 && *p2 > 0x10000) {
            int hpc = rpm<std::int32_t>(h, psec + 0x114C).value_or(0);
            int hpm = rpm<std::int32_t>(h, psec + 0x114C + 0x1C).value_or(0);
            int hp255 = (hpm > 0) ? (int)((hpc * 255LL + hpm / 2) / hpm) : -999;   // health bar fill to exclude
            int firstNonHp = -1;
            for (int slot = 0; slot <= 0x40; slot += 8) {
                auto hb = rpm<std::uint64_t>(h, *p2 + slot);
                if (!hb || *hb <= 0x1000000000ull || *hb > 0x00007FFFFFFFFFFFull) continue;   // skip non-pointer/packed-int slots (real heap ptrs are high)
                int fill = rpm<std::uint8_t>(h, *hb + 0x34).value_or(0);
                if (hp255 >= 0 && std::abs(fill - hp255) <= 12) continue;             // this is the HP bar
                if (firstNonHp < 0) firstNonHp = fill;
                if (fill > 0 && fill < 255) { progress = fill; break; }               // a partially-filled action bar
            }
            if (progress < 0) progress = firstNonHp;
        }
    }

    // Run energy + weight live in the SKILL BLOCK (the same object SkillsXp walks:
    // count@+0x8, records@+0x10), just past the record-array pointer. They are set
    // DIRECTLY BY SERVER PACKETS, not by the var system -- nothing in varp/varc moves as
    // energy drains. Each has a dedicated packet handler (rs2client 949 RVAs), both of
    // which end in `mov [block+off], reg`:
    //   +0x18 energy: rva 0x1B8A00 reads ONE byte off the packet -> 0..100
    //   +0x1C weight: rva 0x1B8960 reads TWO bytes, ror/byte-swaps, MOVSX sign-extends
    //                 -> signed 16-bit (weight is negative with light gear)
    // Sentinels sit OUTSIDE each field's real range (weight is a signed i16, so -9999 is a
    // legal weight): energy -1, weight -100000 = block unavailable (e.g. not logged in).
    int energy = -1, weight = -100000;
    {
        auto stats = deref(root, kOffStats);
        auto block = deref(stats, kStatsInner);
        if (block && *block > 0x10000) {
            int e = rpm<std::int32_t>(h, *block + 0x18).value_or(-1);
            int w = rpm<std::int32_t>(h, *block + 0x1C).value_or(-100000);
            if (e >= 0 && e <= 100) energy = e;
            if (w >= -32768 && w <= 32767) weight = w;
        }
    }

    // Combat level: psec+0x10BC in the player's own entity section; psec+0x10C0 mirrors it.
    // Reading the field beats recomputing from skills -- the EOC/Necromancy formula is easy
    // to get wrong. -1 = unavailable / out of the valid 3..152 range.
    int combat = -1;
    {
        int c = rpm<std::int32_t>(h, psec + 0x10BC).value_or(-1);
        if (c >= 3 && c <= 152) combat = c;
    }

    // Region + local tile: the shareable, instance-stable coordinate (region = (x>>6)<<8|(y>>6),
    // local = global & 63) the tile-marker feature stores by.
    int region = ((tx >> 6) << 8) | (ty >> 6);
    int lx = tx & 63, ly = ty & 63;
    char buf[440];
    std::snprintf(buf, sizeof(buf),
        "{\"in\":true,\"x\":%d,\"y\":%d,\"plane\":%d,\"region\":%d,\"lx\":%d,\"ly\":%d,"
        "\"anim\":%d,\"moving\":%s,\"progress\":%d,\"energy\":%d,\"weight\":%d,\"combat\":%d,"
        "\"interact\":",
        tx, ty, plane, region, lx, ly, anim, moving ? "true" : "false", progress, energy, weight, combat);
    std::string out = buf; out += interactJson; out += "}";
    return out;
}

bool PlayerTile(std::uint32_t pid, int& tx, int& ty, int& plane) {
    constexpr std::uint64_t kContainer = 0x19990, kActiveIdx = 0x70, kEntryArr = 0x58,
                            kEntryWv = 0x8, kVecBegin = 0x138,
                            kVecEnd = 0x140, kSecPtr = 0x1A0, kType = 0x10, kUid = 0x88,
                            kPosX = 0x270, kPosY = 0x278,
                            kPlayerData = 0x19F68, kLocalUid = 0x48;
    auto ps = snap_proc(pid);
    if (!ps) return false;
    HANDLE h = ps.h;
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off)
        -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    auto pdata = deref(root, kPlayerData);
    int local_uid = (pdata && *pdata > 0x10000)
                      ? rpm<std::int32_t>(h, *pdata + kLocalUid).value_or(-1) : -1;
    auto cont = deref(root, kContainer);
    auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, kEntryArr);
    if (!idx || *idx < 0 || !arr || *arr <= 0x10000) return false;
    auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + kEntryWv);
    auto worker = (wv && *wv > 0x10000) ? scene_worker(h, pid, *wv, nullptr)
                                        : std::optional<std::uint64_t>{};
    auto vb = deref(worker, kVecBegin), ve = deref(worker, kVecEnd);
    std::uint64_t psec = 0;
    if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
        std::uint64_t n = (*ve - *vb) / 8; if (n > 20000) n = 20000;
        for (std::uint64_t i = 0; i < n; ++i) {
            auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
            if (!ep || *ep <= 0x10000) continue;
            auto sec = rpm<std::uint64_t>(h, *ep + kSecPtr);
            if (!sec || *sec <= 0x10000) continue;
            if (rpm<std::uint8_t>(h, *sec + kType).value_or(0xff) != 2) continue;
            if (rpm<std::int32_t>(h, *sec + kUid).value_or(0) == local_uid) { psec = *sec; break; }
        }
    }
    if (!psec) return false;
    float fx = rpm<float>(h, psec + kPosX).value_or(0), fy = rpm<float>(h, psec + kPosY).value_or(0);
    tx = (int)(fx / 512.f); ty = (int)(fy / 512.f);
    int pl = rpm<std::int32_t>(h, psec + 0x40).value_or(0);
    plane = (pl < 0 || pl > 3) ? 0 : pl;
    return true;
}

// Augmented items: walk inventory (93) + equipment (94), decode each slot's
// Extra_ints (see project_runetoolsx_perks) for item XP / gizmo perks.
std::string PerksJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"items\":[]}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{\"items\":[]}";
    auto cmgr = rpm<std::uint64_t>(h, *root + kOffInvData);
    if (!cmgr || *cmgr <= 0x10000) return "{\"items\":[]}";
    auto cstart = rpm<std::uint64_t>(h, *cmgr + 0x8);
    auto cend   = rpm<std::uint64_t>(h, *cmgr + 0x10);
    if (!cstart || !cend || *cstart <= 0x10000 || *cend <= *cstart) return "{\"items\":[]}";
    int ncont = (int)((*cend - *cstart) / kContainerStride);
    if (ncont > kMaxContainers) ncont = kMaxContainers;

    std::string items; int n = 0; char buf[256];
    for (int c = 0; c < ncont; ++c) {
        std::uint64_t e = *cstart + (std::uint64_t)c * kContainerStride;
        int cid = rpm<std::int32_t>(h, e + 0x10).value_or(-1);
        if (cid != 93 && cid != 94) continue;
        auto istart = rpm<std::uint64_t>(h, e + 0x18);
        auto iend   = rpm<std::uint64_t>(h, e + 0x20);
        auto xstart = rpm<std::uint64_t>(h, e + 0x30);    // per-slot extended-data array
        if (!istart || !iend || *istart <= 0x10000 || *iend <= *istart ||
            !xstart || *xstart <= 0x10000) continue;
        int nslot = (int)((*iend - *istart) / 0x8);
        if (nslot > kMaxBankSlots) nslot = kMaxBankSlots;
        for (int s = 0; s < nslot; ++s) {
            int iid = rpm<std::int32_t>(h, *istart + (std::uint64_t)s * 0x8).value_or(0);
            if (iid <= 0) continue;
            // The reliable "is augmented" signal is in the ITEM CONFIG, not the name or the
            // live ext-data: Invention-augmented gear has a "Disassemble" worn option and a
            // destroy message mentioning "gizmos". Look-alikes that merely carry item XP
            // (Scripture of Bik, Grace of the elves, the rune pouch) have neither and are
            // correctly skipped.
            if (!rtx::cache::ItemIsAugmented(iid)) continue;
            std::string itemName = rtx::cache::ItemName(iid);
            std::uint64_t X = *xstart + (std::uint64_t)s * 0x38;
            int count = rpm<std::int32_t>(h, X + 0x18).value_or(0);
            if (count < 2 || count > 10) continue;
            auto ptrArr = rpm<std::uint64_t>(h, X + 0x10);
            if (!ptrArr || *ptrArr <= 0x10000) continue;
            // The slot's extra data is a set of KEYED pairs: each non-null pointer
            // holds a field key at +0 and its int value at +8. For
            // augmented gear: key 0 = item XP, key 1 = gizmo-1 packed perk ids
            // (low 15 bits = perk1, high = perk2), key 2 = gizmo-2 packed ids (only
            // when two gizmos), key 3 = packed perk RANKS (4-bit nibbles
            // g1.p1,g1.p2,g2.p1,g2.p2). Items are gated by ItemIsAugmented() above,
            // so here it just decodes whatever keys are present (a freshly-augmented
            // item with no gizmo installed carries only key 0, its item XP).
            int  val[8]    = {0,0,0,0,0,0,0,0};
            bool hasKey[8] = {false,false,false,false,false,false,false,false};
            for (int j = 0; j < count; ++j) {
                auto p = rpm<std::uint64_t>(h, *ptrArr + (std::uint64_t)j * 0x8);
                if (!p || *p <= 0x10000) continue;
                int key = rpm<std::int32_t>(h, *p).value_or(-1);
                if (key < 0 || key >= 8) continue;
                val[key]    = rpm<std::int32_t>(h, *p + 0x8).value_or(0);
                hasKey[key] = true;
            }
            int  xp        = val[0];
            int  perkData1 = val[1];
            int  perkData2 = val[2];
            int  rankInt   = val[3];
            bool twoGizmos = hasKey[2] && perkData2 != 0;
            // Augmentation was already confirmed from the item config above, so this
            // just decodes whatever gizmos + item XP the slot carries (a non-gizmo'd
            // augmented item, e.g. the pickpocketing bag, simply has no perk keys).

            auto nib = [&](int idx){ return (rankInt >> (idx * 4)) & 0xF; };
            struct PK { int id; int rank; };
            std::vector<PK> pk = { { perkData1 & 0x7FFF, nib(0) }, { perkData1 >> 15, nib(1) } };
            if (twoGizmos) { pk.push_back({ perkData2 & 0x7FFF, nib(2) });
                             pk.push_back({ perkData2 >> 15,    nib(3) }); }

            bool g1has = (perkData1 & 0x7FFF) > 0 || (perkData1 >> 15) > 0;
            bool g2has = twoGizmos && ((perkData2 & 0x7FFF) > 0 || (perkData2 >> 15) > 0);
            int  gizmos = (g1has ? 1 : 0) + (g2has ? 1 : 0);

            std::string perks;
            for (const auto& p : pk) {
                if (p.id <= 0) continue;
                std::string nm = rtx::cache::PerkName(p.id);
                if (nm.empty()) nm = "Perk #" + std::to_string(p.id);
                if (p.rank > 0) { nm += ' '; nm += std::to_string(p.rank); }
                if (!perks.empty()) perks.push_back(',');
                std::snprintf(buf, sizeof(buf), "{\"id\":%d,\"rank\":%d,\"name\":\"", p.id, p.rank);
                perks += buf; perks += json_escape(nm); perks += "\"";
                std::string ds = rtx::cache::PerkDesc(p.id);   // effect text (may carry <col=..> markup)
                if (!ds.empty()) { perks += ",\"desc\":\""; perks += json_escape(ds); perks += "\""; }
                perks += "}";
            }
            if (n) items.push_back(',');
            std::snprintf(buf, sizeof(buf),
                "{\"container\":%d,\"slot\":%d,\"id\":%d,\"xp\":%d,\"level\":%d,\"gizmos\":%d,\"name\":\"",
                cid, s, iid, xp, rtx::cache::ItemLevelFromXp(xp), gizmos);
            items += buf; items += json_escape(itemName);
            items += "\",\"perks\":["; items += perks; items += "]}";
            ++n;
        }
    }
    return "{\"items\":[" + items + "]}";
}


// ---- Reader health check -------------------------------------------------
// On-demand diagnostic (Info tab): exercises every major read chain against the live client
// and reports ok/fail per subsystem, so after a GAME UPDATE one glance shows exactly which
// anchor or offset chain broke instead of features silently reading garbage. Checks are
// ordered root-first: a failure high in the list explains everything below it.
//   {"version":"..","checks":[{"k":name,"ok":0|1|2,"d":detail},..]}   ok 2 = warn/informational.
std::string ReaderHealthJson(std::uint32_t pid) {
    std::string checks;
    auto add = [&](const char* k, int ok, const std::string& d) {
        if (!checks.empty()) checks.push_back(',');
        checks += "{\"k\":\""; checks += k; checks += "\",\"ok\":" + std::to_string(ok) +
                  ",\"d\":\"" + json_escape(d) + "\"}";
    };
    auto jcount = [](const std::string& j) {          // entry count of a flat {"k":v,..} dump
        long q = 0; for (char c : j) if (c == '"') ++q; return (int)(q / 2);
    };
    std::string version;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) version = it->second.client_version;
    }

    // Check names/details are USER-FACING: plain language only, no offsets or internals
    // (those live in this file's comments; the row order maps 1:1 to the chains below).
    auto ps = snap_proc(pid);
    add("Game client", ps ? 1 : 0, ps ? "connected" : "not connected");
    if (!ps) return "{\"version\":\"" + json_escape(version) + "\",\"checks\":[" + checks + "]}";
    HANDLE h = ps.h;

    auto root = rpm<std::uint64_t>(h, ps.mgva);
    bool rootOk = root && *root > 0x10000;
    add("Game data", rootOk ? 1 : 0, rootOk ? "" : "not available (lobby / loading is normal)");

    // Tick counter (independent pattern): present + advancing.
    {
        std::uint32_t tc = 0; double age = 0;
        bool ok = TickState(pid, tc, age);
        add("Game tick", ok ? 1 : 0,
            ok ? ("tick " + std::to_string(tc)) : "not found");
    }
    if (!rootOk) return "{\"version\":\"" + json_escape(version) + "\",\"checks\":[" + checks + "]}";

    {
        auto pdata = rpm<std::uint64_t>(h, *root + 0x19F68);
        int uid = (pdata && *pdata > 0x10000) ? rpm<std::int32_t>(h, *pdata + 0x48).value_or(-1) : -1;
        add("Player", uid >= 0 ? 1 : 0, uid >= 0 ? "" : "not found (log in and re-run)");
    }
    {
        int n = jcount(VarpsDumpAllJson(pid));
        add("Player variables", n > 50 ? 1 : 0, std::to_string(n) + " tracked");
    }
    {
        int n = jcount(VarcsDumpAllJson(pid));
        add("Client variables", n > 0 ? 1 : 0, std::to_string(n) + " tracked");
    }
    // Interface tree: group range + the game frame (1477) present.
    {
        std::uint64_t gs = 0, ge = 0; iface_groups_range(h, *root, gs, ge);
        int groups = 0; bool frame = false;
        if (gs && ge > gs) {
            auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
            for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
                std::uint64_t ap2 = r64(g + 8);
                if (ap2 > 0x10000) {
                    int gid = rpm<std::int32_t>(h, ap2).value_or(-1);
                    if (gid > 0 && gid < 70000) { ++groups; if (gid == 1477) frame = true; }
                }
            }
        }
        add("Interfaces", (groups > 0 && frame) ? 1 : 0,
            (groups > 0 && frame) ? (std::to_string(groups) + " open") : "game window layout not readable");
    }
    // Skills block: xp values plausible.
    {
        int xp[29] = {};
        bool ok = SkillsXp(pid, xp);
        bool sane = ok; long long tot = 0; int nz = 0;
        for (int i = 0; i < 29 && sane; ++i) {
            if (xp[i] < 0 || xp[i] > 1000000000) sane = false;
            if (xp[i] > 0) ++nz;
            tot += xp[i];
        }
        add("Skills", (sane && nz > 0) ? 1 : 0,
            (sane && nz > 0) ? (std::to_string(nz) + " skills read") : "values look wrong");
    }
    {
        std::string inv = InventoryJson(pid);
        bool ok = inv.find("\"present\":true") != std::string::npos;
        add("Backpack", ok ? 1 : 0, ok ? "" : "not readable");
    }
    {
        std::string sc = SceneJson(pid, 2);
        bool ok = sc.find("\"players\":[{") != std::string::npos;
        add("Nearby players & NPCs", ok ? 1 : 0, ok ? "" : "not readable");
    }
    // World-to-screen view matrix: finite, non-zero.
    {
        auto cont = rpm<std::uint64_t>(h, *root + 0x19990);
        bool ok = false;
        if (cont && *cont > 0x10000) {
            auto idx = rpm<std::int32_t>(h, *cont + 0x70);
            auto arr = rpm<std::uint64_t>(h, *cont + 0x58);
            if (idx && *idx >= 0 && *idx < 64 && arr && *arr > 0x10000) {
                auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + 0x8);
                if (wv && *wv > 0x10000) {
                    // Same self-healing path the overlay uses, so this check exercises
                    // (and repairs) the real projection, not just a finite/nonzero read.
                    CamProbes probes;
                    auto worker = scene_worker(h, pid, *wv, &probes);
                    float m[16] = {};
                    ok = worker && read_view_matrix(h, pid, *root, *wv, probes, m);
                }
            }
        }
        add("Overlay camera", ok ? 1 : 0, ok ? "" : "not readable (in-world markers won't draw)");
    }
    // Companion-published channels (may be absent when the companion isn't loaded -> warn, not fail).
    {
        std::vector<RuntimeObj> objs;
        bool ok = ReadRuntimeObjects(pid, objs);
        add("Companion: world objects", ok ? 1 : 2, ok ? "" : "inactive (loads with the game client)");
    }
    {
        wchar_t name[64]; rtx::varc::MakeSectionName(pid, name);
        HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        bool ok = false;
        if (m) {
            auto* sh = reinterpret_cast<const rtx::varc::Share*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(rtx::varc::Share)));
            if (sh) { ok = (sh->magic == rtx::varc::kMagic && sh->version == rtx::varc::kVersion); UnmapViewOfFile((void*)sh); }
            CloseHandle(m);
        }
        add("Companion: live variables", ok ? 1 : 2, ok ? "" : "inactive (loads with the game client)");
    }
    // Cache parse health: per-surface decode sweep -- x/y records parsed plus the
    // opcode that breaks the read. After a game update a red row here names the
    // exact decoder (and opcode) that needs a new table, instead of "names look
    // wrong somewhere". Warn (not fail) at >=95% so a handful of oddball defs
    // don't redline the check.
    for (const auto& r : rtx::cache::CacheParseHealth()) {
        std::string note;
        int status;
        if (r.total == 0) { status = 2; note = "no data (cache not open yet?)"; }
        else {
            note = std::to_string(r.ok) + "/" + std::to_string(r.total) +
                   (r.sampled ? " sampled" : " parsed");
            if (r.stop_n > 0) {
                if (r.stop_op == 256)      note += ", " + std::to_string(r.stop_n) + " overrun mid-record";
                else if (r.stop_op == 257) note += ", " + std::to_string(r.stop_n) + " schema mismatch";
                else                       note += ", " + std::to_string(r.stop_n) + " break at opcode " + std::to_string(r.stop_op);
            }
            status = (r.ok == r.total) ? 1 : (r.ok * 20 >= r.total * 19 ? 2 : 0);
        }
        add(("Cache: " + r.name).c_str(), status, note);
    }
    return "{\"version\":\"" + json_escape(version) + "\",\"checks\":[" + checks + "]}";
}

HostInfo ReadHost() {
    return read_host_info();
}

std::string HostJson() {
    HostInfo h = ReadHost();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"cpu_name\":\"%s\",\"cpu_logical_cores\":%d,"
         "\"gpu_name\":\"%s\","
         "\"ram_total_mb\":%lld,\"ram_used_mb\":%lld}",
        json_escape(h.cpu_name).c_str(), h.cpu_logical_cores,
        json_escape(h.gpu_name).c_str(),
        h.ram_total_mb, h.ram_used_mb);
    return buf;
}

}  // namespace rtx::reader
