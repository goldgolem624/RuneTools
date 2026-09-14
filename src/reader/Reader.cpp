#include "Reader.h"
#include "BankCache.h"
#include "Hitmarks.h"
#include "BuffVars.h"
#include "EventZone.h"
#include "../cache/CacheReader.h"
#include "../shared/Log.h"
#include "../../companion/SceneOffsets.h" // client memory offsets shared with the companion
#include "../../companion/SceneShare.h"   // shared layout for runtime scene objects
#include "../../companion/GroundShare.h"  // shared layout for dropped ground items (type 3)
#include "../../companion/VarcShare.h"    // shared layout for live client-var values
#include "../../companion/RenderShare.h"  // launcher->companion render toggles
#include "../../companion/GpuTimeShare.h"
#include "../../companion/SpecialShare.h" // shared layout for transient render-pass highlights
#include "../../companion/NetProbeShare.h" // decoded server->client packet feed (companion framer hook)
#include "../../companion/EventShare.h"    // opcode-filtered event ring (the event channel)
#include "../../companion/ServerOps.h"     // per-build server opcodes (single source, see the header)

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <winternl.h>

#include <algorithm>
#include <fstream>
#include <atomic>
#include <deque>
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
#include <array>
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

// BUILD HISTORY. 940..949-5: one MainData layout. 950-1 (2026-09-07): MainData grew by 0x40 between +0x550 and +0x18D18.
// So every MainData-relative offset >= +0x18D18 moved +0x40 (0x19F68 -> 0x19FA8, 0x36040 -> 0x36080, 0x53588 -> 0x535C8); inner object layouts unchanged.

// MainData ctor anchor: `mov [rip+disp32], rax` publishes the MainData root pointer to a global.
// adjust = -32 walks back to the function start.
const std::uint8_t  kMainAnchorBytes[] =
    { 0x48, 0x2D, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x83 };
const std::size_t   kMainAnchorLen     = sizeof(kMainAnchorBytes);
constexpr int       kMainAnchorAdjust  = -32;
// Client cycle counter, u32 at root + this, 50/s (20ms units). From the CLIENTCLOCK engine op
// (scrambled 1708). NOT the server tick (kTickCounterOff, +1 per 600ms from packet 0xB4).
constexpr std::uint32_t kOffClientClock = 0x528;
constexpr std::uint32_t kOffWorld   = 0x199B0;   // root + this -> ptr -> +0x20 -> +0x8 = world(int)
constexpr std::uint32_t kOffStatus  = 0x19FA0;   // root + this = status(int8)
constexpr std::uint32_t kOffStats   = 0x19920;   // root + this -> stats container
constexpr std::uint32_t kStatsInner = 0x7618;    // stats container + this -> skill block
constexpr std::uint32_t kOffGE      = 0x19990;   // root + this -> ptr -> +0x10 = ge slot array
constexpr std::uint32_t kGEArrayPad = 0x10;      // bytes from slot-container start to slot 0
constexpr std::uint32_t kGESlotSize = 0x28;      // bytes per slot
constexpr int           kGESlotCount = 8;        // members get 8, non-members 3 (rest read as empty)

// Container manager: root + this -> mgr { start@+0x8, end@+0x10 }; entry stride 0x48, id@+0x10,
// items start@+0x18 / end@+0x20 (stride 0x8: item_id@+0, stack@+4). Bank (95) present only while open.
constexpr std::uint32_t kOffInvData      = 0x199C8;
constexpr std::uint32_t kContainerStride = 0x48;
constexpr int           kBankContainerId = 95;
constexpr int           kMetalBankContainerId = 858;   // Mining&Smithing metal bank (ores + bars)
constexpr int           kMaterialsContainerId = 885;   // Archaeology material storage (iface group 1313 is NOT the container id)
constexpr int           kGroupBankContainerId = 963;   // Group Ironman shared bank (964 is its inventory, not the storage)
constexpr int           kBaitBoxContainerId = 867;   // Anachronia Big Game Hunter bait box
constexpr int           kWorkbenchContainerId = 1008; // Archaeologist's workbench damaged-artefact storage
constexpr int           kNexusContainerId = 953;      // Necromancy nexus: the necrotic runes every nexus shares
constexpr int           kMaxContainers   = 64;
constexpr int           kMaxBankSlots    = 8192;

static int container_count(std::uint64_t cstart, std::uint64_t cend) {
    if (cend <= cstart || (cend - cstart) % kContainerStride) return 0;
    std::uint64_t n = (cend - cstart) / kContainerStride;
    if (n > 4096) return 0;
    return n > (std::uint64_t)kMaxContainers ? kMaxContainers : (int)n;
}

// Varp hashmap at MainData+0x36080: buckets@+0x8, divisor(i32)@+0x10; node = buckets[id % divisor],
// node id@+0, value@+0x8 (low 32), type tag@+0x20 (0/1 = int), next@+0x28. Unset varp = no node = 0.
constexpr std::uint32_t kOffVarpHash     = 0x36080;

// Varc (int + string) hashmap at store + kVarcHashOff, store = *(MainData + kOffVarcStore);
// same node layout as the varp map.
constexpr std::uint32_t kOffVarcStore    = 0x19920;
constexpr std::uint32_t kVarcHashOff     = 0x7630;
constexpr std::uint32_t kVarNodeNext     = 0x28;

// Tick anchor: `FF 81 F0 DB 00 00` = INC dword [RCX+0xDBF0], preceded in the same prologue by
// `48 8B 0D <disp32>` = MOV RCX, [rip+disp32] (the owner global).
const std::uint8_t  kTickIncBytes[] =
    { 0xFF, 0x81, 0xF0, 0xDB, 0x00, 0x00 };
const std::size_t   kTickIncLen     = sizeof(kTickIncBytes);
constexpr std::uint32_t kTickCounterOff = 0xDBF0;  // u32 at owner + this


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

// Renderer actually in use. vulkan-1.dll loads only in Vulkan mode; the overlay hooks
// opengl32!wglSwapBuffers, so anything but OpenGL means no in-game overlay.
std::string detect_gfx_mode(HANDLE h) {
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_64BIT)) return {};
    const DWORD n = needed / sizeof(HMODULE);
    bool gl = false, vk = false, dx = false;
    for (DWORD i = 0; i < n; ++i) {
        wchar_t base[260] = {};
        if (!GetModuleBaseNameW(h, mods[i], base, 260)) continue;
        if      (_wcsicmp(base, L"vulkan-1.dll") == 0) vk = true;
        else if (_wcsicmp(base, L"opengl32.dll") == 0) gl = true;
        else if (_wcsicmp(base, L"d3d11.dll")    == 0) dx = true;
    }
    if (vk) return "Vulkan";
    if (gl) return "OpenGL";
    if (dx) return "DirectX";
    // Renderer libraries load late; until then preferences.cfg next to the exe names the renderer.
    wchar_t exe[MAX_PATH] = {}; DWORD len = MAX_PATH;
    if (!QueryFullProcessImageNameW(h, 0, exe, &len)) return {};
    std::ifstream f(std::filesystem::path(exe).parent_path() / L"preferences.cfg");
    std::string line;
    while (f && std::getline(f, line)) {
        if (line.rfind("renderer=", 0) != 0) continue;
        std::string v = line.substr(9);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if (_stricmp(v.c_str(), "vulkan") == 0) return "Vulkan";
        if (_stricmp(v.c_str(), "opengl") == 0 || _stricmp(v.c_str(), "auto") == 0) return "OpenGL";
        return {};
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

// JagString (24-byte std::string-alike): tag@+0x17; bit 7 set = heap (char*@+0, len@+8),
// clear = inline (<=23 chars@+0, len = 0x17 - tag). UTF-8. Returns "" for empty/unreadable.
std::string read_jagstring(HANDLE h, std::uint64_t str, std::uint64_t max_len = 64) {
    auto tag = rpm<std::uint8_t>(h, str + 0x17);
    if (!tag) return {};
    std::uint64_t len = 0, data = 0;
    if (*tag & 0x80) {                       // heap
        auto ptr = rpm<std::uint64_t>(h, str + 0x00);
        auto n   = rpm<std::uint64_t>(h, str + 0x08);
        if (!ptr || !n || *ptr <= 0x10000) return {};
        data = *ptr;
        len  = *n;
    } else {                                 // inline
        if (*tag > 0x17) return {};          // impossible for a real string: torn read
        len  = 0x17u - *tag;
        data = str;
    }
    char buf[64];
    if (len == 0 || len > max_len || len > sizeof(buf)) return {};
    if (!rpm_bytes(h, data, buf, (SIZE_T)len)) return {};
    std::string out;
    for (std::uint64_t i = 0; i < len; ++i)
        if ((unsigned char)buf[i] >= 32) out.push_back(buf[i]);
    return out;
}

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

// Scan forward from start_rva for `48 89 05 <rel32>` (mov [rip+rel32], rax); returns the global's VA.
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

// Walk back from inc_rva for the nearest `48 8B 0D <rel32>` (MOV RCX, [rip+rel32]); returns its global VA.
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
    for (std::ptrdiff_t i = (std::ptrdiff_t)got - 7; i >= 0; --i) {
        if (body[i] == 0x48 && body[i+1] == 0x8B && body[i+2] == 0x0D) {
            std::int32_t disp = *reinterpret_cast<std::int32_t*>(&body[i+3]);
            return mod_base + scan_start + i + 7 + disp;
        }
    }
    return std::nullopt;
}

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


struct State {
    DWORD          pid              = 0;
    HANDLE         proc             = nullptr;
    std::uint64_t  mod_base         = 0;
    std::uint64_t  mod_size         = 0;
    std::string    client_version;
    std::string    display_name;
    std::string    gfx_mode;

    std::uint64_t  main_global_va   = 0;
    std::uint64_t  tick_owner_va    = 0;

    std::uint32_t  last_tick_value     = 0;
    double         last_tick_qpc_ms    = 0.0;
    double         last_dt_ms          = 0.0;
    std::uint32_t  current_tick        = 0;

    ULONGLONG      cpu_sample_wall_ms     = 0;
    ULONGLONG      cpu_sample_total_100ns = 0;
    double         cached_cpu_pct         = 0.0;

    // Character name (inline ASCII at data ptr +0x68); bank-cache key fallback when JX_DISPLAY_NAME is empty.
    std::string    character;

    // Bank (container 95): live while open, persisted to disk on change.
    std::vector<BankSlot> bank_slots;       // full array; slot = index
    std::vector<SlotVar>  bank_vars;        // per-item vars of those slots (sparse)
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
    // Necromancy nexus (container 953): the account's necrotic runes, shared by every nexus item.
    std::vector<BankSlot> nexus_slots;
    bool                  nexus_open      = false;
    long long             nexus_cached_at = 0;
    std::uint64_t         nexus_hash      = 0;
    // Archaeologist's workbench storage (container 1008): loaded only while the workbench UI is open.
    std::vector<BankSlot> workbench_slots;
    bool                  workbench_open      = false;
    long long             workbench_cached_at = 0;
    std::uint64_t         workbench_hash      = 0;
};

std::mutex                              g_mu;
std::unordered_map<DWORD, State>        g_states;
std::unordered_map<DWORD, std::pair<float, float>> g_pinfo_prevpos;
std::mutex                                         g_pinfo_mu;

void release(State& s) {
    if (s.proc) CloseHandle(s.proc);
    s = State{};
}

struct ProcSnap {
    HANDLE        h = nullptr;       // duplicated handle, closed in the dtor
    std::uint64_t mgva = 0;          // main_global_va: address of the MainData root pointer
    std::uint64_t mod_base = 0;      // rs2client.exe image base (for module-relative reads)
    std::uint64_t mod_size = 0;      // image size (for "points into the module" checks)
    ProcSnap() = default;
    ProcSnap(const ProcSnap&) = delete;
    ProcSnap& operator=(const ProcSnap&) = delete;
    ProcSnap(ProcSnap&& o) noexcept : h(o.h), mgva(o.mgva), mod_base(o.mod_base), mod_size(o.mod_size) { o.h = nullptr; }
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
        s.mod_size = it->second.mod_size;
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
    s.gfx_mode       = detect_gfx_mode(h);

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


LARGE_INTEGER     g_qpc_freq{};
std::atomic<bool> g_fast_started{ false };

double qpc_now_ms() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)g_qpc_freq.QuadPart;
}

void fast_tick_loop() {
    timeBeginPeriod(1);

    struct Probe  { DWORD pid; HANDLE proc; HANDLE dup; std::uint64_t owner_va;
                    std::uint32_t last_val; double last_qpc_ms; };
    struct Result { DWORD pid; HANDLE proc; std::uint32_t tick;
                    double now_ms; bool reset; bool advanced; double dt; };
    std::vector<Probe>  probes;
    std::vector<Result> results;

    while (true) {
        probes.clear();
        {
            std::lock_guard<std::mutex> lk(g_mu);
            probes.reserve(g_states.size());
            for (auto& [pid, s] : g_states) {
                if (!s.proc || !s.tick_owner_va) continue;
                HANDLE dup = nullptr;
                if (!DuplicateHandle(GetCurrentProcess(), s.proc, GetCurrentProcess(),
                                     &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) continue;
                probes.push_back({pid, s.proc, dup, s.tick_owner_va,
                                  s.last_tick_value, s.last_tick_qpc_ms});
            }
        }

        results.clear();
        results.reserve(probes.size());
        for (const auto& p : probes) {
            auto owner = rpm<std::uint64_t>(p.dup, p.owner_va);
            if (!owner || !*owner) continue;
            auto t = rpm<std::uint32_t>(p.dup, *owner + kTickCounterOff);
            if (!t) continue;
            Result r{p.pid, p.proc, *t, qpc_now_ms(), false, false, 0.0};
            if (p.last_qpc_ms == 0.0)      r.reset = true;          // first observation
            else if (*t < p.last_val)      r.reset = true;          // counter reset (relogin)
            else if (*t != p.last_val) {   r.advanced = true; r.dt = r.now_ms - p.last_qpc_ms; }
            results.push_back(r);
        }
        for (auto& p : probes) { if (p.dup) CloseHandle(p.dup); p.dup = nullptr; }

        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (const auto& r : results) {
                auto it = g_states.find(r.pid);
                if (it == g_states.end() || !it->second.proc || it->second.proc != r.proc) continue;
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

        // 10 ms keeps the tick-edge timestamp within a frame; with no attached client there is
        // nothing to probe, so idle at 4 Hz instead of spinning.
        Sleep(probes.empty() ? 250 : 10);
    }
}

void ensure_fast_thread_started() {
    bool expected = false;
    if (!g_fast_started.compare_exchange_strong(expected, true)) return;
    QueryPerformanceFrequency(&g_qpc_freq);
    std::thread(fast_tick_loop).detach();
}

Snapshot sample_one(State& s) {
    Snapshot snap;
    snap.pid            = s.pid;
    snap.client_version = s.client_version;
    snap.display_name   = s.display_name;
    if (s.gfx_mode.empty()) s.gfx_mode = detect_gfx_mode(s.proc);
    snap.gfx_mode       = s.gfx_mode;

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

            // Skills: stats container +0x7618 -> { count@+8, entries@+0x10 }; record stride 0x18,
            // +0x0C xp, +0x10 level, +0x14 boosted. 29 skills.
            auto stats = rpm<std::uint64_t>(s.proc, *root + kOffStats);
            if (stats && *stats > 0x10000) {
                auto block = rpm<std::uint64_t>(s.proc, *stats + kStatsInner);
                if (block && *block > 0x10000) {
                    auto count = rpm<std::uint8_t>(s.proc, *block + 0x8);
                    auto arr   = rpm<std::uint64_t>(s.proc, *block + 0x10);
                    if (count && arr && *arr > 0x10000) {
                        int n = (*count > 29) ? 29 : *count;
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
                // Slot: +0x00 status, +0x04 type, +0x08 item, +0x10 price(i64), +0x18 qty,
                // +0x1C filled, +0x20 fill_value(i64).
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

            // Character name: account (root+0x19FA8) JagString @+0x68; empty means "not set", so
            // fall back to Player (account+0x58) @+0xF8 as CHAT_PLAYERNAME (op 220) does.
            auto acct = rpm<std::uint64_t>(s.proc, *root + (kOffStatus + 0x8));
            if (acct && *acct > 0x10000) {
                std::string name = read_jagstring(s.proc, *acct + 0x68);
                if (name.empty()) {
                    auto player = rpm<std::uint64_t>(s.proc, *acct + 0x58);
                    if (player && *player > 0x10000)
                        name = read_jagstring(s.proc, *player + 0xF8);
                }
                if (!name.empty()) s.character = name;
            }
            const std::string& bank_key =
                !s.display_name.empty() ? s.display_name : s.character;

            bool found_bank = false, found_metal = false, found_mats = false, found_group = false, found_bait = false,
                 found_wb = false, found_nexus = false;
            auto cmgr = rpm<std::uint64_t>(s.proc, *root + kOffInvData);
            if (cmgr && *cmgr > 0x10000) {
                auto cstart = rpm<std::uint64_t>(s.proc, *cmgr + 0x8);
                auto cend   = rpm<std::uint64_t>(s.proc, *cmgr + 0x10);
                if (cstart && cend && *cend > *cstart) {
                    int n = container_count(*cstart, *cend);
                    std::vector<std::uint8_t> cbuf((std::size_t)n * kContainerStride);
                    if (n > 0 && rpm_bytes(s.proc, *cstart, cbuf.data(), cbuf.size())) {
                        auto capture = [&](const std::uint8_t* e, std::vector<BankSlot>& dst,
                                           std::uint64_t& hashRef, long long& cachedAtRef,
                                           const char* kind, std::vector<SlotVar>* varsDst = nullptr) {
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
                            // Per-item vars sit in a parallel array (0x38 per slot: +0x10 ptr array, +0x18 count;
                            // each entry {i32 key, pad, i32 value}). Read for the bank only; they are what tells an
                            // Essence of Finality's stored special apart, and they go with the items into the cache.
                            std::vector<SlotVar> vars;
                            if (varsDst) {
                                std::uint64_t xstart = *(const std::uint64_t*)(e + 0x30);
                                std::vector<std::uint8_t> xb((std::size_t)slots * 0x38);
                                if (xstart > 0x10000 && rpm_bytes(s.proc, xstart, xb.data(), xb.size())) {
                                    for (int si = 0; si < slots; ++si) {
                                        if (items[(std::size_t)si].item_id <= 0) continue;
                                        const std::uint8_t* X = xb.data() + (std::size_t)si * 0x38;
                                        int cnt = *(const std::int32_t*)(X + 0x18);
                                        std::uint64_t arr = *(const std::uint64_t*)(X + 0x10);
                                        if (cnt <= 0 || cnt > 32 || arr <= 0x10000) continue;
                                        std::uint64_t ptrs[32];
                                        if (!rpm_bytes(s.proc, arr, ptrs, (SIZE_T)cnt * 8)) continue;
                                        for (int j = 0; j < cnt; ++j) {
                                            if (ptrs[j] <= 0x10000) continue;
                                            std::int32_t kv[3];
                                            if (!rpm_bytes(s.proc, ptrs[j], kv, sizeof(kv))) continue;
                                            vars.push_back({ si, kv[0], kv[2] });
                                        }
                                    }
                                }
                                const std::uint8_t* vp = (const std::uint8_t*)vars.data();
                                for (std::size_t bi = 0; bi < vars.size() * sizeof(SlotVar); ++bi) {
                                    hsh ^= vp[bi]; hsh *= 1099511628211ull;
                                }
                                *varsDst = std::move(vars);
                            }
                            dst = std::move(items);
                            if (hsh != hashRef && !bank_key.empty() &&
                                WriteContainerCache(kind, bank_key, dst, varsDst)) {
                                hashRef     = hsh;
                                cachedAtRef = (long long)std::time(nullptr);
                                rtx::log::Client(s.pid, std::string(kind) + " changed; cached " +
                                                 std::to_string(slots) + " slots for " + bank_key);
                            }
                        };
                        for (int c = 0; c < n && !(found_bank && found_metal && found_mats && found_group && found_bait && found_wb && found_nexus); ++c) {
                            const std::uint8_t* e = cbuf.data() + (std::size_t)c * kContainerStride;
                            int cid = *(const std::int32_t*)(e + 0x10);
                            if (cid == kBankContainerId && !found_bank) {
                                found_bank = true;
                                capture(e, s.bank_slots, s.bank_hash, s.bank_cached_at, "bank", &s.bank_vars);
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
                            } else if (cid == kNexusContainerId && !found_nexus) {
                                found_nexus = true;
                                capture(e, s.nexus_slots, s.nexus_hash, s.nexus_cached_at, "nexus");
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
            s.nexus_open = found_nexus;
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

    snap.tick_count   = s.current_tick;
    snap.last_tick_ms = s.last_dt_ms;

    snap.in_world = snap.world > 0;
    return snap;
}

}  // namespace

std::vector<Snapshot> SampleAll() {
    ensure_fast_thread_started();

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
std::mutex        s_samples_mu;
std::string       s_samples_json;
std::atomic<bool> s_sampler_started{false};

std::string BuildSamplesJson() {
    auto snaps = SampleAll();
    std::string out = "[";
    char buf[1536];
    for (size_t i = 0; i < snaps.size(); ++i) {
        const auto& s = snaps[i];
        if (i) out.push_back(',');
        std::snprintf(buf, sizeof(buf),
            "{\"pid\":%u,\"client_version\":\"%s\","
             "\"in_world\":%s,\"world\":%d,\"status\":%d,\"status_label\":\"%s\","
             "\"display_name\":\"%s\",\"gfx_mode\":\"%s\","
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
            json_escape(s.gfx_mode).c_str(),
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

// message_game (op 0x21 on 950-1, 0x15 before) wire: [type: 1B smart if <0x80, else 2B BE + 0x8000]
// [u32][flags:1]; flags&1 -> NUL sender, flags&2 -> NUL channel name; then NUL message text.
struct ChatPkt {
    std::uint64_t seq;      // ring seq (monotonic per pid; the UI dedupes on it)
    std::uint64_t wall;     // epoch ms at capture (converted from the game's tick clock)
    int           type;     // message type id (109 = game/spam, 138 = broadcast, ...)
    std::string   name;     // sender ("" on system/game lines)
    std::string   chan;     // channel/clan name when the wire carried one, else ""
    std::string   text;     // raw message text, RS3 markup intact
};
std::mutex s_chat_mu;
struct ChatAcc { std::uint64_t drained = 0; std::uint64_t seen = 0; bool hook = false;
                 std::deque<ChatPkt> log; };
std::unordered_map<std::uint32_t, ChatAcc> s_chatAcc;

std::string chat_pkt_escape(const std::uint8_t* s, std::uint32_t n) {
    std::string o; o.reserve(n + 8);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint8_t c = s[i];
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; std::snprintf(b, 8, "\\u%04x", c); o += b; }
        else if (c < 0x80) o.push_back((char)c);
        else {
            int ext = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC2) ? 1 : 0;
            bool ok = ext > 0 && i + (std::uint32_t)ext < n;
            for (int k = 1; ok && k <= ext; ++k) ok = (s[i + k] & 0xC0) == 0x80;
            if (ok) { for (int k = 0; k <= ext; ++k) o.push_back((char)s[i + k]); i += ext; }
            else { char b[8]; std::snprintf(b, 8, "\\u%04x", c); o += b; }
        }
    }
    return o;
}

void drain_chat_rings() {
    std::vector<std::uint32_t> pids;
    { std::lock_guard<std::mutex> lk(g_mu);
      for (auto& kv : g_states) pids.push_back((std::uint32_t)kv.first); }
    const std::uint64_t nowTick = GetTickCount64();
    const std::uint64_t nowWall = (std::uint64_t)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    for (std::uint32_t pid : pids) {
        wchar_t name[64];
        rtx::netprobe::MakeSectionName(pid, name);
        HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (!h) continue;
        auto* sh = reinterpret_cast<const rtx::netprobe::Share*>(
            MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0));
        if (!sh) { CloseHandle(h); continue; }
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(sh, &mbi, sizeof(mbi)) == 0 ||
            mbi.RegionSize < sizeof(rtx::netprobe::Share)) {
            UnmapViewOfFile((void*)sh); CloseHandle(h); continue;
        }
        if (sh->magic == rtx::netprobe::kMagic && sh->version >= 3) {
            const std::uint64_t written = sh->chatWritten;
            std::lock_guard<std::mutex> lk(s_chat_mu);
            ChatAcc& acc = s_chatAcc[pid];
            acc.seen = sh->chatSeen;
            acc.hook = (sh->flags & 1) != 0;
            std::uint64_t oldest = written > rtx::netprobe::kChatRecords
                                   ? written - rtx::netprobe::kChatRecords : 0;
            std::uint64_t from = acc.drained > oldest ? acc.drained : oldest;
            for (std::uint64_t sq = from; sq < written; ++sq) {
                const rtx::netprobe::ChatRecord& r =
                    sh->chat[sq % rtx::netprobe::kChatRecords];
                if (r.seq != sq + 1) continue;                // slot lapped mid-read
                std::uint32_t n = r.kept;
                if (n > rtx::netprobe::kChatSnip) n = rtx::netprobe::kChatSnip;
                const std::uint8_t* b = r.data;
                std::uint32_t p = 0;
                if (n < 6) continue;
                int type;
                if (b[0] < 0x80) { type = b[0]; p = 1; }
                else { type = (((b[0] << 8) | b[1]) + 0x8000) & 0xFFFF; p = 2; }
                p += 4;                                        // u32 field (unused)
                if (p >= n) continue;
                std::uint8_t fl = b[p++];
                auto takeStr = [&](std::uint32_t& at) {
                    std::uint32_t s0 = at;
                    while (at < n && b[at] != 0) ++at;
                    std::string v = chat_pkt_escape(b + s0, at - s0);
                    if (at < n) ++at;                          // skip NUL
                    return v;
                };
                ChatPkt pk;
                pk.seq  = r.seq;
                pk.type = type;
                pk.wall = nowWall - (nowTick > r.tick ? nowTick - r.tick : 0);
                if (fl & 1) { pk.name = takeStr(p); if (fl & 2) pk.chan = takeStr(p); }
                pk.text = takeStr(p);
                if (!pk.text.empty()) acc.log.push_back(std::move(pk));
            }
            acc.drained = written;
            while (acc.log.size() > 5000) acc.log.pop_front();
        }
        UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
        CloseHandle(h);
    }
}

void sample_loop() {
    std::uint64_t lastChatDrain = 0;
    while (true) {
        std::string j = BuildSamplesJson();
        { std::lock_guard<std::mutex> lk(s_samples_mu); s_samples_json.swap(j); }
        const std::uint64_t t = GetTickCount64();
        if (t - lastChatDrain >= 1000) { lastChatDrain = t; drain_chat_rings(); }
        Sleep(200);   // the page refreshes at 250 ms; sampling faster than that is wasted
    }
}

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
                if (now - it->second.last_used > seconds(2)) { it = s_async.erase(it); continue; }   // a closed panel stops costing reads within 2 s
                jobs.emplace_back(it->first, it->second.build);
                ++it;
            }
        }
        for (auto& [key, build] : jobs) {
            std::string v;
            try { v = build(); } catch (const std::exception& ex) { rtx::log::Launcher("[reader] refresh: " + std::string(ex.what())); }
            std::lock_guard<std::mutex> lk(s_async_mu);
            auto it = s_async.find(key);
            if (it != s_async.end()) { it->second.value = std::move(v); it->second.has_value = true; }
        }
        Sleep(jobs.empty() ? 250 : 100);
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

std::string ReadAsync(const std::string& key, std::function<std::string()> build) {
    ensure_sampler_started();
    {
        std::lock_guard<std::mutex> lk(s_async_mu);
        auto& e = s_async[key];
        e.build = build;
        e.last_used = std::chrono::steady_clock::now();
        if (e.has_value) return e.value;
    }
    std::string v;
    try { v = build(); }
    catch (const std::exception& ex) { rtx::log::Launcher("[reader] " + key + ": " + ex.what()); v.clear(); }
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
        if (!s_samples_json.empty()) return s_samples_json;
    }
    std::string j = BuildSamplesJson();
    std::lock_guard<std::mutex> lk(s_samples_mu);
    if (s_samples_json.empty()) s_samples_json = j;
    return s_samples_json;
}

// One bank row: [slot, id, stack, "name"] plus, when the slot carries per-item vars, a fifth element
// [[key, value], ...] so consumers can tell one Essence of Finality (or charged item) from another.
static void append_slot_json(std::string& out, std::size_t slot, int iid, int stack, int& count,
                             const std::string* vars = nullptr) {
    char buf[96];
    if (count) out.push_back(',');
    std::snprintf(buf, sizeof(buf), "[%zu,%d,%d,\"", slot, iid, stack);
    out += buf;
    out += json_escape(rtx::cache::ItemName(iid));
    out += "\"";
    if (vars && !vars->empty()) { out += ",["; out += *vars; out += "]"; }
    out += "]";
    ++count;
}

static std::string cached_container_json(std::uint32_t pid, const char* cacheKey,
                                         std::vector<BankSlot> State::*slotsM,
                                         long long State::*cachedAtM, bool State::*openM,
                                         std::vector<SlotVar> State::*varsM = nullptr) {
    std::string           character;
    bool                  open = false;
    long long             cached_at = 0;
    std::vector<BankSlot> mem_slots;
    std::vector<SlotVar>  mem_vars;
    long long             mem_cached_at = 0;

    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) {
            const State& st = it->second;
            character = !st.display_name.empty() ? st.display_name : st.character;
            if (!(st.*slotsM).empty()) {
                mem_slots     = st.*slotsM;
                if (varsM) mem_vars = st.*varsM;
                mem_cached_at = st.*cachedAtM;
                open          = st.*openM;
            }
        }
    }

    const std::vector<BankSlot>* items = nullptr;
    const std::vector<SlotVar>*  vars  = nullptr;
    std::vector<BankSlot> from_disk;
    std::vector<SlotVar>  vars_disk;
    if (open && !mem_slots.empty()) {
        cached_at = mem_cached_at;
        items     = &mem_slots;
        vars      = &mem_vars;
    }
    if (!items) {
        BankCacheData d = ReadContainerCache(cacheKey, character);
        if (!d.slots.empty()) {
            from_disk = std::move(d.slots);
            vars_disk = std::move(d.vars);
            cached_at = d.cached_at;
            items     = &from_disk;
            vars      = &vars_disk;
        } else if (!mem_slots.empty()) {
            cached_at = mem_cached_at;
            items     = &mem_slots;
            vars      = &mem_vars;
        }
    }
    // per-slot "[k,v],[k,v]" fragments, keyed by slot
    std::unordered_map<std::size_t, std::string> varsBySlot;
    if (vars) {
        char vb[48];
        for (const auto& v : *vars) {
            if (v.slot < 0) continue;
            std::string& f = varsBySlot[(std::size_t)v.slot];
            std::snprintf(vb, sizeof(vb), "%s[%d,%d]", f.empty() ? "" : ",", v.key, v.value);
            f += vb;
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
        for (std::size_t slot = 0; slot < items->size(); ++slot) {
            const auto& bs = (*items)[slot];
            if (bs.item_id <= 0) continue;
            auto vf = varsBySlot.find(slot);
            append_slot_json(out, slot, bs.item_id, bs.stack, count, vf == varsBySlot.end() ? nullptr : &vf->second);
        }
    }
    std::snprintf(hdr, sizeof(hdr), "],\"count\":%d}", count);
    out += hdr;
    return out;
}

std::string BankJson(std::uint32_t pid) {
    return cached_container_json(pid, "bank", &State::bank_slots, &State::bank_cached_at, &State::bank_open, &State::bank_vars);
}
std::string MetalBankJson(std::uint32_t pid) {
    return cached_container_json(pid, "metalbank", &State::metalbank_slots, &State::metalbank_cached_at, &State::metalbank_open);
}
std::string MaterialsJson(std::uint32_t pid) {
    return cached_container_json(pid, "materials", &State::materials_slots, &State::materials_cached_at, &State::materials_open);
}
std::string BaitBoxJson(std::uint32_t pid) {
    return cached_container_json(pid, "baitbox", &State::baitbox_slots, &State::baitbox_cached_at, &State::baitbox_open);
}
// Necromancy nexus (container 953): the necrotic runes, cached to disk like the bank so they can be valued
// with no nexus loaded.
std::string NexusJson(std::uint32_t pid) {
    return cached_container_json(pid, "nexus", &State::nexus_slots, &State::nexus_cached_at, &State::nexus_open);
}
// Archaeologist's workbench (container 1008). Empty until the first workbench upgrade (varbit 61463).
std::string WorkbenchJson(std::uint32_t pid) {
    return cached_container_json(pid, "workbench", &State::workbench_slots, &State::workbench_cached_at, &State::workbench_open);
}
std::string GroupBankJson(std::uint32_t pid) {
    return cached_container_json(pid, "groupbank", &State::groupbank_slots, &State::groupbank_cached_at, &State::groupbank_open);
}

static std::string container_items_json(HANDLE h, std::uint64_t root, int container_id) {
    const char* kAbsent = "{\"present\":false,\"count\":0,\"cap\":0,\"items\":[]}";
    auto cmgr = rpm<std::uint64_t>(h, root + kOffInvData);
    if (!cmgr || *cmgr <= 0x10000) return kAbsent;
    auto cstart = rpm<std::uint64_t>(h, *cmgr + 0x8);
    auto cend   = rpm<std::uint64_t>(h, *cmgr + 0x10);
    if (!cstart || !cend || *cstart <= 0x10000 || *cend <= *cstart) return kAbsent;
    int ncont = container_count(*cstart, *cend);
    for (int c = 0; c < ncont; ++c) {
        std::uint64_t e = *cstart + (std::uint64_t)c * kContainerStride;
        if (rpm<std::int32_t>(h, e + 0x10).value_or(-1) != container_id) continue;
        auto istart = rpm<std::uint64_t>(h, e + 0x18);
        auto iend   = rpm<std::uint64_t>(h, e + 0x20);
        if (!istart || !iend || *istart <= 0x10000 || *iend < *istart)
            return "{\"present\":true,\"count\":0,\"cap\":0,\"items\":[]}";
        int cap = (int)((*iend - *istart) / 0x8);
        if (cap > kMaxBankSlots) cap = kMaxBankSlots;
        std::string items; int count = 0;
        for (int s = 0; s < cap; ++s) {
            std::uint64_t slotAddr = *istart + (std::uint64_t)s * 0x8;
            int iid   = rpm<std::int32_t>(h, slotAddr).value_or(0);
            if (iid <= 0) continue;
            int stack = rpm<std::int32_t>(h, slotAddr + 0x4).value_or(0);
            append_slot_json(items, (std::size_t)s, iid, stack, count);
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
    int ncont = container_count(*cstart, *cend);
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

// POF pens (containers 851-857): slot item = species; inv-var index 2 & 0x1F = trait (enum 14340).
std::string PofJson(std::uint32_t pid) {
    const char* kEmpty = "{\"pens\":[]}";
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
                ncont = container_count(cstart, cend);
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
                if (ns > 64) ns = 64;
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

// Per-slot Extra_ints: entry+0x30 -> slot*0x38 -> {ptrArr@+0x10, count@+0x18}; ptr = key@+0, value@+8.
std::string ItemExtraIntsJson(std::uint32_t pid, int container_id, int item_id, int slot_want) {
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
    int ncont = container_count(*cstart, *cend);
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
            if (slot_want >= 0 && s != slot_want) continue;
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
            char shdr[48];
            std::snprintf(shdr, sizeof(shdr), "{\"present\":true,\"slot\":%d,\"key\":[", s);
            std::string out = shdr;
            for (int k = 0; k < 16; ++k) { std::snprintf(buf, sizeof(buf), "%s%d", k ? "," : "", byKey[k]); out += buf; }
            out += "],\"pos\":["; out += pos; out += "]}";
            return out;
        }
        return kAbsent;
    }
    return kAbsent;
}
std::string EquipmentJson(std::uint32_t pid) { return container_json_for(pid, 94); }

static bool read_varp_found(HANDLE h, std::uint64_t root, int varp_id, int& out) {
    out = 0;
    if (varp_id < 0) return false;
    std::uint64_t hashRoot = root + kOffVarpHash;
    auto ba_o  = rpm<std::uint64_t>(h, hashRoot + 0x8);
    auto div_o = rpm<std::int32_t>(h, hashRoot + 0x10);
    if (!ba_o || !div_o) return false;
    std::uint64_t ba = *ba_o; int div = *div_o;
    if (ba <= 0x10000 || div <= 0 || div > 2000000) return false;   // stale-offset guard
    auto bucket = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varp_id % div) * 8);
    if (!bucket) return false;
    std::uint64_t node = *bucket;
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (rpm<std::int32_t>(h, node).value_or(-1) == varp_id) {
            out = rpm<std::int32_t>(h, node + 0x8).value_or(0);
            return true;
        }
        node = rpm<std::uint64_t>(h, node + kVarNodeNext).value_or(0);
    }
    return true;
}
static int read_varp(HANDLE h, std::uint64_t root, int varp_id) {
    int v = 0;
    read_varp_found(h, root, varp_id, v);
    return v;
}

static constexpr std::size_t kVarcStrCap = 1024;

// EASTL basic_string: flag@+0x17; bit 7 set = heap {char*@+0, size@+8, cap@+0x10}, clear = SSO with
// (0x17 - flag) chars inline at +0. Same class the companion reads off the CS2 VM string stack.
static bool read_eastl_string(HANDLE h, std::uint64_t strbase, std::string& out) {
    out.clear();
    std::uint8_t flag = rpm<std::uint8_t>(h, strbase + 0x17).value_or(0);
    std::size_t size; std::uint64_t src;
    if (flag & 0x80) {                                        // heap
        src  = rpm<std::uint64_t>(h, strbase + 0x0).value_or(0);
        std::uint64_t sz = rpm<std::uint64_t>(h, strbase + 0x8).value_or(0);
        if (src <= 0x10000 || src > 0x00007FFFFFFFFFFFull || sz == 0 || sz > 0x2000) return false;
        size = (std::size_t)std::min<std::uint64_t>(sz, kVarcStrCap);
    } else {                                                  // SSO
        if (flag == 0 || flag > 0x16) return false;
        size = (std::size_t)(0x17 - flag);
        src  = strbase;
    }
    std::string tmp; tmp.resize(size);
    if (!rpm_bytes(h, src, tmp.data(), size)) return false;
    out.swap(tmp);
    return true;
}

static std::optional<std::uint64_t> varc_node(HANDLE h, std::uint64_t client, int varc_id) {
    if (varc_id < 0 || client <= 0x10000) return std::nullopt;
    std::uint64_t store = rpm<std::uint64_t>(h, client + kOffVarcStore).value_or(0);
    if (store <= 0x10000) return std::nullopt;
    std::uint64_t hm = store + kVarcHashOff;
    std::uint64_t ba = rpm<std::uint64_t>(h, hm + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hm + 0x10).value_or(0);
    if (ba <= 0x10000 || ba > 0x00007FFFFFFFFFFFull || div <= 0 || div > 2000000) return std::nullopt;
    std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varc_id % div) * 8).value_or(0);
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (rpm<std::int32_t>(h, node).value_or(-1) == varc_id) return node;
        node = rpm<std::uint64_t>(h, node + kVarNodeNext).value_or(0);
    }
    return std::nullopt;
}

