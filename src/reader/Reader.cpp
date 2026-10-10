#include "Reader.h"
#include "Calibrate.h"
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
#include "../../companion/FrameShare.h"
#include "../../companion/MainDataOffsets.h"
#include "../../companion/Signatures.h"
#include "HealthRun.h"
#include "CodeScan.h"
#include "Pins.h"
#include "BootLog.h"
#include "Cs2Fresh.h"

#include <Windows.h>
#include <cctype>
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
#include <iterator>
#include <map>
#include <mutex>
#include <set>
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
constexpr const auto& kMainAnchorBytes = rtx::sig::kMainAnchor;
const std::size_t   kMainAnchorLen     = sizeof(kMainAnchorBytes);
constexpr int       kMainAnchorAdjust  = -32;
// Client cycle counter, u32 at root + this, 50/s (20ms units). From the CLIENTCLOCK engine op
// (scrambled 1708). NOT the server tick (kTickCounterOff, +1 per 600ms from packet 0xB4).
// These are read out of the client at attach (see Calibrate.h) and only fall back to the values
// below when the client cannot prove them, so a game update moves them without a rebuild.
std::uint32_t kOffClientClock = 0x528;
std::uint32_t kOffWorld   = 0x199B0;   // root + this -> ptr -> +0x20 -> +0x8 = world(int)
std::uint32_t kOffStatus  = 0x19FA0;   // root + this = status(int8)
std::uint32_t kOffStats   = 0x19920;   // root + this -> stats container
std::uint32_t kStatsInner = 0x7618;    // stats container + this -> skill block
std::uint32_t kOffGE      = 0x19990;   // root + this -> ptr -> +0x10 = ge slot array
std::uint32_t kOffAccount = 0x19FA8;   // root + this -> account / user-detail object
std::uint32_t kEngineClock = 0x10;     // root global + this = engine clock (u64 ms), found from code
constexpr std::uint32_t kGEArrayPad = 0x10;      // bytes from slot-container start to slot 0
constexpr std::uint32_t kGESlotSize = 0x28;      // bytes per slot
constexpr int           kGESlotCount = 8;        // members get 8, non-members 3 (rest read as empty)

// Container manager: root + this -> mgr { start@+0x8, end@+0x10 }; entry stride 0x48, id@+0x10,
// items start@+0x18 / end@+0x20 (stride 0x8: item_id@+0, stack@+4). Bank (95) present only while open.
std::uint32_t kOffInvData                = rtx::md::kContainers;   // 0x199C8
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
std::uint32_t kOffVarpHash               = rtx::md::kVarpHash;   // 0x36080 = varp manager + 0x1C0C8, derived at attach

// Varc (int + string) hashmap at store + kVarcHashOff, store = *(MainData + kOffVarcStore);
// same node layout as the varp map.
std::uint32_t kOffVarcStore    = 0x19920;
constexpr std::uint32_t kVarcHashOff     = 0x7630;
constexpr std::uint32_t kVarNodeNext     = 0x28;

// The other MainData stores the reader walks, each the companion table's value until the client
// proves another through the calibration rule of the same name (kCalibrated below), so a store that
// moved is adopted and reported instead of read at the old place.
std::uint32_t kOffChatStore     = rtx::md::kChatStore;      // 0x19880
std::uint32_t kOffClanSettings  = rtx::md::kClanSettings;   // 0x19888
std::uint32_t kOffMapMgr        = rtx::md::kMapMgr;         // 0x19898
std::uint32_t kOffInputReporter = rtx::md::kInputReport;    // 0x198B0, MainData -> input reporter
std::uint32_t kOffArrowMgr      = rtx::md::kArrowMgr;       // 0x198F0
std::uint32_t kOffIfaceOwner    = rtx::md::kIfaceOwner;     // 0x19900
std::uint32_t kOffInputProc     = rtx::md::kInputProc;      // 0x19928 (0x198E8 through 949-5)
std::uint32_t kOffPlayerGroup   = rtx::md::kPlayerGroup;    // 0x19948
std::uint32_t kOffPlayers       = rtx::md::kPlayers;        // 0x19950
std::uint32_t kOffFriends       = rtx::md::kFriends;        // 0x19970
std::uint32_t kOffTracker       = rtx::md::kTracker;        // 0x19850, MainData -> tracker groups {begin, end}
constexpr std::uint32_t kTrackerGroupSize = 0x68;           // group: id i32 +0, column ids +8/+0x10, row ids +0x20/+0x28, grid rows +0x50
std::uint32_t kOffCutscene      = rtx::md::kCutscene;       // 0x19A18
std::uint32_t kOffVarpMgr       = rtx::md::kVarpMgr;        // 0x19FB8 (0x19F78 through 949-5)
std::uint32_t kOffOptions       = rtx::md::kOptions;        // 0x535D0
// Root slots the state readers use beyond the companion table (950-1). Read through the same
// calibration names, so a rule of that name moves them when the client proves another place.
std::uint32_t kOffConnection    = rtx::md::kConnection;     // 0x198B8 -> connection: i32 logout reason +0x50
std::uint32_t kOffVarWatch      = rtx::md::kVarWatch;       // 0x198C8 -> var watch: u32 change counter +0x10, dirty byte +0x14 (bumped by the var and interface packets)
std::uint32_t kOffFriendsChat   = rtx::md::kFriendsChat;    // 0x198E0 -> friends chat channel
std::uint32_t kOffLoginMgr      = rtx::md::kLoginMgr;       // 0x19910 -> login manager: i32 state +0x10, login reply +0x16C, lobby reply +0x1B8
std::uint32_t kOffWorldInfo     = rtx::md::kWorldInfo;      // 0x535C8 -> world info: u8 quick chat +0x8, i32 system update ticks +0xC, u8 members world +0x10
std::uint32_t kOffCountry       = rtx::md::kCountry;        // 0x19AC4 i32 in the root

// Tick anchor: `FF 81 F0 DB 00 00` = INC dword [RCX+0xDBF0], preceded in the same prologue by
// `48 8B 0D <disp32>` = MOV RCX, [rip+disp32] (the owner global).
constexpr const auto& kTickIncBytes = rtx::sig::kTickInc;
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

// The same string, when it can be long: chat lines carry markup and run to a few hundred bytes.
std::string read_jagstring_long(HANDLE h, std::uint64_t str, std::uint64_t max_len = 512) {
    auto tag = rpm<std::uint8_t>(h, str + 0x17);
    if (!tag) return {};
    std::uint64_t len = 0, data = 0;
    if (*tag & 0x80) {
        auto ptr = rpm<std::uint64_t>(h, str + 0x00);
        auto n   = rpm<std::uint64_t>(h, str + 0x08);
        if (!ptr || !n || *ptr <= 0x10000) return {};
        data = *ptr; len = *n;
    } else {
        if (*tag > 0x17) return {};
        len = 0x17u - *tag; data = str;
    }
    if (len == 0 || len > max_len) return {};
    std::vector<char> buf((std::size_t)len);
    if (!rpm_bytes(h, data, buf.data(), (SIZE_T)len)) return {};
    std::string out;
    out.reserve((std::size_t)len);
    // Chat text is whatever someone typed, so a byte sequence that is not valid UTF-8 would make
    // the whole reply unparseable and take the panel down with it. Bytes above ASCII are copied
    // only as complete, well-formed sequences.
    for (std::uint64_t i = 0; i < len; ) {
        const unsigned char c = (unsigned char)buf[i];
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back((char)c); ++i; continue; }
        if (c < 32) { ++i; continue; }
        if (c < 0x80) { out.push_back((char)c); ++i; continue; }
        const int extra = (c >= 0xF0 && c <= 0xF4) ? 3 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xC2 && c <= 0xDF) ? 1 : -1;
        if (extra < 0 || i + (std::uint64_t)extra >= len) { ++i; continue; }
        bool whole = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = (unsigned char)buf[i + k];
            if (cc < 0x80 || cc > 0xBF) { whole = false; break; }
        }
        if (!whole) { ++i; continue; }
        for (int k = 0; k <= extra; ++k) out.push_back(buf[i + k]);
        i += (std::uint64_t)extra + 1;
    }
    return out;
}

// NPC combat stats in the actor object (950-1): stats[7] i32 at +0x1140 (attack, defence, strength,
// constitution, ranged, prayer, magic), baseStats[7] at +0x115C, and the level the game shows at
// +0x1178. rtx::scn::kLpCur / kLpMax are stats[3] / baseStats[3].
constexpr std::uint64_t kNpcStatsOff = 0x1140, kNpcBaseStatsOff = 0x115C, kNpcVisLevel = 0x1178;
static_assert(kNpcStatsOff + 12 == rtx::scn::kLpCur && kNpcBaseStatsOff + 12 == rtx::scn::kLpMax, "life points are the constitution slot of the stat arrays");
struct NpcStats { int cur[7]; int base[7]; int vis; };
static bool read_npc_stats(HANDLE h, std::uint64_t sec, NpcStats& out) {
    std::uint8_t b[0x3C];
    if (!rpm_bytes(h, sec + kNpcStatsOff, b, sizeof(b))) return false;
    std::memcpy(out.cur, b, 28); std::memcpy(out.base, b + 0x1C, 28); std::memcpy(&out.vis, b + 0x38, 4);
    for (int k = 0; k < 7; ++k)
        if (out.cur[k] < -100000 || out.cur[k] > 1000000000 || out.base[k] < 0 || out.base[k] > 1000000000) return false;
    return out.vis >= -1 && out.vis <= 20000;
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
        case 1:  return "Starting";
        case 10: return "Login screen";
        case 20: return "Lobby";
        case 23: return "Creating account";
        case 30: return "In-game";
        case 35:
        case 36: return "Reconnecting";
        case 37: return "Changing worlds";
        case 40: return "Returning to lobby";
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
    ULONGLONG      resolve_retry_at = 0;   // next try at the globals after a miss (backs off)
    int            resolve_misses   = 0;
    ULONGLONG      name_retry_at    = 0;
    ULONGLONG      gfx_retry_at     = 0;

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



// The calibrated offsets with the values they were compiled with, taken at start-up before any
// attach can change them.
struct CalibratedOffset { const char* name; std::uint32_t* at; std::uint32_t compiled; };
const CalibratedOffset kCalibrated[] = {
    { "kOffClientClock", &kOffClientClock, kOffClientClock },
    { "kOffWorld",       &kOffWorld,       kOffWorld },
    { "kOffStatus",      &kOffStatus,      kOffStatus },
    { "kOffStats",       &kOffStats,       kOffStats },
    { "kStatsInner",     &kStatsInner,     kStatsInner },
    { "kOffGE",          &kOffGE,          kOffGE },
    { "kOffVarcStore",   &kOffVarcStore,   kOffVarcStore },
    { "kOffAccount",     &kOffAccount,     kOffAccount },
    { "kEngineClock",    &kEngineClock,    kEngineClock },
    { "kOffVarpHash",    &kOffVarpHash,    kOffVarpHash },
    { "md::kContainers", &kOffInvData,     kOffInvData },
    { "md::kChatStore",  &kOffChatStore,   kOffChatStore },
    { "md::kClanSettings", &kOffClanSettings, kOffClanSettings },
    { "md::kMapMgr",     &kOffMapMgr,      kOffMapMgr },
    { "md::kInputReport", &kOffInputReporter, kOffInputReporter },
    { "md::kIfaceOwner", &kOffIfaceOwner,  kOffIfaceOwner },
    { "md::kInputProc",  &kOffInputProc,   kOffInputProc },
    { "md::kPlayerGroup", &kOffPlayerGroup, kOffPlayerGroup },
    { "md::kPlayers",    &kOffPlayers,     kOffPlayers },
    { "md::kFriends",    &kOffFriends,     kOffFriends },
    { "md::kTracker",    &kOffTracker,     kOffTracker },
    { "md::kCutscene",   &kOffCutscene,    kOffCutscene },
    { "md::kVarpMgr",    &kOffVarpMgr,     kOffVarpMgr },
    { "md::kOptions",    &kOffOptions,     kOffOptions },
    { "md::kConnection", &kOffConnection,  kOffConnection },
    { "md::kVarWatch",   &kOffVarWatch,    kOffVarWatch },
    { "md::kFriendsChat", &kOffFriendsChat, kOffFriendsChat },
    { "md::kLoginMgr",   &kOffLoginMgr,    kOffLoginMgr },
    { "md::kWorldInfo",  &kOffWorldInfo,   kOffWorldInfo },
    { "md::kCountry",    &kOffCountry,     kOffCountry },
};

// Ask the client itself for the offsets, once per attach. The exe on disk is read, not the process.
// Each offset is the client's when it proved it, else the compiled value, never what an earlier
// attach proved: that client may have been another build.
void calibrate_offsets(HANDLE h) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameExW(h, nullptr, path, MAX_PATH)) {
        for (const auto& c : kCalibrated) *c.at = c.compiled;
        rtx::log::Client(GetProcessId(h), "calibrate: client exe not found, using the compiled offsets");
        return;
    }
    std::wstring opcodes;
    {
        wchar_t up[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH))
            opcodes = std::wstring(up) + L"\\RuneToolsX\\cs2\\opcodes.json";
    }
    // nothing found (an operation table from another build among other reasons) gives every offset
    // its compiled value
    rtx::calib::Run(path, opcodes);
    for (const auto& c : kCalibrated) *c.at = rtx::calib::Use(c.name, c.compiled);
    {
        // what the client proved about itself, or why it proved nothing, on the record at every
        // attach; the report is one line per offset, and the log takes a line at a time
        const std::string rep = rtx::calib::Report();
        const DWORD who = GetProcessId(h);
        std::size_t at = 0;
        while (at < rep.size()) {
            std::size_t nl = rep.find((char)10, at);
            if (nl == std::string::npos) nl = rep.size();
            if (nl > at) rtx::log::Client(who, rep.substr(at, nl - at));
            at = nl + 1;
        }
    }
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

    calibrate_offsets(h);
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
    if (s.gfx_mode.empty() && GetTickCount64() >= s.gfx_retry_at) {   // module walk + a settings file read: not every 200 ms
        s.gfx_mode = detect_gfx_mode(s.proc);
        if (s.gfx_mode.empty()) s.gfx_retry_at = GetTickCount64() + 5000;
    }
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
            auto acct = rpm<std::uint64_t>(s.proc, *root + kOffAccount);
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

    // Resolving walks the whole client image and runs under the lock every bridge call waits on, so after a
    // miss (a game update moved a pattern) it is tried again at 1 s, doubling to 30 s, not every 200 ms.
    const ULONGLONG nowMs = GetTickCount64();
    for (auto& [pid, s] : g_states) {
        if (!s.proc) continue;
        if (s.display_name.empty() && nowMs >= s.name_retry_at) {
            s.display_name = read_target_env(s.proc, L"JX_DISPLAY_NAME");
            if (!s.display_name.empty())
                rtx::log::Client(pid, "resolved display name: " + s.display_name);
            else s.name_retry_at = nowMs + 5000;
        }
        if ((s.main_global_va == 0 || s.tick_owner_va == 0) && s.mod_base && nowMs >= s.resolve_retry_at) {
            if (s.main_global_va == 0) {
                s.main_global_va = resolve_main_global(s.proc, s.mod_base, s.mod_size);
                if (s.main_global_va) rtx::log::Client(pid, "resolved MainData global");
            }
            if (s.tick_owner_va == 0) {
                s.tick_owner_va = resolve_tick_owner_global(s.proc, s.mod_base, s.mod_size);
                if (s.tick_owner_va) rtx::log::Client(pid, "resolved tick-owner global");
            }
            if (s.main_global_va == 0 || s.tick_owner_va == 0) {
                const int shift = s.resolve_misses < 5 ? s.resolve_misses : 5;
                s.resolve_retry_at = nowMs + std::min<ULONGLONG>(30000, 1000ull << shift);
                if (++s.resolve_misses == 1) rtx::log::Client(pid, "a client global did not resolve; retrying with backoff");
            }
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
std::vector<ClientPresence> s_presence;
bool              s_presence_set = false;
std::atomic<bool> s_sampler_started{false};

std::string BuildSamplesJson() {
    auto snaps = SampleAll();
    {
        std::vector<ClientPresence> pres;
        pres.reserve(snaps.size());
        for (const auto& s : snaps) {
            ClientPresence p;
            p.pid = s.pid; p.status = s.status; p.in_world = s.in_world; p.display_name = s.display_name;
            for (int i = 0; i < 29; ++i) p.xp[i] = -1;
            for (size_t i = 0; i < s.skills.size() && i < 29; ++i) p.xp[i] = s.skills[i].xp;
            p.have_xp = !s.skills.empty();
            pres.push_back(std::move(p));
        }
        std::lock_guard<std::mutex> lk(s_samples_mu);
        s_presence.swap(pres); s_presence_set = true;
    }
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
        wchar_t name[rtx::ipc::kNameChars];
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
    bool                                  read_since_build = true;   // someone took the value since it was built
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
                // Only a value someone has taken since it was built is built again: a panel polling every
                // 250 ms costs one build per poll, not the 2.5 a fixed 100 ms rebuild would.
                if (it->second.read_since_build) {
                    it->second.read_since_build = false;
                    jobs.emplace_back(it->first, it->second.build);
                }
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
        bool any = false;
        { std::lock_guard<std::mutex> lk(s_async_mu); any = !s_async.empty(); }
        Sleep(!jobs.empty() ? 100 : any ? 50 : 250);   // with panels open, come back soon for the next read
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
        e.read_since_build = true;
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

std::vector<ClientPresence> LastPresence() {
    ensure_sampler_started();
    {
        std::lock_guard<std::mutex> lk(s_samples_mu);
        if (s_presence_set) return s_presence;
    }
    BuildSamplesJson();   // first call before the background loop has run
    std::lock_guard<std::mutex> lk(s_samples_mu);
    return s_presence;
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
    // entries sort by the key at +0 = id * 2 | other-player flag: the own container is the even key
    // (another player's of the same id sits beside it during a trade); an entry matched by id alone
    // is the fallback
    std::uint64_t pick = 0, byId = 0;
    for (int c = 0; c < ncont && !pick; ++c) {
        const std::uint64_t e = *cstart + (std::uint64_t)c * kContainerStride;
        if (rpm<std::int32_t>(h, e + 0x10).value_or(-1) != container_id) continue;
        if (!byId) byId = e;
        if (rpm<std::int32_t>(h, e).value_or(-1) == container_id * 2) pick = e;
    }
    if (!pick) pick = byId;
    if (pick) {
        const std::uint64_t e = pick;
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

// An Essence of Finality amulet (plain or augmented, any variant): it stores one weapon's special attack.
static bool item_is_eof(int item) {
    if (item < 0) return false;
    static std::mutex mu;
    static std::unordered_map<int, bool> memo;
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = memo.find(item);
        if (it != memo.end()) return it->second;
    }
    std::string n = rtx::cache::ItemName(item);
    for (char& ch : n) ch = (char)std::tolower((unsigned char)ch);
    const bool yes = n.rfind("essence of finality amulet", 0) == 0 || n.rfind("augmented essence of finality amulet", 0) == 0;
    std::lock_guard<std::mutex> lk(mu);
    memo[item] = yes;
    return yes;
}

// One container slot's item vars (keys 0..7) from its extras record (0x38 bytes: pointer array +0x10, count +0x18;
// each pointer -> key +0, value +8). A slot without vars reads as all zero. False when a read failed.
static bool slot_item_vars(HANDLE h, std::uint64_t xs, int s, int val[8], int& reads, int& fails) {
    for (int k = 0; k < 8; ++k) val[k] = 0;
    std::uint8_t xb[0x38];
    ++reads;
    if (!rpm_bytes(h, xs + (std::uint64_t)s * 0x38, xb, sizeof(xb))) { ++fails; return false; }
    const int cnt = *reinterpret_cast<const std::int32_t*>(xb + 0x18);
    const std::uint64_t arr = *reinterpret_cast<const std::uint64_t*>(xb + 0x10);
    if (cnt <= 0) return true;
    if (cnt > 16 || arr <= 0x10000) return false;
    std::uint64_t ptrs[16] = {};
    ++reads;
    if (!rpm_bytes(h, arr, ptrs, (std::size_t)cnt * 8)) { ++fails; return false; }
    for (int j = 0; j < cnt; ++j) {
        if (ptrs[j] <= 0x10000) continue;
        std::int32_t node[4] = {};
        ++reads;
        if (!rpm_bytes(h, ptrs[j], node, sizeof(node))) { ++fails; return false; }
        if (node[0] >= 0 && node[0] < 8) val[node[0]] = node[2];
    }
    return true;
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

std::string EntityVarsJson(std::uint32_t pid) {
    CombatSample s;
    if (!CombatRead(pid, false, s)) return "[]";
    auto ps = snap_proc(pid);
    if (!ps) return "[]";
    HANDLE h = ps.h;
    std::string o = "[";
    bool first = true;
    for (const auto& a : s.actors) {
        if (!a.addr) continue;
        // the map: bucket array +8, divisor +0x10, count +0x18; nodes of 0x30: id +0, value +8, type +0x20, next +0x28
        const std::uint64_t m = a.addr + 0x148;
        const std::uint64_t ba = rpm<std::uint64_t>(h, m + 8).value_or(0);
        const int div = rpm<std::int32_t>(h, m + 0x10).value_or(0);
        const int cnt = rpm<std::int32_t>(h, m + 0x18).value_or(0);
        std::string vars;
        if (ba > 0x10000 && div > 0 && div <= 4096 && cnt > 0 && cnt <= 4096) {
            for (int b = 0; b < div; ++b) {
                std::uint64_t node = rpm<std::uint64_t>(h, ba + (std::uint64_t)b * 8).value_or(0);
                for (int k = 0; k < 64 && node > 0x10000; ++k) {
                    std::uint8_t nb[0x30];
                    if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
                    const int id = *reinterpret_cast<const std::int32_t*>(nb), v = *reinterpret_cast<const std::int32_t*>(nb + 8);
                    vars += (vars.empty() ? "" : ",") + std::string("[") + std::to_string(id) + "," + std::to_string(v) + "," + std::to_string(nb[0x20]) + "]";
                    std::memcpy(&node, nb + 0x28, 8);
                }
            }
        }
        o += first ? "" : ","; first = false;
        o += "{\"type\":" + std::to_string(a.type) + ",\"uid\":" + std::to_string(a.uid) + ",\"id\":" + std::to_string(a.id) + ",\"name\":\"" + a.name +
             "\",\"self\":" + (a.self ? "true" : "false") + ",\"target\":" + std::to_string(a.targetUid) + ",\"kind\":" + std::to_string(a.targetKind) +
             ",\"count\":" + std::to_string(cnt) + ",\"vars\":[" + vars + "]}";
    }
    return o + "]";
}

// The varp's node (0x30 bytes: id +0, value +8, type byte +0x20, next +0x28). Unreadable = the map
// itself could not be read (stale offset); Absent = the map holds no node for the id, which the
// engine reads as the domain default (0 for an int).
enum class VarRead { Unreadable, Absent, Found };
static VarRead read_varp_node(HANDLE h, std::uint64_t root, int varp_id, std::uint8_t nb[0x30]) {
    if (varp_id < 0) return VarRead::Unreadable;
    std::uint64_t hashRoot = root + kOffVarpHash;
    auto ba_o  = rpm<std::uint64_t>(h, hashRoot + 0x8);
    auto div_o = rpm<std::int32_t>(h, hashRoot + 0x10);
    if (!ba_o || !div_o) return VarRead::Unreadable;
    std::uint64_t ba = *ba_o; int div = *div_o;
    if (ba <= 0x10000 || div <= 0 || div > 2000000) return VarRead::Unreadable;   // stale-offset guard
    auto bucket = rpm<std::uint64_t>(h, ba + (std::uint64_t)(varp_id % div) * 8);
    if (!bucket) return VarRead::Unreadable;
    std::uint64_t node = *bucket;
    for (int i = 0; i < 128 && node > 0x10000; ++i) {
        if (!rpm_bytes(h, node, nb, 0x30)) return VarRead::Unreadable;
        if (*reinterpret_cast<const std::int32_t*>(nb) == varp_id) return VarRead::Found;
        std::memcpy(&node, nb + kVarNodeNext, 8);
    }
    return VarRead::Absent;
}
// true only when the store holds the varp; an unset varp is not "found, 0".
static bool read_varp_found(HANDLE h, std::uint64_t root, int varp_id, int& out) {
    out = 0;
    std::uint8_t nb[0x30];
    if (read_varp_node(h, root, varp_id, nb) != VarRead::Found) return false;
    std::memcpy(&out, nb + 8, 4);
    return true;
}
// The value a script sees: the node's int, or 0 when unset. false only when the map is unreadable.
static bool read_varp_default(HANDLE h, std::uint64_t root, int varp_id, int& out) {
    out = 0;
    std::uint8_t nb[0x30];
    const VarRead r = read_varp_node(h, root, varp_id, nb);
    if (r == VarRead::Unreadable) return false;
    if (r == VarRead::Found) std::memcpy(&out, nb + 8, 4);
    return true;
}
static int read_varp(HANDLE h, std::uint64_t root, int varp_id) {
    int v = 0;
    read_varp_default(h, root, varp_id, v);
    return v;
}
// A varbit as the engine computes it: an arithmetic shift by the low bit, then a mask of
// (high - low + 1) bits whose shift count is taken mod 32, so a 32-bit-wide varbit reads 0.
static int varbit_bits(int raw, int lsb, int msb) {
    if (lsb < 0 || msb < lsb || msb > 31) return 0;
    const unsigned width = (unsigned)(msb - lsb + 1) & 31u;
    return (raw >> lsb) & (int)((1u << width) - 1u);
}
// The varbit's live value: its varp must hold an int (type byte 0) or the engine reads 0; an unset
// varp reads 0 too. false when the varbit has no definition or the map is unreadable.
static bool read_varbit_live(HANDLE h, std::uint64_t root, int varbit_id, int& out) {
    out = 0;
    int vp = -1, lsb = -1, msb = -1;
    if (!rtx::cache::GetVarbit(varbit_id, vp, lsb, msb) || vp < 0 || lsb < 0 || msb < lsb || msb > 31) return false;
    std::uint8_t nb[0x30];
    const VarRead r = read_varp_node(h, root, vp, nb);
    if (r == VarRead::Unreadable) return false;
    if (r == VarRead::Found && nb[0x20] == 0) { int raw = 0; std::memcpy(&raw, nb + 8, 4); out = varbit_bits(raw, lsb, msb); }
    return true;
}

static constexpr std::size_t kVarcStrCap = 1024;

// EASTL basic_string: flag@+0x17; bit 7 set = heap {char*@+0, size@+8, cap@+0x10}, clear = SSO with
// (0x17 - flag) chars inline at +0 (flag 0 = 23 chars, 0x17 = empty). Same class the companion reads
// off the CS2 VM string stack. false for an empty string, as for an unreadable one.
static bool read_eastl_string(HANDLE h, std::uint64_t strbase, std::string& out) {
    out.clear();
    auto flag_o = rpm<std::uint8_t>(h, strbase + 0x17);
    if (!flag_o) return false;
    const std::uint8_t flag = *flag_o;
    std::size_t size; std::uint64_t src;
    if (flag & 0x80) {                                        // heap
        src  = rpm<std::uint64_t>(h, strbase + 0x0).value_or(0);
        std::uint64_t sz = rpm<std::uint64_t>(h, strbase + 0x8).value_or(0);
        if (src <= 0x10000 || src > 0x00007FFFFFFFFFFFull || sz == 0 || sz > 0x2000) return false;
        size = (std::size_t)std::min<std::uint64_t>(sz, kVarcStrCap);
    } else {                                                  // SSO
        if (flag >= 0x17) return false;
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

// Varc string: int and string varcs share one map; the node's type byte at +0x20 says which (0 int,
// 1 long, 2 string) and the value union at node+8 is an EASTL string for type 2. An int node's
// union bytes would decode as a 23-char string of leftovers, so the type is checked first.
// false for absent / int-typed / empty.
static bool read_varc_str(HANDLE h, std::uint64_t client, int varc_id, std::string& out) {
    out.clear();
    auto n = varc_node(h, client, varc_id);
    if (!n || rpm<std::uint8_t>(h, *n + 0x20).value_or(0xFF) != 2) return false;
    return read_eastl_string(h, *n + 0x8, out);
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
            int v = 0;
            if (read_varbit_live(h, *root, vb, v)) value = v;
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

bool Varps(std::uint32_t pid, const std::vector<int>& ids, std::vector<int>& out) {
    auto ps = snap_proc(pid);
    if (!ps) return false;
    auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
    if (!root || *root <= 0x10000) return false;
    out.resize(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) out[i] = read_varp(ps.h, *root, ids[i]);
    return true;
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
        int val = 0;
        read_varbit_live(h, *root, id, val);
        std::snprintf(buf, sizeof(buf), "%s\"%d\":%d", first ? "" : ",", id, val);
        out += buf; first = false;
    }
    out += "}";
    return out;
}

// Member is engine state, not a var: PLAYERMEMBER op = acct = *(MainData+0x19FA8), *(u8*)(acct+0x28) != 0. Premier = varbit 50572 (varp 10287 bit 5) ANDed with member as script15757 does (legacy varp 12864 reads 0).
// idleLogoutSeconds is derived and server-enforced: 5 min base, +5 members, +5 Jagex account, cap 15, out of combat only.

constexpr std::uint32_t kOffAcctIsMember = 0x28;      // u8, nonzero = members
constexpr std::uint32_t kOffAcctExpiry   = 0x30;      // u64, raw value LOBBY_MEMBERSHIP divides down
constexpr int           kPremierVarbit   = 50572;

constexpr std::uint32_t kOffRepPointerA   = 0x28;     // u64 ms, last pointer flush (recorder A)
constexpr std::uint32_t kOffRepPointerB   = 0x50;     // u64 ms, last pointer flush (recorder B)
constexpr std::uint32_t kOffRepKeyboard   = 0x2858;   // u64 ms, last key batch (0 = none yet)


// Client state that lives outside the var stores, from the engine op handlers (950-1):
// CUTSCENE ops read [MainData+0x19A18]: +0x150 = current cutscene id, -1 when none.
// CLIENTOPTION_GET (0x1401BE8F0): option objects at [[MainData+0x535D0]+0x2DA8 + id*8], value i32 at
// +0x18 (option 39 is a byte), 44 options; the op adds 1 to option 28. Each object also holds its name
// (char* +0x10, a literal in the client image) and range (i32 +0x1C min, +0x20 max; not on 39). Ids 16
// and 17 share the name RemoveRoof: key by id, as the settings scripts do with CLIENTOPTION_GET/SET.
// The value lists the options' own classes carry (their GetValueNames slot) are kept here by name;
// the client-side preset (AutoSetup) object sits at ClientOptions+0 and the window mode object at
// +0x12D0 (value +0x12E8, 1 Small, 2 Resizable, 3 Fullscreen), both outside the 44; fullscreen
// width/height i32 at +0x13C8/+0x13CC.
// (The op the export calls TEXTINPUT_ISFOCUSED reads [[MainData+0x19FA8]+0x14], which client script
// 14944 feeds to DATE_RUNEDAY_TODATE for an age check: that field is the account's date of birth, not
// a focus flag.)
struct OptionLabels { const char* name; const char* labels; int base; };   // '|' separated, index = value - base
static const OptionLabels kOptionLabels[] = {
    { "AmbientOcclusion",       "Off|SSAO|HBAO|HBAO Ultra|Nvidia HBAO+", 0 },
    { "AnisotropicFiltering",   "Off|2x|4x|8x|16x", 0 },
    { "AntialiasingMode",       "Off|FXAA|MSAA|MSAA + FXAA", 0 },
    { "AntialiasingQuality",    "Low|Medium|High|Ultra|Ultra+", 0 },
    { "Bloom",                  "Off|Low|Medium|High", 0 },
    { "CustomCursors",          "Off|On", 0 },
    { "DOF",                    "Off|On", 0 },
    { "DrawDistance",           "Low|Medium|High|Ultra|Ultra+", 0 },
    { "GroundBlending",         "Off|On", 0 },
    { "GroundDecor",            "Off|On", 0 },
    { "LightingQuality",        "Low|Medium|High|Ultra|Ultra+", 0 },
    { "Reflections",            "Low|Medium|High|Ultra|Ultra+", 0 },
    { "RemoveRoof",             "None|Always|Selective|All", 0 },
    { "Shadows",                "Off|On", 0 },
    { "ShadowQuality",          "Low|Medium|High|Ultra|Ultra+", 0 },
    { "Texturing",              "Off|Compressed|Uncompressed", 0 },
    { "VolumetricLighting",     "Off|Low|Medium|High|Ultra|Ultra+", 0 },
    { "VSync",                  "Adaptive|Off|On|Half|Quarter", -1 },   // value -1..3 (the options[] entry is value + 1)
    { "EntityHighlights",       "Off|On", 0 },
    { "SmoothClipFade",         "Off|On", 0 },
    { "CanopyCutout",           "Off|On", 0 },
    { "Diagnostics",            "Off|On", 0 },
    { "RichPresence",           "Off|On", 0 },
    { "Language",               "English|German|French|Portuguese (BR)|Dutch|Spanish|Spanish (MX)", 0 },
    { "HapticFeedback",         "Off|On", 0 },
    { "ParticleQuality",        "Min|Low|Medium|High", 0 },
    { "CustomPlayerModelCount", "Low|Medium|High|Ultra|Ultra+", 0 },
    { "CutsceneSubtitles",      "Off|On", 0 },
    { "ConsoleKey",             "Default|Alternate", 0 },
    { "CursorScale",            "X1|X2|X3", 0 },
    { "AnimationQuality",       "Low|High", 0 },
    { "WindowMode",             "|Small|Resizable|Fullscreen", 0 },
    { "AutoSetup",              "Custom|Minimum|Low|Medium|High|Ultra|Ultra+|Powersave", 0 },
};
static std::string option_label(const std::string& name, long long value) {
    for (const auto& o : kOptionLabels) {
        if (name != o.name) continue;
        long long idx = value - o.base;
        const char* p = o.labels;
        while (idx > 0 && *p) { if (*p == '|') --idx; ++p; }
        if (idx != 0) return {};
        std::string s;
        while (*p && *p != '|') s.push_back(*p++);
        return s;
    }
    return {};
}
// The option's name: a literal in the client image, read by its pointer at +0x10.
static std::string option_name(HANDLE h, const ProcSnap& ps, std::uint64_t obj) {
    const std::uint64_t p = rpm<std::uint64_t>(h, obj + 0x10).value_or(0);
    if (!ps.mod_base || !ps.mod_size || p < ps.mod_base || p + 40 > ps.mod_base + ps.mod_size) return {};
    char buf[40] = {};
    if (!rpm_bytes(h, p, buf, sizeof(buf) - 1)) return {};
    std::string s;
    for (int i = 0; i < (int)sizeof(buf) && buf[i]; ++i) {
        const char c = buf[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return {};
        s.push_back(c);
    }
    return s;
}
std::string ClientStateJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    auto heap = [](std::uint64_t p) { return p > 0x10000 && p <= 0x00007FFFFFFFFFFFull; };
    std::string out = "{";
    int cutscene = -1;
    if (auto cs = rpm<std::uint64_t>(h, *root + kOffCutscene); cs && *cs > 0x10000) cutscene = rpm<std::int32_t>(h, *cs + 0x150).value_or(-1);
    out += "\"cutscene\":" + std::to_string(cutscene) + ",\"inCutscene\":" + std::string(cutscene != -1 ? "true" : "false");
    // world info: system update countdown in ticks (0 = none), members world
    {
        const std::uint64_t wi = rpm<std::uint64_t>(h, *root + kOffWorldInfo).value_or(0);
        int reboot = -1, members = -1, quick = -1;
        if (heap(wi)) {
            reboot = rpm<std::int32_t>(h, wi + 0xC).value_or(-1);
            members = rpm<std::uint8_t>(h, wi + 0x10).value_or(0) ? 1 : 0;
            quick = rpm<std::uint8_t>(h, wi + 0x8).value_or(0) ? 1 : 0;
        }
        if (reboot < 0 || reboot > 100000000) reboot = -1;
        out += ",\"systemUpdateTicks\":" + std::to_string(reboot) + ",\"membersWorld\":" + std::to_string(members) + ",\"quickChatWorld\":" + std::to_string(quick);
    }
    // connection and login: why the last session ended, the last login and lobby reply codes
    {
        const std::uint64_t cn = rpm<std::uint64_t>(h, *root + kOffConnection).value_or(0);
        const std::uint64_t lm = rpm<std::uint64_t>(h, *root + kOffLoginMgr).value_or(0);
        out += ",\"logoutReason\":" + std::to_string(heap(cn) ? rpm<std::int32_t>(h, cn + 0x50).value_or(-1) : -1);
        out += ",\"loginState\":" + std::to_string(heap(lm) ? rpm<std::int32_t>(h, lm + 0x10).value_or(-1) : -1);
        out += ",\"loginReply\":" + std::to_string(heap(lm) ? rpm<std::int32_t>(h, lm + 0x16C).value_or(-1) : -1);
        out += ",\"lobbyReply\":" + std::to_string(heap(lm) ? rpm<std::int32_t>(h, lm + 0x1B8).value_or(-1) : -1);
    }
    // camera: field of view (radians, vertical and horizontal) and the control mode byte
    {
        const std::uint64_t mm = rpm<std::uint64_t>(h, *root + kOffMapMgr).value_or(0);
        float fov = 0.f, fovX = 0.f; int ctl = -1;
        if (heap(mm)) {
            fov = rpm<float>(h, mm + 0x220).value_or(0.f); fovX = rpm<float>(h, mm + 0x224).value_or(0.f);
            ctl = (int)rpm<std::uint8_t>(h, mm + 0xE8).value_or(0xFF);
            if (ctl == 0xFF) ctl = -1;
        }
        if (!(fov > 0.f && fov < 4.f)) fov = 0.f;
        if (!(fovX > 0.f && fovX < 4.f)) fovX = 0.f;
        char cb[96];
        std::snprintf(cb, sizeof(cb), ",\"fov\":%.4f,\"fovX\":%.4f,\"cameraControl\":%d", (double)fov, (double)fovX, ctl);
        out += cb;
    }
    // var change counter: bumped by the var and interface packets, a cheap "something changed" signal
    {
        const std::uint64_t vw = rpm<std::uint64_t>(h, *root + kOffVarWatch).value_or(0);
        const long long seq = heap(vw) ? (long long)rpm<std::uint32_t>(h, vw + 0x10).value_or(0) : -1;
        out += ",\"varSeq\":" + std::to_string(seq);
    }
    out += ",\"country\":" + std::to_string(rpm<std::int32_t>(h, *root + kOffCountry).value_or(-1));
    out += ",\"options\":[";
    std::string named;
    const std::uint64_t opt = rpm<std::uint64_t>(h, *root + kOffOptions).value_or(0);
    if (heap(opt)) {
        for (int i = 0; i < 44; ++i) {
            auto o = rpm<std::uint64_t>(h, opt + 0x2DA8 + (std::uint64_t)i * 8);
            long long v = 0, raw = 0;
            std::string name;
            int mn = 0, mx = 0;
            if (o && *o > 0x10000) {
                v = (i == 39) ? (long long)rpm<std::uint8_t>(h, *o + 0x18).value_or(0) : (long long)rpm<std::int32_t>(h, *o + 0x18).value_or(0);
                raw = v;
                name = option_name(h, ps, *o);
                if (i != 39) { mn = rpm<std::int32_t>(h, *o + 0x1C).value_or(0); mx = rpm<std::int32_t>(h, *o + 0x20).value_or(0); }
            }
            if (i == 28) v += 1;
            out += (i ? "," : "") + std::to_string(v);
            if (name.empty()) continue;
            named += (named.empty() ? "" : ",") + std::string("{\"id\":") + std::to_string(i) + ",\"name\":\"" + name + "\",\"value\":" + std::to_string(raw);
            const std::string label = option_label(name, raw);
            if (!label.empty()) named += ",\"label\":\"" + label + "\"";
            if (i != 39) named += ",\"min\":" + std::to_string(mn) + ",\"max\":" + std::to_string(mx);
            named += "}";
        }
    }
    out += "],\"optionsNamed\":[" + named + "]";
    if (heap(opt)) {
        const int wm = rpm<std::int32_t>(h, opt + 0x12E8).value_or(-1), preset = rpm<std::int32_t>(h, opt + 0x18).value_or(-1);
        const int fw = rpm<std::int32_t>(h, opt + 0x13C8).value_or(0), fh = rpm<std::int32_t>(h, opt + 0x13CC).value_or(0);
        const std::string wmName = option_name(h, ps, opt + 0x12D0);   // "WindowMode" when the object is where the reader expects it
        out += ",\"windowMode\":" + std::to_string(wmName == "WindowMode" ? wm : -1) + ",\"windowModeLabel\":\"" + (wmName == "WindowMode" ? option_label("WindowMode", wm) : std::string()) + "\"";
        out += ",\"fullscreenSize\":[" + std::to_string(fw) + "," + std::to_string(fh) + "]";
        out += ",\"preset\":" + std::to_string(preset >= 0 && preset <= 7 ? preset : -1) + ",\"presetLabel\":\"" + (preset >= 0 && preset <= 7 ? option_label("AutoSetup", preset) : std::string()) + "\"";
    }
    out += "}";
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

    int premier = 0;
    if (read_varbit_live(h, *root, kPremierVarbit, premier)) premier = premier ? 1 : 0; else premier = 0;
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

// The var change counter at [[MainData+0x198C8]+0x10]: the var and interface packet handlers add one
// after every change they apply. -1 when unreadable.
static long long read_var_seq(HANDLE h, std::uint64_t root) {
    const std::uint64_t vw = rpm<std::uint64_t>(h, root + kOffVarWatch).value_or(0);
    if (vw <= 0x10000 || vw > 0x00007FFFFFFFFFFFull) return -1;
    auto v = rpm<std::uint32_t>(h, vw + 0x10);
    return v ? (long long)*v : -1;
}

// The player var store's change-stamp map at root+0x19FB8+0x38170 (buckets +0x38178, bucket count
// +0x38180): node {var id i32 +0, engine-clock ms + 500 i64 +8, flag u8 +0x10, next +0x18}, written
// when a script sets a player var, so its keys are varp ids that changed in the last 500 ms. The
// health row data.varstamps keeps its layout under watch.
struct VarStamp { int id; long long stamp; int flag; };
static bool read_var_stamps(HANDLE h, std::uint64_t root, std::vector<VarStamp>& out, std::uint32_t* bucketCount = nullptr) {
    out.clear();
    const std::uint64_t mgr = root + kOffVarpMgr;
    const std::uint64_t buckets = rpm<std::uint64_t>(h, mgr + 0x38178).value_or(0);
    const std::uint32_t cap = rpm<std::uint32_t>(h, mgr + 0x38180).value_or(0);
    if (bucketCount) *bucketCount = cap;
    if (buckets <= 0x10000 || buckets > 0x00007FFFFFFFFFFFull || cap == 0 || cap > 100000) return false;
    std::vector<std::uint64_t> arr((std::size_t)cap);
    if (!rpm_bytes(h, buckets, arr.data(), (std::size_t)cap * 8)) return false;
    for (std::uint32_t i = 0; i < cap && out.size() < 256; ++i) {
        std::uint64_t node = arr[i];
        for (int guard = 0; node > 0x10000 && guard < 64; ++guard) {
            std::uint8_t nb[0x20];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
            VarStamp s; std::memcpy(&s.id, nb, 4); std::memcpy(&s.stamp, nb + 8, 8); s.flag = nb[0x10];
            out.push_back(s);
            std::memcpy(&node, nb + 0x18, 8);
        }
    }
    return true;
}
// A digest of the stamp map (ids, stamps, flags): it moves whenever a script sets a player var,
// which the var change counter does not cover. -1 when the map is unreadable.
static long long read_var_stamp_digest(HANDLE h, std::uint64_t root) {
    std::vector<VarStamp> stamps;
    if (!read_var_stamps(h, root, stamps)) return -1;
    unsigned long long d = 1469598103934665603ull + stamps.size();
    for (const auto& s : stamps) { d ^= (unsigned long long)(std::uint32_t)s.id * 1000003ull + (unsigned long long)s.stamp * 31ull + (unsigned long long)s.flag; d *= 1099511628211ull; }
    return (long long)(d >> 1);
}

// Every set varp from the MainData+0x36080 hashmap, keyed "4:<id>" (scope 4 = varp). Direct poll stays.
// The walk is skipped while neither the var change counter (server updates) nor the stamp map
// (script sets) has moved since the last one, for at most 3 s; both must be readable for the skip.
std::string VarpsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    struct Gate { long long seq = -1, stamps = -1; unsigned long long ms = 0; std::string json; };
    static std::mutex s_gateMu;
    static std::unordered_map<std::uint32_t, Gate> s_gate;
    const long long seq = read_var_seq(h, *root);
    const long long stamps = seq >= 0 ? read_var_stamp_digest(h, *root) : -1;
    const unsigned long long now = GetTickCount64();
    if (seq >= 0 && stamps >= 0) {
        std::lock_guard<std::mutex> lk(s_gateMu);
        auto it = s_gate.find(pid);
        if (it != s_gate.end() && it->second.seq == seq && it->second.stamps == stamps && now - it->second.ms < 3000 && !it->second.json.empty()) return it->second.json;
    }
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
    if (seq >= 0 && stamps >= 0) {
        std::lock_guard<std::mutex> lk(s_gateMu);
        if (s_gate.size() > 16) s_gate.clear();
        Gate& g = s_gate[pid]; g.seq = seq; g.stamps = stamps; g.ms = now; g.json = out;
    }
    return out;
}

// EASTL hashtable header, read in one go: buckets +8, bucket count +0x10, element count +0x18. The
// varc store and the sub-interface table rehash when they grow, so a bucket array read beside a
// count read a moment later can pair a stale array with a new count; walkers read the header, walk,
// and drop the walk when the header moved. (The varp map never rehashes: its array is inline.)
struct HashHdr { std::uint64_t buckets = 0; std::int64_t count = 0, elements = 0; };
static bool read_hash_hdr(HANDLE h, std::uint64_t map, HashHdr& out, std::int64_t maxCount = 131072) {
    std::uint8_t b[0x18];
    if (!rpm_bytes(h, map + 8, b, sizeof(b))) return false;
    std::memcpy(&out.buckets, b, 8); std::memcpy(&out.count, b + 8, 8); std::memcpy(&out.elements, b + 16, 8);
    return out.buckets > 0x10000 && out.buckets <= 0x00007FFFFFFFFFFFull && out.count > 0 && out.count <= maxCount && out.elements >= 0;
}
static bool hash_hdr_same(HANDLE h, std::uint64_t map, const HashHdr& was) {
    HashHdr now;
    return read_hash_hdr(h, map, now) && now.buckets == was.buckets && now.count == was.count;
}
// The bucket array of a hashtable whose header was read: empty when unreadable.
static std::vector<std::uint64_t> read_hash_buckets(HANDLE h, const HashHdr& hdr) {
    std::vector<std::uint64_t> buckets((std::size_t)hdr.count);
    if (!rpm_bytes(h, hdr.buckets, buckets.data(), (std::size_t)hdr.count * 8)) buckets.clear();
    return buckets;
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
    std::string out;
    for (int attempt = 0; attempt < 2; ++attempt) {
        HashHdr hdr;
        if (!read_hash_hdr(h, hashRoot, hdr)) return "{}";   // real bucket counts are a few thousand
        const std::vector<std::uint64_t> buckets = read_hash_buckets(h, hdr);
        if (buckets.empty()) return "{}";
        out = "{"; out.reserve(1u << 16); bool first = true; char buf[64];
        for (std::size_t b = 0; b < buckets.size(); ++b) {
            std::uint64_t node = buckets[b];
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
        if (hash_hdr_same(h, hashRoot, hdr)) break;   // else the store rehashed under the walk: once more
    }
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

static void append_json_str(std::string& out, const std::string& s);

static void dom_dump(HANDLE h, std::uint64_t table, int domain, std::string& out, bool& first) {
    int div = 0, count = 0;
    if (!dom_table(h, table, div, count)) return;
    std::uint64_t ba = rpm<std::uint64_t>(h, table + 0x10).value_or(0);
    std::vector<std::uint64_t> buckets((std::size_t)div);
    if (!rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) return;
    char buf[96]; std::string sval;
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
                else if (type == 2) {   // string value: the EASTL string sits in the node's value union
                    std::snprintf(buf, sizeof(buf), "%s\"%d:%d\":\"", first ? "" : ",", domain, id);
                    out += buf; first = false;
                    if (read_eastl_string(h, node + 8, sval)) append_json_str(out, sval);
                    out += "\"";
                    node = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
                    continue;
                }
                else                std::snprintf(buf, sizeof(buf), "%s\"%d:%d\":\"(type %d)\"", first ? "" : ",", domain, id, type);
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
    std::uint64_t pinnedVt = 0;   // the class vtable the pins file records for this exe
    if (ps.mod_base) {
        const std::int32_t lfanew = rpm<std::int32_t>(h, ps.mod_base + 0x3C).value_or(0);
        const std::uint32_t stamp = (lfanew > 0 && lfanew < 0x1000) ? rpm<std::uint32_t>(h, ps.mod_base + (std::uint64_t)lfanew + 8).value_or(0) : 0;
        if (const std::uint32_t rva = stamp ? rtx::pins::PinnedRva("vartableVt", stamp) : 0) pinnedVt = ps.mod_base + rva;
    }
    // The store class vtable sits at a different RVA in each rs2client.exe build (OpenGL and Vulkan
    // are separate binaries), so beyond the pinned RVA accept any vtable that lies in the image and
    // whose first slots are code pointers into the image. No RTTI is present to name the class.
    auto in_image = [&](std::uint64_t a) {
        return ps.mod_base && ps.mod_size && a >= ps.mod_base && a < ps.mod_base + ps.mod_size;
    };
    auto vt_ok = [&](std::uint64_t vt) {
        if (kTableVt && vt == kTableVt) return true;
        if (pinnedVt && vt == pinnedVt) return true;
        if (!in_image(vt)) return false;
        for (int i = 0; i < 5; ++i)
            if (!in_image(rpm<std::uint64_t>(h, vt + (std::uint64_t)i * 8).value_or(0))) return false;
        return true;
    };
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    std::uint64_t clan  = store > 0x10000 ? rpm<std::uint64_t>(h, store + 0x77b0).value_or(0) : 0;
    std::uint64_t grp   = rpm<std::uint64_t>(h, *root + kOffPlayerGroup).value_or(0);
    std::uint64_t grpObj = grp > 0x10000 ? rpm<std::uint64_t>(h, grp + 8).value_or(0) : 0;
    // Clan settings (domain 7): the clan manager holds the listened settings store as a counted
    // reference at +0x60 (counter) / +0x68 (object); the object's var map sits at +0x98 (buckets
    // +0xA0, count +0xA8), so the table header the walker reads starts at object + 0x90. Null
    // outside a clan.
    std::uint64_t clanMgr = rpm<std::uint64_t>(h, *root + kOffClanSettings).value_or(0);
    std::uint64_t csCounter = clanMgr > 0x10000 ? rpm<std::uint64_t>(h, clanMgr + 0x60).value_or(0) : 0;
    std::uint64_t csObj = (clanMgr > 0x10000 && csCounter > 0x10000) ? rpm<std::uint64_t>(h, clanMgr + 0x68).value_or(0) : 0;

    std::string out = "{\"stores\":{"; char buf[256]; bool firstS = true;
    auto emit = [&](const char* key, const char* src, std::uint64_t obj, std::uint64_t table) {
        int div = 0, count = 0; bool ok = obj > 0x10000 && dom_table(h, table, div, count);
        std::uint64_t vt = obj > 0x10000 ? rpm<std::uint64_t>(h, table).value_or(0) : 0;
        std::snprintf(buf, sizeof(buf), "%s\"%s\":{\"src\":\"%s\",\"ptr\":\"0x%llx\",\"live\":%s,\"div\":%d,\"count\":%d,\"vt\":%s,\"vt_rva\":\"0x%llx\"}",
                      firstS ? "" : ",", key, src, (unsigned long long)obj, ok ? "true" : "false", ok ? div : 0, ok ? count : 0,
                      vt_ok(vt) ? "true" : "false", (unsigned long long)(in_image(vt) ? vt - ps.mod_base : 0));
        out += buf; firstS = false;
    };
    emit("0",  "MainData+0x19fb8 (varp manager)", *root + kOffVarpMgr, *root + kOffVarpHash - 8);
    emit("2",  "[MainData+0x19920]+0x7620 (varc object)", store > 0x10000 ? store + 0x7620 : 0, store + 0x7620 + 8);
    emit("6",  "[[MainData+0x19920]+0x77b0]", clan, clan);
    emit("7",  "[[MainData+0x19888]+0x68] (listened clan settings; map at +0x98)", csObj, csObj > 0x10000 ? csObj + 0x90 : 0);
    emit("9",  "[[MainData+0x19948]+8]+0x28", grpObj > 0x10000 ? grpObj + 0x28 : 0, grpObj + 0x28);
    out += "},\"vars\":{"; bool first = true;
    if (clan > 0x10000) dom_dump(h, clan, 6, out, first);
    if (csObj > 0x10000) dom_dump(h, csObj + 0x90, 7, out, first);
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
    std::string out;
    for (int attempt = 0; attempt < 2; ++attempt) {
        HashHdr hdr;
        if (!read_hash_hdr(h, hashRoot, hdr)) return "{}";
        const std::vector<std::uint64_t> buckets = read_hash_buckets(h, hdr);
        if (buckets.empty()) return "{}";
        out = "{"; bool first = true; char buf[80];
        for (std::size_t b = 0; b < buckets.size(); ++b) {
            std::uint64_t node = buckets[b];
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
        if (hash_hdr_same(h, hashRoot, hdr)) break;
    }
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
    std::string out;
    for (int attempt = 0; attempt < 2; ++attempt) {
        HashHdr hdr;
        if (!read_hash_hdr(h, hashRoot, hdr)) return "{}";
        const std::vector<std::uint64_t> buckets = read_hash_buckets(h, hdr);
        if (buckets.empty()) return "{}";
        out = "{"; bool first = true; char buf[48];
        for (std::size_t b = 0; b < buckets.size(); ++b) {
            std::uint64_t node = buckets[b];
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
        if (hash_hdr_same(h, hashRoot, hdr)) break;
    }
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

// All varc-strings from the global client-var hashmap (store+0x7630), keyed "2:<id>". Only nodes whose
// type byte (+0x20) says string (2) are read: an int node's union bytes would pass as a short string.
std::string VarcStringsDumpAllJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    std::uint64_t store = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    if (store <= 0x10000) return "{}";
    std::uint64_t hashRoot = store + kVarcHashOff;
    std::string out;
    for (int attempt = 0; attempt < 2; ++attempt) {
        HashHdr hdr;
        if (!read_hash_hdr(h, hashRoot, hdr)) return "{}";
        const std::vector<std::uint64_t> buckets = read_hash_buckets(h, hdr);
        if (buckets.empty()) return "{}";
        out = "{"; out.reserve(1u << 14); bool first = true; std::string val;
        for (std::size_t b = 0; b < buckets.size(); ++b) {
            std::uint64_t node = buckets[b];
            for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
                std::uint8_t nb[0x30];
                if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
                const int id = *reinterpret_cast<const std::int32_t*>(nb);
                const std::uint64_t next = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
                if (id >= 0 && id < 100000 && nb[0x20] == 2 && read_eastl_string(h, node + 0x8, val) && !val.empty()) {
                    out += first ? "\"2:" : ",\"2:"; first = false;
                    out += std::to_string(id); out += "\":\"";
                    append_json_str(out, val);
                    out += "\"";
                }
                node = next;
            }
        }
        out += "}";
        if (hash_hdr_same(h, hashRoot, hdr)) break;
    }
    return out;
}

std::string VarsDumpJson(std::uint32_t pid) {
    wchar_t name[rtx::ipc::kNameChars];
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
    wchar_t name[rtx::ipc::kNameChars];
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

// The scan above reads the whole image, and the panel behind it refreshes several times a second.
// The table is a module global, so a table the scan found is kept per client and module (and
// checked again on use); a miss is kept for a few seconds before the image is scanned again.
struct OpTableMemo { std::uint64_t base = 0, size = 0, tbl = 0, at = 0; };
std::mutex g_optable_mu;
std::unordered_map<std::uint32_t, OpTableMemo> g_optable;

// The table global for this exe as the pins file records it (per PE time stamp), 0 when none.
std::uint64_t pinned_optable_rva(HANDLE h, std::uint64_t base) {
    const std::int32_t lfanew = rpm<std::int32_t>(h, base + 0x3C).value_or(0);
    if (lfanew <= 0 || lfanew > 0x1000) return 0;
    const std::uint32_t stamp = rpm<std::uint32_t>(h, base + (std::uint64_t)lfanew + 8).value_or(0);
    return stamp ? rtx::pins::PinnedRva("optable", stamp) : 0;
}

std::uint64_t cached_optable(std::uint32_t pid, HANDLE h, std::uint64_t base, std::uint64_t size) {
    if (auto t = rpm<std::uint64_t>(h, base + kOpTableRva); t && optable_valid(h, *t))
        return *t;
    if (const std::uint64_t pin = pinned_optable_rva(h, base))
        if (auto t = rpm<std::uint64_t>(h, base + pin); t && optable_valid(h, *t)) return *t;
    OpTableMemo m;
    { std::lock_guard<std::mutex> lk(g_optable_mu);
      auto it = g_optable.find(pid);
      if (it != g_optable.end()) m = it->second; }
    const std::uint64_t now = GetTickCount64();
    if (m.base == base && m.size == size) {
        if (m.tbl && optable_valid(h, m.tbl)) return m.tbl;
        if (!m.tbl && now - m.at < 10000) return 0;
    }
    const std::uint64_t tbl = resolve_optable(h, base, size);
    std::lock_guard<std::mutex> lk(g_optable_mu);
    g_optable[pid] = { base, size, tbl, now };
    return tbl;
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

    std::uint64_t tbl = cached_optable(pid, h, base, size);
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
    wchar_t name[rtx::ipc::kNameChars];
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
    wchar_t name[rtx::ipc::kNameChars];
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

// Tracker group definitions as the client holds them: [MainData+kOffTracker] -> {begin, end} of
// 0x68-byte groups {id i32 +0, column ids +8/+0x10, row ids +0x20/+0x28}. tracker_values carries
// indexes into these lists; the decode adds the ids when a copy is at hand.
struct TrackerDef { int id = -1; std::vector<int> cols, rows; };
struct TrackerDefs { std::vector<TrackerDef> groups; std::uint64_t at = 0; bool ok = false; };
TrackerDefs g_trackerDefs;   // EventsJson's copy, refreshed when a batch holds tracker records

bool read_tracker_defs(HANDLE h, std::uint64_t root, TrackerDefs& out) {
    out.groups.clear(); out.ok = false;
    const std::uint64_t mgr = rpm<std::uint64_t>(h, root + kOffTracker).value_or(0);
    if (mgr <= 0x10000) return false;
    const std::uint64_t gb = rpm<std::uint64_t>(h, mgr).value_or(0), ge = rpm<std::uint64_t>(h, mgr + 8).value_or(0);
    if (gb <= 0x10000 || ge < gb || (ge - gb) % kTrackerGroupSize || (ge - gb) / kTrackerGroupSize > 32) return false;
    auto ids = [&](std::uint64_t b, std::uint64_t e, std::vector<int>& v) {
        v.clear();
        if (b <= 0x10000 || e < b || (e - b) % 4 || (e - b) / 4 > 256) return;
        v.resize((std::size_t)((e - b) / 4));
        if (!v.empty() && !rpm_bytes(h, b, v.data(), v.size() * 4)) v.clear();
    };
    for (std::uint64_t g = gb; g < ge; g += kTrackerGroupSize) {
        std::uint8_t b[0x30];
        if (!rpm_bytes(h, g, b, sizeof(b))) return false;
        TrackerDef d; d.id = *reinterpret_cast<const std::int32_t*>(b);
        ids(*reinterpret_cast<const std::uint64_t*>(b + 0x08), *reinterpret_cast<const std::uint64_t*>(b + 0x10), d.cols);
        ids(*reinterpret_cast<const std::uint64_t*>(b + 0x20), *reinterpret_cast<const std::uint64_t*>(b + 0x28), d.rows);
        out.groups.push_back(std::move(d));
    }
    out.ok = true; return true;
}
// ,"groupId":..,"rowId":..,"columnId":.. for a (group slot, row index, column index) the definitions resolve; -1 index = not carried.
void ev_tracker_ids(std::string& o, const TrackerDefs* trk, int g, int r, int c) {
    if (!trk || g < 0 || g >= (int)trk->groups.size()) return;
    const TrackerDef& d = trk->groups[(std::size_t)g];
    char t[96];
    std::snprintf(t, sizeof(t), ",\"groupId\":%d", d.id); o += t;
    if (r >= 0 && r < (int)d.rows.size()) { std::snprintf(t, sizeof(t), ",\"rowId\":%d", d.rows[(std::size_t)r]); o += t; }
    if (c >= 0 && c < (int)d.cols.size()) { std::snprintf(t, sizeof(t), ",\"columnId\":%d", d.cols[(std::size_t)c]); o += t; }
}

bool ev_decode(std::string& o, int op, const std::uint8_t* b, std::uint32_t n, int len, const TrackerDefs* trk = nullptr) {
    char t[128];
    {   // zone item bodies; a body without a decoder is tagged with its sub id instead of raw
        const int sub = rtx::evzone::subForOpcode(op);
        if (sub >= 0) {
            if (rtx::evzone::subJson(o, sub, b, n)) return true;
            std::snprintf(t, sizeof(t), "\"kind\":\"zone_sub\",\"sub\":%d,\"hex\":\"", sub); o += t; ev_hex(o, b, n); o += "\""; return true;
        }
    }
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
    case rtx::sops::kRunClientScript: {   // exe+0xF7460: [sig NUL-terminated][args in REVERSE sig order: s = NUL string, l = i64 BE, any other letter = i32 BE][scriptId: i32 BE]
        const bool cut = len > (int)n;
        std::uint32_t p = 0; std::string sig;
        while (p < n && b[p] != 0 && sig.size() < 16) sig.push_back((char)b[p++]);
        if (p >= n) return false;                              // no NUL terminator: raw
        for (char c : sig) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
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
    case rtx::sops::kVarcInt: {    // [id: (b1-0x80)&0xFF | b0<<8][value: b3 b2 b5 b4]  (exe+0x141E90)
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
    case rtx::sops::kVarpLong: {   // [i64: hi = b1 b0 b3 b2, lo = b5 b4 b7 b6][id: u16 BE]  (exe+0x142390)
        if (n < 10) return false;
        const std::uint32_t hi = ((std::uint32_t)b[1] << 24) | ((std::uint32_t)b[0] << 16) | ((std::uint32_t)b[3] << 8) | b[2];
        const std::uint32_t lo = ((std::uint32_t)b[5] << 24) | ((std::uint32_t)b[4] << 16) | ((std::uint32_t)b[7] << 8) | b[6];
        const long long v = (long long)(((std::uint64_t)hi << 32) | lo);
        std::snprintf(t, sizeof(t), "\"kind\":\"varp_set\",\"id\":%d,\"value\":%lld,\"long\":true", (b[8] << 8) | b[9], v);
        o += t; return true;
    }
    case rtx::sops::kVarbitVarint: {   // two LEB128 varints: varbit id, value  (exe+0x1420D0)
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
    case rtx::sops::kGeOffer: {    // [market u8][slot u8][header u8: 0 clears the slot, else status = h & 7, buy/sell bit 3; status 7 = extended form follows]
        // extended form: [w u8][status/type u8] then item (w >= 3: u24 BE, else u16 BE), price (w >= 2: i64 BE, else u32 BE),
        // count i32 BE, completed count i32 BE, completed gold (w >= 2: i64 BE, else u32 BE), then (w >= 2) a u32 BE extension length the client skips.
        // The slot lands in the offer array at [MainData+0x19990]+0x10 + (market * 8 + slot) * 0x28.
        if (n < 3) return false;
        const int market = b[0], slot = b[1], hdr = b[2];
        if (market > 1 || slot >= kGESlotCount) return false;
        if (hdr == 0) {
            std::snprintf(t, sizeof(t), "\"kind\":\"ge_offer\",\"market\":%d,\"slot\":%d,\"status\":0,\"type\":-1,\"item\":-1,\"cleared\":true", market, slot);
            o += t; return true;
        }
        int status = hdr & 7, type = (hdr >> 3) & 1;
        if (status != 7) {   // plain form: the fields after the header are not decoded here
            std::snprintf(t, sizeof(t), "\"kind\":\"ge_offer\",\"market\":%d,\"slot\":%d,\"status\":%d,\"type\":%d,\"extended\":false", market, slot, status, type);
            o += t; return true;
        }
        if (n < 5) return false;
        const int w = b[3];
        status = b[4] & 7; type = (b[4] >> 3) & 1;
        std::uint32_t p = 5;
        long long item, price, gold; int count, completed;
        if (w >= 3) { if (p + 3 > n) return false; item = ((long long)b[p] << 16) | ((long long)b[p + 1] << 8) | b[p + 2]; p += 3; }
        else        { if (p + 2 > n) return false; item = ((long long)b[p] << 8) | b[p + 1]; p += 2; }
        if (w >= 2) { if (p + 8 > n) return false; price = (long long)(((std::uint64_t)ev_u32be(b + p) << 32) | ev_u32be(b + p + 4)); p += 8; }
        else        { if (p + 4 > n) return false; price = ev_u32be(b + p); p += 4; }
        if (p + 8 > n) return false;
        count = (int)ev_u32be(b + p); completed = (int)ev_u32be(b + p + 4); p += 8;
        if (w >= 2) { if (p + 8 > n) return false; gold = (long long)(((std::uint64_t)ev_u32be(b + p) << 32) | ev_u32be(b + p + 4)); p += 8; }
        else        { if (p + 4 > n) return false; gold = ev_u32be(b + p); p += 4; }
        char g[256];
        std::snprintf(g, sizeof(g), "\"kind\":\"ge_offer\",\"market\":%d,\"slot\":%d,\"status\":%d,\"type\":%d,\"item\":%lld,\"price\":%lld,\"qty\":%d,\"filled\":%d,\"filledValue\":%lld,\"w\":%d",
                      market, slot, status, type, item, price, count, completed, gold, w);
        o += g; return true;
    }
    case rtx::sops::kVarbitByte: {     // [value: (b0+0x80)&0xFF][varbit id: u16 LE at 1]
        if (n < 3) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"varbit_set\",\"id\":%d,\"value\":%d", b[1] | (b[2] << 8), (b[0] + 0x80) & 0xFF);
        o += t; return true;
    }
    case rtx::sops::kVarbitInt: {      // [value: i32 BE][varbit id: u16 LE at 4]
        if (n < 6) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"varbit_set\",\"id\":%d,\"value\":%d", b[4] | (b[5] << 8), (int)(std::int32_t)ev_u32be(b));
        o += t; return true;
    }
    case rtx::sops::kVarcLong: {       // [hi: u32 LE][lo: u32 LE][varc id: u16 LE at 8]
        if (n < 10) return false;
        const std::uint32_t hi = (std::uint32_t)b[0] | ((std::uint32_t)b[1] << 8) | ((std::uint32_t)b[2] << 16) | ((std::uint32_t)b[3] << 24);
        const std::uint32_t lo = (std::uint32_t)b[4] | ((std::uint32_t)b[5] << 8) | ((std::uint32_t)b[6] << 16) | ((std::uint32_t)b[7] << 24);
        std::snprintf(t, sizeof(t), "\"kind\":\"varc_set\",\"id\":%d,\"value\":%lld,\"long\":true", b[8] | (b[9] << 8), (long long)(((std::uint64_t)hi << 32) | lo));
        o += t; return true;
    }
    case rtx::sops::kContainerFull: {  // [container: u16 BE][flags: u8][count: u16 BE] then count x [itemId+1: u24 BE][qty u8 | 0xFF u32 BE][variant if flags&2]; slot = running index; flags&1 = another player's
        if (n < 5) return false;
        const int cont = (b[0] << 8) | b[1], flags = b[2], count = (b[3] << 8) | b[4];
        std::uint32_t p = 5;
        std::snprintf(t, sizeof(t), "\"kind\":\"container_full\",\"container\":%d,\"flags\":%d,\"other\":%s,\"count\":%d,\"slots\":[", cont, flags, (flags & 1) ? "true" : "false", count);
        o += t;
        bool first = true, partial = false; int emitted = 0;
        for (int slot = 0; slot < count; ++slot) {
            if (p + 3 > n) { partial = true; break; }
            const std::uint32_t item1 = ((std::uint32_t)b[p] << 16) | ((std::uint32_t)b[p + 1] << 8) | b[p + 2]; p += 3;
            if (item1 == 0) continue;
            if (p >= n) { partial = true; break; }
            std::uint32_t qty = b[p++];
            if (qty == 0xFF) { if (p + 4 > n) { partial = true; break; } qty = ev_u32be(b + p); p += 4; }
            if (flags & 2) { if (p < n) p++; }
            if (emitted < 512) { std::snprintf(t, sizeof(t), "%s{\"slot\":%d,\"item\":%d,\"qty\":%u}", first ? "" : ",", slot, (int)item1 - 1, qty); o += t; first = false; ++emitted; }
        }
        o += "],\"partial\":"; o += (partial || (std::uint32_t)len > n) ? "true" : "false";
        return true;
    }
    case rtx::sops::kContainerReset: { // [other: (-b0)&1][container: ((b2-0x80)&0xFF) | b1<<8]
        if (n < 3) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"container_reset\",\"container\":%d,\"other\":%s", ((b[2] - 0x80) & 0xFF) | (b[1] << 8), ((0 - b[0]) & 1) ? "true" : "false");
        o += t; return true;
    }
    case rtx::sops::kTrackerGroup: {   // [group id: b2 b3 b0 b1][slot: (int8)(b4+0x80)]
        if (n < 5) return false;
        const std::uint32_t id = ((std::uint32_t)b[2] << 24) | ((std::uint32_t)b[3] << 16) | ((std::uint32_t)b[0] << 8) | b[1];
        std::snprintf(t, sizeof(t), "\"kind\":\"tracker_group\",\"groupId\":%d,\"slot\":%d", (int)id, (int)(std::int8_t)(b[4] + 0x80));
        o += t; return true;
    }
    case rtx::sops::kTrackerValues: {  // { group i8 (0xFF ends) { row i8 (0xFF ends) { column i8 (0xFF ends), value i32 BE } } }; INT32_MIN = no value
        std::uint32_t p = 0; bool first = true, partial = false; int cells = 0;
        o += "\"kind\":\"tracker_values\",\"cells\":[";
        while (p < n && !partial) {
            const int g = b[p++]; if (g == 0xFF) break;
            while (p < n && !partial) {
                const int r = b[p++]; if (r == 0xFF) break;
                while (p < n) {
                    const int c = b[p++]; if (c == 0xFF) break;
                    if (p + 4 > n) { partial = true; break; }
                    const std::int32_t v = (std::int32_t)ev_u32be(b + p); p += 4;
                    if (++cells > 1024) { partial = true; break; }
                    std::snprintf(t, sizeof(t), "%s{\"group\":%d,\"row\":%d,\"column\":%d,\"value\":", first ? "" : ",", g, r, c); o += t; first = false;
                    if (v == INT32_MIN) o += "null"; else o += std::to_string(v);
                    ev_tracker_ids(o, trk, g, r, c); o += "}";
                }
            }
        }
        o += "],\"partial\":"; o += (partial || (std::uint32_t)len > n) ? "true" : "false";
        return true;
    }
    case rtx::sops::kTrackerRemove:    // [slot: (int8)(-b0)]
        if (n < 1) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"tracker_remove\",\"slot\":%d", (int)(std::int8_t)(0 - b[0]));
        o += t; ev_tracker_ids(o, trk, (int)(std::int8_t)(0 - b[0]), -1, -1); return true;
    case rtx::sops::kTrackerClear:     // [group b0][column: -b1][row b2]: the cell becomes INT32_MIN
        if (n < 3) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"tracker_clear\",\"group\":%d,\"row\":%d,\"column\":%d", b[0], b[2], (0 - b[1]) & 0xFF);
        o += t; ev_tracker_ids(o, trk, b[0], b[2], (0 - b[1]) & 0xFF); return true;
    case rtx::sops::kTrackerColumn:    // [group: 0x80-b0][column b1][shown: b2 == 0x81]
        if (n < 3) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"tracker_column\",\"group\":%d,\"column\":%d,\"shown\":%s", (0x80 - b[0]) & 0xFF, b[1], b[2] == 0x81 ? "true" : "false");
        o += t; ev_tracker_ids(o, trk, (0x80 - b[0]) & 0xFF, -1, b[1]); return true;
    case rtx::sops::kSystemUpdate:     // [seconds: u16 BE]
        if (n < 2) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"system_update\",\"seconds\":%u", (unsigned)((b[0] << 8) | b[1]));
        o += t; return true;
    case rtx::sops::kCameraTarget: {   // [packed tile: b1 b0 b3 b2]; 0xFFFFFFFF clears the target
        if (n < 4) return false;
        const std::uint32_t v = ((std::uint32_t)b[1] << 24) | ((std::uint32_t)b[0] << 16) | ((std::uint32_t)b[3] << 8) | b[2];
        o += "\"kind\":\"camera_target\","; rtx::evzone::packedJson(o, v);
        o += v == 0xFFFFFFFFu ? ",\"cleared\":true" : ",\"cleared\":false"; return true;
    }
    case rtx::sops::kCutscene:         // [cutscene id: u16 BE]
        if (n < 2) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"cutscene\",\"id\":%u", (unsigned)((b[0] << 8) | b[1]));
        o += t; return true;
    case rtx::sops::kFriendsLoaded:    // no payload
        o += "\"kind\":\"friends_loaded\""; return true;
    case rtx::sops::kPrivateFilter:    // [filter: u8]
        if (n < 1) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"private_filter\",\"value\":%u", b[0]);
        o += t; return true;
    case rtx::sops::kMinimapState:     // [state: u8]: mode = state % 3, shown when state < 3
        if (n < 1) return false;
        std::snprintf(t, sizeof(t), "\"kind\":\"minimap_state\",\"value\":%u,\"mode\":%u,\"shown\":%s", b[0], b[0] % 3, b[0] < 3 ? "true" : "false");
        o += t; return true;
    default:
        return false;
    }
}
}  // namespace

// One offer slot as the client keeps it: offers[2][8] of 0x28 from +0x10, market 1 (the second
// market) following market 0's eight slots.
static std::string ge_slot_json(std::uint32_t pid, int slot, int market = 0) {
    if (slot < 0 || slot >= kGESlotCount || market < 0 || market > 1) return "{}";
    auto ps = snap_proc(pid);
    if (!ps) return "{}";
    auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
    if (!root || *root <= 0x10000) return "{}";
    auto ge_box = rpm<std::uint64_t>(ps.h, *root + kOffGE);
    if (!ge_box || !*ge_box) return "{}";
    alignas(8) std::uint8_t b[kGESlotSize];
    if (!rpm_bytes(ps.h, *ge_box + kGEArrayPad + (std::uint64_t)(market * kGESlotCount + slot) * kGESlotSize, b, sizeof(b))) return "{}";
    char t[200];
    std::snprintf(t, sizeof(t),
        "{\"status\":%d,\"type\":%d,\"item\":%d,\"price\":%lld,\"qty\":%d,\"filled\":%d,\"filledValue\":%lld}",
        *(const std::int32_t*)(b + 0x00), *(const std::int32_t*)(b + 0x04), *(const std::int32_t*)(b + 0x08),
        (long long)*(const std::int64_t*)(b + 0x10), *(const std::int32_t*)(b + 0x18),
        *(const std::int32_t*)(b + 0x1C), (long long)*(const std::int64_t*)(b + 0x20));
    return t;
}

std::string EventsJson(std::uint32_t pid, std::uint64_t since) {
    wchar_t name[rtx::ipc::kNameChars];
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
                auto mgr = root && *root > 0x10000 ? rpm<std::uint64_t>(ps.h, *root + kOffMapMgr) : std::nullopt;
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
        const TrackerDefs* trk = nullptr;
        {   // tracker group definitions, read only when the batch holds tracker records, at most every 2 s
            // unless the batch adds or removes a group (the opcode peek needs no seqlock: a stale value only
            // costs a refresh)
            bool any = false, changed = false;
            for (std::uint64_t i = from; i < written; ++i) {
                const int op = sh->recs[i % rtx::events::kMaxRecords].opcode;
                if (op == rtx::sops::kTrackerValues || op == rtx::sops::kTrackerClear || op == rtx::sops::kTrackerColumn) any = true;
                else if (op == rtx::sops::kTrackerGroup || op == rtx::sops::kTrackerRemove) { any = true; changed = true; }
            }
            if (any) {
                const std::uint64_t now = GetTickCount64();
                if (changed || !g_trackerDefs.ok || now - g_trackerDefs.at > 2000) {
                    ProcSnap ps = snap_proc(pid);
                    auto root = ps ? rpm<std::uint64_t>(ps.h, ps.mgva) : std::nullopt;
                    if (root && *root > 0x10000) read_tracker_defs(ps.h, *root, g_trackerDefs);
                    g_trackerDefs.at = now;
                }
                if (g_trackerDefs.ok) trk = &g_trackerDefs;
            }
        }
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
            if (!ev_decode(out, r.opcode, r.payload, n, r.length, trk)) {
                out.resize(mark);
                out += "\"kind\":\"raw\",\"hex\":\"";
                ev_hex(out, r.payload, n);
                out += "\"";
            } else if (r.opcode == rtx::sops::kGeOffer && n >= 2 && r.payload[0] <= 1 && r.payload[1] < kGESlotCount) {
                // the slot as the client holds it after the packet, for a cross-check of the decode
                out += ",\"offer\":" + ge_slot_json(pid, (int)r.payload[1], (int)r.payload[0]) + ",\"hex\":\"";
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
    wchar_t name[rtx::ipc::kNameChars];
    rtx::events::MakeSectionName(pid, name);
    // The companion owns this section. Creating it here was never useful (it went away with the handle a
    // moment later) and let the name be taken first; with no companion there is nothing to mask.
    HANDLE h = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    auto* sh = reinterpret_cast<rtx::events::Share*>(
        MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(rtx::events::Share)));
    if (!sh) { CloseHandle(h); return false; }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(sh, &mbi, sizeof(mbi)) || mbi.RegionSize < sizeof(rtx::events::Share)) {   // a short section is not ours
        UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh)); CloseHandle(h); return false;
    }
    for (int i = 0; i < 8; ++i) sh->mask[i] = mask[i];
    sh->mask[rtx::sops::kMessageGame >> 5] &= ~(1u << (rtx::sops::kMessageGame & 31));   // never message_game
    MemoryBarrier();
    sh->maskSet = 1;
    UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh));
    CloseHandle(h);
    return true;
}

// which: 0 hide NPCs, 1 hide other players, 2 hide scene, 3 keep-focused, 4 true-embed.
std::string GpuTimingJson(std::uint32_t pid) {
    const char* kEmpty = "{\"passes\":[]}";
    wchar_t name[rtx::ipc::kNameChars];
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
    wchar_t name[rtx::ipc::kNameChars];
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
    wchar_t name[rtx::ipc::kNameChars];
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
    bool hidden;              // the game draws nothing for the loc (depleted tree, dormant stump, multiloc with no child)
    float bmin[3], bmax[3];   // live model world AABB (east,north,up); bmax.x==bmin.x = none
};

bool ReadRuntimeObjects(std::uint32_t pid, std::vector<RuntimeObj>& out) {
    out.clear();
    wchar_t name[rtx::ipc::kNameChars];
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
    wchar_t name[rtx::ipc::kNameChars];
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
    wchar_t name[rtx::ipc::kNameChars];
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
            int v = 0;                                            // unreadable map -> value stays -1 (static fallback)
            if (read_varbit_live(h, root, vb, v)) value = v;
        } else if (vp >= 0) {
            int v = 0;
            if (read_varp_default(h, root, vp, v)) value = v;     // selector is a varp directly (unset = 0, as the engine reads it)
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
        bool known = false;                                       // the selector var was read
        if (vb >= 0) {                                            // varbit takes priority (engine order)
            int v = 0;
            if (read_varbit_live(h, root, vb, v)) { value = v; known = true; }
        } else if (vp >= 0) {
            int v = 0;
            if (read_varp_default(h, root, vp, v)) { value = v; known = true; }   // selector is a varp directly (unset = 0)
        }
        // Unreadable selector: the live variant is unknown. No variant is guessed (a wrong one paints a
        // phantom) and the NPC is not hidden either, so callers keep the name the game put on the actor.
        if (!known) return {};
        // As the game picks: the value's own variant when in range, else the default child; -1 is hidden.
        int child = (value >= 0 && value < (int)variants.size()) ? variants[value] : defc;
        if (child < 0) { if (out_hidden) *out_hidden = true; return {}; }
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

// Overhead object of an actor: hitsplat ring and one head bar. splats = JSON array of
// [hitmark, value, startCycle, durationCycles] for records that hold a hit; the ring stores the EXPIRY
// cycle, start = expiry - duration (an expired record stays until reused, so consumers key on
// start+value). bar = fill 0..255 of head-bar slot `barSlot`, -1 when none: NPCs draw health in slot 0,
// the local player draws adrenaline in slot 0 and health in slot 1.
static void actor_overhead_json(HANDLE h, std::uint64_t sec, std::string& splats, int& bar, int barSlot = 0) {
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
                std::snprintf(b, sizeof(b), "%s[%d,%d,%d,%d]", first ? "" : ",", r[0], r[1], r[2] - r[5], r[5]);
                out += b; first = false;
            }
            out += "]"; splats = out;
        }
    }
    auto slots = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvSlots);
    if (slots && *slots > 0x10000 && *slots < 0x00007FFFFFFFFFFFull) {
        std::uint64_t el = *slots;
        if (barSlot > 0) {
            auto se = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvSlots + 8);
            if (se && *se >= *slots + (std::uint64_t)(barSlot + 1) * rtx::scn::kBarStride) el += (std::uint64_t)barSlot * rtx::scn::kBarStride;
        }
        auto fill = rpm<std::int32_t>(h, el + rtx::scn::kBarFill);
        auto stamp = rpm<std::int32_t>(h, el + rtx::scn::kBarStamp);
        if (fill && stamp && *fill >= 0 && *fill <= 255 && *stamp > 0) bar = *fill;
    }
}

// The head bar's cycle stamp, the hitsplat values landed after that stamp (the bar follows the life
// points a few ticks later: 120 cycles seen live) and the start cycle of the newest hit on an actor;
// -1 when none. The game draws a bar for a while after its last update and leaves the fill behind.
constexpr int kBarFreshCycles = 500;    // 10 s of client cycles (50/s)
constexpr int kBarFollowCycles = 150;   // a hit is followed by a bar update well within this
static void actor_bar_state(HANDLE h, std::uint64_t sec, int cycleNow, int& stamp, int& sinceStamp, int& hitCycle) {
    stamp = -1; sinceStamp = 0; hitCycle = -1;
    auto hb = rpm<std::uint64_t>(h, sec + rtx::scn::kOverhead);
    if (!hb || *hb <= 0x10000 || *hb > 0x00007FFFFFFFFFFFull) return;
    auto slots = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvSlots);
    if (slots && *slots > 0x10000 && *slots < 0x00007FFFFFFFFFFFull) stamp = rpm<std::int32_t>(h, *slots + rtx::scn::kBarStamp).value_or(-1);
    auto ring = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvRing);
    if (!ring || *ring <= 0x10000 || *ring > 0x00007FFFFFFFFFFFull) return;
    std::int32_t rec[6 * 6];
    if (!rpm_bytes(h, *ring, rec, sizeof(rec))) return;
    for (int i = 0; i < 6; ++i) {
        const std::int32_t* r = rec + i * 6;
        if (r[0] < 0 || r[1] <= 0 || r[2] <= 0 || r[5] <= 0 || r[0] > 100000 || r[1] > 100000000) continue;
        const int start = r[2] - r[5];           // the ring stores the expiry cycle
        if (start > cycleNow) continue;
        if (start > hitCycle) hitCycle = start;
        if (stamp > 0 && start > stamp) sinceStamp += r[1];
    }
}

// Local player actor through the player registry: [[MD+0x19950]+0x10][idx]+0x38, idx = local uid.
// Validated by type and uid; 0 when the chain does not resolve (callers fall back to the scan).
static std::uint64_t local_player_sec_fast(HANDLE h, std::uint64_t root, int local_uid) {
    if (root <= 0x10000 || local_uid < 0 || local_uid >= 4096) return 0;
    auto reg = rpm<std::uint64_t>(h, root + kOffPlayers);
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
// Records are keyed on (ring slot, expiry cycle, value, hitmark) per actor and compared with the
// previous poll, which logs each hit exactly once even though expired records linger in the ring
// until the game overwrites them. The ring stores each record's EXPIRY cycle; the event carries the
// creation cycle (expiry - duration), the cycle of the server tick the hit landed in.
namespace {
struct CombatEvent {
    std::uint64_t seq; long long t;          // t = wall clock, ms since the Unix epoch
    int type;                                // 1 NPC, 2 player
    bool self;                               // the local player (a hit taken)
    int uid, id;                             // actor uid; NPC config id (-1 for players)
    std::string name;
    int x, y, plane;
    int hitmark, value, cycle, dur;          // ring record; cycle = creation (the ring's expiry - dur)
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
    const int clockNow = (int)rpm<std::uint32_t>(h, *root + kOffClientClock).value_or(0);   // CLIENTCLOCK, 50/s
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
        // An actor seen for the first time may carry old records: only the ones still live (expiry
        // ahead of the client clock) are hits that just landed.
        bool actorRead = false; CombatEvent base{};
        for (int s = 0; s < 6; ++s) {
            const std::int32_t* r = rec + s * 6;
            if (r[0] < 0 || r[1] < 0 || r[2] <= 0 || r[5] <= 0 || r[0] > 100000 || r[1] > 100000000) continue;
            std::uint64_t key = ((std::uint64_t)(std::uint32_t)r[2] << 32) ^ (std::uint64_t)(std::uint32_t)r[1]
                              ^ ((std::uint64_t)(std::uint32_t)r[0] << 52) ^ ((std::uint64_t)s << 60);
            keys.push_back(key);
            if (old && std::find(old->begin(), old->end(), key) != old->end()) continue;   // seen before
            if (!old && (clockNow <= 0 || r[2] <= clockNow)) continue;    // first sight: only records still live
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
            ev.t = now; ev.hitmark = r[0]; ev.value = r[1]; ev.cycle = r[2] - r[5]; ev.dur = r[5];
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
// Combat recorder reads. One pass reads every actor in the scene with one block read of the actor
// object (type, uid, name, tile, animation, target, NPC id, stats, visible level, overhead pointer),
// then the overhead header, the hitsplat ring and, when asked, the head bars. Watched vars are read
// through their map nodes: the node addresses are resolved once (again when the map grows or a node
// stops matching) and read in coalesced windows, so a pass costs a handful of reads however many
// vars it watches. Everything here is read only.
namespace {
constexpr std::uint64_t kCbSecRead   = 0x1380;   // actor object bytes covering every field read below
constexpr std::uint64_t kCbNameLen   = 0x98;
constexpr std::uint64_t kCbAnim      = 0xA90;
constexpr std::uint64_t kCbAnimCount = 0xAFC, kCbAnimCycle = 0xB00;   // animation start count and its CLIENTCLOCK cycle
constexpr std::uint64_t kCbTargetUid = 0x1B4;
constexpr std::uint64_t kCbTargetKind = 0x228;
constexpr std::uint64_t kCbNpcStats  = 0x1140, kCbNpcBase = 0x115C, kCbNpcVis = 0x1178;
constexpr std::uint64_t kCbVarNode   = 0x30;     // var map node: id +0, value +8, type +0x20, next +0x28
constexpr std::uint64_t kCbWindowGap = 0x8000, kCbWindowMax = 0x80000;

struct CombatVarDomain {
    std::vector<int> wanted;
    std::vector<std::pair<std::uint64_t, int>> nodes;          // (node address, id), sorted by address
    std::vector<std::pair<std::uint64_t, std::uint32_t>> windows;
    std::uint64_t elements = 0;                                // map element count at the last resolve
    unsigned long long resolvedAt = 0;
    bool dirty = true;
};
struct CombatEpEntry { std::uint64_t sec = 0; int type = -1; };
struct CombatReadState {
    std::unordered_map<std::uint64_t, CombatEpEntry> eps;      // entity node -> its actor object and type
    std::unordered_map<int, std::string> npcNames;             // NPC id -> cache name
    std::uint64_t localSec = 0;                                // the local player's object from the last pass
    CombatVarDomain vp, vc;
    std::uint32_t itemsTick = 0xFFFFFFFFu;                     // the game tick the gear was last read in
};
std::mutex g_cbMu;
std::unordered_map<std::uint32_t, CombatReadState> g_cb;

inline bool cb_heap(std::uint64_t p) { return p > 0x10000 && p < 0x00007FFFFFFFFFFFull; }

// Map header: buckets +8, bucket count +0x10, element count +0x18. Every wanted id's node is found by
// walking its bucket chain, from one read of the bucket array.
void cb_resolve(HANDLE h, std::uint64_t hdr, CombatVarDomain& d, int& reads, int& fails) {
    d.nodes.clear(); d.windows.clear(); d.dirty = false;
    std::uint8_t hb[0x20];
    ++reads;
    if (!rpm_bytes(h, hdr, hb, sizeof(hb))) { ++fails; return; }
    const std::uint64_t ba = *reinterpret_cast<const std::uint64_t*>(hb + 8);
    const std::uint64_t nb = *reinterpret_cast<const std::uint64_t*>(hb + 0x10);
    d.elements = *reinterpret_cast<const std::uint64_t*>(hb + 0x18);
    if (!cb_heap(ba) || nb == 0 || nb > 2000000) return;
    std::vector<std::uint64_t> buckets((std::size_t)nb);
    ++reads;
    if (!rpm_bytes(h, ba, buckets.data(), buckets.size() * 8)) { ++fails; return; }
    for (int id : d.wanted) {
        if (id < 0) continue;
        std::uint64_t node = buckets[(std::size_t)((std::uint64_t)id % nb)];
        for (int i = 0; i < 128 && cb_heap(node); ++i) {
            std::uint8_t nbuf[kCbVarNode];
            ++reads;
            if (!rpm_bytes(h, node, nbuf, sizeof(nbuf))) { ++fails; break; }
            if (*reinterpret_cast<const std::int32_t*>(nbuf) == id) { d.nodes.push_back({ node, id }); break; }
            std::memcpy(&node, nbuf + kVarNodeNext, 8);
        }
    }
    std::sort(d.nodes.begin(), d.nodes.end());
    for (const auto& n : d.nodes) {
        const std::uint64_t a = n.first;
        if (!d.windows.empty()) {
            auto& w = d.windows.back();
            const std::uint64_t prevEnd = w.first + w.second;
            if (a + kCbVarNode - w.first <= kCbWindowMax && a - prevEnd <= kCbWindowGap) { w.second = (std::uint32_t)(a + kCbVarNode - w.first); continue; }
        }
        d.windows.push_back({ a, (std::uint32_t)kCbVarNode });
    }
}

// (id, value) of every watched var whose node exists and holds an int. A window that fails to read
// falls back to its nodes one by one; a node whose id no longer matches schedules a resolve.
void cb_read_vars(HANDLE h, std::uint64_t hdr, CombatVarDomain& d, unsigned long long now,
                  std::vector<std::pair<int, int>>& out, int& reads, int& fails) {
    if (d.wanted.empty()) return;
    std::uint8_t hb[0x20];
    ++reads;
    if (rpm_bytes(h, hdr, hb, sizeof(hb))) {
        if (*reinterpret_cast<const std::uint64_t*>(hb + 0x18) != d.elements) d.dirty = true;
    } else ++fails;
    if (d.dirty && (d.resolvedAt == 0 || now - d.resolvedAt >= 2000)) { cb_resolve(h, hdr, d, reads, fails); d.resolvedAt = now; }
    std::vector<std::uint8_t> buf;
    std::size_t ni = 0;
    for (const auto& w : d.windows) {
        buf.resize(w.second);
        ++reads;
        const bool ok = rpm_bytes(h, w.first, buf.data(), w.second);
        if (!ok) ++fails;
        for (; ni < d.nodes.size() && d.nodes[ni].first + kCbVarNode <= w.first + w.second; ++ni) {
            const std::uint64_t a = d.nodes[ni].first; const int id = d.nodes[ni].second;
            std::uint8_t one[kCbVarNode]; const std::uint8_t* nb = nullptr;
            if (ok) nb = buf.data() + (a - w.first);
            else { ++reads; if (rpm_bytes(h, a, one, sizeof(one))) nb = one; else { ++fails; d.dirty = true; continue; } }
            if (*reinterpret_cast<const std::int32_t*>(nb) != id) { d.dirty = true; continue; }
            if (nb[0x20] != 0) continue;                                   // long or string typed: not a cycle or count
            out.push_back({ id, *reinterpret_cast<const std::int32_t*>(nb + 8) });
        }
    }
}

// The live name: the string's length field when plausible, else the buffer up to its NUL (the read the
// combat log uses).
void cb_name(const std::uint8_t* b, std::uint64_t len, std::string& out) {
    out.clear();
    if (len == 0 || len > 48) len = 40;
    for (std::uint64_t j = 0; j < len; ++j) {
        const unsigned char c = b[j];
        if (c == 0) break;
        if (c >= 0x20 && c <= 0x7e) out.push_back((char)c);
        else if (c == 0xA0) out.push_back(' ');
    }
}

struct CombatCtx { HANDLE h; std::uint64_t root; CombatReadState& st; CombatSample& out; };

// One actor from its object: false when the block is not an actor's (type byte) or has no plausible tile.
// The head bars are read when `bars` asks (every other pass) and always for the local player.
bool cb_read_actor(CombatCtx& c, std::uint64_t sec, bool bars, CombatActorSample& a) {
    std::uint8_t b[kCbSecRead];
    ++c.out.reads;
    if (!rpm_bytes(c.h, sec, b, sizeof(b))) { ++c.out.fails; return false; }
    auto i32 = [&](std::uint64_t o) { return *reinterpret_cast<const std::int32_t*>(b + o); };
    auto u64 = [&](std::uint64_t o) { return *reinterpret_cast<const std::uint64_t*>(b + o); };
    auto f32 = [&](std::uint64_t o) { return *reinterpret_cast<const float*>(b + o); };
    const int type = b[rtx::scn::kType];
    if (type != 1 && type != 2) return false;
    const float fx = f32(rtx::scn::kPosX), fy = f32(rtx::scn::kPosY);
    if (!(fx > 0.f && fx < 1e8f && fy > 0.f && fy < 1e8f)) return false;
    a.type = type; a.tx = (int)(fx / 512.f); a.ty = (int)(fy / 512.f); a.plane = i32(rtx::scn::kPlane); a.addr = sec;
    a.uid = i32(rtx::scn::kUid); a.anim = i32(kCbAnim); a.targetUid = i32(kCbTargetUid); a.targetKind = b[kCbTargetKind];
    cb_name(b + rtx::scn::kName, u64(kCbNameLen), a.name);
    a.self = (type == 2 && a.uid == c.out.localUid);
    if (type == 1) {
        int id = i32(rtx::scn::kNpcCur);
        if (id < 0) id = i32(rtx::scn::kConfig);
        a.id = id;
        std::memcpy(a.stats, b + kCbNpcStats, 28); std::memcpy(a.base, b + kCbNpcBase, 28); a.vis = i32(kCbNpcVis);
        a.animCount = i32(kCbAnimCount); a.animCycle = i32(kCbAnimCycle); a.haveAnimStart = true;
        a.haveStats = true;
        for (int k = 0; k < 7; ++k) if (a.stats[k] < -100000 || a.stats[k] > 1000000000 || a.base[k] < 0 || a.base[k] > 1000000000) a.haveStats = false;
        if (a.haveStats) { a.lp = a.stats[3] < 0 ? -1 : a.stats[3]; a.lpMax = a.base[3] <= 0 ? -1 : a.base[3]; }
        a.npcTarget = i32(rtx::scn::kNpcTarget);
        // an empty live name, or one that is an id string (summons such as the conjures carry one): the cache
        // name, which has the readable overrides; nothing when the cache has none either
        const bool idLike = a.name.find('_') != std::string::npos && a.name.find(' ') == std::string::npos;
        if ((a.name.empty() || idLike) && id >= 0) {
            auto it = c.st.npcNames.find(id);
            if (it == c.st.npcNames.end()) it = c.st.npcNames.emplace(id, rtx::cache::GetNpc(id).name).first;
            a.name = it->second;
        }
    } else {
        const int cb = i32(rtx::scn::kCombat);
        a.combat = (cb >= 0 && cb <= 5000) ? cb : -1;
    }
    const std::uint64_t ov = u64(rtx::scn::kOverhead);
    if (cb_heap(ov)) {
        std::uint8_t hb[0x40];
        ++c.out.reads;
        if (rpm_bytes(c.h, ov, hb, sizeof(hb))) {
            const std::uint64_t ring = *reinterpret_cast<const std::uint64_t*>(hb + rtx::scn::kOvRing);
            if (cb_heap(ring)) {
                std::int32_t rec[6 * 6];
                ++c.out.reads;
                if (rpm_bytes(c.h, ring, rec, sizeof(rec))) {
                    a.haveRing = true;
                    for (int s = 0; s < 6; ++s) {
                        const std::int32_t* r = rec + s * 6;
                        CombatHitRec& hr = a.ring[s];
                        if (r[0] < 0 || r[0] > 100000 || r[1] < 0 || r[1] > 100000000 || r[2] <= 0 || r[5] <= 0 || r[5] > 1000) { hr = CombatHitRec{}; continue; }
                        hr.hitmark = r[0]; hr.value = r[1]; hr.expiry = r[2]; hr.hm2 = r[3]; hr.value2 = r[4]; hr.dur = r[5];
                    }
                } else ++c.out.fails;
            }
            if (bars || a.self) {
                const std::uint64_t sb = *reinterpret_cast<const std::uint64_t*>(hb + rtx::scn::kOvSlots);
                const std::uint64_t se = *reinterpret_cast<const std::uint64_t*>(hb + rtx::scn::kOvSlots + 8);
                if (cb_heap(sb) && se >= sb && (se - sb) % rtx::scn::kBarStride == 0) {
                    const int n = (int)std::min<std::uint64_t>((se - sb) / rtx::scn::kBarStride, 4);
                    if (n > 0) {
                        std::uint8_t bb[4 * rtx::scn::kBarStride];
                        ++c.out.reads;
                        if (rpm_bytes(c.h, sb, bb, (std::size_t)n * rtx::scn::kBarStride)) {
                            a.nbars = n;
                            for (int k = 0; k < n; ++k) {
                                a.barStamp[k] = *reinterpret_cast<const std::int32_t*>(bb + k * rtx::scn::kBarStride + rtx::scn::kBarStamp);
                                a.barFill[k]  = *reinterpret_cast<const std::int32_t*>(bb + k * rtx::scn::kBarStride + rtx::scn::kBarFill);
                            }
                        } else ++c.out.fails;
                    }
                }
            }
        } else ++c.out.fails;
    }
    return true;
}
}  // namespace

void CombatWatch(std::uint32_t pid, const std::vector<int>& varps, const std::vector<int>& varcs) {
    std::lock_guard<std::mutex> lk(g_cbMu);
    auto& st = g_cb[pid];
    st.vp = CombatVarDomain{}; st.vp.wanted = varps;
    st.vc = CombatVarDomain{}; st.vc.wanted = varcs;
}

void CombatForget(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_cbMu);
    g_cb.erase(pid);
}

static void perks_of(const int val[8], int out[8]);   // an item's instance vars -> perk id, rank x 4 (defined with the perk layout)

bool CombatRead(std::uint32_t pid, bool bars, CombatSample& out) {
    out = CombatSample{};
    auto ps = snap_proc(pid);
    if (!ps) return false;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return false;
    std::lock_guard<std::mutex> lk(g_cbMu);
    CombatReadState& st = g_cb[pid];
    CombatCtx c{ h, *root, st, out };
    using namespace std::chrono;
    auto clk = rpm<std::uint32_t>(h, *root + kOffClientClock);
    out.wallMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    ++out.reads;
    if (!clk) { ++out.fails; return false; }
    out.clock = *clk;
    // inventory and equipment: once per game tick (the game changes them no faster), the container list in one
    // read, then each one's slots
    if (out.clock / 30 != st.itemsTick && (st.itemsTick = out.clock / 30, true)) {
        ++out.reads;
        const std::uint64_t cm = rpm<std::uint64_t>(h, *root + kOffInvData).value_or(0);
        std::uint64_t se[2] = { 0, 0 };
        if (cm > 0x10000 && (++out.reads, rpm_bytes(h, cm + 0x8, se, sizeof(se)))) {
            const int n = container_count(se[0], se[1]);
            if (n > 0 && se[0] > 0x10000) {
                std::vector<std::uint8_t> list((std::size_t)n * kContainerStride);
                ++out.reads;
                if (rpm_bytes(h, se[0], list.data(), list.size())) {
                    bool got93 = false, got94 = false;
                    for (int k = 0; k < n; ++k) {
                        const std::uint8_t* e = list.data() + (std::size_t)k * kContainerStride;
                        const int id = *reinterpret_cast<const std::int32_t*>(e + 0x10);
                        if (id != 93 && id != 94) continue;
                        const std::uint64_t a = *reinterpret_cast<const std::uint64_t*>(e + 0x18), z = *reinterpret_cast<const std::uint64_t*>(e + 0x20);
                        if (a <= 0x10000 || z < a || (z - a) % 8 || (z - a) / 8 > 64) continue;
                        const int cap = (int)((z - a) / 8);
                        std::vector<std::int32_t> sl((std::size_t)cap * 2);
                        ++out.reads;
                        if (cap && !rpm_bytes(h, a, sl.data(), sl.size() * 4)) { ++out.fails; continue; }
                        auto& dst = id == 93 ? out.inv : out.equip;
                        dst.clear();
                        for (int s = 0; s < cap; ++s) {
                            const int item = sl[(std::size_t)s * 2], qty = sl[(std::size_t)s * 2 + 1];
                            dst.push_back({ item >= 0 && item < 0x1000000 ? item : -1, item >= 0 ? qty : 0 });
                        }
                        if (id == 94) {   // perks of each worn augmented item: the slot's instance vars (key +0, value +8)
                            out.equipPerks.assign((std::size_t)cap, std::array<int, 8>{});
                            const std::uint64_t xs = *reinterpret_cast<const std::uint64_t*>(e + 0x30);
                            for (int s = 0; xs > 0x10000 && s < cap; ++s) {
                                const int item = out.equip[(std::size_t)s].first;
                                if (item < 0 || !rtx::cache::ItemIsAugmented(item)) continue;
                                std::uint8_t xb[0x38];
                                ++out.reads;
                                if (!rpm_bytes(h, xs + (std::uint64_t)s * 0x38, xb, sizeof(xb))) { ++out.fails; continue; }
                                const int cnt = *reinterpret_cast<const std::int32_t*>(xb + 0x18);
                                const std::uint64_t arr = *reinterpret_cast<const std::uint64_t*>(xb + 0x10);
                                if (cnt < 2 || cnt > 10 || arr <= 0x10000) continue;
                                std::uint64_t ptrs[10] = {};
                                ++out.reads;
                                if (!rpm_bytes(h, arr, ptrs, (std::size_t)cnt * 8)) { ++out.fails; continue; }
                                int val[8] = {};
                                for (int j = 0; j < cnt; ++j) {
                                    if (ptrs[j] <= 0x10000) continue;
                                    std::int32_t node[4] = {};
                                    ++out.reads;
                                    if (!rpm_bytes(h, ptrs[j], node, sizeof(node))) { ++out.fails; continue; }
                                    if (node[0] >= 0 && node[0] < 8) val[node[0]] = node[2];
                                }
                                int pk[8] = {};
                                perks_of(val, pk);
                                for (int k = 0; k < 8; ++k) out.equipPerks[(std::size_t)s][(std::size_t)k] = pk[k];
                            }
                        }
                        {   // the special attack each Essence of Finality here stores
                            auto& eof = id == 93 ? out.invEof : out.equipEof;
                            eof.assign((std::size_t)cap, -2);
                            const std::uint64_t xs = *reinterpret_cast<const std::uint64_t*>(e + 0x30);
                            for (int s = 0; s < cap; ++s) {
                                if (!item_is_eof(dst[(std::size_t)s].first)) continue;
                                int val[8];
                                eof[(std::size_t)s] = xs > 0x10000 && slot_item_vars(h, xs, s, val, out.reads, out.fails) ? std::max(0, val[3]) : -3;
                            }
                        }
                        (id == 93 ? got93 : got94) = true;
                    }
                    out.haveItems = got93 || got94;
                }
            }
        }
    }
    {   // the map base at [MainData+0x19898]+0x698 / +0x69C, for the zone packets the recorder decodes
        ++out.reads;
        const std::uint64_t mm = rpm<std::uint64_t>(h, *root + kOffMapMgr).value_or(0);
        std::int32_t mb[2] = { 0, 0 };
        if (mm > 0x10000) {
            ++out.reads;
            if (rpm_bytes(h, mm + 0x698, mb, sizeof(mb))) { out.mapBaseX = mb[0]; out.mapBaseY = mb[1]; out.haveMapBase = true; }
        }
    }
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off) -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        ++out.reads;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto pdata = deref(root, rtx::scn::kPlayerData);
    out.localUid = (pdata && *pdata > 0x10000) ? rpm<std::int32_t>(h, *pdata + rtx::scn::kLocalUid).value_or(-1) : -1;
    ++out.reads;
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
    if (!(vb && ve && *vb > 0x10000 && *ve >= *vb)) return false;
    std::uint64_t n = (*ve - *vb) / 8;
    if (n > 20000) n = 20000;
    std::vector<std::uint64_t> eps((std::size_t)n);
    if (n) {
        ++out.reads;
        if (!rpm_bytes(h, *vb, eps.data(), eps.size() * 8)) { ++out.fails; return false; }
    }
    const unsigned long long now = GetTickCount64();
    std::unordered_set<std::uint64_t> live; live.reserve(eps.size());
    bool selfSeen = false;
    for (std::uint64_t ep : eps) {
        if (!cb_heap(ep)) continue;
        live.insert(ep);
        ++out.reads;
        const std::uint64_t sec = rpm<std::uint64_t>(h, ep + rtx::scn::kSecPtr).value_or(0);
        if (!cb_heap(sec)) continue;
        CombatEpEntry& e = st.eps[ep];
        if (e.sec != sec || e.type < 0) {
            e.sec = sec;
            ++out.reads;
            e.type = (int)rpm<std::uint8_t>(h, sec + rtx::scn::kType).value_or(0xff);
        }
        if (e.type != 1 && e.type != 2) continue;
        CombatActorSample a;
        if (!cb_read_actor(c, sec, bars, a)) { e.type = -1; continue; }
        if (a.self) { selfSeen = true; st.localSec = sec; }
        out.actors.push_back(std::move(a));
    }
    if (st.eps.size() > live.size() + 64) {
        for (auto it = st.eps.begin(); it != st.eps.end();) it = live.count(it->first) ? std::next(it) : st.eps.erase(it);
    }
    if (!selfSeen && out.localUid >= 0) {        // the local player through the registry when the vector did not have it
        const std::uint64_t psec = local_player_sec_fast(h, *root, out.localUid);
        out.reads += 5;
        if (psec) {
            CombatActorSample a;
            if (cb_read_actor(c, psec, true, a) && a.self) { st.localSec = psec; out.actors.push_back(std::move(a)); }
        }
    }
    const std::uint64_t vcStore = rpm<std::uint64_t>(h, *root + kOffVarcStore).value_or(0);
    ++out.reads;
    cb_read_vars(h, *root + kOffVarpHash, st.vp, now, out.varps, out.reads, out.fails);
    if (vcStore > 0x10000) cb_read_vars(h, vcStore + kVarcHashOff, st.vc, now, out.varcs, out.reads, out.fails);
    out.ok = true;
    return true;
}

bool CombatReadLocal(std::uint32_t pid, std::uint32_t& clock, int& anim, int& targetUid) {
    std::uint64_t sec = 0;
    {
        std::lock_guard<std::mutex> lk(g_cbMu);
        auto it = g_cb.find(pid);
        if (it == g_cb.end()) return false;
        sec = it->second.localSec;
    }
    if (!sec) return false;
    auto ps = snap_proc(pid);
    if (!ps) return false;
    auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
    if (!root || *root <= 0x10000) return false;
    auto clk = rpm<std::uint32_t>(ps.h, *root + kOffClientClock);
    auto an = rpm<std::int32_t>(ps.h, sec + kCbAnim);
    auto tg = rpm<std::int32_t>(ps.h, sec + kCbTargetUid);
    if (!clk || !an || !tg) return false;
    clock = *clk; anim = *an; targetUid = *tg;
    return true;
}

// Tracker grid: [MainData+kOffTracker] -> {begin, end} of 0x68-byte groups {id +0, column ids +8/+0x10,
// row ids +0x20/+0x28, rows +0x50/+0x58 as 24-byte {values begin, values end, ..} entries}. Group 4
// (one row per skill) is skipped.
bool CombatTrackers(std::uint32_t pid, std::vector<CombatTrackerCell>& out) {
    out.clear();
    auto ps = snap_proc(pid);
    if (!ps) return false;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return false;
    const std::uint64_t mgr = rpm<std::uint64_t>(h, *root + kOffTracker).value_or(0);
    if (mgr <= 0x10000) return false;
    const std::uint64_t gb = rpm<std::uint64_t>(h, mgr).value_or(0), ge = rpm<std::uint64_t>(h, mgr + 8).value_or(0);
    if (gb <= 0x10000 || ge < gb || (ge - gb) % kTrackerGroupSize || (ge - gb) / kTrackerGroupSize > 16) return false;
    std::vector<std::uint8_t> G((std::size_t)(ge - gb));
    if (!G.empty() && !rpm_bytes(h, gb, G.data(), G.size())) return false;
    auto ids = [&](std::uint64_t b, std::uint64_t e, std::vector<int>& v) {
        v.clear();
        if (b <= 0x10000 || e < b || (e - b) % 4 || (e - b) / 4 > 64) return;
        v.resize((std::size_t)((e - b) / 4));
        if (!v.empty() && !rpm_bytes(h, b, v.data(), v.size() * 4)) v.clear();
    };
    for (std::size_t off = 0; off + kTrackerGroupSize <= G.size(); off += kTrackerGroupSize) {
        const std::uint8_t* g = G.data() + off;
        auto u64 = [&](std::size_t o) { return *reinterpret_cast<const std::uint64_t*>(g + o); };
        const int gid = *reinterpret_cast<const std::int32_t*>(g);
        if (gid == 4) continue;
        std::vector<int> cols, rows;
        ids(u64(0x08), u64(0x10), cols); ids(u64(0x20), u64(0x28), rows);
        if (cols.empty() || rows.empty()) continue;
        const std::uint64_t ob = u64(0x50), oe = u64(0x58);
        if (ob <= 0x10000 || oe < ob || (oe - ob) % 24 || (oe - ob) / 24 > 16) continue;
        std::vector<std::uint8_t> outer((std::size_t)(oe - ob));
        if (!rpm_bytes(h, ob, outer.data(), outer.size())) continue;
        const std::size_t nrows = outer.size() / 24;
        for (std::size_t r = 0; r < nrows && r < rows.size(); ++r) {
            const std::uint64_t ib = *reinterpret_cast<const std::uint64_t*>(outer.data() + r * 24);
            const std::uint64_t ie = *reinterpret_cast<const std::uint64_t*>(outer.data() + r * 24 + 8);
            if (ib <= 0x10000 || ie < ib || ie - ib > 256 || (ie - ib) % 4) continue;
            std::vector<std::int32_t> vals((std::size_t)((ie - ib) / 4));
            if (vals.empty() || !rpm_bytes(h, ib, vals.data(), vals.size() * 4)) continue;
            for (std::size_t k = 0; k < vals.size() && k < cols.size(); ++k)
                out.push_back({ gid, rows[r], cols[k], vals[k] });
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Live terrain heights. The game places every actor with the routine at exe+0x357A50 (actor, xy): plane =
// actor plane (+1 on a bridge tile), then per region (x>>6, y>>6) the terrain object at
// region+0xA0 (or +0xEBB0) holds one height grid per plane at +0x170 (0x10 per plane, grid ptr at
// +8; plane 0 also has an adjusted grid at +0x1F8 that the actor path uses). A grid is a column
// table: column x at [grid] + 0x18*(x+1), value (y) at column + 4*(y+1), i32 fine units, padded
// by one on each side. The client interpolates bilinearly on the 9-bit tile fraction and adds 5.0
// for the actor's feet. Read from the 950-1 client; the
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
// Reads n blocks of `size` bytes, block i from addr[i] to dst + i * size. A grid's columns are small
// allocations that usually sit side by side, so one read over their span replaces n small ones; a
// span that is too wide or not wholly readable falls back to one read per block.
bool rpm_blocks(HANDLE h, const std::uint64_t* addr, int n, std::size_t size, void* dst) {
    std::uint8_t* d = static_cast<std::uint8_t*>(dst);
    std::uint64_t lo = addr[0], hi = addr[0];
    for (int i = 1; i < n; ++i) { if (addr[i] < lo) lo = addr[i]; if (addr[i] > hi) hi = addr[i]; }
    const std::uint64_t span = hi - lo + size;
    if (span <= 0x10000) {
        thread_local std::vector<std::uint8_t> buf;
        buf.resize((std::size_t)span);
        if (rpm_bytes(h, lo, buf.data(), (SIZE_T)span)) {
            for (int i = 0; i < n; ++i) std::memcpy(d + (std::size_t)i * size, buf.data() + (addr[i] - lo), size);
            return true;
        }
    }
    for (int i = 0; i < n; ++i)
        if (!rpm_bytes(h, addr[i], d + (std::size_t)i * size, (SIZE_T)size)) return false;
    return true;
}
// The terrain object of region (rx, ry), 0 when the region is not loaded. Every plane shares it.
std::uint64_t terrain_object(HANDLE h, std::uint64_t scene, int rx, int ry) {
    auto mgr = rpm<std::uint64_t>(h, scene + 0x140C0);
    if (!mgr || *mgr <= 0x10000) return 0;
    auto minx = rpm<std::int32_t>(h, *mgr + 0x14034), miny = rpm<std::int32_t>(h, *mgr + 0x14038);
    auto maxx = rpm<std::int32_t>(h, *mgr + 0x1403C), maxy = rpm<std::int32_t>(h, *mgr + 0x14040);
    if (!minx || !miny || !maxx || !maxy || rx < *minx || rx > *maxx || ry < *miny || ry > *maxy) return 0;
    auto rows = rpm<std::uint64_t>(h, *mgr + 0x14080);
    if (!rows || *rows <= 0x10000) return 0;
    auto col = rpm<std::uint64_t>(h, *rows + (std::uint64_t)(rx - *minx) * 0x18);
    if (!col || *col <= 0x10000) return 0;
    auto region = rpm<std::uint64_t>(h, *col + (std::uint64_t)(ry - *miny) * 0x18 + 8);
    if (!region || *region <= 0x10000) return 0;
    std::uint64_t terrain = 0;
    for (std::uint64_t off : { (std::uint64_t)0xA0, (std::uint64_t)0xEBB0 }) {
        auto t = rpm<std::uint64_t>(h, *region + off);
        if (t && *t > 0x10000 && rpm<std::uint8_t>(h, *t + 0x21).value_or(1) == rpm<std::uint8_t>(h, *t + 0x22).value_or(2)) { terrain = *t; break; }
    }
    if (!terrain || rpm<std::uint8_t>(h, terrain + 0x169).value_or(1) != 0) return 0;
    return terrain;
}
// One plane's tile flag bytes, masked, into dst[x][y] (layout below, in terrain_read_plane).
bool terrain_read_flags(HANDLE h, std::uint64_t terrain, int fplane, std::uint8_t dst[64][64], int mask) {
    auto f0 = rpm<std::uint64_t>(h, terrain + 0x200), f1 = rpm<std::uint64_t>(h, terrain + 0x208);
    if (!f0 || !f1 || *f1 < *f0 + (std::uint64_t)(fplane + 1) * 0x10) return false;
    auto fcols = rpm<std::uint64_t>(h, *f0 + (std::uint64_t)fplane * 0x10 + 8);
    if (!fcols || *fcols <= 0x10000) return false;
    auto cb = rpm<std::uint64_t>(h, *fcols), ce = rpm<std::uint64_t>(h, *fcols + 8);
    if (!cb || !ce || *ce < *cb + 66 * 0x18) return false;
    std::uint64_t tab[64 * 3 - 1];                    // columns 1..64 as {begin, end, capacity}, the last capacity not needed
    if (!rpm_bytes(h, *cb + 0x18, tab, sizeof(tab))) return false;
    std::uint64_t colb[64];
    for (int lx = 0; lx < 64; ++lx) {
        colb[lx] = tab[lx * 3];
        if (tab[lx * 3 + 1] < colb[lx] + 66) return false;
    }
    std::uint8_t fb[64][66];
    if (!rpm_blocks(h, colb, 64, sizeof(fb[0]), fb)) return false;
    for (int lx = 0; lx < 64; ++lx)
        for (int ly = 0; ly < 64; ++ly) dst[lx][ly] = (std::uint8_t)(fb[lx][ly + 1] & mask);
    return true;
}
// One plane of a region: heights, standing offsets and that plane's tile flags. The plane-1 bridge
// flags are per region and filled in by the caller.
bool terrain_read_plane(HANDLE h, std::uint64_t terrain, int plane, TerrainRegion& out) {
    out.ok = false;
    auto p0 = rpm<std::uint64_t>(h, terrain + 0x170), p1 = rpm<std::uint64_t>(h, terrain + 0x178);
    if (!p0 || !p1 || *p1 < *p0) return false;
    const int nplanes = (int)((*p1 - *p0) / 16);
    if (plane < 0 || plane >= nplanes) return false;
    std::optional<std::uint64_t> grid = (plane == 0) ? rpm<std::uint64_t>(h, terrain + 0x1F8) : std::nullopt;
    if (!grid || *grid <= 0x10000) grid = rpm<std::uint64_t>(h, *p0 + (std::uint64_t)plane * 0x10 + 8);
    if (!grid || *grid <= 0x10000) return false;
    auto cols = rpm<std::uint64_t>(h, *grid);
    if (!cols || *cols <= 0x10000) return false;
    std::uint64_t ctab[65 * 3 + 1];                   // 66 column entries of 0x18, pointer first
    if (!rpm_bytes(h, *cols, ctab, sizeof(ctab))) return false;
    std::uint64_t colptr[66];
    for (int lx = 0; lx < 66; ++lx) {
        colptr[lx] = ctab[lx * 3];
        if (colptr[lx] <= 0x10000) return false;
    }
    if (!rpm_blocks(h, colptr, 66, sizeof(out.h[0]), out.h)) return false;
    out.ok = true;
    // Per-tile offsets (exe+0x3BE920): [terrain+0x1E8] = planes x 64 x 64 cells of two i16, cell
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
    // Tile flags (exe+0x3BF100 over terrain+0x200): a vector of planes (0x10 each, column table
    // pointer at +8), columns of 0x18-byte byte vectors, one pad byte each side like the heights.
    // The actor plane helper (exe+0x3579F0) asks plane 1 and treats bit 1 (value 2) as "bridge":
    // the tile is drawn and stood on one plane up. Read plane 1's flags for every region.
    // The flag byte is the map's tile settings byte (bit 0 blocked/void, bit 1 bridge, bit 3 force
    // lowest plane); the cache builds its blocked grid from the same bit 0, so reading it live
    // gives the void tiles inside instances, where the cache has nothing.
    std::memset(out.bridge, 0, sizeof(out.bridge)); std::memset(out.flag, 0, sizeof(out.flag));
    out.planeFlagsOk = terrain_read_flags(h, terrain, plane, out.flag, 0xFF);
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
    int planes[3], np = 0;
    for (int pl : { plane, plane + 1, 0 }) {   // plane + 1: bridge tiles are sampled one plane up
        if (pl < 0 || pl > 3 || std::find(planes, planes + np, pl) != planes + np) continue;
        planes[np++] = pl;
    }
    // This runs for every overlay frame, so each region's terrain object is found once and its
    // plane-1 bridge flags read once, then shared by every plane entry of that region.
    for (int rx = rx0 - 1; rx <= rx0 + 1; ++rx)
        for (int ry = ry0 - 1; ry <= ry0 + 1; ++ry) {
            const std::uint64_t terrain = terrain_object(h, *scene, rx, ry);
            if (!terrain) continue;
            std::uint8_t bridge[64][64]; int bridgeOk = -1;   // -1: not read yet
            for (int i = 0; i < np; ++i) {
                TerrainRegion r;
                if (!terrain_read_plane(h, terrain, planes[i], r)) continue;
                if (bridgeOk < 0) {
                    std::memset(bridge, 0, sizeof(bridge));
                    bridgeOk = terrain_read_flags(h, terrain, 1, bridge, 2) ? 1 : 0;
                }
                std::memcpy(r.bridge, bridge, sizeof(bridge));
                r.flagsOk = bridgeOk == 1;
                snap.regions.emplace((planes[i] << 16) | (rx << 8) | ry, r);
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
// on bridge tiles (plane-1 flag bit 1), as exe+0x3579F0 does for every actor. `plane` when unknown.
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

// The height the game adds to an entity's origin before projecting an overhead, read through the
// rule that entity's own class uses. src reports which branch answered, so a wrong reading is
// visible rather than silent: 1 actor override A, 2 actor override B, 3 actor model, 4 loc model,
// 0 none (the game draws at the origin).
static int overhead_height(HANDLE h, std::uint64_t sec, int type, int* src) {
    if (src) *src = 0;
    if (type == 1 || type == 2) {
        if (type == 1) {
            if (auto a = rpm<std::uint64_t>(h, sec + 0x10a8))
                if (*a > 0x10000)
                    if (auto v = rpm<std::int32_t>(h, *a + 0x2a0))
                        if (*v != -1) { if (src) *src = 1; return *v; }
        }
        if (auto b = rpm<std::uint64_t>(h, sec + 0xf58))
            if (*b > 0x10000)
                if (auto v = rpm<std::int32_t>(h, *b + 0xf4))
                    if (*v != -1) { if (src) *src = 2; return *v; }
        if (auto m = rpm<std::uint64_t>(h, sec + 0xc68))
            if (*m > 0x10000)
                if (auto f = rpm<float>(h, *m + 0x24)) { if (src) *src = 3; return (int)*f; }
        return 0;
    }
    if (type == 10 || type == 4) {
        auto d = rpm<std::uint64_t>(h, sec + 0x18);
        if (!d || *d <= 0x10000) return 0;
        auto fl = rpm<std::uint8_t>(h, *d + 0xfc);
        if (!fl || !((*fl >> 3) & 1)) return 0;
        if (auto f = rpm<float>(h, *d + 0x74)) { if (src) *src = 4; return (int)*f; }
    }
    return 0;
}

// The point that height is measured from: three floats (east, up, north), where the class keeps them.
static bool overhead_origin(HANDLE h, std::uint64_t sec, int type, float& x, float& up, float& z) {
    std::uint64_t at = 0;
    if (type == 1 || type == 2) at = sec + 0x270;
    else {
        auto d = rpm<std::uint64_t>(h, sec + 0x18);
        if (!d || *d <= 0x10000) return false;
        at = *d + 0xd0;
    }
    auto fx = rpm<float>(h, at), fu = rpm<float>(h, at + 4), fz = rpm<float>(h, at + 8);
    if (!fx || !fu || !fz) return false;
    x = *fx; up = *fu; z = *fz;
    return true;
}

// Live scenery, walked from the scene entity list the reader already resolves. Independent of the
// companion's own loc tracking: this is the same list the game draws from, read directly.
struct LiveLoc {
    int   id = 0;                 // loc config id
    int   plane = 0;
    int   tx = 0, ty = 0;         // world tile of the model's centre
    float cx = 0, cz = 0;         // world fine centre (east, north)
    float ground = 0;             // foot of the model, world fine up
    int   head = 0;               // what the game adds above `ground` to place an overhead
};

static int live_loc_config(HANDLE h, std::uint64_t sub) {
    constexpr std::uint64_t kConfigId = 0xb0, kDefLocId = 0x28;
    if (auto v = rpm<std::uint64_t>(h, sub + kConfigId))
        if (*v > 0x10000 && *v < 0x00007FFFFFFFFFFFull)
            if (auto id = rpm<std::int32_t>(h, *v + kDefLocId))
                if (*id >= 1 && *id <= 200000) return *id;
    if (auto lo = rpm<std::int32_t>(h, sub + kConfigId))
        if (*lo >= 1 && *lo <= 200000) return *lo;
    return 0;
}

bool ReadLiveLocs(std::uint32_t pid, std::vector<LiveLoc>& out) {
    out.clear();
    auto ps = snap_proc(pid);
    if (!ps) return false;
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return false;
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off) -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto cont = deref(root, rtx::scn::kContainer);
    auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + rtx::scn::kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, rtx::scn::kEntryArr);
    if (!idx || *idx < 0 || !arr || *arr <= 0x10000) return false;
    auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + rtx::scn::kEntryWv);
    if (!wv || *wv <= 0x10000) return false;
    // Scenery is not in the actor vector: the world view keeps per-zone entity vectors in a band of
    // its own. Same band the client walks, matched by its entries being scenery subs.
    out.reserve(512);
    for (std::uint64_t wo = 0x10000; wo < 0x10400 && out.size() < 2000; wo += 8) {
        auto vb = rpm<std::uint64_t>(h, *wv + wo + rtx::scn::kVecBegin);
        auto ve = rpm<std::uint64_t>(h, *wv + wo + rtx::scn::kVecEnd);
        if (!vb || !ve || *vb <= 0x10000 || *ve <= *vb || ((*ve - *vb) & 7)) continue;
        std::uint64_t n = (*ve - *vb) / 8;
        if (!n || n > 30000) continue;
        for (std::uint64_t i = 0; i < n && out.size() < 2000; ++i) {
            auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
            if (!ep || *ep <= 0x10000) continue;
            auto sec = rpm<std::uint64_t>(h, *ep + rtx::scn::kSecPtr);
            if (!sec || *sec <= 0x10000) continue;
            const int type = rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xff);
            if (type != 0 && type != 12) continue;
            LiveLoc L;
            L.id = live_loc_config(h, *sec);
            if (!L.id) continue;
            L.plane = rpm<std::int32_t>(h, *sec + rtx::scn::kPlane).value_or(0);
            // Placement comes from the entity's world box; the class origin is kept in the world
            // view's own space, so it says how tall the model is but not where it stands.
            const float mnx = rpm<float>(h, *ep + 0x40).value_or(0.f);
            const float mny = rpm<float>(h, *ep + 0x44).value_or(0.f);
            const float mnz = rpm<float>(h, *ep + 0x48).value_or(0.f);
            const float mxx = rpm<float>(h, *ep + 0x50).value_or(0.f);
            const float mxz = rpm<float>(h, *ep + 0x58).value_or(0.f);
            if (!(mxx > mnx && mxz > mnz)) continue;
            L.cx = (mnx + mxx) * 0.5f;
            L.cz = (mnz + mxz) * 0.5f;
            L.ground = mny;
            L.tx = (int)(L.cx / 512.f);
            L.ty = (int)(L.cz / 512.f);
            if (L.tx <= 0 || L.ty <= 0 || L.tx > 16384 || L.ty > 16384) continue;
            L.head = overhead_height(h, *sec, 10, nullptr);
            out.push_back(L);
        }
    }
    return !out.empty();
}

// Head-bar slots on every actor that has an overhead, walked with the stride the drawing code
// uses. Reports what the single-slot read sees against every slot in the vector, so a stale
// reading is visible rather than assumed.
std::string OverheadBarsJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"actors\":[]}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{\"actors\":[]}";
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off) -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto cont = deref(root, rtx::scn::kContainer);
    auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + rtx::scn::kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, rtx::scn::kEntryArr);
    if (!idx || *idx < 0 || !arr || *arr <= 0x10000) return "{\"actors\":[]}";
    auto wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + rtx::scn::kEntryWv);
    if (!wv || *wv <= 0x10000) return "{\"actors\":[]}";
    auto worker = scene_worker(h, pid, *wv, nullptr);
    auto vb = deref(worker, rtx::scn::kVecBegin), ve = deref(worker, rtx::scn::kVecEnd);
    if (!vb || !ve || *vb <= 0x10000 || *ve < *vb) return "{\"actors\":[]}";
    std::uint64_t n = (*ve - *vb) / 8;
    if (n > 30000) n = 30000;
    std::string out = "{\"actors\":["; bool first = true;
    for (std::uint64_t i = 0; i < n; ++i) {
        auto ep = rpm<std::uint64_t>(h, *vb + i * 8);
        if (!ep || *ep <= 0x10000) continue;
        auto sec = rpm<std::uint64_t>(h, *ep + rtx::scn::kSecPtr);
        if (!sec || *sec <= 0x10000) continue;
        const int type = rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xff);
        if (type != 1 && type != 2) continue;
        auto hb = rpm<std::uint64_t>(h, *sec + rtx::scn::kOverhead);
        if (!hb || *hb <= 0x10000) continue;
        auto sb = rpm<std::uint64_t>(h, *hb + rtx::scn::kOvSlots);
        auto se = rpm<std::uint64_t>(h, *hb + 0x30);
        if (!sb || !se || *sb <= 0x10000 || *se < *sb) continue;
        const std::uint64_t span = *se - *sb;
        const int slots = (span % 0x1b0 == 0) ? (int)(span / 0x1b0) : -(int)span;
        const int cfg = rpm<std::int32_t>(h, *sec + rtx::scn::kConfig).value_or(-1);
        char b[160];
        std::snprintf(b, sizeof(b), "%s{\"type\":%d,\"cfg\":%d,\"span\":%llu,\"slots\":%d,\"s\":[",
                      first ? "" : ",", type, cfg, (unsigned long long)span, slots);
        out += b; first = false;
        for (int k = 0; k < 8 && (std::uint64_t)k * 0x1b0 < span; ++k) {
            const std::uint64_t el = *sb + (std::uint64_t)k * 0x1b0;
            const int stamp = rpm<std::int32_t>(h, el + 0x78).value_or(-1);
            const int fill  = rpm<std::int32_t>(h, el + 0x7c).value_or(-1);
            auto node = rpm<std::uint64_t>(h, el);
            int ncyc = -1, n34 = -1, n3c = -1, n44 = -1;
            if (node && *node > 0x10000 && *node != el) {
                ncyc = rpm<std::int32_t>(h, *node + 0x30).value_or(-1);
                n34  = rpm<std::int32_t>(h, *node + 0x34).value_or(-1);
                n3c  = rpm<std::int32_t>(h, *node + 0x3c).value_or(-1);
                n44  = rpm<std::int32_t>(h, *node + 0x44).value_or(-1);
            }
            char c[200];
            std::snprintf(c, sizeof(c),
                "%s{\"i\":%d,\"stamp\":%d,\"fill\":%d,\"cyc\":%d,\"a\":%d,\"b\":%d,\"c\":%d}",
                k ? "," : "", k, stamp, fill, ncyc, n34, n3c, n44);
            out += c;
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

std::string LiveLocsJson(std::uint32_t pid) {
    std::vector<LiveLoc> v;
    if (!ReadLiveLocs(pid, v)) return "{\"locs\":[]}";
    std::string out = "{\"locs\":[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        char b[192];
        std::snprintf(b, sizeof(b),
                      "%s{\"id\":%d,\"plane\":%d,\"head\":%d,\"tile\":[%d,%d],\"ground\":%.1f}",
                      i ? "," : "", v[i].id, v[i].plane, v[i].head, v[i].tx, v[i].ty, v[i].ground);
        out += b;
    }
    out += "]}";
    return out;
}

// Every entity in the scene with its class pointer and the two class entries the game's own
// overhead drawing goes through: the position getter and the height it adds before projecting.
// Reported as image offsets so they can be looked up in the binary.
std::string OverheadClassJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"entities\":[]}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{\"entities\":[]}";
    const std::uint64_t image = ps.mod_base;
    auto deref = [&](std::optional<std::uint64_t> p, std::uint64_t off) -> std::optional<std::uint64_t> {
        if (!p || *p <= 0x10000) return std::nullopt;
        return rpm<std::uint64_t>(h, *p + off);
    };
    auto cont = deref(root, rtx::scn::kContainer);
    auto idx  = (cont && *cont > 0x10000) ? rpm<std::int32_t>(h, *cont + rtx::scn::kActiveIdx) : std::nullopt;
    auto arr  = deref(cont, rtx::scn::kEntryArr);
    std::optional<std::uint64_t> wv, worker, vbo, veo;
    if (idx && *idx >= 0 && arr && *arr > 0x10000) {
        wv = rpm<std::uint64_t>(h, *arr + (std::uint64_t)*idx * 0x10 + rtx::scn::kEntryWv);
        if (wv && *wv > 0x10000) worker = scene_worker(h, pid, *wv, nullptr);
        vbo = deref(worker, rtx::scn::kVecBegin);
        veo = deref(worker, rtx::scn::kVecEnd);
    }
    if (!(vbo && veo && *vbo > 0x10000 && *veo >= *vbo)) return "{\"entities\":[]}";
    const std::uint64_t vb = *vbo, ve = *veo;
    // The two pointers the query operations go through before they touch anything else, so a fault
    // inside them can be told apart from a bad argument.
    const std::uint64_t dbMgr  = rpm<std::uint64_t>(h, *root + 0x19980).value_or(0);
    const std::uint64_t dbOther = rpm<std::uint64_t>(h, *root + 0x198c0).value_or(0);
    std::string out = "{\"image\":\"0x" ;
    { char b[32]; std::snprintf(b, sizeof(b), "%llx", (unsigned long long)image); out += b; }
    { char b[96]; std::snprintf(b, sizeof(b), "\",\"dbMgr\":\"0x%llx\",\"dbOther\":\"0x%llx\",\"entities\":[",
                                (unsigned long long)dbMgr, (unsigned long long)dbOther); out += b; }
    bool first = true;
    std::uint64_t n = (ve > vb) ? (ve - vb) / 8 : 0;
    if (n > 30000) n = 30000;
    for (std::uint64_t i = 0; i < n; ++i) {
        auto ep = rpm<std::uint64_t>(h, vb + i * 8);
        if (!ep || *ep <= 0x10000) continue;
        auto sec = rpm<std::uint64_t>(h, *ep + rtx::scn::kSecPtr);
        if (!sec || *sec <= 0x10000) continue;
        auto vt = rpm<std::uint64_t>(h, *sec);
        if (!vt || *vt <= 0x10000) continue;
        auto pos = rpm<std::uint64_t>(h, *vt + 0xb0);
        auto hgt = rpm<std::uint64_t>(h, *vt + 0x78);
        if (!pos || !hgt) continue;
        int type = rpm<std::uint8_t>(h, *sec + rtx::scn::kType).value_or(0xff);
        int cfg  = rpm<std::int32_t>(h, *sec + rtx::scn::kUid).value_or(-1);
        int cfgId = rpm<std::int32_t>(h, *sec + rtx::scn::kConfig).value_or(-1);
        int src = 0;
        const int oh = overhead_height(h, *sec, type, &src);
        float px = 0, up = 0, pz = 0;
        overhead_origin(h, *sec, type, px, up, pz);
        char b[420];
        std::snprintf(b, sizeof(b),
            "%s{\"type\":%d,\"uid\":%d,\"cfg\":%d,\"vt\":\"0x%llx\",\"pos\":\"0x%llx\",\"height\":\"0x%llx\""
            ",\"oh\":%d,\"src\":%d,\"p\":[%.1f,%.1f,%.1f]}",
            first ? "" : ",", type, cfg, cfgId,
            (unsigned long long)(*vt  > image ? *vt  - image : *vt),
            (unsigned long long)(*pos > image ? *pos - image : *pos),
            (unsigned long long)(*hgt > image ? *hgt - image : *hgt),
            oh, src, px, up, pz);
        out += b; first = false;
    }
    out += "]}";
    return out;
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
                    // Facing in degrees, 0 = north, 90 = east, -1 unreadable. While a turn runs (f32 sec+0x1EC >
                    // i32 sec+0x1F0) report its target, the yaw quaternion y sec+0x1E0 / w sec+0x1E8: 2*atan2(-w, y).
                    // Otherwise report the drawn heading of the entity node (y ep+0xE4, w ep+0xEC): 2*atan2(y, w)
                    // + 180. NPCs that face a direction without turning keep identity values in the sec
                    // quaternions; only the node has them.
                    int face = -1;
                    {
                        auto toDeg = [](double r) { double d = std::fmod(r * 180.0 / 3.14159265358979323846, 360.0);
                                                    if (d < 0) d += 360.0; return static_cast<int>(d + 0.5) % 360; };
                        const float tl = rpm<float>(h, *sec + rtx::scn::kTurnLen).value_or(0.f);
                        const int   td = rpm<std::int32_t>(h, *sec + rtx::scn::kTurnDone).value_or(0);
                        if (!(tl > (float)td)) {
                            float q[4] = {};
                            if (rpm_bytes(h, *ep + rtx::scn::kNodeYaw, q, sizeof(q))) {
                                const double n = (double)q[1] * q[1] + (double)q[3] * q[3];
                                if (n > 0.9 && n < 1.1) face = toDeg(2.0 * std::atan2((double)q[1], (double)q[3]) + 3.14159265358979323846);
                            }
                        }
                        if (face < 0) {
                            auto y = rpm<float>(h, *sec + rtx::scn::kFaceTargetY), w = rpm<float>(h, *sec + rtx::scn::kFaceTargetW);
                            if (y && w && (*y != 0.0f || *w != 0.0f)) face = toDeg(2.0 * std::atan2(-(double)*w, (double)*y));
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
                    int vis = rpm<std::int32_t>(h, *sec + kNpcVisLevel).value_or(-1);   // the level the game shows, -1 unread
                    if (vis < 0 || vis > 20000) vis = -1;
                    npcs += "\",\"actions\":["; npcs += acts;
                    npcs += "],\"lp\":" + std::to_string(lp) + ",\"lpMax\":" + std::to_string(lpMax) +
                            ",\"vis\":" + std::to_string(vis) +
                            ",\"target\":" + std::to_string(npcTarget) +
                            ",\"bar\":" + std::to_string(ovBar) + ",\"splats\":" + ovSplats + "}";
                    ++nc;
                }
            }
        }
    }
    struct Obj { int id, x, y, plane, type, dist; std::string name, acts; bool rt; bool vis;
                 int w = 1, h = 1;      // footprint in tiles, rotation-corrected; x/y = SW anchor
                 int mh = -1; };        // live model height in world-fine units; -1 = not live-tracked
    std::vector<Obj> objs;
    int rt_total = -1;   // live objects the client reported before merging; -1 = channel silent
    if (player_x >= 0) {
        if (obj_range < 1)   obj_range = 1;
        if (obj_range > 128) obj_range = 128;   // a full 8x8 instance floor
        constexpr int kCollectCap = 4000;   // safety bound before sorting
        std::unordered_set<long long> seen;
        HANDLE rh = h; std::uint64_t rroot = 0;
        if (h) { auto rv = rpm<std::uint64_t>(h, mgva); if (rv) rroot = *rv; }
        // One resolve per definition per build: a scene repeats the same few ids hundreds of times, and each
        // resolve reads the game's vars to pick the shown variant, which cannot change within one build.
        std::unordered_map<int, rtx::cache::LocMeta> locMemo;
        auto loc_meta = [&](int id) -> const rtx::cache::LocMeta& {
            auto it = locMemo.find(id);
            if (it == locMemo.end()) it = locMemo.emplace(id, resolve_loc(rh, rroot, id)).first;
            return it->second;
        };
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
                    const auto& meta = loc_meta(p.id);          // varbit-aware (live morph state)
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
        // The cache entries by id and plane, so each live object is held against its own few candidates
        // rather than the whole list.
        std::unordered_map<long long, std::vector<std::size_t>> cacheById;
        for (std::size_t i = 0; i < objs.size(); ++i)
            cacheById[((long long)objs[i].id << 4) | (objs[i].plane & 15)].push_back(i);
        std::vector<RuntimeObj> runtime;
        const bool rt_ok = ReadRuntimeObjects(pid, runtime);
        rt_total = rt_ok ? (int)runtime.size() : -1;
        if (rt_ok) {
            for (const auto& r : runtime) {
                if ((int)objs.size() >= kCollectCap) break;
                if (r.config_id <= 0 || r.plane != player_plane) continue;
                int ax = r.x - player_x, ay = r.y - player_y;
                if (ax < 0) ax = -ax;
                if (ay < 0) ay = -ay;
                int dist = ax > ay ? ax : ay;
                if (dist > obj_range) continue;
                const auto& meta = loc_meta(r.config_id);
                if (meta.name.empty()) continue;
                long long dk = ((long long)r.config_id << 40) | ((long long)r.x << 20) | (unsigned)r.y;
                int tol = std::max(meta.dim_x, meta.dim_y) - 1; if (tol < 0) tol = 0;
                bool dup = !seen.insert(dk).second;          // already have it from the cache (exact tile)
                // The cache lists a loc whether or not the game is showing it; the live entity knows.
                // A depleted tree (and a stump waiting to be shown) carries the hidden flag, so the
                // cache entry's vis follows the live flag. Match on id, plane and footprint tolerance.
                auto cand = cacheById.find(((long long)r.config_id << 4) | (r.plane & 15));
                if (cand != cacheById.end()) for (std::size_t ci : cand->second) {
                    auto& o = objs[ci];
                    if (!o.rt && o.id == r.config_id && o.plane == r.plane &&
                        std::abs(o.x - r.x) <= tol && std::abs(o.y - r.y) <= tol) {
                        dup = true;
                        if (r.hidden) o.vis = false;
                        if (r.bmax[2] > r.bmin[2]) o.mh = (int)(r.bmax[2] - r.bmin[2]);
                    }
                }
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
                const int mh = (r.bmax[2] > r.bmin[2]) ? (int)(r.bmax[2] - r.bmin[2]) : -1;
                objs.push_back({ r.config_id, ox, oy, r.plane, -1, dist, meta.name, std::move(acts), true,
                                 r.bmax[0] > r.bmin[0] && !r.hidden,   // degenerate AABB or hidden flag = not shown
                                 fw, fh, mh });
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
            "{\"id\":%d,\"x\":%d,\"y\":%d,\"plane\":%d,\"type\":%d,\"dist\":%d,\"w\":%d,\"h\":%d,\"mh\":%d,\"rt\":%s,\"vis\":%s,\"name\":\"",
            o.id, o.x, o.y, o.plane, o.type, o.dist, o.w, o.h, o.mh, o.rt ? "true" : "false",
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
           ",\"rtObjs\":" + std::to_string(rt_total) +
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
    std::uint64_t owner = r64(mainData + kOffIfaceOwner);
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
// The open group's object. The owner's vector is indexed by interface id (entry = begin + id * 16,
// {counter +0, object +8}, a null object = not open), so the entry is read directly and its id
// field checked; the linear scan remains for a vector that is not laid out that way.
static std::uint64_t iface_group_obj(HANDLE h, std::uint64_t main_data, int gid) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(h, main_data, gs, ge);
    if (!gs || gid < 0) return 0;
    if (gs + (std::uint64_t)gid * 0x10 + 0x10 <= ge) {
        std::uint64_t ap = r64(gs + (std::uint64_t)gid * 0x10 + 8);
        if (ap > 0x10000 && r32(ap) == gid) return ap;
        if (ap == 0) return 0;   // the slot exists and is empty: not open
    }
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
    std::uint64_t owner = rpm<std::uint64_t>(h, mainData + kOffIfaceOwner).value_or(0);
    if (owner <= 0x10000) return;
    std::uint64_t s = rpm<std::uint64_t>(h, owner + kIfaceGroupsBegin).value_or(0);
    std::uint64_t e = rpm<std::uint64_t>(h, owner + kIfaceGroupsEnd).value_or(0);
    if (s <= 0x10000 || e <= s || (e - s) % 0x10 || (e - s) > 0x200000) return;
    gs = s; ge = e;
}

// Companion-published var by (scope,id): 4 = varp/varbit, 5 = varc-int.
// One mapped view of each client's var share, kept between calls: a panel-rect pass asks for dozens of
// values at a time, and opening, mapping and unmapping 256 KB for each one cost far more than the read.
// The name carries the session, so a rebind (new name) opens the new section and drops the old view.
struct VarcView { std::wstring name; HANDLE h = nullptr; const rtx::varc::Share* sh = nullptr; };
static std::mutex s_varcViewMu;
static std::unordered_map<std::uint32_t, VarcView> s_varcViews;   // by pid, under s_varcViewMu

static const rtx::varc::Share* varc_view_locked(std::uint32_t pid) {
    wchar_t name[rtx::ipc::kNameChars];
    rtx::varc::MakeSectionName(pid, name);
    auto& v = s_varcViews[pid];
    if (v.sh && v.name == name) return v.sh;
    if (v.sh) UnmapViewOfFile(reinterpret_cast<LPCVOID>(v.sh));
    if (v.h) CloseHandle(v.h);
    v = VarcView{};
    if (s_varcViews.size() > 16) {   // clients come and go; views of old ones are not kept for ever
        for (auto& kv : s_varcViews) {
            if (kv.first == pid) continue;
            if (kv.second.sh) UnmapViewOfFile(reinterpret_cast<LPCVOID>(kv.second.sh));
            if (kv.second.h) CloseHandle(kv.second.h);
        }
        VarcView keep = v; s_varcViews.clear(); s_varcViews[pid] = keep;
    }
    auto& w = s_varcViews[pid];
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return nullptr;
    auto* sh = reinterpret_cast<const rtx::varc::Share*>(MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(rtx::varc::Share)));
    if (!sh) { CloseHandle(h); return nullptr; }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(sh, &mbi, sizeof(mbi)) || mbi.RegionSize < sizeof(rtx::varc::Share)) {   // a short section is not ours
        UnmapViewOfFile(reinterpret_cast<LPCVOID>(sh)); CloseHandle(h); return nullptr;
    }
    w.name = name; w.h = h; w.sh = sh;
    return sh;
}

static bool read_companion_var(std::uint32_t pid, int scope, int id, int& out) {
    std::lock_guard<std::mutex> lk(s_varcViewMu);
    const rtx::varc::Share* sh = varc_view_locked(pid);
    if (!sh) return false;
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
    return found;
}

// FALLBACK ONLY (see iface_panel_origin): origins now come from the engine's sub-interface table, which
// gives the exact parent-component rect for every attached group. This manual table is consulted only for
// groups the engine has not attached. An entry whose X/Y varcs (scope 5) are a HUD window's gets that
// window's content origin by the game's own rule (hud_window_content_origin); other entries take their vars
// or mount comp as the origin. off_left / off_top are per-panel nudges on top of that origin (a group that
// sits deeper than the window's content comp), never the window frame's inset.
struct PanelOriginSpec { int group; int var_x; int var_y; int off_left; int off_top; int mount_comp; bool is_varc; int req_group; };
static const PanelOriginSpec kPanelOrigins[] = {
    { 1477, -1, -1, 0, 0, 0 },    // HUD root / game frame: tree is screen-absolute, identity spec so "a" is emitted
    { 1188, 3082, 3083, 0, 0, 0, true },   // Choose dialog (option select) -- centered; position via live VARC 3082/3083
    { 1184, 3082, 3083, 0, 0, 0, true },   // NPC dialog
    { 1191, 3082, 3083, 0, 0, 0, true },   // Player dialog
    { 1189, 3082, 3083, 0, 0, 0, true },   // Clue continue
    { 1186, 3082, 3083, 0, 0, 0, true },   // Server message dialog
    { 1552, 3096, 3097, 0, 0, 0, true },   // Serenity posts pose-select; the Agility plugin adds its own (2,16) on top
    { 1603, 0, 0, 0, 0 },   // Input text -- no position var known yet
    { 1370, 3089, 3090, 0, 0, 0, true },   // Item Production content, always inside the 1371 frame (varcs 3089/3090)
    { 13,   3089, 3090, 0, 40, 0, true },  // Bank pin -- window varcs 3089/3090; off_top 40 keeps the earlier frame-based placement (not yet seen live)
    { 1466, 3166, 3167, 0, 0, 0, true },   // Skills; varc 3165 == 1 while open (draw gate for the XP bars)
    { 1224, 3089, 3090, 0, 0, 0, true },   // Ritual selection (Necromancy! communion ritual) -- window-frame varcs 3089/3090
    { 533,  3089, 3090, 0, 0, 0, true },   // Display case -- window-frame varcs 3089/3090, content centred in the frame
    { 1286, 3089, 3090, 0, 0, 0, true },   // Discovered Ratings (Fish Flingers) -- window-frame varcs 3089/3090
    // House Controls 1665: frame slot comp varies with docking, so key off its own varcs 3096/3097.
    { 1665, 3096, 3097, 0, 0, 0, true },
    { 1223, 3096, 3097, 0, 0, 0, true },   // Active ritual (Necromancy!) -- position varcs 3096/3097
    { 923,  3096, 3097, 0, 0, 0, true },   // Fish Flingers competition results -- position varcs 3096/3097
    { 919,  3047, 3048, 0, 0, 0, true },   // Fish Flingers live scoreboard -- varcs 3047/3048
    { 1222, 6463, 6464, 0, 0, 0, true },   // Well of Souls talent tree (Necromancy!) -- varcs 6463/6464 (central-overlay family)
    { 660,  0, 0, 0, -9 },  // Material storage -- no position var known yet
    { 656,  6310, 6311, 0, 0, 728, true }, // Museum / Training Weapons donation -- varcs 6310/6311, mount 1477:728 fallback
    { 691,  6463, 6464, 0, 0, 0, true },   // Relic power -- varcs 6463/6464 (central-overlay family)
    { 1594, 6463, 6464, 0, 0, 0, true },   // Shop (e.g. Ezreal's) -- varcs 6463/6464
    { 517,  5632, 5633, 0, 0, 0, true },   // Bank -- varcs 5632/5633
    { 190,  5846, 5847, 0, 0, 0, true },   // Quest -- varcs 5846/5847
    { 1092, 6463, 6464, 0, 0, 0, true },   // Lodestone network -- varcs 6463/6464
    { 584,  0, 0, 0, 0 }, // DXP timer -- no position var known yet
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
    { 1933, 3089, 3090, 0, 0, 0, true },   // Lockbox (master clue) -- varcs 3089/3090, no mount fallback
    { 1371, 3089, 3090, 0, 0, 0, true },   // Make-x / potion crafting -- varcs 3089/3090
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
// accumulated through the child vectors from the group's root widgets, and the own hide flag of every
// top-level comp. One walk per group per 100 ms, each node fetched as a single block, because the origin
// resolver asks for many game-frame slots a frame.
struct IfaceGroupPos { unsigned long long ms = 0; std::unordered_map<int, std::pair<int,int>> pos; std::unordered_map<int, bool> hid; };
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
        std::function<void(std::uint64_t,int,int,int,bool)> walk =
            [&](std::uint64_t node, int bx, int by, int depth, bool hidden) {
            if (depth > 14 || visited > 30000) return;
            std::uint8_t nb[0x210];
            if (!rpm_bytes(h, node, nb, sizeof(nb))) return;
            ++visited;
            auto i32 = [&](std::size_t o){ std::int32_t v; std::memcpy(&v, nb + o, 4); return (int)v; };
            auto i16 = [&](std::size_t o){ std::int16_t v; std::memcpy(&v, nb + o, 2); return (int)v; };
            auto u64 = [&](std::size_t o){ std::uint64_t v; std::memcpy(&v, nb + o, 8); return v; };
            const int ax = bx + i32(0x98), ay = by + i32(0x9c);
            if (i16(0x3c) == -1 && i32(0xa0) > 0 && i32(0xa4) > 0) gp.pos.emplace(i16(0x3a), std::make_pair(ax, ay));
            if (i16(0x3c) == -1) gp.hid.emplace(i16(0x3a), hidden);
            const std::size_t co[3] = { 0x1d0, 0x1b8, 0x200 };
            for (int k = 0; k < 3; ++k) {
                std::uint64_t cs = u64(co[k]), ce = u64(co[k] + 8);
                if (!cs || !ce || cs + 8 <= 0x10000 || ce <= cs || (ce - cs) > 0x100000) continue;
                vec.resize((std::size_t)(ce - cs));
                if (!rpm_bytes(h, cs, vec.data(), vec.size())) continue;
                std::vector<std::pair<std::uint64_t, bool>> kids;   // entry: +0 tag (bit 0 = hidden), +8 node
                for (std::size_t o = 0; o + 0x10 <= vec.size(); o += 0x18) {
                    std::uint64_t tag, ch; std::memcpy(&tag, vec.data() + o, 8); std::memcpy(&ch, vec.data() + o + 8, 8);
                    if (ch <= 0x10000 || ch >= 0x7ff000000000ull) continue;   // nodes are heap objects; an image pointer here is a union payload
                    std::int64_t d = (std::int64_t)(cs + o + 8) - (std::int64_t)ch; if (d < 0) d = -d;
                    if (d <= 0x3000) continue;                         // child lives in a separate alloc
                    kids.push_back({ ch, (tag & 1) != 0 });
                }
                for (const auto& kd : kids) walk(kd.first, ax, ay, depth + 1, kd.second);   // vec is reused by the callee
            }
        };
        for (std::uint64_t wn = a; wn + 0x18 <= b; wn += 0x18) {
            std::uint64_t nd = r64(wn);
            if (nd > 0x10000) walk(nd, 0, 0, 0, (r64(wn - 8) & 1) != 0);
        }
        break;
    }
    return gp;
}

// The walked group, at most 100 ms old; `use` runs under the snapshot lock.
static bool iface_group_pos(HANDLE h, std::uint64_t main_data, int gid, const std::function<bool(const IfaceGroupPos&)>& use) {
    static std::mutex mu;
    static std::unordered_map<std::uint64_t, IfaceGroupPos> cache;   // (main_data, gid) -> positions
    const std::uint64_t key = (main_data << 8) ^ (std::uint64_t)(std::uint32_t)gid;
    const unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = cache.find(key);
        if (it != cache.end() && now - it->second.ms < 100) return use(it->second);
    }
    IfaceGroupPos fresh = iface_walk_positions(h, main_data, gid);   // walk outside the lock
    fresh.ms = now;
    std::lock_guard<std::mutex> lk(mu);
    if (cache.size() > 512) cache.clear();
    IfaceGroupPos& gp = cache[key];
    gp = std::move(fresh);
    return use(gp);
}

static bool iface_comp_abs(HANDLE h, std::uint64_t main_data, int gid, int comp, int& ox, int& oy) {
    return iface_group_pos(h, main_data, gid, [&](const IfaceGroupPos& gp) {
        auto it = gp.pos.find(comp);
        if (it == gp.pos.end()) return false;
        ox = it->second.first; oy = it->second.second;
        return true;
    });
}

// Own hide flag of a top-level comp (what IF_GETHIDE reads); false when the comp is not in the tree.
static bool iface_comp_hidden(HANDLE h, std::uint64_t main_data, int gid, int comp, bool& hidden) {
    return iface_group_pos(h, main_data, gid, [&](const IfaceGroupPos& gp) {
        auto it = gp.hid.find(comp);
        if (it == gp.hid.end()) return false;
        hidden = it->second;
        return true;
    });
}

static bool read_iface_mount_origin(HANDLE h, std::uint64_t main_data, int mount_comp, int& ox, int& oy) {
    return iface_comp_abs(h, main_data, 1477, mount_comp, ox, oy);
}

// Sub-interface table (950-1). The interface owner keeps a hash map of every attached sub-interface, keyed by
// the PARENT component hash ((group << 16) | comp): owner+0xE0 = bucket array, owner+0xE8 = bucket count,
// owner+0xF0 = entry count. Entry: +0 u32 parent hash, +8 ref-counted holder, +0x10 attachment node, +0x18 next.
// Attachment node: +8 i32 state (2 = closed / detached), +0xC i32 sub group id, +0x10 u32 parent hash.
// Inbound packets 0x2D (move sub, two hashes) and 0x45 (close sub, one hash) resolve through this same map
// (exe+0x0E2080 / exe+0x1A0840 / exe+0x1A09F0), so it is the engine's own answer to "where is group G
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
        std::uint64_t owner = r64(main_data + kOffIfaceOwner);
        // the table rehashes as groups open: the header is read once, the walk dropped when it moved
        for (int attempt = 0; owner > 0x10000 && attempt < 2; ++attempt) {
            HashHdr hdr;
            if (!read_hash_hdr(h, owner + kIfaceSubBuckets - 8, hdr, 65536)) break;
            const std::vector<std::uint64_t> heads = read_hash_buckets(h, hdr);
            int total = 0;
            for (std::size_t i = 0; i < heads.size() && total < 4096; ++i) {
                std::uint64_t e = heads[i];
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
            if (hash_hdr_same(h, owner + kIfaceSubBuckets - 8, hdr)) break;
            sn.map.clear();
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
    const std::uint64_t ap = iface_group_obj(h, main_data, gid);
    if (!ap) return false;
    std::uint64_t ws = r64(ap + 0x20), we = r64(ap + 0x28);
    std::uint64_t a = ws + 8, b = we + 8;
    return ws && we && a > 0x10000 && b > a && (b - a) <= 0x100000;
}

// HUD windows (the panel registry, enum 7716). In the custom layouts client script 8701 keeps eight varcs per
// window: x, y, width, height first and transparency eighth. { slot, x, y, w, h, transparency } for every slot.
struct HudWindowVarcs { int slot; int var_x; int var_y; int var_w; int var_h; int var_trans; };
static const HudWindowVarcs kHudWindowVarcs[] = {
    { 0, 3166, 3167, 3162, 3163, 5520 }, { 2, 3040, 3041, 3036, 3037, 5502 }, { 3, 3075, 3076, 3071, 3072, 5507 }, { 4, 3173, 3174, 3169, 3170, 5521 }, { 5, 3131, 3132, 3127, 3128, 5515 },
    { 6, 3117, 3118, 3113, 3114, 5513 }, { 7, 3124, 3125, 3120, 3121, 5514 }, { 8, 3138, 3139, 3134, 3135, 5516 }, { 9, 3159, 3160, 3155, 3156, 5519 }, { 10, 3187, 3188, 3183, 3184, 5523 },
    { 11, 3194, 3195, 3190, 3191, 5524 }, { 12, 3103, 3104, 3099, 3100, 5511 }, { 14, 3201, 3202, 3197, 3198, 5525 }, { 15, 5611, 5612, 5607, 5608, 5533 }, { 16, 3208, 3209, 3204, 3205, 5526 },
    { 17, 3096, 3097, 3092, 3093, 5510 }, { 18, 3033, 3034, 3029, 3030, 5501 }, { 19, 3054, 3055, 3050, 3051, 5504 }, { 20, 3061, 3062, 3057, 3058, 5505 }, { 21, 3068, 3069, 3064, 3065, 5506 },
    { 22, 3180, 3181, 3176, 3177, 5522 }, { 23, 5653, 5654, 5649, 5650, 5539 }, { 24, 5688, 5689, 5684, 5685, 5544 }, { 25, 5695, 5696, 5691, 5692, 5545 }, { 26, 5681, 5682, 5677, 5678, 5543 },
    { 27, 5709, 5710, 5705, 5706, 5547 }, { 28, 5737, 5738, 5733, 5734, 5551 }, { 29, 5807, 5808, 5803, 5804, 5561 }, { 30, 5821, 5822, 5817, 5818, 5563 }, { 31, 5846, 5847, 5842, 5843, 5849 },
    { 32, 6011, 6012, 6007, 6008, 6014 }, { 33, 6140, 6141, 6136, 6137, 6143 }, { 34, 6148, 6149, 6144, 6145, 6151 }, { 35, 6156, 6157, 6152, 6153, 6159 }, { 36, 6164, 6165, 6160, 6161, 6167 },
    { 39, 6172, 6173, 6168, 6169, 6175 }, { 40, 6180, 6181, 6176, 6177, 6183 }, { 41, 6283, 6284, 6279, 6280, 6286 }, { 42, 7172, 7173, 7168, 7169, 7175 }, { 43, 7180, 7181, 7176, 7177, 7183 },
    { 44, 7188, 7189, 7184, 7185, 7191 }, { 45, 8190, 8191, 8186, 8187, 8193 }, { 46, 8198, 8199, 8194, 8195, 8201 }, { 1000, 3005, 3006, 3001, 3002, 5497 }, { 1001, 5646, 5647, 5642, 5643, 5538 },
    { 1002, 3012, 3013, 3008, 3009, 5498 }, { 1003, 3026, 3027, 3022, 3023, 5500 }, { 1004, 3019, 3020, 3015, 3016, 5499 }, { 1005, 3047, 3048, 3043, 3044, 5503 }, { 1006, 3082, 3083, 3078, 3079, 5508 },
    { 1007, 3089, 3090, 3085, 3086, 5509 }, { 1008, 3152, 3153, 3148, 3149, 5518 }, { 1009, 5569, 5570, 5565, 5566, 5527 }, { 1010, 5576, 5577, 5572, 5573, 5528 }, { 1012, 5590, 5591, 5586, 5587, 5530 },
    { 1013, 5597, 5598, 5593, 5594, 5531 }, { 1014, 5604, 5605, 5600, 5601, 5532 }, { 1015, 5618, 5619, 5614, 5615, 5534 }, { 1016, 5625, 5626, 5621, 5622, 5535 }, { 1017, 5632, 5633, 5628, 5629, 5536 },
    { 1018, 5639, 5640, 5635, 5636, 5537 }, { 1019, 3110, 3111, 3106, 3107, 5512 }, { 1021, 5674, 5675, 5670, 5671, 5542 }, { 1023, 5702, 5703, 5698, 5699, 5546 }, { 1024, 5716, 5717, 5712, 5713, 5548 },
    { 1025, 5723, 5724, 5719, 5720, 5549 }, { 1026, 5730, 5731, 5726, 5727, 5550 }, { 1027, 5744, 5745, 5740, 5741, 5552 }, { 1028, 5751, 5752, 5747, 5748, 5553 }, { 1029, 5758, 5759, 5754, 5755, 5554 },
    { 1030, 5765, 5766, 5761, 5762, 5555 }, { 1031, 5772, 5773, 5768, 5769, 5556 }, { 1032, 5779, 5780, 5775, 5776, 5557 }, { 1033, 5786, 5787, 5782, 5783, 5558 }, { 1034, 5793, 5794, 5789, 5790, 5559 },
    { 1035, 5800, 5801, 5796, 5797, 5560 }, { 1036, 5814, 5815, 5810, 5811, 5562 }, { 1037, 5828, 5829, 5824, 5825, 5564 }, { 1038, 5953, 5954, 5949, 5950, 5956 }, { 1039, 6052, 6053, 6048, 6049, 6055 },
    { 1040, 6310, 6311, 6306, 6307, 6313 }, { 1041, 6329, 6330, 6325, 6326, 6332 }, { 1045, 6423, 6424, 6419, 6420, 6426 }, { 1047, 6463, 6464, 6459, 6460, 6466 }, { 1049, 6627, 6628, 6623, 6624, 6630 },
    { 1050, 6939, 6940, 6935, 6936, 6942 }, { 1051, 7018, 7019, 7014, 7015, 7021 }, { 1052, 7369, 7370, 7365, 7366, 7372 }, { 1053, 7390, 7391, 7386, 7387, 7393 }, { 2008, 5667, 5668, 5663, 5664, 5541 },
};

static const HudWindowVarcs* hud_window_by_varcs(int var_x, int var_y) {
    for (const auto& w : kHudWindowVarcs) if (w.var_x == var_x && w.var_y == var_y) return &w;
    return nullptr;
}

static int read_player_varbit(HANDLE h, std::uint64_t root, int id) {
    int v = 0;
    read_varbit_live(h, root, id, v);
    return v;
}

// Content origin of a HUD window, placed the way the game places it. Client script 8781 puts the window frame
// (panel struct param 3503) at the window's x/y varcs, relative to the frame's parent, and client script 8391
// puts the content comp (3505), where the window's group is mounted, at the inset client script 20543 takes
// from the window style and from whether the border (3506) and the tab strip (3509) are shown:
//   left = border ? 3550 : 0
//   top  = (border ? 3547 : 0) + (strip, unless style 8296 is 2 ? 3577 + 3586 : border ? 3586 : 0)
// The style is client script 8418's: param 3518, through the theme map (enum 9014 by varbit 22875), then its
// 3795 form when title bars hide (varbit 19928 with the interface locked: varbit 19925 outside edit mode; or
// a transparent window that allows it, param 5770), else its 3794 form for slim headers (varbit 19924).
// The shown flags are the comps' live hide flags; unreadable ones are taken as drawn outside edit mode
// (border unless param 3533, strip when the window takes tabs, param 3521). Client script 8391's exceptions:
// the central interface (slot 1007) centres its content in the frame, a border-less panel (param 3533) puts it
// at the frame's corner in edit mode, and All Chat (struct 21279) is placed by client script 20504 in the legacy
// layout or with varbit 60441. The management window (slot 1001) is left out: the management interface it holds
// places its content (client script 8288 centres a 742 x 450 box 10 px above the frame's bottom). The legacy
// layout (varbit 27169) places frames by script, not from the varcs, so there the frame's live position stands in.
// The alternate interface (varbit 38842) has styles of its own and is not covered. `contentComp` is set once the
// panel is known, also when the origin is not.
static bool hud_window_content_origin(HANDLE h, std::uint64_t md, std::uint32_t pid, const HudWindowVarcs& win,
                                      int& ox, int& oy, int* contentComp = nullptr) {
    if (win.slot == 1001 || read_player_varbit(h, md, 38842)) return false;
    auto P = [](int st, int key, int dflt) { return rtx::cache::StructIntParamOr(st, key, dflt); };
    int panel = rtx::cache::EnumIntValue(7716, win.slot, -1);                     // client script 10405
    int swap = -1;
    if (win.slot == 17 && read_varp_found(h, md, 11967, swap) && swap != -1) panel = swap;
    if (panel < 0) return false;
    if (contentComp) *contentComp = P(panel, 3505, -1);
    const int frame = P(panel, 3503, -1);
    if ((frame >> 16) != 1477) return false;
    const bool legacy = read_player_varbit(h, md, 27169) != 0;
    int fx = 0, fy = 0;
    if (legacy) {
        if (!iface_comp_abs(h, md, 1477, frame & 0xFFFF, fx, fy)) return false;
    } else {
        if (!read_panel_pos_var(h, md, pid, win.var_x, true, fx) || !read_panel_pos_var(h, md, pid, win.var_y, true, fy)) return false;
        rtx::cache::IfaceCompDefLite def;
        int px = 0, py = 0;
        if (rtx::cache::IfaceCompDefLookup(1477, frame & 0xFFFF, def) && def.parent >= 0 &&
            iface_comp_abs(h, md, 1477, def.parent, px, py)) { fx += px; fy += py; }
    }
    int edit = 0;
    read_varc_found(h, md, 3477, edit);
    int style = P(panel, 3518, -1);
    if (panel == 21278 && read_player_varbit(h, md, 60446)) {
        style = 28517;                                                             // the minimap's own frame
    } else if (style >= 0) {
        const int theme = read_player_varbit(h, md, 22875);
        if (theme != 0) {
            const int map = rtx::cache::EnumIntValue(9014, theme, -1);
            const int themed = map >= 0 ? rtx::cache::EnumIntValue(map, style, -1) : -1;
            if (themed >= 0) style = themed;
        }
        int trans = 0;
        const bool locked = read_player_varbit(h, md, 19925) && (edit == 0 || legacy);
        const bool faded = P(panel, 5770, 0) && read_panel_pos_var(h, md, pid, win.var_trans, true, trans) && trans != 0;
        const int bare = P(style, 3795, -1), slim = P(style, 3794, -1);
        if ((read_player_varbit(h, md, 19928) && locked) || faded) { if (bare >= 0) style = bare; }
        else if (read_player_varbit(h, md, 19924)) { if (slim >= 0) style = slim; }
    }
    if (style < 0) return false;
    auto shown = [&](int comp, bool dflt) {
        bool hid = false;
        if ((comp >> 16) != 1477) return false;
        return iface_comp_hidden(h, md, 1477, comp & 0xFFFF, hid) ? !hid : dflt;
    };
    const bool strip = shown(P(panel, 3509, -1), P(panel, 3521, 0) != 0);
    int left = 0, top = 0;
    if (win.slot == 1007) {
        // content sized from enum 7720 by varc 3678 (512 x 334 while that is unset), frame from the w/h varcs
        int cw = 512, ch = 334, fw = 0, fh = 0, key = 0;
        if (read_varc_found(h, md, 3678, key)) {
            const int sz = rtx::cache::EnumIntValue(7720, key, -1);
            if (sz >= 0) { cw = P(sz, 3638, cw); ch = P(sz, 3639, ch); }
        }
        if (!read_panel_pos_var(h, md, pid, win.var_w, true, fw) || !read_panel_pos_var(h, md, pid, win.var_h, true, fh)) return false;
        left = (fw - cw) / 2; top = (fh - ch) / 2;
    } else if (edit != 0 && P(panel, 3533, 0)) {
        // the frame's corner
    } else if (panel == 21279 && (legacy || read_player_varbit(h, md, 60441))) {
        // client script 20504: 4 px down in the legacy layout, else 4 px above the window's whole vertical inset
        if (legacy) top = 4;
        else {
            const bool border = shown(P(panel, 3506, -1), true);
            if (strip && P(style, 8296, 0) == 2) return false;   // that inset counts live comp heights
            int total = border ? P(style, 3547, 0) + P(style, 3549, 0) : 0;
            if (strip) total += P(style, 3577, 0) + P(style, 3586, 0);
            else if (border) total += P(style, 3586, 0);
            top = total - 4;
        }
    } else {
        const bool border = shown(P(panel, 3506, -1), P(panel, 3533, 0) == 0);
        if (border) { left = P(style, 3550, 0); top = P(style, 3547, 0); }
        if (strip && P(style, 8296, 0) != 2) top += P(style, 3577, 0) + P(style, 3586, 0);
        else if (border) top += P(style, 3586, 0);
    }
    ox = fx + left; oy = fy + top;
    return true;
}

// Fallback origin of `gid` when the engine has not attached it: the manual table, then the cache's panel
// mounts. No live calibration nudge.
static bool panel_fallback_origin(HANDLE h, std::uint64_t main_data, std::uint32_t pid, int gid, int& ox, int& oy) {
    for (const auto& s : kPanelOrigins) {
        if (s.group != gid) continue;
        if (s.req_group && !iface_group_open(h, main_data, s.req_group)) continue;   // variant gate
        int bx = 0, by = 0; bool ok = false;
        if (s.var_x < 0 && s.var_y < 0) {
            ok = true;   // identity spec: the group's tree is already screen-absolute
        } else if (const HudWindowVarcs* win = s.is_varc ? hud_window_by_varcs(s.var_x, s.var_y) : nullptr) {
            ok = hud_window_content_origin(h, main_data, pid, *win, bx, by);
        } else if (s.var_x > 0 && s.var_y > 0) {
            ok = read_panel_pos_var(h, main_data, pid, s.var_x, s.is_varc, bx) &&
                 read_panel_pos_var(h, main_data, pid, s.var_y, s.is_varc, by);
        }
        if (!ok && s.mount_comp) ok = read_iface_mount_origin(h, main_data, s.mount_comp, bx, by);
        if (!ok) continue;   // this spec can't resolve -> try the group's next spec
        ox = bx - s.off_left; oy = by - s.off_top;
        return true;
    }
    // Cache-driven fallback: HUD panel registry (enum 7716) params 3514-3517 pack content comps as
    // (group<<16)|sub and param 3503 the 1477 mount comp. Only runs when the table missed.
    static constexpr bool kCachePanelMounts = true;
    if (kCachePanelMounts) {
        int mc = rtx::cache::PanelMountComp(gid);
        if (mc > 0 && read_iface_mount_origin(h, main_data, mc, ox, oy)) return true;
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
    // 2. Fallback: manual table, then the cache's panel mounts (groups the engine did not attach).
    if (!panel_fallback_origin(h, main_data, pid, gid, ox, oy)) return false;
    { std::lock_guard<std::mutex> lk(g_ifaceOffMu);   // live calibration nudge from the Interfaces tab
      auto ov = g_ifaceOff.find(gid);
      if (ov != g_ifaceOff.end()) { ox += ov->second.first; oy += ov->second.second; } }
    return true;
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
        if (s.var_w) { if (!read_companion_var(pid, 5, s.var_w, W) && !read_companion_var(pid, 4, s.var_w, W)) W = 0; }
        if (s.var_h) { if (!read_companion_var(pid, 5, s.var_h, H) && !read_companion_var(pid, 4, s.var_h, H)) H = 0; }
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

// Just the ids of the open interface groups ("[1477,1430,...]"): what a solver needs to know whether its
// puzzle is up, without the per-group mount and origin work InterfaceGroupsJson does.
std::string InterfaceGroupIdsJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "[]";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "[]";
    std::uint64_t gs, ge; iface_groups_range(h, *root, gs, ge);
    if (!gs) return "[]";
    std::string out = "["; bool first = true;
    for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
        std::uint64_t ap2 = rpm<std::uint64_t>(h, g + 8).value_or(0);
        if (ap2 <= 0x10000) continue;
        int gid = rpm<std::int32_t>(h, ap2).value_or(0);
        if (gid <= 0 || gid > 70000) continue;
        out += first ? "" : ","; first = false;
        out += std::to_string(gid);
    }
    return out + "]";
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
    constexpr std::uint64_t kRecBase = 0x90, kRecSize = 0x2c, kSlots = 8;
    // Fine coordinate = 0x100 + tile*512; must land on a tile centre.
    auto fine_to_tile = [](float f) -> int {
        if (!(f > 0.0f) || f > 16383.0f * 512.0f + 512.0f) return -1;
        double t = ((double)f - 256.0) / 512.0;
        int ti = (int)(t + 0.5);
        if (ti <= 0 || ti > 16383) return -1;
        if (t - (double)ti > 0.02 || (double)ti - t > 0.02) return -1;   // must sit ON a tile centre
        return ti;
    };
    std::uint64_t bases[2] = { root + kOffArrowMgr, 0 };
    if (auto pv = rpm<std::uint64_t>(h, root + kOffArrowMgr); pv && *pv > 0x10000) bases[1] = *pv;
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
    constexpr std::uint64_t kHoverSlot = 0x13F8;
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
        constexpr std::uint64_t kContainer = rtx::scn::kContainer, kActiveIdx = 0x70, kEntryArr = 0x58,
                                kEntryWv = 0x8, kVecBegin = 0x138, kVecEnd = 0x140,
                                kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType, kUid = rtx::scn::kUid,
                                kPosX = 0x270, kPosY = 0x278;
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
                    int cfg = (t == 1) ? rpm<std::int32_t>(h, *sec + rtx::scn::kNpcCur).value_or(-1) : -1;
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
                    std::string stats;
                    if (t == 1) {   // the NPC's combat stats: current and base, and the level the game shows
                        NpcStats ns;
                        if (read_npc_stats(h, *sec, ns)) {
                            stats = ",\"stats\":[";
                            for (int k = 0; k < 7; ++k) stats += (k ? "," : "") + std::to_string(ns.cur[k]);
                            stats += "],\"base\":[";
                            for (int k = 0; k < 7; ++k) stats += (k ? "," : "") + std::to_string(ns.base[k]);
                            stats += "],\"vis\":" + std::to_string(ns.vis);
                        }
                    }
                    return out + buf + stats + ",\"name\":\"" + json_escape(sname.empty() ? name : sname) + "\"}";
                }
            }
        }
    }
    return out + ",\"name\":\"" + json_escape(name) + "\"}";
}

bool HoverLoc(std::uint32_t pid, HoverLocInfo& out) {
    out = HoverLocInfo{};
    const std::string j = HoverEntityJson(pid);
    auto num = [&](const char* key, int& v) {
        auto p = j.find(key);
        if (p == std::string::npos) return false;
        v = std::atoi(j.c_str() + p + std::strlen(key));
        return true;
    };
    if (j.find("\"kind\":\"item\"") != std::string::npos) {
        int id = -1;
        if (num("\"id\":", id) && num("\"slot\":", out.item_slot) && num("\"iface\":", out.item_iface) && num("\"comp\":", out.item_comp))
            out.item_id = id;
        return false;
    }
    if (j.find("\"kind\":\"npc\"") != std::string::npos) {
        int id = -1, uid = -1;
        if (num("\"id\":", id) && num("\"uid\":", uid) && id >= 0 && uid >= 0) { out.npc_id = id; out.npc_uid = uid; }
        return false;
    }
    if (j.find("\"kind\":\"loc\"") == std::string::npos) return false;
    {
        const char* key = "\"verb\":\"";
        auto p = j.find(key);
        if (p != std::string::npos) {
            p += std::strlen(key);
            auto e = j.find('"', p);
            if (e != std::string::npos) out.verb = j.substr(p, e - p);
        }
    }
    if (!num("\"id\":", out.id) || !num("\"x\":", out.x) || !num("\"y\":", out.y)) return false;

    // The hovered tile is any tile of the loc. A scene object stands on the centre of its
    // footprint (a tile centre for an odd size, a tile corner for an even one), so the object
    // meant is the nearest one of this loc whose footprint reaches the hovered tile; failing
    // that, the nearest of any id, which is the case for a loc that changes with a var.
    std::vector<RuntimeObj> objs;
    if (!ReadRuntimeObjects(pid, objs)) return true;
    const RuntimeObj* best = nullptr;
    float bestD = 1e30f;
    const auto meta = rtx::cache::GetLoc(out.id);
    const float hx = (out.x + 0.5f) * 512.f, hy = (out.y + 0.5f) * 512.f;
    const float reach = (float)std::max(meta.dim_x, meta.dim_y) * 256.f + 1.f;
    for (int pass = 0; pass < 2 && !best; ++pass)
        for (const auto& r : objs) {
            if (r.hidden) continue;
            if (pass == 0 && r.config_id != out.id) continue;
            const float cx = ((float)r.x + ((meta.dim_x & 1) ? 0.5f : 0.0f)) * 512.f;
            const float cy = ((float)r.y + ((meta.dim_y & 1) ? 0.5f : 0.0f)) * 512.f;
            if (std::fabs(cx - hx) > reach || std::fabs(cy - hy) > reach) continue;
            const float d = (cx - hx) * (cx - hx) + (cy - hy) * (cy - hy);
            if (d < bestD) { bestD = d; best = &r; }
        }
    if (best) {
        out.in_scene = true;
        out.scene_x = best->x; out.scene_y = best->y; out.scene_id = best->config_id;
    }
    return true;
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
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28), a = ws + 8, b = we + 8;
    // The box can close between the group match and this read, leaving garbage bounds behind.
    if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) return kEmpty;
    for (std::uint64_t w = a; w + 0x18 <= b && sp < 4096; w += 0x18) { std::uint64_t v = r64(w); if (v > 0x10000) stack[sp++] = v; }
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
    int visited = 0;   // a tree read out of a freed interface can loop; the real one is far smaller
    std::function<void(std::uint64_t,int,int,int)> find =
        [&](std::uint64_t node, int bx, int by, int depth) {
        if (grid || depth > 14 || visited++ > 30000) return;
        int ax = bx + r32(node + 0x98), ay = by + r32(node + 0x9c);
        if (r16s(node + 0x3a) == 18 && r16s(node + 0x3c) == -1) { grid = node; gx = ax; gy = ay; return; }
        { std::vector<IfaceChildRef> kids_; iface_child_refs(h, node, kids_);
          for (const auto& kid_ : kids_) { if (!(true && !grid && !grid)) break; find(kid_.addr, ax, ay, depth + 1); } }
    };
    std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28), a = ws + 8, b = we + 8;
    if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) return kEmpty;
    for (std::uint64_t w = a; w + 0x18 <= b && !grid; w += 0x18) { std::uint64_t nd = r64(w); if (nd > 0x10000) find(nd, ox, oy, 0); }
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
        // per client, or two open puzzles log on every pass
        static std::unordered_map<std::uint32_t, std::array<int, 4>> l_seen;
        const std::array<int, 4> cur{ ox, oy, gx, gy };
        auto seen = l_seen.find(pid);
        if (seen == l_seen.end() || seen->second != cur) {
            if (l_seen.size() > 32) l_seen.clear();
            l_seen[pid] = cur;
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
    int px = ox, py = oy;   // panel box origin: the content origin, or the frame once one is found by size

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
        bool atFrame = false;
        if (!exactOrigin && contentW > 0 && contentH > 0 &&
            iface_live_frame_origin(h, gs, ge, px, py, contentW + 1, contentW + 119,
                                    contentH + 1, contentH + 299, frameX, frameY, &panelW, &panelH)) {
            ox = frameX; oy = frameY; px = frameX; py = frameY; atFrame = true;
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
        // The engine and the window rule both give the content origin; only a frame found by size above needs
        // its chrome added back.
        int border = 0, header = 0;
        if (atFrame) {
            border = (panelW - contentW) / 2;
            header = (panelH - contentH) - border;
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
// The component under a rectangle of the screen, for hanging our own drawing off the game's own
// interface tree: the smallest visible node that contains the rectangle wins, and what comes back
// is the parent to put a child under (a dynamic child reports its own component, anything else its
// parent layer) with that parent's top-left corner, so the caller can give positions relative to it.
namespace {

struct LocateBest { bool ok = false; int depth = -1; long long area = 0; int parent = 0, px = 0, py = 0; };

// The rectangle is given in the coordinates of whatever it is attached to, so the two things that
// come back have to belong together: the component to attach under, and that component's corner.
// A dynamic child can hold children of its own, so it is its own attachment point; anything else
// attaches to the component it sits inside, which is the one this walk came through, not the
// `parent` field of the node itself (that is a sub index, not a component id).
void iface_locate_walk(HANDLE h, int group, std::uint64_t node, int depth,
                       int baseX, int baseY, int holderComp, bool hidden,
                       int qx, int qy, int qw, int qh, LocateBest& best, int& budget) {
    if (budget <= 0 || depth > 12) return;
    --budget;
    IfaceNode n;
    if (!iface_read_node(h, node, n)) return;
    const int ax = baseX + n.x, ay = baseY + n.y;
    const bool covers = !hidden && n.w > 0 && n.h > 0 &&
                        qx >= ax && qy >= ay && qx + qw <= ax + n.w && qy + qh <= ay + n.h;
    if (covers) {
        const long long area = (long long)n.w * (long long)n.h;
        // Smallest wins: a 40x36 slot must beat the 231x213 panel that happens to overlap the same
        // point on screen. Depth only settles a tie between two of the same size.
        const bool better = !best.ok || area < best.area || (area == best.area && depth > best.depth);
        if (better) {
            best.ok = true; best.depth = depth; best.area = area;
            // Ours becomes another child of whatever holds this node, so it is placed in that
            // holder's coordinates. A dynamic child carries its container's own component id and
            // tells itself apart by its sub index, so the holder is its component either way; only
            // a node with nothing above it holds ours itself.
            if (n.sub >= 0) {
                best.parent = (n.group << 16) | (n.comp & 0xFFFF);
                best.px = baseX; best.py = baseY;
            } else if (holderComp >= 0) {
                best.parent = (n.group << 16) | (holderComp & 0xFFFF);
                best.px = baseX; best.py = baseY;
            } else {
                best.parent = (n.group << 16) | (n.comp & 0xFFFF);
                best.px = ax; best.py = ay;
            }
        }
    }
    // Only go into a child that could hold the point. Without this the walk reads every node of
    // every open interface, which with a hundred of them open runs out of budget long before it
    // reaches the one under the cursor. A child with no size of its own is a pass-through and is
    // always followed, since it positions its own children freely.
    std::vector<IfaceChildRef> kids; iface_child_refs(h, n, kids);
    for (const auto& k : kids) {
        if (budget <= 0) break;
        IfaceNode c;
        if (!iface_read_node(h, k.addr, c)) continue;
        --budget;
        const int cx = ax + c.x, cy = ay + c.y;
        const bool passthrough = c.w <= 0 || c.h <= 0;
        const bool holds = qx >= cx && qy >= cy && qx + qw <= cx + c.w && qy + qh <= cy + c.h;
        if (!passthrough && !holds) continue;
        iface_locate_walk(h, group, k.addr, depth + 1, ax, ay, n.comp, hidden || k.hidden,
                          qx, qy, qw, qh, best, budget);
    }
}

}  // namespace

IfaceHit InterfaceLocate(std::uint32_t pid, int x, int y, int w, int h) {
    IfaceHit hit;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    auto ps = snap_proc(pid);
    if (!ps) return hit;
    HANDLE ph = ps.h;
    auto root = rpm<std::uint64_t>(ph, ps.mgva);
    if (!root || *root <= 0x10000) return hit;
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(ph, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(ph, a).value_or(0); };
    std::uint64_t gs, ge; iface_groups_range(ph, *root, gs, ge);
    if (!gs) return hit;

    LocateBest best;
    int budget = 20000;      // with the subtrees that cannot hold the point skipped, this is ample
    for (std::uint64_t g = gs; g + 0x10 <= ge && budget > 0; g += 0x10) {
        std::uint64_t ap2 = r64(g + 8);
        if (ap2 <= 0x10000) continue;
        const int gid = r32(ap2);
        if (gid <= 0) continue;
        int ox = 0, oy = 0;
        if (!iface_panel_origin(ph, *root, pid, gid, ox, oy)) continue;   // not on screen
        std::uint64_t ws = r64(ap2 + 0x20), we = r64(ap2 + 0x28);
        if (!ws || !we) continue;
        std::vector<IfaceChildRef> roots; iface_entry_refs(ph, ws, we, roots);
        for (const auto& rt : roots) {
            if (budget <= 0) break;
            iface_locate_walk(ph, gid, rt.addr, 0, ox, oy, -1, rt.hidden, x, y, w, h, best, budget);
        }
    }
    if (best.ok) { hit.ok = true; hit.parent = best.parent; hit.px = best.px; hit.py = best.py; }
    return hit;
}

// The game's own message store, which every chat line goes through whether or not the chat window
// shows it. The store keeps its messages in a hash map keyed by id (node +0x00 == +0x10); +0x08/+0x0C
// next/previous id; +0x14 client clock, +0x18 wall clock; +0x20 type; +0x24 news sub-kind, not a
// channel; sender as displayed with icon and title markup +0x28, with icon only +0x40, plain +0x58;
// clan or group name +0x70; +0x88 probably a quickchat phrase id, else -1; text +0x90.
// Layout read from the routine that writes it, and checked against the running game.
std::string chat_store_json(HANDLE h, std::uint64_t root, int want) {
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };

    const std::uint64_t store = r64(root + kOffChatStore);
    if (store <= 0x10000) return "[]";
    const std::int32_t next = r32(store + 0x18);          // the id the next message will take
    const std::uint64_t buckets = r64(store + 0x880);
    const std::uint64_t nbuckets = r64(store + 0x888);
    if (next <= 0 || buckets <= 0x10000 || nbuckets == 0 || nbuckets > 0x10000) return "[]";

    if (want <= 0 || want > 200) want = 200;
    // the newest record's key is next - 1 (the add takes the counter as the key, then advances it)
    std::int32_t from = next - want; if (from < 0) from = 0;

    std::string a = "[";
    bool first = true;
    for (std::int32_t k = next - 1; k >= from; --k) {
        const std::uint32_t key = (std::uint32_t)k;
        std::uint64_t node = r64(buckets + (std::uint64_t)(key % nbuckets) * 8);
        int guard = 0;
        while (node > 0x10000 && guard++ < 64) {
            if ((std::uint32_t)r32(node) == key) break;
            node = r64(node + 0xB8);                       // the next record in this bucket
        }
        if (node <= 0x10000 || guard > 64 || (std::uint32_t)r32(node) != key) continue;

        const std::int32_t rid  = r32(node + 0x10);
        const std::int32_t tick = r32(node + 0x14);
        const std::uint64_t when = r64(node + 0x18);
        const std::int32_t type = r32(node + 0x20);
        const std::int32_t chan = r32(node + 0x24);
        const std::string tagged = read_jagstring_long(h, node + 0x28, 96);
        const std::string name   = read_jagstring_long(h, node + 0x58, 96);
        const std::string clan   = read_jagstring_long(h, node + 0x70, 96);
        const std::string text   = read_jagstring_long(h, node + 0x90, 480);
        if (text.empty() && name.empty()) continue;

        a += first ? "" : ","; first = false;
        a += "{\"id\":" + std::to_string(rid) +
             ",\"tick\":" + std::to_string(tick) +
             ",\"t\":" + std::to_string((unsigned long long)when) +
             ",\"type\":" + std::to_string(type) +
             ",\"chan\":" + std::to_string(chan) +
             ",\"name\":\"" + name + "\"" +
             ",\"tagged\":\"" + tagged + "\"" +
             ",\"clan\":\"" + clan + "\"" +
             ",\"raw\":\"" + text + "\"}";
    }
    a += "]";
    return a;
}

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

    std::string storeArr = "[]";
    {
        auto ps = snap_proc(pid);
        if (ps) {
            auto root = rpm<std::uint64_t>(ps.h, ps.mgva);
            // the panel keeps every line it is given, so each poll only carries the recent tail
            if (root && *root > 0x10000) storeArr = chat_store_json(ps.h, *root, 60);
        }
    }

    std::string out = "{\"lines\":" + ifaceArr + ",\"store\":" + storeArr;
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
// A buff's name is game text: <nbsp> for a space, colour tags, and for a buff whose struct has no
// short name (52776 on 950-1) its whole description, lines joined by <br>. The first line is the
// name and the lines after it the description, both plain.
static void buff_name_lines(const std::string& s, std::string& name, std::string& desc) {
    std::vector<std::string> lines(1);
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '<') {
            const std::size_t e = s.find('>', i);
            if (e == std::string::npos) { lines.back() += s.substr(i); break; }
            std::string tag = s.substr(i + 1, e - i - 1);
            for (char& c : tag) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (tag == "br" || tag == "br/" || tag == "br /") lines.emplace_back();
            else if (tag == "nbsp") lines.back() += ' ';
            i = e + 1;
            continue;
        }
        lines.back() += s[i++];
    }
    name.clear(); desc.clear();
    for (auto& l : lines) {
        const std::size_t a = l.find_first_not_of(' '), b = l.find_last_not_of(' ');
        if (a == std::string::npos) continue;
        const std::string t = l.substr(a, b - a + 1);
        if (name.empty()) name = t;
        else desc += (desc.empty() ? "" : "\n") + t;
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
            std::string desc;
            { const std::string raw = name; buff_name_lines(raw, name, desc); }
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
                   (desc.empty() ? std::string() : ",\"desc\":\"" + json_escape(desc) + "\"") +
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


// Depth-first collect bound abilities under `node`: name at *(node+0xB8), ability id = the slot icon's
// sprite at +0x1A8 (the cooldown key; +0x188 through 949-5), item slots carry the item at +0x1D8.
static void abar_collect(HANDLE h, std::uint64_t node, std::uint64_t parent, std::uint64_t gp,
                         int depth, std::vector<AbarSlot>& dst) {
    if (depth > 14 || dst.size() >= 64) return;
    int id = rpm<std::int32_t>(h, node + 0x1a8).value_or(0);
    {
        const int slotItem = rpm<std::int32_t>(h, node + 0x1d8).value_or(0);
        if (slotItem > 0 && slotItem < 200000) id = slotItem;   // an item slot is keyed by its item
    }
    if (id > 0 && id < 0x100000) {
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
    // Engine wall-clock ms: the counter beside the MainData global (the 949 module RVA is a constant on 950-1).
    long long clock = (long long)rpm<std::uint64_t>(h, ps.mgva + kEngineClock).value_or(0);
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

// The var change-stamp map (read_var_stamps, beside VarpsDumpAllJson) is not a cooldown registry:
// the reader took it for one until 2026-10-09.

// Ability sprite id -> its {cast clock, ready clock} varc pair, from the cache's ability configs
// (the pairs client script 6506 keeps, by struct; the sprite id is the id the bar slots carry).
// Abilities on the shared global cooldown have no pair of their own and are kept as (0, 0), so a
// bar slot is still known as an ability.
static std::unordered_map<int, std::pair<int, int>> ability_cooldown_pairs() {
    static std::mutex mu;
    static std::size_t lastHash = 0;
    static std::unordered_map<int, std::pair<int, int>> map;
    const std::string json = rtx::cache::AbilityConfigsJson();
    const std::size_t hsh = std::hash<std::string>{}(json);
    std::lock_guard<std::mutex> lk(mu);
    if (hsh == lastHash && !map.empty()) return map;
    std::unordered_map<int, std::pair<int, int>> m;
    rtx::health::JVal j;
    if (rtx::health::ParseJson(json, j) && j.t == rtx::health::JVal::Obj) {
        for (const auto& kv : j.o) {
            if (kv.first == "_byId" || kv.second.t != rtx::health::JVal::Obj) continue;
            const int sprite = (int)kv.second.num("i", -1);
            const rtx::health::JVal* v = kv.second.get("v");
            if (sprite <= 0) continue;
            if (kv.second.get("g")) { m.emplace(sprite, std::make_pair(0, 0)); continue; }   // shared global cooldown: a known ability without a pair of its own
            if (!v || v->a.size() != 2) continue;
            const int a = std::atoi(v->a[0].s.c_str()), b = std::atoi(v->a[1].s.c_str());
            if (a > 0 && b > 0) m.emplace(sprite, std::make_pair(a, b));
        }
    }
    if (!m.empty()) { map.swap(m); lastHash = hsh; }
    return map;
}

// Cooldowns of the abilities on the action bars, from the cooldown varc pairs: the cast clock and
// the ready clock in client cycles (50/s); an ability is counting down while the client clock sits
// between them. remaining is in ms. `scan` reports how much of the bar the pairs cover.
struct CooldownScan { int barSlots = 0, known = 0, paired = 0, counting = 0; bool bars = false, pairs = false; };   // known: bar slots the ability configs name
static std::string cooldowns_json(HANDLE h, std::uint64_t root, std::uint64_t mgva, CooldownScan* scan) {
    const long long clock = (long long)rpm<std::uint64_t>(h, mgva + kEngineClock).value_or(0);
    const long long cycles = (long long)rpm<std::uint32_t>(h, root + kOffClientClock).value_or(0);
    std::string out = "{\"clock\":" + std::to_string(clock) + ",\"cycles\":" + std::to_string(cycles) + ",\"cooldowns\":[";
    const auto pairs = ability_cooldown_pairs();
    CooldownScan sc; sc.pairs = !pairs.empty();
    std::vector<AbarSlot> abis;
    for (int bar : { 1430, 1670, 1671, 1672, 1673 }) {
        const std::uint64_t ap = iface_group_obj(h, root, bar);
        if (!ap) continue;
        auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
        const std::uint64_t ws = r64(ap + 0x20), we = r64(ap + 0x28);
        const std::uint64_t a = ws + 8, b = we + 8;
        if (!ws || !we || a <= 0x10000 || b <= a || (b - a) > 0x100000) continue;
        sc.bars = true;
        for (std::uint64_t w = a; w + 0x18 <= b; w += 0x18) {
            const std::uint64_t nd = r64(w);
            if (nd > 0x10000) abar_collect(h, nd, 0, 0, 0, abis);
        }
    }
    bool first = true;
    for (const auto& s : abis) {
        if (s.item > 0) continue;   // item slots have no ability cooldown
        ++sc.barSlots;
        auto it = pairs.find(s.id);
        if (it == pairs.end()) continue;   // a prayer, spell or anything else the ability configs do not name
        ++sc.known;
        if (it->second.first <= 0) continue;   // shared global cooldown
        ++sc.paired;
        int cast = 0, ready = 0;
        if (!read_varc_found(h, root, it->second.first, cast) || !read_varc_found(h, root, it->second.second, ready)) continue;
        if (cast <= 0 || ready <= cast || cycles < cast || cycles >= ready || ready - cast > 360000) continue;
        ++sc.counting;
        out += first ? "" : ","; first = false;
        out += "{\"id\":" + std::to_string(s.id) + ",\"name\":\"" + s.name + "\",\"remaining\":" + std::to_string((ready - cycles) * 20) +
               ",\"castCycle\":" + std::to_string(cast) + ",\"readyCycle\":" + std::to_string(ready) + ",\"duration\":" + std::to_string((ready - cast) * 20) + "}";
    }
    out += "]}";
    if (scan) *scan = sc;
    return out;
}

std::string AbilityCooldownsJson(std::uint32_t pid) {
    const char* kEmpty = "{\"cooldowns\":[]}";
    auto ps = snap_proc(pid);
    if (!ps || !ps.mod_base) return kEmpty;
    auto rootv = rpm<std::uint64_t>(ps.h, ps.mgva);
    if (!rootv || *rootv <= 0x10000) return kEmpty;
    return cooldowns_json(ps.h, *rootv, ps.mgva, nullptr);
}

static void fill_view_metrics(HANDLE h, std::uint64_t rootv, std::uint32_t pid, OverlayFrame& out) {
    // Gameview rect from view-1000 varcs (x 3005, y 3006, w 3001, h 3002; physical pixels), cached ~2x/sec
    // per client: one shared cache made every pass a miss with two clients docked.
    {
        struct GvCache { unsigned long long ms = 0; int x = 0, y = 0, w = 0, h = 0, lcw = 0, lch = 0; float ui = 0.0f;
                         std::array<int, 7> logged{ -99999, -99999, -99999, -99999, -99999, -99999, -99999 }; bool fresh = true; };
        static std::mutex s_gvMu;   // the render thread and the click path both come here
        std::lock_guard<std::mutex> lk(s_gvMu);
        static std::unordered_map<std::uint32_t, GvCache> s_gv;
        if (s_gv.size() > 32) s_gv.clear();
        GvCache& c = s_gv[pid];
        unsigned long long nowms = GetTickCount64();
        if (c.fresh || nowms - c.ms > 500) {
            c.fresh = false;
            int gx = 0, gy = 0, gw = 0, gh = 0, lw = 0, lh = 0;
            const bool tree = (rootv && read_gameview_rect(h, rootv, gx, gy, gw, gh, &lw, &lh));
            c.lcw = lw; c.lch = lh;
            int vx = 0, vy = 0, vw = 0, vh = 0;
            if (rootv) {
                vx = read_varc(h, rootv, 3005); vy = read_varc(h, rootv, 3006);
                vw = read_varc(h, rootv, 3001); vh = read_varc(h, rootv, 3002);
            }
            if (vw > 0 && vh > 0)  { c.x = vx; c.y = vy; c.w = vw; c.h = vh; }
            else if (tree)         { c.x = gx; c.y = gy; c.w = gw; c.h = gh; }
            else                   { c.w = 0; c.h = 0; }
            c.ui = 0.0f;
            if (tree && gw > 0 && vw > 0) {
                const float r = (float)vw / (float)gw;
                if (r > 0.2f && r < 5.0f) c.ui = r;
            }
            const std::array<int, 7> cur{ vx, vy, vw, vh, gx, gw, gh };
            if (cur != c.logged) {
                c.logged = cur;
                char gb[224];
                std::snprintf(gb, sizeof(gb),
                    "[gv] varc=%d,%d %dx%d tree=%d,%d %dx%d root=%dx%d ui=%.3f (varc wins when set)",
                    vx, vy, vw, vh, gx, gy, gw, gh, lw, lh, (double)c.ui);
                rtx::log::Client(pid, gb);
            }
            c.ms = nowms;
        }
        out.gv_x = c.x; out.gv_y = c.y; out.gv_w = c.w; out.gv_h = c.h;
        out.lc_w = c.lcw; out.lc_h = c.lch;
        out.ui_scale = c.ui;
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
    out.matrix_addr = *wv + g_matrixOff.load(std::memory_order_relaxed);

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
                int cur = rpm<std::int32_t>(h, *sec + rtx::scn::kNpcCur).value_or(-1);
                if (cur < 0 || rtx::cache::GetNpc(cur).actions.empty()) continue;
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
        std::vector<LiveLoc> glocs;                       // scenery read straight from the world view
        ReadLiveLocs(pid, glocs);
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
                // Anchor the label where the game anchors its own overheads for this object: the
                // model's foot plus the height its class reports, which is what the game's bars and
                // hitsplats stack up from. A bounding box is not that height and overstates it badly
                // on some models.
                const LiveLoc* bestL = nullptr;
                int bestD2 = gs.snap_id ? 0x7fffffff : 2;
                for (const auto& L : glocs) {
                    if (L.plane != gs.plane) continue;
                    if (gs.snap_id && L.id != gs.snap_id) continue;
                    int dx = L.tx - gs.gx, dy = L.ty - gs.gy;
                    int d2 = dx * dx + dy * dy;
                    if (gs.snap_id && d2 > 4) continue;       // the named loc, on or beside the marked tile
                    if (d2 >= bestD2) continue;
                    bestD2 = d2; bestL = &L;
                }
                // A miss must not drop the label. The loc list is rebuilt every frame and a single
                // frame without this entity would blink the label out and back; hold the last
                // anchor for a moment instead, and only give up once it is really gone.
                // per client: two clients on the same guide would otherwise keep each other's held anchor alive
                struct Held { float p[3]; long long at; };
                static std::unordered_map<std::uint32_t, std::unordered_map<long long, Held>> s_holdBy;
                if (s_holdBy.size() > 32) s_holdBy.clear();
                auto& s_hold = s_holdBy[pid];
                const long long hk = ((long long)gs.snap_id << 32) ^ ((long long)gs.gx << 16) ^ gs.gy;
                const long long nowMs = (long long)GetTickCount64();
                float ax = 0, ay = 0, top = 0;
                if (bestL) {
                    ax = bestL->cx; ay = bestL->cz; top = bestL->ground + (float)bestL->head;
                    Held& e = s_hold[hk];
                    e.p[0] = ax; e.p[1] = ay; e.p[2] = top; e.at = nowMs;
                } else {
                    auto it = s_hold.find(hk);
                    if (it == s_hold.end() || nowMs - it->second.at > 1500) {
                        if (it != s_hold.end()) s_hold.erase(it);
                        continue;
                    }
                    ax = it->second.p[0]; ay = it->second.p[1]; top = it->second.p[2];
                }
                op.has_box3d = true;
                op.label_only = true;        // the caller marks the ground itself; this only lifts the label
                op.bmin[0] = op.bmax[0] = ax;
                op.bmin[1] = op.bmax[1] = ay;
                op.bmin[2] = op.bmax[2] = top;
                op.wx = ax; op.wy = ay; op.wz = top;
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
        // This runs every overlay frame: each region's placements are copied out of the cache once
        // and each loc id resolved once, not again for every object that shares them.
        std::unordered_map<std::uint64_t, std::vector<rtx::cache::LocPlacement>> oregions;
        auto regionLocs = [&](int rx, int ry) -> const std::vector<rtx::cache::LocPlacement>& {
            const std::uint64_t k = ((std::uint64_t)(std::uint32_t)rx << 32) | (std::uint32_t)ry;
            auto it = oregions.find(k);
            if (it == oregions.end()) it = oregions.emplace(k, rtx::cache::RegionLocations(rx, ry)).first;
            return it->second;
        };
        std::unordered_map<int, rtx::cache::LocMeta> omemo;
        auto locOf = [&](int cfg) -> const rtx::cache::LocMeta& {
            auto it = omemo.find(cfg);
            if (it == omemo.end()) it = omemo.emplace(cfg, resolve_loc(h, rroot, cfg)).first;
            return it->second;
        };

        std::vector<RuntimeObj> runtime;
        if (ReadRuntimeObjects(pid, runtime)) {
            for (const auto& r : runtime) {
                if (oc >= kMaxObjects) break;
                if (r.config_id <= 0 || r.plane != out.plane) continue;
                const auto& meta = locOf(r.config_id);
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
                    for (const auto& pl : regionLocs(r.x >> 6, r.y >> 6)) {
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
            for (const auto& p : regionLocs(rx, ry)) {
                if (oc >= kMaxObjects) break;
                if (p.plane != out.plane) continue;
                const auto& meta = locOf(p.id);                       // varbit-aware
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
    constexpr std::uint64_t kContainer = rtx::scn::kContainer, kActiveIdx = 0x70, kEntryArr = 0x58,
                            kEntryWv = 0x8, kVecBegin = 0x138,
                            kVecEnd = 0x140, kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType, kUid = rtx::scn::kUid,
                            kPosX = 0x270, kPosY = 0x278, kAnim = 0xA90, kLocalUid = 0x48;
    const std::uint64_t kPlayerData = kOffAccount;
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
                    ecfg = rpm<std::int32_t>(h, *sec + rtx::scn::kNpcCur).value_or(-1);
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

    // Head progress bar: overhead object psec+0xF08 -> head-bar vector (+0x28 .. +0x30) of 0x1B0
    // elements, cycle stamp +0x78 and fill 0..255 +0x7C. Only elements with a recent stamp are live.
    int progress = -1;
    {
        auto ov = rpm<std::uint64_t>(h, psec + rtx::scn::kOverhead);
        auto sb = (ov && *ov > 0x10000) ? rpm<std::uint64_t>(h, *ov + rtx::scn::kOvSlots) : std::nullopt;
        auto se = (ov && *ov > 0x10000) ? rpm<std::uint64_t>(h, *ov + rtx::scn::kOvSlots + 8) : std::nullopt;
        if (sb && se && *sb > 0x10000 && *se >= *sb && (*se - *sb) % rtx::scn::kBarStride == 0) {
            const int now = rpm<std::int32_t>(h, *root + kOffClientClock).value_or(0);
            int hpc = read_varp(h, *root, 13537), hpm = read_varp(h, *root, 13538);   // the local player's life points
            int hp255 = (hpm > 0) ? (int)((hpc * 255LL + hpm / 2) / hpm) : -999;      // health bar fill to exclude
            int firstNonHp = -1;
            const int n = (int)std::min<std::uint64_t>((*se - *sb) / rtx::scn::kBarStride, 8);
            for (int k = 0; k < n; ++k) {
                const std::uint64_t el = *sb + (std::uint64_t)k * rtx::scn::kBarStride;
                const int stamp = rpm<std::int32_t>(h, el + rtx::scn::kBarStamp).value_or(-1);
                const int fill = rpm<std::int32_t>(h, el + rtx::scn::kBarFill).value_or(-1);
                if (stamp <= 0 || stamp > now + 50 || now - stamp > 30000 || fill < 0 || fill > 255) continue;
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
        actor_overhead_json(h, psec, pSplats, pBar, 1);   // slot 1: the local player's health bar (slot 0 is adrenaline)
        int world = -1, mx = -1, my = -1, mb = 0, mods = 0, loadPct = -1, loadScreen = 0;
        if (root && *root > 0x10000) {
            auto w = rpm<std::uint64_t>(h, *root + kOffWorld);
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
            auto cam = rpm<std::uint64_t>(h, *root + kOffMapMgr);
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

// A display name the game keeps as a 24-byte string (inline up to 23 chars, else heap), with the
// NBSP the game writes between name parts as a space.
static std::string social_name(HANDLE h, std::uint64_t str) {
    std::string s = read_jagstring(h, str, 48), o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA0) { o.push_back(' '); ++i; }
        else if (c == 0xA0) o.push_back(' ');
        else if (c >= 0x20) o.push_back((char)c);
    }
    return o;
}

// Social state (950-1).
// Friends: [MD+0x19970] state i32 +0x10 (2 = loaded), entries +0x18..+0x20 of 0x78: name string +0,
// previous name +0x18, world i32 +0x30 (0 = offline), world name +0x38, rank i32 +0x50, notes +0x60.
// Ignores: entries +0x30..+0x38 of 0x50: name +0, previous name +0x18, notes +0x30, temporary u8 +0x48.
// Friends chat: [MD+0x198E0] channel name +0x08, owner +0x20, own rank i32 +0x38, kick rank +0x3C,
// users +0x40..+0x48 of 0x50: name +0, world i32 +0x30, rank i32 +0x34, display name +0x38.
// Player group: [[MD+0x19948]+8] name +8, max size i16 +0x22, owner slot i32 +0x90, members
// +0x60..+0x68 of 0xB0: name +0x18, last seen node i16 +0x32, online u8 +0x34, rank u8 +0x35,
// status i32 +0x38, team i32 +0x3C.
std::string SocialJson(std::uint32_t pid) {
    auto ps = snap_proc(pid);
    if (!ps) return "{\"in\":false}";
    HANDLE h = ps.h;
    auto root = rpm<std::uint64_t>(h, ps.mgva);
    if (!root || *root <= 0x10000) return "{\"in\":false}";
    auto heap = [](std::uint64_t p) { return p > 0x10000 && p <= 0x00007FFFFFFFFFFFull; };
    auto r64 = [&](std::uint64_t a){ return rpm<std::uint64_t>(h, a).value_or(0); };
    auto r32 = [&](std::uint64_t a){ return rpm<std::int32_t>(h, a).value_or(0); };
    // a {begin, end} vector of fixed-stride entries: count, or -1 when it does not read as one
    auto vec_count = [&](std::uint64_t at, std::uint64_t stride, std::uint64_t cap, std::uint64_t& begin) -> int {
        begin = r64(at); const std::uint64_t end = r64(at + 8);
        if (begin == 0 && end == 0) return 0;
        if (!heap(begin) || end < begin || (end - begin) % stride || (end - begin) / stride > cap) return -1;
        return (int)((end - begin) / stride);
    };
    int world = -1;
    {
        auto w = rpm<std::uint64_t>(h, *root + kOffWorld);
        auto w2 = (w && *w > 0x10000) ? rpm<std::uint64_t>(h, *w + 0x20) : std::nullopt;
        if (w2 && *w2 > 0x10000) world = rpm<std::int32_t>(h, *w2 + 8).value_or(-1);
    }
    std::string friends, ignores; int fc = 0, online = 0; bool loaded = false;
    const std::uint64_t fr = r64(*root + kOffFriends);
    if (heap(fr)) {
        loaded = r32(fr + 0x10) == 2;
        std::uint64_t b0 = 0;
        const int n = loaded ? vec_count(fr + 0x18, 0x78, 600, b0) : 0;
        for (int i = 0; i < n; ++i) {
            const std::uint64_t e = b0 + (std::uint64_t)i * 0x78;
            const std::string name = social_name(h, e);
            if (name.empty()) continue;
            int fw = r32(e + 0x30);
            if (fw < 0 || fw > 2000) fw = 0;   // lobby worlds read 1100 and up (world name "Lobby N")
            if (fw > 0) ++online;
            const int rank = r32(e + 0x50);
            if (fc) friends.push_back(',');
            friends += "{\"name\":\"" + json_escape(name) + "\",\"world\":" + std::to_string(fw) +
                       ",\"worldName\":\"" + json_escape(social_name(h, e + 0x38)) + "\",\"rank\":" + std::to_string(rank >= -1 && rank < 1000 ? rank : -1) +
                       ",\"prev\":\"" + json_escape(social_name(h, e + 0x18)) + "\",\"notes\":\"" + json_escape(social_name(h, e + 0x60)) + "\"}";
            ++fc;
        }
        std::uint64_t i0 = 0;
        const int ni = loaded ? vec_count(fr + 0x30, 0x50, 600, i0) : 0;
        for (int i = 0; i < ni; ++i) {
            const std::uint64_t e = i0 + (std::uint64_t)i * 0x50;
            const std::string name = social_name(h, e);
            if (name.empty()) continue;
            if (!ignores.empty()) ignores.push_back(',');
            ignores += "{\"name\":\"" + json_escape(name) + "\",\"prev\":\"" + json_escape(social_name(h, e + 0x18)) +
                       "\",\"notes\":\"" + json_escape(social_name(h, e + 0x30)) + "\",\"temporary\":" + (rpm<std::uint8_t>(h, e + 0x48).value_or(0) ? "true" : "false") + "}";
        }
    }
    // friends chat channel: in one when the channel name is set
    std::string chat = "null";
    if (const std::uint64_t ch = r64(*root + kOffFriendsChat); heap(ch)) {
        const std::string cname = social_name(h, ch + 0x08);
        if (!cname.empty()) {
            std::string users; int nu = 0;
            std::uint64_t u0 = 0;
            const int n = vec_count(ch + 0x40, 0x50, 600, u0);
            for (int i = 0; i < n; ++i) {
                const std::uint64_t e = u0 + (std::uint64_t)i * 0x50;
                const std::string name = social_name(h, e);
                if (name.empty()) continue;
                if (nu++) users.push_back(',');
                users += "{\"name\":\"" + json_escape(name) + "\",\"world\":" + std::to_string(r32(e + 0x30)) + ",\"rank\":" + std::to_string(r32(e + 0x34)) + "}";
            }
            chat = "{\"name\":\"" + json_escape(cname) + "\",\"owner\":\"" + json_escape(social_name(h, ch + 0x20)) + "\",\"rank\":" + std::to_string(r32(ch + 0x38)) +
                   ",\"kickRank\":" + std::to_string(r32(ch + 0x3C)) + ",\"count\":" + std::to_string(nu) + ",\"users\":[" + users + "]}";
        }
    }
    // player group (group ironman): null without one
    std::string group = "null";
    if (const std::uint64_t pgm = r64(*root + kOffPlayerGroup); heap(pgm)) {
        if (const std::uint64_t g = r64(pgm + 8); heap(g)) {
            std::string members; int nm = 0;
            std::uint64_t m0 = 0;
            const int n = vec_count(g + 0x60, 0xB0, 64, m0);
            for (int i = 0; i < n; ++i) {
                const std::uint64_t e = m0 + (std::uint64_t)i * 0xB0;
                const std::string name = social_name(h, e + 0x18);
                if (nm++) members.push_back(',');
                members += "{\"name\":\"" + json_escape(name) + "\",\"online\":" + (rpm<std::uint8_t>(h, e + 0x34).value_or(0) ? "true" : "false") +
                           ",\"rank\":" + std::to_string((int)rpm<std::uint8_t>(h, e + 0x35).value_or(0)) + ",\"status\":" + std::to_string(r32(e + 0x38)) +
                           ",\"team\":" + std::to_string(r32(e + 0x3C)) + "}";
            }
            group = "{\"name\":\"" + json_escape(social_name(h, g + 8)) + "\",\"maxSize\":" + std::to_string((int)rpm<std::int16_t>(h, g + 0x22).value_or(0)) +
                    ",\"ownerSlot\":" + std::to_string(r32(g + 0x90)) + ",\"count\":" + std::to_string(nm) + ",\"members\":[" + members + "]}";
        }
    }
    return "{\"in\":true,\"world\":" + std::to_string(world) + ",\"friendsLoaded\":" + (loaded ? "true" : "false") +
           ",\"online\":" + std::to_string(online) + ",\"friends\":[" + friends + "],\"ignores\":[" + ignores + "]" +
           ",\"friendsChat\":" + chat + ",\"group\":" + group + "}";
}

bool PlayerTile(std::uint32_t pid, int& tx, int& ty, int& plane) {
    constexpr std::uint64_t kContainer = rtx::scn::kContainer, kActiveIdx = 0x70, kEntryArr = 0x58,
                            kEntryWv = 0x8, kVecBegin = 0x138,
                            kVecEnd = 0x140, kSecPtr = rtx::scn::kSecPtr, kType = rtx::scn::kType, kUid = rtx::scn::kUid,
                            kPosX = 0x270, kPosY = 0x278, kLocalUid = 0x48;
    const std::uint64_t kPlayerData = kOffAccount;
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

static void perks_of(const int val[8], int out[8]) {
    const PerkFieldLayout& L = perk_field_layout();
    auto fld = [&](const PerkField& f) {
        const std::uint32_t mask = (f.msb - f.lsb >= 31) ? 0xFFFFFFFFu : ((1u << (f.msb - f.lsb + 1)) - 1u);
        return (int)(((std::uint32_t)val[f.var] >> f.lsb) & mask);
    };
    const PerkField* ids[4] = { &L.g1p1, &L.g1p2, &L.g2p1, &L.g2p2 }, * ranks[4] = { &L.g1p1r, &L.g1p2r, &L.g2p1r, &L.g2p2r };
    for (int k = 0; k < 4; ++k) {
        const int id = fld(*ids[k]);
        out[k * 2] = id > 0 ? id : 0; out[k * 2 + 1] = id > 0 ? fld(*ranks[k]) : 0;
    }
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

namespace {

using rtx::health::kFail;
using rtx::health::kPass;
using rtx::health::kWarn;
using rtx::health::kUnchecked;

std::atomic<bool> g_healthHeadless{ false };

struct HCtx {
    std::uint32_t pid = 0;
    HANDLE h = nullptr;
    std::uint64_t mgva = 0, base = 0, size = 0, root = 0;
    std::string version, flavour;
    std::uint32_t stamp = 0, imageSize = 0;
    std::wstring exe;
    int status = -1, world = -1;
    bool inWorld = false;
    int localUid = -1;
    std::uint64_t localSec = 0;
    std::uint64_t wv = 0, worker = 0;
    bool cli = false;
    std::string runtimePins;
    std::string trigger;                // why the run started
    bool bootFound = false;             // the companion's boot record was read
    bool complete = false;              // in the world with a companion: every group judged
    int varpMaxId = -1;                 // largest varp id the store holds (live packet rows)
    bool fxRead = false;                // the type-4 effects below were read from the scene
    int t4Seen = 0;                     // type-4 effects in the scene
    std::map<int, int> t4Gfx;           // graphic -> type-4 effects with it that validated
};

std::string hx(std::uint64_t v) { return rtx::health::Hex(v); }

std::uint32_t module_stamp(HANDLE h, std::uint64_t base) {
    const std::int32_t lfanew = rpm<std::int32_t>(h, base + 0x3C).value_or(0);
    if (lfanew <= 0 || lfanew > 0x1000) return 0;
    return rpm<std::uint32_t>(h, base + (std::uint64_t)lfanew + 8).value_or(0);
}

bool printable_name(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return false;
    return true;
}

// A live name as the client stores it: UTF-8 (NBSP is C2 A0), no control bytes, 1..24 bytes.
bool utf8_name(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) { ++i; continue; }
        const int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : -1;
        if (n < 0 || i + n >= s.size()) return false;
        for (int k = 1; k <= n; ++k) if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += n + 1;
    }
    return true;
}

std::string norm_name(std::string s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xA0) { o.push_back(' '); continue; }
        if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA0) { o.push_back(' '); ++i; continue; }
        if (c == '_') { o.push_back(' '); continue; }
        o.push_back((char)std::tolower(c));
    }
    return o;
}

std::string sec_name(HANDLE h, std::uint64_t sec) {
    char b[40] = {};
    rpm_bytes(h, sec + rtx::scn::kName, b, sizeof(b) - 1);
    std::string s;
    for (int i = 0; i < (int)sizeof(b) && b[i]; ++i) s.push_back(b[i]);
    return s;
}

// Every entity sec in the scene's actor vector.
void scene_secs(const HCtx& c, std::vector<std::pair<std::uint64_t, std::uint64_t>>& out) {   // (entity, sec)
    out.clear();
    if (!c.worker) return;
    auto vb = rpm<std::uint64_t>(c.h, c.worker + rtx::scn::kVecBegin), ve = rpm<std::uint64_t>(c.h, c.worker + rtx::scn::kVecEnd);
    if (!vb || !ve || *vb <= 0x10000 || *ve < *vb) return;
    std::uint64_t n = (*ve - *vb) / 8; if (n > 20000) n = 20000;
    std::vector<std::uint64_t> eps((std::size_t)n);
    if (n && !rpm_bytes(c.h, *vb, eps.data(), (std::size_t)n * 8)) return;
    for (std::uint64_t ep : eps) {
        if (ep <= 0x10000) continue;
        auto sec = rpm<std::uint64_t>(c.h, ep + rtx::scn::kSecPtr);
        if (sec && *sec > 0x10000) out.emplace_back(ep, *sec);
    }
}

// A type-4 effect validates when its graphic is a cache spotanim and it sits within a map of the player.
bool t4_valid(HANDLE h, std::uint64_t sec, float px, float py, int& gfx) {
    gfx = rpm<std::int32_t>(h, sec + rtx::scn::kT4Gfx).value_or(-1);
    const int ex = rpm<std::int32_t>(h, sec + rtx::scn::kT4PosE).value_or(0), en = rpm<std::int32_t>(h, sec + rtx::scn::kT4PosN).value_or(0);
    if (gfx < 0 || rtx::cache::PinFingerprint("spotanim", std::to_string(gfx), "").empty()) return false;
    return std::fabs(ex - px) <= 104 * 512 && std::fabs(en - py) <= 104 * 512;
}

// The scene's type-4 effects into c, once per run: the scene group fills them while it reads the
// scene, and a run where it did not reads them here.
void scene_t4(HCtx& c) {
    if (c.fxRead || !c.inWorld || !c.worker) return;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> secs; scene_secs(c, secs);
    const float px = c.localSec ? rpm<float>(c.h, c.localSec + rtx::scn::kPosX).value_or(0) : 0.f;
    const float py = c.localSec ? rpm<float>(c.h, c.localSec + rtx::scn::kPosY).value_or(0) : 0.f;
    for (const auto& es : secs) {
        if (rpm<std::uint8_t>(c.h, es.second + rtx::scn::kType).value_or(0xFF) != 4) continue;
        ++c.t4Seen;
        int gfx = -1;
        if (t4_valid(c.h, es.second, px, py, gfx)) ++c.t4Gfx[gfx];
    }
    c.fxRead = true;
}

template <class S> struct ShareMap {
    HANDLE m = nullptr; const S* sh = nullptr;
    ~ShareMap() { if (sh) UnmapViewOfFile((void*)sh); if (m) CloseHandle(m); }
    bool open(const wchar_t* name) {
        m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (!m) return false;
        sh = reinterpret_cast<const S*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
        MEMORY_BASIC_INFORMATION mbi{};
        if (sh && (VirtualQuery(sh, &mbi, sizeof(mbi)) == 0 || mbi.RegionSize < sizeof(S))) { UnmapViewOfFile((void*)sh); sh = nullptr; }
        return sh != nullptr;
    }
};

int level_for_xp(long long xp, int cap) {
    long long pts = 0; int lvl = 1;
    for (int l = 1; l < cap; ++l) {
        pts += (long long)std::floor(l + 300.0 * std::pow(2.0, l / 7.0));
        if (xp >= pts / 4) lvl = l + 1; else break;
    }
    return lvl;
}

// ---- Build ----
struct RevKey { const char* key; int index; int archive; const char* name; };
const RevKey kRevKeys[] = {
    { "2/5", 2, 5, "inventories" }, { "2/11", 2, 11, "params" }, { "2/34", 2, 34, "map scenes" }, { "2/35", 2, 35, "quests" },
    { "2/36", 2, 36, "map elements" }, { "2/40", 2, 40, "dbtables" }, { "2/41", 2, 41, "dbrows" }, { "2/46", 2, 46, "hitmarks" },
    { "2/60", 2, 60, "player vars" }, { "2/61", 2, 61, "npc vars" }, { "2/62", 2, 62, "client vars" }, { "2/63", 2, 63, "world vars" },
    { "2/64", 2, 64, "region vars" }, { "2/65", 2, 65, "object vars" }, { "2/66", 2, 66, "clan vars" }, { "2/67", 2, 67, "clan setting vars" },
    { "2/68", 2, 68, "campaign vars" }, { "2/69", 2, 69, "varbits" }, { "2/70", 2, 70, "var config 70" }, { "2/71", 2, 71, "var config 71" },
    { "2/72", 2, 72, "var config 72" }, { "2/73", 2, 73, "var config 73" }, { "2/74", 2, 74, "var config 74" }, { "2/75", 2, 75, "group vars" },
    { "2/76", 2, 76, "var config 76" }, { "2/77", 2, 77, "var config 77" },
    { "3", 3, -1, "interfaces" }, { "5", 5, -1, "maps" }, { "8", 8, -1, "sprites" }, { "12", 12, -1, "scripts" }, { "14", 14, -1, "sound effects" },
    { "16", 16, -1, "locs" }, { "17", 17, -1, "enums" }, { "18", 18, -1, "npcs" }, { "19", 19, -1, "items" }, { "22", 22, -1, "structs" },
    { "23", 23, -1, "world map" }, { "40", 40, -1, "music" }, { "57", 57, -1, "achievements" },
};

std::map<std::string, std::string> parse_revs(const std::string& s) {
    std::map<std::string, std::string> m;
    std::size_t at = 0;
    while (at < s.size()) {
        std::size_t e = s.find(',', at); if (e == std::string::npos) e = s.size();
        const std::string kv = s.substr(at, e - at);
        const std::size_t eq = kv.find('=');
        if (eq != std::string::npos) m[kv.substr(0, eq)] = kv.substr(eq + 1);
        at = e + 1;
    }
    return m;
}

std::string read_small(const std::wstring& p) {
    std::ifstream f(p.c_str(), std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

std::string g_buildRevs;   // this run's cache revisions, for the builds file once the run is scored
bool g_buildCarried = false;   // first record of the build the app last ran on: reviewed already

void health_build(HCtx& c, rtx::health::Run& run) {
    const char* G = "Build";
    const rtx::codescan::ExeFacts ef = rtx::codescan::Facts(c.exe);
    if (ef.ok) { c.stamp = ef.stamp; c.imageSize = ef.sizeOfImage; c.flavour = ef.flavour; }
    if (c.stamp == 0 && c.base) c.stamp = module_stamp(c.h, c.base);
    char st[16]; std::snprintf(st, sizeof(st), "%08x", c.stamp);
    const std::string bl = rtx::pins::BuildLine(c.stamp);
    const bool known = !bl.empty();
    const std::string validated = rtx::pins::Field(bl, "validated");
    // cache revisions of the archives and indexes the app reads
    std::string revs, revJson;
    for (const auto& k : kRevKeys) {
        int r = k.archive >= 0 ? rtx::cache::ArchiveRevision(k.index, k.archive) : rtx::cache::IndexInfo(k.index).revision;
        if (k.archive < 0 && !rtx::cache::IndexInfo(k.index).open) r = -1;
        revs += (revs.empty() ? "" : ",") + std::string(k.key) + "=" + std::to_string(r);
        revJson += (revJson.empty() ? "\"" : ",\"") + std::string(k.key) + "\":" + std::to_string(r);
    }
    g_buildRevs = revs;
    run.Section("build", "{\"stamp\":\"" + std::string(st) + "\",\"flavour\":\"" + rtx::health::Escape(c.flavour) +
                "\",\"imageSize\":\"" + hx(c.imageSize) + "\",\"version\":\"" + rtx::health::Escape(c.version) +
                "\",\"exeVersion\":\"" + rtx::health::Escape(ef.version) + "\",\"known\":" + (known ? "true" : "false") +
                ",\"validated\":\"" + rtx::health::Escape(validated) + "\"}");
    run.Section("cache", "{\"root\":\"" + rtx::health::Escape(rtx::cache::CacheRoot()) + "\",\"gen\":" +
                std::to_string(rtx::cache::CacheGeneration()) + ",\"rev\":{" + revJson + "}}");
    run.Fact("build.version", c.version);
    run.Fact("build.stamp", st);
    run.Fact("build.flavour", c.flavour);
    run.Fact("build.size", hx(c.imageSize));
    for (const auto& kv : parse_revs(revs)) run.Fact("rev." + kv.first, kv.second);

    const rtx::health::BuildState bs = rtx::health::BuildsLookup(c.version, st, c.flavour, revs);
    g_buildCarried = !bs.exeSeen && !bs.prevLabel.empty() && bs.prevLabel == c.version;
    std::string flav = c.flavour == "vulkan" ? "Vulkan" : c.flavour == "opengl" ? "OpenGL" : c.flavour;
    std::string d = c.version + " " + flav + " " + st;
    int ok = kPass;
    rtx::health::BuildChange bc;
    bc.to = c.version + " " + st;
    bc.firstSeen = bs.firstSeen;
    if (!known) {
        ok = kWarn;
        bc.exe = "unvalidated";
        d += ": exe not in the manifest (new build). Build-pinned features: scene blank, packet table, var table, zone table, sound origin labels";
    } else {
        d += ", validated " + (validated.empty() ? std::string("(no date)") : validated);
    }
    if (bs.known) {
        if (!bs.reviewed) { ok = kWarn; d += "; first seen " + bs.firstSeen + ", not yet reviewed (a clean complete run or Mark reviewed clears this)"; }
        else d += "; cache unchanged since " + bs.firstSeen;
    } else if (bs.exeSeen) {
        ok = kWarn;
        bc.cache = "changed";
        const auto was = parse_revs(bs.prevRevs), now = parse_revs(revs);
        std::string changed;
        for (const auto& k : kRevKeys) {
            auto a = was.find(k.key), b = now.find(k.key);
            if (a != was.end() && b != now.end() && a->second != b->second) {
                changed += std::string(changed.empty() ? "" : ", ") + k.name;
                bc.archives += std::string(bc.archives.empty() ? "" : "|") + k.name;
            }
        }
        d += "; exe unchanged, cache changed: " + (changed.empty() ? std::string("revisions differ") : changed) + " (since " + bs.firstSeen + ")";
    } else if (!bs.prevLabel.empty()) {
        bc.from = bs.prevLabel;
        if (bs.prevLabel != c.version) { ok = kWarn; if (bc.exe == "same") bc.exe = "new"; d += "; game updated from " + bs.prevLabel; }
        else d += "; first fingerprint recorded (stamp and cache revisions now tracked)";
    } else {
        ok = kWarn;
        if (bc.exe == "same") bc.exe = "new";
        d += "; first run on this exe";
    }
    if (ok != kPass) d = "NEW: " + d;
    run.SetBuild(bc);
    run.Add(G, "build.game", "Game build", ok, d, "Everything", known ? "validated exe" : "a validated exe", st);

    // CS2 export: the developer's copy of the game's scripts. Nothing in the running client reads
    // it (operations are named by their own code, the generated tables are compiled in and checked
    // against the cache under Content), so a PC without one is complete.
    {
        wchar_t up[MAX_PATH] = {};
        GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
        const std::wstring dir = std::wstring(up) + L"\\RuneToolsX\\cs2\\";
        const std::string meta = read_small(dir + L"meta.json");
        const std::string label = read_small(dir + L"client_version.txt");
        rtx::health::JVal mv;
        std::string det; int k = kPass;
        if (meta.empty() || !rtx::health::ParseJson(meta, mv)) det = "no script export on this PC, none needed";
        else {
            std::string failed;
            if (const rtx::health::JVal* f = mv.get("failed")) for (const auto& x : f->a) failed += (failed.empty() ? "" : ",") + x.s;
            const std::string date = mv.str("date"), bn = mv.str("buildnr");
            det = "exported " + date.substr(0, 10) + ", " + mv.str("ok") + "/" + mv.str("total") + " scripts";
            if (!failed.empty()) { k = kWarn; det += "; scripts " + failed + " failed to decompile (tables built from them are stale)"; }
            run.Fact("cs2.failed", failed);
            run.Fact("cs2.buildnr", bn);
            // cache revisions are pack times: content packed after the export is not in its tables
            int y = 0, mo = 0, dd = 0, hh = 0, mi = 0, ss = 0;
            if (std::sscanf(date.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &dd, &hh, &mi, &ss) == 6) {
                std::tm tm{}; tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = dd; tm.tm_hour = hh; tm.tm_min = mi; tm.tm_sec = ss;
                const long long exported = (long long)_mkgmtime(&tm);
                long long newest = 0; std::string newestName;
                const auto now = parse_revs(revs);
                for (const auto& rk : kRevKeys) {
                    auto it = now.find(rk.key);
                    const long long r = it == now.end() ? 0 : std::atoll(it->second.c_str());
                    if (r > newest) { newest = r; newestName = rk.name; }
                }
                run.Fact("cs2.exported", std::to_string(exported));
                run.Fact("cache.newestRev", std::to_string(newest) + " " + newestName);
                if (exported > 0 && newest > 1000000000LL && newest > exported) {
                    if (k == kPass) k = kWarn;
                    det += "; the cache's " + newestName + " were packed after the export: export again";
                }
            }
            // exact: the cache's script count, and the indexes the export reads whose stored reference
            // table or archives changed since it started (the state the launcher recorded then; write
            // times without one)
            const auto fr = rtx::cs2fresh::Check(date, read_small(dir + L"cache_state.json"));
            const std::string stale = rtx::cs2fresh::Why(fr, std::atoi(mv.str("total").c_str()));
            run.Fact("cs2.cacheScripts", std::to_string(fr.cacheScripts));
            run.Fact("cs2.newerIndexes", fr.newer + (fr.newer.empty() || fr.written.empty() ? "" : ", ") + fr.written);
            run.Fact("cs2.cacheState", fr.recorded ? "recorded" : "none");
            if (!stale.empty()) {
                if (k == kPass) k = kWarn;
                det += "; " + stale + (det.find("export again") == std::string::npos ? ": export again" : "");
            }
            if (!fr.recorded) det += "; no cache state recorded with this export, file write times compared: export once to record the cache state";
        }
        std::string lab = label;
        while (!lab.empty() && (lab.back() == '\n' || lab.back() == '\r' || lab.back() == ' ')) lab.pop_back();
        run.Fact("cs2.label", lab);
        const std::string ev = ef.version;
        if (!lab.empty() && lab.substr(0, lab.find(' ')) != ev) { k = kWarn; det += "; op table labelled " + lab + ", the exe is " + ev; }
        // the handler table the update tools diff against, entry by entry: its handler must be the one
        // this exe registers under the entry's number, and its name the one opcodes.json gives that
        // number (every export rewrites opcodes.json without changing either, so file times say nothing)
        const std::string hj = read_small(dir + L"opcode_handlers.json");
        wchar_t exe[MAX_PATH] = {};
        if (!hj.empty() && c.h && GetModuleFileNameExW(c.h, nullptr, exe, MAX_PATH)) {
            const auto live = rtx::calib::ExeHandlers(exe);
            // each [{"op":N,...,"name":"X"}] record's number and name, up to its closing brace
            auto records = [](const std::string& j, auto&& each) {
                std::size_t at = 0;
                while ((at = j.find("{\"op\":", at)) != std::string::npos) {
                    const std::size_t end = j.find('}', at);
                    if (end == std::string::npos) break;
                    const std::string r = j.substr(at, end - at);
                    std::string name;
                    const std::size_t nm = r.find("\"name\":\"");
                    if (nm != std::string::npos) { const std::size_t q = r.find('"', nm + 8); if (q != std::string::npos) name = r.substr(nm + 8, q - nm - 8); }
                    each((std::uint32_t)std::strtoul(r.c_str() + 6, nullptr, 10), r, name);
                    at = end;
                }
            };
            std::map<std::uint32_t, std::string> named;
            records(read_small(dir + L"opcodes.json"), [&](std::uint32_t op, const std::string&, const std::string& nm) { if (!nm.empty()) named[op] = nm; });
            int n = 0, moved = 0, renamed = 0;
            if (!live.empty()) records(hj, [&](std::uint32_t op, const std::string& r, const std::string& nm) {
                const std::size_t hr = r.find("\"handlerRva\":");
                if (hr == std::string::npos) return;
                ++n;
                const auto h = live.find(op);
                if (h == live.end() || h->second != (std::uint32_t)std::strtoul(r.c_str() + hr + 13, nullptr, 10)) ++moved;
                const auto t = named.find(op);
                if (!nm.empty() && !named.empty() && (t == named.end() || t->second != nm)) ++renamed;
            });
            if (!live.empty()) {
                run.Fact("cs2.handlers", std::to_string(n - moved) + "/" + std::to_string(n) + " match, " + std::to_string(renamed) + " renamed");
                if (n == 0 || moved || renamed) {
                    if (k == kPass) k = kWarn;
                    det += n == 0 ? std::string("; opcode_handlers.json holds no handler")
                         : moved ? "; opcode_handlers.json is from another build (" + std::to_string(moved) + "/" + std::to_string(n) + " handlers moved)"
                                 : "; opcode_handlers.json names " + std::to_string(renamed) + (renamed == 1 ? " operation" : " operations") + " differently from opcodes.json: extract the handlers again";
                }
            }
        }
        // An export older than the cache is the CS2 Scripts panel showing older scripts, nothing more:
        // said in the row, never a warning on a PC that only plays.
        if (k != kPass) { det += "; the CS2 Scripts panel shows the scripts as exported"; k = kPass; }
        run.Add(G, "build.cs2", "Script export", k, det, "CS2 Scripts panel", "", "", "build.game");
    }
}

// ---- Context ----
std::string context_json(const HCtx& c) {
    const auto lines = rtx::pins::Lines();
    const std::string perr = rtx::pins::Error();
    std::wstring pf = rtx::pins::File();
    std::string pfile;
    for (wchar_t ch : pf) pfile.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
    for (char& ch : pfile) if (ch == '\\') ch = '/';
    const std::size_t slash = pfile.find_last_of('/');
    return std::string("{\"inWorld\":") + (c.inWorld ? "true" : "false") +
           ",\"status\":" + std::to_string(c.status) + ",\"process\":\"" + (c.cli ? "cli" : "launcher") +
           "\",\"trigger\":\"" + rtx::health::Escape(c.trigger) + "\",\"complete\":" + (c.complete ? "true" : "false") +
           ",\"pins\":\"" + rtx::health::Escape(perr.empty() ? (slash == std::string::npos ? pfile : pfile.substr(slash + 1)) + " " + rtx::pins::Made() + " (" + std::to_string(lines->size()) + " pins)" : perr) +
           "\",\"cacheGen\":" + std::to_string(rtx::cache::CacheGeneration()) + "}";
}

void health_context(HCtx& c, rtx::health::Run& run) {
    const char* G = "Context";
    run.Add(G, "context.client", "Game client", kPass, "attached, pid " + std::to_string(c.pid));
    run.Add(G, "context.world", "In the world", c.inWorld ? kPass : kUnchecked,
            c.inWorld ? "status 30, world " + std::to_string(c.world) : "status " + std::to_string(c.status) + ": live checks need a logged-in client").need = c.inWorld ? "" : "log in";
    run.Section("context", context_json(c));
}

// ---- Calibration ----
void health_calibration(HCtx& c, rtx::health::Run& run) {
    const char* G = "Calibration";
    const std::string why = rtx::calib::Why();
    const rtx::calib::TableState ts = rtx::calib::Table();
    const auto spots = rtx::calib::SpotChecks();
    {
        std::string d = ts.usable ? (std::to_string(ts.handlers) + " handlers, " + std::to_string(ts.names) + " named: " + std::to_string(ts.fromCode) + " by their own code" +
                                     (ts.exportUsed ? ", the rest by the script export labelled " + ts.label + (ts.differ ? " (" + std::to_string(ts.differ) + " named differently by the two, the code's used" +
                                      (ts.differRuled ? ", " + std::to_string(ts.differRuled) + " of them read by an offset rule" : std::string()) + ")" : "")
                                                    : "; script export not used (" + ts.exportNote + ")"))
                                  : (why.empty() ? std::string("not run") : why);
        if (!ts.usable) d = "GONE: " + d + " (the compiled offsets stay in force)";
        int ok = ts.usable ? kPass : kFail;
        if (ts.usable && ts.differRuled) { ok = kWarn; d = "NEW: " + d; }
        run.Add(G, "calib.table", "Operation names", ok, d,
                "Calibrated offsets|Engine ops by name|Asks|In-frame panels", "names for " + ts.exeVersion, std::to_string(ts.names), "code.exe");
        run.Fact("calib.label", ts.label);
        run.Fact("calib.fromCode", std::to_string(ts.fromCode));
    }
    if (!spots.empty()) {
        int good = 0, checkable = 0; std::string bad;
        for (const auto& s : spots) {
            if (s.ok < 0) continue;
            ++checkable;
            if (s.ok == 1) ++good;
            else bad += (bad.empty() ? "" : "; ") + s.op + " is " + std::to_string(s.number) + ", handler naming " + s.text + " is op " + s.owners;
            run.Fact("calib.spot." + s.op, std::to_string(s.number) + " in [" + s.owners + "]");
        }
        run.Add(G, "calib.spots", "Self-naming handlers", bad.empty() ? (checkable ? kPass : kUnchecked) : kFail,
                bad.empty() ? (checkable ? std::to_string(good) + "/" + std::to_string(checkable) + " sit under their names" : std::string("UNVERIFIED: no self-naming handler could be matched with a name")) : "MOVED: " + bad,
                "Operation names", std::to_string(checkable), std::to_string(good), "calib.table");
    }
    for (const auto& f : rtx::calib::Results()) {
        const std::string id = std::string("calib.") + f.name;
        const std::string key = std::string("Offset ") + f.name;
        using O = rtx::calib::Outcome;
        int ok = kPass; std::string d, kind;
        const std::string what = f.what ? std::string(" (") + f.what + ")" : std::string();
        switch (f.status) {
            case O::Found:   d = hx(f.found) + " from " + f.op + what; break;
            // read from the client and in use: where the compiled value differs (another client of the
            // build, or an update) nothing is wrong here; a compiled copy still in use elsewhere is the
            // Compiled copies row's to name
            case O::Moved:   d = hx(f.found) + " from " + f.op + " (compiled " + hx(f.compiled) + ", the client's in use)" + what; break;
            case O::NotFound: ok = kFail; kind = "fallback"; d = "GONE: no read of the expected shape in " + std::string(f.op) + "; compiled " + hx(f.compiled) + " in use" + what; break;
            case O::Ambiguous: ok = kFail; kind = "fallback"; d = "NEW: " + std::string(f.op) + " reads more than one candidate; compiled " + hx(f.compiled) + " in use" + what; break;
            case O::NoOp:    ok = kFail; kind = "fallback"; d = "GONE: " + std::string(f.op) + " is not recognised in this exe; compiled " + hx(f.compiled) + " in use"; break;
            case O::NoHandler: ok = kFail; kind = "fallback"; d = "GONE: " + std::string(f.op) + " has no handler in this exe; compiled " + hx(f.compiled) + " in use"; break;
        }
        run.Fact(std::string("calib.") + f.name, hx(f.found ? f.found : f.compiled));
        rtx::health::Row& row = run.Add(G, id, key, ok, d, "Game data reads", hx(f.compiled), f.found ? hx(f.found) : std::string("none"), "calib.table");
        if (!kind.empty()) row.kind = kind;
    }
    // offsets the compiled tables carry, found the same way: what a game update asks to change
    if (const auto refs = rtx::calib::References(); !refs.empty()) {
        using O = rtx::calib::Outcome;
        int at = 0; std::string moved, lost;
        for (const auto& f : refs) {
            run.Fact(std::string("calib.ref.") + f.name, f.found ? hx(f.found) : std::string("none"));
            if (f.status == O::Found) ++at;
            else if (f.status == O::Moved) moved += std::string(moved.empty() ? "" : ", ") + f.name + " " + hx(f.compiled) + " -> " + hx(f.found);
            else lost += std::string(lost.empty() ? "" : ", ") + f.name;
        }
        std::string d = std::to_string(at) + "/" + std::to_string(refs.size()) + " at their compiled values";
        if (!moved.empty()) d += "; moved, update the constants: " + moved;
        if (!lost.empty()) d += "; not found: " + lost;
        if (!moved.empty()) d = "MOVED: " + d; else if (!lost.empty()) d = "GONE: " + d;
        run.Add(G, "calib.refs", "Compiled offsets", moved.empty() && lost.empty() ? kPass : kWarn, d,
                "Companion and reader offsets", std::to_string(refs.size()), std::to_string(at), "calib.table");
    }
    // compiled copies the companion and the scene code still carry, against every offset the reader
    // applies: a pair that differs means the reader adopted a moved store the companion still reads
    // at the old place
    {
        static const struct { const char* name; std::uint32_t copy; const std::uint32_t* live; } kPairs[] = {
            { "status", rtx::md::kStatus, &kOffStatus }, { "player data", (std::uint32_t)rtx::scn::kPlayerData, &kOffAccount },
            { "world", rtx::md::kWorld, &kOffWorld }, { "containers", rtx::md::kContainers, &kOffInvData },
            { "chat store", rtx::md::kChatStore, &kOffChatStore }, { "clan settings", rtx::md::kClanSettings, &kOffClanSettings },
            { "map manager", rtx::md::kMapMgr, &kOffMapMgr }, { "input reporter", rtx::md::kInputReport, &kOffInputReporter },
            { "arrow manager", rtx::md::kArrowMgr, &kOffArrowMgr }, { "interface owner", rtx::md::kIfaceOwner, &kOffIfaceOwner },
            { "input processor", rtx::md::kInputProc, &kOffInputProc }, { "player group", rtx::md::kPlayerGroup, &kOffPlayerGroup },
            { "players", rtx::md::kPlayers, &kOffPlayers }, { "friends", rtx::md::kFriends, &kOffFriends },
            { "cutscene", rtx::md::kCutscene, &kOffCutscene }, { "varp manager", rtx::md::kVarpMgr, &kOffVarpMgr },
            { "varp hash", rtx::md::kVarpHash, &kOffVarpHash }, { "options", rtx::md::kOptions, &kOffOptions },
            { "varc store", rtx::md::kVarcStore, &kOffVarcStore }, { "Grand Exchange", rtx::md::kGrandExchange, &kOffGE },
            { "client clock", rtx::md::kClientClock, &kOffClientClock },
            { "connection", rtx::md::kConnection, &kOffConnection }, { "var watch", rtx::md::kVarWatch, &kOffVarWatch },
            { "friends chat", rtx::md::kFriendsChat, &kOffFriendsChat }, { "login manager", rtx::md::kLoginMgr, &kOffLoginMgr },
            { "world info", rtx::md::kWorldInfo, &kOffWorldInfo }, { "country", rtx::md::kCountry, &kOffCountry },
        };
        std::string bad; int agree = 0;
        for (const auto& p : kPairs) {
            if (p.copy == *p.live) { ++agree; continue; }
            bad += std::string(bad.empty() ? "" : "; ") + p.name + " " + hx(p.copy) + " compiled, reader uses " + hx(*p.live);
        }
        const int total = (int)(sizeof(kPairs) / sizeof(kPairs[0]));
        run.Add(G, "calib.copies", "Compiled copies", bad.empty() ? kPass : kWarn,
                bad.empty() ? std::to_string(total) + " companion and scene constants agree with the offsets in use" : "MOVED: companion copies behind the reader: " + bad,
                "Companion engine ops|Scene reads|Engine markers", std::to_string(total), std::to_string(agree), "calib.table");
    }
}

// ---- Game data ----
void health_data(HCtx& c, rtx::health::Run& run) {
    const char* G = "Game data";
    HANDLE h = c.h;
    const std::uint64_t root = c.root;
    // A/B window for every rate check
    const ULONGLONG tA = GetTickCount64();
    const std::uint32_t ccA = rpm<std::uint32_t>(h, root + kOffClientClock).value_or(0);
    const std::uint64_t ecA = rpm<std::uint64_t>(h, c.mgva + kEngineClock).value_or(0);
    const int worldA = c.world;
    std::uint32_t tickA = 0; double ageA = 0; const bool haveTick = TickState(c.pid, tickA, ageA);
    Sleep(300);
    const ULONGLONG tB = GetTickCount64();
    const std::uint32_t ccB = rpm<std::uint32_t>(h, root + kOffClientClock).value_or(0);
    const std::uint64_t ecB = rpm<std::uint64_t>(h, c.mgva + kEngineClock).value_or(0);
    int worldB = -1;
    if (auto p1 = rpm<std::uint64_t>(h, root + kOffWorld); p1 && *p1 > 0x10000)
        if (auto p2 = rpm<std::uint64_t>(h, *p1 + 0x20); p2 && *p2 > 0x10000) worldB = rpm<std::int32_t>(h, *p2 + 8).value_or(-1);
    const double secs = (double)(tB - tA) / 1000.0;
    // root and status
    {
        static const int kStates[] = { 0, 1, 10, 20, 23, 30, 35, 36, 37, 40 };
        const bool known = std::find(std::begin(kStates), std::end(kStates), c.status) != std::end(kStates);
        int ok = known ? kPass : kFail;
        std::string d = "status " + std::to_string(c.status) + (known ? "" : " (not a known login state: status offset moved?)");
        if (!known) d = "MOVED: " + d;
        if (c.status == 0) { ok = kWarn; d = "UNVERIFIED: " + d + " (boot state; a moved offset also reads 0)"; }
        if (c.status == 30) {
            if (worldA < 1 || worldA > 300 || worldA != worldB) { ok = kFail; d = "MOVED: " + d + ", world " + std::to_string(worldA) + "/" + std::to_string(worldB) + " (world chain moved?)"; }
            else d += ", world " + std::to_string(worldA);
        }
        if (kOffAccount != kOffStatus + 8) { if (ok == kPass) { ok = kWarn; d = "MOVED: " + d; } d += "; account no longer sits at status + 8"; }
        if (c.status != 30 && c.status != 35 && c.status != 36 && c.status != 37 && c.localSec) { ok = kFail; d = "MOVED: " + d + "; the local player is in the scene while the status says otherwise (status offset moved?)"; }
        run.Add(G, "data.root", "Root and status", ok, d, "Everything", "status in 0/1/10/20/23/30/35/36/37/40, world 1..300", std::to_string(c.status) + (c.status == 30 ? ", world " + std::to_string(worldA) : std::string()), "code.sig.main-anchor");
        run.Fact("data.status", std::to_string(c.status));
    }
    // clocks
    {
        const double ccRate = (double)(std::int64_t)(ccB - ccA) / secs;
        const bool ok = ccRate >= 40 && ccRate <= 60;
        run.Add(G, "data.clock.client", "Client clock", c.inWorld ? (ok ? kPass : kFail) : kUnchecked,
                std::string(c.inWorld && !ok ? "MOVED: " : "") + "MainData+" + hx(kOffClientClock) + " advances " + std::to_string((int)std::lround(ccRate)) + "/s" + (ok ? "" : " (expected 50/s: the cycle counter is not at this offset)"),
                "Buff timers|Ticks|Combat log", "50/s", std::to_string((int)std::lround(ccRate)), "data.root").need = c.inWorld ? "" : "log in";
        const double ecRate = (double)(std::int64_t)(ecB - ecA) / secs;
        const long long drift = (long long)ecB - (long long)tB;
        const bool eok = ecRate >= 800 && ecRate <= 1200 && std::llabs(drift) < 2000;
        run.Add(G, "data.clock.engine", "Engine clock", eok ? kPass : kFail,
                std::string(eok ? "" : "MOVED: ") + "root global + " + hx(kEngineClock) + " advances " + std::to_string((int)std::lround(ecRate)) + "/s, " + std::to_string(drift) + " ms from the system clock" + (eok ? "" : " (expected 1000/s within 2 s of the system clock)"),
                "Ability cooldowns|Action bar clock|Input idle", "1000/s, drift < 2000 ms", std::to_string((int)std::lround(ecRate)) + "/s, " + std::to_string(drift) + " ms", "code.sig.main-anchor");
        std::uint32_t tickB = 0; double ageB = 0; TickState(c.pid, tickB, ageB);
        const bool tok = haveTick && ageB >= 0 && ageB < 5000;
        run.Add(G, "data.tick", "Server tick", !haveTick ? kFail : c.inWorld ? (tok ? kPass : kFail) : kUnchecked,
                !haveTick ? "GONE: tick counter not found (the owner global or the increment moved)" : std::string(c.inWorld && !tok ? "MOVED: " : "") + "tick " + std::to_string(tickB) + ", last change " + std::to_string((int)ageB) + " ms ago" + (c.inWorld && !tok ? " (a live tick changes every 600 ms)" : ""),
                "Ticks|Tick timers|Metronome", "changed < 5000 ms ago", std::to_string((int)ageB) + " ms", "code.sig.tick-anchor").need = c.inWorld || !haveTick ? "" : "log in";
        const float fps = rpm<float>(h, root + rtx::md::kFps).value_or(-1.f);
        const bool fpsOk = fps >= 1.f && fps <= 1000.f;
        run.Add(G, "data.fps", "FPS", fpsOk ? kPass : kFail, std::string(fpsOk ? "" : "MOVED: ") + std::to_string((int)fps) + " at MainData+" + hx(rtx::md::kFps) + (fpsOk ? "" : " (not a frame rate: the field moved)"),
                "Player State FPS", "1..1000", std::to_string((int)fps), "data.root");
    }
    // varp store
    {
        const std::uint64_t hr = root + kOffVarpHash;
        const std::uint64_t ba = rpm<std::uint64_t>(h, hr + 0x8).value_or(0);
        const int div = rpm<std::int32_t>(h, hr + 0x10).value_or(0);
        const int count = rpm<std::int32_t>(h, hr + 0x18).value_or(-1);
        int walked = 0, maxId = 0;
        std::vector<std::pair<int, int>> sample;
        if (ba > 0x10000 && div > 0 && div <= 131072) {
            std::vector<std::uint64_t> buckets((std::size_t)div);
            if (rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) {
                for (int b = 0; b < div; ++b) {
                    std::uint64_t node = buckets[(std::size_t)b];
                    for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
                        std::uint8_t nb[0x30];
                        if (!rpm_bytes(h, node, nb, sizeof(nb))) break;
                        const int id = *reinterpret_cast<const std::int32_t*>(nb);
                        const int val = *reinterpret_cast<const std::int32_t*>(nb + 8);
                        ++walked;
                        if (id > maxId) maxId = id;
                        if ((walked % 97) == 1 && sample.size() < 40) sample.emplace_back(id, val);
                        node = *reinterpret_cast<const std::uint64_t*>(nb + kVarNodeNext);
                    }
                }
            }
        }
        int agree = 0;
        for (const auto& s : sample) if (read_varp(h, root, s.first) == s.second) ++agree;
        int ok = kPass; std::string d = std::to_string(walked) + " nodes, store count " + std::to_string(count) +
            ", lookups " + std::to_string(agree) + "/" + std::to_string(sample.size()) + ", largest id " + std::to_string(maxId);
        if (!c.inWorld && walked == 0) { ok = kUnchecked; d = "no varps before login"; }
        else if (walked == 0) { ok = kFail; d = "GONE: no varps while logged in at MainData+" + hx(kOffVarpHash) + " (store offset moved?)"; }
        else if (count >= 0 && count != walked) { ok = kWarn; d = "FORMAT: " + d + " (walk and count differ: node or bucket layout)"; }
        if (!sample.empty() && agree != (int)sample.size()) { ok = kFail; d = "FORMAT: " + d + " (lookup chain differs from the walk)"; }
        if (maxId > 90000) { if (ok == kPass) { ok = kWarn; d = "NEW: " + d; } d += "; ids near the 100000 filter"; }
        run.Add(G, "data.varps", "Player variables", ok, d, "Every varp and varbit read", "store count == walked nodes, every sampled lookup agrees, ids < 90000",
                std::to_string(walked) + " walked, count " + std::to_string(count) + ", " + std::to_string(agree) + "/" + std::to_string(sample.size()) + " lookups", "data.root").need = ok == kUnchecked ? "log in" : "";
        run.Fact("varp.maxId", std::to_string(maxId));
        c.varpMaxId = maxId;
    }
    // varc store
    {
        const std::uint64_t store = rpm<std::uint64_t>(h, root + kOffVarcStore).value_or(0);
        const std::uint64_t hr = store + kVarcHashOff;
        const std::uint64_t ba = store > 0x10000 ? rpm<std::uint64_t>(h, hr + 0x8).value_or(0) : 0;
        const int div = store > 0x10000 ? rpm<std::int32_t>(h, hr + 0x10).value_or(0) : 0;
        int walked = 0, maxId = 0;
        if (ba > 0x10000 && div > 0 && div <= 131072) {
            std::vector<std::uint64_t> buckets((std::size_t)div);
            if (rpm_bytes(h, ba, buckets.data(), (std::size_t)div * 8)) {
                for (int b = 0; b < div; ++b) {
                    std::uint64_t node = buckets[(std::size_t)b];
                    for (int steps = 0; steps < 512 && node > 0x10000; ++steps) {
                        const int id = rpm<std::int32_t>(h, node).value_or(-1);
                        ++walked; if (id > maxId) maxId = id;
                        node = rpm<std::uint64_t>(h, node + kVarNodeNext).value_or(0);
                    }
                }
            }
        }
        int vw = 0, vh = 0, yaw = 0, pitch = 0, zoom = 0;
        const bool fw = read_varc_found(h, root, 3001, vw), fh = read_varc_found(h, root, 3002, vh);
        const bool fy = read_varc_found(h, root, 5115, yaw), fp = read_varc_found(h, root, 5114, pitch), fz = read_varc_found(h, root, 1971, zoom);
        int gx = 0, gy = 0, gw = 0, gh = 0;
        const bool tree = read_gameview_rect(h, root, gx, gy, gw, gh);
        int ok = kPass; std::string d = std::to_string(walked) + " varcs, largest id " + std::to_string(maxId);
        if (walked == 0) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: no varcs at [MainData+" + hx(kOffVarcStore) + "]+" + hx(kVarcHashOff) + " (store offset moved?)" : "no varcs before login"; }
        else {
            if (c.inWorld && (!fw || !fh)) { ok = kFail; d = "GONE: " + d + "; gameview size varcs 3001/3002 missing"; }
            else if (tree && gw > 0 && (std::abs(vw - gw) > 2 || std::abs(vh - gh) > 2)) { ok = kWarn; d = "MOVED: " + d + "; varcs " + std::to_string(vw) + "x" + std::to_string(vh) + " vs layout " + std::to_string(gw) + "x" + std::to_string(gh); }
            else if (fw) d += "; gameview " + std::to_string(vw) + "x" + std::to_string(vh);
            if (c.inWorld && (!fy || !fp || !fz)) { if (ok == kPass) { ok = kWarn; d = "GONE: " + d; } d += "; camera varcs 5115/5114/1971 not all present"; }
            else if (fy && (yaw < 0 || yaw > 16383)) { ok = kFail; d = "FORMAT: " + d + "; yaw 5115 = " + std::to_string(yaw) + " out of range"; }
        }
        if (maxId > 90000) { if (ok == kPass) { ok = kWarn; d = "NEW: " + d; } d += "; ids near the 100000 filter"; }
        run.Add(G, "data.varcs", "Client variables", ok, d, "Overlays|Camera|Panel positions", "varcs 3001/3002 and 5115/5114/1971 present, yaw 0..16383",
                std::to_string(walked) + " varcs, gameview " + std::to_string(vw) + "x" + std::to_string(vh) + ", yaw " + std::to_string(yaw), "calib.kOffVarcStore").need = ok == kUnchecked ? "log in" : "";
        run.Fact("varc.maxId", std::to_string(maxId));
    }
    // var domain stores
    {
        rtx::health::JVal j; rtx::health::ParseJson(VarDomainStoresJson(c.pid), j);
        const rtx::health::JVal* st = j.get("stores");
        auto field = [&](const char* dom, const char* f) -> std::string {
            const rtx::health::JVal* d = st ? st->get(dom) : nullptr; return d ? d->str(f) : std::string();
        };
        auto present = [&](const char* dom) { const std::string p = field(dom, "ptr"); return !p.empty() && p != "0x0"; };   // the store object is set
        const bool p0 = field("0", "live") == "true", p2 = field("2", "live") == "true", vt2 = field("2", "vt") == "true";
        const bool clan = field("6", "live") == "true", clanSet = field("7", "live") == "true";   // slot 0 is the player's own clan
        const bool clanPtr = present("6"), clanSetPtr = present("7");
        int ok = (p0 && p2 && vt2) ? kPass : kFail;
        std::string d = "player " + (p0 ? field("0", "count") + " vars" : std::string("absent")) +
                        ", client " + (p2 ? field("2", "count") + " vars" : std::string("absent")) +
                        (p2 && !vt2 ? " (class vtable " + field("2", "vt_rva") + " not recognised)" : std::string()) +
                        ", clan " + (clan ? field("6", "count") + " vars" : std::string("absent")) +
                        ", clan settings " + (clanSet ? field("7", "count") + " vars" : std::string("absent")) +
                        ", group " + (field("9", "live") == "true" ? field("9", "count") + " vars" : std::string("absent"));
        run.Fact("domains.clientVt", field("2", "vt_rva"));
        if (!p0 || !p2) d = "GONE: " + d + " (a var store reads empty at its offset)";
        else if (!vt2) d = "FORMAT: " + d;
        // the clan var store and the listened clan settings come and go on their own (clan settings
        // with no clan var state is seen live); a store whose object is set but whose table does not
        // read is the one that moved
        else if (c.inWorld && ((clanPtr && !clan) || (clanSetPtr && !clanSet))) {
            ok = kWarn;
            d = "UNVERIFIED: " + std::string(clanPtr && !clan ? "clan var store at [[MainData+" + hx(kOffVarcStore) + "]+0x77B0]" : "clan settings store at [[MainData+" + hx(kOffClanSettings) + "]+0x68]") +
                " is set but its table does not read (a moved store); " + d;
        }
        else if (clanSet && !clan) d += " (clan settings without clan vars: no clan var state)";
        if (!c.inWorld && !p0) { ok = kUnchecked; d = "player store empty before login"; }
        run.Add(G, "data.domains", "Var domain stores", ok, d, "Clan vars|Group vars|Varbit reads by domain", "player and client stores live; clan vars and clan settings each absent or live (settings without clan vars is a real state)",
                std::string("player ") + (p0 ? "live" : "absent") + ", client " + (p2 ? "live" : "absent") + ", clan " + (clan ? "live" : clanPtr ? "set, unread" : "absent") + ", clan settings " + (clanSet ? "live" : clanSetPtr ? "set, unread" : "absent"), "data.varps").need = ok == kUnchecked ? "log in" : "";
    }
    // containers
    {
        const std::uint64_t mgr = rpm<std::uint64_t>(h, root + kOffInvData).value_or(0);
        const std::uint64_t cs = mgr > 0x10000 ? rpm<std::uint64_t>(h, mgr + 0x8).value_or(0) : 0;
        const std::uint64_t ce = mgr > 0x10000 ? rpm<std::uint64_t>(h, mgr + 0x10).value_or(0) : 0;
        int ok = kPass; std::string d; std::string got;
        if (cs <= 0x10000 || ce < cs) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: container list not readable at [MainData+" + hx(kOffInvData) + "]+8/+0x10" : "container list not set before login"; }
        else if ((ce - cs) % kContainerStride) { ok = kFail; d = "FORMAT: list span " + hx(ce - cs) + " not a multiple of the 0x48 stride"; }
        else {
            const std::uint64_t n = (ce - cs) / kContainerStride;
            std::set<int> ids; bool dup = false, unsorted = false; int named = 0, items = 0, size93 = -1, size94 = -1, badKey = 0, prevKey = INT32_MIN;
            for (std::uint64_t i = 0; i < n && i < 256; ++i) {
                const std::uint64_t e = cs + i * kContainerStride;
                const int cid = rpm<std::int32_t>(h, e + 0x10).value_or(-1);
                const int key = rpm<std::int32_t>(h, e).value_or(-1);   // sort key: id * 2 | other-player flag
                if ((key >> 1) != cid) ++badKey;
                if (i && key < prevKey) unsorted = true;
                prevKey = key;
                if (!ids.insert(cid).second) dup = true;
                const std::uint64_t is = rpm<std::uint64_t>(h, e + 0x18).value_or(0), ie = rpm<std::uint64_t>(h, e + 0x20).value_or(0);
                const int slots = (is > 0x10000 && ie >= is) ? (int)((ie - is) / 8) : -1;
                if (cid == 93) size93 = slots;
                if (cid == 94) size94 = slots;
                if ((cid == 93 || cid == 94) && slots > 0 && slots < 64) {
                    std::vector<BankSlot> v((std::size_t)slots);
                    if (rpm_bytes(h, is, v.data(), v.size() * sizeof(BankSlot)))
                        for (const auto& s : v) if (s.item_id > 0) { ++items; if (!rtx::cache::ItemName(s.item_id).empty()) ++named; }
                }
            }
            auto invSize = [](int id) { return std::atoi(rtx::pins::Field(rtx::cache::PinFingerprint("inv", std::to_string(id), ""), "size").c_str()); };
            const int def93 = invSize(93), def94 = invSize(94);
            d = std::to_string(n) + " containers";
            got = std::to_string(n) + " containers, backpack " + std::to_string(size93) + ", equipment " + std::to_string(size94) + ", " + std::to_string(named) + "/" + std::to_string(items) + " named";
            std::string word;
            if (n > (std::uint64_t)kMaxContainers) { ok = kFail; word = "NEW"; d += " (more than the 64 the reader takes)"; }
            if (dup) { ok = kFail; if (word.empty()) word = "FORMAT"; d += "; repeated ids (stride or id offset moved)"; }
            if (badKey || unsorted) { ok = kFail; if (word.empty()) word = "FORMAT"; d += "; " + std::to_string(badKey) + " entry keys are not id * 2 | other" + (unsorted ? ", keys not ascending" : "") + " (own and other-player containers told apart by the key)"; }
            if (size93 >= 0) {
                d += "; backpack " + std::to_string(size93) + " slots (def " + std::to_string(def93) + ")";
                if (def93 > 0 && size93 != def93) { ok = kFail; if (word.empty()) word = "FORMAT"; d += " mismatch"; }
            } else if (c.inWorld) { ok = kFail; if (word.empty()) word = "GONE"; d += "; no backpack (93)"; }
            if (size94 >= 0 && def94 > 0 && size94 != def94) { ok = kFail; if (word.empty()) word = "FORMAT"; d += "; equipment " + std::to_string(size94) + " slots, def " + std::to_string(def94); }
            if (items) { d += "; " + std::to_string(named) + "/" + std::to_string(items) + " items name in the cache"; if (named * 10 < items * 9) { ok = kFail; if (word.empty()) word = "FORMAT"; } }
            if (!word.empty()) d = word + ": " + d;
        }
        run.Add(G, "data.containers", "Containers", ok, d, "Backpack|Equipment|Bank|Storages", "<= 64 containers, unique ids, keys id * 2 | other ascending, backpack and equipment sizes as their inv defs, 90 % of items named", got, "data.root").need = ok == kUnchecked ? "log in" : "";
    }
    // Grand Exchange slots
    {
        const std::uint64_t box = rpm<std::uint64_t>(h, root + kOffGE).value_or(0);
        int ok = kPass, bad = 0, active = 0; std::string d, need;
        if (box <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: slot block pointer at MainData+" + hx(kOffGE) + " not readable" : "slot block not set before login"; need = c.inWorld ? "" : "log in"; }
        else {
            alignas(8) std::uint8_t gb[kGESlotCount * kGESlotSize];
            if (!rpm_bytes(h, box + kGEArrayPad, gb, sizeof(gb))) { ok = kFail; d = "GONE: slot block at " + hx(box + kGEArrayPad) + " not readable"; }
            else {
                for (int i = 0; i < kGESlotCount; ++i) {
                    const std::uint8_t* b = gb + (std::size_t)i * kGESlotSize;
                    const int status = *(const std::int32_t*)(b), item = *(const std::int32_t*)(b + 8);
                    const long long price = *(const std::int64_t*)(b + 0x10);
                    const int qty = *(const std::int32_t*)(b + 0x18), filled = *(const std::int32_t*)(b + 0x1C);
                    if (status == 0) { if (item != 0) ++bad; continue; }
                    ++active;
                    if (status < 0 || status > 7 || item <= 0 || rtx::cache::ItemName(item).empty() || price <= 0 || qty < filled || filled < 0) ++bad;
                }
                d = std::to_string(active) + " active offers" + (bad ? ", " + std::to_string(bad) + " implausible" : "");
                if (bad && !c.inWorld) { ok = kUnchecked; d = "the slots are filled at login (" + d + " before it)"; need = "log in"; }   // the lobby leaves them unset
                else if (bad) { ok = kFail; d = "FORMAT: " + d + " (status, item, price or quantity fields do not read as an offer)"; }
                else if (!active) { ok = kUnchecked; d = "no active offer to judge the slot layout (empty slots read as empty)"; need = "open a Grand Exchange offer"; }
            }
        }
        run.Add(G, "data.ge", "Grand Exchange slots", ok, d, "Grand Exchange panel|GE alerts", "active slots: status 1..7, a named item, price > 0, filled <= quantity",
                std::to_string(active) + " active, " + std::to_string(bad) + " implausible", "calib.kOffGE").need = need;
    }
    // input reporter, cutscene, options, mouse, map, friends, chat store, hover, cooldowns, destination
    {
        const std::uint64_t rep = rpm<std::uint64_t>(h, root + kOffInputReporter).value_or(0);
        int ok = kPass; std::string d, got;
        if (rep <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: reporter pointer at MainData+" + hx(kOffInputReporter) + " not readable" : "reporter not set before login"; }
        else {
            const std::uint64_t a = rpm<std::uint64_t>(h, rep + kOffRepPointerA).value_or(0), b = rpm<std::uint64_t>(h, rep + kOffRepPointerB).value_or(0);
            const std::uint64_t now = ecB;
            const std::uint64_t last = std::max(a, b);
            got = std::to_string(a) + ", " + std::to_string(b) + " vs clock " + std::to_string(now);
            if (last > now + 2000 || (last && now - last > 24ull * 3600 * 1000)) { ok = kFail; d = "FORMAT: pointer stamps not on the engine clock (" + std::to_string(a) + ", " + std::to_string(b) + ")"; }
            else d = "last pointer activity " + std::to_string((long long)(now - last) / 1000) + " s ago";
        }
        run.Add(G, "data.input", "Input reporter", ok, d, "Idle timer|AFK alerts", "pointer stamps within a day of the engine clock", got, "data.clock.engine").need = ok == kUnchecked ? "log in" : "";
    }
    {
        int cut = -2;
        if (auto cs = rpm<std::uint64_t>(h, root + kOffCutscene); cs && *cs > 0x10000) cut = rpm<std::int32_t>(h, *cs + 0x150).value_or(-2);
        const bool ok = cut == -1 || (cut >= 0 && cut <= 65535);
        run.Add(G, "data.cutscene", "Cutscene state", ok ? kPass : (c.inWorld ? kFail : kUnchecked),
                ok ? std::to_string(cut) : c.inWorld ? "FORMAT: " + std::to_string(cut) + " at [MainData+" + hx(kOffCutscene) + "]+0x150 is neither -1 nor an id" : "cutscene object not set before login",
                "Cutscene hide", "-1 or an id", std::to_string(cut), "data.root").need = ok || c.inWorld ? "" : "log in";
        int heap = 0;
        if (auto opt = rpm<std::uint64_t>(h, root + kOffOptions); opt && *opt > 0x10000)
            for (int i = 0; i < 44; ++i) if (rpm<std::uint64_t>(h, *opt + 0x2DA8 + (std::uint64_t)i * 8).value_or(0) > 0x10000) ++heap;
        run.Add(G, "data.options", "Client options", heap == 44 ? kPass : kFail, std::string(heap == 44 ? "" : "GONE: ") + std::to_string(heap) + "/44 option objects" + (heap == 44 ? "" : " at [[MainData+" + hx(kOffOptions) + "]+0x2DA8]"), "Settings readouts|Layout detection", "44", std::to_string(heap), "data.root");
    }
    {
        const std::uint64_t st = rpm<std::uint64_t>(h, root + kOffVarcStore).value_or(0);
        int ok = kPass; std::string d, word;
        if (st <= 0x10000) { ok = kFail; d = "store not readable at MainData+" + hx(kOffVarcStore); word = "GONE"; }
        else {
            const float mx = rpm<float>(h, st + 0x46D8).value_or(-9), my = rpm<float>(h, st + 0x46DC).value_or(-9);
            const std::uint32_t mods = rpm<std::uint32_t>(h, st + 0x4F0).value_or(0xFFFFFFFFu);
            int lw = 0, lh = 0, gx = 0, gy = 0, gw = 0, gh = 0;
            read_gameview_rect(h, root, gx, gy, gw, gh, &lw, &lh);
            d = "mouse " + std::to_string((int)mx) + "," + std::to_string((int)my) + ", modifiers " + std::to_string(mods);
            if (!(mx >= -1.f && my >= -1.f && mx < 32768.f && my < 32768.f) || (lw > 0 && (mx > lw + 4 || my > lh + 4))) { ok = kFail; word = "FORMAT"; d += " (outside the window)"; }
            if (mods > 0xF) { ok = kFail; word = "FORMAT"; d += " (modifier word)"; }
        }
        const std::uint64_t mm = rpm<std::uint64_t>(h, root + kOffMapMgr).value_or(0);
        if (mm > 0x10000) {
            const int a = rpm<std::int32_t>(h, mm + 0x710).value_or(-1), b = rpm<std::int32_t>(h, mm + 0x714).value_or(-1);
            const int ls = rpm<std::uint8_t>(h, mm + 0x71C).value_or(0);
            d += ", map load " + std::to_string(std::min(a, b)) + "%";
            if (a < 0 || a > 100 || b < 0 || b > 100) { ok = kFail; if (word.empty()) word = "FORMAT"; d += " (out of range)"; }
            else if (!ls && c.inWorld && std::min(a, b) != 100) { if (ok == kPass) { ok = kWarn; word = "MOVED"; } d += " while not loading (progress fields +0x710/+0x714 or the loading flag moved)"; }
        }
        if (!word.empty()) d = word + ": " + d;
        run.Add(G, "data.input2", "Mouse, keys, map load", ok, d, "Player State mouse and keys|Loading detection", "mouse inside the window, modifiers <= 0xF, map load 0..100 % (100 when not loading)", d.substr(d.find("mouse ") == std::string::npos ? 0 : d.find("mouse ")), "data.root");
    }
    {
        const std::uint64_t mm = rpm<std::uint64_t>(h, root + kOffMapMgr).value_or(0);
        int ok = kPass; std::string d, got;
        if (mm <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: map manager pointer at MainData+" + hx(kOffMapMgr) + " not readable" : "map manager not set before login"; }
        else {
            const int bx = rpm<std::int32_t>(h, mm + 0x698).value_or(-1), by = rpm<std::int32_t>(h, mm + 0x69C).value_or(-1);
            d = "base " + std::to_string(bx) + "," + std::to_string(by);
            got = d;
            if (bx < 0 || by < 0 || (bx % 8) || (by % 8)) { ok = kFail; d = "FORMAT: " + d + " (not on a zone boundary: +0x698/+0x69C are not the base tile)"; }
            if (c.localSec) {
                const int tx = (int)(rpm<float>(h, c.localSec + rtx::scn::kPosX).value_or(0) / 512.f), ty = (int)(rpm<float>(h, c.localSec + rtx::scn::kPosY).value_or(0) / 512.f);
                got += ", player " + std::to_string(tx) + "," + std::to_string(ty);
                if (tx - bx < 0 || tx - bx >= 256 || ty - by < 0 || ty - by >= 256) { ok = kFail; if (d.rfind("FORMAT", 0) != 0) d = "MOVED: " + d; d += "; player tile " + std::to_string(tx) + "," + std::to_string(ty) + " outside the loaded map"; }
                else d += ", player at +" + std::to_string(tx - bx) + ",+" + std::to_string(ty - by);
            }
        }
        run.Add(G, "data.mapbase", "Map base", c.inWorld ? ok : kUnchecked, c.inWorld ? d : "map base is read in the world", "Zone events|Ground items|Loc changes", "base on a zone boundary, the player within 256 tiles of it", got, "data.root").need = c.inWorld ? "" : "log in";
    }
    {
        const std::uint64_t fr = rpm<std::uint64_t>(h, root + kOffFriends).value_or(0);
        int ok = kPass; std::string d, got, need;
        if (fr <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: friends list pointer at MainData+" + hx(kOffFriends) + " not readable" : "friends list not set before login"; need = c.inWorld ? "" : "log in"; }
        else {
            const int state = rpm<std::int32_t>(h, fr + 0x10).value_or(-1);
            const std::uint64_t b0 = rpm<std::uint64_t>(h, fr + 0x18).value_or(0), e0 = rpm<std::uint64_t>(h, fr + 0x20).value_or(0);
            got = "state " + std::to_string(state);
            if (state != 2) { ok = kUnchecked; d = "list state " + std::to_string(state) + " (not loaded)"; need = "the friends list loaded (state 2)"; }
            else if (b0 <= 0x10000 && e0 == b0) d = "empty list";
            else if (b0 <= 0x10000 || e0 < b0 || (e0 - b0) % 0x78) { ok = kFail; d = "FORMAT: span " + hx(e0 - b0) + " not a multiple of the 0x78 stride"; }
            else {
                const int n = (int)((e0 - b0) / 0x78); int good = 0;
                for (int i = 0; i < n && i < 400; ++i) {
                    const std::string nm = social_name(h, b0 + (std::uint64_t)i * 0x78);
                    const int w = rpm<std::int32_t>(h, b0 + (std::uint64_t)i * 0x78 + 0x30).value_or(-1);
                    if (printable_name(nm) && w >= 0 && w <= 2000) ++good;   // lobby worlds are 1100 and up
                }
                d = std::to_string(good) + "/" + std::to_string(n) + " friends read cleanly";
                got += ", " + d;
                if (good * 10 < n * 9) { ok = kFail; d = "FORMAT: " + d + " (name string at +0 or world at +0x30 do not read as such)"; }
            }
        }
        run.Add(G, "data.friends", "Friends", ok, d, "Friends panel|Hiscores lookups", "state 2, entries of 0x78 with a printable name and a world 0..2000", got, "data.root").need = need;
    }
    {   // friends chat channel and player group: the rosters read as vectors of their stride, names as strings
        const std::uint64_t ch = rpm<std::uint64_t>(h, root + kOffFriendsChat).value_or(0);
        int ok = kPass; std::string d, got, need;
        if (ch <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: friends chat pointer at MainData+" + hx(kOffFriendsChat) + " not readable" : "friends chat not set before login"; need = c.inWorld ? "" : "log in"; }
        else {
            const std::string name = social_name(h, ch + 0x08);
            const std::uint64_t u0 = rpm<std::uint64_t>(h, ch + 0x40).value_or(0), u1 = rpm<std::uint64_t>(h, ch + 0x48).value_or(0);
            const int rank = rpm<std::int32_t>(h, ch + 0x38).value_or(-9);
            if (name.empty()) { ok = kUnchecked; d = "not in a friends chat (channel name empty, rank " + std::to_string(rank) + ")"; need = "join a friends chat"; }
            else if (u0 <= 0x10000 || u1 < u0 || (u1 - u0) % 0x50) { ok = kFail; d = "FORMAT: in channel '" + name + "' but the user vector at +0x40 does not read as 0x50-byte entries"; }
            else {
                const int n = (int)((u1 - u0) / 0x50); int good = 0;
                for (int i = 0; i < n && i < 400; ++i) {
                    const int w = rpm<std::int32_t>(h, u0 + (std::uint64_t)i * 0x50 + 0x30).value_or(-1);
                    if (printable_name(social_name(h, u0 + (std::uint64_t)i * 0x50)) && w >= 0 && w <= 2000) ++good;
                }
                d = "channel '" + name + "', " + std::to_string(good) + "/" + std::to_string(n) + " users read cleanly, own rank " + std::to_string(rank);
                if (n && good * 10 < n * 9) { ok = kFail; d = "FORMAT: " + d; }
            }
            got = d;
        }
        run.Add(G, "data.friendschat", "Friends chat", ok, d, "Social panel: friends chat", "a channel name, users of 0x50 with a name and a world 0..2000", got, "data.root").need = need;
    }
    {
        const std::uint64_t pgm = rpm<std::uint64_t>(h, root + kOffPlayerGroup).value_or(0);
        const std::uint64_t g = pgm > 0x10000 ? rpm<std::uint64_t>(h, pgm + 8).value_or(0) : 0;
        int ok = kPass; std::string d, got, need;
        if (g <= 0x10000) { ok = kUnchecked; d = "no player group (the group pointer at [MainData+" + hx(kOffPlayerGroup) + "]+8 is null)"; need = "a player group (group ironman)"; }
        else {
            const std::uint64_t m0 = rpm<std::uint64_t>(h, g + 0x60).value_or(0), m1 = rpm<std::uint64_t>(h, g + 0x68).value_or(0);
            const int maxSize = (int)rpm<std::int16_t>(h, g + 0x22).value_or(0);
            if (m0 <= 0x10000 || m1 < m0 || (m1 - m0) % 0xB0) { ok = kFail; d = "FORMAT: member vector at +0x60 does not read as 0xB0-byte entries"; }
            else {
                const int n = (int)((m1 - m0) / 0xB0); int good = 0;
                for (int i = 0; i < n && i < 64; ++i) {
                    const std::uint64_t e = m0 + (std::uint64_t)i * 0xB0;
                    if (printable_name(social_name(h, e + 0x18)) && rpm<std::uint8_t>(h, e + 0x34).value_or(2) <= 1) ++good;
                }
                d = "group '" + social_name(h, g + 8) + "', " + std::to_string(good) + "/" + std::to_string(n) + " members with a name and an online flag, max size " + std::to_string(maxSize);
                if (n && good * 10 < n * 9) { ok = kFail; d = "FORMAT: " + d; }
                else if (maxSize <= 0 || maxSize > 64) { ok = kWarn; d = "FORMAT: " + d + " (max size at +0x22 out of range)"; }
            }
            got = d;
        }
        run.Add(G, "data.group", "Player group", ok, d, "Social panel: group roster|Group vars", "members of 0xB0 with a name and an online flag, max size 1..64", got, "data.root").need = need;
    }
    {
        const std::uint64_t store = rpm<std::uint64_t>(h, root + kOffChatStore).value_or(0);
        int ok = kPass; std::string d, got, need;
        if (store <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: chat store pointer at MainData+" + hx(kOffChatStore) + " not readable" : "chat store not set before login"; need = c.inWorld ? "" : "log in"; }
        else {
            const std::string arr = chat_store_json(h, root, 8);
            rtx::health::JVal v;
            if (!rtx::health::ParseJson(arr, v) || v.a.empty()) { ok = c.inWorld ? kWarn : kUnchecked; d = "UNVERIFIED: no record for the newest id (empty chat, or the store layout moved)"; need = "a chat line"; }
            else {
                int sane = 0;
                for (const auto& r : v.a) { const long long t = r.num("type", -1); if (t >= 0 && t < 200) ++sane; }
                d = std::to_string(sane) + "/" + std::to_string(v.a.size()) + " newest records with a known type";
                got = d;
                if (sane * 10 < (int)v.a.size() * 8) { ok = kFail; d = "FORMAT: " + d + " (type field +0x20 does not read as a message type)"; }
            }
        }
        run.Add(G, "data.chat", "Chat store", ok, d, "Chat log|Chat alerts", "80 % of the newest records with a type < 200", got, "data.root").need = need;
    }
    {
        const std::uint64_t ip = rpm<std::uint64_t>(h, root + kOffInputProc).value_or(0);
        int ok = kPass; std::string d, got, need;
        if (ip <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: input processor pointer at MainData+" + hx(kOffInputProc) + " not readable" : "input processor not set before login"; need = c.inWorld ? "" : "log in"; }
        else {
            const std::uint64_t slot = rpm<std::uint64_t>(h, ip + 0x13F8).value_or(1);
            got = hx(slot);
            if (slot != 0 && (slot <= 0x10000 || slot > 0x00007FFFFFFFFFFFull)) { ok = kFail; d = "FORMAT: hover slot " + hx(slot) + " at +0x13F8 is not a pointer"; }
            else if (slot) d = "hovering " + hx(slot);
            else { ok = kUnchecked; d = "nothing hovered: the slot reads 0 whether or not it moved"; need = "hover an item or entity"; }
        }
        run.Add(G, "data.hover", "Hover slot", ok, d, "Hover tooltips|Entity hover", "0 or a heap pointer", got, "data.root").need = need;
    }
    {   // the var change-stamp map: var ids set by scripts in the last 500 ms, stamped engine clock + 500
        std::vector<VarStamp> stamps; std::uint32_t cap = 0;
        const bool readable = read_var_stamps(h, root, stamps, &cap);
        const long long clock = (long long)rpm<std::uint64_t>(h, c.mgva + kEngineClock).value_or(0);
        int ok = kPass; std::string d, got = "buckets " + std::to_string(cap) + ", " + std::to_string(stamps.size()) + " stamps";
        if (!readable) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "FORMAT: " + std::to_string(cap) + " buckets at MainData+" + hx(kOffVarpMgr) + "+0x38178 do not read as a hash map" : "stamp map empty before login"; }
        else {
            // only a stamp near the clock is judged (a script set within the last second): older
            // entries and the second setter's flag-1 marks carry other values
            int recent = 0, agree = 0;
            for (const auto& s : stamps) {
                const long long ahead = s.stamp - clock;
                if (s.flag != 0 || ahead <= -1000 || ahead > 600) continue;
                ++recent;
                std::uint8_t nb[0x30];
                if (read_varp_node(h, root, s.id, nb) == VarRead::Found) ++agree;
            }
            d = std::to_string(cap) + " buckets, " + std::to_string(stamps.size()) + " stamps, " + std::to_string(recent) + " within 500 ms of the clock";
            if (recent) { d += ", " + std::to_string(agree) + " of those are set varps"; if (agree * 10 < recent * 9) { ok = kFail; d = "FORMAT: " + d + " (the keys are not var ids or the stamps are not engine clock + 500)"; } }
        }
        run.Add(G, "data.varstamps", "Var change stamps", ok, d, "Var updates (script sets)", "a hash map; a stamp within 500 ms of the clock names a set varp", got, "data.varps").need = ok == kUnchecked ? "log in" : "";
    }
    {   // ability cooldowns from the action bar and the cache's cooldown varc pairs
        CooldownScan sc;
        const std::string js = c.inWorld ? cooldowns_json(h, root, c.mgva, &sc) : std::string();
        int ok = kPass; std::string d, need;
        if (!c.inWorld) { ok = kUnchecked; d = "read in the world"; need = "log in"; }
        else if (!sc.pairs && rtx::cache::AbilityConfigsJson().size() <= 2) { ok = kUnchecked; d = "ability configs not loaded"; need = "an open cache"; }
        else if (!sc.pairs) { ok = kFail; d = "GONE: no cooldown varc pairs from the cache (the cache row 'cooldown pairs' names the script shape)"; }
        else if (!sc.bars) { ok = kUnchecked; d = "no action bar open"; need = "open an action bar with abilities on it"; }
        else if (sc.known == 0) { ok = kUnchecked; d = "none of the " + std::to_string(sc.barSlots) + " bar slots is an ability the cache names (prayers, spells, items)"; need = "an ability on the action bar"; }
        else if (sc.paired == 0 && sc.known >= 4) { ok = kFail; d = "GONE: 0/" + std::to_string(sc.known) + " named bar abilities have a cooldown varc pair (ability ids or the pairs changed)"; }
        else d = std::to_string(sc.paired) + "/" + std::to_string(sc.known) + " named bar abilities have a cooldown varc pair (" + std::to_string(sc.barSlots) + " slots), " + std::to_string(sc.counting) + " counting down";
        run.Add(G, "data.cooldowns", "Ability cooldowns", ok, d, "Ability cooldowns|Ability Bar panel", "a cooldown varc pair on the bar abilities the cache names",
                std::to_string(sc.paired) + "/" + std::to_string(sc.known) + " paired, " + std::to_string(sc.counting) + " counting", "data.varcs").need = need;
    }
    {   // state fields read beside the var stores: system update, members world, login replies, camera
        const std::uint64_t wi = rpm<std::uint64_t>(h, root + kOffWorldInfo).value_or(0);
        const std::uint64_t lm = rpm<std::uint64_t>(h, root + kOffLoginMgr).value_or(0);
        const std::uint64_t cn = rpm<std::uint64_t>(h, root + kOffConnection).value_or(0);
        const std::uint64_t mm = rpm<std::uint64_t>(h, root + kOffMapMgr).value_or(0);
        const std::uint64_t vw = rpm<std::uint64_t>(h, root + kOffVarWatch).value_or(0);
        int ok = kPass; std::string d, bad;
        if (wi <= 0x10000 || lm <= 0x10000 || cn <= 0x10000 || mm <= 0x10000 || vw <= 0x10000) {
            ok = c.inWorld ? kFail : kUnchecked;
            d = c.inWorld ? "GONE: a state object pointer is null (world info " + hx(wi) + ", login " + hx(lm) + ", connection " + hx(cn) + ", map " + hx(mm) + ", var watch " + hx(vw) + ")" : "state objects are set in the world";
        } else {
            const int reboot = rpm<std::int32_t>(h, wi + 0xC).value_or(-1), members = rpm<std::uint8_t>(h, wi + 0x10).value_or(9);
            const int loginReply = rpm<std::int32_t>(h, lm + 0x16C).value_or(-1), lobbyReply = rpm<std::int32_t>(h, lm + 0x1B8).value_or(-1), logout = rpm<std::int32_t>(h, cn + 0x50).value_or(-1);
            const float fov = rpm<float>(h, mm + 0x220).value_or(0.f), fovX = rpm<float>(h, mm + 0x224).value_or(0.f);
            const long long seq = read_var_seq(h, root);
            if (reboot < 0 || reboot > 100000000) bad += " system update " + std::to_string(reboot);
            if (members > 1) bad += " members byte " + std::to_string(members);
            if (loginReply < 0 || loginReply > 255) bad += " login reply " + std::to_string(loginReply);
            if (lobbyReply < 0 || lobbyReply > 255) bad += " lobby reply " + std::to_string(lobbyReply);
            if (logout < -1 || logout > 255) bad += " logout reason " + std::to_string(logout);
            if (!(fov > 0.1f && fov < 3.f && fovX > 0.1f && fovX < 3.5f)) bad += " fov " + std::to_string(fov) + "/" + std::to_string(fovX);   // the wider one depends on the window shape
            if (seq < 0) bad += " var change counter unreadable";
            d = "system update " + std::to_string(reboot) + " ticks, members world " + std::to_string(members) + ", login reply " + std::to_string(loginReply) + ", lobby reply " + std::to_string(lobbyReply) +
                ", logout reason " + std::to_string(logout) + ", fov " + std::to_string((int)(fov * 180.f / 3.14159265f)) + " deg, var changes " + std::to_string(seq);
            if (!bad.empty() && !c.inWorld) { ok = kUnchecked; d = "the fields are set at login (" + d + " before it)"; }   // the lobby holds a login reply of -2
            else if (!bad.empty()) { ok = kFail; d = "FORMAT: " + d + " (out of range:" + bad + ")"; }
        }
        run.Add(G, "data.state", "Client state fields", ok, d, "System update alert|Members world|Login replies|FOV|Var change signal",
                "update 0..1e8 ticks, members 0/1, replies 0..255, fov 6..170 deg, a change counter", d, "data.root").need = ok == kUnchecked ? "log in" : "";
    }
    {
        const std::uint64_t mm = rpm<std::uint64_t>(h, root + rtx::scn::kMiniMap).value_or(0);
        int ok = kPass; std::string d, got, need;
        if (mm <= 0x10000) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: minimap pointer at MainData+" + hx(rtx::scn::kMiniMap) + " not readable" : "minimap not set before login"; need = c.inWorld ? "" : "log in"; }
        else {
            const int dx = rpm<std::int32_t>(h, mm + rtx::scn::kDestX).value_or(-1), dy = rpm<std::int32_t>(h, mm + rtx::scn::kDestY).value_or(-1);
            got = std::to_string(dx) + "," + std::to_string(dy);
            if (dx <= 0 || dy <= 0) { ok = kUnchecked; d = "no destination set: the fields read 0 whether or not they moved"; need = "click a tile to walk to"; }
            else {
                d = "destination " + std::to_string(dx / 512) + "," + std::to_string(dy / 512);
                const bool centred = (dx % 512) == 256 && (dy % 512) == 256;
                bool inReach = true;
                if (c.localSec) {
                    const float px = rpm<float>(h, c.localSec + rtx::scn::kPosX).value_or(0), py = rpm<float>(h, c.localSec + rtx::scn::kPosY).value_or(0);
                    inReach = std::fabs(px - dx) < 104 * 512 && std::fabs(py - dy) < 104 * 512;
                }
                if (!centred || !inReach) { ok = kFail; d = "FORMAT: " + d + (centred ? " (not near the player)" : " (not a tile centre: +0x14/+0x18 are not fine coordinates)"); }
            }
        }
        run.Add(G, "data.dest", "Walk destination", ok, d, "Destination marker", "fine coordinates on a tile centre within 104 tiles of the player", got, "data.root").need = need;
    }
}

// ---- Player ----
void health_player(HCtx& c, rtx::health::Run& run) {
    const char* G = "Player";
    HANDLE h = c.h;
    if (!c.inWorld) {
        run.Add(G, "player.account", "Account", kUnchecked, "not in the world", "Account|Member state", "", "", "data.root").need = "log in";
        return;
    }
    const std::uint64_t acct = rpm<std::uint64_t>(h, c.root + kOffAccount).value_or(0);
    {
        int ok = kPass; std::string d, word, got;
        if (acct <= 0x10000) { ok = kFail; word = "GONE"; d = "account object pointer at MainData+" + hx(kOffAccount) + " not readable"; }
        else {
            std::string name = read_jagstring(h, acct + 0x68);
            if (name.empty()) if (auto pl = rpm<std::uint64_t>(h, acct + 0x58); pl && *pl > 0x10000) name = read_jagstring(h, *pl + 0xF8);
            const int member = rpm<std::uint8_t>(h, acct + kOffAcctIsMember).value_or(0xFF);
            const std::uint64_t expiry = rpm<std::uint64_t>(h, acct + kOffAcctExpiry).value_or(0);
            const int uid = c.localUid;
            d = "uid " + std::to_string(uid) + ", member " + std::to_string(member);
            got = d + (name.empty() ? ", no name" : ", named");
            if (name.empty()) { ok = kFail; word = "GONE"; d += ", no name"; }
            if (member != 0 && member != 1) { ok = kFail; if (word.empty()) word = "FORMAT"; d += " (member byte not 0/1)"; }
            if (uid < 1 || uid > 2047) { ok = kFail; if (word.empty()) word = "FORMAT"; d += " (uid out of range)"; }
            if (c.localSec) {
                const int secUid = rpm<std::int32_t>(h, c.localSec + rtx::scn::kUid).value_or(-1);
                const std::string sn = sec_name(h, c.localSec);
                if (secUid != uid) { ok = kFail; if (word.empty()) word = "MOVED"; d += "; actor uid " + std::to_string(secUid); }
                if (!name.empty() && norm_name(sn) != norm_name(name)) { ok = kFail; if (word.empty()) word = "MOVED"; d += "; actor name differs from the account name"; }
            }
            run.Fact("account.expiryRaw", std::to_string(expiry));
        }
        if (!word.empty()) d = word + ": " + d;
        run.Add(G, "player.account", "Account", ok, d, "Account key (bank cache)|Member state|Premier", "a name, member 0/1, uid 1..2047 equal to the actor's", got, "calib.kOffAccount");
    }
    {
        int ok = kPass; std::string d;
        std::uint64_t viaReg = c.localUid >= 0 ? local_player_sec_fast(h, c.root, c.localUid) : 0;
        std::uint64_t viaScan = 0;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> secs; scene_secs(c, secs);
        for (const auto& es : secs)
            if (rpm<std::uint8_t>(h, es.second + rtx::scn::kType).value_or(0xFF) == 2 && rpm<std::int32_t>(h, es.second + rtx::scn::kUid).value_or(-1) == c.localUid) { viaScan = es.second; break; }
        if (!viaReg) { ok = kFail; d = "GONE: registry at MainData+" + hx(kOffPlayers) + " does not give the local player"; }
        else if (viaScan && viaReg != viaScan) { ok = kFail; d = "MOVED: registry and scene disagree on the local player's actor"; }
        else d = "registry and scene agree";
        if (!viaScan) { if (ok == kPass) { ok = kWarn; d = "GONE: " + d; } d += "; local player not in the actor vector"; }
        run.Add(G, "player.registry", "Player registry", ok, d, "Terrain heights|In-frame labels|Player State", "registry entry == the scene's actor with the local uid",
                "registry " + hx(viaReg) + ", scene " + hx(viaScan), "data.root");
    }
    {
        const std::uint64_t stats = rpm<std::uint64_t>(h, c.root + kOffStats).value_or(0);
        const std::uint64_t block = stats > 0x10000 ? rpm<std::uint64_t>(h, stats + kStatsInner).value_or(0) : 0;
        int ok = kPass; std::string d, word;
        if (block <= 0x10000) { ok = kFail; word = "GONE"; d = "skill block at [MainData+" + hx(kOffStats) + "]+" + hx(kStatsInner) + " not readable"; }
        else {
            const int count = rpm<std::uint8_t>(h, block + 8).value_or(0);
            const std::uint64_t arr = rpm<std::uint64_t>(h, block + 0x10).value_or(0);
            const int energy = rpm<std::int32_t>(h, block + 0x18).value_or(-1), weight = rpm<std::int32_t>(h, block + 0x1C).value_or(-9999);
            int agree = 0, n = 0; std::string bad;
            if (count == 29 && arr > 0x10000) {
                std::vector<std::uint8_t> sb(29 * 0x18);
                if (rpm_bytes(h, arr, sb.data(), sb.size())) {
                    for (int i = 0; i < 29; ++i) {
                        const std::uint8_t* e = sb.data() + (std::size_t)i * 0x18;
                        const int xp = *(const std::int32_t*)(e + 0xC), lvl = *(const std::int32_t*)(e + 0x10), boosted = *(const std::int32_t*)(e + 0x14);
                        if (i == 26) continue;                         // Invention: its own curve
                        ++n;
                        const bool levelOk = lvl == level_for_xp(xp, 99) || lvl == level_for_xp(xp, 110) || lvl == level_for_xp(xp, 120);
                        const bool ok1 = levelOk && boosted >= 0 && boosted <= lvl + 30;
                        if (ok1) ++agree; else if (bad.size() < 60) bad += " " + std::to_string(i) + ":" + std::to_string(lvl) + "/" + std::to_string(xp);
                    }
                }
            }
            d = std::to_string(count) + " skills, " + std::to_string(agree) + "/" + std::to_string(n) + " levels match their xp, energy " + std::to_string(energy) + ", weight " + std::to_string(weight);
            if (count > 29) { ok = kFail; word = "NEW"; d += " (a new skill)"; }
            else if (count != 29) { ok = kFail; word = "MOVED"; d += " (count byte moved?)"; }
            if (n && agree < n) { ok = kFail; if (word.empty()) word = "FORMAT"; d += "; off:" + bad; }
            if (energy < 0 || energy > 100) { ok = kFail; if (word.empty()) word = "FORMAT"; d += "; energy out of range"; }
            if (weight < -100 || weight > 300) { ok = kFail; if (word.empty()) word = "FORMAT"; d += "; weight out of range"; }
        }
        if (!word.empty()) d = word + ": " + d;
        run.Add(G, "player.skills", "Skills", ok, d, "Skills panel|XP tracker|Run energy|Weight", "29 skills, every level on its xp curve, energy 0..100, weight -100..300",
                block > 0x10000 ? d.substr(d.find_first_of("0123456789")) : std::string("no block"), "calib.kStatsInner");
    }
    {
        const std::string pj = PlayerInfoJson(c.pid);
        const bool in = pj.find("\"in\":true") != std::string::npos;
        run.Add(G, "player.state", "Player state", in ? kPass : kFail, in ? "local player found" : "GONE: local player not found in the scene",
                "Player State|Tasks|Overlays", "in: true", in ? "in: true" : "in: false", "player.registry");
    }
}

// ---- Scene ----
void health_scene(HCtx& c, rtx::health::Run& run) {
    const char* G = "Scene";
    HANDLE h = c.h;
    {
        const std::uint32_t wo = g_workerOff.load(), mo = g_matrixOff.load();
        const std::int32_t cr = g_camPosRel.load();
        std::string d = "worker " + hx(wo) + ", matrix " + hx(mo) + ", camera " + std::to_string(cr);
        int ok = kPass;
        if (wo != rtx::scn::kWorkerOffDefault) { ok = kWarn; d += "; worker moved from " + hx(rtx::scn::kWorkerOffDefault); }
        if (mo != rtx::scn::kMatrixOffDefault) { ok = kWarn; d += "; matrix moved from " + hx(rtx::scn::kMatrixOffDefault); }
        if (cr != rtx::scn::kCamPosRelDefault) { ok = kWarn; d += "; camera position moved from " + std::to_string(rtx::scn::kCamPosRelDefault); }
        if (ok == kWarn) d = "MOVED: " + d + " (the scan found them, the compiled defaults are behind)";
        if (!c.worker) { ok = c.inWorld ? kFail : kUnchecked; d = c.inWorld ? "GONE: scene worker not found from the worldView (worker offset scan failed)" : "scene worker is read in the world"; }
        run.Fact("scene.workerOff", hx(wo)); run.Fact("scene.matrixOff", hx(mo)); run.Fact("scene.camRel", std::to_string(cr));
        run.Add(G, "scene.offsets", "Scene offsets", ok, d, "Scene|Overlays|Camera", "worker " + hx(rtx::scn::kWorkerOffDefault) + ", matrix " + hx(rtx::scn::kMatrixOffDefault) + ", camera " + std::to_string(rtx::scn::kCamPosRelDefault),
                "worker " + hx(wo) + ", matrix " + hx(mo) + ", camera " + std::to_string(cr), "data.root").need = ok == kUnchecked ? "log in" : "";
    }
    if (!c.inWorld || !c.worker) return;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> secs; scene_secs(c, secs);
    std::map<int, int> types;
    int npcs = 0, npcNamed = 0, players = 0, playersOk = 0, motionOk = 0, motionN = 0, combatN = 0, combatOk = 0, combatStale = 0, ovN = 0, ovOk = 0;
    int fxN = 0, fxOk = 0; std::string npcBad, fxBad, playerBad, combatBad;
    const int cycleNow = (int)rpm<std::uint32_t>(h, c.root + kOffClientClock).value_or(0);
    int curOk = 0, curMorphed = 0, curBadN = 0; std::string curBad;
    int visN = 0, visOk = 0; std::string visBad;
    const float px = c.localSec ? rpm<float>(h, c.localSec + rtx::scn::kPosX).value_or(0) : 0.f;
    const float py = c.localSec ? rpm<float>(h, c.localSec + rtx::scn::kPosY).value_or(0) : 0.f;
    const int seqMax = rtx::cache::IndexInfo(20).maxArchive;
    c.t4Seen = 0; c.t4Gfx.clear();
    for (const auto& es : secs) {
        const std::uint64_t sec = es.second;
        const int t = rpm<std::uint8_t>(h, sec + rtx::scn::kType).value_or(0xFF);
        ++types[t];
        if (t == 1 || t == 2) {
            const int anim = rpm<std::int32_t>(h, sec + 0xA90).value_or(-2);
            const float qy = rpm<float>(h, sec + rtx::scn::kFaceTargetY).value_or(0), qz = rpm<float>(h, sec + 0x1E4).value_or(0), qw = rpm<float>(h, sec + rtx::scn::kFaceTargetW).value_or(0);
            const float norm = qy * qy + qz * qz + qw * qw;
            float nq[4] = {};
            const float nnorm = rpm_bytes(h, es.first + rtx::scn::kNodeYaw, nq, sizeof(nq)) ? nq[0] * nq[0] + nq[1] * nq[1] + nq[2] * nq[2] + nq[3] * nq[3] : 0.f;
            ++motionN;
            if ((anim == -1 || (anim >= 0 && (seqMax < 0 || anim <= (seqMax + 1) * 128))) && norm > 0.96f && norm < 1.04f && nnorm > 0.96f && nnorm < 1.04f) ++motionOk;
            const std::uint64_t ov = rpm<std::uint64_t>(h, sec + rtx::scn::kOverhead).value_or(0);
            if (ov > 0x10000) {
                ++ovN;
                const std::uint64_t sb = rpm<std::uint64_t>(h, ov + rtx::scn::kOvSlots).value_or(0), se = rpm<std::uint64_t>(h, ov + 0x30).value_or(0);
                const int splats = rpm<std::int32_t>(h, ov + rtx::scn::kOvSplatCount).value_or(-1);
                if (se >= sb && (se - sb) % rtx::scn::kBarStride == 0 && splats >= 0 && splats <= 6) ++ovOk;
            }
        }
        if (t == 1) {
            ++npcs;
            const int cfg = rpm<std::int32_t>(h, sec + rtx::scn::kConfig).value_or(-1);
            const std::string live = norm_name(sec_name(h, sec));
            const rtx::cache::NpcMeta m = rtx::cache::GetNpc(cfg);
            bool match = !m.name.empty() && norm_name(m.name) == live;
            if (!match && cfg >= 0) {   // a morph: the live name is one of its variants
                int vb = -1, vp = -1, def = -1; std::vector<int> vars;
                if (rtx::cache::GetNpcMorph(cfg, vb, vp, def, vars))
                    for (int v : vars) if (v >= 0 && norm_name(rtx::cache::GetNpc(v).name) == live) { match = true; break; }
            }
            if (!match && !live.empty() && !m.name.empty()) {
                // Some NPCs carry their internal name there, not the one shown ("combatv2_necromancy_
                // spirit_vengeful_ghost" for the Vengeful Ghost), and this read stops at 39 characters.
                // The config id still names it when the shown name closes the internal one, or as much
                // of it as the read holds.
                auto letters = [](const std::string& s) { std::string o; for (unsigned char ch : s) if (std::isalnum(ch)) o.push_back((char)std::tolower(ch)); return o; };
                const std::string la = letters(live), ca = letters(m.name);
                const bool capped = live.size() >= 39;
                for (std::size_t k = ca.size(); k >= 5 && k * 2 >= ca.size() && !match; --k) {
                    if (k < ca.size() && !capped) break;
                    match = la.size() > k && la.compare(la.size() - k, k, ca, 0, k) == 0;
                }
            }
            if (match || live.empty()) ++npcNamed;
            else if (npcBad.size() < 80) npcBad += " " + std::to_string(cfg) + "='" + sec_name(h, sec) + "'";
            {   // the current type id against the morph evaluation of the base id
                const int cur = rpm<std::int32_t>(h, sec + rtx::scn::kNpcCur).value_or(-2);
                bool hid = false;
                const rtx::cache::NpcMeta rm = resolve_npc(h, c.root, cfg, &hid);
                if (cur != cfg) ++curMorphed;
                if (hid ? cur == -1 : cur == (rm.id >= 0 ? rm.id : cfg)) ++curOk;
                else if (curBadN < 4) { curBad += " " + std::to_string(cfg) + "->" + std::to_string(cur); ++curBadN; }
            }
            const int lp = rpm<std::int32_t>(h, sec + rtx::scn::kLpCur).value_or(-1), lpMax = rpm<std::int32_t>(h, sec + rtx::scn::kLpMax).value_or(-1);
            const int target = rpm<std::int32_t>(h, sec + rtx::scn::kNpcTarget).value_or(-2);
            std::string sp; int bar = -1; actor_overhead_json(h, sec, sp, bar);
            {   // the level the game shows (sec+0x1178) against the cache's combat level of the drawn type
                const int cur = rpm<std::int32_t>(h, sec + rtx::scn::kNpcCur).value_or(-1);
                const int cl = rtx::cache::GetNpc(cur >= 0 ? cur : cfg).combat_level;
                NpcStats ns;
                if (cl > 0 && read_npc_stats(h, sec, ns)) {
                    ++visN;
                    if (ns.vis == cl) ++visOk;
                    else if (visBad.size() < 60) visBad += " " + std::to_string(cur >= 0 ? cur : cfg) + ":" + std::to_string(ns.vis) + "/" + std::to_string(cl);
                }
            }
            const bool fights = lpMax > 1 && rtx::cache::GetNpc(cfg).combat_level > 0;
            if (bar >= 0 && fights) {
                // a bar the game no longer draws keeps its last fill (stamps hours old seen on NPCs back
                // at full life points), so only a fresh one is judged, against the life points plus the
                // hits landed since its stamp (the bar follows a few ticks later, and a dying NPC reads 0
                // with the fill before the last hit); a hit 3 s old on a bar that has not moved for 10 s
                // is a stamp field that stopped moving, and counts against the row
                int stamp = -1, since = 0, hitCycle = -1; actor_bar_state(h, sec, cycleNow, stamp, since, hitCycle);
                const int stampAge = stamp > 0 ? cycleNow - stamp : -1, hitAge = hitCycle > 0 ? cycleNow - hitCycle : -1;
                const bool fresh = stampAge >= 0 && stampAge <= kBarFreshCycles;
                const bool hitUnfollowed = !fresh && hitAge >= kBarFollowCycles && hitAge <= kBarFreshCycles;
                if (!fresh && !hitUnfollowed) ++combatStale;
                else {
                    ++combatN;
                    auto agrees = [&](int points) { return points >= 0 && points <= lpMax && std::abs(bar - (int)((long long)points * 255 / lpMax)) <= 12; };
                    if (fresh && (agrees(lp) || agrees(lp + since)) && (target == -1 || (target >= 0 && target < 4096))) ++combatOk;
                    else if (combatBad.size() < 80) combatBad += " " + std::to_string(cfg) + ":" + std::to_string(lp) + "/" + std::to_string(lpMax) + " bar " + std::to_string(bar) + (fresh ? (since ? " (" + std::to_string(since) + " hit since the bar)" : "") : " (stamp " + std::to_string(stampAge) + " cycles still, hit " + std::to_string(hitAge) + " ago)");
                }
            }
        } else if (t == 2) {
            ++players;
            const std::string nm = sec_name(h, sec);
            const int cb = rpm<std::int32_t>(h, sec + rtx::scn::kCombat).value_or(-1), cb2 = rpm<std::int32_t>(h, sec + rtx::scn::kCombat + 4).value_or(-2);
            const int pl = rpm<std::int32_t>(h, sec + rtx::scn::kPlane).value_or(-1);
            // two combat levels: the one shown and a second that can sit above it (the local player
            // reads 140 and 152), each in 3..152; the name is UTF-8 as the client stores it
            const char* why = !utf8_name(nm) ? "name" : (cb < 3 || cb > 152) ? "combat" : (cb2 < 3 || cb2 > 152) ? "second combat" : (pl < 0 || pl > 3) ? "plane" : nullptr;
            if (!why) ++playersOk;
            else if (playerBad.size() < 100) playerBad += " '" + nm + "' " + why + " (" + std::to_string(cb) + "/" + std::to_string(cb2) + ", plane " + std::to_string(pl) + ")";
        } else if (t == 4 || t == 5) {
            ++fxN;
            bool ok = true;
            if (t == 4) {
                int gfx = -1;
                ok = t4_valid(h, sec, px, py, gfx);
                ++c.t4Seen;
                if (ok) ++c.t4Gfx[gfx];
                if (!ok && fxBad.size() < 60) fxBad += " gfx " + std::to_string(gfx);
            } else {   // the source tile, stored as fine ints like the type-4 position
                const int sx = rpm<std::int32_t>(h, sec + rtx::scn::kProjSrcX).value_or(0), sy = rpm<std::int32_t>(h, sec + rtx::scn::kProjSrcY).value_or(0);
                const float fx = rpm<float>(h, sec + rtx::scn::kProjSrcX).value_or(0), fy = rpm<float>(h, sec + rtx::scn::kProjSrcY).value_or(0);
                const bool asInt = std::fabs(sx - px) <= 104 * 512 && std::fabs(sy - py) <= 104 * 512;
                const bool asFloat = std::fabs(fx - px) <= 104 * 512 && std::fabs(fy - py) <= 104 * 512;
                if (!asInt && !asFloat) { ok = false; if (fxBad.size() < 60) fxBad += " projectile " + std::to_string(sx) + "," + std::to_string(sy); }
            }
            if (ok) ++fxOk;
        }
    }
    c.fxRead = true;
    {
        std::string hist; bool unknown = false;
        for (const auto& kv : types) {
            hist += (hist.empty() ? "" : " ") + std::to_string(kv.first) + "x" + std::to_string(kv.second);
            if (!(kv.first <= 5 || (kv.first >= 10 && kv.first <= 13))) unknown = true;
        }
        const bool self = c.localSec != 0;
        const int tok = (!unknown && self) ? kPass : kFail;
        run.Add(G, "scene.types", "Actor types", tok, std::string(tok == kPass ? "" : unknown ? "FORMAT: " : "GONE: ") + "types " + hist + (self ? "" : "; no player with the local uid") + (unknown ? "; unknown type codes (type byte moved?)" : ""),
                "Scene panel|Overlays|Plugins scene API", "type codes in 1..5 and 10..13, a player with the local uid", "types " + hist, "scene.offsets");
    }
    {
        const int ok = npcs == 0 ? kUnchecked : (npcNamed * 10 >= npcs * 9 ? kPass : kFail);
        run.Add(G, "scene.npcs", "NPC names", ok,
                npcs == 0 ? "no NPCs nearby" : std::string(ok == kFail ? "MOVED: " : "") + std::to_string(npcNamed) + "/" + std::to_string(npcs) + " config ids name the live NPC" + (ok == kFail ? " (the config id at sec+" + hx(rtx::scn::kConfig) + " or the name buffer moved)" : "") + (npcBad.empty() ? "" : "; e.g." + npcBad),
                "NPC labels|Boss info|Slayer|Plugins", "90 % named", std::to_string(npcNamed) + "/" + std::to_string(npcs), "scene.types").need = npcs == 0 ? "an NPC nearby" : "";
    }
    {
        const bool curPass = curOk * 10 >= npcs * 9;
        run.Add(G, "scene.npctype", "NPC current type", npcs == 0 ? kUnchecked : (curPass ? kPass : kWarn),
                npcs == 0 ? std::string("no NPCs nearby") : std::string(curPass ? "" : "MOVED: ") +
                std::to_string(curOk) + "/" + std::to_string(npcs) + " current type ids at sec+" + hx(rtx::scn::kNpcCur) + " agree with the morph evaluation, " +
                std::to_string(curMorphed) + " differ from the base id" + (curPass ? "" : " (sec+" + hx(rtx::scn::kNpcCur) + " disagrees with the morph evaluation)") +
                (curBad.empty() ? "" : "; e.g." + curBad),
                "NPC hover ids|Interactable NPCs|Interaction target", "90 %", std::to_string(curOk) + "/" + std::to_string(npcs), "scene.types").need = npcs == 0 ? "an NPC nearby" : "";
    }
    {
        const bool pPass = playersOk * 10 >= players * 9;
        run.Add(G, "scene.players", "Players", players == 0 ? kUnchecked : (pPass ? kPass : kFail),
                players == 0 ? std::string("no other players nearby") : std::string(pPass ? "" : "FORMAT: ") + std::to_string(playersOk) + "/" + std::to_string(players) + " with a UTF-8 name, two combat levels in 3..152 and a plane" + (playerBad.empty() ? "" : "; off:" + playerBad),
                "Player labels|Nameplates", "90 %: UTF-8 name of 1..24 bytes, combat 3..152 at sec+" + hx(rtx::scn::kCombat) + " and +" + hx(rtx::scn::kCombat + 4) + " (the second can sit above the first), plane 0..3", std::to_string(playersOk) + "/" + std::to_string(players), "scene.types").need = players == 0 ? "another player nearby" : "";
    }
    run.Add(G, "scene.motion", "Animation and facing", motionN == 0 ? kUnchecked : (motionOk * 10 >= motionN * 9 ? kPass : kFail),
            motionN == 0 ? std::string("no players or NPCs nearby") : std::string(motionOk * 10 >= motionN * 9 ? "" : "FORMAT: ") + std::to_string(motionOk) + "/" + std::to_string(motionN) + " actors with a valid animation and a unit facing (turn target and entity node)",
            "Tick timers by animation|Facing arrows", "90 %", std::to_string(motionOk) + "/" + std::to_string(motionN), "scene.types").need = motionN == 0 ? "an actor nearby" : "";
    run.Add(G, "scene.npccombat", "NPC life points", combatN == 0 ? kUnchecked : (combatOk * 10 >= combatN * 9 ? kPass : kFail),
            combatN == 0 ? (combatStale ? std::to_string(combatStale) + " health bars, none stamped within " + std::to_string(kBarFreshCycles) + " cycles (no fight going on)" : std::string("no NPC with a health bar")) :
            std::string(combatOk * 10 >= combatN * 9 ? "" : "FORMAT: ") + std::to_string(combatOk) + "/" + std::to_string(combatN) + " fresh health bars agree with life points plus the hits landed since the bar's stamp" + (combatStale ? ", " + std::to_string(combatStale) + " stale bars skipped" : "") + (combatBad.empty() ? "" : "; e.g." + combatBad),
            "Boss HP|Combat log|NPC HP labels", "90 % of bars stamped within " + std::to_string(kBarFreshCycles) + " cycles of the client clock", std::to_string(combatOk) + "/" + std::to_string(combatN), "scene.types").need = combatN == 0 ? "an NPC in a fight nearby" : "";
    // judged from five NPCs on: a single scaled or instanced NPC can show another level than its config
    run.Add(G, "scene.npcvis", "NPC shown level", visN < 5 ? kUnchecked : (visOk * 10 >= visN * 9 ? kPass : kFail),
            visN < 5 ? std::string("UNVERIFIED: ") + std::to_string(visN) + " NPCs with a combat level in view, 5 needed (sec+" + hx(kNpcVisLevel) + (visN ? ": " + std::to_string(visOk) + "/" + std::to_string(visN) + " equal the cache level)" : " not checked)") : std::string(visOk * 10 >= visN * 9 ? "" : "FORMAT: ") + std::to_string(visOk) + "/" + std::to_string(visN) + " shown levels at sec+" + hx(kNpcVisLevel) + " equal the cache combat level" + (visBad.empty() ? "" : "; e.g." + visBad),
            "NPC hover stats|Scene NPC levels", "90 % of 5 or more", std::to_string(visOk) + "/" + std::to_string(visN), "scene.npcs").need = visN < 5 ? "five NPCs with a combat level in view" : "";
    run.Add(G, "scene.overhead", "Overhead objects", ovN == 0 ? kUnchecked : (ovOk == ovN ? kPass : kFail),
            ovN == 0 ? std::string("no actor with an overhead object") : std::string(ovOk == ovN ? "" : "FORMAT: ") + std::to_string(ovOk) + "/" + std::to_string(ovN) + " with a 0x1B0-stride bar vector and at most 6 splats",
            "Combat log|Head bars", "every overhead object", std::to_string(ovOk) + "/" + std::to_string(ovN), "scene.types").need = ovN == 0 ? "an actor with a head bar nearby" : "";
    run.Add(G, "scene.effects", "Effects and projectiles", fxN == 0 ? kUnchecked : (fxOk == fxN ? kPass : kFail),
            fxN == 0 ? "none nearby" : std::string(fxOk == fxN ? "" : "FORMAT: ") + std::to_string(fxOk) + "/" + std::to_string(fxN) + " near the player with a cache graphic" + (fxBad.empty() ? "" : ";" + fxBad),
            "Specials: graphic highlights|Clue scan ring|Projectiles", "every effect", std::to_string(fxOk) + "/" + std::to_string(fxN), "scene.types").need = fxN == 0 ? "an effect or projectile nearby" : "";
    // movement route: every player and NPC carries a 21-entry route at sec+0x268, and a moving
    // actor's newest entry (its true tile) sits near where it is drawn. A runner covers 2 tiles a tick
    // and is drawn up to 2 ticks behind, so 4 tiles; a moved layout reads tiles far off or none. Fewer
    // than 3 moving actors are too few to hold to 90 %: one runner at the edge would fail the row.
    // Steps leave from the front, so the read cursor stays at begin and the count is (write - read) / stride.
    {
        int total = 0, routes = 0, queueOk = 0, moving = 0, close = 0;
        for (const auto& es : secs) {
            if (total >= 300) break;
            const std::uint64_t sec = es.second;
            const int t = rpm<std::uint8_t>(h, sec + rtx::scn::kType).value_or(0xFF);
            if (t != 1 && t != 2) continue;
            const float fx = rpm<float>(h, sec + rtx::scn::kPosX).value_or(0), fy = rpm<float>(h, sec + rtx::scn::kPosY).value_or(0);
            if (fx <= 0.f || fy <= 0.f) continue;
            ++total;
            const std::uint64_t mm = rpm<std::uint64_t>(h, sec + rtx::scn::kMoveMgr).value_or(0);
            if (mm <= 0x10000) continue;
            const std::uint64_t beg = rpm<std::uint64_t>(h, mm + rtx::scn::kRouteBegin).value_or(0), end = rpm<std::uint64_t>(h, mm + rtx::scn::kRouteEnd).value_or(0);
            const std::uint64_t wr = rpm<std::uint64_t>(h, mm + rtx::scn::kRouteWrite).value_or(0);
            if (beg <= 0x10000 || end < beg || end - beg != 21 * rtx::scn::kRouteStride || wr < beg || wr > end) continue;
            ++routes;
            const std::uint64_t rd = rpm<std::uint64_t>(h, mm + rtx::scn::kRouteRead).value_or(0);
            const int cnt = rpm<std::int32_t>(h, mm + rtx::scn::kRouteCount).value_or(-1);
            if (rd == beg && cnt >= 0 && cnt <= 21 && (std::uint64_t)cnt == (wr - rd) / rtx::scn::kRouteStride) ++queueOk;
            const int tx = (int)(fx / 512.f), ty = (int)(fy / 512.f);
            int ttx = tx, tty = ty;
            if (!actor_true_tile(h, sec, ttx, tty)) continue;
            ++moving;
            if (std::abs(ttx - tx) <= 4 && std::abs(tty - ty) <= 4) ++close;
        }
        const bool routesOk = routes * 10 >= total * 9;
        const bool queuesOk = queueOk * 10 >= routes * 9;
        const bool few = moving > 0 && moving < 3 && close < moving;
        const bool moveOk = routesOk && (moving == 0 || few || close * 10 >= moving * 9);
        const bool ok = moveOk && queuesOk;
        const int st = total == 0 ? kUnchecked : !ok ? kFail : (routesOk && few) ? kUnchecked : kPass;
        run.Add(G, "scene.route", "Actor true tile", st,
                total == 0 ? std::string("no players or NPCs in the scene") : std::string(st == kFail ? "MOVED: " : "") +
                std::to_string(routes) + "/" + std::to_string(total) + " actors carry a movement route at sec+" + hx(rtx::scn::kMoveMgr) + ", " +
                std::to_string(queueOk) + "/" + std::to_string(routes) + " queues consistent, " +
                std::to_string(close) + "/" + std::to_string(moving) + " moving within 4 tiles of where they are drawn" +
                (few ? " (too few moving to judge)" : moveOk ? "" : " (route object or entry layout moved?)") +
                (queuesOk ? "" : " (read cursor or count moved?)"),
                "True tile overlay|Plugins scene API", "90 %",
                std::to_string(routes) + "/" + std::to_string(total) + " routes, " + std::to_string(queueOk) + "/" + std::to_string(routes) + " queues, " +
                std::to_string(close) + "/" + std::to_string(moving) + " near", "scene.types").need = total == 0 ? "an actor nearby" : st == kUnchecked ? "three or more actors moving nearby" : "";
    }
    // projection: the local player lands near the middle of the view
    {
        CamProbes probes; float m[16] = {};
        int ok = kUnchecked; std::string d = "camera matrix not readable", got;
        if (c.wv && read_view_matrix(h, c.pid, c.root, c.wv, probes, m) && c.localSec) {
            const float x = rpm<float>(h, c.localSec + rtx::scn::kPosX).value_or(0), y = rpm<float>(h, c.localSec + rtx::scn::kPosY).value_or(0), z = rpm<float>(h, c.localSec + rtx::scn::kPosZ).value_or(0);
            const float w = m[3] * x + m[11] * y + m[7] * z + m[15];
            if (w > 1.f) {
                const float nx = (m[0] * x + m[8] * y + m[4] * z + m[12]) / w, ny = (m[1] * x + m[9] * y + m[5] * z + m[13]) / w;
                char b[96]; std::snprintf(b, sizeof(b), "local player at %.2f, %.2f of the view", nx, ny);
                d = b; got = b;
                ok = (std::fabs(nx) < 0.5f && std::fabs(ny) < 0.5f) ? kPass : kWarn;
                if (ok == kWarn) d = "UNVERIFIED: " + d + " (expected near the centre; the camera may be panned)";
            } else { ok = kFail; d = "FORMAT: local player behind the camera (the matrix at the camera offset does not project the scene)"; got = "w <= 1"; }
        }
        run.Add(G, "scene.projection", "Projection", ok, d, "Overlays|Markers|In-frame labels", "local player within 0.5 of the view centre", got, "scene.offsets").need = ok == kUnchecked ? "the camera matrix (in the world with a scene)" : ok == kWarn ? "the camera centred on the player" : "";
    }
    // terrain: the actor stands on the live height grid (+5)
    {
        int ok = kUnchecked; std::string d = "no local player", got;
        if (c.localSec) {
            const float x = rpm<float>(h, c.localSec + rtx::scn::kPosX).value_or(0), y = rpm<float>(h, c.localSec + rtx::scn::kPosY).value_or(0), z = rpm<float>(h, c.localSec + rtx::scn::kPosZ).value_or(0);
            const int plane = rpm<std::int32_t>(h, c.localSec + rtx::scn::kPlane).value_or(0);
            const int tx = (int)(x / 512.f), ty = (int)(y / 512.f);
            if (LiveTerrainSnapshot(c.pid, tx, ty, plane)) {
                const std::int32_t h00 = LiveCornerHeight(c.pid, tx, ty, plane), h10 = LiveCornerHeight(c.pid, tx + 1, ty, plane);
                const std::int32_t h01 = LiveCornerHeight(c.pid, tx, ty + 1, plane), h11 = LiveCornerHeight(c.pid, tx + 1, ty + 1, plane);
                if (h00 == INT32_MIN || h10 == INT32_MIN || h01 == INT32_MIN || h11 == INT32_MIN) { ok = kFail; d = "GONE: height grid not readable at the player's tile"; got = "no corner heights"; }
                else {
                    const float fx = x / 512.f - tx, fy = y / 512.f - ty;
                    const float hb = (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy;
                    const std::int32_t lift = LiveTileLift(c.pid, tx, ty, plane);
                    const float want = hb + (lift == INT32_MIN ? 0 : lift) + 5.f;
                    char b[120]; std::snprintf(b, sizeof(b), "actor height %.1f, grid %.1f (off by %.1f)", z, want, z - want);
                    d = b; got = b;
                    ok = std::fabs(z - want) <= 1.5f ? kPass : (std::fabs(z - want) <= 64.f ? kWarn : kFail);
                    if (ok != kPass) d = "FORMAT: " + d + (ok == kWarn ? " (the grid or the lift reads a nearby field)" : " (the height grid chain does not land on the actor)");
                }
            } else { ok = kFail; d = "GONE: terrain snapshot failed (the scene's height grid chain did not resolve)"; got = "no snapshot"; }
        }
        run.Add(G, "scene.terrain", "Terrain heights", ok, d, "Tile markers|Ground overlays|In-frame markers", "actor height within 1.5 of grid + lift + 5", got, "player.registry").need = ok == kUnchecked ? "a local player in the scene" : "";
    }
    // world grid: the worldView owns it on the static map; its bounds are a box of mapsquares inside
    // 0..98 x 0..198 (the whole world, or the part loaded since a relog), the player's mapsquare lies
    // inside it and the region object there points back at its worldView and its own mapsquare
    {
        int ok = kUnchecked; std::string d = "no local player", got;
        const std::uint64_t wv = c.wv;
        if (wv > 0x10000 && c.localSec) {
            const std::uint64_t mgr = rpm<std::uint64_t>(h, wv + rtx::scn::kWvGrid).value_or(0);
            if (rpm<std::uint64_t>(h, wv + rtx::scn::kMapDef).value_or(0) != 0) {
                ok = kUnchecked;
                d = "instanced worldView, grid owner " + (mgr == wv ? std::string("self") : hx(mgr)) + " (instance layout not verified)";
            }
            else if (mgr != wv) { ok = kFail; d = "MOVED: grid owner [wv+" + hx(rtx::scn::kWvGrid) + "] is " + hx(mgr) + ", not the worldView (grid moved?)"; }
            else {
                ok = kPass;
                std::int32_t b[4] = { -1, -1, -1, -1 };
                rpm_bytes(h, mgr + rtx::scn::kGridMin, b, sizeof(b));
                got = std::to_string(b[0]) + "," + std::to_string(b[1]) + ".." + std::to_string(b[2]) + "," + std::to_string(b[3]);
                d = "self, bounds " + got;
                const int rx = (int)(rpm<float>(h, c.localSec + rtx::scn::kPosX).value_or(0) / 512.f) >> 6;
                const int ry = (int)(rpm<float>(h, c.localSec + rtx::scn::kPosY).value_or(0) / 512.f) >> 6;
                const std::string ms = std::to_string(rx) + "," + std::to_string(ry);
                if (b[0] < 0 || b[1] < 0 || b[2] > 98 || b[3] > 198 || b[0] > b[2] || b[1] > b[3]) { ok = kFail; d = "FORMAT: " + d + " (not a box inside 0,0..98,198: the bounds at +" + hx(rtx::scn::kGridMin) + " moved)"; }
                else if (rx < b[0] || rx > b[2] || ry < b[1] || ry > b[3]) { ok = kFail; d = "FORMAT: " + d + ", player mapsquare " + ms + " outside the grid"; }
                else {
                    const std::uint64_t rows = rpm<std::uint64_t>(h, mgr + rtx::scn::kGridRows).value_or(0);
                    const std::uint64_t col = rows > 0x10000 ? rpm<std::uint64_t>(h, rows + (std::uint64_t)(rx - b[0]) * 0x18).value_or(0) : 0;
                    const std::uint64_t region = col > 0x10000 ? rpm<std::uint64_t>(h, col + (std::uint64_t)(ry - b[1]) * 0x18 + 8).value_or(0) : 0;
                    if (region <= 0x10000) { ok = kFail; d = "GONE: " + d + ", no region object at mapsquare " + ms; }
                    else {
                        const std::uint64_t back = rpm<std::uint64_t>(h, region + rtx::scn::kRegionWv).value_or(0);
                        const int qx = rpm<std::int32_t>(h, region + rtx::scn::kRegionRx).value_or(-1), qy = rpm<std::int32_t>(h, region + rtx::scn::kRegionRy).value_or(-1);
                        if (back != wv || qx != rx || qy != ry) {
                            ok = kFail;
                            d = "MOVED: " + d + ", region at " + ms + " names worldView " + hx(back) + ", mapsquare " + std::to_string(qx) + "," + std::to_string(qy) + " (region layout moved?)";
                        } else d += ", player mapsquare " + ms + " inside, region back reference";
                    }
                }
                d += "; +" + hx(rtx::scn::kMapDef) + " = 0 (not instanced)";
                if (rpm<std::uint64_t>(h, wv + rtx::scn::kWvBox).value_or(0) != wv) {
                    if (ok == kPass) { ok = kWarn; d = "MOVED: " + d; }
                    d += "; [wv+" + hx(rtx::scn::kWvBox) + "] is not the worldView (loc menu box moved?)";
                }
            }
        }
        run.Add(G, "scene.grid", "World grid", ok, d, "Terrain heights|Tile markers", "bounds a box inside 0,0..98,198 (the whole world or the loaded part) holding the player's mapsquare, the region there pointing back", got, "scene.offsets").need = ok == kUnchecked ? (c.localSec ? "the static map (not an instance)" : "a local player in the scene") : "";
    }
    // live scenery against the cache's map placements (reader side, works from the command line)
    {
        std::vector<LiveLoc> locs;
        int ok = kUnchecked; std::string d = "no live scenery read";
        if (ReadLiveLocs(c.pid, locs) && !locs.empty()) {
            std::unordered_map<int, std::unordered_set<long long>> sets;
            int matched = 0, total = 0;
            for (const auto& L : locs) {
                if (total >= 400) break;
                ++total;
                const int rx = L.tx >> 6, ry = L.ty >> 6, key = (rx << 8) | ry;
                auto it = sets.find(key);
                if (it == sets.end()) {
                    auto& s = sets[key];
                    for (const auto& pl : rtx::cache::RegionLocations(rx, ry))
                        for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy)
                            s.insert(((long long)pl.id << 28) | ((long long)(rx * 64 + pl.x + dx) << 14) | (long long)(ry * 64 + pl.y + dy));
                    it = sets.find(key);
                }
                if (it->second.count(((long long)L.id << 28) | ((long long)L.tx << 14) | (long long)L.ty)) ++matched;
            }
            d = std::to_string(matched) + "/" + std::to_string(total) + " live scenery ids sit on a cache placement of that id";
            ok = matched * 10 >= total ? kPass : kFail;
            if (ok == kFail) d = "MOVED: " + d + " (the loc id or tile field in the scenery object moved)";
        }
        run.Add(G, "scene.locs", "Live scenery", ok, d, "Loc labels|Obelisk tracker|Tree timers", "10 % on a placement", ok == kUnchecked ? std::string() : d.substr(d.find_first_of("0123456789")), "scene.offsets").need = ok == kUnchecked ? "scenery in view" : "";
    }
}

// ---- Interfaces ----
void health_interfaces(HCtx& c, rtx::health::Run& run) {
    const char* G = "Interfaces";
    HANDLE h = c.h;
    std::uint64_t gs = 0, ge = 0; iface_groups_range(h, c.root, gs, ge);
    std::vector<int> open;
    if (gs && ge > gs)
        for (std::uint64_t g = gs; g + 0x10 <= ge; g += 0x10) {
            const std::uint64_t ap = rpm<std::uint64_t>(h, g + 8).value_or(0);
            if (ap > 0x10000) { const int gid = rpm<std::int32_t>(h, ap).value_or(-1); if (gid > 0 && gid < 70000) open.push_back(gid); }
        }
    const bool frame = std::find(open.begin(), open.end(), 1477) != open.end();
    int maxGroup = 0; for (int g : open) maxGroup = std::max(maxGroup, g);
    {
        const int ok = open.empty() ? (c.inWorld ? kFail : kUnchecked) : (frame ? (maxGroup > 3500 ? kWarn : kPass) : kFail);
        run.Add(G, "iface.groups", "Open interfaces", ok,
                open.empty() ? (c.inWorld ? "GONE: interface list not readable at [MainData+" + hx(kOffIfaceOwner) + "]" : std::string("no interfaces before login")) :
                std::string(!frame ? "GONE: " : maxGroup > 3500 ? "NEW: " : "") + std::to_string(open.size()) + " open" + (frame ? "" : ", game frame 1477 missing") +
                ", largest group " + std::to_string(maxGroup) + (maxGroup > 3500 ? " (near the 4096 hover cap)" : ""),
                "Every interface read", "game frame 1477 open, groups < 3500", std::to_string(open.size()) + " open, largest " + std::to_string(maxGroup), "data.root").need = ok == kUnchecked ? "log in" : "";
    }
    if (!frame) return;
    // game frame walk against its cache definitions
    {
        rtx::health::JVal defs;
        rtx::health::ParseJson(rtx::cache::IfaceGroupDefsJson(1477), defs);
        struct Def { int t = -1, par = -1, w = 0, h = 0, sprite = -1; std::string text; };
        std::unordered_map<int, Def> dm;
        if (const rtx::health::JVal* cs = defs.get("comps"))
            for (const auto& cv : cs->a) { Def d; d.t = (int)cv.num("t", -1); d.par = (int)cv.num("par", -1); d.w = (int)cv.num("w"); d.h = (int)cv.num("h"); d.sprite = (int)cv.num("sprite", -1); d.text = cv.str("text"); dm[(int)cv.num("id", -1)] = d; }
        const std::uint64_t go = iface_group_obj(h, c.root, 1477);
        std::vector<IfaceChildRef> stack; iface_root_refs(h, go, stack);
        int visited = 0, statics = 0, known = 0, parentOk = 0, parentN = 0, spriteOk = 0, spriteN = 0, sizeOk = 0, sizeN = 0, textOk = 0, textN = 0;
        std::unordered_map<std::uint64_t, std::set<int>> classTypes;
        std::set<int> seen;
        while (!stack.empty() && visited < 8000) {
            const IfaceChildRef r = stack.back(); stack.pop_back();
            IfaceNode n;
            if (!iface_read_node(h, r.addr, n)) continue;
            ++visited;
            if (n.group == 1477 && n.sub == -1 && n.comp >= 0 && seen.insert(n.comp).second) {
                ++statics;
                auto it = dm.find(n.comp);
                if (it != dm.end()) {
                    ++known;
                    const Def& d = it->second;
                    classTypes[n.kind].insert(d.t);
                    ++parentN; if (n.parent == d.par) ++parentOk;
                    if (d.t == 5 && d.sprite > 0) { ++spriteN; if (n.sprite == d.sprite) ++spriteOk; }
                    if (d.w > 0 && d.h > 0) { ++sizeN; if (n.w > 0 && n.h > 0) ++sizeOk; }
                    if (d.t == 4 && !d.text.empty()) { ++textN; if (iface_sso_text(h, n.addr) == d.text) ++textOk; }
                }
            }
            std::vector<IfaceChildRef> kids; iface_child_refs(h, n, kids);
            for (const auto& k : kids) stack.push_back(k);
        }
        int mixed = 0; for (const auto& kv : classTypes) if (kv.second.size() > 1) ++mixed;
        int ok = kPass;
        std::string d = std::to_string(statics) + " static comps (" + std::to_string(known) + " in the cache), parents " + std::to_string(parentOk) + "/" + std::to_string(parentN) +
                        ", sprites " + std::to_string(spriteOk) + "/" + std::to_string(spriteN) + ", sized " + std::to_string(sizeOk) + "/" + std::to_string(sizeN) +
                        ", texts " + std::to_string(textOk) + "/" + std::to_string(textN);
        const std::string got = d;
        if (statics == 0 || known * 10 < statics * 9) { ok = kFail; d = "MOVED: " + d + "; comp ids do not match the definitions (node id field moved?)"; }
        else if (parentN && parentOk * 10 < parentN * 9) { ok = kFail; d = "MOVED: " + d + "; parent field disagrees"; }
        else if (mixed) { ok = kFail; d = "FORMAT: " + d + "; " + std::to_string(mixed) + " component classes map to several types"; }
        else if (!c.inWorld && sizeN && sizeOk == 0 && !(spriteN && spriteOk * 2 < spriteN)) { ok = kUnchecked; d += "; the frame is laid out at login"; }   // nothing is sized in the lobby
        else if ((spriteN && spriteOk * 2 < spriteN) || (sizeN && sizeOk * 10 < sizeN * 8)) { ok = kWarn; d = "FORMAT: " + d + "; sprite or size fields disagree with the definitions"; }
        run.Add(G, "iface.frame", "Game frame layout", ok, d, "Panel positions|Gameview rect|Overlays|Hover", "90 % of static comps in the cache with their parent, one type per class, sprites and sizes as defined", got, "iface.groups").need = ok == kUnchecked ? "log in" : "";
    }
    // component slots: the walkers read every component field at block + 0x20, which holds only
    // while the block's object pointer (+0x18) says so; the open-group vector is indexed by id
    {
        std::uint64_t gs = 0, ge = 0; iface_groups_range(h, c.root, gs, ge);
        int idxN = 0, idxOk = 0;
        for (int g : open) {
            if (idxN >= 40) break;
            ++idxN;
            if (gs + (std::uint64_t)g * 0x10 + 0x10 <= ge) {
                const std::uint64_t ap = rpm<std::uint64_t>(h, gs + (std::uint64_t)g * 0x10 + 8).value_or(0);
                if (ap > 0x10000 && rpm<std::int32_t>(h, ap).value_or(-1) == g) ++idxOk;
            }
        }
        const std::uint64_t go = iface_group_obj(h, c.root, 1477);
        std::vector<IfaceChildRef> roots; iface_root_refs(h, go, roots);
        int n = 0, okN = 0;
        for (const auto& r : roots) {
            if (n >= 64) break;
            ++n;
            if (rpm<std::uint64_t>(h, r.addr + 0x18).value_or(0) == r.addr + 0x20 && rpm<std::uint64_t>(h, r.addr + 0x10).value_or(0) == r.addr) ++okN;
        }
        int ok = kPass; std::string d = std::to_string(idxOk) + "/" + std::to_string(idxN) + " open groups at the entry of their id, " + std::to_string(okN) + "/" + std::to_string(n) + " frame slots with the object at block + 0x20";
        if (n == 0) { ok = kFail; d = "GONE: no root slots under the game frame"; }
        else if (okN != n) { ok = kFail; d = "FORMAT: " + d + " (the component object no longer sits at block + 0x20: every node field reads off)"; }
        else if (idxOk != idxN) { ok = kWarn; d = "FORMAT: " + d + " (the group vector is no longer indexed by interface id; the linear scan is in use)"; }
        run.Add(G, "iface.slots", "Component slots", ok, d, "Every interface read", "open groups at entry id * 16, every slot block with its object at +0x20", std::to_string(idxOk) + "/" + std::to_string(idxN) + ", " + std::to_string(okN) + "/" + std::to_string(n), "iface.groups");
    }
    // sub-interface table: every open group mounted on a live parent component
    {
        int mounted = 0, parentOk = 0, n = 0; std::string bad;
        for (int g : open) {
            if (g == 1477) continue;
            ++n;
            IfaceSubParent p;
            if (!iface_sub_parent(h, c.root, g, p)) continue;
            ++mounted;
            rtx::cache::IfaceCompDefLite d;
            const bool parentOpen = std::find(open.begin(), open.end(), p.group) != open.end();
            if (parentOpen && rtx::cache::IfaceCompDefLookup(p.group, p.comp, d)) ++parentOk;
            else if (bad.size() < 60) bad += " " + std::to_string(g) + "@" + std::to_string(p.group) + ":" + std::to_string(p.comp);
        }
        int ok = mounted == 0 ? kFail : (parentOk * 10 >= mounted * 9 ? kPass : kWarn);
        run.Add(G, "iface.mounts", "Sub-interface table", ok,
                std::string(ok == kFail ? "GONE: " : ok == kWarn ? "MOVED: " : "") + std::to_string(mounted) + "/" + std::to_string(n) + " open groups mounted, " + std::to_string(parentOk) + " on a live parent comp" + (bad.empty() ? "" : "; odd:" + bad) +
                (ok == kFail ? " (the owner's sub table at +0xE0 reads empty)" : ""),
                "Panel positions (automatic origins)", "open groups mounted on an open parent comp (90 %)", std::to_string(mounted) + "/" + std::to_string(n) + " mounted, " + std::to_string(parentOk) + " on a parent", "iface.groups");
    }
    // the fallback origins against the engine's own answer: every open, attached group of the manual table,
    // and the window rule for every window whose content comp holds an attached group. The fallback is meant
    // to equal the engine, nudges included, so the tolerance only covers read skew.
    {
        constexpr int kTol = 2;
        int compared = 0, agree = 0, wins = 0, winsOk = 0; std::string stale;
        auto note = [&](const std::string& tag, bool ok, int dx, int dy) {
            if (stale.size() >= 100) return;
            stale += " " + tag + (ok ? "(" + std::to_string(dx) + "," + std::to_string(dy) + ")" : std::string("(none)"));
        };
        std::set<int> seen;
        for (const auto& s : kPanelOrigins) {
            if (s.group == 1477 || !seen.insert(s.group).second || std::find(open.begin(), open.end(), s.group) == open.end()) continue;
            int ax = 0, ay = 0, fx = 0, fy = 0;
            if (!iface_auto_origin(h, c.root, s.group, ax, ay)) continue;
            ++compared;
            const bool ok = panel_fallback_origin(h, c.root, c.pid, s.group, fx, fy);
            if (ok && std::abs(fx - ax) <= kTol && std::abs(fy - ay) <= kTol) ++agree;
            else note(std::to_string(s.group), ok, fx - ax, fy - ay);
        }
        std::map<int, int> onComp;   // game frame comp -> an attached group mounted on it
        for (int g : open) { IfaceSubParent p; if (g != 1477 && iface_sub_parent(h, c.root, g, p) && p.group == 1477) onComp.emplace(p.comp, g); }
        for (const auto& w : kHudWindowVarcs) {
            int fx = 0, fy = 0, content = -1;
            const bool ok = hud_window_content_origin(h, c.root, c.pid, w, fx, fy, &content);
            if ((content >> 16) != 1477) continue;
            auto it = onComp.find(content & 0xFFFF);
            int ax = 0, ay = 0;
            if (it == onComp.end() || !iface_auto_origin(h, c.root, it->second, ax, ay)) continue;
            ++wins;
            if (ok && std::abs(fx - ax) <= kTol && std::abs(fy - ay) <= kTol) ++winsOk;
            else note("w" + std::to_string(w.slot), ok, fx - ax, fy - ay);
        }
        const int n = compared + wins;
        const bool agreeAll = agree == compared && winsOk == wins;
        run.Add(G, "iface.origins", "Manual panel origins", n == 0 ? kUnchecked : (agreeAll ? kPass : kWarn),
                n == 0 ? "no listed panel or window open and attached"
                       : std::string(agreeAll ? "" : "MOVED: ") + std::to_string(agree) + "/" + std::to_string(compared) + " fallback origins and " + std::to_string(winsOk) + "/" +
                         std::to_string(wins) + " window content origins match the engine" + (stale.empty() ? "" : "; stale:" + stale),
                "Panel positions (fallback)", "every fallback origin within 2 px of the engine's", std::to_string(agree + winsOk) + "/" + std::to_string(n), "iface.mounts").need = n == 0 ? "a listed panel or window open" : "";
    }
    // backpack drawn against container 93
    {
        int ok = kUnchecked; std::string d = "backpack not open";
        if (std::find(open.begin(), open.end(), 1473) != open.end()) {
            // the backpack grid: comp 5, one dynamic child per slot carrying the slot's item
            std::map<int, int> drawn;
            const std::uint64_t go = iface_group_obj(h, c.root, 1473);
            std::vector<IfaceChildRef> stack; iface_root_refs(h, go, stack);
            int visited = 0;
            while (!stack.empty() && visited < 4000) {
                const IfaceChildRef r = stack.back(); stack.pop_back();
                IfaceNode n; if (!iface_read_node(h, r.addr, n)) continue;
                ++visited;
                if (n.group == 1473 && n.comp == 5 && n.sub >= 0 && n.sub < 64 && n.item > 0 && n.item < 200000) drawn[n.sub] = n.item;
                std::vector<IfaceChildRef> kids; iface_child_refs(h, n, kids);
                for (const auto& k : kids) stack.push_back(k);
            }
            std::map<int, int> held;
            rtx::health::JVal inv; rtx::health::ParseJson(InventoryJson(c.pid), inv);
            if (const rtx::health::JVal* items = inv.get("items"))
                for (const auto& it : items->a) if (it.a.size() >= 2) held[std::atoi(it.a[0].s.c_str())] = std::atoi(it.a[1].s.c_str());
            int match = 0;
            for (const auto& kv : held) { auto f = drawn.find(kv.first); if (f != drawn.end() && f->second == kv.second) ++match; }
            d = std::to_string(match) + "/" + std::to_string(held.size()) + " held slots drawn with their item (" + std::to_string(drawn.size()) + " cells with an item)";
            ok = held.empty() ? (drawn.empty() ? kPass : kFail) : (match * 10 >= (int)held.size() * 9 ? kPass : kFail);
            if (ok == kFail) d = "MOVED: " + d + "; the grid at 1473:5 and container 93 disagree (cell or item field moved)";
        }
        run.Add(G, "iface.backpack", "Backpack cells", ok, d, "Inventory highlights|Item overlays", "90 % of held slots drawn with their item", ok == kUnchecked ? std::string() : d.substr(d.find_first_of("0123456789")), "data.containers").need = ok == kUnchecked ? "open the backpack" : "";
    }
    // buff bar: icons the bar draws against the effects the reader emits
    {
        const bool b284 = std::find(open.begin(), open.end(), 284) != open.end();
        int ok = kUnchecked; std::string d = "buff bar not open";
        if (b284) {
            rtx::health::JVal bj; rtx::health::ParseJson(BuffsJson(c.pid), bj);
            const rtx::health::JVal* bl = bj.get("buffs");
            const int emitted = bl ? (int)bl->a.size() : 0;
            int named = 0; if (bl) for (const auto& b : bl->a) if (!b.str("name").empty()) ++named;
            // drawn: the slots of 284:18 whose icon is live (the walk the buff reader does, before it
            // asks each slot for its buff struct)
            int drawn = 0;
            const std::uint64_t go = iface_group_obj(h, c.root, 284);
            std::vector<IfaceChildRef> stack; iface_root_refs(h, go, stack);
            int visited = 0;
            while (!stack.empty() && visited < 3000) {
                const IfaceChildRef r = stack.back(); stack.pop_back();
                IfaceNode n; if (!iface_read_node(h, r.addr, n)) continue;
                ++visited;
                if (n.group == 284 && n.comp == 18 && n.sub >= 0 && !r.hidden) {
                    const std::uint64_t slotArr = rpm<std::uint64_t>(h, n.addr + 0x200).value_or(0);
                    const std::uint64_t icon = slotArr > 0x10000 ? rpm<std::uint64_t>(h, slotArr + 0x8).value_or(0) : 0;
                    if (icon > 0x10000 && rpm<std::uint64_t>(h, icon + 0x40).value_or(0) == n.addr && rpm<std::int32_t>(h, icon + 0x8).value_or(0) != 0) {
                        const int spr = rpm<std::uint16_t>(h, icon + 0x1b0).value_or(0), item = rpm<std::int32_t>(h, icon + 0x1d8).value_or(0);
                        if ((item > 0 && item < 200000) || (spr > 0 && spr < 0xFFFF)) ++drawn;
                    }
                }
                std::vector<IfaceChildRef> kids; iface_child_refs(h, n, kids);
                for (const auto& k : kids) stack.push_back(k);
            }
            d = std::to_string(drawn) + " icons drawn, " + std::to_string(emitted) + " effects read (" + std::to_string(named) + " named)";
            if (drawn > 0 && emitted == 0) { ok = kFail; d = "MOVED: " + d + ": the bar draws buffs the reader does not see (slot struct param 8106 or icon fields moved)"; }
            else if (emitted > 0) { ok = named == emitted ? kPass : kWarn; if (ok == kWarn) d = "GONE: " + d + ": " + std::to_string(emitted - named) + " buffs without a name (struct param or game text moved)"; }
            else { ok = kUnchecked; d += ": no buffs active to check against"; }
        }
        run.Add(G, "iface.buffs", "Buff bar", ok, d, "Buffs panel|Buff alerts|Buff timers", "icons drawn == effects read, every effect named", ok == kUnchecked && !b284 ? std::string() : d.substr(d.find_first_of("0123456789")), "iface.groups").need = ok == kUnchecked ? (b284 ? "a buff active" : "open the buff bar") : "";
    }
    // chat: interface lines, store records, packet lines
    {
        rtx::health::JVal cj; rtx::health::ParseJson(ChatJson(c.pid), cj);
        const int lines = cj.get("lines") ? (int)cj.get("lines")->a.size() : 0;
        const int store = cj.get("store") ? (int)cj.get("store")->a.size() : 0;
        const rtx::health::JVal* ph = cj.get("phook");
        const bool open137 = std::find(open.begin(), open.end(), 137) != open.end();
        run.Add(G, "iface.chatlines", "Chat lines (interface)", !open137 ? kUnchecked : (lines > 0 ? kPass : kFail),
                !open137 ? "chatbox not open" : std::string(lines > 0 ? "" : "GONE: ") + std::to_string(lines) + " lines on the chatbox" + (lines > 0 ? "" : " (the line comps of group 137 read empty)"),
                "Chat log", "> 0 lines", std::to_string(lines), "iface.groups").need = !open137 ? "open the chatbox" : "";
        run.Add(G, "iface.chatstore", "Chat records (store)", store > 0 ? kPass : (c.inWorld ? kWarn : kUnchecked),
                std::string(store > 0 || !c.inWorld ? "" : "GONE: ") + std::to_string(store) + " recent records", "Chat log|Chat alerts", "> 0 records", std::to_string(store), "data.chat").need = store > 0 || c.inWorld ? "" : "log in";
        if (c.cli) run.Add(G, "iface.chatpackets", "Chat lines (packets)", kUnchecked, "launcher only", "Chat log (packet lines)", "hook feeding, packets seen", "not read from the command line", "comp.boot").need = "a launcher run";
        else run.Add(G, "iface.chatpackets", "Chat lines (packets)", ph && ph->s == "true" ? (cj.num("pseen") > 0 ? kPass : kUnchecked) : kFail,   // no chat message yet is nothing to warn about
                     ph && ph->s == "true" ? std::string(cj.num("pseen") > 0 ? "" : "UNVERIFIED: ") + std::to_string(cj.num("pseen")) + " message_game packets seen" : "GONE: framer not feeding chat (hook or opcode moved)",
                     "Chat log (packet lines)", "hook feeding, packets seen", ph ? ph->s + ", " + std::to_string(cj.num("pseen")) : std::string("no hook state"), "comp.boot").need = ph && ph->s == "true" && cj.num("pseen") == 0 ? "a chat message" : "";
    }
    // action bar: at least one slot with a cache-known ability and a name
    {
        int ok = kUnchecked; std::string d = "action bar not open";
        if (std::find(open.begin(), open.end(), 1430) != open.end()) {
            rtx::health::JVal ab; rtx::health::ParseJson(ActionBarJson(c.pid), ab);
            int slots = 0, named = 0;
            if (const rtx::health::JVal* bars = ab.get("bars"))
                for (const auto& b : bars->a) if (const rtx::health::JVal* s = b.get("slots")) for (const auto& x : s->a) { ++slots; if (x.num("id", -1) > 0 && !x.str("name").empty()) ++named; }
            // what the bar itself holds: item icons in its slots, drawn or not
            int filled = 0;
            {
                const std::uint64_t go = iface_group_obj(h, c.root, 1430);
                std::vector<IfaceChildRef> stack; iface_root_refs(h, go, stack);
                int visited = 0;
                while (!stack.empty() && visited < 3000) {
                    const IfaceChildRef r = stack.back(); stack.pop_back();
                    IfaceNode n; if (!iface_read_node(h, r.addr, n)) continue;
                    ++visited;
                    if (n.group == 1430 && n.item > 0 && n.item < 200000) ++filled;
                    std::vector<IfaceChildRef> kids; iface_child_refs(h, n, kids);
                    for (const auto& k : kids) stack.push_back(k);
                }
            }
            d = std::to_string(named) + "/" + std::to_string(slots) + " slots read with an id and name; the bar holds " + std::to_string(filled) + " item slots";
            if (slots == 0 && filled > 0) { ok = kFail; d = "MOVED: " + d + " (the slot reader finds none: label or id field moved)"; }
            else if (slots == 0) { ok = kUnchecked; d = "no filled slots to check"; }
            else { ok = named > 0 ? kPass : kFail; if (ok == kFail) d = "GONE: " + d + " (no slot resolves an ability name)"; }
        }
        run.Add(G, "iface.actionbar", "Action bar", ok, d, "Ability Bar panel|Cooldown overlays", "filled slots read with an id and a name", ok == kUnchecked ? std::string() : d.substr(d.find_first_of("0123456789")), "iface.groups").need = ok == kUnchecked ? "open the action bar with abilities on it" : "";
    }
    {
        const bool dlg = std::find(open.begin(), open.end(), 1188) != open.end() || std::find(open.begin(), open.end(), 1184) != open.end();
        const bool read = dlg && DialogJson(c.pid).size() > 4;
        run.Add(G, "iface.dialog", "Dialogs", dlg ? (read ? kPass : kFail) : kUnchecked,
                dlg ? (read ? "dialog read" : "FORMAT: dialog open (1188/1184) but its text comps do not read") : "no dialog open", "Dialog detection|Quest helper", "dialog text read while open", dlg ? (read ? "read" : "empty") : std::string(), "iface.groups").need = dlg ? "" : "open a dialog";
    }
}

// ---- Packets ----
// The first 48 bytes of a packet handler with its relative operands masked, so the same code at
// another address hashes the same. Most handlers are a thunk (add rcx,8 ; jmp body): the body is hashed.
std::uint32_t handler_fingerprint(HANDLE h, std::uint64_t fn) {
    constexpr int kN = 48;
    std::uint8_t b[kN];
    for (int hop = 0; hop < 2; ++hop) {
        if (!rpm_bytes(h, fn, b, 16)) return 0;
        int j = -1;
        if (b[0] == 0xE9) j = 0;
        else if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xC1 && b[4] == 0xE9) j = 4;
        if (j < 0) break;
        std::int32_t rel; std::memcpy(&rel, b + j + 1, 4);
        fn = fn + (std::uint64_t)(j + 5) + (std::int64_t)rel;
    }
    if (!rpm_bytes(h, fn, b, sizeof(b))) return 0;
    for (int i = 0; i < kN; ++i) {   // relative operands masked: calls, jumps and rip-relative operands
        if ((b[i] == 0xE8 || b[i] == 0xE9) && i + 5 <= kN) { std::memset(b + i + 1, 0, 4); i += 4; continue; }
        if (b[i] == 0x0F && i + 6 <= kN && b[i + 1] >= 0x80 && b[i + 1] <= 0x8F) { std::memset(b + i + 2, 0, 4); i += 5; continue; }
        if ((b[i] == 0x48 || b[i] == 0x4C) && i + 7 <= kN && (b[i + 1] == 0x8B || b[i + 1] == 0x8D || b[i + 1] == 0x89) && (b[i + 2] & 0xC7) == 0x05) { std::memset(b + i + 3, 0, 4); i += 6; continue; }
    }
    std::uint32_t hsh = 2166136261u;
    for (std::uint8_t x : b) { hsh ^= x; hsh *= 16777619u; }
    return hsh;
}

// The zone sub-packet table, found in the running image so no exe needs it recorded. The client
// keeps several tables of pointers to descriptors {id, wire length} whose ids count up from 0 (the
// server packets and three more); the zone one is the table whose lengths agree with the most
// sub-packets this reader knows, and it has to stand alone at that. 0 when none does. `runOut`
// takes how many descriptors the table holds.
std::uint32_t find_zone_sub_table(HCtx& c, int* runOut) {
    if (runOut) *runOut = 0;
    const std::size_t size = c.imageSize;
    if (!c.base || size < 0x10000 || size > (256u << 20)) return 0;
    std::vector<std::uint8_t> img(size, 0);
    for (std::size_t off = 0; off < size; off += 0x10000) {
        const std::size_t n = size - off < 0x10000 ? size - off : 0x10000;
        if (rpm_bytes(c.h, c.base + off, img.data() + off, n)) continue;
        for (std::size_t pg = 0; pg < n; pg += 0x1000) rpm_bytes(c.h, c.base + off + pg, img.data() + off + pg, n - pg < 0x1000 ? n - pg : 0x1000);
    }
    auto desc = [&](std::size_t slot, std::int32_t& id, std::int32_t& len) {
        std::uint64_t ptr; std::memcpy(&ptr, img.data() + slot, 8);
        if (ptr < c.base || ptr - c.base + 8 > size) return false;
        std::memcpy(&id, img.data() + (ptr - c.base), 4); std::memcpy(&len, img.data() + (ptr - c.base) + 4, 4);
        return true;
    };
    std::uint32_t best = 0; int bestAgree = 0, bestRun = 0, ties = 0;
    for (std::size_t i = 0; i + 8 <= size; ) {
        int run = 0, agree = 0;
        for (std::size_t j = i; j + 8 <= size; j += 8, ++run) {
            std::int32_t id, len;
            if (!desc(j, id, len) || id != run) break;
            if (run < rtx::evzone::kSubCount && len == rtx::evzone::subLen(run)) ++agree;
        }
        if (run >= 8) {
            if (agree > bestAgree) { best = (std::uint32_t)i; bestAgree = agree; bestRun = run; ties = 0; }
            else if (agree == bestAgree) ++ties;
        }
        i += run >= 8 ? (std::size_t)run * 8 : 8;
    }
    if (!best || ties || bestAgree * 3 < rtx::evzone::kSubCount * 2) return 0;
    if (runOut) *runOut = bestRun;
    return best;
}

// ---- Packets: live layouts ----
// One row per expected opcode seen at least 20 times in the event ring: records the decoder read,
// and records whose decoded values the game state and the cache do not allow. A layout that kept
// its length but moved its fields shows up here, not in the length rows. Var sets are judged on the
// last packet per id against the store, read after the ring (the store holds the latest value).
void health_packets_live(HCtx& c, rtx::health::Run& run) {
    const char* G = "Packets: live layouts";
    const char* kFeat = "Packet decoders|Events channel|Zone events|Var updates|GE offers";
    if (c.cli) { run.Add(G, "pkt.live", "Live layouts", kUnchecked, "launcher only (the event ring is read in the launcher)", kFeat, "", "", "pkt.events").need = "a launcher run"; return; }
    wchar_t name[rtx::ipc::kNameChars]; rtx::events::MakeSectionName(c.pid, name);
    ShareMap<rtx::events::Share> ev;
    if (!ev.open(name) || ev.sh->magic != rtx::events::kMagic || ev.sh->version != rtx::events::kVersion) {
        run.Add(G, "pkt.live", "Live layouts", kUnchecked, "event ring not mapped", kFeat, "", "", "pkt.events").need = "the companion's event ring";
        return;
    }
    HANDLE h = c.h;
    {   // map origin for the zone-relative packets
        const std::uint64_t mm = rpm<std::uint64_t>(h, c.root + kOffMapMgr).value_or(0);
        if (mm > 0x10000) { rtx::evzone::g_mapBaseX = rpm<std::int32_t>(h, mm + 0x698).value_or(0); rtx::evzone::g_mapBaseY = rpm<std::int32_t>(h, mm + 0x69C).value_or(0); }
    }
    const int mapX = rtx::evzone::g_mapBaseX, mapY = rtx::evzone::g_mapBaseY;
    TrackerDefs trk; const bool trkOk = read_tracker_defs(h, c.root, trk);
    const int itemCap = rtx::cache::MaxId("item") > 0 ? rtx::cache::MaxId("item") + 1 : 0;
    const int locCap = rtx::cache::MaxId("loc") > 0 ? rtx::cache::MaxId("loc") + 1 : 0;
    const int gfxCap = rtx::cache::IndexInfo(21).maxArchive >= 0 ? (rtx::cache::IndexInfo(21).maxArchive + 1) * 256 : 0;
    const int scriptCap = rtx::cache::IndexInfo(12).open ? rtx::cache::IndexInfo(12).archives : 0;
    struct Stat { int seen = 0, decoded = 0, bad = 0; std::string first, features; bool rawOk = false; std::uint32_t lastWall = 0; std::vector<int> gaps; };
    std::map<std::string, Stat> st;
    auto nameOf = [](int op) -> const char* { for (const auto& e : rtx::sops::kExpected) if (e.op == op) return e.name; return nullptr; };
    auto featOf = [](int op) -> const char* {
        switch (op) {
            case rtx::sops::kSkillUpdate: return "Skills panel|XP tracker";
            case rtx::sops::kContainerUpdate: return "Backpack|Bank|Storages|Events channel";
            case rtx::sops::kRunClientScript: return "Events channel|Buff updates";
            case rtx::sops::kRunEnergy: case rtx::sops::kRunWeight: return "Run energy|Weight";
            case rtx::sops::kPingEcho: case rtx::sops::kServerTick: return "Ticks|Tick timers";
            case rtx::sops::kVarpInt: case rtx::sops::kVarpByte: case rtx::sops::kVarpLong: case rtx::sops::kVarcInt: case rtx::sops::kVarcByte: case rtx::sops::kVarbitVarint: return "Var updates|Live variable changes";
            case rtx::sops::kGeOffer: return "GE offers|GE alerts";
            case rtx::sops::kVarbitByte: case rtx::sops::kVarbitInt: case rtx::sops::kVarcLong: return "Var updates|Live variable changes";
            case rtx::sops::kContainerFull: case rtx::sops::kContainerReset: return "Backpack|Bank|Storages|Events channel";
            case rtx::sops::kTrackerGroup: case rtx::sops::kTrackerValues: case rtx::sops::kTrackerRemove: case rtx::sops::kTrackerClear: case rtx::sops::kTrackerColumn: return "Trackers|Events channel";
            case rtx::sops::kSystemUpdate: case rtx::sops::kCameraTarget: case rtx::sops::kCutscene: case rtx::sops::kFriendsLoaded: case rtx::sops::kPrivateFilter: case rtx::sops::kMinimapState: return "Events channel|State events";
            case rtx::sops::kZoneSub3: case rtx::sops::kZoneSub14: return "Zone events";
            case rtx::sops::kSound: case rtx::sops::kAreaSound: case rtx::sops::kAreaSoundAbs: return "Sounds";
            case rtx::sops::kHintArrow: return "Engine markers: hint arrows";
            case rtx::sops::kSpotAnim: case rtx::sops::kSpotAnim2: case rtx::sops::kSpotAnimActor: case rtx::sops::kSpotAnimActor2:
            case rtx::sops::kProjectile: case rtx::sops::kProjectile20: case rtx::sops::kProjectile28: case rtx::sops::kProjectile29: return "Spot animations|Projectiles|Specials: graphic highlights";
            default: return "Zone events|Ground items|Loc changes";
        }
    };
    struct Last { long long value = 0; std::string name; };
    std::map<int, Last> lastVarp, lastVarc;
    std::map<int, int> lastGe;   // slot -> item
    const std::uint64_t written = ev.sh->written;
    const std::uint64_t from = written > (std::uint64_t)rtx::events::kMaxRecords ? written - rtx::events::kMaxRecords : 0;
    rtx::events::Record r;
    auto bad = [&](Stat& s, const std::string& why) { ++s.bad; if (s.first.empty()) s.first = why; };
    for (std::uint64_t i = from; i < written; ++i) {
        const rtx::events::Record& slot = ev.sh->recs[i % rtx::events::kMaxRecords];
        const std::uint32_t want = (std::uint32_t)((i + 1) * 2);
        if (slot.seq != want) continue;
        MemoryBarrier();
        std::memcpy(&r, &slot, sizeof(r));
        MemoryBarrier();
        if (slot.seq != want) continue;
        const char* nm = nameOf(r.opcode);
        if (!nm) continue;
        std::uint32_t n = r.length < 0 ? 0 : (std::uint32_t)r.length;
        if (n > (std::uint32_t)rtx::events::kPayload) n = rtx::events::kPayload;
        Stat& s = st[nm];
        if (s.features.empty()) s.features = featOf(r.opcode);
        ++s.seen;
        const int op = r.opcode;
        if (op == rtx::sops::kServerTick) { if (s.lastWall) s.gaps.push_back((int)(r.wallMs - s.lastWall)); s.lastWall = r.wallMs; ++s.decoded; continue; }
        if (op == rtx::sops::kGeOffer) {
            s.rawOk = true;
            if (n >= 7 && r.payload[4] < kGESlotCount) { ++s.decoded; lastGe[r.payload[4]] = ((int)r.payload[5] << 8) | r.payload[6]; if (itemCap && lastGe[r.payload[4]] >= itemCap) bad(s, "item " + std::to_string(lastGe[r.payload[4]]) + " past the item ceiling"); }
            else bad(s, "slot byte " + std::to_string(n >= 5 ? r.payload[4] : 255) + " not a slot");
            continue;
        }
        if (op == rtx::sops::kHintArrow) { s.rawOk = true; ++s.decoded; continue; }
        std::string body;
        if (!ev_decode(body, op, r.payload, n, r.length, trkOk ? &trk : nullptr)) { if (op == rtx::sops::kRunClientScript) bad(s, "signature or terminator not readable"); continue; }
        ++s.decoded;
        rtx::health::JVal j;
        if (!rtx::health::ParseJson("{" + body + "}", j)) continue;
        const long long id = j.num("id", -1), value = j.num("value", 0);
        auto planeOk = [&]() { const rtx::health::JVal* p = j.get("plane"); return !p || p->t == rtx::health::JVal::Null || (p->s != "" && std::atoi(p->s.c_str()) >= 0 && std::atoi(p->s.c_str()) <= 3); };
        auto zoneOk = [&]() { const long long x = j.num("x", -1), y = j.num("y", -1); return x >= mapX - 104 && x < mapX + 256 + 104 && y >= mapY - 104 && y < mapY + 256 + 104; };
        auto gfxOk = [&]() { const long long g = j.num("gfx", -1); return g == -1 || !gfxCap || g < gfxCap; };
        switch (op) {
            case rtx::sops::kSkillUpdate: {
                const long long sk = j.num("skill", -1), lv = j.num("level", -1), xp = j.num("xp", -1);
                if (sk < 0 || sk >= 29 || lv < 1 || lv > 150 || xp < 0 || xp > 200000000) bad(s, "skill " + std::to_string(sk) + " level " + std::to_string(lv) + " xp " + std::to_string(xp));
                break;
            }
            case rtx::sops::kVarpInt: case rtx::sops::kVarpByte: case rtx::sops::kVarpLong:
                if (c.varpMaxId > 0 && id > c.varpMaxId) bad(s, "id " + std::to_string(id) + " past the store's largest " + std::to_string(c.varpMaxId));
                else lastVarp[(int)id] = { value, nm };
                break;
            case rtx::sops::kVarcInt: case rtx::sops::kVarcByte:
                lastVarc[(int)id] = { value, nm };
                break;
            case rtx::sops::kVarbitVarint: case rtx::sops::kVarbitByte: case rtx::sops::kVarbitInt: {
                int vp = -1, lo = 0, hi = 0;
                if (!rtx::cache::GetVarbit((int)id, vp, lo, hi)) bad(s, "varbit " + std::to_string(id) + " has no definition");
                else if (hi >= lo && hi - lo < 31 && (unsigned long long)(std::uint32_t)value >= (1ull << (hi - lo + 1))) bad(s, "value " + std::to_string(value) + " wider than varbit " + std::to_string(id));
                break;
            }
            case rtx::sops::kContainerUpdate: {
                const long long cont = j.num("container", -1);
                const int size = std::atoi(rtx::pins::Field(rtx::cache::PinFingerprint("inv", std::to_string(cont), ""), "size").c_str());
                if (size <= 0) { bad(s, "container " + std::to_string(cont) + " has no inv definition"); break; }
                if (j.str("partial") == "true") { bad(s, "container " + std::to_string(cont) + " not consumed to its length"); break; }
                if (const rtx::health::JVal* slots = j.get("slots"))
                    for (const auto& sl : slots->a) {
                        const long long slotNo = sl.num("slot", -1), item = sl.num("item", -1), qty = sl.num("qty", 0);
                        if (slotNo < 0 || slotNo >= size || (itemCap && item >= itemCap) || (item >= 0 && qty <= 0)) { bad(s, "container " + std::to_string(cont) + " slot " + std::to_string(slotNo) + " item " + std::to_string(item) + " x" + std::to_string(qty)); break; }
                    }
                break;
            }
            case rtx::sops::kContainerFull: {   // partial only counts when the whole payload was kept (a bank exceeds the window)
                const long long cont = j.num("container", -1), count = j.num("count", -1);
                const int size = std::atoi(rtx::pins::Field(rtx::cache::PinFingerprint("inv", std::to_string(cont), ""), "size").c_str());
                if (size <= 0) { bad(s, "container " + std::to_string(cont) + " has no inv definition"); break; }
                if (count < 0 || count > size) { bad(s, "container " + std::to_string(cont) + " count " + std::to_string(count) + " past its " + std::to_string(size) + " slots"); break; }
                if (j.str("partial") == "true" && r.length <= rtx::events::kPayload) { bad(s, "container " + std::to_string(cont) + " not consumed to its length"); break; }
                if (const rtx::health::JVal* slots = j.get("slots"))
                    for (const auto& sl : slots->a) {
                        const long long slotNo = sl.num("slot", -1), item = sl.num("item", -1), qty = sl.num("qty", 0);
                        if (slotNo < 0 || slotNo >= size || (itemCap && item >= itemCap) || qty <= 0) { bad(s, "container " + std::to_string(cont) + " slot " + std::to_string(slotNo) + " item " + std::to_string(item) + " x" + std::to_string(qty)); break; }
                    }
                break;
            }
            case rtx::sops::kContainerReset: {
                const long long cont = j.num("container", -1);
                if (std::atoi(rtx::pins::Field(rtx::cache::PinFingerprint("inv", std::to_string(cont), ""), "size").c_str()) <= 0) bad(s, "container " + std::to_string(cont) + " has no inv definition");
                break;
            }
            case rtx::sops::kTrackerGroup:
                if (j.num("slot", -1) < 0 || j.num("slot", -1) > 32 || j.num("groupId", -1) < 0) bad(s, "slot " + j.str("slot") + " group " + j.str("groupId"));
                break;
            case rtx::sops::kTrackerValues: {   // every cell indexes the live group definitions
                if (!trkOk) break;
                if (const rtx::health::JVal* cells = j.get("cells"))
                    for (const auto& cl : cells->a) {
                        const long long g = cl.num("group", -1), rw = cl.num("row", -1), col = cl.num("column", -1);
                        const bool okg = g >= 0 && g < (long long)trk.groups.size();
                        if (!okg || rw < 0 || rw >= (long long)trk.groups[(std::size_t)g].rows.size() || col < 0 || col >= (long long)trk.groups[(std::size_t)g].cols.size()) { bad(s, "cell " + std::to_string(g) + "," + std::to_string(rw) + "," + std::to_string(col) + " outside the group definitions"); break; }
                    }
                break;
            }
            case rtx::sops::kTrackerRemove: case rtx::sops::kTrackerClear: case rtx::sops::kTrackerColumn: {
                const char* key = op == rtx::sops::kTrackerRemove ? "slot" : "group";
                if (trkOk && j.num(key, -1) >= (long long)trk.groups.size()) bad(s, std::string(key) + " " + j.str(key) + " past the " + std::to_string(trk.groups.size()) + " groups");
                break;
            }
            case rtx::sops::kCameraTarget: if (!planeOk()) bad(s, "plane " + j.str("plane")); break;
            case rtx::sops::kMinimapState: if (value > 5) bad(s, "state " + std::to_string(value)); break;
            case rtx::sops::kRunClientScript: {
                if (j.str("kind") == "buff_update") {   // script 10623 reported by its struct: plausible when the struct names a buff
                    if (j.num("struct", -1) < 0 || j.str("name").empty()) bad(s, "buff struct " + j.str("struct") + " without a name");
                    break;
                }
                const long long script = j.num("script", -1);
                if (j.str("partial") != "true" && (script < 0 || (scriptCap && script >= scriptCap))) bad(s, "script " + std::to_string(script) + " outside index 12");
                break;
            }
            case rtx::sops::kRunEnergy: if (value < 0 || value > 100) bad(s, "energy " + std::to_string(value)); break;
            case rtx::sops::kRunWeight: if (value < -200 || value > 500) bad(s, "weight " + std::to_string(value)); break;
            case rtx::sops::kZoneBase: case rtx::sops::kZoneClear:
                if (!planeOk() || !zoneOk()) bad(s, "zone " + j.str("x") + "," + j.str("y") + " plane " + j.str("plane") + " off the loaded map " + std::to_string(mapX) + "," + std::to_string(mapY));
                break;
            case rtx::sops::kZoneUpdate:
                if (!planeOk() || !zoneOk()) bad(s, "zone " + j.str("x") + "," + j.str("y") + " plane " + j.str("plane") + " off the loaded map");
                else if (j.str("partial") == "true") bad(s, "item walk did not end at the length (unknown sub id or short body)");
                break;
            case rtx::sops::kObjAdd: case rtx::sops::kObjDel: case rtx::sops::kObjCount: {
                const long long item = j.num("item", -1);
                if (item < 0 || (itemCap && item >= itemCap)) bad(s, "item " + std::to_string(item) + " past the item ceiling");
                else if (op == rtx::sops::kObjCount && j.num("from", -1) == j.num("qty", -2)) bad(s, "count change with equal quantities");
                break;
            }
            case rtx::sops::kLocAdd: case rtx::sops::kLocDel: {
                const long long loc = j.num("loc", 0), type = j.num("type", -1), rot = j.num("rot", -1);
                if ((locCap && loc >= locCap) || type < 0 || type > 22 || rot < 0 || rot > 3) bad(s, "loc " + std::to_string(loc) + " type " + std::to_string(type) + " rot " + std::to_string(rot));
                break;
            }
            case rtx::sops::kSpotAnim: case rtx::sops::kSpotAnim2: case rtx::sops::kSpotAnimActor: case rtx::sops::kSpotAnimActor2: {
                const std::string target = j.str("target");
                if (!gfxOk()) bad(s, "graphic " + j.str("gfx") + " past the spot animation ceiling");
                else if ((target == "player" || target == "npc") && j.num("index", 0) >= 0x800) bad(s, target + " index " + j.str("index"));
                else if (!planeOk()) bad(s, "plane " + j.str("plane"));
                break;
            }
            case rtx::sops::kProjectile: case rtx::sops::kProjectile29:
                if (!gfxOk()) bad(s, "graphic " + j.str("gfx") + " past the spot animation ceiling");
                else if (j.num("t0", 0) > j.num("t1", 0)) bad(s, "start " + j.str("t0") + " after end " + j.str("t1"));
                break;
            case rtx::sops::kProjectile20: case rtx::sops::kProjectile28:
                if (!gfxOk()) bad(s, "graphic " + j.str("gfx") + " past the spot animation ceiling");
                break;
            case rtx::sops::kAreaSound: case rtx::sops::kAreaSoundAbs:
                if (!planeOk()) bad(s, "plane " + j.str("plane"));
                break;
            default: break;
        }
    }
    // the stores after the ring: the last value set per id is the one they hold (one tick of grace:
    // a value set again after the ring was read disagrees without a moved layout, so a few are allowed)
    for (const auto& kv : lastVarp) {
        Stat& s = st[kv.second.name];
        int now = 0;
        if (!read_varp_found(h, c.root, kv.first, now)) continue;
        if ((long long)now != (long long)(std::int32_t)kv.second.value) bad(s, "varp " + std::to_string(kv.first) + " set to " + std::to_string(kv.second.value) + ", the store holds " + std::to_string(now));
    }
    for (const auto& kv : lastVarc) {
        Stat& s = st[kv.second.name];
        int now = 0;
        if (!read_varc_found(h, c.root, kv.first, now)) { bad(s, "varc " + std::to_string(kv.first) + " not in the store"); continue; }
        if ((long long)now != kv.second.value) bad(s, "varc " + std::to_string(kv.first) + " set to " + std::to_string(kv.second.value) + ", the store holds " + std::to_string(now));
    }
    if (!lastGe.empty()) {
        Stat& s = st["ge_offer"];
        const std::uint64_t box = rpm<std::uint64_t>(h, c.root + kOffGE).value_or(0);
        for (const auto& kv : lastGe) {
            const int item = box > 0x10000 ? rpm<std::int32_t>(h, box + kGEArrayPad + (std::uint64_t)kv.first * kGESlotSize + 8).value_or(-1) : -1;
            if (item >= 0 && item != kv.second) bad(s, "slot " + std::to_string(kv.first) + " item " + std::to_string(kv.second) + ", the slot holds " + std::to_string(item));
        }
    }
    int rows = 0, records = 0;
    for (const auto& e : rtx::sops::kExpected) {
        auto it = st.find(e.name);
        if (it == st.end()) continue;
        Stat& s = it->second;
        if (s.seen < 20) { records += s.seen; s.seen = 0; continue; }   // one row per name: a second opcode of the same name adds to it
        rows++;
        std::string d = std::to_string(s.seen) + " seen, " + std::to_string(s.decoded) + " decoded, " + std::to_string(s.bad) + " implausible";
        int ok = kPass;
        if (!s.gaps.empty()) {
            std::vector<int> g = s.gaps; std::sort(g.begin(), g.end());
            const int median = g[g.size() / 2];
            int off = 0; for (int x : g) if (x < 400 || x > 900) ++off;
            s.bad += off;
            d += ", tick interval median " + std::to_string(median) + " ms, " + std::to_string(off) + " outside 400..900";
            if (s.first.empty() && off) s.first = "an interval of " + std::to_string(g.back()) + " ms";
        }
        const int pct = s.seen ? (int)((long long)s.bad * 100 / s.seen) : 0;
        if (s.bad * 20 > s.seen) { ok = kFail; d = "FORMAT: " + std::to_string(pct) + " % of " + e.name + " records implausible (" + s.first + "); " + d; }
        else if (!s.rawOk && s.decoded * 4 < s.seen * 3) { ok = kWarn; d = "FORMAT: " + std::to_string(s.seen - s.decoded) + " of " + std::to_string(s.seen) + " " + e.name + " records did not decode; " + d; }
        else if (s.bad) d += " (" + s.first + ")";
        run.Add(G, std::string("pkt.live.") + e.name, std::string("Live: ") + e.name, ok, d, s.features, "< 5 % implausible", std::to_string(s.bad) + "/" + std::to_string(s.seen), "pkt.opcodes");
        run.Fact(std::string("op.") + e.name + ".live", std::to_string(s.seen) + "/" + std::to_string(s.decoded) + "/" + std::to_string(s.bad));
        s.seen = 0;
    }
    if (!rows) run.Add(G, "pkt.live", "Live layouts", kUnchecked, "fewer than 20 records of any expected opcode in the ring (" + std::to_string(records) + " seen)", kFeat, "", "", "pkt.events").need = "a few minutes in the world with skilling, containers and movement";
}

void health_packets(HCtx& c, rtx::health::Run& run) {
    const char* G = "Packets";
    HANDLE h = c.h;
    std::uint64_t tbl = 0;
    {
        std::uint64_t modSize = c.size;
        tbl = (c.base && modSize) ? cached_optable(c.pid, h, c.base, modSize) : 0;
    }
    if (!tbl) { run.Add(G, "pkt.table", "Packet table", kFail, "GONE: descriptor table not found (no table global reads as a descriptor vector)", "Every packet decoder", "", "", "code.packets"); return; }
    {
        // the table is an inline-buffer vector {begin, end, capacity, .., buffer at +0x28}: begin
        // names the buffer the reads go through, so the object sits 0x28 before it, and the
        // descriptor count is (capacity - begin) / 8, read instead of probed past the end
        const std::uint64_t self = tbl - 0x28;
        const std::uint64_t vb = rpm<std::uint64_t>(h, self).value_or(0), vc = rpm<std::uint64_t>(h, self + 0x10).value_or(0);
        const bool shape = vb == tbl && vc >= vb && ((vc - vb) % 8) == 0 && (vc - vb) / 8 <= 4096;
        const int count = shape ? (int)((vc - vb) / 8) : -1;
        const std::uint64_t dMax = rpm<std::uint64_t>(h, tbl + (std::uint64_t)rtx::sops::kOpMax * 8).value_or(0);
        const bool atMax = dMax > 0x10000 && rpm<std::int32_t>(h, dMax).value_or(-1) == rtx::sops::kOpMax;
        run.Fact("packets.live.descriptors", std::to_string(count));
        int ok = kPass; std::string d;
        if (!shape) { ok = atMax ? kWarn : kFail; d = std::string(atMax ? "FORMAT: " : "GONE: ") + "the table global is not an inline-buffer vector (begin " + hx(vb) + " vs buffer " + hx(tbl) + ")" + (atMax ? "; a descriptor still sits at " + hx(rtx::sops::kOpMax) : ""); }
        else if (count != rtx::sops::kOpMax + 1) { ok = kFail; d = "FORMAT: descriptor vector holds " + std::to_string(count) + ", compiled bound " + hx(rtx::sops::kOpMax) + " (opcodes renumbered: re-pin ServerOps.h)"; }
        else if (!atMax) { ok = kFail; d = "FORMAT: " + std::to_string(count) + " descriptors but the one at " + hx(rtx::sops::kOpMax) + " does not carry its opcode (descriptor layout moved)"; }
        else d = std::to_string(count) + " descriptors, opcodes 0.." + hx(rtx::sops::kOpMax);
        run.Add(G, "pkt.table", "Packet table", ok, d, "Every packet decoder", std::to_string(rtx::sops::kOpMax + 1) + " descriptors", count < 0 ? std::string("no vector") : std::to_string(count), "code.packets");
    }
    // lengths and handler fingerprints of every opcode the reader decodes
    {
        std::map<std::uint32_t, int> fpOp;   // fingerprint -> opcode, this exe
        std::vector<std::uint32_t> fps((std::size_t)rtx::sops::kOpMax + 1);
        std::vector<int> lens((std::size_t)rtx::sops::kOpMax + 1, 0x7FFF);   // wire length per opcode, 0x7FFF = no descriptor
        for (int op = 0; op <= rtx::sops::kOpMax; ++op) {
            const std::uint64_t d = rpm<std::uint64_t>(h, tbl + (std::uint64_t)op * 8).value_or(0);
            if (d <= 0x10000) continue;
            lens[(std::size_t)op] = rpm<std::int32_t>(h, d + kDescLenOff).value_or(0x7FFF);
            const std::uint64_t vt = rpm<std::uint64_t>(h, d + kDescVtblOff).value_or(0);
            const std::uint64_t fn = vt > 0x10000 ? rpm<std::uint64_t>(h, vt + kVtblHandlerOff).value_or(0) : 0;
            const std::uint32_t fp = fn ? handler_fingerprint(h, fn) : 0;
            fps[(std::size_t)op] = fp;
            if (fp) fpOp[fp] = op;
        }
        int okc = 0, total = 0, pinned = 0; std::string bad; bool moved = false;
        char stampHex[16]; std::snprintf(stampHex, sizeof(stampHex), "%08x", c.stamp);
        // The other clients of one game version (OpenGL, Vulkan) are built from the same source: where
        // this exe has no handlers recorded, a recorded one of its version stands in. A handler there
        // that now sits under another opcode here is a renumbering; one found nowhere here was only
        // compiled differently.
        std::uint32_t sibling = 0; int same = 0, differ = 0;
        {
            const rtx::pins::Line mine = rtx::pins::Find("build", stampHex);
            const std::string ver = mine.kind.empty() ? std::string() : rtx::pins::Field(mine.expect, "version");
            const rtx::pins::Line first = rtx::pins::Find("op", std::string(rtx::sops::kExpected[0].name) + "." + hx((std::uint32_t)rtx::sops::kExpected[0].op));
            if (!ver.empty() && !first.kind.empty() && rtx::pins::StampValue(first.expect, c.stamp).empty())
                for (const auto& l : *rtx::pins::Lines()) {
                    if (l.kind != "build" || l.key == stampHex || rtx::pins::Field(l.expect, "version") != ver) continue;
                    const std::uint32_t st = (std::uint32_t)std::strtoul(l.key.c_str(), nullptr, 16);
                    if (st && !rtx::pins::StampValue(first.expect, st).empty()) { sibling = st; break; }
                }
        }
        for (const auto& e : rtx::sops::kExpected) {
            ++total;
            const int len = lens[(std::size_t)e.op];
            char fpb[16]; std::snprintf(fpb, sizeof(fpb), "%08x", fps[(std::size_t)e.op]);
            run.Fact(std::string("op.") + e.name + "." + hx((std::uint32_t)e.op), hx((std::uint32_t)e.op) + "/len=" + std::to_string(len) + "/h=" + fpb);
            bool good = len == e.len;
            std::string why;
            if (!good) why = "length " + std::to_string(len) + ", expected " + std::to_string(e.len);
            // the manifest's handler fingerprint for this opcode on this exe
            const rtx::pins::Line pin = rtx::pins::Find("op", std::string(e.name) + "." + hx((std::uint32_t)e.op));
            const std::string want = pin.kind.empty() ? std::string() : rtx::pins::StampValue(pin.expect, c.stamp);
            if (!want.empty()) {
                ++pinned;
                const std::size_t hp = want.find("h=");
                const std::uint32_t wantFp = hp == std::string::npos ? 0 : (std::uint32_t)std::strtoul(want.c_str() + hp + 2, nullptr, 16);
                if (wantFp && wantFp != fps[(std::size_t)e.op]) {
                    good = false; moved = true;
                    auto it = fpOp.find(wantFp);
                    why += std::string(why.empty() ? "" : ", ") + "handler changed" + (it != fpOp.end() ? " (the recorded handler is now op " + hx((std::uint32_t)it->second) + ")" : "");
                }
            }
            if (want.empty() && sibling && !pin.kind.empty()) {
                const std::string sv = rtx::pins::StampValue(pin.expect, sibling);
                const std::size_t hp = sv.find("h=");
                const std::uint32_t sibFp = hp == std::string::npos ? 0 : (std::uint32_t)std::strtoul(sv.c_str() + hp + 2, nullptr, 16);
                if (sibFp && sibFp == fps[(std::size_t)e.op]) ++same;
                else if (sibFp) {
                    auto it = fpOp.find(sibFp);
                    if (it != fpOp.end() && it->second != e.op) { good = false; moved = true; why += std::string(why.empty() ? "" : ", ") + "its handler is now op " + hx((std::uint32_t)it->second); }
                    else ++differ;
                }
            }
            if (good) ++okc;
            else bad += std::string(bad.empty() ? "" : "; ") + e.name + " " + hx((std::uint32_t)e.op) + ": " + why;
        }
        run.Fact("packets.pinnedOps", std::to_string(pinned) + "/" + std::to_string(total));
        if (okc == total && pinned == 0 && sibling && same * 2 >= total) {
            char sib[16]; std::snprintf(sib, sizeof(sib), "%08x", sibling);
            run.Fact("packets.siblingOps", std::to_string(same) + "/" + std::to_string(total) + " as " + sib);
            run.Add(G, "pkt.opcodes", "Server opcodes", kPass,
                    std::to_string(total) + " opcodes carry their wire length; " + std::to_string(same) + " handlers are the ones recorded for the " + sib + " client of this game version, " +
                    std::to_string(differ) + " compiled differently, none under another opcode",
                    "Chat capture|Events channel|Zone events|Var updates|GE offers", std::to_string(total) + " lengths and handlers", std::to_string(okc) + " match, " + std::to_string(same) + " as " + sib, "pkt.table");
        } else if (okc == total && pinned == 0) {
            // lengths alone pass a renumbering that keeps a length: without the handler fingerprints
            // of this exe the compare says nothing
            run.Add(G, "pkt.opcodes", "Server opcodes", kFail,
                    std::string("UNVERIFIED: handler fingerprints not recorded for this build (") + stampHex + "): " + std::to_string(total) + " lengths match, a renumbered opcode with the same length would too; record op lines for this exe with the pins generator",
                    "Chat capture|Events channel|Zone events|Var updates|GE offers", std::to_string(total) + " fingerprints", "0 recorded", "pkt.table").kind = "unrecorded";
        } else {
            std::string d = okc == total ? std::to_string(total) + " opcodes carry their wire length" + (pinned < total ? ", " + std::to_string(pinned) + " with a recorded handler" : " and handler") : std::to_string(okc) + "/" + std::to_string(total) + " match; " + bad;
            if (okc != total) d = std::string(moved ? "MOVED: " : "FORMAT: ") + d;
            run.Add(G, "pkt.opcodes", "Server opcodes", okc == total ? kPass : kFail, d,
                    "Chat capture|Events channel|Zone events|Var updates|GE offers", std::to_string(total) + " lengths and handlers", std::to_string(okc) + " match, " + std::to_string(pinned) + " recorded", "pkt.table");
        }
        // every opcode's wire length against the compiled table: an update that resizes any packet is named here
        {
            const int all = rtx::sops::kOpMax + 1; int agree = 0; std::string diff;
            for (int op = 0; op < all; ++op) {
                if (lens[(std::size_t)op] == rtx::sops::kAllLengths[op]) { ++agree; continue; }
                if (diff.size() < 240) diff += std::string(diff.empty() ? "" : ", ") + hx((std::uint32_t)op) + " " + (lens[(std::size_t)op] == 0x7FFF ? std::string("none") : std::to_string(lens[(std::size_t)op])) + "/" + std::to_string(rtx::sops::kAllLengths[op]);
            }
            run.Add(G, "pkt.lengths", "Wire lengths, every opcode", agree == all ? kPass : kFail,
                    agree == all ? std::to_string(all) + " opcodes carry their compiled wire length" : "FORMAT: " + std::to_string(all - agree) + " of " + std::to_string(all) + " opcodes changed length (live/compiled): " + diff,
                    "Packet decoders|Events channel", std::to_string(all) + " lengths", std::to_string(agree) + " agree", "pkt.table");
        }
    }
    // zone sub-packets: the table the zone update dispatches through
    {
        int tableRun = 0;
        const std::uint32_t found = find_zone_sub_table(c, &tableRun);
        const std::uint32_t recorded = rtx::pins::PinnedRva("zoneSub", c.stamp);
        const std::uint32_t pinned = found ? found : recorded;
        if (found) run.Fact("packets.zoneSubRva", hx(found));
        int ok = kFail; std::string d = "GONE: no table of zone sub-packet descriptors found in the client", word;
        if (pinned) {
            int agree = 0, n = 0; std::string bad;
            for (int sub = 0; sub <= rtx::evzone::kSubCount; ++sub) {
                const std::uint64_t d0 = rpm<std::uint64_t>(h, c.base + pinned + (std::uint64_t)sub * 8).value_or(0);
                const bool desc = d0 > 0x10000 && rpm<std::int32_t>(h, d0).value_or(-1) == sub;
                if (sub == rtx::evzone::kSubCount) { if (desc) { word = "NEW"; bad += " a sub-packet past " + hx((std::uint32_t)(rtx::evzone::kSubCount - 1)); ++n; } break; }
                const int want = rtx::evzone::subLen(sub);
                const int len = desc ? rpm<std::int32_t>(h, d0 + 4).value_or(-100) : -100;
                ++n;
                if (len == want) ++agree; else { if (word.empty()) word = desc ? "FORMAT" : "GONE"; if (bad.size() < 60) bad += " sub " + std::to_string(sub) + " " + std::to_string(len) + "/" + std::to_string(want); }
            }
            ok = n && agree == n ? kPass : kFail;
            d = std::to_string(agree) + "/" + std::to_string(n) + " zone sub-packet lengths agree" + (bad.empty() ? "" : ";" + bad);
            if (found && recorded && found != recorded) d += "; table at " + hx(found) + ", recorded " + hx(recorded);
            if (ok != kPass) d = (word.empty() ? std::string("GONE") : word) + ": " + d;
        }
        run.Add(G, "pkt.zone", "Zone sub-packets", ok, d, "Ground items|Loc changes|Spot animations|Projectiles|Sounds", std::to_string(rtx::evzone::kSubCount) + " subs with their lengths", pinned ? d.substr(d.find_first_of("0123456789")) : std::string("no table"), "pkt.table");
    }
    // the event ring the launcher reads: mask as asked, message_game left to the chat ring
    if (c.cli) {
        run.Add(G, "pkt.events", "Event channel", kUnchecked, "launcher only", "Events channel (plugins)", "", "", "comp.boot").need = "a launcher run";
    } else {
        wchar_t name[rtx::ipc::kNameChars]; rtx::events::MakeSectionName(c.pid, name);
        ShareMap<rtx::events::Share> ev;
        if (!ev.open(name) || ev.sh->magic != rtx::events::kMagic) run.Add(G, "pkt.events", "Event channel", kFail, "GONE: event ring not mapped (the companion did not create it)", "Events channel (plugins)", "", "", "comp.boot");
        else {
            const bool hook = (ev.sh->flags & 1) != 0;
            const bool chatOff = !(ev.sh->mask[rtx::sops::kMessageGame >> 5] & (1u << (rtx::sops::kMessageGame & 31)));
            run.Add(G, "pkt.events", "Event channel", hook && chatOff ? kPass : kFail,
                    std::string(hook && chatOff ? "" : hook ? "FORMAT: " : "GONE: ") + (hook ? "hook feeding" : "hook not feeding") + ", " + std::to_string(ev.sh->written) + " records, message_game " + (chatOff ? "left to the chat ring" : "in the mask"),
                    "Events channel (plugins)", "hook feeding, message_game masked out", std::string(hook ? "feeding" : "not feeding") + ", " + std::to_string(ev.sh->written) + " records", "comp.boot");
        }
    }
    health_packets_live(c, run);
}

// ---- Content ----
void health_dailies(HCtx& c, rtx::health::Run& run) {
    std::string slots, changed, exp, got; int defsBad = 0, assigned = 0, n = 0;
    auto bits = [](const std::string& fp) {   // dom=0;var=3240;lo=0;hi=5 -> varp 3240 [0..5]
        const std::string dom = rtx::pins::Field(fp, "dom");
        return (dom == "0" ? std::string("varp ") : "domain " + dom + " var ") + rtx::pins::Field(fp, "var") + " [" + rtx::pins::Field(fp, "lo") + ".." + rtx::pins::Field(fp, "hi") + "]";
    };
    for (int i = 0; i < 5; ++i) {
        const int vb = 16574 + i * 4;
        int vp = -1, lsb = -1, msb = -1;
        const bool have = rtx::cache::GetVarbit(vb, vp, lsb, msb);
        const rtx::pins::Line pin = rtx::pins::Find("varbit", std::to_string(vb));
        if (!pin.kind.empty() && have) {
            const std::string want = pin.expect, now = rtx::cache::PinFingerprint("varbit", std::to_string(vb), "");
            if (want != now) {
                ++defsBad;
                changed += (changed.empty() ? "" : "; ") + std::to_string(vb) + " changed: was " + bits(want) + ", now " + bits(now);
                if (exp.empty()) { exp = "varbit " + std::to_string(vb) + " = " + bits(want); got = bits(now); }
            }
        }
        if (!have) { ++defsBad; slots += (slots.empty() ? "" : "; ") + std::to_string(vb) + " gone"; continue; }
        ++n;
        const int raw = read_varp(c.h, c.root, vp);
        const int v = (raw >> lsb) & (int)((1u << (msb - lsb + 1)) - 1);
        if (v >= 1) ++assigned;
        slots += (slots.empty() ? "" : "; ") + std::to_string(vb) + " -> varp " + std::to_string(vp) + " [" + std::to_string(lsb) + ".." + std::to_string(msb) + "] raw " + std::to_string(raw);
    }
    int ok;
    std::string d, need;
    if (defsBad) { ok = kFail; d = std::string(changed.empty() ? "GONE: " : "MOVED: ") + std::to_string(defsBad) + (defsBad == 1 ? " slot definition changed: " : " slot definitions changed: ") + changed + "; now " + slots; }
    else if (!c.inWorld) { ok = kUnchecked; d = "not in the world; " + slots; need = "log in"; }
    else if (assigned) { ok = kPass; d = std::to_string(assigned) + " slots assigned; " + slots; }
    else {
        ok = kUnchecked;
        int day = 0, vp = -1, lo = 0, hi = 0;
        if (rtx::cache::GetVarbit(16572, vp, lo, hi)) day = (read_varp(c.h, c.root, vp) >> lo) & (int)((1u << (hi - lo + 1)) - 1);
        const long long today = (long long)((std::time(nullptr) - 1014768000LL) / 86400);   // runeday 0 = 2002-02-27
        d = (day > 0 && day >= today - 1) ? "set of runeday " + std::to_string(day) + " is current: challenges completed; " + slots
                                          : "no challenges assigned; " + slots;
        need = "a daily challenge assigned";
    }
    run.Add("Content", "content.dailies.slots", "Daily challenge slots", ok, d, "D&D Tracker: daily challenges", exp, got, "content.cache").need = need;
}

// ---- Live values (manifest "live" lines) ----
void health_live(HCtx& c, rtx::health::Run& run) {
    const char* G = "Live values";
    const auto lines = rtx::pins::Lines();
    int total = 0, good = 0;
    for (const auto& l : *lines) {
        if (l.kind != "live") continue;
        ++total;
        const std::size_t colon = l.key.find(':');
        const std::string vk = l.key.substr(0, colon);
        const int id = std::atoi(l.key.c_str() + (colon == std::string::npos ? 0 : colon + 1));
        const std::string gate = rtx::pins::Field(l.expect, "gate");
        const std::string rid = "live." + vk + "." + std::to_string(id);
        const std::string key = (vk == "varp" ? "Varp " : vk == "varc" ? "Varc " : "Varbit ") + std::to_string(id);
        const std::string hint = rtx::pins::Field(l.expect, "need");
        if (gate == "world" && !c.inWorld) { run.Add(G, rid, key, kUnchecked, "not in the world", l.features).need = hint.empty() ? "log in" : hint; continue; }
        long long v = 0; bool found = true;
        if (vk == "varp") v = read_varp(c.h, c.root, id);
        else if (vk == "varc") { int x = 0; found = read_varc_found(c.h, c.root, id, x); v = x; }
        else if (vk == "varbit") {
            int vp = -1, lo = 0, hi = 0;
            found = rtx::cache::GetVarbit(id, vp, lo, hi);
            if (found) v = (read_varp(c.h, c.root, vp) >> lo) & (long long)((1ull << (hi - lo + 1)) - 1);
        }
        if (!found) { run.Add(G, rid, key, kFail, "GONE: not present in the store" + (vk == "varbit" ? std::string(" (no definition for the varbit)") : std::string()), l.features, l.expect, "absent", "data.varps"); continue; }
        long long x = v;
        const std::string field = rtx::pins::Field(l.expect, "field");
        if (!field.empty()) {
            const int lo = std::atoi(field.c_str()), hi = std::atoi(field.c_str() + field.find("..") + 2);
            x = (v >> lo) & (long long)((1ull << (hi - lo + 1)) - 1);
        }
        bool ok = true; std::string why;
        const std::string range = rtx::pins::Field(l.expect, "range");
        if (!range.empty()) {
            const long long lo = std::atoll(range.c_str()), hi = std::atoll(range.c_str() + range.find("..") + 2);
            if (x < lo || x > hi) { ok = false; why = "out of " + range; }
        }
        // mask=varbits: the bits some cache varbit defines on this varp, so no account's flags read as foreign
        const std::string mask = rtx::pins::Field(l.expect, "mask");
        if (!mask.empty()) {
            const bool fields = mask == "varbits";
            const unsigned long long m = fields ? rtx::cache::VarpFieldMask(id) : std::strtoull(mask.c_str(), nullptr, 0);
            if ((unsigned long long)(std::uint32_t)x & ~m) {
                char mb[24]; std::snprintf(mb, sizeof(mb), "0x%llX", m);
                ok = false; why = fields ? std::string("bits outside its varbits (") + mb + ")" : "bits outside " + mask;
            }
        }
        const std::string set = rtx::pins::Field(l.expect, "set");
        if (!set.empty()) {
            bool in = false; std::size_t at = 0;
            while (at <= set.size()) { std::size_t e = set.find(',', at); if (e == std::string::npos) e = set.size(); if (std::atoll(set.c_str() + at) == x) in = true; at = e + 1; }
            if (!in) { ok = false; why = "not one of " + set; }
        }
        const std::string check = rtx::pins::Field(l.expect, "check");
        if (check == "pouch" && x != -1) {
            std::string nm = rtx::cache::ItemName((int)x);
            for (char& ch : nm) ch = (char)std::tolower((unsigned char)ch);
            if (nm.size() < 5 || nm.compare(nm.size() - 5, 5, "pouch") != 0) { ok = false; why = "item " + std::to_string(x) + " is not a pouch"; }
        } else if (check.rfind("structparam", 0) == 0 && x != -1 && x != 0) {
            std::string s; const int p = std::atoi(check.c_str() + 11);
            if (!rtx::cache::StructStrParam((int)x, p, s) || s.empty()) { ok = false; why = "struct " + std::to_string(x) + " has no param " + std::to_string(p); }
        } else if (check.rfind("enumkey", 0) == 0 && x != 0) {
            const int e = std::atoi(check.c_str() + 7);
            if (rtx::cache::EnumJson(e).find("\"" + std::to_string(x) + "\":") == std::string::npos) { ok = false; why = "not a key of enum " + std::to_string(e); }
        } else if (check.rfind("enumval", 0) == 0 && x != -1 && x != 0) {   // one of the values the game's own list holds
            const int e = std::atoi(check.c_str() + 7);
            const std::string j = rtx::cache::EnumJson(e), v = ":" + std::to_string(x);
            bool in = false;
            for (std::size_t p = j.find(v); p != std::string::npos && !in; p = j.find(v, p + 1)) in = p + v.size() < j.size() && (j[p + v.size()] == ',' || j[p + v.size()] == '}');
            if (!in) { ok = false; why = "not a value of enum " + std::to_string(e); }
        } else if (check == "lpmax") {
            long long con = -1;
            for (const auto& s : SampleAll()) if (s.pid == c.pid && s.skills.size() > 3) con = s.skills[3].real;
            if (con > 0 && (x < con * 100 - 2000 || x > con * 100 + 25000)) { ok = false; why = "not near 100 x Constitution " + std::to_string(con); }
        } else if (check.rfind("lpcur", 0) == 0) {   // current life points: boosts go past the maximum, never 3 times it
            const int maxVarp = std::atoi(check.c_str() + 5);
            const long long mx = read_varp(c.h, c.root, maxVarp);
            if (mx <= 0 || x < 0 || x > 3 * mx) { ok = false; why = "not within 0..3 x the maximum " + std::to_string(mx) + " (varp " + std::to_string(maxVarp) + ")"; }
        }
        if (ok) ++good;
        run.Add(G, rid, key, ok ? kPass : kFail, std::string(ok ? "" : "FORMAT: ") + std::to_string(x) + (ok ? "" : " (" + why + ")"), l.features, l.expect, std::to_string(x), "data.varps");
    }
    if (total == 0) run.Add(G, "live.none", "Live values", kUnchecked, "UNVERIFIED: no live lines in the manifest", "", "", "", "content.pins").kind = "unrecorded";
}

// ---- Cache format ----
void health_cache(HCtx& c, rtx::health::Run& run) {
    (void)c;
    const char* G = "Cache format";
    // the full sweep started with the run; once done its rows and opcode counts replace the sampled
    // pass, so give it a moment rather than report a sample
    for (int i = 0; i < 300 && !rtx::cache::CacheFullSweepReady() && rtx::cache::CacheFullSweepRunning(); ++i) Sleep(50);
    const bool sweepDone = rtx::cache::CacheFullSweepReady();
    rtx::pins::CheckCacheFormat(run);
    // one row per decoder: records read to their end marker, how the others ended (stop code, the
    // opcode before it, the first id), implausible values, and whether the full sweep has finished
    for (const auto& r : rtx::cache::CacheParseHealth()) {
        std::string note, word, got; int status;
        const std::string feats = r.features.empty() ? "Everything that reads " + r.name : r.features;
        if (r.total == 0) { status = kUnchecked; note = "no data (cache not open yet)"; }
        else {
            note = std::to_string(r.ok) + "/" + std::to_string(r.total) + (r.full ? " parsed" : r.sampled ? (sweepDone ? " sampled" : " sampled, full sweep running") : " parsed");
            got = std::to_string(r.ok) + "/" + std::to_string(r.total);
            if (r.stop_n > 0) {
                const std::string n = std::to_string(r.stop_n) + " of " + std::to_string(r.total);
                const std::string after = !r.first_detail.empty() ? " " + r.first_detail : r.stop_last >= 0 ? " after opcode " + std::to_string(r.stop_last) : std::string();
                const std::string first = r.first_id >= 0 ? " (first id " + std::to_string(r.first_id) + ")" : std::string();
                switch (r.stop_op) {
                    case 256: word = "FORMAT"; note += ", " + n + " records ran past their end" + after + first; break;
                    case 257: word = "FORMAT"; note += ", " + n + " records disagree with their table schema" + first; break;
                    case 258: word = "FORMAT"; note += ", " + n + " records ended with bytes unread" + after + first; break;
                    case 259: word = "FORMAT"; note += ", " + n + " records with a length-prefixed block that did not fit" + after + first; break;
                    default:  word = "NEW"; note += ", " + n + " records break at opcode " + std::to_string(r.stop_op) + " (not in the decoder's table)" + first; break;
                }
                got += ", " + std::to_string(r.stop_n) + " stopped on " + std::to_string(r.stop_op);
            }
            if (r.implausible > 0) {
                if (word.empty()) word = "FORMAT";
                note += "; " + std::to_string(r.implausible) + " implausible" + (r.implausible_rule.empty() ? "" : " (rule " + r.implausible_rule + (r.implausible_id >= 0 ? ", first id " + std::to_string(r.implausible_id) : "") + ")");
                got += ", " + std::to_string(r.implausible) + " implausible";
            }
            const bool clean = r.ok == r.total && r.implausible * 1000 <= r.total;   // one odd record in a thousand is data, not a format change
            const bool nearly = r.ok * 20 >= r.total * 19 && r.implausible * 100 <= r.total;
            status = clean ? kPass : nearly ? kWarn : kFail;
            if (status != kPass && !word.empty()) note = word + ": " + note;
        }
        std::string slug = r.name; for (char& ch : slug) if (ch == ' ' || ch == '(' || ch == ')') ch = '-';
        rtx::health::Row& row = run.Add(G, "cache.parse." + slug, "Decode: " + r.name, status, note, feats, std::to_string(r.total) + " clean", got, "content.cache");
        if (status == kUnchecked) row.need = "an open cache";
        run.Fact("cache.parse." + slug, std::to_string(r.ok) + "/" + std::to_string(r.total) + (r.full ? " full" : " sampled"));
    }
    {   // var domains and quest tracking, kept from the earlier checks
        std::string cen = rtx::cache::VarbitDomainsJson();
        int doms = 0;
        for (int dom = 0; dom <= 8; ++dom) if (cen.find("\"" + std::to_string(dom) + "\":{") != std::string::npos) ++doms;
        auto unknownOf = [](const std::string& j) { auto p = j.find("\"unknown\":"); return p == std::string::npos ? -1 : std::atoi(j.c_str() + p + 10); };
        const int u60 = unknownOf(rtx::cache::VarDefsJson(60)), u62 = unknownOf(rtx::cache::VarDefsJson(62));
        const int dok = (doms == 9 && u60 == 0 && u62 == 0) ? kPass : (doms == 0 ? kUnchecked : kFail);
        run.Add(G, "cache.vardomains", "Var domains", dok,
                std::string(dok == kFail ? (doms < 9 ? "GONE: " : "NEW: ") : "") + std::to_string(doms) + "/9 domains defined; unknown var-config opcodes: player " + std::to_string(u60) + ", client " + std::to_string(u62) + (dok == kFail && doms == 9 ? " (a var-config opcode the reader does not know)" : ""),
                "Every varbit read", "9 domains, 0 unknown opcodes", std::to_string(doms) + " domains, " + std::to_string(u60 + u62) + " unknown", "content.cache").need = dok == kUnchecked ? "an open cache" : "";
        const std::string j = rtx::cache::QuestHealthJson();
        auto num = [&](const char* k) { auto p2 = j.find(std::string("\"") + k + "\":"); return p2 == std::string::npos ? -1 : std::atoi(j.c_str() + p2 + std::strlen(k) + 3); };
        const int quests = num("quests"), tracked = num("tracked"), failed = num("failedArchives");
        const int qok = quests <= 0 ? kUnchecked : (tracked == quests && failed <= 0 ? kPass : kFail);
        run.Add(G, "cache.quests", "Quest tracking", qok,
                quests <= 0 ? "cache not open yet" : std::string(qok == kFail ? (failed > 0 ? "GONE: " : "MOVED: ") : "") + std::to_string(tracked) + "/" + std::to_string(quests) + " quests resolve a progress tracker" + (qok == kFail && failed <= 0 ? " (the tracker opcode moved or changed shape)" : "") + (failed > 0 ? "; " + std::to_string(failed) + " config archives failed" : ""),
                "Quests panel|Quest helper", "every quest with a tracker", std::to_string(tracked) + "/" + std::to_string(quests), "content.cache").need = qok == kUnchecked ? "an open cache" : "";
        const PerkFieldLayout& L = perk_field_layout();
        const int talking = rtx::cache::PerkRankCount(23), honed = rtx::cache::PerkRankCount(5);
        const bool pok = L.from_cache && talking == 1 && honed == 6;
        run.Add(G, "cache.perks", "Perk layout", pok ? kPass : kFail,
                std::string(pok ? "" : "FORMAT: ") + std::string(L.from_cache ? (L.drift ? std::to_string(L.drift) + " fields differ from the built-in layout, cache layout in use" : "9 fields match the cache") : "perk varbits not all resolved, built-in layout in use") +
                "; rank counts Talking=" + std::to_string(talking) + " Honed=" + std::to_string(honed), "Perks panel", "Talking 1, Honed 6 from the cache layout", "Talking " + std::to_string(talking) + ", Honed " + std::to_string(honed), "content.cache");
    }
}

// ---- Companion ----
void health_companion(HCtx& c, rtx::health::Run& run) {
    const char* G = "Companion";
    const rtx::bootlog::Boot b = rtx::bootlog::Read(c.pid);
    c.bootFound = b.found;
    if (!b.found) {
        run.Add(G, "comp.boot", "Boot record", kFail, "GONE: no companion boot record for this client (the companion did not load)", "Everything the companion does");
    } else {
        int attached = 0; std::string missing, refused;
        for (const auto& hk : b.hooks) {
            if (hk.state == "ATTACHED") ++attached;
            else if (hk.state == "REFUSED") refused += (refused.empty() ? "" : ", ") + hk.name;
            else missing += (missing.empty() ? "" : ", ") + hk.name + " " + hk.state;
        }
        std::string d = std::to_string(attached) + " attached" + (missing.empty() ? "" : "; " + missing) +
                        (refused.empty() ? "" : "; held back: " + refused) +
                        (b.attached < 0 ? " (record predates the hook summary)" : "");
        if (!missing.empty()) d = "GONE: " + d;
        run.Add(G, "comp.boot", "Boot record", missing.empty() ? kPass : kFail, d, "Everything the companion does", "every hook attached", std::to_string(attached) + " attached", "");
        auto sigFeatures = [](const std::string& n) -> std::string {
            for (const auto& sg : rtx::sig::kTable) if (n == sg.name) return sg.features;
            return "Companion: " + n;
        };
        for (const auto& hk : b.hooks) {
            const int sig = run.StatusOf("code.sig." + hk.name);
            std::string dd = hk.state + (hk.rva ? " at " + hx(hk.rva) : std::string()) + (hk.note.empty() || hk.rva ? std::string() : " " + hk.note);
            int ok = hk.state == "ATTACHED" ? kPass : hk.state == "REFUSED" ? kUnchecked : kFail;
            std::string need;
            if (hk.state != "ATTACHED" && c.flavour == "vulkan") {
                for (const auto& sg : rtx::sig::kTable)
                    if (sg.expect == rtx::sig::kOpenGlOnce && hk.name == sg.name) { ok = kPass; dd = "not used on Vulkan"; break; }
            }
            if (ok == kPass && hk.via == "anchor") { ok = kWarn; dd = "MOVED: found by anchor at " + hx(hk.rva) + ", signature missed" + (hk.sigRva ? " (signature at " + hx(hk.sigRva) + ")" : "") + "; attached there"; }
            else if (ok == kPass && hk.via == "sig" && hk.anchor.rfind("0x", 0) == 0) { ok = kWarn; dd = "UNVERIFIED: " + dd + "; its anchor names another routine (" + hk.anchor + "): the signature or the anchor is stale"; }
            else if (ok == kPass && !hk.anchor.empty() && hk.anchor != "agrees" && hk.anchor.rfind("0x", 0) != 0 && hk.anchor != "none" && hk.anchor.rfind("ambiguous", 0) != 0) dd += "; anchor " + hk.anchor;
            if (ok == kFail) dd = (hk.state == "AMBIGUOUS" ? "NEW: " : "GONE: ") + dd + (hk.state == "AMBIGUOUS" ? " (more than one hit, the hook refuses to guess)" : hk.state == "FAILED" ? " (the detour did not take)" : " (the signature and its anchor found nothing)");
            if (ok == kUnchecked) { dd = "UNVERIFIED: " + dd; need = "a validated build (the companion holds this hook back until then)"; }
            if (ok == kFail && sig == kPass) dd += "; this build's signature finds it (the companion running now predates it; a companion built from this source attaches it)";
            if (ok == kPass && sig == kFail) { ok = kWarn; dd = "MOVED: " + dd + "; its signature row fails for this exe (see Signature " + hk.name + ")"; }
            run.Fact("hook." + hk.name, hk.state + (hk.rva ? "@" + hx(hk.rva) : std::string()) + (hk.via.empty() ? "" : " via " + hk.via));
            run.Add(G, "comp.hook." + hk.name, "Hook " + hk.name, ok, dd, sigFeatures(hk.name), "ATTACHED", hk.state + (hk.via.empty() ? "" : " via " + hk.via), "comp.boot").need = need;
        }
        // the companion's own self-checks (one line each in the boot record): what it read back
        // before it writes anywhere, and what it refuses when the read-back is not what it expects
        {
            static const struct { const char* name; const char* features; } kCheckFeatures[] = {
                { "scene-root", "Everything the companion reads" }, { "player-entity", "Local player|In-frame labels|Overhead anchors" },
                { "ground-stacks", "Ground items" }, { "framer-conn", "Packet capture|Events channel|Chat capture" },
                { "markers", "Engine markers" }, { "menu-record", "Menu swaps|Menu swaps: left-click option" },
                { "hover-object", "Tooltip text (item prices, levels)" }, { "highlight", "Native outlines" },
                { "packets", "Packet decoders|Events channel" }, { "engine-ops", "Asks|Sounds|Camera|In-frame panels" },
            };
            for (const std::string& name : rtx::bootlog::ExpectedChecks()) {
                std::string feats = "Companion: " + name;
                for (const auto& cf : kCheckFeatures) if (name == cf.name) feats = cf.features;
                const rtx::bootlog::Check* ck = nullptr;
                for (const auto& x : b.checks) if (x.name == name) { ck = &x; break; }
                const std::string rid = "comp.check." + name, key = "Check " + name;
                if (!ck) {
                    rtx::health::Row& row = run.Add(G, rid, key, kUnchecked, "UNVERIFIED: companion did not report " + name + " (an older companion, or the module never got that far)", feats, "a check line", "none", "comp.boot");
                    row.kind = "unrecorded"; row.need = "a companion built from this source running in the client";
                    continue;
                }
                if (!ck->features.empty()) feats = ck->features;
                run.Fact("check." + name, ck->state + (ck->kind.empty() ? "" : " " + ck->kind));
                std::string dd = ck->text;
                int ok = ck->state == "OK" ? kPass : ck->state == "SKIP" ? kUnchecked : kFail;
                auto hasWord = [](const std::string& s) { for (const char* w : { "MOVED", "FORMAT", "GONE", "NEW", "UNVERIFIED" }) if (s.rfind(w, 0) == 0) return true; return false; };
                auto upper = [](std::string w) { for (char& ch : w) ch = (char)std::toupper((unsigned char)ch); return w; };
                if (ok == kPass && !ck->kind.empty()) {   // works after adopting a moved value
                    ok = kWarn;
                    if (dd.empty()) dd = "adopted a moved value";
                    if (!hasWord(dd)) dd = upper(ck->kind) + ": " + dd;
                } else if (ok == kFail) {
                    if (dd.empty()) dd = "the companion's read-back was not what it expects; the module refuses its writes";
                    if (!hasWord(dd)) dd = upper(ck->kind.empty() ? std::string("GONE") : ck->kind) + ": " + dd;
                } else if (ok == kUnchecked && dd.empty()) dd = "the companion could not judge this yet";
                if (ok == kPass && dd.empty()) dd = "ok" + (ck->got.empty() ? std::string() : " (" + ck->got + ")");
                run.Add(G, rid, key, ok, dd, feats, ck->exp, ck->got, "comp.boot").need = ok == kUnchecked ? (ck->need.empty() ? "the state the companion names in its boot record" : ck->need) : "";
            }
        }
        // heartbeat: a graphic id that never changes reads the wrong field, unless every effect around
        // the player shows that graphic; the reader's own scene read tells the two apart. The companion
        // keeps the last graphic it saw, and a scan ring one (6841..6843) until another ring replaces it,
        // so a constant one with no effect showing it now proves nothing either way.
        if (b.beats.size() >= 3) {
            std::set<std::string> gfx;
            for (const auto& s : b.beats) { const std::size_t p = s.find("gfx="); if (p != std::string::npos) gfx.insert(s.substr(p + 4, s.find(' ', p) - p - 4)); }
            const bool constant = gfx.size() == 1 && *gfx.begin() != "0";
            int beatOk = kPass; std::string beatD = std::to_string(b.beats.size()) + " recent beats";
            if (constant) {
                const std::string& g = *gfx.begin();
                const int gi = (int)std::strtoul(g.c_str(), nullptr, 10);   // logged unsigned
                scene_t4(c);
                const auto it = c.t4Gfx.find(gi);
                beatD = "type-4 graphic reads " + g + " in every beat; ";
                if (!c.fxRead) { beatOk = kUnchecked; beatD += std::string("scene not read (") + (c.inWorld ? "no scene worker" : "not in the world") + "), nothing to compare it with"; }
                else if (c.t4Seen == 0) { beatOk = kUnchecked; beatD += "the reader's scene read sees no type-4 effects to compare it with"; }
                else if (it != c.t4Gfx.end()) {
                    beatD += "the reader's scene read sees it too (" + std::to_string(it->second) + "/" + std::to_string(c.t4Seen) + " type-4 effects), so the scene holds that graphic";
                } else if (gi >= 6841 && gi <= 6843) {
                    beatOk = kUnchecked;
                    beatD += "a scan ring graphic, which the companion keeps once seen, not in the reader's scene read now: nothing to compare it with";
                } else {
                    beatOk = kWarn;
                    std::string seen; int n = 0;
                    for (const auto& kv : c.t4Gfx) { if (++n > 4) { seen += " ..."; break; } seen += " " + std::to_string(kv.first); }
                    beatD = "FORMAT: " + beatD + "the reader's scene read sees " + std::to_string(c.t4Seen) + " type-4 effects and none with it (" +
                             (seen.empty() ? std::string("none validated") : "validated:" + seen) + "): graphic field offset wrong in the running companion";
                }
            }
            run.Add(G, "comp.heartbeat", "Heartbeat", beatOk, beatD, "Specials: clue scan ring", "a changing graphic, or one the scene holds", gfx.empty() ? std::string() : *gfx.begin(), "comp.boot").need = beatOk == kUnchecked ? "a type-4 effect in view" : "";
        }
        for (const auto& n : b.notes)
            if (n.find("are from game build") != std::string::npos || n.find("not extracted") != std::string::npos)
                run.Add(G, "comp.engineops", "Engine ops names", kFail, "GONE: " + n, "Asks|Sounds|Camera|In-frame panels", "", "", "comp.boot");
    }
    if (c.cli) {
        run.Add(G, "comp.channels", "Live channels", kUnchecked, "launcher only (channel names are per session)", "Overlays|Specials|Live variables|Packets|Menus|Sounds|Chat mute", "", "", "comp.boot").need = "a launcher run";
        return;
    }
    {
        wchar_t name[rtx::ipc::kNameChars]; rtx::frame::MakeSectionName(c.pid, name);
        ShareMap<rtx::frame::Share> f;
        int ok = kFail; std::string d = "GONE: frame channel not mapped";
        if (f.open(name) && f.sh->magic == rtx::frame::kMagic) {
            if (f.sh->module_seq > 0 && f.sh->client_w > 0 && f.sh->client_h > 0) { ok = kPass; d = "compositing at " + std::to_string(f.sh->client_w) + "x" + std::to_string(f.sh->client_h) + ", " + std::to_string(f.sh->module_seq) + " presents"; }
            else d = "GONE: mapped, the game never presented through it (the present entry was not found)";
        }
        run.Add(G, "comp.frame", "In-game UI frame", ok, d, "In-game panels|Overlays", "presents through the channel", d.substr(d.find(": ") == std::string::npos ? 0 : d.find(": ") + 2), "comp.boot");
    }
    {
        wchar_t name[rtx::ipc::kNameChars]; rtx::render::MakeSectionName(c.pid, name);
        ShareMap<rtx::render::Share> r;
        int ok = kFail; std::string d = "GONE: render channel not mapped";
        if (r.open(name) && r.sh->magic == rtx::render::kMagic) {
            const std::uint32_t in = r.sh->installed;
            const bool vk = c.flavour == "vulkan";
            const bool want = (in & 3) == 3 && (vk || (in & 4));
            ok = want ? kPass : kFail;
            d = std::string(want ? "" : "GONE: ") + ((in & 1) ? "NPC hide" : "no NPC hide") + ", " + ((in & 2) ? "player hide" : "no player hide") + ", " + ((in & 4) ? "scene blank" : vk ? "no scene blank (Vulkan)" : "no scene blank");
        }
        run.Add(G, "comp.render", "Rendering toggles", ok, d, "Rendering panel", "NPC hide, player hide, scene blank on OpenGL", d.substr(d.find(": ") == std::string::npos ? 0 : d.find(": ") + 2), "comp.boot");
    }
    {
        wchar_t name[rtx::ipc::kNameChars]; rtx::special::MakeSectionName(c.pid, name);
        ShareMap<rtx::special::Share> s;
        int ok = kFail; std::string d = "GONE: specials channel not mapped";
        if (s.open(name) && s.sh->magic == rtx::special::kMagic) {
            ok = (s.sh->flags & 1) ? kPass : kFail;
            d = std::string((s.sh->flags & 1) ? "spawn observer installed" : "GONE: spawn observer missing") + ", tracked " + std::to_string(s.sh->diag[6]) + ", last graphic " + std::to_string(s.sh->diag[2]);
        }
        run.Add(G, "comp.specials", "Specials", ok, d, "Specials: graphic highlights|Clue scan", "spawn observer installed", d.substr(d.find(": ") == std::string::npos ? 0 : d.find(": ") + 2), "comp.boot");
    }
    {
        wchar_t name[rtx::ipc::kNameChars]; rtx::varc::MakeSectionName(c.pid, name);
        ShareMap<rtx::varc::Share> v;
        int ok = kFail; std::string d = "GONE: variables channel not mapped";
        if (v.open(name) && v.sh->magic == rtx::varc::kMagic && v.sh->version == rtx::varc::kVersion) {
            const std::uint32_t f = v.sh->flags;
            ok = (f & 1) && (f & 2) ? kPass : kFail;
            d = std::string(ok == kPass ? "" : "GONE: ") + ((f & 1) ? "var observers installed" : "var observers missing") + ", " + ((f & 2) ? "drag observer installed" : "drag observer missing");
        }
        run.Add(G, "comp.vars", "Live variables", ok, d, "Live variable changes|Interface drag", "var and drag observers installed", d.substr(d.find(": ") == std::string::npos ? 0 : d.find(": ") + 2), "comp.boot");
    }
    {
        wchar_t name[rtx::ipc::kNameChars]; rtx::netprobe::MakeSectionName(c.pid, name);
        ShareMap<rtx::netprobe::Share> n;
        int ok = kFail; std::string d = "GONE: packet channel not mapped";
        if (n.open(name) && n.sh->magic == rtx::netprobe::kMagic) {
            const bool hooked = (n.sh->flags & 1) != 0;
            ok = hooked ? (n.sh->diag[2] ? kWarn : kPass) : kFail;
            d = std::string(ok == kFail ? "GONE: " : ok == kWarn ? "NEW: " : "") + (hooked ? "framer hooked at " + hx(n.sh->framerRva) : "framer not hooked") + ", " + std::to_string(n.sh->chatSeen) + " chat packets" +
                (n.sh->diag[2] ? ", " + std::to_string(n.sh->diag[2]) + " opcodes above the bound dropped (the bound or the opcode byte moved)" : "");
        }
        run.Add(G, "comp.netprobe", "Packet capture", ok, d, "Chat capture|Events channel|Packet feed", "framer hooked, nothing above the bound", d.substr(d.find(": ") == std::string::npos ? 0 : d.find(": ") + 2), "comp.boot");
    }
    {   // runtime scenery the companion publishes: loc ids on a cache placement of that id and tile
        std::vector<RuntimeObj> objs;
        int ok = kUnchecked, matched = 0, total = 0, rendered = 0; std::string d = "no runtime objects published yet";
        if (ReadRuntimeObjects(c.pid, objs) && !objs.empty()) {
            std::unordered_map<int, std::unordered_set<long long>> sets;
            for (std::size_t i = 0; i < objs.size() && total < 300; ++i) {
                const auto& r = objs[i];
                if (r.config_id <= 0) continue;
                ++total;
                if (r.bmax[0] > r.bmin[0]) ++rendered;
                const int rx = r.x >> 6, ry = r.y >> 6, key = (rx << 8) | ry;
                auto it = sets.find(key);
                if (it == sets.end()) {
                    auto& s = sets[key];
                    for (const auto& pl : rtx::cache::RegionLocations(rx, ry))
                        s.insert(((long long)pl.id << 28) | ((long long)(rx * 64 + pl.x) << 14) | (long long)(ry * 64 + pl.y));
                    it = sets.find(key);
                }
                if (it->second.count(((long long)r.config_id << 28) | ((long long)r.x << 14) | (long long)r.y)) ++matched;
            }
            ok = (total == 0 || matched * 10 >= total) ? kPass : kFail;   // 10 % leaves room for dynamic areas
            d = std::string(ok == kPass ? "" : "MOVED: ") + std::to_string(matched) + "/" + std::to_string(total) + " loc ids sit on a map placement of that id, " +
                std::to_string(rendered) + " with a live model" + (ok == kPass ? "" : " (companion loc id or tile field moved?)");
        }
        run.Add(G, "comp.objects", "Scene objects", ok, d, "Scene objects|Loc labels|Tree timers|Plugins scene API", "10 % on a placement",
                std::to_string(matched) + "/" + std::to_string(total), "comp.boot").need = ok == kUnchecked ? "scenery published (in the world with the companion)" : "";
    }
}

}  // namespace

void SetHealthHeadless(bool headless) { g_healthHeadless.store(headless); }

std::string ReaderHealthJson(std::uint32_t pid, const std::string& runtimePinsJson, const std::string& trigger) {
    static std::mutex runMu;   // one run at a time: the build record and the history are per run
    std::lock_guard<std::mutex> runLock(runMu);
    const ULONGLONG t0 = GetTickCount64();
    rtx::health::Run run;
    HCtx c;
    c.pid = pid;
    c.cli = g_healthHeadless.load();
    c.runtimePins = runtimePinsJson;
    c.trigger = !trigger.empty() ? trigger : c.cli ? "cli" : "manual";
    run.SetTrigger(c.trigger);
    rtx::cache::CacheFullSweepStart();   // the parse rows and opcode counts come from it; it runs while the other groups are read
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_states.find((DWORD)pid);
        if (it != g_states.end()) { c.version = it->second.client_version; c.flavour = it->second.gfx_mode == "Vulkan" ? "vulkan" : it->second.gfx_mode == "OpenGL" ? "opengl" : ""; }
    }
    auto ps = snap_proc(pid);
    if (!ps) {
        run.Add("Context", "context.client", "Game client", kFail, "GONE: not connected (no rs2client.exe with this pid, or MainData not resolved)", "Everything");
        run.Section("context", context_json(c));
        const std::string json = run.Json(c.version, (long long)(GetTickCount64() - t0));
        if (!rtx::pins::Overridden()) rtx::health::SaveHistory(json, "");
        rtx::log::Launcher("health: " + c.trigger + " run without a client (pid " + std::to_string(pid) + ")");
        return json;
    }
    c.h = ps.h; c.mgva = ps.mgva; c.base = ps.mod_base; c.size = ps.mod_size;
    {
        wchar_t path[MAX_PATH] = {}; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(c.h, 0, path, &n)) c.exe = path;
    }
    c.root = rpm<std::uint64_t>(c.h, c.mgva).value_or(0);
    if (c.root > 0x10000) {
        c.status = rpm<std::int8_t>(c.h, c.root + kOffStatus).value_or(-1);
        if (auto p1 = rpm<std::uint64_t>(c.h, c.root + kOffWorld); p1 && *p1 > 0x10000)
            if (auto p2 = rpm<std::uint64_t>(c.h, *p1 + 0x20); p2 && *p2 > 0x10000) c.world = rpm<std::int32_t>(c.h, *p2 + 8).value_or(-1);
        const std::uint64_t pdata = rpm<std::uint64_t>(c.h, c.root + kOffAccount).value_or(0);
        c.localUid = pdata > 0x10000 ? rpm<std::int32_t>(c.h, pdata + rtx::scn::kLocalUid).value_or(-1) : -1;
        const std::uint64_t cont = rpm<std::uint64_t>(c.h, c.root + rtx::scn::kContainer).value_or(0);
        const int idx = cont > 0x10000 ? rpm<std::int32_t>(c.h, cont + rtx::scn::kActiveIdx).value_or(-1) : -1;
        const std::uint64_t arr = cont > 0x10000 ? rpm<std::uint64_t>(c.h, cont + rtx::scn::kEntryArr).value_or(0) : 0;
        c.wv = (idx >= 0 && arr > 0x10000) ? rpm<std::uint64_t>(c.h, arr + (std::uint64_t)idx * 0x10 + rtx::scn::kEntryWv).value_or(0) : 0;
        if (c.wv > 0x10000) { CamProbes probes; auto w = scene_worker(c.h, pid, c.wv, &probes); c.worker = w ? *w : 0; }
        if (c.localUid >= 0) c.localSec = local_player_sec_fast(c.h, c.root, c.localUid);
        if (!c.localSec && c.worker) {
            std::vector<std::pair<std::uint64_t, std::uint64_t>> secs; scene_secs(c, secs);
            for (const auto& es : secs)
                if (rpm<std::uint8_t>(c.h, es.second + rtx::scn::kType).value_or(0xFF) == 2 && rpm<std::int32_t>(c.h, es.second + rtx::scn::kUid).value_or(-1) == c.localUid) { c.localSec = es.second; break; }
        }
    }
    c.inWorld = c.status == 30 && c.localUid >= 0;

    health_context(c, run);
    health_build(c, run);
    rtx::codescan::Check(c.exe, run);
    health_calibration(c, run);
    if (c.root > 0x10000) {
        health_data(c, run);
        health_player(c, run);
        health_scene(c, run);
        health_interfaces(c, run);
        health_packets(c, run);
    } else {
        // the anchor is found and the root is empty: the lobby or a load, not a moved layout
        const bool anchored = run.StatusOf("code.sig.main-anchor") == kPass;
        run.Add("Game data", "data.root", "Root and status", anchored ? kUnchecked : kFail,
                anchored ? "MainData root not set (lobby or loading)" : "GONE: MainData root not readable", "Everything", "", "", "code.sig.main-anchor").need = anchored ? "log in" : "";
    }
    {
        run.Add("Content", "content.cache", "Cache open", rtx::cache::IndexInfo(2).open ? kPass : kFail,
                rtx::cache::IndexInfo(2).open ? rtx::cache::CacheRoot() : "GONE: config index not readable", "Every cache read");
        rtx::pins::CheckContent(run, c.runtimePins, c.cli ? 15000 : 0);
        if (c.root > 0x10000) health_dailies(c, run);
    }
    if (c.root > 0x10000) health_live(c, run);
    health_cache(c, run);
    health_companion(c, run);

    // complete: every group judged a logged-in client with its companion
    c.complete = c.inWorld && c.bootFound && c.root > 0x10000;
    run.SetComplete(c.complete);
    run.Section("context", context_json(c));

    const long long ms = (long long)(GetTickCount64() - t0);
    char st[16]; std::snprintf(st, sizeof(st), "%08x", c.stamp);
    const std::string json = run.Json(c.version, ms);
    if (!rtx::pins::Overridden()) {   // a test manifest: not a verdict on this build
        rtx::health::SaveHistory(json, st);
        rtx::health::BuildsRecord(c.version, st, c.flavour, g_buildRevs, run.Count(kFail), g_buildCarried, c.complete);
    }
    rtx::log::Launcher("health: " + c.trigger + " " + st + " " + run.ImpactLine() + " (" + std::to_string(ms) + " ms)");
    return json;
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