static int read_varc(HANDLE h, std::uint64_t client, int varc_id) {
    auto n = varc_node(h, client, varc_id);
    return n ? rpm<std::int32_t>(h, *n + 0x8).value_or(0) : 0;
}

static bool read_varc_found(HANDLE h, std::uint64_t client, int varc_id, int& out_val) {
    out_val = 0;
    auto n = varc_node(h, client, varc_id);
    if (!n) return false;
    out_val = rpm<std::int32_t>(h, *n + 0x8).value_or(0);
    return true;
}

// Varc string: int and string varcs share one map with no type tag in the node (type is in the var
// definition); the value union at node+8 is an EASTL string. false for absent / int-typed / garbage.
static bool read_varc_str(HANDLE h, std::uint64_t client, int varc_id, std::string& out) {
    out.clear();
    auto n = varc_node(h, client, varc_id);
    return n ? read_eastl_string(h, *n + 0x8, out) : false;
}

std::string LocMorphsJson(std::uint32_t pid, const std::string& ids_csv) {
    auto ps = snap_proc(pid);
    if (!ps) return "[]";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "[]";
    std::string out = "["; bool first = true;
    std::size_t i = 0, n = ids_csv.size();
    while (i < n) {
        while (i < n && (ids_csv[i] < '0' || ids_csv[i] > '9')) ++i;
        int id = 0; bool any = false;
        while (i < n && ids_csv[i] >= '0' && ids_csv[i] <= '9') {
            id = id * 10 + (ids_csv[i] - '0'); any = true; ++i;
            if (id > 10000000) { id = 0; any = false; break; }
        }
        if (!any) continue;
        int vb = -1, vp = -1, defc = -1;
        std::vector<int> variants;
        if (!rtx::cache::GetLocMorph(id, vb, vp, defc, variants) || variants.empty()) {
            out += (first ? "" : ",");
            out += "{\"id\":" + std::to_string(id) + ",\"static\":1}";
            first = false;
            continue;
        }
        int value = -1;
        if (vb >= 0) {
            int wvp = -1, lsb = -1, msb = -1;
            if (rtx::cache::GetVarbit(vb, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
                int raw = read_varp(h, *root, wvp);
                int width = msb - lsb + 1;
                unsigned mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
                value = (int)(((unsigned)raw >> lsb) & mask);
            }
        } else if (vp >= 0) {
            value = read_varp(h, *root, vp);
        }
        int child = -1;
        std::string name;
        if (value >= 0) {
            child = (value < (int)variants.size()) ? variants[(std::size_t)value] : defc;
            if (child >= 0) name = rtx::cache::GetLoc(child).name;
        }
        out += (first ? "" : ",");
        out += "{\"id\":" + std::to_string(id) + ",\"vb\":" + std::to_string(vb) +
               ",\"vp\":" + std::to_string(vp) + ",\"value\":" + std::to_string(value) +
               ",\"child\":" + std::to_string(child) + ",\"name\":\"" + json_escape(name) + "\"}";
        first = false;
    }
    out += "]";
    return out;
}

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
            if (id > 1000000) { id = 0; any = false; break; }
        }
        if (!any) continue;
        int v = read_varp(h, *root, id);
        std::snprintf(buf, sizeof(buf), "%s\"%d\":%d", first ? "" : ",", id, v);
        out += buf; first = false;
    }
    out += "}";
    return out;
}

// CSV of varbit ids -> {"<id>":value,..}; each varbit (cache idx2/arch69) = varp + [lsb,msb]. Unset -> 0.
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

// Member is engine state, not a var: PLAYERMEMBER op = acct = *(MainData+0x19FA8), *(u8*)(acct+0x28) != 0. Premier = varbit 50572 (varp 10287 bit 5) ANDed with member as script15757 does (legacy varp 12864 reads 0).
// idleLogoutSeconds is derived and server-enforced: 5 min base, +5 members, +5 Jagex account, cap 15, out of combat only.
constexpr std::uint32_t kOffAccount      = 0x19FA8;   // MainData -> account / user-detail object
constexpr std::uint32_t kOffAcctIsMember = 0x28;      // u8, nonzero = members
constexpr std::uint32_t kOffAcctExpiry   = 0x30;      // u64, raw value LOBBY_MEMBERSHIP divides down
constexpr int           kPremierVarbit   = 50572;

constexpr std::uint32_t kOffInputReporter = 0x198B0;  // MainData -> input reporter
constexpr std::uint32_t kOffRepPointerA   = 0x28;     // u64 ms, last pointer flush (recorder A)
constexpr std::uint32_t kOffRepPointerB   = 0x50;     // u64 ms, last pointer flush (recorder B)
constexpr std::uint32_t kOffRepKeyboard   = 0x2858;   // u64 ms, last key batch (0 = none yet)


// Client state that lives outside the var stores, from the engine op handlers (950-1):
// CUTSCENE ops read [MainData+0x19A18]: +0x150 = current cutscene id, -1 when none.
// CLIENTOPTION_GET (0x1401BE8F0): option objects at [[MainData+0x535D0]+0x2DA8 + id*8], value i32 at
// +0x18 (option 39 is a byte), 44 options; the op adds 1 to option 28. Option names are not in the
// client; the ids are what the settings scripts pass to CLIENTOPTION_GET/SET. (The op the export
// calls TEXTINPUT_ISFOCUSED reads [[MainData+0x19FA8]+0x14], which client script 14944 feeds to
// DATE_RUNEDAY_TODATE for an age check: that field is the account's date of birth, not a focus flag.)
std::string ClientStateJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::string out = "{";
    int cutscene = -1;
    if (auto cs = rpm<std::uint64_t>(h, *root + 0x19A18); cs && *cs > 0x10000) cutscene = rpm<std::int32_t>(h, *cs + 0x150).value_or(-1);
    out += "\"cutscene\":" + std::to_string(cutscene) + ",\"inCutscene\":" + std::string(cutscene != -1 ? "true" : "false");
    out += ",\"options\":[";
    if (auto opt = rpm<std::uint64_t>(h, *root + 0x535D0); opt && *opt > 0x10000) {
        for (int i = 0; i < 44; ++i) {
            auto o = rpm<std::uint64_t>(h, *opt + 0x2DA8 + (std::uint64_t)i * 8);
            long long v = 0;
            if (o && *o > 0x10000) v = (i == 39) ? (long long)rpm<std::uint8_t>(h, *o + 0x18).value_or(0) : (long long)rpm<std::int32_t>(h, *o + 0x18).value_or(0);
            if (i == 28) v += 1;
            out += (i ? "," : "") + std::to_string(v);
        }
    }
    out += "]}";
    return out;
}

std::string MembershipJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";

    std::uint64_t acct = rpm<std::uint64_t>(h, *root + kOffAccount).value_or(0);
    bool resolved = acct > 0x10000 && acct <= 0x00007FFFFFFFFFFFull;
    int member = 0; std::uint64_t expiry = 0;
    if (resolved) {
        member = rpm<std::uint8_t>(h, acct + kOffAcctIsMember).value_or(0) ? 1 : 0;
        expiry = rpm<std::uint64_t>(h, acct + kOffAcctExpiry).value_or(0);
    }

    int premier = 0, wvp = -1, lsb = -1, msb = -1;
    if (rtx::cache::GetVarbit(kPremierVarbit, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
        int raw = read_varp(h, *root, wvp);
        unsigned mask = (msb - lsb + 1 >= 32) ? 0xFFFFFFFFu : ((1u << (msb - lsb + 1)) - 1);
        premier = (((unsigned)raw >> lsb) & mask) ? 1 : 0;
    }
    premier = (premier && member) ? 1 : 0;

    int jagex = read_target_env(h, L"JX_DISPLAY_NAME").empty() ? 0 : 1;
    int budget = 300 + (member ? 300 : 0) + (jagex ? 300 : 0);

    long long idleMs = -1;
    std::uint64_t rep = rpm<std::uint64_t>(h, *root + kOffInputReporter).value_or(0);
    if (rep > 0x10000 && rep <= 0x00007FFFFFFFFFFFull) {
        // The flush stamps are GetTickCount64 values (system uptime, ms). The client keeps its own
        // copy next to the MainData slot, but that slot moved between builds (+8 on 949-5, +0x10 on
        // 950-1); the launcher runs on the same machine, so its own tick clock is the same domain.
        std::uint64_t now = (std::uint64_t)GetTickCount64();
        std::uint64_t pa  = rpm<std::uint64_t>(h, rep + kOffRepPointerA).value_or(0);
        std::uint64_t pb  = rpm<std::uint64_t>(h, rep + kOffRepPointerB).value_or(0);
        std::uint64_t kb  = rpm<std::uint64_t>(h, rep + kOffRepKeyboard).value_or(0);
        std::uint64_t last = pa > pb ? pa : pb;
        if (kb > last) last = kb;
        if (now && last && now >= last && now - last < 86400000ull) idleMs = (long long)(now - last);
    }

    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "{\"resolved\":%s,\"member\":%d,\"premier\":%d,\"jagexAccount\":%d,\"tier\":%d,"
                  "\"idleLogoutSeconds\":%d,\"idleMs\":%lld,\"expiryRaw\":%llu}",
                  resolved ? "true" : "false", member, premier, jagex,
                  member ? (premier ? 2 : 1) : 0,
                  budget, idleMs,
                  (unsigned long long)expiry);
    return buf;
}

// Every set varp from the MainData+0x36080 hashmap, keyed "4:<id>" (scope 4 = varp). Direct poll stays
std::string VarpsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t hashRoot = *root + kOffVarpHash;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";   // real bucket counts are a few thousand

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
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
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

// All set varc-ints from the global client-var hashmap (store+0x7630, store = *(MainData+0x19920)),
// keyed "5:<id>". Node: id@+0, value@+8, next@+0x28.
std::string VarcsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + kVarcHashOff;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";   // real bucket counts are a few thousand

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
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
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

// Domain stores bound by the script context binder (950-1 fn 0x14008df60); domains 3, 4 and 8 are never bound:
//   0 player       MainData+0x19fb8            (varp manager; hashmap at +0x36080)
//   2 client       [MainData+0x19920]+0x7620   (varc object; hashmap at +0x7630)
//   6 clan         [[MainData+0x19920]+0x77b0] (null until clan join)
//   7 clansettings [MainData+0x19888] + slot*16
//   9 playergroup  [[MainData+0x19948]+8]+0x28 (null without a group)
//   1 npc          script target entity +0x130 (only while a script runs on that NPC)
// Table (vtable rs2client+0xB609C0 on the 950-1 OpenGL build, +0xC6D818 on the 950-1 Vulkan build; the
// player table at MainData+0x36078 carries no vtable): buckets@+0x10, count@+0x18, elements@+0x20;
// node = {u32 id, value union@+8, type byte@+0x20 (0 int, 1 long, 2 string), next@+0x28}.
struct DomStore { const char* src; std::uint64_t obj; std::uint64_t table; int div; int count; bool typed; };

static bool dom_table(HANDLE h, std::uint64_t table, int& div, int& count) {
    div = rpm<std::int32_t>(h, table + 0x18).value_or(0);
    count = rpm<std::int32_t>(h, table + 0x20).value_or(0);
    std::uint64_t ba = rpm<std::uint64_t>(h, table + 0x10).value_or(0);
    return ba > 0x10000 && div > 0 && div <= 131072 && count >= 0 && count <= div * 8;
}

static void dom_dump(HANDLE h, std::uint64_t table, int domain, std::string& out, bool& first) {
    int div = 0, count = 0;
    if (!dom_table(h, table, div, count)) return;
    std::uint64_t ba = rpm<std::uint64_t>(h, table + 0x10).value_or(0);
    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return;
    char buf[96];
    for (int b = 0; b < div; ++b) {
        std::uint64_t node = buckets[(std::size_t)b];
        for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
            std::uint8_t nb[0x30];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
            int id = *reinterpret_cast<const std::int32_t*>(nb);
            int type = nb[0x20];
            if (id >= 0 && id < 100000) {
                if (type == 0)      std::snprintf(buf, sizeof(buf), "%s\"%d:%d\":%d", first ? "" : ",", domain, id, *reinterpret_cast<const std::int32_t*>(nb + 8));
                else if (type == 1) std::snprintf(buf, sizeof(buf), "%s\"%d:%d\":\"%lld\"", first ? "" : ",", domain, id, (long long)*reinterpret_cast<const std::int64_t*>(nb + 8));
                else                std::snprintf(buf, sizeof(buf), "%s\"%d:%d\":\"(string)\"", first ? "" : ",", domain, id);
                out += buf; first = false;
            }
            node = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
        }
    }
}

std::string VarDomainStoresJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    const std::uint64_t kTableVt = ps.mod_base ? ps.mod_base + 0xB609C0 : 0;
    // The store class vtable sits at a different RVA in each rs2client.exe build (OpenGL and Vulkan
    // are separate binaries), so beyond the pinned RVA accept any vtable that lies in the image and
    // whose first slots are code pointers into the image. No RTTI is present to name the class.
    auto in_image = [&](std::uint64_t a) {
        return ps.mod_base && ps.mod_size && a >= ps.mod_base && a < ps.mod_base + ps.mod_size;
    };
    auto vt_ok = [&](std::uint64_t vt) {
        if (kTableVt && vt == kTableVt) return true;
        if (!in_image(vt)) return false;
        for (int i = 0; i < 5; ++i)
            if (!in_image(rpm<std::uint64_t>(h, vt + (std::uint64_t)i * 8).value_or(0))) return false;
        return true;
    };
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    std::uint64_t clan  = store > 0x10000 ? rpm<std::uint64_t>(h, store + 0x77b0).value_or(0) : 0;
    std::uint64_t grp   = rpm<std::uint64_t>(h, *root + 0x19948).value_or(0);
    std::uint64_t grpObj = grp > 0x10000 ? rpm<std::uint64_t>(h, grp + 8).value_or(0) : 0;
    std::uint64_t clanReg = rpm<std::uint64_t>(h, *root + 0x19888).value_or(0);
    std::uint64_t cs0 = clanReg > 0x10000 ? rpm<std::uint64_t>(h, clanReg).value_or(0) : 0;
    std::uint64_t cs1 = clanReg > 0x10000 ? rpm<std::uint64_t>(h, clanReg + 16).value_or(0) : 0;

    std::string out = "{\"stores\":{"; char buf[256]; bool firstS = true;
    auto emit = [&](const char* key, const char* src, std::uint64_t obj, std::uint64_t table) {
        int div = 0, count = 0; bool ok = obj > 0x10000 && dom_table(h, table, div, count);
        std::uint64_t vt = obj > 0x10000 ? rpm<std::uint64_t>(h, table).value_or(0) : 0;
        std::snprintf(buf, sizeof(buf), "%s\"%s\":{\"src\":\"%s\",\"ptr\":\"0x%llx\",\"live\":%s,\"div\":%d,\"count\":%d,\"vt\":%s,\"vt_rva\":\"0x%llx\"}",
                      firstS ? "" : ",", key, src, (unsigned long long)obj, ok ? "true" : "false", ok ? div : 0, ok ? count : 0,
                      vt_ok(vt) ? "true" : "false", (unsigned long long)(in_image(vt) ? vt - ps.mod_base : 0));
        out += buf; firstS = false;
    };
    emit("0",  "MainData+0x19fb8 (varp manager)", *root + 0x19fb8, *root + kOffVarpHash - 8);
    emit("2",  "[MainData+0x19920]+0x7620 (varc object)", store > 0x10000 ? store + 0x7620 : 0, store + 0x7620 + 8);
    emit("6",  "[[MainData+0x19920]+0x77b0]", clan, clan);
    emit("7",  "[[MainData+0x19888]+0] (clan settings slot 0; table offset not pinned)", cs0, cs0);
    emit("7b", "[[MainData+0x19888]+16] (clan settings slot 1; table offset not pinned)", cs1, cs1);
    emit("9",  "[[MainData+0x19948]+8]+0x28", grpObj > 0x10000 ? grpObj + 0x28 : 0, grpObj + 0x28);
    out += "},\"vars\":{"; bool first = true;
    if (clan > 0x10000) dom_dump(h, clan, 6, out, first);
    if (grpObj > 0x10000) dom_dump(h, grpObj + 0x28, 9, out, first);
    out += "}}";
    return out;
}

// Selected varcs read as i64 from the value union at +0x08 (e.g. death interface prices, varcs
// 4829-4875 / 7109-7111 / 4876). Emitted as JSON strings: values can exceed double precision.
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
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + kVarcHashOff;
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
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
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

std::string VarcIntsJson(std::uint32_t pid, const std::string& ids_csv) {
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
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + kVarcHashOff;
    std::uint64_t ba = rpm<std::uint64_t>(h, hashRoot + 0x8).value_or(0);
    int div = rpm<std::int32_t>(h, hashRoot + 0x10).value_or(0);
    if (ba <= 0x10000 || div <= 0 || div > 131072) return "{}";

    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return "{}";

    std::string out = "{"; bool first = true; char buf[48];
    for (int b = 0; b < div; ++b) {
        std::uint64_t node = buckets[(std::size_t)b];
        for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
            std::uint8_t nb[0x30];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
            int id = *reinterpret_cast<const std::int32_t*>(nb + 0x00);
            std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
            if (want.count(id)) {
                int val = *reinterpret_cast<const std::int32_t*>(nb + 0x08);
                std::snprintf(buf, sizeof(buf), "%s\"%d\":%d", first ? "" : ",", id, val);
                out += buf; first = false;
            }
            node = next;
        }
    }
    out += "}";
    return out;
}

// Long-typed varps (e.g. vp137, vp140, vp13483, vp9458): full i64 from the node's value union
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
            node = rpm<std::uint64_t>(h, node + kVarNodeNext).value_or(0);
        }
    }
    out += "}";
    return out;
}

static void append_json_str(std::string& out, const std::string& s) {
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += (char)ch; }
        else if (ch >= 0x20 && ch <= 0x7e) out += (char)ch;
        else out += ' ';
    }
}

// (e.g. varc 2251 = interface-1177 hover tooltip, 1691 = mouseover text).
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

// All varc-strings from the global client-var hashmap (store+0x7630), keyed "2:<id>". The node has no
std::string VarcStringsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + kVarcHashOff;
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
            std::uint64_t next = rpm<std::uint64_t>(h, node + kVarNodeNext).value_or(0);
            if (id >= 0 && id < 100000 && read_eastl_string(h, node + 0x8, val) && !val.empty()) {
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

// Inbound opcode descriptor table (base rs2client+0xC70BB0, opcodes 0x00..0xDE on 950-1). Entry = ptr to 0x50-byte descriptor {+0x00 int opcode, +0x04 int length, +0x10 vtable, handler at vtable+0x10}.
// length >= 0 fixed, -1 var-byte, -2 var-short. Resolution is RVA-first, then a structural scan validated by desc[op].opcode == op.
namespace {
constexpr std::uint64_t kOpTableRva   = 0xC70BB0;   // module global holding the table base
constexpr int           kOpMax        = 0xDE;       // highest valid opcode (framer bails above; 0xE5 through 949-5)
constexpr int           kOpCount      = kOpMax + 1; // 223
constexpr std::uint64_t kDescOpcodeOff = 0x00;
constexpr std::uint64_t kDescLenOff    = 0x04;
constexpr std::uint64_t kDescVtblOff   = 0x10;
constexpr std::uint64_t kVtblHandlerOff = 0x10;

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

std::uint64_t resolve_optable(HANDLE h, std::uint64_t base, std::uint64_t size) {
    if (auto t = rpm<std::uint64_t>(h, base + kOpTableRva); t && optable_valid(h, *t))
        return *t;
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

std::string ServerPacketsJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"ok\":false,\"reason\":\"not attached\"}";
    HANDLE h = ps.h;
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
        // handler fn = *(*(desc+0x10) + 0x10), reported as an RVA
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

std::string ServerPacketFeedJson(std::uint32_t pid, std::uint64_t since) {
    wchar_t name[64];
    rtx::netprobe::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return "{\"ok\":false,\"reason\":\"companion not loaded / panel not built in\"}";
    auto* sh = reinterpret_cast<const rtx::netprobe::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0));
    if (!sh) { CloseHandle(h); return "{\"ok\":false,\"reason\":\"map failed\"}"; }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(sh, &mbi, sizeof(mbi)) == 0 ||
        mbi.RegionSize < offsetof(rtx::netprobe::Share, chatWritten)) {
        UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh)); CloseHandle(h);
        return "{\"ok\":false,\"reason\":\"share undersized\"}";
    }
    std::string out;
    if (sh->magic != rtx::netprobe::kMagic || sh->version < 2) {
        out = "{\"ok\":false,\"reason\":\"share magic/version mismatch\"}";
    } else {
        const std::uint64_t written = sh->written;
        const std::uint64_t oldest  = written > rtx::netprobe::kMaxRecords
                                     ? written - rtx::netprobe::kMaxRecords : 0;
        std::uint64_t from = since > oldest ? since : oldest;   // clamp to what still lives
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

bool ServerPacketFeedEnable(std::uint32_t pid, bool on) {
    wchar_t name[64];
    rtx::netprobe::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::netprobe::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0));
    if (!sh) { CloseHandle(h); return false; }
    bool ok = (sh->magic == rtx::netprobe::kMagic && sh->version >= 2);
    if (ok) sh->enable = on ? (std::uint32_t)GetTickCount64() : 0u;
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return ok;
}

namespace {
const char* const kEvSkills[29] = { "Attack", "Defence", "Strength", "Constitution", "Ranged",
    "Prayer", "Magic", "Cooking", "Woodcutting", "Fletching", "Fishing", "Firemaking", "Crafting",
    "Smithing", "Mining", "Herblore", "Agility", "Thieving", "Slayer", "Farming", "Runecrafting",
    "Hunter", "Construction", "Summoning", "Dungeoneering", "Divination", "Invention",
    "Archaeology", "Necromancy" };

void ev_hex(std::string& o, const std::uint8_t* b, std::uint32_t n) {
    static const char* hexd = "0123456789abcdef";
    for (std::uint32_t i = 0; i < n; ++i) { o += hexd[b[i] >> 4]; o += hexd[b[i] & 0xF]; }
}
std::uint32_t ev_u32be(const std::uint8_t* b) {
    return ((std::uint32_t)b[0] << 24) | ((std::uint32_t)b[1] << 16) | ((std::uint32_t)b[2] << 8) | b[3];
}

bool ev_decode(std::string& o, int op, const std::uint8_t* b, std::uint32_t n, int len) {
    char t[128];
    { const int sub = rtx::evzone::subForOpcode(op); if (sub >= 0) return rtx::evzone::subJson(o, sub, b, n); }
    if (rtx::evzone::topJson(o, op, b, n)) return true;
    switch (op) {
    case rtx::sops::kSkillUpdate: {   // 950-1: [skill: -b0][level: -b1][xp: u32 BE]  (949: [xp LE][level b4+0x80][skill -b5])
        if (n < 6) return false;
        const int sk = (256 - b[0]) & 0xFF, level = (256 - b[1]) & 0xFF;
        const std::uint32_t xp = ev_u32be(b + 2);
        std::snprintf(t, sizeof(t), "\"kind\":\"skill_update\",\"skill\":%d,\"name\":\"%s\",\"level\":%d,\"xp\":%u",
                      sk, sk < 29 ? kEvSkills[sk] : "", level, xp);
        o += t; return true;
    }
    case rtx::sops::kContainerUpdate: {   // [container: u16 BE][flags: u8] then per slot [slot smart][itemId+1: u24 BE][qty u8 | 0xFF u32 BE][variant if flags&2]
        if (n < 3) return false;
        std::uint32_t p = 0;
        const int cont = (b[0] << 8) | b[1]; p = 2;
        const int flags = b[p++];
        std::snprintf(t, sizeof(t), "\"kind\":\"container_update\",\"container\":%d,\"flags\":%d,\"slots\":[", cont, flags);
        o += t;
        bool first = true; int count = 0; bool partial = false;
        while (p < n && count < 64) {
            int slot;
            if (b[p] < 0x80) { slot = b[p]; p += 1; }
            else { if (p + 2 > n) { partial = true; break; } slot = (((b[p] << 8) | b[p + 1]) + 0x8000) & 0xFFFF; p += 2; }
            if (p + 3 > n) { partial = true; break; }
            const std::uint32_t item1 = ((std::uint32_t)b[p] << 16) | ((std::uint32_t)b[p + 1] << 8) | b[p + 2]; p += 3;
            std::uint32_t qty = 0; int item = -1;
            if (item1 != 0) {
                if (p >= n) { partial = true; break; }
                qty = b[p++];
                if (qty == 0xFF) { if (p + 4 > n) { partial = true; break; } qty = ev_u32be(b + p); p += 4; }
                if (flags & 2) { if (p < n) p++; }
                item = (int)item1 - 1;
            }
            std::snprintf(t, sizeof(t), "%s{\"slot\":%d,\"item\":%d,\"qty\":%u}", first ? "" : ",", slot, item, qty);
            o += t; first = false; ++count;
        }
        o += "],\"partial\":"; o += (partial || (std::uint32_t)len > n) ? "true" : "false";
        return true;
    }
    case rtx::sops::kRunClientScript: {   // [sig NUL-terminated, i/s/l][args in REVERSE sig order: s = NUL string, i = i32 BE, l = i64 BE][scriptId: i32 BE]
        const bool cut = len > (int)n;
        std::uint32_t p = 0; std::string sig;
        while (p < n && b[p] != 0 && sig.size() < 16) sig.push_back((char)b[p++]);
        if (p >= n) return false;                              // no NUL terminator: raw
        for (char c : sig) if (c != 'i' && c != 's' && c != 'l') return false;
        p++;
        std::vector<std::string> args(sig.size());
        bool partial = false;
        for (int i = (int)sig.size() - 1; i >= 0 && !partial; --i) {
            if (sig[(std::size_t)i] == 's') {
                std::uint32_t s0 = p;
                while (p < n && b[p] != 0) ++p;
                if (p >= n) {
                    if (!cut) return false;                    // malformed inside a complete packet: raw
                    args[(std::size_t)i] = "\"" + chat_pkt_escape(b + s0, p - s0) + "\""; partial = true; break;
                }
                args[(std::size_t)i] = "\"" + chat_pkt_escape(b + s0, p - s0) + "\"";
                p++;
            } else if (sig[(std::size_t)i] == 'l') {
                if (p + 8 > n) { if (!cut) return false; partial = true; break; }
                const std::int64_t v = (std::int64_t)(((std::uint64_t)ev_u32be(b + p) << 32) | ev_u32be(b + p + 4));
                args[(std::size_t)i] = std::to_string(v); p += 8;
            } else {
                if (p + 4 > n) { if (!cut) return false; partial = true; break; }
                args[(std::size_t)i] = std::to_string((std::int32_t)ev_u32be(b + p)); p += 4;
            }
        }
        std::int64_t script = -1;
        if (!partial) {
            if (p + 4 > n) { if (!cut) return false; partial = true; }
            else script = ev_u32be(b + p);
        }
        // Script 10623(struct, active) is the server adding (1) or removing (0) a buff-bar entry
        // (client scripts 10624 -> 10625 add / 15426 remove). Reported as its own kind with the name.
        if (script == 10623 && sig == "ii" && !partial) {
            const int structId = std::atoi(args[0].c_str());
            std::string nm; rtx::cache::StructStrParam(structId, 2794, nm);
            std::snprintf(t, sizeof(t), "\"kind\":\"buff_update\",\"struct\":%d,\"active\":%s,\"name\":\"", structId, args[1] == "0" ? "false" : "true");
            o += t; o += json_escape(nm); o += "\""; return true;
        }
        std::snprintf(t, sizeof(t), "\"kind\":\"runclientscript\",\"script\":%lld,\"sig\":\"%s\",\"partial\":%s,\"args\":[",
                      (long long)script, sig.c_str(), partial ? "true" : "false");
        o += t;
        for (std::size_t i = 0; i < args.size(); ++i) { if (i) o += ","; o += args[i].empty() ? std::string("null") : args[i]; }
        o += "]"; return true;
    }
    case rtx::sops::kRunEnergy:     // reads 1 byte -> skill block +0x18
        if (n < 1) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"run_energy\",\"value\":%d", (int)b[0]);
        o += t; return true;
    case rtx::sops::kRunWeight:     // reads 2 bytes, byte-swapped -> skill block +0x1c
        if (n < 2) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"run_weight\",\"value\":%d", (int)(std::int16_t)((b[0] << 8) | b[1]));
        o += t; return true;
    case rtx::sops::kVarpInt: {    // [id: (b0-0x80)&0xFF | b1<<8][value: b4 b5 b2 b3 big-endian order]
        if (n < 6) return false;
        const int id = ((b[0] - 0x80) & 0xFF) | (b[1] << 8);
        const std::int32_t v = (std::int32_t)(((std::uint32_t)b[4] << 24) | ((std::uint32_t)b[5] << 16) | ((std::uint32_t)b[2] << 8) | b[3]);
        std::snprintf(t, sizeof(t), "\"kind\":\"varp_set\",\"id\":%d,\"value\":%d", id, v);
        o += t; return true;
    }
    case rtx::sops::kVarpByte: {   // [value: i8][id: (b2-0x80)&0xFF | b1<<8]
        if (n < 3) return false;
        const int id = ((b[2] - 0x80) & 0xFF) | (b[1] << 8);
        std::snprintf(t, sizeof(t), "\"kind\":\"varp_set\",\"id\":%d,\"value\":%d", id, (int)(std::int8_t)b[0]);
        o += t; return true;
    }
    case rtx::sops::kVarcInt: {    // [id: (b1-0x80)&0xFF | b0<<8][value: b3 b2 b5 b4]  (FUN_140141e90)
        if (n < 6) return false;
        const int id = ((b[1] - 0x80) & 0xFF) | (b[0] << 8);
        const std::int32_t v = (std::int32_t)(((std::uint32_t)b[3] << 24) | ((std::uint32_t)b[2] << 16) | ((std::uint32_t)b[5] << 8) | b[4]);
        std::snprintf(t, sizeof(t), "\"kind\":\"varc_set\",\"id\":%d,\"value\":%d", id, v);
        o += t; return true;
    }
    case rtx::sops::kVarcByte: {   // [id: b0 | b1<<8][value: (0x80-b2)&0xFF]
        if (n < 3) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"varc_set\",\"id\":%d,\"value\":%d", b[0] | (b[1] << 8), (int)(std::int8_t)((0x80 - b[2]) & 0xFF));
        o += t; return true;
    }
    case rtx::sops::kVarpLong: {   // [i64: hi = b1 b0 b3 b2, lo = b5 b4 b7 b6][id: u16 BE]  (FUN_140142390)
        if (n < 10) return false;
        const std::uint32_t hi = ((std::uint32_t)b[1] << 24) | ((std::uint32_t)b[0] << 16) | ((std::uint32_t)b[3] << 8) | b[2];
        const std::uint32_t lo = ((std::uint32_t)b[5] << 24) | ((std::uint32_t)b[4] << 16) | ((std::uint32_t)b[7] << 8) | b[6];
        const long long v = (long long)(((std::uint64_t)hi << 32) | lo);
        std::snprintf(t, sizeof(t), "\"kind\":\"varp_set\",\"id\":%d,\"value\":%lld,\"long\":true", (b[8] << 8) | b[9], v);
        o += t; return true;
    }
    case rtx::sops::kVarbitVarint: {   // two LEB128 varints: varbit id, value  (FUN_1401420d0)
        std::uint32_t p = 0; std::uint64_t vals[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k) {
            int sh = 0;
            for (;;) {
                if (p >= n || sh > 56) return false;
                const std::uint8_t c = b[p++];
                vals[k] |= (std::uint64_t)(c & 0x7F) << sh; sh += 7;
                if (c < 0x80) break;
            }
        }
        std::snprintf(t, sizeof(t), "\"kind\":\"varbit_set\",\"id\":%u,\"value\":%d", (unsigned)vals[0], (int)(std::uint32_t)vals[1]);
        o += t; return true;
    }
    case rtx::sops::kPingEcho:     // two u32 BE, client echoes back (9-byte reply)
        if (n < 8) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"ping\",\"a\":%u,\"b\":%u", ev_u32be(b), ev_u32be(b + 4));
        o += t; return true;
    default:
        return false;   // 0x05 / 0x51 ge_offer: field layout not documented -> raw
    }
}
}  // namespace

static std::string ge_slot_json(std::uint32_t pid, int slot) {
    if (slot < 0 || slot >= kGESlotCount) return "{}";
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    auto ge_box = rpm<std::uint64_t>(ps.h, *root + kOffGE);
    if (!ge_box || !*ge_box) return "{}";
    alignas(8) std::uint8_t b[kGESlotSize];
    if (!rpm_bytes(ps.h, *ge_box + kGEArrayPad + (std::uint64_t)slot * kGESlotSize, b, sizeof(b))) return "{}";
    char t[200];
    std::snprintf(t, sizeof(t),
        "{\"status\":%d,\"type\":%d,\"item\":%d,\"price\":%lld,\"qty\":%d,\"filled\":%d,\"filledValue\":%lld}",
        *(const std::int32_t*)(b + 0x00), *(const std::int32_t*)(b + 0x04), *(const std::int32_t*)(b + 0x08),
        (long long)*(const std::int64_t*)(b + 0x10), *(const std::int32_t*)(b + 0x18),
        *(const std::int32_t*)(b + 0x1C), (long long)*(const std::int64_t*)(b + 0x20));
    return t;
}

std::string EventsJson(std::uint32_t pid, std::uint64_t since) {
    wchar_t name[64];
    rtx::events::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return "{\"ok\":false,\"why\":\"companion not loaded\"}";
    auto* sh = reinterpret_cast<const rtx::events::Share*>(
        MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::events::Share)));
    if (!sh) { CloseHandle(h); return "{\"ok\":false,\"why\":\"map failed\"}"; }
    std::string out;
    if (sh->magic != rtx::events::kMagic || sh->version != rtx::events::kVersion) {
        out = "{\"ok\":false,\"why\":\"share version mismatch\"}";
    } else {
        std::uint32_t tickCount = 0; double age = 0;
        const bool haveTick = TickState(pid, tickCount, age);
        {   // map origin for zone-relative packets: [MainData+0x19898]+0x698 / +0x69C (the loaded map's base tile)
            ProcSnap ps = snap_proc(pid);
            if (ps) {
                auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
                auto mgr = root && *root > 0x10000 ? rpm<std::uint64_t>(ps.h, *root + 0x19898) : std::nullopt;
                if (mgr && *mgr > 0x10000) {
                    auto bx = rpm<std::int32_t>(ps.h, *mgr + 0x698), by = rpm<std::int32_t>(ps.h, *mgr + 0x69C);
                    if (bx && by) { rtx::evzone::g_mapBaseX = *bx; rtx::evzone::g_mapBaseY = *by; }
                }
            }
        }
        const std::uint64_t written = sh->written;
        const std::uint64_t oldest  = written > (std::uint64_t)rtx::events::kMaxRecords
                                     ? written - rtx::events::kMaxRecords : 0;
        std::uint64_t from = since > oldest ? since : oldest;
        if (from > written) from = written;
        constexpr std::uint64_t kMaxPerCall = 512;
        if (written - from > kMaxPerCall) from = written - kMaxPerCall;
        char meta[256];
        std::snprintf(meta, sizeof(meta),
            "{\"ok\":true,\"seq\":%llu,\"tick\":%d,\"from\":%llu,\"inbound\":%llu,\"truncated\":%llu,"
            "\"hook\":%s,\"mask\":[%u,%u,%u,%u,%u,%u,%u,%u],\"events\":[",
            (unsigned long long)written, haveTick ? (int)tickCount : -1, (unsigned long long)from,
            (unsigned long long)sh->inbound, (unsigned long long)sh->truncated,
            (sh->flags & 1) ? "true" : "false",
            sh->mask[0], sh->mask[1], sh->mask[2], sh->mask[3], sh->mask[4], sh->mask[5], sh->mask[6], sh->mask[7]);
        out = meta;
        bool first = true;
        rtx::events::Record r;
        for (std::uint64_t i = from; i < written; ++i) {
            const rtx::events::Record& slot = sh->recs[i % rtx::events::kMaxRecords];
            const std::uint32_t want = (std::uint32_t)((i + 1) * 2);   // companion: pub = (i+1)*2, odd while filling
            if (slot.seq != want) continue;                            // lapped or in flux
            MemoryBarrier();
            std::memcpy(&r, &slot, sizeof(r));
            MemoryBarrier();
            if (slot.seq != want) continue;                            // rewritten during the copy
            std::uint32_t n = r.length < 0 ? 0 : (std::uint32_t)r.length;
            if (n > (std::uint32_t)rtx::events::kPayload) n = rtx::events::kPayload;
            char b[160];
            std::snprintf(b, sizeof(b), "%s{\"seq\":%llu,\"t\":%u,\"wall\":%u,\"op\":%d,\"len\":%d,",
                          first ? "" : ",", (unsigned long long)(i + 1), r.tick, r.wallMs, r.opcode, r.length);
            out += b; first = false;
            const std::size_t mark = out.size();
            if (!ev_decode(out, r.opcode, r.payload, n, r.length)) {
                out.resize(mark);
                if ((r.opcode == 0x05 || r.opcode == 0x51) && n >= 7) {
                    // ge_offer: bytes 0-3 fixed 00 02 07 03, byte 4 slot, bytes 5-6 item id (BE); rest unknown,
                    const int slot = (int)r.payload[4];
                    const int item = ((int)r.payload[5] << 8) | (int)r.payload[6];
                    out += "\"kind\":\"ge_offer\",\"slot\":" + std::to_string(slot) + ",\"item\":" + std::to_string(item)
                         + ",\"offer\":" + ge_slot_json(pid, slot) + ",\"hex\":\"";
                } else {
                    out += "\"kind\":\"raw\",\"hex\":\"";
                }
                ev_hex(out, r.payload, n);
                out += "\"";
            }
            out += "}";
        }
        out += "]}";
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return out;
}

// Opcode mask the hook records (bit op&31 of word op>>5). Creates the section if the companion
bool EventsMaskSet(std::uint32_t pid, const std::uint32_t mask[8]) {
    wchar_t name[64];
    rtx::events::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, name);
    if (!h) h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   sizeof(rtx::events::Share), name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::events::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::events::Share)));
    if (!sh) { CloseHandle(h); return false; }
    for (int i = 0; i < 8; ++i) sh->mask[i] = mask[i];
    sh->mask[0] &= ~(1u << 0x15);                             // never message_game
    MemoryBarrier();
    sh->maskSet = 1;
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return true;
}

// which: 0 hide NPCs, 1 hide other players, 2 hide scene, 3 keep-focused, 4 true-embed.
std::string GpuTimingJson(std::uint32_t pid) {
    const char* kEmpty = "{\"passes\":[]}";
    wchar_t name[64];
    rtx::gputime::MakeSectionName(pid, name);
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return kEmpty;
    auto* sh = reinterpret_cast<const rtx::gputime::Share*>(MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::gputime::Share)));
    if (!sh) { CloseHandle(h); return kEmpty; }
    std::string out = kEmpty;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::uint32_t s0 = sh->seq;
        if (s0 & 1u) { Sleep(1); continue; }
        rtx::gputime::Share snap;
        std::memcpy(&snap, sh, sizeof(snap));
        if (sh->seq != s0 || snap.magic != rtx::gputime::kMagic) { Sleep(1); continue; }
        std::uint32_t n = snap.count < rtx::gputime::kMaxPasses ? snap.count : rtx::gputime::kMaxPasses;
        out = "{\"frame\":" + std::to_string(snap.frame) + ",\"total_us\":" + std::to_string(snap.total_us) +
              ",\"frame_us\":" + std::to_string(snap.frame_us) + ",\"passes\":[";
        for (std::uint32_t i = 0; i < n; ++i) {
            char desc[rtx::gputime::kDescMax + 1];
            std::memcpy(desc, snap.passes[i].desc, rtx::gputime::kDescMax); desc[rtx::gputime::kDescMax] = 0;
            out += (i ? "," : "") + std::string("{\"desc\":\"") + json_escape(desc) + "\",\"draws\":" +
                   std::to_string(snap.passes[i].draws) + ",\"us\":" + std::to_string(snap.passes[i].us) + "}";
        }
        out += "]}";
        break;
    }
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return out;
}

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

//   container = *(root + 0x199D0); idx = *(int)(container + 0x70); worldView = *( *(container + 0x58) + idx*0x10 + 8 ); worker = *(worldView + 0x10170)
//   vector begin = *(worker + 0x138), end = *(worker + 0x140) (Entity* each); entity sec = *(entity + 0x1A0), type = *(u8)(sec + 0x10) (1 = NPC, 2 = player); name(asciiz)@sec+0xB8, uid@sec+0x88, NPC configId@sec+0x1080, posX/posY(float)@sec+0x270/+0x278 (tile = pos / 512)
namespace {

struct RuntimeObj {
    int config_id, x, y, plane, kind;
    bool hidden;              // the game has switched the loc off (depleted tree, dormant stump)
    float bmin[3], bmax[3];   // live model world AABB (east,north,up); bmax.x==bmin.x = none
};

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
                RuntimeObj r{ o.config_id, o.x, o.y, o.plane, o.kind & 0xFF,
                              (o.kind & rtx::scene::kHiddenBit) != 0,
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

struct RuntimeHi { int gfx, x, y, uid, plane, type, kind; std::uint32_t stamp; };
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
                out.push_back(RuntimeHi{ it.gfx, it.x, it.y, it.uid, it.plane, it.type, it.kind, it.stamp });
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

rtx::cache::LocMeta resolve_loc(HANDLE h, std::uint64_t root, int base_id) {
    int vb = -1, vp = -1, defc = -1;
    std::vector<int> variants;
    if (h && root && rtx::cache::GetLocMorph(base_id, vb, vp, defc, variants) && !variants.empty()) {
        int value = -1;
        if (vb >= 0) {                                            // varbit takes priority (engine order)
            int wvp = -1, lsb = -1, msb = -1;                     // varbit -> backing varp + bit range
            if (rtx::cache::GetVarbit(vb, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
                int raw = 0;                                      // unreadable map -> value stays -1 (static fallback)
                if (read_varp_found(h, root, wvp, raw)) {
                    int width = msb - lsb + 1;
                    unsigned mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
                    value = (int)(((unsigned)raw >> lsb) & mask);
                }
            }
        } else if (vp >= 0) {
            int v = 0;
            if (read_varp_found(h, root, vp, v)) value = v;       // selector is a varp directly
        }
        if (value >= 0) {
            int child = (value < (int)variants.size()) ? variants[value] : defc;
            if (child < 0) return {};            // hidden, no marker
            return rtx::cache::GetLoc(child);
        }
    }
    return rtx::cache::GetLoc(base_id);
}

rtx::cache::NpcMeta resolve_npc(HANDLE h, std::uint64_t root, int base_id, bool* out_hidden = nullptr) {
    if (out_hidden) *out_hidden = false;
    int vb = -1, vp = -1, defc = -1;
    std::vector<int> variants;
    if (h && root && rtx::cache::GetNpcMorph(base_id, vb, vp, defc, variants) && !variants.empty()) {
        int value = -1;
        if (vb >= 0) {                                            // varbit takes priority (engine order)
            int wvp = -1, lsb = -1, msb = -1;
            if (rtx::cache::GetVarbit(vb, wvp, lsb, msb) && wvp >= 0 && lsb >= 0 && msb >= lsb && msb < 32) {
                int raw = 0;                                      // unreadable map -> value stays -1 (static fallback)
                if (read_varp_found(h, root, wvp, raw)) {
                    int width = msb - lsb + 1;
                    unsigned mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
                    value = (int)(((unsigned)raw >> lsb) & mask);
                }
            }
        } else if (vp >= 0) {
            int v = 0;
            if (read_varp_found(h, root, vp, v)) value = v;       // selector is a varp directly
        }
        (void)defc;
        if (value < 0 || value >= (int)variants.size()) { if (out_hidden) *out_hidden = true; return {}; }
        int child = variants[value];
        if (child < 0) { if (out_hidden) *out_hidden = true; return {}; }   // variant "none" -> hidden
        return rtx::cache::GetNpc(child);
    }
    return rtx::cache::GetNpc(base_id);
}

}  // namespace

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
std::atomic<std::uint32_t>       g_workerOff{ rtx::scn::kWorkerOffDefault };
std::atomic<std::uint32_t>       g_matrixOff{ rtx::scn::kMatrixOffDefault };
std::atomic<std::int32_t>        g_camPosRel{ rtx::scn::kCamPosRelDefault };   // stored camera position, relative to the matrix
std::atomic<unsigned long long>  g_workerScanAt{ 0 };   // rescan throttles (GetTickCount64)
std::atomic<unsigned long long>  g_matrixScanAt{ 0 };
constexpr std::uint32_t          kWvSpan = 0x14000;     // worldView bytes covered by a rescan

struct CamProbes { int n = 0; float p[8][3]; };

// +0x138/+0x140 must bound a sane entity vector whose secs (+0x1A0) carry known type bytes.
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
        int t = rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xFF);
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

// Matrix selection: render camera matrix at worldView+0x13090, minimap impostor at 0x13970.

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

// Per-client camera-read state. A block that stopped updating still centre-matches its own stale
// position, so camera varcs 5115 (yaw) / 5114 (pitch) / 1971 (zoom) changing while the matrix
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

// True (server) tile of an actor from its movement route; see kMoveMgr in SceneOffsets.h. Returns
// false when the route is empty (stationary) or unreadable, leaving tx/ty as the caller's fallback,
// which should be the visible tile. Live-verified 2026-09-11: 99.6 % of moving samples within 2 tiles.
static bool actor_true_tile(HANDLE h, std::uint64_t sec, int& tx, int& ty) {
    auto mm = rpm<std::uint64_t>(h, sec + rtx::scn::kMoveMgr);
    if (!mm || *mm <= 0x10000 || *mm > 0x00007FFFFFFFFFFFull) return false;
    auto beg = rpm<std::uint64_t>(h, *mm + rtx::scn::kRouteBegin);
    auto end = rpm<std::uint64_t>(h, *mm + rtx::scn::kRouteEnd);
    auto wr  = rpm<std::uint64_t>(h, *mm + rtx::scn::kRouteWrite);
    if (!beg || !end || !wr || *beg <= 0x10000 || *end < *beg || *end - *beg > 64 * rtx::scn::kRouteStride) return false;
    if (*wr <= *beg || *wr > *end || (*wr - *beg) % rtx::scn::kRouteStride) return false;   // empty: stationary
    const std::uint64_t e = *wr - rtx::scn::kRouteStride;
    auto x = rpm<float>(h, e + rtx::scn::kRouteX), y = rpm<float>(h, e + rtx::scn::kRouteY);
    if (!x || !y || !(*x > 0.f) || !(*y > 0.f) || *x >= 16384.f * 512.f || *y >= 16384.f * 512.f) return false;
    tx = (int)(*x / 512.f); ty = (int)(*y / 512.f);
    return true;
}

// Overhead object of an actor: hitsplat ring and the first head bar. splats = JSON array of
// [hitmark, value, startCycle, durationCycles] for records that hold a hit (an expired record stays
// until reused, so consumers key on start+value); bar = fill 0..255 of head-bar slot 0, -1 when none.
static void actor_overhead_json(HANDLE h, std::uint64_t sec, std::string& splats, int& bar) {
    splats = "[]"; bar = -1;
    auto hb = rpm<std::uint64_t>(h, sec + rtx::scn::kOverhead);
    if (!hb || *hb <= 0x10000 || *hb > 0x00007FFFFFFFFFFFull) return;
    auto ring = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvRing);
    if (ring && *ring > 0x10000 && *ring < 0x00007FFFFFFFFFFFull) {
        std::int32_t rec[6 * 6];
        if (rpm_bytes(h, *ring, rec, sizeof(rec))) {
            std::string out = "["; bool first = true;
            for (int i = 0; i < 6; ++i) {
                const std::int32_t* r = rec + i * 6;
                if (r[0] < 0 || r[1] < 0 || r[2] <= 0 || r[5] <= 0 || r[0] > 100000 || r[1] > 100000000) continue;
                char b[96];
                std::snprintf(b, sizeof(b), "%s[%d,%d,%d,%d]", first ? "" : ",", r[0], r[1], r[2], r[5]);
                out += b; first = false;
            }
            out += "]"; splats = out;
        }
    }
    auto slots = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvSlots);
    if (slots && *slots > 0x10000 && *slots < 0x00007FFFFFFFFFFFull) {
        auto fill = rpm<std::int32_t>(h, *slots + rtx::scn::kBarFill);
        auto stamp = rpm<std::int32_t>(h, *slots + rtx::scn::kBarStamp);
        if (fill && stamp && *fill >= 0 && *fill <= 255 && *stamp > 0) bar = *fill;
    }
}

// Local player actor through the player registry: [[MD+0x19950]+0x10][idx]+0x38, idx = local uid.
// Validated by type and uid; 0 when the chain does not resolve (callers fall back to the scan).
static std::uint64_t local_player_sec_fast(HANDLE h, std::uint64_t root, int local_uid) {
    if (root <= 0x10000 || local_uid < 0 || local_uid >= 4096) return 0;
    auto reg = rpm<std::uint64_t>(h, root + 0x19950);
    if (!reg || *reg <= 0x10000) return 0;
    auto tbl = rpm<std::uint64_t>(h, *reg + 0x10);
    if (!tbl || *tbl <= 0x10000) return 0;
    auto ent = rpm<std::uint64_t>(h, *tbl + (std::uint64_t)local_uid * 8);
    if (!ent || *ent <= 0x10000) return 0;
    auto sec = rpm<std::uint64_t>(h, *ent + 0x38);
    if (!sec || *sec <= 0x10000 || *sec > 0x00007FFFFFFFFFFFull) return 0;
    if (rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xff) != 2) return 0;
    if (rpm<std::int32_t>(h, *sec + rtx::scn::kUid).value_or(-1) != local_uid) return 0;
    return *sec;
}


// ---------------------------------------------------------------------------------------------
// Combat log. Every hitsplat the game draws on an actor in the scene becomes one event in an
// append-only stream that developers read with state.combatLog(sinceSeq). The launcher polls
// CombatLogPoll at 5 Hz; a record lives 60 cycles (1.2 s) in the actor's ring, so no hit is missed.
// Records are keyed on (ring slot, start cycle, value, hitmark) per actor and compared with the
// previous poll, which logs each hit exactly once even though expired records linger in the ring
// until the game overwrites them.
namespace {
struct CombatEvent {
    std::uint64_t seq; long long t;          // t = wall clock, ms since the Unix epoch
    int type;                                // 1 NPC, 2 player
    bool self;                               // the local player (a hit taken)
    int uid, id;                             // actor uid; NPC config id (-1 for players)
    std::string name;
    int x, y, plane;
    int hitmark, value, cycle, dur;          // raw ring record
    int lp, lpMax;                           // NPC life points at the poll (-1 unknown)
};
struct CombatLogState {
    std::uint64_t seq = 0;
    std::deque<CombatEvent> events;
    std::unordered_map<int, std::vector<std::uint64_t>> present;   // uid -> record keys seen last poll
    long long polls = 0;
};
std::mutex g_combat_mu;
std::unordered_map<std::uint32_t, CombatLogState> g_combat;
constexpr std::size_t kCombatLogMax = 4096;
}  // namespace

void CombatLogPoll(std::uint32_t pid) {
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
    if (!h || !mgva) return;
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off) -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto root = rpm<std::uint64_t>(h, mgva);
    if (!root || *root <= 0x10000) return;
    auto pdata = deref(root, rtx::scn::kPlayerData);
    int local_uid = (pdata && *pdata > 0x10000) ? rpm<std::int32_t>(h, *pdata + rtx::scn::kLocalUid).value_or(-1) : -1;
    auto cont = deref(root, rtx::scn::kContainer);
    auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + rtx::scn::kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, rtx::scn::kEntryArr);
    std::optional<std::uint64_t> wv, worker, vb, ve;
    if (idx && *idx >= 0 && arr && *arr > 0x10000) {
        wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + rtx::scn::kEntryWv);
        if (wv && *wv > 0x10000) worker = scene_worker(h, pid, *wv, nullptr);
        vb = deref(worker, rtx::scn::kVecBegin);
        ve = deref(worker, rtx::scn::kVecEnd);
    }
    if (!(vb && ve && *vb > 0x10000 && *ve >= *vb)) return;

    std::unordered_map<int, std::vector<std::uint64_t>> prev;
    { std::lock_guard<std::mutex> lk(g_combat_mu); prev = g_combat[pid].present; }
    using namespace std::chrono;
    const long long now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    std::unordered_map<int, std::vector<std::uint64_t>> cur;
    std::vector<CombatEvent> fresh;
    std::uint64_t n = (*ve - *vb) / 8;
    if (n > 20000) n = 20000;
    // The hit record names no source. The personal/other set of the hitmark is the only ownership
    // the game gives, so nothing here guesses an attacker from who was targeting whom.
    for (std::uint64_t i = 0; i < n; ++i) {
        auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
        if (!ep || *ep <= 0x10000) continue;
        auto sec = rpm<std::uint64_t>(h, *ep + rtx::scn::kSecPtr);
        if (!sec || *sec <= 0x10000) continue;
        int type = rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xff);
        if (type != 1 && type != 2) continue;
        // Every actor is tracked from the poll it is first seen, ring or no ring: an NPC only gets
        // its overhead object the first time it is hit, and those first hits (a killing blow that
        // spreads onto untouched enemies, a fresh spawn you open on) must not look like a stale ring.
        int uid = rpm<std::int32_t>(h, *sec + rtx::scn::kUid).value_or(0);
        std::vector<std::uint64_t>& keys = cur[uid];
        const std::vector<std::uint64_t>* old = nullptr;
        { auto pit = prev.find(uid); if (pit != prev.end()) old = &pit->second; }
        auto hb = rpm<std::uint64_t>(h, *sec + rtx::scn::kOverhead);
        if (!hb || *hb <= 0x10000 || *hb > 0x00007FFFFFFFFFFFull) continue;
        auto ring = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvRing);
        if (!ring || *ring <= 0x10000 || *ring > 0x00007FFFFFFFFFFFull) continue;
        std::int32_t rec[6 * 6];
        if (!rpm_bytes(h, *ring, rec, sizeof(rec))) continue;
        // An actor seen for the first time may carry old records; on NPCs the actor's own cycle
        // clock (+0x1330) tells which records are fresh (started within the last 300 ms).
        int freshAfter = -1;
        if (!old && type == 1) {
            int clk = rpm<std::int32_t>(h, *sec + 0x1330).value_or(0);
            if (clk > 15) freshAfter = clk - 15;
        }
        bool actorRead = false; CombatEvent base{};
        for (int s = 0; s < 6; ++s) {
            const std::int32_t* r = rec + s * 6;
            if (r[0] < 0 || r[1] < 0 || r[2] <= 0 || r[5] <= 0 || r[0] > 100000 || r[1] > 100000000) continue;
            std::uint64_t key = ((std::uint64_t)(std::uint32_t)r[2] << 32) ^ (std::uint64_t)(std::uint32_t)r[1]
                              ^ ((std::uint64_t)(std::uint32_t)r[0] << 52) ^ ((std::uint64_t)s << 60);
            keys.push_back(key);
            if (old && std::find(old->begin(), old->end(), key) != old->end()) continue;   // seen before
            if (!old && (freshAfter < 0 || r[2] < freshAfter)) continue;   // first sight: only records that just started
            if (!actorRead) {
                actorRead = true;
                base.type = type; base.uid = uid; base.self = (type == 2 && uid == local_uid);
                auto fx = rpm<float>(h, *sec + rtx::scn::kPosX), fy = rpm<float>(h, *sec + rtx::scn::kPosY);
                base.x = fx ? (int)(*fx / 512.f) : 0; base.y = fy ? (int)(*fy / 512.f) : 0;
                base.plane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);
                char nm[40] = {0};
                rpm_bytes(h, *sec + rtx::scn::kName, nm, sizeof(nm) - 1);
                for (int j = 0; j < (int)sizeof(nm) && nm[j]; ++j) {
                    unsigned char c = (unsigned char)nm[j];
                    if (c >= 0x20 && c <= 0x7e) base.name.push_back((char)c);
                    else if (c == 0xA0) base.name.push_back(' ');
                }
                base.id = -1; base.lp = -1; base.lpMax = -1;
                if (base.self) {                  // your own life points: varps 13537 current / 13538 max
                    int cur = read_varp(h, *root, 13537), mx = read_varp(h, *root, 13538);
                    if (mx > 0 && mx < 100000 && cur >= 0) { base.lp = cur; base.lpMax = mx; }
                }
                if (type == 1) {
                    int cfg = rpm<std::int32_t>(h, *sec + rtx::scn::kConfig).value_or(-1);
                    auto meta = resolve_npc(h, *root, cfg);
                    base.id = meta.id >= 0 ? meta.id : cfg;
                    if (!meta.name.empty()) base.name = meta.name;
                    int lp = rpm<std::int32_t>(h, *sec + rtx::scn::kLpCur).value_or(-1);
                    int lpMax = rpm<std::int32_t>(h, *sec + rtx::scn::kLpMax).value_or(-1);
                    base.lp = (lp < 0 || lp > 1000000000) ? -1 : lp;
                    base.lpMax = (lpMax <= 0 || lpMax > 1000000000) ? -1 : lpMax;
                }
            }
            CombatEvent ev = base;
            ev.t = now; ev.hitmark = r[0]; ev.value = r[1]; ev.cycle = r[2]; ev.dur = r[5];
            fresh.push_back(std::move(ev));
        }
    }
    // Oldest start cycle first within a poll, so a burst reads in the order it landed.
    std::stable_sort(fresh.begin(), fresh.end(), [](const CombatEvent& a, const CombatEvent& b) { return a.cycle < b.cycle; });
    std::lock_guard<std::mutex> lk(g_combat_mu);
    CombatLogState& st = g_combat[pid];
    st.present = std::move(cur);
    ++st.polls;
    for (auto& ev : fresh) {
        ev.seq = ++st.seq;
        st.events.push_back(std::move(ev));
    }
    while (st.events.size() > kCombatLogMax) st.events.pop_front();
}

std::string CombatLogJson(std::uint32_t pid, std::uint64_t since, int max_events) {
    if (max_events < 1) max_events = 1;
    if (max_events > 2000) max_events = 2000;
    std::lock_guard<std::mutex> lk(g_combat_mu);
    auto it = g_combat.find(pid);
    if (it == g_combat.end()) return "{\"seq\":0,\"gap\":false,\"events\":[]}";
    const CombatLogState& st = it->second;
    bool gap = since != 0 && !st.events.empty() && since + 1 < st.events.front().seq;
    std::string out = "{\"seq\":" + std::to_string(st.seq) + ",\"gap\":" + (gap ? "true" : "false") + ",\"events\":[";
    int c = 0;
    for (const auto& ev : st.events) {
        if (ev.seq <= since) continue;
        if (c >= max_events) break;
        const auto* hm = rtx::hitmarks::Find(ev.hitmark);
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s{\"seq\":%llu,\"t\":%lld,\"type\":\"%s\",\"uid\":%d,\"id\":%d,\"x\":%d,\"y\":%d,\"plane\":%d,"
            "\"hitmark\":%d,\"kind\":\"%s\",\"other\":%s,\"value\":%d,\"cycle\":%d,\"dur\":%d,\"lp\":%d,\"lpMax\":%d,\"name\":\"",
            c ? "," : "", (unsigned long long)ev.seq, ev.t, ev.self ? "self" : ev.type == 1 ? "npc" : "player",
            ev.uid, ev.id, ev.x, ev.y, ev.plane, ev.hitmark, hm ? hm->kind : "unknown", (hm && hm->other) ? "true" : "false",
            ev.value, ev.cycle, ev.dur, ev.lp, ev.lpMax);
        out += buf; out += json_escape(ev.name); out += "\"}";
        ++c;
    }
    out += "]}";
    return out;
}


// ---------------------------------------------------------------------------------------------
// Live terrain heights. The game places every actor with FUN_140357a50(actor, xy): plane =
// actor plane (+1 on a bridge tile), then per region (x>>6, y>>6) the terrain object at
// region+0xA0 (or +0xEBB0) holds one height grid per plane at +0x170 (0x10 per plane, grid ptr at
// +8; plane 0 also has an adjusted grid at +0x1F8 that the actor path uses). A grid is a column
// table: column x at [grid] + 0x18*(x+1), value (y) at column + 4*(y+1), i32 fine units, padded
// by one on each side. The client interpolates bilinearly on the 9-bit tile fraction and adds 5.0
// for the actor's feet. Decompiled from 950-1 (docs/fieldmap-950-1.md "Terrain heights"); the
// values match the actor heights exactly, in instances too, where the cache has nothing.
// Region grid: scene = actor+0x60; manager = [scene+0x140C0]; bounds i32 at +0x14034 (min x),
// +0x14038 (min y), +0x1403C (max x), +0x14040 (max y); cells: [[mgr+0x14080] + (rx-minx)*0x18]
// + (ry-miny)*0x18, region object at cell+8.
namespace {
constexpr std::int32_t kNoFine = INT32_MIN;
struct TerrainRegion { std::int32_t h[66][66]; std::int16_t lift[64][64]; std::uint8_t bridge[64][64]; std::uint8_t flag[64][64]; bool ok = false; bool liftOk = false; bool flagsOk = false; bool planeFlagsOk = false; };   // h[lx+1][ly+1], lx/ly 0..64; lift = standing offset; bridge = plane-1 flag bit 1; flag = this plane's tile flag byte (bit 0 = blocked)
struct TerrainSnap { std::uint32_t pid = 0; int plane = -1; int cx = 0, cy = 0; std::uint32_t stamp = 0; std::unordered_map<int, TerrainRegion> regions; };  // key (plane<<16)|(rx<<8)|ry
std::mutex g_terr_mu;
TerrainSnap g_terr;
bool terrain_read_region(HANDLE h, std::uint64_t scene, int rx, int ry, int plane, TerrainRegion& out) {
    out.ok = false;
    auto mgr = rpm<std::uint64_t>(h, scene + 0x140C0);
    if (!mgr || *mgr <= 0x10000) return false;
    auto minx = rpm<std::int32_t>(h, *mgr + 0x14034), miny = rpm<std::int32_t>(h, *mgr + 0x14038);
    auto maxx = rpm<std::int32_t>(h, *mgr + 0x1403C), maxy = rpm<std::int32_t>(h, *mgr + 0x14040);
    if (!minx || !miny || !maxx || !maxy || rx < *minx || rx > *maxx || ry < *miny || ry > *maxy) return false;
    auto rows = rpm<std::uint64_t>(h, *mgr + 0x14080);
    if (!rows || *rows <= 0x10000) return false;
    auto col = rpm<std::uint64_t>(h, *rows + (std::uint64_t)(rx - *minx) * 0x18);
    if (!col || *col <= 0x10000) return false;
    auto region = rpm<std::uint64_t>(h, *col + (std::uint64_t)(ry - *miny) * 0x18 + 8);
    if (!region || *region <= 0x10000) return false;
    std::uint64_t terrain = 0;
    for (std::uint64_t off : { (std::uint64_t)0xA0, (std::uint64_t)0xEBB0 }) {
        auto t = rpm<std::uint64_t>(h, *region + off);
        if (t && *t > 0x10000 && rpm<std::uint8_t>(h, *t + 0x21).value_or(1) == rpm<std::uint8_t>(h, *t + 0x22).value_or(2)) { terrain = *t; break; }
    }
    if (!terrain || rpm<std::uint8_t>(h, terrain + 0x169).value_or(1) != 0) return false;
    auto p0 = rpm<std::uint64_t>(h, terrain + 0x170), p1 = rpm<std::uint64_t>(h, terrain + 0x178);
    if (!p0 || !p1 || *p1 < *p0) return false;
    const int nplanes = (int)((*p1 - *p0) / 16);
    if (plane < 0 || plane >= nplanes) return false;
    std::optional<std::uint64_t> grid = (plane == 0) ? rpm<std::uint64_t>(h, terrain + 0x1F8) : std::nullopt;
    if (!grid || *grid <= 0x10000) grid = rpm<std::uint64_t>(h, *p0 + (std::uint64_t)plane * 0x10 + 8);
    if (!grid || *grid <= 0x10000) return false;
    auto cols = rpm<std::uint64_t>(h, *grid);
    if (!cols || *cols <= 0x10000) return false;
    std::uint64_t colptr[66];
    for (int lx = 0; lx < 66; ++lx) {
        auto c = rpm<std::uint64_t>(h, *cols + 0x18 * (std::uint64_t)lx);
        if (!c || *c <= 0x10000) return false;
        colptr[lx] = *c;
    }
    for (int lx = 0; lx < 66; ++lx)
        if (!rpm_bytes(h, colptr[lx], out.h[lx], sizeof(out.h[lx]))) return false;
    out.ok = true;
    // Per-tile offsets (FUN_1403be920): [terrain+0x1E8] = planes x 64 x 64 cells of two i16, cell
    // (x, y) at ((x + plane*64)*64 + y)*4. Mode 1 (actors, spot animations) adds the first i16, the
    // standing offset raised locs set (a fountain rim reads 180, a lodestone 80); mode 2 uses
    // max(first, second), the second being the tallest scenery on the tile. Plane count at +0x1E0.
    std::memset(out.lift, 0, sizeof(out.lift));
    auto nlift = rpm<std::uint64_t>(h, terrain + 0x1E0), tab = rpm<std::uint64_t>(h, terrain + 0x1E8);
    if (nlift && tab && *tab > 0x10000 && (std::uint64_t)plane < *nlift && *nlift <= 8) {
        std::int16_t cells[64 * 64 * 2];
        if (rpm_bytes(h, *tab + (std::uint64_t)plane * sizeof(cells), cells, sizeof(cells))) {
            for (int lx = 0; lx < 64; ++lx) for (int ly = 0; ly < 64; ++ly) out.lift[lx][ly] = cells[(lx * 64 + ly) * 2];
            out.liftOk = true;
        }
    }
    // Tile flags (FUN_1403bf100 over terrain+0x200): a vector of planes (0x10 each, column table
    // pointer at +8), columns of 0x18-byte byte vectors, one pad byte each side like the heights.
    // The actor plane helper (FUN_1403579f0) asks plane 1 and treats bit 1 (value 2) as "bridge":
    // the tile is drawn and stood on one plane up. Read plane 1's flags for every region.
    // The flag byte is the map's tile settings byte (bit 0 blocked/void, bit 1 bridge, bit 3 force
    // lowest plane); the cache builds its blocked grid from the same bit 0, so reading it live
    // gives the void tiles inside instances, where the cache has nothing.
    std::memset(out.bridge, 0, sizeof(out.bridge)); std::memset(out.flag, 0, sizeof(out.flag));
    auto readFlags = [&](int fplane, std::uint8_t dst[64][64], int mask) -> bool {
        auto f0 = rpm<std::uint64_t>(h, terrain + 0x200), f1 = rpm<std::uint64_t>(h, terrain + 0x208);
        if (!f0 || !f1 || *f1 < *f0 + (std::uint64_t)(fplane + 1) * 0x10) return false;
        auto fcols = rpm<std::uint64_t>(h, *f0 + (std::uint64_t)fplane * 0x10 + 8);
        if (!fcols || *fcols <= 0x10000) return false;
        auto cb = rpm<std::uint64_t>(h, *fcols), ce = rpm<std::uint64_t>(h, *fcols + 8);
        if (!cb || !ce || *ce < *cb + 66 * 0x18) return false;
        for (int lx = 0; lx < 64; ++lx) {
            auto colb = rpm<std::uint64_t>(h, *cb + (std::uint64_t)(lx + 1) * 0x18), cole = rpm<std::uint64_t>(h, *cb + (std::uint64_t)(lx + 1) * 0x18 + 8);
            std::uint8_t fb[66];
            if (!colb || !cole || *cole < *colb + 66 || !rpm_bytes(h, *colb, fb, sizeof(fb))) return false;
            for (int ly = 0; ly < 64; ++ly) dst[lx][ly] = (std::uint8_t)(fb[ly + 1] & mask);
        }
        return true;
    };
    out.flagsOk = readFlags(1, out.bridge, 2);
    out.planeFlagsOk = readFlags(plane, out.flag, 0xFF);
    return true;
}
}  // namespace

// Refresh the live terrain snapshot for the 3x3 regions around (cx, cy) on `plane` (and plane 0
// for bridge fallbacks). Called once per overlay build; lookups then hit the snapshot only.
bool LiveTerrainSnapshot(std::uint32_t pid, int cx, int cy, int plane) {
    ProcSnap ps = snap_proc(pid);
    if (!ps) return false;
    HANDLE h = ps.h; const std::uint64_t mgva = ps.mgva;
    auto root = rpm<std::uint64_t>(h, mgva);
    if (!root || *root <= 0x10000) return false;
    auto pdata = rpm<std::uint64_t>(h, *root + rtx::scn::kPlayerData);
    int local_uid = (pdata && *pdata > 0x10000) ? rpm<std::int32_t>(h, *pdata + rtx::scn::kLocalUid).value_or(-1) : -1;
    std::uint64_t psec = local_player_sec_fast(h, *root, local_uid);
    if (!psec) return false;
    auto scene = rpm<std::uint64_t>(h, psec + 0x60);
    if (!scene || *scene <= 0x10000) return false;
    TerrainSnap snap; snap.pid = pid; snap.plane = plane; snap.cx = cx; snap.cy = cy; snap.stamp = (std::uint32_t)GetTickCount64();
    const int rx0 = cx >> 6, ry0 = cy >> 6;
    for (int pl : { plane, plane + 1, 0 }) {   // plane + 1: bridge tiles are sampled one plane up
        if (pl < 0 || pl > 3) continue;
        for (int rx = rx0 - 1; rx <= rx0 + 1; ++rx)
            for (int ry = ry0 - 1; ry <= ry0 + 1; ++ry) {
                const int key = (pl << 16) | (rx << 8) | ry;
                if (snap.regions.count(key)) continue;
                TerrainRegion r;
                if (terrain_read_region(h, *scene, rx, ry, pl, r)) snap.regions.emplace(key, r);
            }
    }
    std::lock_guard<std::mutex> lk(g_terr_mu);
    g_terr = std::move(snap);
    return !g_terr.regions.empty();
}

// Height of the tile corner (wx, wy) on `plane`, fine units, from the last snapshot; kNoFine when
// that region/plane was not readable (caller falls back to the cache).
std::int32_t LiveCornerHeight(std::uint32_t pid, int wx, int wy, int plane) {
    if (wx < 0 || wy < 0) return kNoFine;
    std::lock_guard<std::mutex> lk(g_terr_mu);
    if (g_terr.pid != pid) return kNoFine;
    const int rx = wx >> 6, ry = wy >> 6;
    auto it = g_terr.regions.find((plane << 16) | (rx << 8) | ry);
    if (it == g_terr.regions.end() || !it->second.ok) return kNoFine;
    return it->second.h[(wx & 63) + 1][(wy & 63) + 1];
}
// Standing offset of tile (wx, wy): what the game adds to the bilinear terrain height for an actor
// or spot animation on that tile (0 for almost every tile). From the last snapshot.
std::int32_t LiveTileLift(std::uint32_t pid, int wx, int wy, int plane) {
    if (wx < 0 || wy < 0) return 0;
    std::lock_guard<std::mutex> lk(g_terr_mu);
    if (g_terr.pid != pid) return 0;
    auto it = g_terr.regions.find((plane << 16) | ((wx >> 6) << 8) | (wy >> 6));
    if (it == g_terr.regions.end() || !it->second.liftOk) return 0;
    return it->second.lift[wx & 63][wy & 63];
}
// The plane the game draws and stands on for tile (wx, wy) whose logical plane is `plane`: one up
// on bridge tiles (plane-1 flag bit 1), as FUN_1403579f0 does for every actor. `plane` when unknown.
int LiveTileEffPlane(std::uint32_t pid, int wx, int wy, int plane) {
    if (wx < 0 || wy < 0 || plane < 0 || plane >= 3) return plane;
    std::lock_guard<std::mutex> lk(g_terr_mu);
    if (g_terr.pid != pid) return plane;
    for (int pl : { plane, plane + 1, 0 }) {   // flags are per region, any loaded plane entry carries them
        auto it = g_terr.regions.find((pl << 16) | ((wx >> 6) << 8) | (wy >> 6));
        if (it == g_terr.regions.end() || !it->second.flagsOk) continue;
        return it->second.bridge[wx & 63][wy & 63] ? plane + 1 : plane;
    }
    return plane;
}
// Blocked (void) flag of tile (wx, wy): bit 0 of the map's tile settings on the tile's effective
// plane, from the snapshot. -1 when that region/plane was not readable, else 0 or 1. Only the
// map's own void flag: loc footprints are not part of it (the client keeps no loc collision map).
int LiveTileVoid(std::uint32_t pid, int wx, int wy, int plane) {
    if (wx < 0 || wy < 0) return -1;
    const int ep = LiveTileEffPlane(pid, wx, wy, plane);
    std::lock_guard<std::mutex> lk(g_terr_mu);
    if (g_terr.pid != pid) return -1;
    auto it = g_terr.regions.find((ep << 16) | ((wx >> 6) << 8) | (wy >> 6));
    if (it == g_terr.regions.end() || !it->second.planeFlagsOk) return -1;
    return (it->second.flag[wx & 63][wy & 63] & 1) ? 1 : 0;
}
// The four corner heights of tile (wx, wy) as the game stands an actor on it: sampled on the
// tile's effective plane (bridges) plus the tile's standing offset. SW SE NE NW.
bool LiveCornerHeights(std::uint32_t pid, int wx, int wy, int plane, std::int32_t out[4]) {
    static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
    const int ep = LiveTileEffPlane(pid, wx, wy, plane);
    const std::int32_t lift = LiveTileLift(pid, wx, wy, ep);
    bool all = true;
    for (int c = 0; c < 4; ++c) { out[c] = LiveCornerHeight(pid, wx + CX[c], wy + CY[c], ep); if (out[c] == kNoFine) all = false; else out[c] += lift; }
    return all;
}

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

    std::string players, npcs, specials, projectiles, effects;
    int prc = 0, efc = 0;
    std::unordered_set<int> seenSpecialUids;
    std::unordered_set<std::uint64_t> seenSpecialTiles;
    auto tileKey = [](int t, int x, int y, int p) -> std::uint64_t {
        return ((std::uint64_t)(std::uint32_t)t << 48) ^ ((std::uint64_t)(std::uint32_t)x << 28) ^
               ((std::uint64_t)(std::uint32_t)y << 4) ^ (std::uint64_t)(std::uint32_t)p;
    };
    int vt4 = 0, vgfx4 = -1;   // diag: type-4 entities seen in the worldview vector + last gfx
    int pc = 0, nc = 0, sc4 = 0;
    int player_x = -1, player_y = -1;  // local player tile (anchors the object range)
    int player_plane = 0;              // local player plane (sec + 0x40)

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
                    // gfx at sec+0x74; position on the entity (ep+0x30/0x38).
                    int gfx  = rpm<std::int32_t>(h, *sec + rtx::scn::kT4Gfx).value_or(-1);
                    auto ex = rpm<float>(h, *ep + 0x30);
                    auto ey = rpm<float>(h, *ep + 0x38);
                    int sx4 = ex ? (int)(*ex / 512.f) : 0;
                    int sy4 = ey ? (int)(*ey / 512.f) : 0;
                    ++vt4; vgfx4 = gfx;
                    if (sx4 > 0 && sy4 > 0 && efc < 64) {    // every world spot animation, by gfx id
                        char eb[128];
                        std::snprintf(eb, sizeof(eb), "%s{\"gfx\":%d,\"x\":%d,\"y\":%d,\"fx\":%d,\"fy\":%d}",
                                      efc ? "," : "", gfx, sx4, sy4, (int)*ex, (int)*ey);
                        effects += eb; ++efc;
                    }
                    if (sx4 > 0 && sy4 > 0) {
                        int uid4 = rpm<std::int32_t>(h, *sec + 0x98).value_or(0);
                        int pl4  = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);   // plane (sec+0x40)
                        if (pl4 < 0 || pl4 > 3) pl4 = 0;
                        if (sc4) specials.push_back(',');
                        char sbuf[160];
                        std::snprintf(sbuf, sizeof(sbuf),
                            "{\"x\":%d,\"y\":%d,\"p\":%d,\"gfx\":%d,\"uid\":%d,\"t\":4,\"w\":1}", sx4, sy4, pl4, gfx, uid4);
                        specials += sbuf; ++sc4;
                        if (uid4) seenSpecialUids.insert(uid4);   // so the hook merge can dedupe
                        seenSpecialTiles.insert(tileKey(4, sx4, sy4, pl4));
                    }
                    continue;
                }
                if (type == 13) {
                    // Type-13 ground markers, two classes (vtables 0xB5ED70 vs 0xB5E878): the clue-scan
                    // marker stores its destination as a fine position at sub+0x74/+0x7C (equal to its own
                    auto ex = rpm<float>(h, *ep + 0x30);
                    auto ey = rpm<float>(h, *ep + 0x38);
                    int sx13 = ex ? (int)(*ex / 512.f) : 0;
                    int sy13 = ey ? (int)(*ey / 512.f) : 0;
                    if (sx13 > 0 && sy13 > 0) {
                        auto dxf = rpm<float>(h, *sec + 0x84);
                        auto dyf = rpm<float>(h, *sec + 0x8C);
                        bool hasDest = dxf && dyf && *dxf > 0.f && *dxf < 1e9f && *dyf > 0.f && *dyf < 1e9f;
                        int dtx = hasDest ? (int)(*dxf / 512.f) : 0;
                        int dty = hasDest ? (int)(*dyf / 512.f) : 0;
                        bool isScan = hasDest && dtx == sx13 && dty == sy13;
                        int pl13 = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);
                        if (pl13 < 0 || pl13 > 3) pl13 = 0;
                        int uid13 = rpm<std::int32_t>(h, *sec + 0x98).value_or(0);
                        if (sc4) specials.push_back(',');
                        char sbuf[192];
                        std::snprintf(sbuf, sizeof(sbuf),
                            "{\"x\":%d,\"y\":%d,\"p\":%d,\"gfx\":-1,\"uid\":%d,\"t\":13,\"k\":\"%s\",\"w\":1}",
                            sx13, sy13, pl13, uid13, isScan ? "scan" : "dest");
                        specials += sbuf; ++sc4;
                        if (uid13) seenSpecialUids.insert(uid13);
                        seenSpecialTiles.insert(tileKey(13, sx13, sy13, pl13));
                    }
                    continue;
                }
                if (type == 5 && prc < 64) {                  // projectile: source and target tiles
                    auto sx = rpm<std::int32_t>(h, *sec + rtx::scn::kProjSrcX), sy = rpm<std::int32_t>(h, *sec + rtx::scn::kProjSrcY);
                    auto dx = rpm<std::int32_t>(h, *sec + rtx::scn::kProjDstX), dy = rpm<std::int32_t>(h, *sec + rtx::scn::kProjDstY);
                    if (sx && sy && dx && dy && *sx > 0 && *sy > 0 && *dx > 0 && *dy > 0 && *sx < 8388608 && *dx < 8388608) {
                        char pb[160];
                        std::snprintf(pb, sizeof(pb), "%s{\"sx\":%d,\"sy\":%d,\"dx\":%d,\"dy\":%d,\"fsx\":%d,\"fsy\":%d,\"fdx\":%d,\"fdy\":%d}",
                                      prc ? "," : "", *sx / 512, *sy / 512, *dx / 512, *dy / 512, *sx, *sy, *dx, *dy);
                        projectiles += pb; ++prc;
                    }
                    continue;
                }
                if (type != 1 && type != 2) continue;        // players + NPCs only

                auto fx = rpm<float>(h, *sec + kPosX);
                auto fy = rpm<float>(h, *sec + kPosY);
                int tx = fx ? (int)(*fx / 512.f) : 0;
                int ty = fy ? (int)(*fy / 512.f) : 0;
                if (tx <= 0 || ty <= 0) continue;
                int ttx = tx, tty = ty;                       // server tile; visible tile when stationary
                actor_true_tile(h, *sec, ttx, tty);
                std::string ovSplats; int ovBar = -1;         // hitsplat ring + head bar (empty / -1 when none)
                actor_overhead_json(h, *sec, ovSplats, ovBar);

                char nm[40] = {0};
                rpm_bytes(h, *sec + kName, nm, sizeof(nm) - 1);
                std::string name;
                for (int j = 0; j < (int)sizeof(nm) && nm[j]; ++j) {
                    unsigned char c = (unsigned char)nm[j];
                    if (c >= 0x20 && c <= 0x7e) name.push_back((char)c);
                    else if (c == 0xA0) name.push_back(' ');   // CP-1252 NBSP appears in display names
                }
                int uid = rpm<std::int32_t>(h, *sec + kUid).value_or(0);

                char buf[256];
                if (type == 2) {                              // player
                    if (name.empty()) continue;               // players always carry a live name
                    bool self = (uid == local_uid);
                    if (self) { player_x = tx; player_y = ty;
                                player_plane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0); }
                    int combat = rpm<std::int32_t>(h, *sec + kCombat).value_or(-1);
                    if (combat < 0 || combat > 5000) combat = -1;
                    int anim = rpm<std::int32_t>(h, *sec + 0xA90).value_or(-1);  // shared actor anim
                    int plane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);   // shared actor plane
                    if (pc) players.push_back(',');
                    std::snprintf(buf, sizeof(buf),
                        "{\"uid\":%d,\"x\":%d,\"y\":%d,\"trueTile\":{\"x\":%d,\"y\":%d},\"plane\":%d,\"combat\":%d,\"anim\":%d,\"self\":%s,\"name\":\"",
                        uid, tx, ty, ttx, tty, plane, combat, anim, self ? "true" : "false");
                    players += buf; players += json_escape(name);
                    players += "\",\"bar\":" + std::to_string(ovBar) + ",\"splats\":" + ovSplats + "}";
                    ++pc;
                } else {                                      // NPC
                    int cfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                    int anim = rpm<std::int32_t>(h, *sec + 0xA90).value_or(-1);  // shared actor anim
                    bool morphHidden = false;
                    auto meta = resolve_npc(h, root.value_or(0), cfg, &morphHidden);
                    std::string npcName = !meta.name.empty() ? meta.name : (morphHidden ? std::string() : name);
                    // Hidden morph variants and nameless decorations are dropped, except a nameless NPC
                    // that carries a head bar: the game hangs object timers on those (the Eternal magic
                    // tree helper, config 31500), and plugins match them by id.
                    if (npcName.empty() && ovBar < 0) continue;
                    std::string acts;
                    for (const auto& a : meta.actions) {
                        if (!acts.empty()) acts.push_back(',');
                        acts += '"'; acts += json_escape(a); acts += '"';
                    }
                    if (nc) npcs.push_back(',');
                    int reportId = (meta.id >= 0) ? meta.id : cfg;
                    // Facing in degrees (-1 unreadable) from the yaw quaternion sec+0x1E0 = w, sec+0x1E8 = y:
                    // heading = 2 * atan2(-y, w). Not 0x1D0/0x1D8, which lags by one step.
                    int face = -1;
                    {
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
                    int npcPlane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);   // shared actor plane
                    std::snprintf(buf, sizeof(buf),
                        "{\"id\":%d,\"uid\":%d,\"x\":%d,\"y\":%d,\"trueTile\":{\"x\":%d,\"y\":%d},\"plane\":%d,\"combat\":%d,\"anim\":%d,\"face\":%d,\"size\":%d,\"name\":\"",
                        reportId, uid, tx, ty, ttx, tty, npcPlane, meta.combat_level, anim, face, meta.size);
                    npcs += buf; npcs += json_escape(npcName);
                    int lp = rpm<std::int32_t>(h, *sec + rtx::scn::kLpCur).value_or(-1);
                    int lpMax = rpm<std::int32_t>(h, *sec + rtx::scn::kLpMax).value_or(-1);
                    if (lp < 0 || lp > 1000000000) lp = -1;
                    if (lpMax <= 0 || lpMax > 1000000000) lpMax = -1;
                    int npcTarget = rpm<std::int32_t>(h, *sec + rtx::scn::kNpcTarget).value_or(-1);
                    if (npcTarget < -1 || npcTarget >= 4096) npcTarget = -1;
                    npcs += "\",\"actions\":["; npcs += acts;
                    npcs += "],\"lp\":" + std::to_string(lp) + ",\"lpMax\":" + std::to_string(lpMax) +
                            ",\"target\":" + std::to_string(npcTarget) +
                            ",\"bar\":" + std::to_string(ovBar) + ",\"splats\":" + ovSplats + "}";
                    ++nc;
                }
            }
        }
    }
    struct Obj { int id, x, y, plane, type, dist; std::string name, acts; bool rt; bool vis;
                 int w = 1, h = 1; };   // footprint in tiles, rotation-corrected; x/y = SW anchor
    std::vector<Obj> objs;
    if (player_x >= 0) {
        if (obj_range < 1)   obj_range = 1;
        if (obj_range > 128) obj_range = 128;   // a full 8x8 instance floor
        constexpr int kCollectCap = 4000;   // safety bound before sorting
        std::unordered_set<long long> seen;
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
                    int fw = meta.dim_x, fh = meta.dim_y;
                    if (p.rotation & 1) std::swap(fw, fh);
                    objs.push_back({ p.id, wx, wy, p.plane, p.type, dist, meta.name, std::move(acts), false, true, fw, fh });
                }
            }
        }
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
                int tol = std::max(meta.dim_x, meta.dim_y) - 1; if (tol < 0) tol = 0;
                bool dup = !seen.insert(dk).second;          // already have it from the cache (exact tile)
                // The cache lists a loc whether or not the game is showing it; the live entity knows.
                // A depleted tree (and a stump waiting to be shown) carries the hidden flag, so the
                // cache entry's vis follows the live flag. Match on id, plane and footprint tolerance.
                for (auto& o : objs)
                    if (!o.rt && o.id == r.config_id && o.plane == r.plane &&
                        std::abs(o.x - r.x) <= tol && std::abs(o.y - r.y) <= tol) { dup = true; if (r.hidden) o.vis = false; }
                if (dup) continue;
                std::string acts;
                for (const auto& a : meta.actions) {
                    if (!acts.empty()) acts.push_back(',');
                    acts += '"'; acts += json_escape(a); acts += '"';
                }
                int fw = meta.dim_x, fh = meta.dim_y, ox = r.x, oy = r.y;
                if (r.bmax[0] > r.bmin[0] && r.bmax[1] > r.bmin[1]) {
                    int tw = (int)std::lround((r.bmax[0] - r.bmin[0]) / 512.0f);
                    int th = (int)std::lround((r.bmax[1] - r.bmin[1]) / 512.0f);
                    int ax = (int)std::floor(r.bmin[0] / 512.0f);
                    int ay = (int)std::floor(r.bmin[1] / 512.0f);
                    if (tw >= 1 && tw <= 16 && th >= 1 && th <= 16 &&
                        std::abs(ax - r.x) <= 8 && std::abs(ay - r.y) <= 8) {
                        fw = tw; fh = th; ox = ax; oy = ay;
                    }
                }
                objs.push_back({ r.config_id, ox, oy, r.plane, -1, dist, meta.name, std::move(acts), true,
                                 r.bmax[0] > r.bmin[0] && !r.hidden,   // degenerate AABB or hidden flag = not shown
                                 fw, fh });
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
            "{\"id\":%d,\"x\":%d,\"y\":%d,\"plane\":%d,\"type\":%d,\"dist\":%d,\"w\":%d,\"h\":%d,\"rt\":%s,\"vis\":%s,\"name\":\"",
            o.id, o.x, o.y, o.plane, o.type, o.dist, o.w, o.h, o.rt ? "true" : "false",
            o.vis ? "true" : "false");
        objects += buf; objects += json_escape(o.name);
        objects += "\",\"actions\":["; objects += o.acts; objects += "]}";
        ++oc;
    }

    std::uint32_t sdiag[12] = { 0,0,0,0,0,0,0,0,0,0,0,0 };   // ...+gfxhits/t4hits/MB/sub
    {
        std::vector<RuntimeHi> highs;
        if (ReadRuntimeHighlights(pid, highs, sdiag)) {
            std::uint32_t now = (std::uint32_t)GetTickCount64();
            for (const auto& hi : highs) {
                if ((std::uint32_t)(now - hi.stamp) > rtx::special::kStaleMs) continue;   // stale
                if (hi.uid && seenSpecialUids.count(hi.uid)) continue;   // already emitted by the walk
                if (!hi.uid && hi.x > 0 && hi.y > 0 &&
                    seenSpecialTiles.count(tileKey(hi.type, hi.x, hi.y, hi.plane))) continue;
                if (sc4) specials.push_back(',');
                const char* kind = "";
                switch (hi.kind) {
                    case rtx::special::kKindScan:   kind = "scan";   break;
                    case rtx::special::kKindDest:   kind = "dest";   break;
                    case rtx::special::kKindAdorn:  kind = "adorn";  break;
                    case rtx::special::kKindEffect: kind = "effect"; break;
                    default: break;
                }
                char sbuf[200];
                std::snprintf(sbuf, sizeof(sbuf),
                    "{\"x\":%d,\"y\":%d,\"p\":%d,\"gfx\":%d,\"uid\":%d,\"t\":%d,\"k\":\"%s\",\"w\":%d}",
                    hi.x, hi.y, hi.plane, hi.gfx, hi.uid, hi.type, kind, (hi.x > 0 && hi.y > 0) ? 1 : 0);
                specials += sbuf; ++sc4;
            }
        }
    }
    char sdbuf[192];
    std::snprintf(sdbuf, sizeof(sdbuf), "[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]", sdiag[0], sdiag[1], sdiag[2], sdiag[3], sdiag[4], sdiag[5], sdiag[6], sdiag[7], sdiag[8], sdiag[9], sdiag[10], sdiag[11]);
    char vdbuf[48];
    std::snprintf(vdbuf, sizeof(vdbuf), "[%d,%d]", vt4, vgfx4);   // worldview-vector type-4 count + last gfx

    std::string walk = "null";
    if (h && mgva) {
        auto wroot = rpm<std::uint64_t>(h, mgva);
        std::uint64_t mmp = 0;
        if (wroot && *wroot > 0x10000) {
            auto mmv = rpm<std::uint64_t>(h, *wroot + rtx::scn::kMiniMap);
            if (mmv) mmp = *mmv;
        }
        if (mmp > 0x10000) {
            const std::uint64_t* mm = &mmp;
            auto dfx = rpm<std::int32_t>(h, *mm + rtx::scn::kDestX);
            auto dfy = rpm<std::int32_t>(h, *mm + rtx::scn::kDestY);
            auto dsr = rpm<std::uint8_t>(h, *mm + rtx::scn::kDestSrc);
            if (dfx && dfy && *dfx > 0 && *dfy > 0) {
                char wbuf[160];
                std::snprintf(wbuf, sizeof(wbuf),
                    "{\"x\":%d,\"y\":%d,\"fx\":%d,\"fy\":%d,\"src\":%d}",
                    *dfx / 512, *dfy / 512, *dfx, *dfy, (int)dsr.value_or(0));
                walk = wbuf;
            }
        }
    }

    return "{\"players\":[" + players + "],\"npcs\":[" + npcs +
           "],\"objects\":[" + objects + "],\"specials\":[" + specials + "],\"walk\":" + walk +
           ",\"projectiles\":[" + projectiles + "],\"effects\":[" + effects + "]" +
           ",\"sdiag\":" + sdbuf + ",\"vdiag\":" + vdbuf + "}";
}

// View-projection matrix offset drifts across builds (0x13030 -> 0x13070 -> 0x13970 on 949).

// Interface manager: mainData+0x19900 (0x198C0 through 949-5) -> owner; open-group array begin/end at owner+0x50/+0x58 on 950-1 (was a container at owner+0x30, +0x58/+0x60). Entries 0x10 wide: id@+0, group obj@+8.
// Widget node (950-1): component ids i16 @+0x38/+0x3A/+0x3C, rect x/y/w/h @+0x98..+0xA4, text ptr @+0xB8, SSO string / graphic key union @+0x1B0, item id @+0x1D8, stack @+0x1E0, child vectors @+0x1D0/+0x1B8/+0x200. Hidden flag (was +0x50) not re-derived.
constexpr std::uint64_t kIfaceGroupsBegin = 0x50;
constexpr std::uint64_t kIfaceGroupsEnd   = 0x58;

// Gameview (3D viewport) rect from the interface tree: group 1477 = game frame, 1477:27 -> 1477:28 =
static bool read_gameview_rect(HANDLE h, std::uint64_t mainData,
                               int& gx, int& gy, int& gw, int& gh,
                               int* rootW = nullptr, int* rootH = nullptr) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t owner = r64(mainData + 0x19900);
    if (owner <= 0x10000) return false;
    std::uint64_t gs = r64(owner + kIfaceGroupsBegin), ge = r64(owner + kIfaceGroupsEnd);
    if (gs <= 0x10000 || ge <= gs || (ge - gs) % 0x10 || (ge - gs) > 0x200000) return false; // UI-container gate
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != 1477) continue;            // group 1477 only
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) return false;
        if (rootW && rootH) {
            long long best = 0;
            for (std::uint64_t wn = a; wn + 0x18 <= b; wn += 0x18) {
                std::uint64_t nd = r64(wn);
                if (nd <= 0x10000) continue;
                int w = r32(nd + 0xa0), hh = r32(nd + 0xa4);
                long long area = (long long)w * hh;
                if (w > 0 && hh > 0 && area > best) { best = area; *rootW = w; *rootH = hh; }
            }
        }
        // Since the 2026-09-14 interface update 1477:28 is its own root rather than a child of 1477:27, so
        // the child walk below finds nothing. Find both nodes directly in the group's node list first.
        std::uint64_t n27 = 0, n28 = 0;
        for (std::uint64_t wn = a; wn + 0x18 <= b; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd <= 0x10000 || r16(nd + 0x38) != 1477 || r16(nd + 0x3c) != -1) continue;
            const int cid = r16(nd + 0x3a);
            if (cid == 27 && !n27) n27 = nd; else if (cid == 28 && !n28) n28 = nd;
            if (n27 && n28) break;
        }
        if (n28) {
            int cw = r32(n28 + 0xa0), chh = r32(n28 + 0xa4);
            if (cw > 0 && chh > 0) {
                gx = r32(n28 + 0x98); gy = r32(n28 + 0x9c); gw = cw; gh = chh;
                return true;
            }
        }
        for (std::uint64_t wn = a; wn + 0x18 <= b; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd <= 0x10000) continue;
            if (r16(nd + 0x38) != 1477 || r16(nd + 0x3a) != 27 || r16(nd + 0x3c) != -1) continue;  // 1477:27
            int px = r32(nd + 0x98), py = r32(nd + 0x9c);             // parent (accumulates into child)
            const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };      // child-array offsets
            for (int k = 0; k < 3; ++k) {
                std::uint64_t cs = r64(nd + co[k]), ce = r64(nd + co[k] + 8);
                std::uint64_t ca = cs + 8, cb = ce + 8;
                if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
                    std::uint64_t ch = r64(c);
                    if (ch <= 0x10000) continue;
                    std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;                         // child lives in a separate alloc
                    if (r16(ch + 0x38) != 1477 || r16(ch + 0x3a) != 28 || r16(ch + 0x3c) != -1) continue;  // 1477:28
                    int cw = r32(ch + 0xa0), chh = r32(ch + 0xa4);
                    if (cw <= 0 || chh <= 0) return false;
                    gx = px + r32(ch + 0x98); gy = py + r32(ch + 0x9c); gw = cw; gh = chh;
                    return true;
                }
            }
            return false;                                             // found 1477:27 but no child 28
        }
        return false;                                                 // group present, no 27 node
    }
    return false;                                                     // group 1477 absent (not in-world / login)
}

static float iface_ui_scale(HANDLE h, std::uint64_t root, std::uint32_t pid,
                            int* outVw = nullptr, int* outGw = nullptr) {
    static std::mutex mu;
    struct Ent { unsigned long long ms; float ui; int vw, gw; };
    static std::unordered_map<std::uint32_t, Ent> cache;
    const unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = cache.find(pid);
        if (it != cache.end() && now - it->second.ms < 500) {
            if (outVw) *outVw = it->second.vw;
            if (outGw) *outGw = it->second.gw;
            return it->second.ui;
        }
    }
    float ui = 1.0f;
    int gx = 0, gy = 0, gw = 0, gh = 0, vw = 0;
    if (read_gameview_rect(h, root, gx, gy, gw, gh) && gw > 0) {
        vw = read_varc(h, root, 3001);   // view 1000 (gameview) width, physical px
        if (vw > 0) {
            float r = (float)vw / (float)gw;
            if (r > 0.2f && r < 5.0f) ui = r;
        }
    } else {
        gw = 0;
    }
    if (outVw) *outVw = vw;
    if (outGw) *outGw = gw;
    std::lock_guard<std::mutex> lk(mu);
    cache[pid] = { now, ui, vw, gw };
    return ui;
}

static std::string iface_encode_text(const char* buf, int len) {
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
    if (cap <= 0) return {};
    int first = (int)std::min<std::uint64_t>((std::uint64_t)cap, 0x1000 - (p & 0xFFF));
    if (!rpm_bytes(h, p, buf, first)) return {};
    int len = 0;
    while (len < first && buf[len]) ++len;
    if (len == first && first < cap) {
        if (!rpm_bytes(h, p + first, buf + first, cap - first)) return {};
        while (len < cap && buf[len]) ++len;
    }
    return iface_encode_text(buf, len);
}

// Display text at the usual field (*(node+0x90), legacy I_textP).
static std::string iface_text(HANDLE h, std::uint64_t node) {
    return iface_text_at(h, node, 0xb8, 127);
}

// Second text member at node+0x180: 24-byte SSO string. Flag byte +0x197: 0x80 = heap {ptr@+0x180,
// size@+0x188, cap@+0x190}; else inline chars at +0x180, size = 23 - flag. Holds dialogue options, cooldowns, chat.
static std::string iface_sso_text(HANDLE h, std::uint64_t node) {
    std::uint8_t raw[0x18];
    if (!rpm_bytes(h, node + 0x1b0, raw, sizeof(raw))) return {};
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
    // declared length is junk, not a string. 0xFF is the empty jstring sentinel, treated as non-text.
    int printable = 0;
    for (int i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0) return {};                    // shorter than declared -> not a string
        if (c >= 0x20 && c != 0xFF) ++printable;
    }
    if (len < 1 || printable < len) return {};
    return iface_encode_text(buf, len);
}

// ---- 950-1 interface component node ---------------------------------------------------------------
// Verified live (2026-09-13) against the js5 definitions of the same components:
//   +0x00 class table pointer (one per component class: layer, rect, three text classes, graphic, model)
//   +0x38 group u16, +0x3A comp i16, +0x3C sub i16 (-1 = static comp), +0x3E parent comp i16
//   +0x60 def-derived flag bits (0x200 mirrors the definition's hidden byte; NOT the live visibility)
//   +0x98/+0x9C x/y parent-relative, +0xA0/+0xA4 w/h, +0xA8 colour (rect fill / text / graphic tint)
//   +0x1A8 sprite id (graphic classes; other classes reuse the slot), +0x1B0 24-byte union: SSO string on
//   text classes, item key 0x4000000002000000|item on item icons, graphic key otherwise
//   +0x1D8 item id, +0x1E0 item amount
//   +0x1D0 / +0x1B8 / +0x200 child vectors {begin,end} of 0x18-byte entries {+0 tagged pointer, +8 node}.
// LIVE VISIBILITY is bit 0 of the entry's +0 word in the vector that holds the node (root widgets use the
// same entry format at group+0x20/+0x28); a hidden entry hides its whole subtree. The engine flips that bit
// for if_sethide, which is why 1477's panel frames all carry 0x200 at +0x60 yet only the docked ones draw.
constexpr std::size_t kIfaceNodeBytes = 0x210;
struct IfaceNode {
    std::uint64_t addr = 0, kind = 0;
    int group = 0, comp = -1, sub = -1, parent = -1;
    int x = 0, y = 0, w = 0, h = 0;
    std::uint32_t colour = 0, flags = 0;
    int sprite = -1, item = 0, amount = 0;
    std::uint64_t key = 0;
    std::uint8_t raw[kIfaceNodeBytes];
};
struct IfaceChildRef { std::uint64_t addr; bool hidden; };
static void iface_groups_range(HANDLE h, std::uint64_t mainData, std::uint64_t& gs, std::uint64_t& ge);

static bool iface_read_node(HANDLE h, std::uint64_t addr, IfaceNode& n) {
    if (addr <= 0x10000 || addr >= 0x7ff000000000ull) return false;
    if (!rpm_bytes(h, addr, n.raw, sizeof(n.raw))) return false;
    auto u64 = [&](std::size_t o){ std::uint64_t v; std::memcpy(&v, n.raw + o, 8); return v; };
    auto i32 = [&](std::size_t o){ std::int32_t v; std::memcpy(&v, n.raw + o, 4); return (int)v; };
    auto i16 = [&](std::size_t o){ std::int16_t v; std::memcpy(&v, n.raw + o, 2); return (int)v; };
    n.addr = addr; n.kind = u64(0);
    n.group = (int)(std::uint16_t)i16(0x38); n.comp = i16(0x3a); n.sub = i16(0x3c); n.parent = i16(0x3e);
    n.x = i32(0x98); n.y = i32(0x9c); n.w = i32(0xa0); n.h = i32(0xa4);
    n.colour = (std::uint32_t)i32(0xa8); n.flags = (std::uint32_t)i32(0x60);
    n.sprite = i32(0x1a8); n.key = u64(0x1b0); n.item = i32(0x1d8); n.amount = i32(0x1e0);
    return true;
}

// Entries of one {begin,end} vector: +0 tagged pointer (bit 0 = hidden), +8 node pointer.
static void iface_entry_refs(HANDLE h, std::uint64_t begin, std::uint64_t end, std::vector<IfaceChildRef>& out) {
    if (!begin || !end || begin <= 0x10000 || end < begin || (end - begin) > 0x100000) return;
    std::vector<std::uint8_t> blk((std::size_t)(end - begin));
    if (blk.empty() || !rpm_bytes(h, begin, blk.data(), blk.size())) return;
    for (std::size_t o = 0; o + 0x10 <= blk.size(); o += 0x18) {
        std::uint64_t tag, ch; std::memcpy(&tag, blk.data() + o, 8); std::memcpy(&ch, blk.data() + o + 8, 8);
        if (ch <= 0x10000 || ch >= 0x7ff000000000ull) continue;     // nodes are heap objects; image pointers are union payload
        std::int64_t d = (std::int64_t)(begin + o + 8) - (std::int64_t)ch; if (d < 0) d = -d;
        if (d <= 0x3000) continue;                                    // child lives in a separate alloc
        out.push_back({ ch, (tag & 1) != 0 });
    }
}
static void iface_child_refs(HANDLE h, const IfaceNode& n, std::vector<IfaceChildRef>& out) {
    const std::size_t co[3] = { 0x1d0, 0x1b8, 0x200 };   // order matters for the inspector's listing
    for (std::size_t off : co) {
        std::uint64_t cs, ce; std::memcpy(&cs, n.raw + off, 8); std::memcpy(&ce, n.raw + off + 8, 8);
        iface_entry_refs(h, cs, ce, out);
    }
}
static void iface_child_refs(HANDLE h, std::uint64_t node, std::vector<IfaceChildRef>& out) {
    IfaceNode n;
    if (iface_read_node(h, node, n)) iface_child_refs(h, n, out);
}
static std::uint64_t iface_group_obj(HANDLE h, std::uint64_t main_data, int gid) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, main_data, gs, ge);
    if (!gs) return 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap = r64(g + 8);
        if (ap > 0x10000 && r32(ap) == gid) return ap;
    }
    return 0;
}
// Root widgets of a group object: same entry format at +0x20/+0x28.
static void iface_root_refs(HANDLE h, std::uint64_t group_obj, std::vector<IfaceChildRef>& out) {
    if (group_obj <= 0x10000) return;
    std::uint64_t ws = rpm<std::uint64_t>(h, group_obj + 0x20).value_or(0), we = rpm<std::uint64_t>(h, group_obj + 0x28).value_or(0);
    iface_entry_refs(h, ws, we, out);
}

// Component class -> definition type, learned from the js5 definitions of static comps as they are seen
// (the class table pointers move every build; the mapping is rebuilt per process for free). -1 = unknown.
static std::mutex g_ifaceKindMu;
static std::unordered_map<std::uint64_t, int> g_ifaceKindType;
static int iface_learn_type(const IfaceNode& n) {
    if (n.sub == -1 && n.group > 0 && n.comp >= 0) {
        rtx::cache::IfaceCompDefLite d;
        if (rtx::cache::IfaceCompDefLookup(n.group, n.comp, d) && d.type >= 0) {
            std::lock_guard<std::mutex> lk(g_ifaceKindMu);
            g_ifaceKindType[n.kind] = d.type;
            return d.type;
        }
    }
    std::lock_guard<std::mutex> lk(g_ifaceKindMu);
    auto it = g_ifaceKindType.find(n.kind);
    return it == g_ifaceKindType.end() ? -1 : it->second;
}
static const char* iface_type_name(int t) {
    switch (t) { case 0: return "layer"; case 3: return "rect"; case 4: return "text"; case 5: return "graphic";
                 case 6: return "model"; case 9: return "line"; default: return t < 0 ? "" : "other"; }
}

// Full live tree of one group as JSON widgets. "r"=[relX,relY,w,h], "a"=[absX,absY] when an origin is known,
// "v":1 only when the widget is actually drawn (its entry and every ancestor's entry unhidden), "p" parent
// comp, "col" colour on rect/text/graphic, "ty" from the learned class map (payload heuristics as fallback).
constexpr int kIfaceWalkCap = 12000, kIfaceDynPerParent = 150;
static void iface_walk(HANDLE h, int group, std::uint64_t node, int depth,
                       std::string& out, int& count, bool& first, int baseX, int baseY, bool haveAbs, bool hidden) {
    if (count >= kIfaceWalkCap || depth > 12) return;
    IfaceNode n;
    if (!iface_read_node(h, node, n)) return;
    const int ax = baseX + n.x, ay = baseY + n.y;        // absolute screen position of this node
    int type = iface_learn_type(n);
    std::string txt;
    if (type < 0 || type == 4) {
        txt = iface_sso_text(h, node);                   // +0x1B0 SSO member (labels, options, chat)
        if (txt.empty() && type < 0) txt = iface_text(h, node);   // legacy pointer slot, unused on 950-1
    }
    const bool sprUnset = n.key == ~0ull;
    const bool sprIsItem = !sprUnset && (n.key >> 62) == 1 && (n.key & 0xFFFFFF) < 200000;
    // A real item icon carries the item key 0x4000000002000000|item (or the unset sentinel on currency pouches).
    const bool realItem = (n.item > 0 && n.item < 200000) && (sprUnset || (sprIsItem && (int)(n.key & 0xFFFFFF) == n.item) ||
                                                              n.key == 0x60000ull + (std::uint64_t)n.item);
    int spr = 0;
    if (type == 5 || type < 0) { if (n.sprite > 0 && n.sprite < 0x100000) spr = n.sprite; }
    if (sprIsItem && !realItem) spr = 131072 + (int)(n.key & 0xFFFFFF);   // obj-icon graphic without an item field
    std::vector<IfaceChildRef> kids; iface_child_refs(h, n, kids);
    const char* ty;
    if (type >= 0) ty = realItem ? "item" : iface_type_name(type);
    else ty = realItem ? "item" : !txt.empty() ? "text" : (spr > 0 && n.w > 0 && n.h > 0) ? "graphic" : !kids.empty() ? "layer" : "rect";   // class not learned yet
    out += first ? "" : ",";
    out += "{\"g\":" + std::to_string(group) + ",\"t\":[" + std::to_string(n.group) + "," +
           std::to_string(n.comp) + "," + std::to_string(n.sub) + "],\"d\":" + std::to_string(depth) +
           ",\"ty\":\"" + ty + "\",\"r\":[" + std::to_string(n.x) + "," + std::to_string(n.y) + "," +
           std::to_string(n.w) + "," + std::to_string(n.h) + "]";
    if (haveAbs) out += ",\"a\":[" + std::to_string(ax) + "," + std::to_string(ay) + "]";
    if (n.parent >= 0)         out += ",\"p\":" + std::to_string(n.parent);
    if (!txt.empty())          out += ",\"x\":\"" + txt + "\"";
    if (realItem)              out += ",\"it\":" + std::to_string(n.item);
    if (realItem && n.amount > 0) out += ",\"n\":" + std::to_string(n.amount);          // stack / amount
    if (spr > 0 && !realItem)  out += ",\"s\":" + std::to_string(spr);
    if ((type == 3 || type == 4 || type == 5) && n.colour) { char cb[16]; std::snprintf(cb, sizeof(cb), "%06X", n.colour & 0xFFFFFF); out += ",\"col\":\""; out += cb; out += "\""; }
    if (!hidden)               out += ",\"v\":1";
    out += "}";
    first = false; ++count;
    // Big grids (the bank's hundreds of item slots) would swallow the whole budget with dynamic children, so
    // every static comp is always emitted and each parent contributes at most kIfaceDynPerParent dynamic ones.
    int dyn = 0;
    for (const auto& k : kids) {
        if (count >= kIfaceWalkCap) break;
        IfaceNode peek;
        if (!iface_read_node(h, k.addr, peek)) continue;
        if (peek.sub >= 0 && ++dyn > kIfaceDynPerParent) continue;
        iface_walk(h, group, k.addr, depth + 1, out, count, first, ax, ay, haveAbs, hidden || k.hidden);
    }
}

static void iface_groups_range(HANDLE h, std::uint64_t mainData,
                               std::uint64_t& gs, std::uint64_t& ge) {
    gs = ge = 0;
    std::uint64_t owner = rpm<std::uint64_t>(h, mainData + 0x19900).value_or(0);
    if (owner <= 0x10000) return;
    std::uint64_t s = rpm<std::uint64_t>(h, owner + kIfaceGroupsBegin).value_or(0);
    std::uint64_t e = rpm<std::uint64_t>(h, owner + kIfaceGroupsEnd).value_or(0);
    if (s <= 0x10000 || e <= s || (e - s) % 0x10 || (e - s) > 0x200000) return;
    gs = s; ge = e;
}

// Companion-published var by (scope,id): 4 = varp/varbit, 5 = varc-int.
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

// FALLBACK ONLY (see iface_panel_origin): origins now come from the engine's sub-interface table, which
// gives the exact parent-component rect for every attached group. This manual varc / mount table is
// consulted only for groups the engine has not attached; its offsets compensate varc-vs-content chrome.
// Movable-panel origin table: a group's origin = its X/Y varc-ints (scope 5); dialogues share 9102/9103.
struct PanelOriginSpec { int group; int var_x; int var_y; int off_left; int off_top; int mount_comp; bool is_varc; int req_group; };
static const PanelOriginSpec kPanelOrigins[] = {
    { 1477, -1, -1, 0, 0, 0 },    // HUD root / game frame: tree is screen-absolute, identity spec so "a" is emitted
    { 1188, 3082, 3083, 0, 0, 0, true },   // Choose dialog (option select) -- centered; position via live VARC 3082/3083
    { 1184, 3082, 3083, 0, 0, 0, true },   // NPC dialog
    { 1191, 3082, 3083, 0, 0, 0, true },   // Player dialog
    { 1189, 3082, 3083, 0, 0, 0, true },   // Clue continue
    { 1186, 3082, 3083, 0, 0, 0, true },   // Server message dialog
    { 1552, 3096, 3097, 0, 0, 0, true },   // Serenity posts pose-select; chrome inset (2,16) is applied by the Agility plugin
    { 1603, 9102, 9103, 0, 0 },   // Input text
    { 1370, 3089, 3090, 0, -9, 0, true },  // Item Production content, always inside the 1371 frame (varcs 3089/3090)
    { 13,   3089, 3090, 0, 31, 0, true },  // Bank pin -- window-frame varcs 3089/3090
    { 1466, 3166, 3167, 0, 0, 0, true },   // Skills; varc 3165 == 1 while open (draw gate for the XP bars)
    { 1224, 3089, 3090, 0, 0, 0, true },   // Ritual selection (Necromancy! communion ritual) -- window-frame varcs 3089/3090
    { 533,  3089, 3090, 0, 0, 0, true },   // Display case -- window-frame varcs 3089/3090, content centred in the frame
    { 1286, 3089, 3090, 0, 0, 0, true },   // Discovered Ratings (Fish Flingers) -- window-frame varcs 3089/3090
    // House Controls 1665: frame slot comp varies with docking, so key off its own varcs 3096/3097.
    { 1665, 3096, 3097, -4, -16, 0, true },
    { 1223, 3096, 3097, 0, 0, 0, true },   // Active ritual (Necromancy!) -- position varcs 3096/3097
    { 923,  3096, 3097, 0, 0, 0, true },   // Fish Flingers competition results -- position varcs 3096/3097
    { 919,  3047, 3048, 0, 0, 0, true },   // Fish Flingers live scoreboard -- varcs 3047/3048
    { 1222, 6463, 6464, 0, 0, 0, true },   // Well of Souls talent tree (Necromancy!) -- varcs 6463/6464 (central-overlay family)
    { 660,  9121, 9122, 0, -9 },  // Material storage -- UNVERIFIED varp path (flip to its varcs once live-checked)
    { 656,  6310, 6311, 0, 0, 728, true }, // Museum / Training Weapons donation -- varcs 6310/6311, mount 1477:728 fallback
    { 691,  6463, 6464, 0, 0, 0, true },   // Relic power -- varcs 6463/6464 (central-overlay family)
    { 1594, 6463, 6464, 0, 0, 0, true },   // Shop (e.g. Ezreal's) -- varcs 6463/6464
    { 517,  5632, 5633, 0, 0, 0, true },   // Bank -- varcs 5632/5633
    { 190,  5846, 5847, 0, -16, 0, true }, // Quest -- varcs 5846/5847; off_top -16 nudges widgets down 16px
    { 1092, 6463, 6464, 0, 0, 0, true },   // Lodestone network -- varcs 6463/6464
    { 584,  10071, 10072, 0, 0 }, // DXP timer -- UNVERIFIED varp path (flip to its varcs once live-checked)
    { 1473, 3040, 3041, 0, 0, 0, true },   // Inventory (backpack) -- varcs 3040/3041, read fresh from the hashmap each frame
    // Celtic-knot puzzle layouts: varcs 6310/6311; 1477 mount slot 728 as last-ditch fallback.
    { 394, 6310, 6311, 0, 0, 728, true }, { 519, 6310, 6311, 0, 0, 728, true }, { 525, 6310, 6311, 0, 0, 728, true },
    { 526, 6310, 6311, 0, 0, 728, true }, { 529, 6310, 6311, 0, 0, 728, true }, { 1000, 6310, 6311, 0, 0, 728, true },
    { 1001, 6310, 6311, 0, 0, 728, true }, { 1002, 6310, 6311, 0, 0, 728, true }, { 1003, 6310, 6311, 0, 0, 728, true },
    { 1934, 6463, 6464, 0, 0, 0, true },  // Towers (Skyscrapers, master clue). Positioned by VARC 6463 (x) / 6464 (y).
    { 518,  6463, 6464, 0, 0, 0, true },  // Deduction notes (Secrets of Amberfell). Same position varcs as the towers panel.
    // Player-Owned Ports screens are slot-relative to the 800x600 central-large slot (1477:724) at varcs 6463/6464.
    { 916,  6463, 6464, 0, 0, 0, true },  // Ports ship view (shipyard / crew window / voyages)
    { 1276, 6463, 6464, 0, 0, 0, true },  // Ports Crew Roster
    { 1933, 3089, 3090, 0, -8, 0, true },  // Lockbox (master clue) -- varcs 3089/3090, no mount fallback
    { 1371, 3089, 3090, 0, -9, 0, true },  // Make-x / potion crafting -- varcs 3089/3090; off_top -9 nudges widgets down 9px
    { 720,  0, 0, 0, 0, 735, false },      // Option-select window: central interface pre-centred for the 512x334 slot 1477:735
    { 743,  6423, 6424, 0, 0, 0, true },  // Extra action button -- varcs 6423/6424; box comp 7 (38x38 button), root is 150x56
    { 1512, 0, 0, 0, 0, 722 },    // Build-mode furniture sidebar: always open (gate on 1514), viewport-anchored via 1477:722;
    { 1030, 6463, 6464, 0, 0, 0, true },    // "Link a clue" investigation board -- varcs 6463/6464, no mount fallback
    { 1518, 6463, 6464, 0, 0, 726, true },  // Furniture Storage: tree persists while hidden, so gate on 1514; comp 19 subs
    { 1516, 6463, 6464, 0, 0, 726, true },  // Furniture Construction -- varcs 6463/6464, fallback 1477:726; box comp 23 = construct slot
    { 1931, 0, 0, 0, -12, 732 },  // Sliding puzzle box: centered central interface, origin = live rect of 1477 mount comp 732
};
static std::mutex g_ifaceOffMu;
static std::unordered_map<int, std::pair<int,int>> g_ifaceOff;
void SetIfaceOffset(int gid, int dx, int dy) {
    std::lock_guard<std::mutex> lk(g_ifaceOffMu);
    if (dx == 0 && dy == 0) g_ifaceOff.erase(gid);
    else g_ifaceOff[gid] = { dx, dy };
}

// Absolute (group-tree) positions of every top-level comp (sub == -1, non-empty rect) in group `gid`,
// accumulated through the child vectors from the group's root widgets. One walk per group per 100 ms,
// each node fetched as a single block, because the origin resolver asks for many game-frame slots a frame.
struct IfaceGroupPos { unsigned long long ms = 0; std::unordered_map<int, std::pair<int,int>> pos; };
static IfaceGroupPos iface_walk_positions(HANDLE h, std::uint64_t main_data, int gid) {
    IfaceGroupPos gp;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, main_data, gs, ge);
    if (!gs) return gp;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != gid) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        int visited = 0;
        std::vector<std::uint8_t> vec;
        std::function<void(std::uint64_t,int,int,int)> walk =
            [&](std::uint64_t node, int bx, int by, int depth) {
            if (depth > 14 || visited > 30000) return;
            std::uint8_t nb[0x210];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) return;
            ++visited;
            auto i32 = [&](std::size_t o){ std::int32_t v; std::memcpy(&v, nb + o, 4); return (int)v; };
            auto i16 = [&](std::size_t o){ std::int16_t v; std::memcpy(&v, nb + o, 2); return (int)v; };
            auto u64 = [&](std::size_t o){ std::uint64_t v; std::memcpy(&v, nb + o, 8); return v; };
            const int ax = bx + i32(0x98), ay = by + i32(0x9c);
            if (i16(0x3c) == -1 && i32(0xa0) > 0 && i32(0xa4) > 0) gp.pos.emplace(i16(0x3a), std::make_pair(ax, ay));
            const std::size_t co[3] = { 0x1d0, 0x1b8, 0x200 };
            for (int k = 0; k < 3; ++k) {
                std::uint64_t cs = u64(co[k]), ce = u64(co[k] + 8);
                std::uint64_t ca = cs + 8, cb = ce + 8;
                if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
                vec.resize((std::size_t)(cb - ca));
                if (!rpm_bytes(h, ca, vec.data(), vec.size())) continue;
                std::vector<std::uint64_t> kids;
                for (std::size_t o = 0; o + 8 <= vec.size(); o += 0x18) {
                    std::uint64_t ch; std::memcpy(&ch, vec.data() + o, 8);
                    if (ch <= 0x10000 || ch >= 0x7ff000000000ull) continue;   // nodes are heap objects; an image pointer here is a union payload
                    std::int64_t d = (std::int64_t)(ca + o) - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;                         // child lives in a separate alloc
                    kids.push_back(ch);
                }
                for (std::uint64_t ch : kids) walk(ch, ax, ay, depth + 1);   // vec is reused by the callee
            }
        };
        for (std::uint64_t wn = a; wn + 0x18 <= b; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd > 0x10000) walk(nd, 0, 0, 0);
        }
        break;
    }
    return gp;
}

static bool iface_comp_abs(HANDLE h, std::uint64_t main_data, int gid, int comp, int& ox, int& oy) {
    static std::mutex mu;
    static std::unordered_map<std::uint64_t, IfaceGroupPos> cache;   // (main_data, gid) -> positions
    const std::uint64_t key = (main_data << 8) ^ (std::uint64_t)(std::uint32_t)gid;
    const unsigned long long now = GetTickCount64();
    auto lookup = [&](const IfaceGroupPos& gp) {
        auto it = gp.pos.find(comp);
        if (it == gp.pos.end()) return false;
        ox = it->second.first; oy = it->second.second;
        return true;
    };
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = cache.find(key);
        if (it != cache.end() && now - it->second.ms < 100) return lookup(it->second);
    }
    IfaceGroupPos fresh = iface_walk_positions(h, main_data, gid);   // walk outside the lock
    fresh.ms = now;
    std::lock_guard<std::mutex> lk(mu);
    if (cache.size() > 512) cache.clear();
    IfaceGroupPos& gp = cache[key];
    gp = std::move(fresh);
    return lookup(gp);
}

static bool read_iface_mount_origin(HANDLE h, std::uint64_t main_data, int mount_comp, int& ox, int& oy) {
    return iface_comp_abs(h, main_data, 1477, mount_comp, ox, oy);
}

// Sub-interface table (950-1). The interface owner keeps a hash map of every attached sub-interface, keyed by
// the PARENT component hash ((group << 16) | comp): owner+0xE0 = bucket array, owner+0xE8 = bucket count,
// owner+0xF0 = entry count. Entry: +0 u32 parent hash, +8 ref-counted holder, +0x10 attachment node, +0x18 next.
// Attachment node: +8 i32 state (2 = closed / detached), +0xC i32 sub group id, +0x10 u32 parent hash.
// Inbound packets 0x2D (move sub, two hashes) and 0x45 (close sub, one hash) resolve through this same map
// (FUN_1400e2080 / FUN_1401a0840 / FUN_1401a09f0), so it is the engine's own answer to "where is group G
// mounted". Snapshotted for 250 ms per client.
constexpr std::uint64_t kIfaceSubBuckets = 0xE0, kIfaceSubBucketCount = 0xE8;
struct IfaceSubParent { int group; int comp; };
static bool iface_sub_parent(HANDLE h, std::uint64_t main_data, int gid, IfaceSubParent& out) {
    static std::mutex mu;
    struct Snap { unsigned long long ms; std::unordered_map<int, IfaceSubParent> map; };
    static std::unordered_map<std::uint64_t, Snap> snaps;   // main_data -> snapshot
    const unsigned long long now = GetTickCount64();
    std::lock_guard<std::mutex> lk(mu);
    auto& sn = snaps[main_data];
    if (sn.ms == 0 || now - sn.ms >= 250) {
        sn.ms = now; sn.map.clear();
        auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
        auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
        std::uint64_t owner = r64(main_data + 0x19900);
        if (owner > 0x10000) {
            std::uint64_t buckets = r64(owner + kIfaceSubBuckets);
            int nb = r32(owner + kIfaceSubBucketCount);
            if (buckets > 0x10000 && nb > 0 && nb <= 65536) {
                int total = 0;
                for (int i = 0; i < nb && total < 4096; ++i) {
                    std::uint64_t e = r64(buckets + (std::uint64_t)i * 8);
                    for (int guard = 0; e > 0x10000 && guard < 256; ++guard, ++total) {
                        std::uint32_t key = (std::uint32_t)r32(e);
                        std::uint64_t node = r64(e + 0x10);
                        if (node > 0x10000 && r32(node + 8) != 2) {
                            int sub = r32(node + 0xC);
                            std::uint32_t ph = (std::uint32_t)r32(node + 0x10);
                            if (ph != key) ph = key;
                            if (sub > 0 && sub < 70000) sn.map[sub] = { (int)(ph >> 16), (int)(ph & 0xFFFF) };
                        }
                        e = r64(e + 0x18);
                    }
                }
            }
        }
    }
    auto it = sn.map.find(gid);
    if (it == sn.map.end()) return false;
    out = it->second;
    return true;
}

// Automatic origin: follow the sub-interface table up to the game frame (1477, screen-absolute) and add the
// absolute position of each parent component along the way. Exact for every group the engine attached as a
// sub-interface, with no per-group knowledge; false only for groups that are not attached (or hidden slots).
static bool iface_auto_origin(HANDLE h, std::uint64_t main_data, int gid, int& ox, int& oy, int depth = 0) {
    if (gid == 1477) { ox = 0; oy = 0; return true; }
    if (depth > 6) return false;
    IfaceSubParent p;
    if (!iface_sub_parent(h, main_data, gid, p)) return false;
    if (p.group == gid) return false;
    int px = 0, py = 0;
    if (!iface_auto_origin(h, main_data, p.group, px, py, depth + 1)) return false;
    int cx = 0, cy = 0;
    if (!iface_comp_abs(h, main_data, p.group, p.comp, cx, cy)) return false;
    ox = px + cx; oy = py + cy;
    return true;
}

// Mount of a group as the engine sees it ("1477:501"), for the Interfaces tab; empty when not attached.
static std::string iface_mount_label(HANDLE h, std::uint64_t main_data, int gid) {
    IfaceSubParent p;
    if (!iface_sub_parent(h, main_data, gid, p)) return std::string();
    return std::to_string(p.group) + ":" + std::to_string(p.comp);
}

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
    if (vok && vv != 0) { out = vv; return true; }
    if (cok && cv != 0) { out = cv; return true; }
    if (vok) { out = vv; return true; }
    if (cok) { out = cv; return true; }
    return false;
}

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

static bool iface_has_sprite(HANDLE h, std::uint64_t main_data, int sprite_id) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
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
            if (r32(node + 0x1a8) == sprite_id) { found = true; return; }
            { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
              for (const auto& kid_ : kids_) { if (!(true && !found && !found)) break; walk(kid_.addr, depth + 1); } }
        };
        for (std::uint64_t w = a; w + 0x18 <= b && !found; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) walk(nd, 0);
        }
        return found;
    }
    return false;
}

static bool iface_panel_origin(HANDLE h, std::uint64_t main_data, std::uint32_t pid, int gid, int& ox, int& oy,
                               bool* exact = nullptr) {
    if (exact) *exact = false;
    // 1. Engine sub-interface table: the parent component's live rect IS the group's origin. No varcs, no
    //    per-group offsets, no size-matched frame search; the manual table below is only a fallback.
    {
        int ax = 0, ay = 0;
        if (iface_auto_origin(h, main_data, gid, ax, ay)) {
            ox = ax; oy = ay;
            { std::lock_guard<std::mutex> lk(g_ifaceOffMu);   // live calibration nudge from the Interfaces tab
              auto ov = g_ifaceOff.find(gid);
              if (ov != g_ifaceOff.end()) { ox += ov->second.first; oy += ov->second.second; } }
            if (exact) *exact = true;
            return true;
        }
    }
    // 2. Fallback: manual varc / mount table (groups the engine did not attach as a sub-interface).
    for (const auto& s : kPanelOrigins) {
        if (s.group != gid) continue;
        if (s.req_group && !iface_group_open(h, main_data, s.req_group)) continue;   // variant gate
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
        // House Controls (1665): classic interface (varbit 27169 = varp 3680 bit 21) -> -12;
        // modern tabbed (tab icon sprite 18788 present) -> +28.
        if (gid == 1665) {
            if ((read_varp(h, main_data, 3680) >> 21) & 1) oy -= 12;
            else if (iface_has_sprite(h, main_data, 18788)) oy += 28;
        }
        { std::lock_guard<std::mutex> lk(g_ifaceOffMu);   // live calibration nudge from the Interfaces tab
          auto ov = g_ifaceOff.find(gid);
          if (ov != g_ifaceOff.end()) { ox += ov->second.first; oy += ov->second.second; } }
        return true;
    }
    // Cache-driven fallback: HUD panel registry (enum 7716) params 3514-3517 pack content comps as
    // (group<<16)|sub and param 3503 the 1477 mount comp. Only runs when the table missed.
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

static bool iface_live_frame_origin(HANDLE h, std::uint64_t gs, std::uint64_t ge,
                                    int px, int py, int wLo, int wHi, int hLo, int hHi,
                                    int& outX, int& outY, int* outW = nullptr, int* outH = nullptr) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    long long bestD = -1, bestArea = 0;
    int anyCount = 0, anyX = 0, anyY = 0, anyW = 0, anyH = 0;
    std::function<void(std::uint64_t,int,int,int)> fwalk =
        [&](std::uint64_t node, int bx, int by, int depth) {
        if (depth > 14) return;
        int ax = bx + r32(node + 0x98), ay = by + r32(node + 0x9c);
        int w2 = r32(node + 0xa0), h2 = r32(node + 0xa4);
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
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true)) break; fwalk(kid_.addr, ax, ay, depth + 1); } }
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
        out += "{\"id\":" + std::to_string(gid) + ",\"n\":" + std::to_string(n);
        std::string mount = iface_mount_label(h, *root, gid);
        if (!mount.empty()) out += ",\"mount\":\"" + mount + "\"";
        int ax = 0, ay = 0;
        if (iface_auto_origin(h, *root, gid, ax, ay)) out += ",\"ox\":" + std::to_string(ax) + ",\"oy\":" + std::to_string(ay);
        out += "}";
    }
    out += "]}";
    return out;
}

// Compass-clue needle: group 996 comp 5, rotation int32 at node+0x180, raw 0..~2092 (bearing = raw / 5.8127); -1 when not open.
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
            if (r16(nd + 0x3a) == 5)            // component id 5 = the needle
                return r32(nd + 0x1b0);          // its rotation (+0x180 through 949-5)
        }
        return -1;   // group open but needle component not found
    }
    return -1;       // group 996 not open
}

// Compass-clue dig tile: varc 1323 = (plane<<28)|(x<<14)|y. Returns "x,y,plane", or "" when unset / out of range.
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

// Scan-orb ring tiles from the scene-graphic registry: op83 stores a 0x2c-byte record at MainData+0x198f0 (+0x90 + slot*0x2c) with fine coords (0x100 + tile*512) as floats.
// Base may be the struct or a pointer to it; every plausible tile is returned for the caller to validate.
static void ScanRingTilesFromMemory(HANDLE h, std::uint64_t root, std::vector<std::pair<int,int>>& out) {
    constexpr std::uint64_t kOffRegistry = 0x198f0, kRecBase = 0x90, kRecSize = 0x2c, kSlots = 8;
    // Fine coordinate = 0x100 + tile*512; must land on a tile centre.
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

// Hover slot: input_proc = *(root+0x198E8), slot = *(input_proc+0x13F8). action_obj: name string @+0x00, verb @+0x18, ref @+0x48 (loc: loc id with tile x/y inline at +0x4C/+0x50; npc/player: scene uid).
// +0x188 key encodings: all-FF sentinel (no graphic), item + flavour<<16 with a small flavour, or bit-62 obj-icon 0x4000000000000000 | flavour<<24 | item.
static bool iface_key_is_item(std::uint64_t key, int item) {
    if (key == ~0ull) return true;
    if ((key >> 62) == 1) return (int)(key & 0xFFFFFF) == item;
    if (key < 0x1000000ull && key >= (std::uint64_t)item) {
        std::uint64_t d = key - (std::uint64_t)item;
        return (d & 0xFFFF) == 0 && (d >> 16) < 64;
    }
    return false;
}

// Item behind a hovered cell (group, comp +0x2a, sub +0x2c). Grids split cells over components (shop:
// ops on 1265:20, object on 1265:24), so fall back to the nearest component with the same sub index. -1 if none.
static int iface_item_at(HANDLE h, std::uint64_t mainData, int group, int comp, int sub, int* dbg = nullptr) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::int16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, mainData, gs, ge);
    if (!gs) return -1;
    int found = -1, visited = 0, matched = 0, groups = 0;
    int nearItem = -1, nearDist = 1 << 30;   // best same-sub-index item on another component
    std::function<void(std::uint64_t, int)> walk = [&](std::uint64_t node, int depth) {
        if (found >= 0 || depth > 12 || visited++ > 6000) return;
        if (r16(node + 0x3c) == sub) {
            int c = r16(node + 0x3a);
            if (c == comp) ++matched;
            int item = r32(node + 0x1d8);
            if (item > 0 && item < 200000 && iface_key_is_item(r64(node + 0x1b0), item)) {
                if (c == comp) { found = item; return; }
                int d = c > comp ? c - comp : comp - c;
                if (d < nearDist) { nearDist = d; nearItem = item; }
            }
        }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true && found < 0 && found < 0)) break; walk(kid_.addr, depth + 1); } }
    };
    for (std::uint64_t g = gs; g + 0x10 <= ge && found < 0; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != group) continue;
        ++groups;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        for (std::uint64_t w = a; w + 0x18 <= b && found < 0; w += 0x18) {
            std::uint64_t nd = r64(w);
            if (nd > 0x10000) walk(nd, 0);
        }
        break;
    }
    if (dbg) { dbg[0] = groups; dbg[1] = visited; dbg[2] = matched; }
    return found >= 0 ? found : nearItem;
}

std::string HoverEntityJson(std::uint32_t pid) {
    const char* kNone = "{\"ok\":false}";
    constexpr std::uint64_t kOffInputProc = 0x19928, kHoverSlot = 0x13F8;   // 0x198E8 through 949-5 (+0x40 on 950-1)
    auto ps = snap_proc(pid);
    if (!ps) return kNone;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kNone;
    auto ip = rpm<std::uint64_t>(h, *root + kOffInputProc);
    if (!ip || *ip <= 0x10000) return kNone;
    auto ao = rpm<std::uint64_t>(h, *ip + kHoverSlot);
    if (!ao || *ao <= 0x10000) return kNone;

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
    std::string name = (raw.compare(0, 5, "<col=") == 0) ? strip_markup(raw) : std::string();
    std::string out = "{\"ok\":true,\"verb\":\"" + json_escape(verb) + "\"";

    int ref = rpm<std::int32_t>(h, *ao + 0x48).value_or(0);
    int tx  = rpm<std::int32_t>(h, *ao + 0x4C).value_or(0);
    int ty  = rpm<std::int32_t>(h, *ao + 0x50).value_or(0);
    char buf[96];
    // Item hover: +0x44 = item id (-1 otherwise), +0x4C = slot index, +0x50 = (component u16, interface u16).
    int itemId = rpm<std::int32_t>(h, *ao + 0x44).value_or(-1);
    int ifdbg[3] = { -1, -1, -1 };
    {
        int slot = rpm<std::int32_t>(h, *ao + 0x4C).value_or(-1);
        int comp = rpm<std::uint16_t>(h, *ao + 0x50).value_or(0);
        int ifid = rpm<std::uint16_t>(h, *ao + 0x52).value_or(0);
        if (itemId < 0 && ifid > 0 && ifid < 4096 && slot >= 0) {
            int wi = iface_item_at(h, *root, ifid, comp, slot, ifdbg);
            if (wi > 0) itemId = wi;
        }
        if (itemId >= 0) {
            std::snprintf(buf, sizeof(buf), ",\"kind\":\"item\",\"id\":%d,\"slot\":%d,\"iface\":%d,\"comp\":%d",
                          itemId, slot, ifid, comp);
            return out + buf + ",\"name\":\"" + json_escape(name) + "\"}";
        }
    }
    if (tx > 0 && tx < 16384 && ty > 0 && ty < 16384) {          // loc: id + tile inline
        std::snprintf(buf, sizeof(buf), ",\"kind\":\"loc\",\"id\":%d,\"x\":%d,\"y\":%d", ref, tx, ty);
        return out + buf + ",\"name\":\"" + json_escape(name) + "\"}";
    }
    // Interface-action hover: +0x44 == -1, no tile, +0x50 = (component u16, interface u16), second pair at +0x5C.
    {
        int comp  = rpm<std::uint16_t>(h, *ao + 0x50).value_or(0);
        int ifid  = rpm<std::uint16_t>(h, *ao + 0x52).value_or(0);
        if (ifid > 0 && ifid < 4096) {
            int comp2 = rpm<std::uint16_t>(h, *ao + 0x5C).value_or(0);
            int slot = rpm<std::int32_t>(h, *ao + 0x4C).value_or(-1);
            std::snprintf(buf, sizeof(buf), ",\"kind\":\"iface\",\"iface\":%d,\"comp\":%d,\"comp2\":%d,\"slot\":%d,\"dbg\":[%d,%d,%d]",
                          ifid, comp, comp2, slot, ifdbg[0], ifdbg[1], ifdbg[2]);
            return out + buf + "}";
        }
    }
    bool targetless = verb == "Walk here" || verb == "Cancel" || verb == "Continue" || name.empty();
    if (targetless) return out + "}";
    if (ref > 0) {
        constexpr std::uint64_t kContainer = 0x199D0, kActiveIdx = 0x70, kEntryArr = 0x58,
                                kEntryWv = 0x8, kVecBegin = 0x138, kVecEnd = 0x140,
                                kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType, kUid = rtx::scn::kUid,
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
                    int plane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);
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
    return out + ",\"name\":\"" + json_escape(name) + "\"}";
}

// Puzzle box board: interface 1931 comp 18, 25 cells each with a sprite u16 @+0x188 (consecutive ids,
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
        const std::uint64_t offs[3] = { 0x1d0, 0x1b8, 0x200 };
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
        if (r16s(n + 0x3a) == 18 && r16s(n + 0x3c) == -1) { grid = n; break; }
        gather(n, stack, sp, 4096);
    }
    if (!grid) return kEmpty;
    std::uint64_t cells[64]; int cn = 0; gather(grid, cells, cn, 64);
    int board[25]; for (int i = 0; i < 25; i++) board[i] = -1;
    int filled = 0;
    for (int i = 0; i < cn; i++) {
        int s = r16s(cells[i] + 0x3c);
        if (s < 0 || s > 24 || board[s] != -1) continue;
        board[s] = r16u(cells[i] + 0x1b0);
        filled++;
    }
    if (filled < 25) return kEmpty;
    std::string out = "[";
    for (int i = 0; i < 25; i++) { if (i) out += ","; out += std::to_string(board[i]); }
    out += "]";
    return out;
}

// Screen rects of the 25 puzzle-box cells (1931 comp 18) in sub order:
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
    std::uint64_t grid = 0; int gx = 0, gy = 0;
    std::function<void(std::uint64_t,int,int,int)> find =
        [&](std::uint64_t node, int bx, int by, int depth) {
        if (grid || depth > 14) return;
        int ax = bx + r32(node + 0x98), ay = by + r32(node + 0x9c);
        if (r16s(node + 0x3a) == 18 && r16s(node + 0x3c) == -1) { grid = node; gx = ax; gy = ay; return; }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true && !grid && !grid)) break; find(kid_.addr, ax, ay, depth + 1); } }
    };
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
    for (std::uint64_t w = ws + 8; w + 0x18 <= we + 8 && !grid; w += 0x18) { std::uint64_t nd = r64(w); if (nd > 0x10000) find(nd, ox, oy, 0); }
    if (!grid) return kEmpty;
    int cx[25], cy[25], cw[25] = {0}, ch_[25];
    const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };
    for (int k = 0; k < 3; ++k) {
        std::uint64_t cs = r64(grid + co[k]), ce = r64(grid + co[k] + 8), ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
        for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
            std::uint64_t cell = r64(c); if (cell <= 0x10000) continue;
            int s = r16s(cell + 0x3c); if (s < 0 || s > 24 || cw[s] > 0) continue;
            cx[s] = gx + r32(cell + 0x98); cy[s] = gy + r32(cell + 0x9c);
            cw[s] = r32(cell + 0xa0); ch_[s] = r32(cell + 0xa4);
        }
    }
    {
        static std::uint32_t l_pid = 0; static int l_ox = -99999, l_oy = -99999, l_gx = -99999, l_gy = -99999;
        if (l_pid != pid || ox != l_ox || oy != l_oy || gx != l_gx || gy != l_gy) {
            l_pid = pid; l_ox = ox; l_oy = oy; l_gx = gx; l_gy = gy;
            int c0x = cw[0] > 0 ? cx[0] : -1, c0y = cw[0] > 0 ? cy[0] : -1;
            rtx::log::Client(pid, "[pzr] abs=" + std::to_string(haveAbs ? 1 : 0) +
                " origin=" + std::to_string(ox) + "," + std::to_string(oy) +
                " grid=" + std::to_string(gx) + "," + std::to_string(gy) +
                " cell0=" + std::to_string(c0x) + "," + std::to_string(c0y));
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
    bool haveAbs = iface_panel_origin(h, *root, pid, group, ox, oy);
    if (!haveAbs && mountComp > 0) haveAbs = read_iface_mount_origin(h, *root, mountComp, ox, oy);
    std::vector<int> seen; std::string comps;
    std::function<void(std::uint64_t,int,int,int,bool)> walk =
        [&](std::uint64_t node, int bx, int by, int depth, bool hid) {
        if (depth > 16) return;
        int ax = bx + r32(node + 0x98), ay = by + r32(node + 0x9c);
        int comp = r16s(node + 0x3a);
        if (r16s(node + 0x3c) == -1 && std::find(want.begin(), want.end(), comp) != want.end()
            && std::find(seen.begin(), seen.end(), comp) == seen.end()) {
            seen.push_back(comp);
            comps += (comps.empty() ? "" : ",");
            comps += "\"" + std::to_string(comp) + "\":[" + std::to_string(ax) + "," + std::to_string(ay)
                   + "," + std::to_string(r32(node + 0xa0)) + "," + std::to_string(r32(node + 0xa4)) + "," + (hid ? "0" : "1") + "]";   // [x,y,w,h,visible]
        }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true)) break; walk(kid_.addr, ax, ay, depth + 1, hid || kid_.hidden); } }
    };
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
    { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots); for (const auto& rt : roots) walk(rt.addr, ox, oy, 0, rt.hidden); }
    std::string out = "{\"abs\":"; out += haveAbs ? "1" : "0"; out += ",\"comps\":{" + comps + "}}";
    return out;
}

std::string IfaceSpriteParentRectJson(std::uint32_t pid, int group, int sprite) {
    const char* kEmpty = "{\"ok\":0}";
    auto ps = snap_proc(pid); if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return kEmpty;
    std::uint64_t ap2 = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t a = r64(g + 8);
        if (a > 0x10000 && r32(a) == group) { ap2 = a; break; }
    }
    if (!ap2) return kEmpty;
    int ox = 0, oy = 0;
    iface_panel_origin(h, *root, pid, group, ox, oy);   // 1477 seeds at 0,0 -> already screen
    bool found = false; int fx = 0, fy = 0, fw = 0, fh = 0, fv = 0;
    std::function<void(std::uint64_t,int,int,int,int,int,int,int,int,bool)> walk =
        [&](std::uint64_t node, int bx, int by, int depth, int px, int py, int pw, int ph, int pv, bool hid) {
        if (found || depth > 16) return;
        int ax = bx + r32(node + 0x98), ay = by + r32(node + 0x9c);
        int w = r32(node + 0xa0), hh = r32(node + 0xa4);
        int vis = hid ? 0 : 1;
        int sprRaw = r32(node + 0x1a8);   // sprite id slot (graphic classes)
        if (sprRaw == sprite && pw > 0 && ph > 0) {
            fx = px; fy = py; fw = pw; fh = ph; fv = pv; found = true; return;
        }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true && !found && !found)) break; walk(kid_.addr, ax, ay, depth + 1, ax, ay, w, hh, vis, hid || kid_.hidden); } }
    };
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
    { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots);
      for (const auto& rt : roots) { if (found) break; walk(rt.addr, ox, oy, 0, 0, 0, 0, 0, 0, rt.hidden); } }
    if (!found) return kEmpty;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "{\"ok\":1,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"v\":%d}",
                  fx, fy, fw, fh, fv);
    return buf;
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
    int ox = 0, oy = 0;
    bool haveAbs = iface_panel_origin(h, *root, pid, groupId, ox, oy);
    int uiVw = 0, uiGw = 0;
    const float uiSc = iface_ui_scale(h, *root, pid, &uiVw, &uiGw);
    std::string out = "{\"ui\":" + std::to_string(uiSc) +
                      ",\"uiw\":" + std::to_string(uiVw) +
                      ",\"uig\":" + std::to_string(uiGw) +
                      ",\"widgets\":["; int count = 0; bool first = true;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != groupId) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots);
          for (const auto& rt : roots) { if (count >= kIfaceWalkCap) break; iface_walk(h, groupId, rt.addr, 0, out, count, first, ox, oy, haveAbs, rt.hidden); } }
        break;   // only the matching group
    }
    out += "]}";
    return out;
}

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
        std::function<void(std::uint64_t,int,int,int,bool)> walk =
            [&](std::uint64_t node, int bx, int by, int depth, bool hid) {
            if (depth > 14 || total > 120000 || matched >= 600) return;
            ++total;
            int x = r32(node+0x98), y = r32(node+0x9c), w = r32(node+0xa0), hh = r32(node+0xa4);
            int ax = bx + x, ay = by + y;
            if (adiff(w, tw) <= tol && adiff(hh, th) <= tol) {
                out += first ? "" : ","; first = false;
                out += "{\"g\":" + std::to_string(gid) + ",\"c\":" + std::to_string(r16(node+0x3a))
                     + ",\"s\":" + std::to_string(r16(node+0x3c)) + ",\"w\":" + std::to_string(w)
                     + ",\"h\":" + std::to_string(hh) + ",\"v\":" + (hid ? "0" : "1");
                if (haveAbs) out += ",\"ax\":" + std::to_string(ax) + ",\"ay\":" + std::to_string(ay);
                out += "}";
                ++matched;
            }
            { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
              for (const auto& kid_ : kids_) { if (!(true && matched < 600)) break; walk(kid_.addr, ax, ay, depth + 1, hid || kid_.hidden); } }
        };
        { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots);
          for (const auto& rt : roots) { if (matched >= 600) break; walk(rt.addr, ox, oy, 0, rt.hidden); } }
    }
    out += "]}";
    return out;
}

// Option-select dialogue (group 1188) state with absolute option rects; {} when closed.
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

    const int kGroup = 1188;   // option-select is the one the quest highlight uses
    int ox = 0, oy = 0; bool exactOrigin = false;
    bool haveAbs = iface_panel_origin(h, *root, pid, kGroup, ox, oy, &exactOrigin);

    std::function<void(std::uint64_t,int,int,int,bool)> walk;
    std::string opts; bool firstOpt = true; std::string header; int headerComp = -1; int optN = 0;
    int found = 0;
    auto isNumText = [](const std::string& t) {   // text made only of digits / '.' / ' ' (e.g. "1." or "38.")
        if (t.empty() || t.size() > 4) return false;
        for (char c : t) if (!(c >= '0' && c <= '9') && c != '.' && c != ' ') return false;
        return true;
    };
    walk = [&](std::uint64_t node, int bx, int by, int depth, bool hid) {
        if (depth > 16 || found > 600) return;
        int comp = r16(node + 0x3a);
        int tag  = r32(node + 0x1a8);   // 0x178 through 949-5; +0x30 like the neighbouring SSO member (unverified on 950-1)
        int x = r32(node + 0x98), y = r32(node + 0x9c), w = r32(node + 0xa0), hh = r32(node + 0xa4);
        int ax = bx + x, ay = by + y;
        ++found;
        std::string txt = iface_sso_text(h, node);
        if (comp == 3 && tag == 2 && !txt.empty() && header.empty()) { header = txt; headerComp = comp; }
        else if (!hid && !txt.empty() && w > 0 && !(comp == 3 && tag == 2) && !(isNumText(txt) && w < 48)) {
            opts += firstOpt ? "" : ","; firstOpt = false; ++optN;
            opts += "{\"n\":" + std::to_string(optN) + ",\"comp\":" + std::to_string(comp) +
                    ",\"tag\":" + std::to_string(tag) + ",\"text\":\"" + txt + "\"";
            if (haveAbs) opts += ",\"x\":" + std::to_string(ax) + ",\"y\":" + std::to_string(ay) +
                                 ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(hh);
            opts += "}";
        }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true)) break; walk(kid_.addr, ax, ay, depth + 1, hid || kid_.hidden); } }
    };
    bool open = false;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != kGroup) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        open = true;
        bool varcPositioned = false;
        if (!exactOrigin) for (const auto& s : kPanelOrigins) if (s.group == kGroup) { varcPositioned = s.var_x != 0; break; }
        if (haveAbs && varcPositioned) {
            std::uint64_t c0 = r64(a);
            int rw = (c0 > 0x10000) ? r32(c0 + 0xa0) : 0, rh = (c0 > 0x10000) ? r32(c0 + 0xa4) : 0;
            int fx = 0, fy = 0;
            if (rw > 0 && rh > 0 && iface_live_frame_origin(h, gs, ge, ox, oy, rw - 2, rw + 2, rh - 2, rh + 2, fx, fy)) { ox = fx; oy = fy; }
        }
        { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots); for (const auto& rt : roots) walk(rt.addr, ox, oy, 0, rt.hidden); }
        break;
    }
    if (!open) return "{}";
    std::string out = "{\"group\":" + std::to_string(kGroup);
    if (!header.empty()) out += ",\"header\":\"" + header + "\",\"headerComp\":" + std::to_string(headerComp);
    out += ",\"hasAbs\":" + std::string(haveAbs ? "true" : "false");
    out += ",\"options\":[" + opts + "]}";
    return out;
}

// Live text + absolute rect of requested comps in a group. NPC chat 1184: comp 4 = name, 10 = message,
// 11 = the visible continue button (comp 15 is the CS2 space-key target in a mispositioned variant).
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
    int ox = 0, oy = 0; bool exactOrigin = false;
    bool haveAbs = iface_panel_origin(h, *root, pid, group, ox, oy, &exactOrigin);

    std::string comps; bool firstC = true; bool open = false; int found = 0;
    std::function<void(std::uint64_t,int,int,int,bool)> walk;
    walk = [&](std::uint64_t node, int bx, int by, int depth, bool hid) {
        if (depth > 14 || found > 16000) return;   // the game frame alone holds ~5000 widgets
        ++found;
        int comp = r16(node + 0x3a);
        int x = r32(node + 0x98), y = r32(node + 0x9c), w = r32(node + 0xa0), hh = r32(node + 0xa4);
        const int vflags = hid ? 0 : 1;   // effective visibility: the node's own vector entry and every ancestor's are unhidden
        int ax = bx + x, ay = by + y;
        if (want.count(comp)) {
            std::string txt = iface_text(h, node);          // +0x90 display text
            if (txt.empty()) txt = iface_sso_text(h, node); // +0x180 SSO text
            std::uint64_t sprRaw = rpm<std::uint64_t>(h, node + 0x1b0).value_or(0);   // item / graphic key union
            int sprId = r32(node + 0x1a8);
            int sprv = (sprId > 0 && sprId < 0x100000 && txt.empty()) ? sprId : 0;
            int objv = ((sprRaw >> 62) == 1 && (sprRaw & 0xFFFFFF) < 200000) ? (int)(sprRaw & 0xFFFFFF) : 0;
            int itemv = r32(node + 0x1d8), amtv = r32(node + 0x1e0);   // item slot: id and stack size
            if (objv <= 0 && itemv > 0 && itemv < 200000) objv = itemv;
            if (objv <= 0) amtv = 0;
            int subv = r16(node + 0x3c);   // entry index within a templated grid
            comps += firstC ? "" : ","; firstC = false;
            comps += "{\"comp\":" + std::to_string(comp) + ",\"sub\":" + std::to_string(subv) +
                     ",\"text\":\"" + json_escape(txt) + "\"" +
                     ",\"vis\":" + std::to_string(vflags) + ",\"spr\":" + std::to_string(sprv) +
                     ",\"obj\":" + std::to_string(objv) + ",\"amt\":" + std::to_string(amtv) +
                     ",\"col\":" + std::to_string(r32(node + 0xa8) & 0xFFFFFF);
            if (haveAbs) comps += ",\"x\":" + std::to_string(ax) + ",\"y\":" + std::to_string(ay) +
                                  ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(hh);
            comps += "}";
        }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true)) break; walk(kid_.addr, ax, ay, depth + 1, hid || kid_.hidden); } }
    };
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != group) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        open = true;
        bool varcPositioned = false;
        if (!exactOrigin) for (const auto& s : kPanelOrigins) if (s.group == group) { varcPositioned = s.var_x != 0; break; }
        if (haveAbs && varcPositioned) {
            std::uint64_t c0 = r64(a);
            int rw = (c0 > 0x10000) ? r32(c0 + 0xa0) : 0, rh = (c0 > 0x10000) ? r32(c0 + 0xa4) : 0;
            int fx = 0, fy = 0;
            if (rw > 0 && rh > 0 && iface_live_frame_origin(h, gs, ge, ox, oy, rw - 2, rw + 2, rh - 2, rh + 2, fx, fy)) { ox = fx; oy = fy; }
        }
        { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots); for (const auto& rt : roots) walk(rt.addr, ox, oy, 0, rt.hidden); }
        break;
    }
    return "{\"group\":" + std::to_string(group) + ",\"open\":" + (open ? "true" : "false") +
           ",\"hasAbs\":" + (haveAbs ? "true" : "false") + ",\"exact\":" + (exactOrigin ? "true" : "false") +
           ",\"comps\":[" + comps + "]}";
}

// Live backpack slot rect from group 1473 (origin varcs 3040/3041, size 8990/8991): cells found by size,
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
    int ox = 0, oy = 0; bool exactOrigin = false;
    bool originOk = iface_panel_origin(h, *root, pid, kGroup, ox, oy, &exactOrigin);
    if (!originOk) return "{}";   // panel position varc not resolved yet
    int px = ox, py = oy;   // outer frame origin, before the chrome inset

    int vpx0 = 0, vpy0 = 0, vpx1 = 0, vpy1 = 0; bool haveVp = false;

    struct Cell { int x, y, w, h; std::uint64_t parent; std::uint64_t node; bool hid; };
    std::vector<Cell> cells;
    std::function<void(std::uint64_t,std::uint64_t,int,int,int,bool)> walk;
    walk = [&](std::uint64_t node, std::uint64_t parent, int bx, int by, int depth, bool hid) {
        if (depth > 12 || cells.size() > 4000) return;
        int x = r32(node + 0x98), y = r32(node + 0x9c), w = r32(node + 0xa0), hh = r32(node + 0xa4);
        int ax = bx + x, ay = by + y;
        if (w >= 28 && w <= 60 && hh >= 26 && hh <= 52) cells.push_back({ ax, ay, w, hh, parent, node, hid }); // a backpack cell (size-gated; tolerates UI scale)
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true)) break; walk(kid_.addr, node, ax, ay, depth + 1, hid || kid_.hidden); } }
    };
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000 || r32(ap2) != kGroup) continue;
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) break;
        std::uint64_t c0 = r64(a);
        int contentW = (c0 > 0x10000) ? r32(c0 + 0xa0) : 0;
        int contentH = (c0 > 0x10000) ? r32(c0 + 0xa4) : 0;
        int panelW = 0, panelH = 0;
        int frameX = 0, frameY = 0;
        if (!exactOrigin && contentW > 0 && contentH > 0 &&
            iface_live_frame_origin(h, gs, ge, px, py, contentW + 1, contentW + 119,
                                    contentH + 1, contentH + 299, frameX, frameY, &panelW, &panelH)) {
            ox = frameX; oy = frameY; px = frameX; py = frameY;
        }
        if (!exactOrigin && panelW == 0) for (std::uint64_t g2 = gs; g2 + 0x10 <= ge; g2 += 0x10) {
            std::uint64_t ap = r64(g2 + 8);
            if (ap <= 0x10000 || r32(ap) != 1477) continue;
            std::uint64_t ws2 = r64(ap + 0x20), a2 = ws2 + 8;
            if (ws2 > 0x10000 && a2 > 0x10000) {
                std::uint64_t w0 = r64(a2);
                if (w0 > 0x10000) {
                    int pw = r32(w0 + 0xa0), ph = r32(w0 + 0xa4);
                    if (contentW > 0 && contentH > 0 && pw > contentW && pw < contentW + 120 && ph > contentH && ph < contentH + 300) { panelW = pw; panelH = ph; }
                }
            }
            break;
        }
        int border, header;
        if (exactOrigin) {
            border = 0; header = 0;   // engine origin = content origin (the sub's parent component rect)
        } else if (panelW > 0 && panelH > 0) {
            border = (panelW - contentW) / 2;
            header = (panelH - contentH) - border;
        } else {
            // "Slim headers" varbit 19924 (varp 3814 bit 0): grid mounts at (4,44) slim / (4,64) full.
            border = 4; header = (read_varp(h, *root, 3814) & 1) ? 44 : 64;
        }
        ox += border; oy += header;
        if (contentW > 0 && contentH > 0)      { vpx0 = ox; vpy0 = oy; vpx1 = ox + contentW; vpy1 = oy + contentH; haveVp = true; }
        else if (panelW > 0 && panelH > 0) { vpx0 = px; vpy0 = py; vpx1 = px + panelW; vpy1 = py + panelH; haveVp = true; }
        if (haveVp && panelW > 0 && panelH > 0) {
            if (vpx0 < px) vpx0 = px;            if (vpy0 < py) vpy0 = py;
            if (vpx1 > px + panelW) vpx1 = px + panelW;  if (vpy1 > py + panelH) vpy1 = py + panelH;
        }
        { std::vector<IfaceChildRef> roots; iface_entry_refs(h, ws, we, roots); for (const auto& rt : roots) walk(rt.addr, c0, ox, oy, 0, rt.hidden); }   // c0 (group root) is the parent of each top-level widget
        break;
    }
    std::size_t rawCells = cells.size();
    auto visible = [&](const Cell& c) {
        if (c.hid) return false;               // hidden subtree (other tab / collapsed section)
        if (!haveVp) return true;
        int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        return cx >= vpx0 && cx <= vpx1 && cy >= vpy0 && cy <= vpy1;
    };
    std::unordered_map<std::uint64_t,int> votes;
    for (const auto& c : cells) if (visible(c)) votes[c.parent]++;
    std::uint64_t cont = 0; int best = 0;
    for (const auto& kv : votes) if (kv.second > best) { best = kv.second; cont = kv.first; }

    std::vector<Cell> slots;
    for (const auto& c : cells) if (c.parent == cont) {
        bool dup = false;
        for (const auto& u : slots) if (std::abs(u.x - c.x) < 18 && std::abs(u.y - c.y) < 16) { dup = true; break; }
        if (!dup) slots.push_back(c);
    }
    std::sort(slots.begin(), slots.end(), [](const Cell& a, const Cell& b){ return a.y != b.y ? a.y < b.y : a.x < b.x; });
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
    if (!visible(s)) return "{}";   // scrolled out of the viewport
    return "{\"x\":" + std::to_string(s.x) + ",\"y\":" + std::to_string(s.y) +
           ",\"w\":" + std::to_string(s.w) + ",\"h\":" + std::to_string(s.h) + ",\"n\":" + std::to_string((int)slots.size()) + "}";
}

// Chat lines from group 137 comp-86 widgets (raw markup at +0x180, newest first) plus the packet log
// (op-0x15 message_game from the companion ring):
std::string ChatJson(std::uint32_t pid) {
    std::string ifaceArr = "[]";
    do {
    auto ps = snap_proc(pid);
    if (!ps) break;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) break;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    auto r16 = [&](std::uint64_t a){ return (int)rpm<std::uint16_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) break;

    std::uint64_t top = 0;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 > 0x10000 && r32(ap2) == 137) {
            std::uint64_t ws = r64(ap2 + 0x20);
            top = ws > 0x10000 ? r64(ws + 8) : 0;
            break;
        }
    }
    if (!top) break;

    auto kids = [&](std::uint64_t node, std::vector<std::uint64_t>& dst) {
        const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };
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
    // Comp-86 lines: raw markup at +0x1b0, base colour u24 RGB at +0xa8 (+0x180/+0x80 through 949-5);
    std::string a = "["; bool first = true; int emitted = 0;
    std::vector<std::pair<std::uint64_t, int>> stk{ { top, 0 } };
    int guard = 0;
    while (!stk.empty() && guard++ < 20000 && emitted < 500) {
        auto cur = stk.back(); stk.pop_back();
        if (cur.first <= 0x10000 || cur.second > 12) continue;
        if (r16(cur.first + 0x3a) == 86) {
            std::string msg = iface_text_at(h, cur.first, 0x1b0, 480);
            if (!msg.empty()) {
                std::uint32_t basecol = (std::uint32_t)r32(cur.first + 0xa8) & 0xFFFFFF;   // 950-1: +0xa8 (was +0x80)
                std::string nm = iface_text_at(h, cur.first, 0xb8, 96);                      // sender name; 950-1: +0xb8 (was +0x90)
                a += first ? "" : ","; first = false;
                a += "{\"raw\":\"" + msg + "\",\"base\":" + std::to_string(basecol) +
                     ",\"name\":\"" + nm + "\"}";
                ++emitted;
            }
        }
        std::vector<std::uint64_t> cs; kids(cur.first, cs);
        for (auto c : cs) stk.push_back({ c, cur.second + 1 });
    }
    a += "]";
    ifaceArr = std::move(a);
    } while (false);

    std::string out = "{\"lines\":" + ifaceArr;
    {
        std::lock_guard<std::mutex> lk(s_chat_mu);
        auto it = s_chatAcc.find(pid);
        if (it != s_chatAcc.end()) {
            const ChatAcc& acc = it->second;
            out += ",\"phook\":"; out += acc.hook ? "true" : "false";
            out += ",\"pseen\":" + std::to_string(acc.seen);
            out += ",\"packets\":[";
            const std::size_t nlog = acc.log.size();
            const std::size_t take = nlog < 300 ? nlog : 300;
            for (std::size_t i = 0; i < take; ++i) {
                const ChatPkt& p = acc.log[nlog - 1 - i];      // newest first
                if (i) out += ",";
                out += "{\"seq\":" + std::to_string(p.seq) +
                       ",\"t\":" + std::to_string(p.wall) +
                       ",\"type\":" + std::to_string(p.type) +
                       ",\"name\":\"" + p.name + "\",\"chan\":\"" + p.chan +
                       "\",\"raw\":\"" + p.text + "\"}";
            }
            out += "]";
        }
    }
    out += "}";
    return out;
}

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
// Each slot component carries the buff STRUCT the game rendered it from (cc param 8106, set by
// client script 10819), so identity is exact. Component params live at [[comp+0x170]]: a vector of
// 40-byte records {key i32, pad, value i32 ...} (found on 950-1 by matching the record's 8106 value
// against the slot's icon: struct 48340 Bone Shield -> graphic 30099). The countdown the bar shows
// is client script 10886: seconds = 1 + (end - CLIENTCLOCK) / 50 where end = script 11073(struct),
// a switch over struct ids onto the varc that holds the end cycle; script 11077 gives the stack
// count var. Those two switches are lifted verbatim into BuffVars.h (tools/rtx_buffvars.py), so
// `secs`, `remainMs` and `count` below are the game's own numbers, not parsed from "2m" text.
// Struct 2794 = display name, 2802 = graphic, 4677 = item icon. Slots with no struct are inert
// (script 10823 returns early for them) and are not reported.
static bool comp_int_param(HANDLE h, std::uint64_t comp, int key, int& out) {
    auto vec = rpm<std::uint64_t>(h, comp + 0x170);
    if (!vec || *vec <= 0x10000) return false;
    auto b = rpm<std::uint64_t>(h, *vec), e = rpm<std::uint64_t>(h, *vec + 8);
    if (!b || !e || *b <= 0x10000 || *e < *b || *e - *b > 0x4000) return false;
    for (std::uint64_t rec = *b; rec + 40 <= *e; rec += 40) {
        if (rpm<std::int32_t>(h, rec).value_or(-1) != key) continue;
        auto v = rpm<std::int32_t>(h, rec + 8);
        if (!v) return false;
        out = *v; return true;
    }
    return false;
}
// Value of a var named by a BuffVars entry (1 varc, 2 varp, 3 varbit). false when unreadable.
static bool buff_var_value(HANDLE h, std::uint64_t root, const rtx::buffvars::Entry& e, int& out) {
    switch (e.kind) {
    case 1: return read_varc_found(h, root, e.var, out);
    case 2: return read_varp_found(h, root, e.var, out);
    case 3: {
        int wvp = -1, lsb = -1, msb = -1;
        if (!rtx::cache::GetVarbit(e.var, wvp, lsb, msb) || wvp < 0 || lsb < 0 || msb < lsb || msb >= 32) return false;
        int raw = 0;
        if (!read_varp_found(h, root, wvp, raw)) return false;
        unsigned mask = (msb - lsb + 1 >= 32) ? 0xFFFFFFFFu : ((1u << (msb - lsb + 1)) - 1);
        out = (int)(((unsigned)raw >> lsb) & mask); return true;
    }
    default: return false;
    }
}
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
    const long long cycles = (long long)rpm<std::uint32_t>(h, *root + kOffClientClock).value_or(0);   // CLIENTCLOCK, 50/s

    auto kids = [&](std::uint64_t node, std::vector<std::uint64_t>& dst) {
        const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };
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
    auto find_comp = [&](std::uint64_t top, int want) -> std::uint64_t {
        std::vector<std::pair<std::uint64_t, int>> stk{ { top, 0 } };
        int guard = 0;
        while (!stk.empty() && guard++ < 4000) {
            auto cur = stk.back(); stk.pop_back();
            if (cur.first <= 0x10000 || cur.second > 4) continue;
            std::vector<std::uint64_t> cs; kids(cur.first, cs);
            for (auto c : cs) if (r16(c + 0x3a) == want) return c;
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
            std::uint64_t slotArr = r64(slot + 0x200);   // child vector (+0x1c8 through 949-5)
            if (slotArr <= 0x10000) continue;
            std::uint64_t icon = r64(slotArr + 0x8), textw = r64(slotArr + 0x20);
            if (icon <= 0x10000) continue;
            if (r64(icon + 0x40) != slot) continue;      // parent backlink (+0x30 through 949-5; +0x48 = parent+0x20)
            int sprite = r16(icon + 0x1b0);
            int item   = r32(icon + 0x1d8);
            std::string timer;
            if (textw > 0x10000) {
                char tb[8] = {};
                if (rpm_bytes(h, textw + 0x1b0, tb, sizeof(tb) - 1)) {
                    for (int i = 0; i < (int)sizeof(tb) - 1 && tb[i]; ++i) {
                        unsigned char c = (unsigned char)tb[i];
                        if (c < 0x20 || c > 0x7e) break;
                        timer += (char)c;
                    }
                }
            }
            bool itemBased = (item > 0 && item < 200000);
            bool spriteOk  = (sprite > 0 && sprite < 0xFFFF);
            if (!itemBased && spriteOk && rtx::cache::GetBuffIconIsItem(sprite)) {
                item = sprite; sprite = 0; itemBased = true; spriteOk = false;
            }
            if (!itemBased && !spriteOk) continue;            // no real icon -> not a buff
            // Expired-buff filter: a torn-down icon's content count at icon+0x8 reads 0, a live one 1.
            if (r32(icon + 0x8) == 0) continue;
            int structId = -1;
            if (!comp_int_param(h, slot, 8106, structId) || structId <= 0) continue;   // inert slot: no buff bound
            // 949-5 also required icon+0x50 & 0x01010000 (visible state); that flag moved on 950-1 and is not re-derived.
            int id = itemBased ? item : sprite;
            unsigned dkey = itemBased ? (0x80000000u | (unsigned)item) : (unsigned)sprite;
            bool dup = false; for (unsigned k : seen) if (k == dkey) { dup = true; break; }
            if (dup) continue; seen.push_back(dkey);
            std::string name;
            { std::string sn; if (rtx::cache::StructStrParam(structId, 2794, sn) && !sn.empty()) name = sn; }
            if (name.empty()) name = debuff ? rtx::cache::GetDebuffName(id) : rtx::cache::GetBuffName(id);
            if (name.empty() && itemBased) name = rtx::cache::ItemName(item);
            int knd = 0;
            { int f = 0;
              if (rtx::cache::StructIntParam(structId, 8112, f) && f) knd |= 1;
              if (rtx::cache::StructIntParam(structId, 8110, f) && f) knd |= 2;
              if (rtx::cache::StructIntParam(structId, 8111, f) && f) knd |= 4;
              if (rtx::cache::StructIntParam(structId, 8113, f) && f) knd |= 8;
              if (!knd) knd = rtx::cache::GetBuffKind(id); }
            const char* kindStr = (knd & 1) ? "timer" : (knd & 4) ? "pct" : (knd & 2) ? "count" : "";
            // Exact countdown from the game's own end-cycle var (BuffVars.h); text parse only as a fallback.
            long long endCycle = -1, remainMs = -1; bool exact = false;
            if (const auto* te = rtx::buffvars::FindTimer(structId)) {
                int endv = 0;
                if (buff_var_value(h, *root, *te, endv)) {
                    endCycle = endv; const long long rem = endCycle - cycles;
                    remainMs = rem > 0 ? rem * 20 : 0; exact = true;
                }
            }
            int sc = exact ? (int)((endCycle - cycles) > 0 ? 1 + (endCycle - cycles) / 50 : 0)
                           : ((kindStr[0] == '\0' || (knd & 1)) ? parse_buff_secs(timer) : 0);
            long long count = -1; bool haveCount = false;
            if (const auto* ce = rtx::buffvars::FindCount(structId)) { int cv = 0; if (buff_var_value(h, *root, *ce, cv)) { count = cv; haveCount = true; } }
            out += first ? "" : ","; first = false;
            out += "{\"sprite\":" + std::to_string(itemBased ? 0 : sprite) +
                   ",\"item\":"   + std::to_string(itemBased ? item : 0) +
                   ",\"name\":\"" + json_escape(name) + "\"" +
                   ",\"timer\":\"" + json_escape(timer) + "\"" +
                   ",\"kind\":\"" + std::string(kindStr) + "\"" +
                   ",\"secs\":"   + std::to_string(sc) +
                   ",\"struct\":" + std::to_string(structId) +
                   ",\"exact\":"  + std::string(exact ? "true" : "false") +
                   (exact ? ",\"endCycle\":" + std::to_string(endCycle) + ",\"remainMs\":" + std::to_string(remainMs) : std::string()) +
                   (haveCount ? ",\"count\":" + std::to_string(count) : std::string()) + "}";
        }
        return out;
    };

    std::string out = "{\"cycles\":" + std::to_string(cycles) + ",\"buffs\":[";
    out += bar_json(284, 18, false);
    out += "],\"debuffs\":[";
    out += bar_json(291, 1, true);
    out += "]}";
    return out;
}

// Walk the 5 action bars (1430 main + 1670-1673) collecting (ability/item id -> label).
// Label at *(node+0x90) (with <col> tags), id at +0x188 = cooldown-registry key space (Surge = 14233).
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
                int id = r16(node + 0x1b0);
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
                const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };
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

// One bound action-bar ability. item: item slots store it at +0x1a0, abilities leave 0.
struct AbarSlot { int id; int item; std::string name; std::string key; int mod; int en;
                  std::string cd; };

static bool is_cd_timer(const std::string& t) {
    if (t.empty()) return false;
    bool digit = false;
    for (char c : t) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c != ':' && c != '.' && c != 'm' && c != 's' && c != ' ') return false;
    }
    return digit;
}

// Widget's inline label at +0x180 (legacy I_itemids3: short text in-place, not a pointer).
static std::string iface_inline(HANDLE h, std::uint64_t node) {
    char buf[16] = {};
    if (!rpm_bytes(h, node + 0x1b0, buf, sizeof(buf) - 1)) return {};
    std::string s;
    for (int i = 0; i < 15 && buf[i]; ++i) { unsigned char c = (unsigned char)buf[i]; if (c < 0x20 || c >= 0x7f) return {}; s += (char)c; }
    return s;
}

// Slot box's keybind label and per-ability cooldown text. Each slot is a 13-component block; typically box+4 = name, box+5 = GCD swirl, box+11 = keybind, box+12 = cooldown (+0x180 inline).
// mod: 0 none, 1 shift, 2 ctrl, 3 alt.
static std::string box_keybind(HANDLE h, std::uint64_t box, int& mod, std::string& cd) {
    mod = 0; cd.clear();
    std::string keyb;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto alnum = [](char c){ return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    int boxComp = (int)rpm<std::uint16_t>(h, box + 0x3a).value_or(0xFFFF);
    if (boxComp == 0xFFFF) return keyb;

    struct Kid { int rel; std::string text; int x, y; };
    std::vector<Kid> kids;
    const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };
    for (int kk = 0; kk < 3; ++kk) {
        std::uint64_t cs = r64(box + co[kk]), ce = r64(box + co[kk] + 8);
        std::uint64_t ca = cs + 8, cb = ce + 8;
        if (!cs || !ce || ca <= 0x10000 || cb <= ca || (cb - ca) > 0x100000) continue;
        for (std::uint64_t c = ca; c + 0x18 <= cb; c += 0x18) {
            std::uint64_t ch = r64(c);
            if (ch <= 0x10000) continue;
            std::int64_t d = (std::int64_t)c - (std::int64_t)ch; if (d < 0) d = -d;
            if (d <= 0x3000) continue;
            if (rpm<std::uint16_t>(h, ch + 0x3c).value_or(0) != 0xFFFF) continue;   // direct component
            int rel = (int)rpm<std::uint16_t>(h, ch + 0x3a).value_or(0) - boxComp;
            std::string t = iface_sso_text(h, ch);
            std::string clean; bool intag = false;      // strip <col=..> markup, as the name path does
            for (char c2 : t) {
                if (c2 == '<') intag = true;
                else if (c2 == '>') intag = false;
                else if (!intag) clean += c2;
            }
            while (!clean.empty() && (clean.front() == ' ' || clean.front() == '\t')) clean.erase(clean.begin());
            while (!clean.empty() && (clean.back()  == ' ' || clean.back()  == '\t')) clean.pop_back();
            if (!clean.empty())
                kids.push_back({ rel, clean,
                                 rpm<std::int32_t>(h, ch + 0x98).value_or(0),
                                 rpm<std::int32_t>(h, ch + 0x9c).value_or(0) });
        }
    }
    std::sort(kids.begin(), kids.end(), [](const Kid& a, const Kid& b){ return a.rel < b.rel; });

    auto as_key = [&](const std::string& t, int& outMod) -> std::string {
        outMod = 0;
        if (t.size() == 1 && alnum(t[0])) return t;
        if (t.size() == 3 && t[1] == '-' && alnum(t.back())) {
            char m = (char)(t[0] | 0x20);
            int mm = (m == 's') ? 1 : (m == 'c') ? 2 : (m == 'a') ? 3 : 0;
            if (mm) { outMod = mm; return std::string(1, t.back()); }
        }
        if (t.size() >= 2 && t.size() <= 3 && (t[0] == 'F' || t[0] == 'f')) {
            bool digits = true;
            for (std::size_t i = 1; i < t.size(); ++i) if (t[i] < '0' || t[i] > '9') digits = false;
            if (digits) return t;
        }
        return {};
    };

    const int bw = rpm<std::int32_t>(h, box + 0xa0).value_or(0);
    const int bh = rpm<std::int32_t>(h, box + 0xa4).value_or(0);
    auto topLeft = [&](const Kid& k) {
        int lx = bw > 8 ? bw / 3 : 10, ly = bh > 8 ? bh / 3 : 10;
        if (lx > 12) lx = 12;
        if (ly > 12) ly = 12;
        return k.x >= 0 && k.y >= 0 && k.x < lx && k.y < ly;
    };
    bool haveKey = false; int keyRel = 0;
    {
        const Kid* best = nullptr; int bestMod = 0; std::string bestKey;
        for (const auto& kv : kids) {
            int mm = 0;
            std::string k = as_key(kv.text, mm);
            if (k.empty() || !topLeft(kv)) continue;
            if (!best || (kv.x + kv.y) < (best->x + best->y)) { best = &kv; bestMod = mm; bestKey = k; }
        }
        if (best) { keyb = bestKey; mod = bestMod; keyRel = best->rel; haveKey = true; }
    }
    for (const auto& kv : kids)
        if (haveKey && kv.rel == keyRel + 1 && is_cd_timer(kv.text)) { cd = kv.text; break; }
    if (cd.empty())
        for (const auto& kv : kids) {
            if (haveKey && kv.rel == keyRel) continue;
            if (!is_cd_timer(kv.text)) continue;
            if (haveKey && topLeft(kv)) continue;
            cd = kv.text; break;
        }
    return keyb;
}


// Depth-first collect bound abilities under `node`: name at *(node+0x90), id u16 at +0x1b0.
static void abar_collect(HANDLE h, std::uint64_t node, std::uint64_t parent, std::uint64_t gp,
                         int depth, std::vector<AbarSlot>& dst) {
    if (depth > 14 || dst.size() >= 64) return;
    int id = (int)rpm<std::uint16_t>(h, node + 0x1b0).value_or(0);
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
                    int item = rpm<std::int32_t>(h, node + 0x1d8).value_or(0); if (item < 0 || item > 200000) item = 0;   // item slots store the item here
                    // I_AbilityEnabled (+0x80): 255 = castable, lower (e.g. 51) = greyed. Raw byte exported.
                    int en = (int)rpm<std::uint8_t>(h, node + 0xa8).value_or(0);
                    dst.push_back({ id, item, clean, key, mod, en, cd });
                }
            }
        }
    }
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    const std::uint64_t co[3] = { 0x1d0, 0x1b8, 0x200 };
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

// Bound abilities on every action bar: main (group 1430) + secondaries (1670..1673; the UI gates them on varbits 29138..29141, >0 = visible, value = preset). Read from the interface tree.
// clock = engine wall-clock ms; cycles = CLIENTCLOCK units (50/s), the unit the cooldown varcs are stamped in.
std::string ActionBarJson(std::uint32_t pid) {
    const char* kEmpty = "{\"clock\":0,\"cycles\":0,\"bars\":[]}";
    auto ps = snap_proc(pid);
    if (!ps) return kEmpty;
    HANDLE h = ps.h;
    auto rootv = rpm<std::uint64_t>(h, ps.mgva);
    if (!rootv || *rootv <= 0x10000) return kEmpty;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, *rootv, gs, ge);
    if (!gs) return kEmpty;
    // Engine wall-clock ms at module+0xED2FF8 (949 RVA).
    long long clock = (long long)(ps.mod_base
        ? rpm<std::uint64_t>(h, ps.mod_base + 0xED2FF8).value_or(0) : 0);
    long long cycles = (long long)rpm<std::uint32_t>(h, *rootv + kOffClientClock).value_or(0);
    const int bars[5] = { 1430, 1670, 1671, 1672, 1673 };
    std::string out = "{\"clock\":" + std::to_string(clock) +
                      ",\"cycles\":" + std::to_string(cycles) + ",\"bars\":["; bool firstBar = true;
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

// Cooldown hashmap at root+0x19FB8 on 950-1 (0x19F78 before): buckets ptr @mgr+0x38178, cap @+0x38180;
// node {key i32@0, expiry i64@+8 in engine-clock ms (base+0xED2FF8, 949 RVA, stale on 950-1), flag u8@+0x10, next@+0x18}.
std::string AbilityCooldownsJson(std::uint32_t pid) {
    const char* kEmpty = "{\"cooldowns\":[]}";
    auto ps = snap_proc(pid);
    if (!ps || !ps.mod_base)
        return kEmpty;
    HANDLE h = ps.h;
    auto rootv = rpm<std::uint64_t>(h, ps.mgva);
    if (!rootv || *rootv <= 0x10000) return kEmpty;
    std::uint64_t mgr     = *rootv + 0x19fb8;   // 0x19f78 through 949-5 (+0x40 on 950-1)
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

static void fill_view_metrics(HANDLE h, std::uint64_t rootv, std::uint32_t pid, OverlayFrame& out) {
    // Gameview rect from view-1000 varcs (x 3005, y 3006, w 3001, h 3002; physical pixels), cached ~2x/sec.
    {
        static std::uint32_t s_pid = 0; static unsigned long long s_ms = 0;
        static int s_x = 0, s_y = 0, s_w = 0, s_h = 0; static float s_ui = 0.0f;
        static int s_lcw = 0, s_lch = 0;
        unsigned long long nowms = GetTickCount64();
        if (s_pid != pid || nowms - s_ms > 500) {
            int gx = 0, gy = 0, gw = 0, gh = 0, lw = 0, lh = 0;
            const bool tree = (rootv && read_gameview_rect(h, rootv, gx, gy, gw, gh, &lw, &lh));
            s_lcw = lw; s_lch = lh;
            int vx = 0, vy = 0, vw = 0, vh = 0;
            if (rootv) {
                vx = read_varc(h, rootv, 3005); vy = read_varc(h, rootv, 3006);
                vw = read_varc(h, rootv, 3001); vh = read_varc(h, rootv, 3002);
            }
            if (vw > 0 && vh > 0)  { s_x = vx; s_y = vy; s_w = vw; s_h = vh; }
            else if (tree)         { s_x = gx; s_y = gy; s_w = gw; s_h = gh; }
            else                   { s_w = 0; s_h = 0; }
            s_ui = 0.0f;
            if (tree && gw > 0 && vw > 0) {
                const float r = (float)vw / (float)gw;
                if (r > 0.2f && r < 5.0f) s_ui = r;
            }
            {
                static int l_vx = -99999, l_vy = -99999, l_vw = -99999, l_vh = -99999,
                           l_gx = -99999, l_gw = -99999, l_gh = -99999;
                static std::uint32_t l_pid = 0;
                if (l_pid != pid || vx != l_vx || vy != l_vy || vw != l_vw || vh != l_vh ||
                    gx != l_gx || gw != l_gw || gh != l_gh) {
                    l_pid = pid; l_vx = vx; l_vy = vy; l_vw = vw; l_vh = vh;
                    l_gx = gx; l_gw = gw; l_gh = gh;
                    char gb[224];
                    std::snprintf(gb, sizeof(gb),
                        "[gv] varc=%d,%d %dx%d tree=%d,%d %dx%d root=%dx%d ui=%.3f (varc wins when set)",
                        vx, vy, vw, vh, gx, gy, gw, gh, lw, lh, (double)s_ui);
                    rtx::log::Client(pid, gb);
                }
            }
            s_pid = pid; s_ms = nowms;
        }
        out.gv_x = s_x; out.gv_y = s_y; out.gv_w = s_w; out.gv_h = s_h;
        out.lc_w = s_lcw; out.lc_h = s_lch;
        out.ui_scale = s_ui;
    }

}

bool ReadViewMetrics(std::uint32_t pid, OverlayFrame& out) {
    auto ps = snap_proc(pid); if (!ps) return false;
    auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
    std::uint64_t rootv = (root && *root > 0x10000) ? *root : 0;
    fill_view_metrics(ps.h, rootv, pid, out);
    return out.gv_w > 0 || out.lc_w > 0;
}

bool BuildOverlayFrame(std::uint32_t pid, bool want_players, bool want_npcs,
                       bool want_objects, bool want_specials, int grid_radius, bool interactable,
                       bool want_true_tile,
                       const std::vector<std::string>& highlight_names,
                       const std::vector<int>& outline_uids,
                       const std::vector<OutlineLocReq>& outline_locs,
                       const std::vector<GuideSite>& guide_sites,
                       OverlayFrame& out) {
    constexpr std::uint64_t kContainer = rtx::scn::kContainer, kActiveIdx = rtx::scn::kActiveIdx,
                            kEntryArr = rtx::scn::kEntryArr, kEntryWv = rtx::scn::kEntryWv,
                            kVecBegin = rtx::scn::kVecBegin, kVecEnd = rtx::scn::kVecEnd,
                            kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType,
                            kName = rtx::scn::kName, kUid = rtx::scn::kUid,
                            kConfig = rtx::scn::kConfig, kPosX = rtx::scn::kPosX,
                            kPosZ = rtx::scn::kPosZ, kPosY = rtx::scn::kPosY,
                            kPlayerData = rtx::scn::kPlayerData, kLocalUid = rtx::scn::kLocalUid;

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

    CamProbes mprobes;
    auto worker = scene_worker(h, pid, *wv, &mprobes);
    if (!worker) return false;
    std::uint64_t rootv = (root && *root > 0x10000) ? *root : 0;
    if (!read_view_matrix(h, pid, rootv, *wv, mprobes, out.matrix)) return false;

    fill_view_metrics(h, rootv, pid, out);

    auto vb = deref(worker, kVecBegin);
    auto ve = deref(worker, kVecEnd);
    bool have_player = false;
    std::unordered_set<int> regions;
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
        if (t.size() > 1 && t[0] == '#') wid = std::atoi(t.c_str() + 1);   // "#<id>" = live NPC model id
        for (auto& ch : t) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        if (!t.empty()) hlow.push_back({ t, lbl, wtx, wty, wid, wall, wrad });
    }
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
                int gfx = rpm<std::int32_t>(h, *sec + rtx::scn::kT4Gfx).value_or(-1);
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
            int ttx = tx, tty = ty;                                    // server tile (movement route)
            const bool onRoute = actor_true_tile(h, *sec, ttx, tty);
            if (type == 2 && uid == local_uid) {
                have_player = true;
                out.player_tx = tx; out.player_ty = ty; out.player_z = fz.value_or(0);
                out.player_fx = *fx; out.player_fy = *fy;   // smooth sub-tile position for the ground indicator
                out.plane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);  // live plane
                out.player_ttx = ttx; out.player_tty = tty; out.player_route = onRoute;
            }

            char nm[40] = {0};
            rpm_bytes(h, *sec + kName, nm, sizeof(nm) - 1);
            std::string name;
            for (int j = 0; j < (int)sizeof(nm) && nm[j]; ++j) {
                unsigned char c = (unsigned char)nm[j];
                if (c >= 0x20 && c <= 0x7e) name.push_back((char)c);
                else if (c == 0xA0) name.push_back(' ');   // CP-1252 NBSP -> space (matches needles typed with a plain space)
            }

            // Actor model world AABB: min @ ent+0x40, max @ ent+0x50, floats east/up/north.
            bool  have_box = false;
            float bmnE = 0, bmnU = 0, bmnN = 0, bmxE = 0, bmxU = 0, bmxN = 0;
            float cE = *fx, cN = *fy, cU = fz.value_or(0);   // fallback: feet/tile centre
            if (type == 1 || type == 2) {   // players too: nameplate sits above the head
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

            if (type == 1 && !hlow.empty()) {
                int hcfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                bool fromMem = !name.empty();
                bool morphHidden = false;
                rtx::cache::NpcMeta hmeta = resolve_npc(h, root_raw, hcfg, &morphHidden);
                const std::string hname = !hmeta.name.empty() ? hmeta.name : (morphHidden ? std::string() : name);
                int hresolvedId = (hmeta.id >= 0) ? hmeta.id : hcfg;   // live model id (== SceneJson's reported id)
                std::string lower = hname;
                for (auto& ch : lower) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                for (std::size_t ni = 0; ni < hlow.size(); ++ni) {
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
                }
            }

            if (type == 1 && have_box && !outline_uids.empty()) {
                int uid = rpm<std::int32_t>(h, *sec + kUid).value_or(0);
                if (std::find(outline_uids.begin(), outline_uids.end(), uid) != outline_uids.end()) {
                    OverlayPoint op;
                    op.wx = cE; op.wy = cN; op.wz = cU; op.kind = 1;
                    op.uid = uid;                 // the launcher's anti-flicker hold keys on it
                    op.has_box3d = true;
                    op.bmin[0] = bmnE; op.bmin[1] = bmnN; op.bmin[2] = bmnU;
                    op.bmax[0] = bmxE; op.bmax[1] = bmxN; op.bmax[2] = bmxU;
                    out.highlights.push_back(op);
                }
            }

            bool want = (type == 2) ? want_players : want_npcs;
            if (!want) continue;
            if (interactable && type == 1) {
                int cfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                if (rtx::cache::GetNpc(cfg).actions.empty()) continue;
            }
            OverlayPoint p;
            p.wx = *fx; p.wy = *fy; p.wz = fz.value_or(0);
            p.kind = (type == 2) ? 2 : 1;
            std::string label = name;
            if (type == 1) {
                int cfg = rpm<std::int32_t>(h, *sec + kConfig).value_or(-1);
                bool mh = false;
                auto m = resolve_npc(h, root_raw, cfg, &mh);
                label = !m.name.empty() ? m.name : (mh ? std::string() : name);
                if (label.empty()) continue;   // hidden morph variant
            }
            p.label = label;
            p.uid = uid;
            p.is_self = (type == 2 && uid == local_uid);
            p.has_true = onRoute; p.true_x = ttx; p.true_y = tty;
            if (have_box) {
                p.head_z = bmxU;
                if (type == 1) {   // 3D box outline is NPC-only
                    p.has_box3d = true;
                    p.bmin[0] = bmnE; p.bmin[1] = bmnN; p.bmin[2] = bmnU;   // east, north, up
                    p.bmax[0] = bmxE; p.bmax[1] = bmxN; p.bmax[2] = bmxU;
                }
            }
            out.points.push_back(p);
        }
    }

    if (!hcands.empty()) {
        std::vector<int> best(hlow.size(), -1);
        std::vector<long long> bestScore(hlow.size(), 0);
        for (std::size_t i = 0; i < hcands.size(); ++i) {
            const HiCand& c = hcands[i];
            const HiNeedle& nd = hlow[c.ndl];
            if (nd.all) {
                if (!c.mem) continue;
                if (nd.tx > 0 && nd.rad > 0 &&
                    (std::abs(c.tx - nd.tx) > nd.rad || std::abs(c.ty - nd.ty) > nd.rad)) continue;
                out.highlights.push_back(c.hp);
                continue;
            }
            int anchorX = (nd.tx > 0) ? nd.tx : out.player_tx;
            int anchorY = (nd.ty > 0) ? nd.ty : out.player_ty;
            long long dx = (long long)c.tx - anchorX, dy = (long long)c.ty - anchorY;
            long long score = (c.mem ? 0LL : (1LL << 50)) + (c.inter ? 0LL : (1LL << 40)) + dx * dx + dy * dy;
            if (best[c.ndl] < 0 || score < bestScore[c.ndl]) { best[c.ndl] = (int)i; bestScore[c.ndl] = score; }
        }
        for (int bi : best) if (bi >= 0) out.highlights.push_back(hcands[bi].hp);
    }

    // Heights are absolute: live fine-z = 32 * cache surface height. The game's own terrain grid is
    // preferred when readable (exact, and the only source inside instances); the cache is the fallback.
    constexpr std::int16_t kNoH = -32768;
    constexpr float kHScale = 32.0f;
    out.pid = pid;
    const bool liveTerrain = have_player && LiveTerrainSnapshot(pid, out.player_tx, out.player_ty, out.plane);
    // SW corner of tile (tx, ty) as the game stands on it: effective plane (bridges) + standing offset.
    auto liveZ = [&](int tx, int ty, int plane, float& z) -> bool {
        if (!liveTerrain) return false;
        const int ep = LiveTileEffPlane(pid, tx, ty, plane);
        const std::int32_t v = LiveCornerHeight(pid, tx, ty, ep);
        if (v == INT32_MIN) return false;
        z = (float)(v + LiveTileLift(pid, tx, ty, ep)); return true;
    };
    // Raw corner on an explicit plane plus a given tile's offset (multi-tile boxes).
    auto liveRawZ = [&](int cx, int cy, int ep, int liftTx, int liftTy, float& z) -> bool {
        if (!liveTerrain) return false;
        const std::int32_t v = LiveCornerHeight(pid, cx, cy, ep);
        if (v == INT32_MIN) return false;
        z = (float)(v + LiveTileLift(pid, liftTx, liftTy, ep)); return true;
    };
    out.anchor_h = have_player
        ? rtx::cache::TileHeight(out.player_tx, out.player_ty, out.plane) : kNoH;

    auto cornerZ = [&](int tx, int ty) -> float {
        float lz; if (liveZ(tx, ty, out.plane, lz)) return lz;
        std::int16_t hh = rtx::cache::TileHeight(tx, ty, out.plane);
        return (hh == kNoH) ? out.player_z : kHScale * (float)hh;
    };
    auto fillBox = [&](OverlayPoint& op, int swx, int swy, int W, int H,
                       int forPlane = -1, bool atTop = false) {
        if (W < 1) W = 1;
        if (H < 1) H = 1;
        if (forPlane < 0) forPlane = out.plane;
        const int ep = liveTerrain ? LiveTileEffPlane(pid, swx + W / 2, swy + H / 2, forPlane)
                                   : rtx::cache::TileEffPlane(swx + W / 2, swy + H / 2, forPlane);
        const int cx[4] = { swx, swx + W, swx + W, swx };
        const int cy[4] = { swy, swy, swy + H, swy + H };
        float base = out.player_z, top = out.player_z; bool haveBase = false;
        for (int i = 0; i < 4; ++i) {
            float z;
            if (!liveRawZ(cx[i], cy[i], ep, swx + W / 2, swy + H / 2, z)) {
                std::int16_t hh = rtx::cache::TileHeightAtPlane(cx[i], cy[i], ep);
                if (hh == kNoH) continue;
                z = kHScale * (float)hh;
            }
            if (!haveBase) { base = top = z; haveBase = true; }
            else { if (z < base) base = z; if (z > top) top = z; }
        }
        const float lev = atTop ? top : base;
        for (int i = 0; i < 4; ++i) {
            op.box[i * 3 + 0] = cx[i] * 512.f;
            op.box[i * 3 + 1] = cy[i] * 512.f;
            op.box[i * 3 + 2] = lev;
        }
        op.has_box = true;
        int m = (W > H ? W : H);
        if (m > 4) m = 4;
        op.box_h = 280.f + 120.f * (float)(m - 1) + (atTop ? 0.f : (top - base));
    };

    if (want_true_tile && have_player) {
        // True-tile outlines (kind 5): the local player's server tile always, and the server tile of
        // every other marked actor whose movement route is live. Drawn flat on the ground.
        auto trueTilePoint = [&](int gx, int gy, int src, int uid, bool self) {
            OverlayPoint op;
            op.kind = 5; op.src = src; op.uid = uid; op.is_self = self;
            fillBox(op, gx, gy, 1, 1);
            op.box_h = 0.f;
            op.wx = ((float)gx + 0.5f) * 512.f; op.wy = ((float)gy + 0.5f) * 512.f;
            op.wz = op.box[2]; op.head_z = op.wz;
            return op;
        };
        std::vector<OverlayPoint> extra;
        extra.push_back(trueTilePoint(out.player_ttx, out.player_tty, 2, local_uid, true));
        for (const auto& p : out.points)
            if ((p.kind == 1 || p.kind == 2) && p.has_true && !p.is_self)
                extra.push_back(trueTilePoint(p.true_x, p.true_y, p.kind, p.uid, false));
        out.points.insert(out.points.end(), extra.begin(), extra.end());
    }

    if (have_player && !guide_sites.empty()) {
        std::uint64_t groot = (root && *root > 0x10000) ? *root : 0;
        std::vector<RuntimeObj> gobjs;                    // live placements: true model AABBs
        ReadRuntimeObjects(pid, gobjs);
        auto fillTileOnGround = [&](OverlayPoint& op, int gx, int gy, int plane) {
            const int cx[4] = { gx, gx + 1, gx + 1, gx };
            const int cy[4] = { gy, gy, gy + 1, gy + 1 };
            std::int16_t ch[4];
            rtx::cache::TileCornerHeights(gx, gy, plane, ch);
            std::int32_t lch[4]; const bool liveOk = liveTerrain && LiveCornerHeights(pid, gx, gy, plane, lch);
            float fallback = out.player_z;
            bool haveFallback = false;
            if (!liveOk && (ch[0] == kNoH || ch[1] == kNoH || ch[2] == kNoH || ch[3] == kNoH)) {
                int bestD2 = 4 * 4 + 1;                            // within 4 tiles or not at all
                for (const auto& r : gobjs) {
                    if (r.config_id <= 0 || r.plane != plane) continue;
                    if (!(r.bmax[0] > r.bmin[0])) continue;        // need a real AABB
                    int dx = r.x - gx, dy = r.y - gy;
                    int d2 = dx * dx + dy * dy;
                    if (d2 >= bestD2) continue;
                    bestD2 = d2; fallback = r.bmin[2]; haveFallback = true;
                }
                if (!haveFallback) {
                    int bestT = 2;                                 // this tile or an immediate neighbour
                    for (const auto& r : gobjs) {
                        if (r.config_id <= 0) continue;
                        if (!(r.bmax[0] > r.bmin[0])) continue;
                        int dx = r.x - gx, dy = r.y - gy;
                        int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                        if (d >= bestT) continue;
                        bestT = d; fallback = r.bmin[2]; haveFallback = true;
                    }
                }
                if (!haveFallback && plane != out.plane) {
                    float lz0;
                    if (liveZ(gx, gy, 0, lz0)) fallback = lz0;
                    else { std::int16_t g0 = rtx::cache::TileHeight(gx, gy, 0); if (g0 != kNoH) fallback = kHScale * (float)g0; }
                }
            }
            for (int i = 0; i < 4; ++i) {
                op.box[i * 3 + 0] = cx[i] * 512.f;
                op.box[i * 3 + 1] = cy[i] * 512.f;
                op.box[i * 3 + 2] = liveOk ? (float)lch[i] : (ch[i] != kNoH) ? kHScale * (float)ch[i] : fallback;
            }
            op.has_box = true;
            op.box_h = 0.f;                                        // ground tile, not a prism
        };
        {
            auto tkey = [](int tx, int ty) -> long long { return ((long long)tx << 20) | (long long)(ty & 0xFFFFF); };
            std::unordered_map<std::string, std::vector<const GuideSite*>> rgroups;
            for (const auto& gs : guide_sites)
                if (gs.region) rgroups[gs.label + '\x01' + std::to_string(gs.rgb)].push_back(&gs);
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
                    int bestD2 = 6 * 6 + 1; const RuntimeObj* bestR = nullptr;
                    for (const auto& r : gobjs) {
                        if (r.config_id <= 0 || r.plane != s->plane) continue;
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
                    op.rgb2 = first->rgb2;
                    op.wx = t.first * 512.f + 256.f; op.wy = t.second * 512.f + 256.f;
                    op.wz = cornerZ(t.first, t.second);
                    fillBox(op, t.first, t.second, 1, 1, first->plane);
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
            const bool nameHidden = !gs.label.empty() && gs.label[0] == '-';
            if (nameHidden) {
                std::size_t nl = gs.label.find('\n');
                op.label = (nl == std::string::npos) ? std::string() : gs.label.substr(nl + 1);
            }
            op.rgb = gs.rgb;
            op.rgb2 = gs.rgb2;
            op.wx = gs.gx * 512.f + 256.f; op.wy = gs.gy * 512.f + 256.f;
            {   // label anchor height on the mark's plane
                float lz;
                if (liveZ(gs.gx, gs.gy, gs.plane, lz)) op.wz = lz;
                else { std::int16_t hh = rtx::cache::TileHeight(gs.gx, gs.gy, gs.plane); op.wz = (hh == kNoH) ? out.player_z : kHScale * (float)hh; }
            }
            if (gs.gx2 >= gs.gx && gs.gy2 >= gs.gy && gs.gx2 > 0 && gs.gy2 > 0) {
                fillBox(op, gs.gx, gs.gy, gs.gx2 - gs.gx + 1, gs.gy2 - gs.gy + 1, gs.plane, true);
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
                int bestD2 = 5 * 5 + 1; const RuntimeObj* bestR = nullptr;
                for (const auto& r : gobjs) {
                    if (r.config_id <= 0 || r.plane != gs.plane) continue;
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
                int bestD2 = 8 * 8 + 1; const RuntimeObj* bestR = nullptr;
                for (const auto& r : gobjs) {
                    if (r.config_id <= 0 || r.plane != gs.plane) continue;
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
                std::unordered_set<int> rkeys;
                for (int dx = -3; dx <= 3; dx += 6)
                    for (int dy = -3; dy <= 3; dy += 6)
                        rkeys.insert((((gs.gx + dx) >> 6) << 8) | ((gs.gy + dy) >> 6));
                int bestD = 0x7fffffff, bswx = 0, bswy = 0, bW = 1, bH = 1;
                for (int key : rkeys) {
                    int rx = (key >> 8) & 0xff, ry = key & 0xff;
                    for (const auto& p : rtx::cache::RegionLocations(rx, ry)) {
                        if (p.plane != gs.plane) continue;
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
                    fillBox(op, bswx, bswy, bW, bH, gs.plane);
                    op.wx = (bswx + bW * 0.5f) * 512.f;
                    op.wy = (bswy + bH * 0.5f) * 512.f;
                    found = true;
                }
            }
            if (!found) fillTileOnGround(op, gs.gx, gs.gy, gs.plane);   // terrain-followed tile
            out.guides.push_back(op);
        }

    }

    {
        int tgx = 0, tgy = 0; bool haveTarget = false;
        bool npcAtObjective = false;
        for (const auto& hp : out.highlights) {
            if (hp.label.empty() || hp.tile_x <= 0 || hp.tile_y <= 0) continue;
            bool atObjective = guide_sites.empty();
            for (const auto& gs : guide_sites) { int dx = hp.tile_x - gs.gx, dy = hp.tile_y - gs.gy; if (dx * dx + dy * dy <= 24 * 24) { atObjective = true; break; } }
            if (atObjective) { tgx = hp.tile_x; tgy = hp.tile_y; haveTarget = true; npcAtObjective = !guide_sites.empty(); break; }
        }
        if (npcAtObjective)                                       // hide the ground tile marker; coloured marks stay
            out.guides.erase(std::remove_if(out.guides.begin(), out.guides.end(),
                                            [](const OverlayPoint& p){ return p.rgb == 0; }),
                             out.guides.end());
        else if (!haveTarget) {                                   // first navigational site (snap boxes and coloured marks never get an arrow)
            for (const auto& gs : guide_sites) { if (!gs.snap_obj && gs.rgb == 0) { tgx = gs.gx; tgy = gs.gy; haveTarget = true; break; } }
        }
        // No arrow across the y=6400 surface/dungeon band boundary (dungeons are stacked at y+6400).
        bool crossBand = haveTarget && ((out.player_ty < 6400) != (tgy < 6400));
        if (have_player && haveTarget && !crossBand) { out.has_arrow = true; out.arrow_tx = tgx; out.arrow_ty = tgy; }
    }

    if (!outline_locs.empty()) {
        std::uint64_t oroot = (root && *root > 0x10000) ? *root : 0;
        std::vector<RuntimeObj> oruntime;
        ReadRuntimeObjects(pid, oruntime);
        for (const auto& rq : outline_locs) {
            if (rq.id <= 0 || rq.plane != out.plane) continue;
            auto meta = resolve_loc(h, oroot, rq.id);
            OverlayPoint op;
            op.kind = 0;                      // empty label: neutral inspect style
            const RuntimeObj* best = nullptr; int bestD = 0x7fffffff;
            for (const auto& r : oruntime) {
                if (r.config_id != rq.id || r.plane != rq.plane) continue;
                if (!(r.bmax[0] > r.bmin[0])) continue;
                int rx = r.x, ry = r.y;
                int d1 = std::max(std::abs(rx - rq.x), std::abs(ry - rq.y));
                int cx = (int)((r.bmin[0] + r.bmax[0]) * 0.5f) / 512;
                int cy = (int)((r.bmin[1] + r.bmax[1]) * 0.5f) / 512;
                int d2 = std::max(std::abs(cx - rq.x), std::abs(cy - rq.y));
                int d = std::min(d1, d2);
                if (d <= 6 && d < bestD) { bestD = d; best = &r; }
            }
            if (best) {
                op.has_box3d = true;
                for (int j2 = 0; j2 < 3; ++j2) { op.bmin[j2] = best->bmin[j2]; op.bmax[j2] = best->bmax[j2]; }
                op.wx = (best->bmin[0] + best->bmax[0]) * 0.5f;
                op.wy = (best->bmin[1] + best->bmax[1]) * 0.5f;
                op.wz = (best->bmin[2] + best->bmax[2]) * 0.5f;
            } else {
                int W = meta.dim_x, H = meta.dim_y;
                int ax = rq.x, ay = rq.y;
                for (const auto& pl : rtx::cache::RegionLocations(rq.x >> 6, rq.y >> 6)) {
                    if (pl.id != rq.id || pl.plane != rq.plane) continue;
                    int wx = ((rq.x >> 6) << 6) + pl.x, wy = ((rq.y >> 6) << 6) + pl.y;
                    if (std::abs(wx - rq.x) > 3 || std::abs(wy - rq.y) > 3) continue;
                    ax = wx; ay = wy;
                    if (pl.rotation == 1 || pl.rotation == 3) std::swap(W, H);
                    break;
                }
                float gz;
                if (!liveZ(ax, ay, out.plane, gz)) { std::int16_t objH = rtx::cache::TileHeight(ax, ay, out.plane); gz = (objH == kNoH) ? out.player_z : kHScale * (float)objH; }
                op.has_box3d = true;
                op.bmin[0] = ax * 512.f;            op.bmin[1] = ay * 512.f;            op.bmin[2] = gz;
                op.bmax[0] = (ax + W) * 512.f;      op.bmax[1] = (ay + H) * 512.f;      op.bmax[2] = gz + 512.f;
                op.wx = (op.bmin[0] + op.bmax[0]) * 0.5f;
                op.wy = (op.bmin[1] + op.bmax[1]) * 0.5f;
                op.wz = gz;
            }
            out.highlights.push_back(op);
        }
    }

    if (want_objects && have_player) {
        constexpr int kMaxRegions = 9, kMaxObjects = 600;
        std::unordered_set<long long> seen;
        std::uint64_t rroot = (root && *root > 0x10000) ? *root : 0;   // for varbit-aware morphs
        struct RO { int id, x, y; };
        std::vector<RO> robjs;                                          // runtime placements (dedupe static)
        int oc = 0;

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
                int W = meta.dim_x, H = meta.dim_y;
                int swx = r.x - (W - 1) / 2, swy = r.y - (H - 1) / 2;
                bool placed = false;
                {
                    const int tol = std::max(W, H);
                    for (const auto& pl : rtx::cache::RegionLocations(r.x >> 6, r.y >> 6)) {
                        if (pl.id != r.config_id || pl.plane != r.plane) continue;
                        int wx = ((r.x >> 6) << 6) + pl.x, wy = ((r.y >> 6) << 6) + pl.y;
                        if (std::abs(wx - r.x) > tol || std::abs(wy - r.y) > tol) continue;
                        swx = wx; swy = wy;
                        if (pl.rotation & 1) std::swap(W, H);
                        placed = true;
                        break;
                    }
                }
                if (!placed && r.bmax[0] > r.bmin[0] && r.bmax[1] > r.bmin[1]) {
                    const float spanX = r.bmax[0] - r.bmin[0], spanY = r.bmax[1] - r.bmin[1];
                    if ((spanX >= spanY) != (W >= H)) std::swap(W, H);
                    const int ctx = (int)std::floor((r.bmin[0] + r.bmax[0]) * 0.5f / 512.f);
                    const int cty = (int)std::floor((r.bmin[1] + r.bmax[1]) * 0.5f / 512.f);
                    swx = ctx - (W - 1) / 2; swy = cty - (H - 1) / 2;
                }
                fillBox(op, swx, swy, W, H);
                op.box_h = 0.f;                      // flat tile footprint
                op.wx = (swx + W * 0.5f) * 512.f;
                op.wy = (swy + H * 0.5f) * 512.f;
                op.wz = op.box[2];
                op.head_z = op.wz;
                out.points.push_back(op);
                ++oc;
            }
        }

        int rn = 0;
        for (int key : regions) {
            if (rn++ >= kMaxRegions || oc >= kMaxObjects) break;
            int rx = (key >> 8) & 0xff, ry = key & 0xff;
            for (const auto& p : rtx::cache::RegionLocations(rx, ry)) {
                if (oc >= kMaxObjects) break;
                if (p.plane != out.plane) continue;
                auto meta = resolve_loc(h, rroot, p.id);              // varbit-aware
                if (meta.name.empty()) continue;
                if (interactable && meta.actions.empty()) continue;
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
                if (!liveZ(wx, wy, out.plane, op.wz)) { std::int16_t objH = rtx::cache::TileHeight(wx, wy, out.plane); op.wz = (objH == kNoH) ? out.player_z : kHScale * (float)objH; }
                op.kind = 0; op.label = meta.name;
                int W = meta.dim_x, H = meta.dim_y;
                if (p.rotation == 1 || p.rotation == 3) std::swap(W, H);
                fillBox(op, wx, wy, W, H);
                op.box_h = 0.f;
                op.head_z = op.wz;
                out.points.push_back(op);
                ++oc;
            }
        }
    }

    if (have_player && grid_radius > 0) {
        out.grid_r = grid_radius;
        rtx::cache::RegionBlockedFill(out.player_tx, out.player_ty, out.plane,
                                      grid_radius, out.blocked);
        if (liveTerrain) {   // the map's void flag read live: adds the blocked tiles of instances, where the cache is empty
            const int T = 2 * grid_radius + 1;
            for (int gx = 0; gx < T; ++gx) for (int gy = 0; gy < T; ++gy)
                if (LiveTileVoid(pid, out.player_tx - grid_radius + gx, out.player_ty - grid_radius + gy, out.plane) == 1)
                    out.blocked[(std::size_t)gx * T + gy] |= rtx::cache::kTileBlockFull;
        }
        rtx::cache::RegionCornerHeightsFill(out.player_tx, out.player_ty, out.plane,
                                            grid_radius, out.heights);
        if (liveTerrain) {   // same indexing as heights: ((gx*T)+gy)*4+c, corners SW SE NE NW
            const int T = 2 * grid_radius + 1; static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
            out.heights_fine.assign((std::size_t)T * T * 4, INT32_MIN);
            for (int gx = 0; gx < T; ++gx) for (int gy = 0; gy < T; ++gy) {
                const int tx = out.player_tx - grid_radius + gx, ty = out.player_ty - grid_radius + gy;
                std::int32_t lch[4];
                LiveCornerHeights(pid, tx, ty, out.plane, lch);   // effective plane + standing offset per tile; INT32_MIN corners stay unknown
                for (int c = 0; c < 4; ++c) out.heights_fine[((std::size_t)gx * T + gy) * 4 + c] = lch[c];
            }
        }
    }

    out.ok = have_player || !out.highlights.empty();
    return out.ok;
}


std::string PlayerInfoJson(std::uint32_t pid) {
    constexpr std::uint64_t kContainer = 0x199D0, kActiveIdx = 0x70, kEntryArr = 0x58,
                            kEntryWv = 0x8, kVecBegin = 0x138,
                            kVecEnd = 0x140, kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType, kUid = rtx::scn::kUid,
                            kPosX = 0x270, kPosY = 0x278, kAnim = 0xA90,
                            kPlayerData = 0x19FA8, kLocalUid = 0x48;
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
    std::uint64_t psec = (root && *root > 0x10000) ? local_player_sec_fast(h, *root, local_uid) : 0;
    if (!psec && vb && ve && *vb > 0x10000 && *ve >= *vb) {   // registry miss: scan the scene vector
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
    int ttx = tx, tty = ty;                                   // server tile; visible tile when stationary
    actor_true_tile(h, psec, ttx, tty);
    int plane = rpm<std::int32_t>(h, psec + rtx::scn::kPlane).value_or(0);   // live plane (sec + 0x40)
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

    // Interaction target: entity ptr sec+0x218, uid sec+0x1b4, resolved against the live entity
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

    // Head progress bar: psec+0xF08 -> +0x28 = head-bar list, each a ptr with fill u8 @ +0x34.
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
                if (!hb || *hb <= 0x1000000000ull || *hb > 0x00007FFFFFFFFFFFull) continue;   // skip non-pointer slots
                int fill = rpm<std::uint8_t>(h, *hb + 0x34).value_or(0);
                if (hp255 >= 0 && std::abs(fill - hp255) <= 12) continue;             // this is the HP bar
                if (firstNonHp < 0) firstNonHp = fill;
                if (fill > 0 && fill < 255) { progress = fill; break; }               // a partially-filled action bar
            }
            if (progress < 0) progress = firstNonHp;
        }
    }

    // Energy (+0x18, u8 0..100) and weight (+0x1C, signed i16) in the skill block, set directly by
    // server packets (949 handlers rva 0x1B8A00 / 0x1B8960), not vars. Sentinels: -1 / -100000.
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

    // Combat level: psec+0x10BC (psec+0x10C0 mirrors it). -1 = outside 3..152.
    int combat = -1;
    {
        int c = rpm<std::int32_t>(h, psec + 0x10BC).value_or(-1);
        if (c >= 3 && c <= 152) combat = c;
    }

    int region = ((tx >> 6) << 8) | (ty >> 6);
    int lx = tx & 63, ly = ty & 63;
    // FPS: float at MainData+0x550 (the FPS_STATS op 885 truncates it). -1 when implausible.
    int fps = -1;
    {
        auto mroot = rpm<std::uint64_t>(h, ps.mgva);
        if (mroot && *mroot > 0x10000) {
            auto f = rpm<float>(h, *mroot + 0x550);
            if (f && *f > 0.0f && *f < 4000.0f) fps = (int)*f;
        }
    }
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"in\":true,\"x\":%d,\"y\":%d,\"trueTile\":{\"x\":%d,\"y\":%d},\"plane\":%d,\"region\":%d,\"lx\":%d,\"ly\":%d,"
        "\"anim\":%d,\"moving\":%s,\"progress\":%d,\"energy\":%d,\"weight\":%d,\"combat\":%d,"
        "\"fps\":%d,\"interact\":",
        tx, ty, ttx, tty, plane, region, lx, ly, anim, moving ? "true" : "false", progress, energy, weight, combat, fps);
    std::string out = buf; out += interactJson;
    {   // Overhead (incoming hitsplats + head bar), world id, mouse, modifier keys, map loading.
        std::string pSplats; int pBar = -1;
        actor_overhead_json(h, psec, pSplats, pBar);
        int world = -1, mx = -1, my = -1, mb = 0, mods = 0, loadPct = -1, loadScreen = 0;
        if (root && *root > 0x10000) {
            auto w = rpm<std::uint64_t>(h, *root + 0x199B0);
            auto w2 = (w && *w > 0x10000) ? rpm<std::uint64_t>(h, *w + 0x20) : std::nullopt;
            if (w2 && *w2 > 0x10000) world = rpm<std::int32_t>(h, *w2 + 8).value_or(-1);
            auto st = rpm<std::uint64_t>(h, *root + kOffVarcStore);      // 0x19920: the settings/input store
            if (st && *st > 0x10000) {
                auto fx2 = rpm<float>(h, *st + 0x46D8), fy2 = rpm<float>(h, *st + 0x46DC);
                if (fx2 && fy2 && *fx2 >= -1.f && *fx2 < 32768.f && *fy2 >= -1.f && *fy2 < 32768.f) { mx = (int)*fx2; my = (int)*fy2; }
                std::uint8_t btn[3] = {0, 0, 0};
                if (rpm_bytes(h, *st + 0x46E8, btn, 3)) mb = (btn[0] ? 1 : 0) | (btn[1] ? 2 : 0) | (btn[2] ? 4 : 0);
                mods = rpm<std::uint8_t>(h, *st + 0x4F0).value_or(0);
            }
            auto cam = rpm<std::uint64_t>(h, *root + 0x19898);
            if (cam && *cam > 0x10000) {
                int a = rpm<std::int32_t>(h, *cam + 0x710).value_or(-1), b = rpm<std::int32_t>(h, *cam + 0x714).value_or(-1);
                if (a >= 0 && b >= 0) loadPct = std::min(a, b);
                loadScreen = rpm<std::uint8_t>(h, *cam + 0x71C).value_or(0) ? 1 : 0;
            }
        }
        char xb[320];
        std::snprintf(xb, sizeof(xb),
            ",\"bar\":%d,\"world\":%d,\"mouse\":{\"x\":%d,\"y\":%d,\"buttons\":%d},"
            "\"keys\":{\"shift\":%s,\"alt\":%s,\"ctrl\":%s},\"loading\":{\"pct\":%d,\"screen\":%s},\"splats\":",
            pBar, world, mx, my, mb, (mods & 1) ? "true" : "false", (mods & 2) ? "true" : "false", (mods & 4) ? "true" : "false",
            loadPct, loadScreen ? "true" : "false");
        out += xb; out += pSplats;
    }
    out += "}";
    return out;
}

// Friends list and current world (950-1): [MD+0x19970] state i32 +0x10 (2 = loaded), entries
// +0x18..+0x20 stride 0x78, display name as a NUL-terminated buffer at +0x00, world i32 +0x30 (0 = offline).
std::string SocialJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"in\":false}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{\"in\":false}";
    int world = -1;
    {
        auto w = rpm<std::uint64_t>(h, *root + 0x199B0);
        auto w2 = (w && *w > 0x10000) ? rpm<std::uint64_t>(h, *w + 0x20) : std::nullopt;
        if (w2 && *w2 > 0x10000) world = rpm<std::int32_t>(h, *w2 + 8).value_or(-1);
    }
    std::string friends; int fc = 0, online = 0; bool loaded = false;
    auto fr = rpm<std::uint64_t>(h, *root + 0x19970);
    if (fr && *fr > 0x10000) {
        loaded = rpm<std::int32_t>(h, *fr + 0x10).value_or(0) == 2;
        auto b0 = rpm<std::uint64_t>(h, *fr + 0x18), e0 = rpm<std::uint64_t>(h, *fr + 0x20);
        if (loaded && b0 && e0 && *b0 > 0x10000 && *e0 >= *b0 && (*e0 - *b0) / 0x78 <= 600) {
            const int n = (int)((*e0 - *b0) / 0x78);
            for (int i = 0; i < n; ++i) {
                std::uint64_t e = *b0 + (std::uint64_t)i * 0x78;
                char nm[16] = {0};
                if (!rpm_bytes(h, e, nm, 15)) continue;
                std::string name;
                for (int j = 0; j < 15 && nm[j]; ++j) {
                    unsigned char c = (unsigned char)nm[j];
                    if (c >= 0x20 && c <= 0x7e) name.push_back((char)c);
                    else if (c == 0xA0) name.push_back(' ');
                }
                if (name.empty()) continue;
                int fw = rpm<std::int32_t>(h, e + 0x30).value_or(0);
                if (fw < 0 || fw > 1000) fw = 0;
                if (fw > 0) ++online;
                if (fc) friends.push_back(',');
                friends += "{\"name\":\"" + json_escape(name) + "\",\"world\":" + std::to_string(fw) + "}";
                ++fc;
            }
        }
    }
    return "{\"in\":true,\"world\":" + std::to_string(world) + ",\"friendsLoaded\":" + (loaded ? "true" : "false") +
           ",\"online\":" + std::to_string(online) + ",\"friends\":[" + friends + "]}";
}

bool PlayerTile(std::uint32_t pid, int& tx, int& ty, int& plane) {
    constexpr std::uint64_t kContainer = 0x199D0, kActiveIdx = 0x70, kEntryArr = 0x58,
                            kEntryWv = 0x8, kVecBegin = 0x138,
                            kVecEnd = 0x140, kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType, kUid = rtx::scn::kUid,
                            kPosX = 0x270, kPosY = 0x278,
                            kPlayerData = 0x19FA8, kLocalUid = 0x48;
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
    int pl = rpm<std::int32_t>(h, psec + rtx::scn::kPlane).value_or(0);
    plane = (pl < 0 || pl > 3) ? 0 : pl;
    return true;
}

// Instance-var bit field of an augmented item: id = domain-5 varbit; var/lsb/msb = 950-1 fallback layout.
struct PerkField { int id; int var; int lsb; int msb; };
struct PerkFieldLayout {
    PerkField xp    { 30212, 0,  0, 19 };
    PerkField g1p1  { 30215, 1,  0, 14 }, g1p1r { 30216, 3,  0,  3 };
    PerkField g1p2  { 30217, 1, 15, 29 }, g1p2r { 30218, 3,  4,  7 };
    PerkField g2p1  { 30219, 2,  0, 14 }, g2p1r { 30220, 3,  8, 11 };
    PerkField g2p2  { 30221, 2, 15, 29 }, g2p2r { 30222, 3, 12, 15 };
    bool from_cache = false;   // every field resolved from the varbit archive
    int  drift      = 0;       // fields whose cache definition differs from the fallback
};
static const PerkFieldLayout& perk_field_layout() {
    static PerkFieldLayout L;
    if (L.from_cache) return L;
    PerkField* fs[] = { &L.xp, &L.g1p1, &L.g1p1r, &L.g1p2, &L.g1p2r, &L.g2p1, &L.g2p1r, &L.g2p2, &L.g2p2r };
    int hit = 0, drift = 0;
    for (PerkField* f : fs) {
        int var = -1, lsb = -1, msb = -1;
        if (!rtx::cache::GetObjVarbit(f->id, var, lsb, msb)) continue;
        if (var < 0 || var >= 8 || lsb < 0 || msb < lsb || msb > 31) continue;
        if (var != f->var || lsb != f->lsb || msb != f->msb) ++drift;
        ++hit;
    }
    if (hit != (int)(sizeof(fs) / sizeof(fs[0]))) return L;   // cache not ready: fallback
    for (PerkField* f : fs) {
        int var = -1, lsb = -1, msb = -1;
        rtx::cache::GetObjVarbit(f->id, var, lsb, msb);
        f->var = var; f->lsb = lsb; f->msb = msb;
    }
    L.drift = drift; L.from_cache = true;
    return L;
}

// Augmented items in inventory (93) + equipment (94): item XP / gizmo perks from instance vars.
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
    int ncont = container_count(*cstart, *cend);

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
            if (!rtx::cache::ItemIsAugmented(iid)) continue;
            std::string itemName = rtx::cache::ItemName(iid);
            std::uint64_t X = *xstart + (std::uint64_t)s * 0x38;
            int count = rpm<std::int32_t>(h, X + 0x18).value_or(0);
            if (count < 2 || count > 10) continue;
            auto ptrArr = rpm<std::uint64_t>(h, X + 0x10);
            if (!ptrArr || *ptrArr <= 0x10000) continue;
            // Slot ext-data = keyed pairs: instance-var key @+0, int value @+8. Scripts read them
            // through domain-5 varbits (INV_GETVAR, CS2 12197/12199); layout in PerkFieldLayout.
            int val[8] = {0,0,0,0,0,0,0,0};
            for (int j = 0; j < count; ++j) {
                auto p = rpm<std::uint64_t>(h, *ptrArr + (std::uint64_t)j * 0x8);
                if (!p || *p <= 0x10000) continue;
                int key = rpm<std::int32_t>(h, *p).value_or(-1);
                if (key < 0 || key >= 8) continue;
                val[key] = rpm<std::int32_t>(h, *p + 0x8).value_or(0);
            }
            const PerkFieldLayout& L = perk_field_layout();
            auto fld = [&](const PerkField& f) {
                std::uint32_t mask = (f.msb - f.lsb >= 31) ? 0xFFFFFFFFu : ((1u << (f.msb - f.lsb + 1)) - 1u);
                return (int)(((std::uint32_t)val[f.var] >> f.lsb) & mask);
            };
            int xp = fld(L.xp);
            struct PK { int id; int rank; };
            PK pk[4] = { { fld(L.g1p1), fld(L.g1p1r) }, { fld(L.g1p2), fld(L.g1p2r) },
                         { fld(L.g2p1), fld(L.g2p1r) }, { fld(L.g2p2), fld(L.g2p2r) } };
            bool g1has = pk[0].id > 0 || pk[1].id > 0;
            bool g2has = pk[2].id > 0 || pk[3].id > 0;
            int  gizmos = (g1has ? 1 : 0) + (g2has ? 1 : 0);

            std::string perks;
            for (const auto& p : pk) {
                if (p.id <= 0) continue;
                std::string nm = rtx::cache::PerkName(p.id);
                if (nm.empty()) nm = "Perk #" + std::to_string(p.id);
                // Rank printed only for multi-rank perks (CS2 12079); a single-rank perk's rank var is junk.
                int ranks = rtx::cache::PerkRankCount(p.id);
                int rank  = (ranks > 0 && ranks <= 1) ? 1 : p.rank;
                if (ranks > 1 && rank > 0) { nm += ' '; nm += std::to_string(rank); }
                if (!perks.empty()) perks.push_back(',');
                std::snprintf(buf, sizeof(buf), "{\"id\":%d,\"rank\":%d,\"ranks\":%d,\"name\":\"", p.id, rank, ranks);
                perks += buf; perks += json_escape(nm); perks += "\"";
                std::string ds = rtx::cache::PerkDesc(p.id);   // may carry <col=..> markup
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


std::string AccountKey(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_states.find((DWORD)pid);
    if (it == g_states.end()) return {};
    const auto& s = it->second;
    return !s.display_name.empty() ? s.display_name : s.character;
}

std::string ServerOpsJson() {
    std::string out = "{"; bool first = true;
    for (const auto& e : rtx::sops::kExpected) {
        out += first ? "" : ","; first = false;
        out += "\""; out += e.name; out += "\":" + std::to_string(e.op);
    }
    out += ",\"op_max\":" + std::to_string(rtx::sops::kOpMax) + "}";
    return out;
}

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

    std::string buildNote; int buildOk = 1;
    if (!version.empty()) {
        wchar_t up[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) {
            std::wstring bp = std::wstring(up) + L"\\RuneToolsX\\lastbuild.txt";
            std::string prev;
            { std::ifstream f(bp.c_str()); if (f) std::getline(f, prev); }
            if (prev.empty()) {
                buildNote = "build " + version;
            } else if (prev != version) {
                buildOk = 2;
                buildNote = "GAME UPDATED (" + prev + " -> " + version + "): watch the rows below";
            } else {
                buildNote = "build " + version + " (unchanged)";
            }
            if (prev != version) { std::ofstream f(bp.c_str(), std::ios::trunc); if (f) f << version; }
        }
    }

    auto ps = snap_proc(pid);
    add("Game client", ps ? 1 : 0, ps ? "connected" : "not connected");
    if (!buildNote.empty()) add("Game build", buildOk, buildNote);
    if (!ps) return "{\"version\":\"" + json_escape(version) + "\",\"checks\":[" + checks + "]}";
    HANDLE h = ps.h;

    auto root = rpm<std::uint64_t>(h, ps.mgva);
    bool rootOk = root && *root > 0x10000;
    add("Game data", rootOk ? 1 : 0, rootOk ? "" : "not available (lobby / loading is normal)");

    {
        std::uint32_t tc = 0; double age = 0;
        bool ok = TickState(pid, tc, age);
        const bool stale = ok && age > 5000.0;
        add("Game tick", ok ? (stale ? 0 : 1) : 0,
            !ok ? "not found" : stale ? "found but not advancing" : ("tick " + std::to_string(tc)));
    }
    if (!rootOk) return "{\"version\":\"" + json_escape(version) + "\",\"checks\":[" + checks + "]}";

    {
        auto pdata = rpm<std::uint64_t>(h, *root + 0x19FA8);
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
        bool pOk = sc.find("\"players\":[{") != std::string::npos;
        bool nOk = sc.find("\"npcs\":[{") != std::string::npos;
        add("Nearby players", pOk ? 1 : 0, pOk ? "" : "not readable");
        add("Nearby NPCs", nOk ? 1 : pOk ? 2 : 0,
            nOk ? "" : pOk ? "none nearby (or walk broken; re-run near NPCs)" : "not readable");
    }
    {
        int gx = 0, gy = 0, gw = 0, gh = 0, lw = 0, lh = 0;
        const bool tree = read_gameview_rect(h, *root, gx, gy, gw, gh, &lw, &lh);
        const int vw = read_varc(h, *root, 3001), vh = read_varc(h, *root, 3002);
        std::string d2;
        int ok2 = 0;
        if (vw > 0 && vh > 0 && tree && gw > 0) {
            const double r = (double)vw / (double)gw;
            const bool sane = r > 0.2 && r < 5.0;
            ok2 = sane ? 1 : 0;
            d2 = std::to_string(vw) + "x" + std::to_string(vh) + " px, layout " +
                 std::to_string(gw) + "x" + std::to_string(gh) +
                 (lw > 0 ? ", window " + std::to_string(lw) : "");
            if (!sane) d2 += " (spaces disagree)";
        } else if (tree && gw > 0) { ok2 = 2; d2 = "layout only (viewport record missing)"; }
        else if (vw > 0)           { ok2 = 2; d2 = "record only (layout tree missing)"; }
        else                        { ok2 = 0; d2 = "not readable (world overlays will misplace)"; }
        add("Overlay viewport", ok2, d2);
    }
    // Life points varps 13537 current / 13538 max. cur > mx is a normal boost; 3x max = remap.
    {
        const int cur = read_varp(h, *root, 13537), mx = read_varp(h, *root, 13538);
        const bool logged = cur > 0 || mx > 0;
        const bool sane = logged && mx > 0 && mx < 100000 && cur >= 0 && cur <= mx * 3;
        add("Life points", !logged ? 2 : sane ? 1 : 0,
            !logged ? "not logged in" :
            sane ? (std::to_string(cur) + " / " + std::to_string(mx) + (cur > mx ? " (boosted)" : ""))
                 : "values look wrong");
    }
    {
        auto cont = rpm<std::uint64_t>(h, *root + 0x199D0);
        bool ok = false;
        if (cont && *cont > 0x10000) {
            auto idx = rpm<std::int32_t>(h, *cont + 0x70);
            auto arr = rpm<std::uint64_t>(h, *cont + 0x58);
            if (idx && *idx >= 0 && *idx < 64 && arr && *arr > 0x10000) {
                auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + 0x8);
                if (wv && *wv > 0x10000) {
                    CamProbes probes;
                    auto worker = scene_worker(h, pid, *wv, &probes);
                    float m[16] = {};
                    ok = worker && read_view_matrix(h, pid, *root, *wv, probes, m);
                }
            }
        }
        add("Overlay camera", ok ? 1 : 0, ok ? "" : "not readable (in-world markers won't draw)");
    }
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
    {   // Server opcodes: each ServerOps.h opcode must carry its recorded wire length in the packet table.
        std::uint64_t modSize = 0;
        { std::lock_guard<std::mutex> lk(g_mu); auto it = g_states.find((DWORD)pid); if (it != g_states.end()) modSize = it->second.mod_size; }
        std::uint64_t tbl = (ps.mod_base && modSize) ? resolve_optable(h, ps.mod_base, modSize) : 0;
        int okc = 0, total = 0; std::string bad;
        for (const auto& e : rtx::sops::kExpected) {
            ++total;
            int len = 0x7FFF;
            if (tbl) {
                auto desc = rpm<std::uint64_t>(h, tbl + (std::uint64_t)e.op * 8);
                if (desc && *desc > 0x10000) len = rpm<std::int32_t>(h, *desc + kDescLenOff).value_or(0x7FFF);
            }
            if (len == e.len) ++okc; else if (bad.empty()) bad = std::string(e.name) + " (0x" + [&]{ char hx[8]; std::snprintf(hx, sizeof(hx), "%02X", e.op); return std::string(hx); }() + ")";
        }
        add("Server opcodes", !tbl ? 0 : okc == total ? 1 : 0,
            !tbl ? "packet table not found" : okc == total ? (std::to_string(total) + " opcodes carry their expected wire length")
                                                            : (std::to_string(okc) + "/" + std::to_string(total) + " match; first stale: " + bad + " (game update reshuffled opcodes)"));
    }
    {   // Varp lookups: read_varp must agree with the full-dump walk (a wrong link offset only breaks deep ids).
        std::string dump = VarpsDumpAllJson(pid);
        int checked = 0, agree = 0; size_t pos = 0;
        for (int k = 0; k < 4000 && checked < 40; ++k) {
            size_t q = dump.find('"', pos); if (q == std::string::npos) break;
            size_t e = dump.find('"', q + 1); if (e == std::string::npos) break;
            size_t c = dump.find(':', e); if (c == std::string::npos) break;
            size_t end = dump.find_first_of(",}", c); if (end == std::string::npos) break;
            pos = end;
            if ((k % 97) != 0) continue;                       // spread the sample across the map
            const char* kp = dump.c_str() + q + 1; if (kp[0] == '4' && kp[1] == ':') kp += 2;
            int id = std::atoi(kp), v = std::atoi(dump.c_str() + c + 1);
            if (id <= 0) continue;
            ++checked; if (read_varp(h, *root, id) == v) ++agree;
        }
        add("Varp lookups", checked == 0 ? 2 : agree == checked ? 1 : 0,
            checked == 0 ? "no varps to sample" : (std::to_string(agree) + "/" + std::to_string(checked) + " sampled ids agree with the full dump" + (agree == checked ? "" : " (chain link offset)")));
    }
    {   // Player State / Tasks: PlayerInfoJson must find the local player while in-world
        const bool inWorld = rpm<std::int8_t>(h, *root + kOffStatus).value_or(0) == 30;
        std::string pj = PlayerInfoJson(pid);
        const bool in = pj.find("\"in\":true") != std::string::npos;
        add("Player state", !inWorld ? 2 : in ? 1 : 0,
            !inWorld ? "not in-world" : in ? "" : "local player not found in the scene (entity layout)");
    }
    {   // Buffs panel: buff/debuff bar groups 284/291
        std::uint64_t gs = 0, ge = 0; iface_groups_range(h, *root, gs, ge);
        bool g284 = false, g291 = false;
        if (gs && ge > gs)
            for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
                std::uint64_t ap2 = rpm<std::uint64_t>(h, g + 8).value_or(0);
                if (ap2 <= 0x10000) continue;
                int gid = rpm<std::int32_t>(h, ap2).value_or(-1);
                g284 |= (gid == 284); g291 |= (gid == 291);
            }
        std::string bj = BuffsJson(pid);
        int n = 0; for (size_t i = 0; (i = bj.find("\"id\":", i)) != std::string::npos; ++i) ++n;
        add("Buff bar", (g284 || g291) ? 1 : 0,
            (g284 || g291) ? (std::to_string(n) + " effect" + (n == 1 ? "" : "s") + " read") : "buff bar interface not open (widget layout)");
    }
    {   // Chat log: chatbox widgets and the companion's packet ring (message_game opcode)
        std::string cj = ChatJson(pid);
        int lines = 0; for (size_t i = 0; (i = cj.find("\"raw\":", i)) != std::string::npos; ++i) ++lines;
        add("Chat (interface)", lines > 0 ? 1 : 0, lines > 0 ? (std::to_string(lines) + " lines") : "chatbox text not readable (widget layout)");
        wchar_t name[64]; rtx::netprobe::MakeSectionName(pid, name);
        HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        int st = 2; std::string d = "inactive (loads with the game client)";
        if (m) {
            auto* sh = reinterpret_cast<const rtx::netprobe::Share*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(rtx::netprobe::Share)));
            if (sh && sh->magic == rtx::netprobe::kMagic) {
                const bool hooked = (sh->flags & 1) != 0;
                const unsigned long long msgs = sh->chatWritten;
                if (!hooked)          { st = 0; d = "framer not hooked (pattern moved)"; }
                else if (msgs > 0)    { st = 1; d = std::to_string(msgs) + " messages captured"; }
                else                  { st = 0; d = "framer hooked but no game message seen (message_game opcode moved?)"; }
            }
            if (sh) UnmapViewOfFile((void*)sh);
            CloseHandle(m);
        }
        add("Chat (packets)", st, d);
    }
    {   // Daily challenges: slots in varbits 16574/16578/16582 (category 1..31)
        int cats = 0;
        for (int vb : { 16574, 16578, 16582 }) {
            int vp = -1, lsb = -1, msb = -1;
            if (!rtx::cache::GetVarbit(vb, vp, lsb, msb) || vp < 0) continue;
            int raw = read_varp(h, *root, vp);
            int v = (raw >> lsb) & (int)((1u << (msb - lsb + 1)) - 1);
            if (v >= 1 && v <= 31) ++cats;
        }
        const bool logged = read_varp(h, *root, 13538) > 0;
        add("Daily challenges", !logged ? 2 : cats > 0 ? 1 : 0,
            !logged ? "not logged in" : cats > 0 ? (std::to_string(cats) + " slots assigned") : "no slot reads a category (varbit ids moved?)");
    }
    {   // Perk layout: domain-5 varbits 30212, 30215..30222 and perk dbrow rank counts (Talking = 1, Honed = 6).
        const PerkFieldLayout& L = perk_field_layout();
        int talking = rtx::cache::PerkRankCount(23), honed = rtx::cache::PerkRankCount(5);
        std::string d;
        int st = 1;
        if (!L.from_cache) { st = 0; d = "varbits 30212/30215..30222 not all resolved, built-in 950-1 layout in use"; }
        else d = L.drift ? (std::to_string(L.drift) + " fields differ from the built-in layout, cache layout in use")
                         : "9 fields match the cache";
        if (talking != 1 || honed != 6) { st = 0; d += "; perk rank counts Talking=" + std::to_string(talking) + " Honed=" + std::to_string(honed) + " (expected 1/6)"; }
        else d += "; rank counts ok";
        add("Perk layout", st, d);
    }
    {   // Var domains: varbit archive defines all nine domains (0 player .. 8 campaign); var config
        std::string cen = rtx::cache::VarbitDomainsJson();
        int doms = 0;
        for (int dom = 0; dom <= 8; ++dom) if (cen.find("\"" + std::to_string(dom) + "\":{") != std::string::npos) ++doms;
        auto unknownOf = [](const std::string& j) {
            auto p = j.find("\"unknown\":"); return p == std::string::npos ? -1 : std::atoi(j.c_str() + p + 10);
        };
        int u60 = unknownOf(rtx::cache::VarDefsJson(60)), u62 = unknownOf(rtx::cache::VarDefsJson(62));
        int st = (doms == 9 && u60 == 0 && u62 == 0) ? 1 : (doms == 0 ? 2 : 0);
        add("Var domains", st, doms == 0 ? "varbit archive not readable yet" :
            std::to_string(doms) + "/9 domains defined; unknown var-config opcodes: player " + std::to_string(u60) + ", client " + std::to_string(u62));
    }
    {   // Quest tracking: a quest with no progress tracker renders as "Untracked", so this is the
        // number that separates a quiet account from a broken cache read or a changed config format.
        const std::string j = rtx::cache::QuestHealthJson();
        auto num = [&](const char* k) {
            auto p2 = j.find(std::string("\"") + k + "\":");
            return p2 == std::string::npos ? -1 : std::atoi(j.c_str() + p2 + std::strlen(k) + 3);
        };
        const int quests = num("quests"), tracked = num("tracked");
        const int failed = num("failedArchives"), varbits = num("varbits");
        int st; std::string d;
        if (quests <= 0) {
            st = 2; d = "cache not open yet";
        } else {
            d = std::to_string(tracked) + "/" + std::to_string(quests) + " quests resolve a progress tracker";
            if (varbits <= 0) d += "; varbit map EMPTY (config archive 69 unreadable)";
            else              d += "; " + std::to_string(varbits) + " varbits";
            if (failed > 0)   d += "; " + std::to_string(failed) + " config archive(s) failed to read";
            st = (tracked == quests && failed <= 0) ? 1 : 0;
            if (tracked < quests) d += " -- the rest show as Untracked (quest config opcode moved?)";
        }
        add("Quest tracking", st, d);
    }
    {   // Var domain stores: player and client tables must resolve; clan / player group absent is normal.
        std::string j = VarDomainStoresJson(pid);
        auto field = [&](const char* dom, const char* name) -> std::string {
            auto p = j.find("\"" + std::string(dom) + "\":{"); if (p == std::string::npos) return "";
            auto q = j.find("\"" + std::string(name) + "\":", p); if (q == std::string::npos) return "";
            q += std::strlen(name) + 3; auto e = j.find_first_of(",}", q);
            return j.substr(q, e - q);
        };
        bool p0 = field("0", "live") == "true";
        bool p2 = field("2", "live") == "true" && field("2", "vt") == "true";
        std::string d = "player " + (p0 ? "ok (" + field("0", "count") + " vars)" : "FAILED") +
                        ", client " + (p2 ? "ok (" + field("2", "count") + " vars)" : "FAILED") +
                        ", clan " + (field("6", "live") == "true" ? field("6", "count") + " vars" : "absent") +
                        ", player group " + (field("9", "live") == "true" ? field("9", "count") + " vars" : "absent");
        add("Var domain stores", (p0 && p2) ? 1 : 0, d);
    }
    {   // Scene objects: runtime loc ids proven by a static map placement of that id on the same tile
        std::vector<RuntimeObj> objs;
        if (ReadRuntimeObjects(pid, objs) && !objs.empty()) {
            std::unordered_map<int, std::unordered_set<long long>> regionSets;   // region key -> (id<<20|x<<10|y)
            int matched = 0, total = 0, rendered = 0;
            for (size_t i = 0; i < objs.size() && total < 300; ++i) {
                const auto& r = objs[i];
                if (r.config_id <= 0) continue;
                ++total;
                if (r.bmax[0] > r.bmin[0]) ++rendered;
                const int rx = r.x >> 6, ry = r.y >> 6, key = (rx << 8) | ry;
                auto it = regionSets.find(key);
                if (it == regionSets.end()) {
                    auto& set = regionSets[key];
                    for (const auto& pl : rtx::cache::RegionLocations(rx, ry))
                        set.insert(((long long)pl.id << 20) | ((long long)(rx * 64 + pl.x) << 10) | (long long)(ry * 64 + pl.y));
                    it = regionSets.find(key);
                }
                if (it->second.count(((long long)r.config_id << 20) | ((long long)r.x << 10) | (long long)r.y)) ++matched;
            }
            const bool ok = total == 0 || matched * 10 >= total;   // 10 % leaves headroom for dynamic areas
            add("Scene objects", ok ? 1 : 0,
                std::to_string(matched) + "/" + std::to_string(total) + " loc ids match a map placement on their tile, " +
                std::to_string(rendered) + " with a live model" + (ok ? "" : " (companion loc-id field moved)"));
        } else {
            add("Scene objects", 2, "no runtime objects published yet");
        }
    }
    {   // Actor true tile: every player and NPC must carry a well-formed movement route, and a moving
        // actor's newest route entry must sit within 2 tiles of its visible position.
        int total = 0, routes = 0, moving = 0, close = 0;
        auto root = rpm<std::uint64_t>(h, ps.mgva);
        auto cont = (root && *root > 0x10000) ? rpm<std::uint64_t>(h, *root + rtx::scn::kContainer) : std::nullopt;
        auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + rtx::scn::kActiveIdx) : std::nullopt;
        auto arr  = (cont && *cont > 0x10000) ? rpm<std::uint64_t>(h, *cont + rtx::scn::kEntryArr) : std::nullopt;
        auto wv   = (idx && *idx >= 0 && arr && *arr > 0x10000) ? rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + rtx::scn::kEntryWv) : std::nullopt;
        auto worker = (wv && *wv > 0x10000) ? scene_worker(h, pid, *wv, nullptr) : std::optional<std::uint64_t>{};
        auto vb = worker ? rpm<std::uint64_t>(h, *worker + rtx::scn::kVecBegin) : std::nullopt;
        auto ve = worker ? rpm<std::uint64_t>(h, *worker + rtx::scn::kVecEnd) : std::nullopt;
        if (vb && ve && *vb > 0x10000 && *ve >= *vb) {
            std::uint64_t n = (*ve - *vb) / 8; if (n > 20000) n = 20000;
            for (std::uint64_t i = 0; i < n && total < 300; ++i) {
                auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
                if (!ep || *ep <= 0x10000) continue;
                auto sec = rpm<std::uint64_t>(h, *ep + rtx::scn::kSecPtr);
                if (!sec || *sec <= 0x10000) continue;
                int type = rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xff);
                if (type != 1 && type != 2) continue;
                auto fx = rpm<float>(h, *sec + rtx::scn::kPosX), fy = rpm<float>(h, *sec + rtx::scn::kPosY);
                if (!fx || !fy || *fx <= 0.f || *fy <= 0.f) continue;
                ++total;
                auto mm = rpm<std::uint64_t>(h, *sec + rtx::scn::kMoveMgr);
                if (!mm || *mm <= 0x10000) continue;
                auto beg = rpm<std::uint64_t>(h, *mm + rtx::scn::kRouteBegin), end = rpm<std::uint64_t>(h, *mm + rtx::scn::kRouteEnd);
                auto wr  = rpm<std::uint64_t>(h, *mm + rtx::scn::kRouteWrite);
                if (!beg || !end || !wr || *beg <= 0x10000 || *end - *beg != 21 * rtx::scn::kRouteStride || *wr < *beg || *wr > *end) continue;
                ++routes;
                int tx = (int)(*fx / 512.f), ty = (int)(*fy / 512.f), ttx = tx, tty = ty;
                if (!actor_true_tile(h, *sec, ttx, tty)) continue;
                ++moving;
                if (std::abs(ttx - tx) <= 2 && std::abs(tty - ty) <= 2) ++close;
            }
        }
        if (total == 0) add("Actor true tile", 2, "no players or NPCs in the scene yet");
        else {
            const bool ok = routes * 10 >= total * 9 && (moving == 0 || close * 10 >= moving * 9);
            add("Actor true tile", ok ? 1 : 0,
                std::to_string(routes) + "/" + std::to_string(total) + " actors carry a movement route, " +
                std::to_string(close) + "/" + std::to_string(moving) + " moving within 2 tiles of the visible position" +
                (ok ? "" : " (route object at sec+0x268 moved?)"));
        }
    }
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
