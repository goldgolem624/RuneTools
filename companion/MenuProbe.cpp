// Right-click menu probe rvas: string-init 0x161C10, clear(bool) 0x164880, add 0x160AB0, finalize 0x1661E0, item decoder 0x2DCDF0. Poll() reads on the companion thread while ApplyOrder writes on the game thread, so lane reads can tear.
// mgr+0x90/+0x98 entries: 16-byte {action object*, target string*}, REVERSE display order. mgr+0x1380/+0x1388 stride 0x2E8 hover-target list; subset lanes +0x0d30 all-but-Cancel, +0x06e0 targeted; hover block +0x000 = entity handle ((x<<16)|y world object, index actor). Action object: +0x08 refcount, +0x0c = 1, +0x20 EASTL target, +0x38 EASTL verb.

#include "SceneOffsets.h"
#include "MenuProbe.h"
#include "MenuShare.h"

#include <windows.h>
#include <detours.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace rtx::menuprobe {
namespace {

typedef void(__fastcall* Init_t)(std::uint64_t);
typedef void(__fastcall* Clear_t)(std::uint64_t, char);

Init_t        g_origInit  = nullptr;
Clear_t       g_origClear = nullptr;
bool          g_installed = false;
std::uint64_t g_base      = 0;
volatile std::uint64_t g_mgr = 0;
std::uint32_t g_lastSig   = 0;
int           g_dumps     = 0;

constexpr int kMaxDumps = 12;

constexpr std::uint64_t kTargetStride = 0x2E8;
struct Vec { std::uint64_t begin, end; std::uint64_t stride; const char* name; };
const Vec kVecs[] = {
    { 0x1380, 0x1388, kTargetStride, "targets" },
    { 0x0090, 0x0098, 0,     "v_0x90"   },
    { 0x03B8, 0x03C0, 0,     "v_0x3b8"  },
    { 0x06E0, 0x06E8, 0,     "v_0x6e0"  },
    { 0x0A08, 0x0A10, 0,     "v_0xa08"  },
    { 0x0D30, 0x0D38, 0,     "v_0xd30"  },
    { 0x1058, 0x1060, 0,     "v_0x1058" },
    { 0x13A0, 0x13A8, 0,     "v_0x13a0" },
    { 0x1468, 0x1470, 0,     "v_0x1468" },
    { 0x1790, 0x1798, 0,     "v_0x1790" },
};

std::mutex g_logMu;
void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    char path[MAX_PATH] = "C:\\rtx_menu.log";
    char home[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
    if (n > 0 && n < sizeof(home)) std::snprintf(path, sizeof(path), "%s\\rtx_menu.log", home);
    FILE* f = nullptr;
    // Bounded: start over once the file passes 4 MB so a long session cannot fill the profile.
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) &&
            (((std::uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) > 4ull * 1024 * 1024)
            DeleteFileA(path);
    }
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    std::fprintf(f, "%s\n", msg);
    std::fclose(f);
}

// String-init: +0x19b10 language-index load (+0x19ad0 through 949-5), stores into mgr+0x10..0x38.
const unsigned char kInit[] = {
    0x40,0x57, 0x48,0x83,0xEC,0x30, 0x48,0x8B,0x41,0x08, 0x48,0x8B,0xF9,
    0x48,0x8D,0x0D,0,0,0,0,
    0x48,0x89,0x5C,0x24,0x40, 0x48,0x8D,0x99,0,0,0,0,
    0x48,0x8B,0x90,0x10,0x9B,0x01,0x00, 0x48,0x63,0x02,
    0x48,0x8D,0x14,0xC5,0x00,0x00,0x00,0x00,
    0x48,0x8B,0x84,0x0A,0,0,0,0, 0x48,0x03,0xDA, 0x48,0x89,0x47,0x10,
    0x48,0x8B,0x03, 0x48,0x89,0x47,0x18,
    0x48,0x8B,0x84,0x0A,0,0,0,0, 0x48,0x89,0x47,0x20,
    0x48,0x8B,0x84,0x0A,0,0,0,0, 0x48,0x89,0x47,0x28,
    0x48,0x8B,0x84,0x0A,0,0,0,0, 0x48,0x89,0x47,0x30,
    0x48,0x8B,0x84,0x0A,0,0,0,0, 0x48,0x89,0x47,0x38,
};
const unsigned char kInitMask[] = {
    1,1, 1,1,1,1, 1,1,1,1, 1,1,1,
    1,1,1,0,0,0,0,
    1,1,1,1,1, 1,1,1,0,0,0,0,
    1,1,1,1,1,1,1, 1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1, 1,1,1,1,
    1,1,1, 1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1,1,
};
static_assert(sizeof(kInit) == sizeof(kInitMask), "init pattern/mask length mismatch");

// clear(): self-validating, contains [rcx+0x1388] end, [rcx+0x1380] begin, add 0x2E8 stride.
const unsigned char kClear[] = {
    0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18,
    0x57, 0x48,0x83,0xEC,0x20, 0xC6,0x41,0x68,0x00, 0x0F,0xB6,0xEA,
    0x48,0x8B,0xB1,0x88,0x13,0x00,0x00,          // mov rsi,[rcx+0x1388]  END
    0x48,0x8B,0xF9,
    0x48,0x8B,0x99,0x80,0x13,0x00,0x00,          // mov rbx,[rcx+0x1380]  BEGIN
    0x48,0x3B,0xDE, 0x74,0x19,
    0x48,0x8B,0x53,0x18, 0x48,0x8D,0x4B,0x08, 0xE8,0,0,0,0,
    0x48,0x81,0xC3,0xE8,0x02,0x00,0x00,          // add rbx,0x2E8         STRIDE
    0x48,0x3B,0xDE, 0x75,0xE7,
    0x48,0x8B,0x87,0x80,0x13,0x00,0x00, 0x48,0x89,0x87,0x88,0x13,0x00,0x00,
    0x48,0x8B,0x97,0xA8,0x13,0x00,0x00, 0x48,0x8B,0x8F,0xA0,0x13,0x00,0x00,
    0xE8,0,0,0,0,
    0x48,0x8B,0x87,0xA0,0x13,0x00,0x00, 0x48,0x89,0x87,0xA8,0x13,0x00,0x00,
};
const unsigned char kClearMask[] = {
    1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1,
    1, 1,1,1,1, 1,1,1,1, 1,1,1,
    1,1,1,1,1,1,1,
    1,1,1,
    1,1,1,1,1,1,1,
    1,1,1, 1,1,
    1,1,1,1, 1,1,1,1, 1,0,0,0,0,
    1,1,1,1,1,1,1,
    1,1,1, 1,1,
    1,1,1,1,1,1,1, 1,1,1,1,1,1,1,
    1,1,1,1,1,1,1, 1,1,1,1,1,1,1,
    1,0,0,0,0,
    1,1,1,1,1,1,1, 1,1,1,1,1,1,1,
};
static_assert(sizeof(kClear) == sizeof(kClearMask), "clear pattern/mask length mismatch");

// Per-tick menu builder: the clear() caller touching +0x90/+0x98 (not the 0x160AB0 caller).
const unsigned char kBuild[] = {
    0x44,0x88,0x4C,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xC8,0x48,0x81,0xEC,0x38,0x01,
    0x00,0x00,0x48,0x8B,0xF9,0xE8,0xE2,0xBF,0x5A,0x00,0x48,0x8B,0xC8,0xE8,0xAA,0xC5,0x5A,0x00,
    0x48,0x8B,0x47,0x08,0x4C,0x8B,0x3D,0x6F,0x48,0xBF,0x00,0x4C,0x8B,0xA0,0x98,0x98,0x01,0x00,
    0x4C,0x8B,0xA8,0x68,0x8D,0x01,0x00,0x48,0x8B,0xB0,0x58,0x8D,0x01,0x00,0x48,0x8B,0x98,0x80,
    0x8D,0x01,0x00,0x4C,0x89,0xA5,0x80,0x00,0x00,0x00,0x4C,0x89,0x6C,0x24,0x30,0xE8,0x42,0xC4,
    0x71,0x00,0x4C,0x8B,0xF0,0xE8,0x1E,0xC4,0x71,0x00,0x49,0x81,0xFE,0x80,0x96,0x98,0x00,
};
const unsigned char kBuildMask[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,0,0,0,0,
};
static_assert(sizeof(kBuild) == sizeof(kBuildMask), "build pattern/mask length mismatch");

// Snapshot FUN_14012d660 (rva 0x12d660, rcx = manager): copies the last +0x90 record into mgr+0x13f0, second-from-top into 0x13e0.
const unsigned char kSnap[] = {
    0x48,0x89,0x4C,0x24,0x08, 0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0xAC,0x24,0,0,0,0, 0x48,0x81,0xEC,0,0,0,0,
    0x48,0xFF,0x81,0,0,0,0, 0x48,0x8D,0xB1,0,0,0,0, 0x48,0x8B,0xF9,
    0x4C,0x8D,0xB6,0,0,0,0,
};
const unsigned char kSnapMask[] = {
    1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1,0,0,0,0,
    1,1,1,0,0,0,0, 1,1,1,0,0,0,0, 1,1,1,
    1,1,1,0,0,0,0,
};
static_assert(sizeof(kSnap) == sizeof(kSnapMask), "snapshot pattern/mask length mismatch");

std::uint64_t Scan(const unsigned char* pat, const unsigned char* mask, std::size_t n) {
    auto dos = (const IMAGE_DOS_HEADER*)g_base;
    auto nt  = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    std::uint64_t tb = 0, ts = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s)
        if (sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            tb = g_base + sec[s].VirtualAddress;
            ts = sec[s].Misc.VirtualSize;
            break;
        }
    if (!tb || !ts) return 0;
    __try {
        const unsigned char* b = (const unsigned char*)tb;
        std::uint64_t hit = 0;
        for (std::uint64_t i = 0; i + n < ts; ++i) {
            if (b[i] != pat[0]) continue;
            bool ok = true;
            for (std::size_t j = 1; j < n; ++j)
                if (mask[j] && b[i + j] != pat[j]) { ok = false; break; }
            if (!ok) continue;
            if (hit) return 0;   // ambiguous: refuse
            hit = tb + i;
        }
        return hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

void ScanHoverSlotRefs() {
    auto dos = (const IMAGE_DOS_HEADER*)g_base;
    auto nt  = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    std::uint64_t tb = 0, ts = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            tb = g_base + sec[i].VirtualAddress;
            ts = sec[i].Misc.VirtualSize;
            break;
        }
    if (!tb || !ts) return;

    int hits = 0;
    __try {
        const unsigned char* b = (const unsigned char*)tb;
        for (std::uint64_t i = 4; i + 8 < ts && hits < 40; ++i) {
            if (b[i+2] != 0x00 || b[i+3] != 0x00) continue;
            const unsigned int disp = (unsigned int)b[i] | ((unsigned int)b[i+1] << 8);
            if (disp < 0x13C0 || disp > 0x1420) continue;
            const unsigned char modrm = b[i-1];
            if ((modrm & 0xC0) != 0x80) continue;
            char pre[64];
            int at = 0;
            for (int k = -6; k <= 7; ++k)
                at += std::snprintf(pre + at, sizeof(pre) - at, "%02x ", b[i + k]);
            Log("  [slotref] disp 0x%04x  rva 0x%llx   %s", disp,
                (unsigned long long)(tb + i - 3 - g_base), pre);
            ++hits;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("  [slotref] %d instruction(s) reference +0x13F8", hits);
}

bool Rd(std::uint64_t a, void* out, std::size_t n) {
    __try { std::memcpy(out, (const void*)a, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Printable(const char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if ((unsigned char)p[i] < 0x20 || (unsigned char)p[i] > 0x7E) return false;
    return true;
}

// EASTL SSO string: 24 bytes, last byte = remaining capacity (0x17 empty), high bit = heap.
int ReadEastl(std::uint64_t s, char* out, std::size_t cap, bool* heap) {
    unsigned char raw[0x18];
    if (!Rd(s, raw, sizeof(raw))) return -1;
    const unsigned char flag = raw[0x17];
    if (!(flag & 0x80)) {
        if (flag > 0x17) return -1;
        const int len = 0x17 - flag;
        if (len <= 0 || (std::size_t)len >= cap) return -1;
        if (!Printable((const char*)raw, (std::size_t)len)) return -1;
        if (raw[len] != 0) return -1;
        std::memcpy(out, raw, (std::size_t)len);
        out[len] = 0;
        *heap = false;
        return len;
    }
    std::uint64_t ptr = 0, size = 0;
    std::memcpy(&ptr, raw, 8);
    std::memcpy(&size, raw + 8, 8);
    if (!ptr || size == 0 || size > 300 || size >= cap) return -1;
    if (!Rd(ptr, out, (std::size_t)size)) return -1;
    if (!Printable(out, (std::size_t)size)) return -1;
    out[size] = 0;
    *heap = true;
    return (int)size;
}

rtx::menu::Share* g_share = nullptr;

constexpr std::uint64_t kEntriesBegin = 0x90;
constexpr std::uint64_t kEntriesEnd   = 0x98;
constexpr std::uint64_t kRecSize      = 16;
// Read the record's own target (+8); the action object's copy at +0x20 diverges when menus merge.
constexpr std::uint64_t kRecTarget    = 8;
constexpr std::uint64_t kObjTarget    = 0x20;
constexpr std::uint64_t kObjVerb      = 0x38;

// Record qword 1 = display object: target +0x00, verb +0x18, op index +0x50, class tag +0x38 (priority at tag+0x40); priority < 1000 rows are re-inserted at top each tick.
constexpr std::uint64_t kDispTag  = 0x38;
constexpr std::uint64_t kTagPrio  = 0x40;
constexpr std::int32_t  kPromoted = 1000;
constexpr std::int32_t  kDemotedIface = 1007;   // interface demoted class: the only one with a proven promoted partner

bool EntryTag(std::uint64_t rec, std::uint64_t& tag, std::int32_t& prio);

rtx::menu::Share* MapShare() {
    wchar_t name[64];
    rtx::menu::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  (DWORD)sizeof(rtx::menu::Share), name);
    if (!h) return nullptr;
    return (rtx::menu::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0,
                                            sizeof(rtx::menu::Share));
}

bool EntryVec(std::uint64_t mgr, std::uint64_t& begin, std::uint64_t& count) {
    std::uint64_t e = 0;
    if (!Rd(mgr + kEntriesBegin, &begin, 8) || !Rd(mgr + kEntriesEnd, &e, 8)) return false;
    if (!begin || e < begin) return false;
    const std::uint64_t span = e - begin;
    if (span % kRecSize || span > kRecSize * rtx::menu::kMaxEntries) return false;
    count = span / kRecSize;
    return count > 0;
}

void HoverDump(std::uint64_t mgr) {
    std::uint64_t b = 0, e = 0;
    if (!Rd(mgr + 0x1380, &b, 8) || !Rd(mgr + 0x1388, &e, 8)) return;
    if (!b || e <= b || (e - b) % kTargetStride) return;
    const std::uint64_t n = (e - b) / kTargetStride;

    for (std::uint64_t t = 0; t < n && t < 4; ++t) {
        const std::uint64_t rec = b + t * kTargetStride;
        char nm[288];
        bool hp = false;
        if (ReadEastl(rec + 0x80, nm, sizeof(nm), &hp) <= 0) nm[0] = 0;
        Log("  -- hover target %llu/%llu @0x%llx  \"%s\" --",
            (unsigned long long)(t + 1), (unsigned long long)n,
            (unsigned long long)rec, nm);
        for (std::uint64_t o = 0; o < kTargetStride; o += 32) {
            std::uint32_t w[8] = { 0 };
            const std::uint64_t take = (kTargetStride - o) < 32 ? (kTargetStride - o) : 32;
            if (!Rd(rec + o, w, (std::size_t)take)) continue;
            if (o >= 0x80 && o < 0x98) continue;
            char line[128];
            int at = 0;
            for (std::uint64_t k = 0; k < take / 4 && at < (int)sizeof(line) - 10; ++k)
                at += std::snprintf(line + at, sizeof(line) - at, "%08x ", w[k]);
            Log("      +%03llx  %s", (unsigned long long)o, line);
        }
    }
}

void Publish(std::uint64_t mgr) {
    std::uint64_t begin = 0, count = 0;
    if (!EntryVec(mgr, begin, count)) return;      // menu closed: keep what was last captured

    rtx::menu::Entry stage[rtx::menu::kMaxEntries];
    char buf[288];
    std::uint32_t out = 0;
    for (std::uint64_t i = 0; i < count && out < (std::uint32_t)rtx::menu::kMaxEntries; ++i) {
        const std::uint64_t rec = begin + (count - 1 - i) * kRecSize;    // reverse
        std::uint64_t obj = 0;
        if (!Rd(rec, &obj, 8) || !obj) continue;
        std::uint32_t hdr[4] = { 0, 0, 0, 0 };
        Rd(obj, hdr, sizeof(hdr));
        if (!hdr[2]) continue;                      // refcount 0 = mid-teardown, not a live entry
        rtx::menu::Entry& en = stage[out];
        en.verb[0] = 0; en.target[0] = 0;
        en.refs = hdr[2];
        en.slot = (std::int32_t)(count - 1 - i);
        en.type = -1;
        {   // class ordinal = entity type
            std::uint64_t tag = 0;
            std::int32_t  prio = 0;
            if (EntryTag(rec, tag, prio)) Rd(tag + 0x44, &en.type, 4);
        }
        bool hp = false;
        if (ReadEastl(obj + kObjVerb, buf, sizeof(buf), &hp) > 0) {
            std::strncpy(en.verb, buf, rtx::menu::kVerbLen - 1);
            en.verb[rtx::menu::kVerbLen - 1] = 0;
        }
        std::uint64_t tstr = 0;
        if (Rd(rec + kRecTarget, &tstr, 8) && tstr &&
            ReadEastl(tstr, buf, sizeof(buf), &hp) > 0) {
            std::strncpy(en.target, buf, rtx::menu::kTargetLen - 1);
            en.target[rtx::menu::kTargetLen - 1] = 0;
        }
        ++out;
    }
    if (!out) return;                              // nothing readable: keep the last capture
    std::uint32_t targeted = 0;
    const char* first = nullptr;
    for (std::uint32_t k = 0; k < out; ++k)
        if (stage[k].target[0]) {
            ++targeted;
            if (!first) first = stage[k].target;
        }
    if (!targeted) return;                          // empty ground: Cancel + Walk here only

    const bool sameObject = first && g_share->lastTarget[0] &&
                            std::strncmp(first, g_share->lastTarget, rtx::menu::kTargetLen) == 0;
    if (sameObject && targeted < g_share->lastTargeted) return;

    {
        std::uint64_t tb = 0, te = 0;
        std::uint32_t h = 0;
        if (Rd(mgr + 0x1380, &tb, 8) && Rd(mgr + 0x1388, &te, 8) && tb && te > tb) {
            Rd(tb, &h, 4);
        }
        g_share->handle = h;
    }
    std::memcpy(g_share->entries, stage, sizeof(rtx::menu::Entry) * out);
    std::strncpy(g_share->lastTarget, first, rtx::menu::kTargetLen - 1);
    g_share->lastTarget[rtx::menu::kTargetLen - 1] = 0;
    g_share->lastTargeted = targeted;
    g_share->count = out;
    g_share->seq = g_share->seq + 1;
    ++g_share->diag[0];
}

void StripTags(const char* in, char* out) {
    std::size_t o = 0;
    bool skip = false;
    for (std::size_t i = 0; in[i] && o + 1 < (std::size_t)rtx::menu::kTargetLen; ++i) {
        if (in[i] == '<') { skip = true; continue; }
        if (in[i] == '>') { skip = false; continue; }
        if (!skip) out[o++] = in[i];
    }
    out[o] = 0;
}

// Reorder: permuting whole 16-byte records is refcount-neutral. "Top of menu" = end of array.
struct Lane { std::uint64_t begin, end; int stat; const char* name; };
// Permute only the drawn menu (+0x90) and +0x3b8, which FUN_14012d660 copies over +0x90 wholesale for interface/inventory menus.
// Never the subset lanes (+0x6e0, +0xa08, +0xd30, +0x13a0): permuting them independently desyncs draw from dispatch (row i clicks row i+1).
const Lane kLanes[] = {
    { 0x0090, 0x0098, 0, "menu"  },
    { 0x03B8, 0x03C0, 4, "iface" },
};

bool RankLane(std::uint64_t begin, int n, unsigned char recs[][kRecSize], int* rank,
              bool* fixedSlot, int* decoded) {
    *decoded = 0;
    bool anyTargeted = false;
    for (int k = 0; k < n && !anyTargeted; ++k) {
        std::uint64_t t = 0;
        if (!Rd(begin + (std::uint64_t)k * kRecSize + kRecTarget, &t, 8) || !t) continue;
        char probe[288];
        bool hp2 = false;
        if (ReadEastl(t, probe, sizeof(probe), &hp2) > 0) {
            char plain[288];
            StripTags(probe, plain);
            if (plain[0]) anyTargeted = true;
        }
    }
    char verb[288], raw[288], tgt[rtx::menu::kTargetLen];
    static rtx::menu::Pin pinsLocal[rtx::menu::kMaxPins];
    std::uint32_t pins = 0;
    for (int tries = 0; tries < 8; ++tries) {
        const std::uint32_t s0 = g_share->pinSeq;
        if (s0 & 1u) { YieldProcessor(); continue; }
        MemoryBarrier();
        std::uint32_t c = g_share->pinCount;
        if (c > (std::uint32_t)rtx::menu::kMaxPins) c = (std::uint32_t)rtx::menu::kMaxPins;
        std::memcpy(pinsLocal, g_share->pins, (std::size_t)c * sizeof(rtx::menu::Pin));
        pins = c;
        MemoryBarrier();
        if (g_share->pinSeq == s0) break;
    }
    for (int i = 0; i < n; ++i) {
        if (!Rd(begin + (std::uint64_t)i * kRecSize, recs[i], kRecSize)) return false;
        std::uint64_t obj = 0;
        std::memcpy(&obj, recs[i], 8);
        rank[i] = 0x7FFFFFFF;
        fixedSlot[i] = false;
        bool hp = false;
        if (!obj || ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) <= 0) continue;
        ++*decoded;
        std::strncpy(g_share->lastVerb, verb, rtx::menu::kVerbLen - 1);
        g_share->lastVerb[rtx::menu::kVerbLen - 1] = 0;
        tgt[0] = 0;
        std::uint64_t tstr = 0;
        std::memcpy(&tstr, recs[i] + kRecTarget, 8);     // already copied out with the record
        if (tstr && ReadEastl(tstr, raw, sizeof(raw), &hp) > 0) StripTags(raw, tgt);
        if (i == 0 || (anyTargeted && !tgt[0])) { fixedSlot[i] = true; continue; }
        for (std::uint32_t p = 0; p < pins; ++p) {
            if (std::strncmp(verb, pinsLocal[p].verb, rtx::menu::kVerbLen) != 0) continue;
            if (pinsLocal[p].target[0] &&
                std::strncmp(tgt, pinsLocal[p].target, rtx::menu::kTargetLen) != 0) continue;
            rank[i] = (int)p;
            ++g_share->stage[1];
            break;
        }
    }
    {
        bool pinMatched[rtx::menu::kMaxPins] = {};
        for (int i = 0; i < n; ++i)
            if (rank[i] != 0x7FFFFFFF) pinMatched[rank[i]] = true;
        for (int i = 0; i < n; ++i) {
            if (rank[i] == 0x7FFFFFFF) continue;
            const char* rt = pinsLocal[rank[i]].target;
            int first = -1;
            for (std::uint32_t p = 0; p < pins; ++p)
                if (std::strncmp(rt, pinsLocal[p].target, rtx::menu::kTargetLen) == 0) { first = (int)p; break; }
            if (first >= 0 && !pinMatched[first]) rank[i] = 0x7FFFFFFF;
        }
    }
    return true;
}

int LaneCount(std::uint64_t mgr, const Vec& v, std::uint64_t& begin) {
    std::uint64_t e = 0;
    if (!Rd(mgr + v.begin, &begin, 8) || !Rd(mgr + v.end, &e, 8)) return 0;
    if (!begin || e <= begin) return 0;
    const std::uint64_t span = e - begin;
    if (span % kRecSize || span > kRecSize * rtx::menu::kMaxEntries) return 0;
    return (int)(span / kRecSize);
}

// Never write the hover slot mgr+0x13F8 directly: the snapshot rewrites 0x13f0/0x13f8 as a pair,
// and reordering before it runs is sufficient.
void ApplyOrder(std::uint64_t mgr) {
    if (!g_share) return;
    ++g_share->diag[2];
    if (!g_share->pinCount) return;
    ++g_share->diag[3];

    unsigned char recs[rtx::menu::kMaxEntries][kRecSize];
    int  rank[rtx::menu::kMaxEntries];
    bool fixedSlot[rtx::menu::kMaxEntries];
    int  slots[rtx::menu::kMaxEntries];
    int  items[rtx::menu::kMaxEntries];

    for (const Lane& lane : kLanes) {
        std::uint64_t begin = 0, e = 0;
        if (!Rd(mgr + lane.begin, &begin, 8) || !Rd(mgr + lane.end, &e, 8)) continue;
        if (!begin || e < begin) continue;
        const std::uint64_t span = e - begin;
        if (span % kRecSize || span > kRecSize * rtx::menu::kMaxEntries) continue;
        const int n = (int)(span / kRecSize);
        if (n < 2) continue;
        if (lane.stat == 0) ++g_share->stage[0];
        int decoded = 0;
        if (!RankLane(begin, n, recs, rank, fixedSlot, &decoded)) continue;

        int m = 0;
        for (int d = 0; d < n; ++d) {
            const int idx = n - 1 - d;
            if (!fixedSlot[idx]) slots[m++] = idx;
        }
        int out = 0;
        for (int pass = 0; pass < 2; ++pass) {
            for (int j = 0; j < m; ++j) {
                const int idx = slots[j];
                const bool ranked = rank[idx] != 0x7FFFFFFF;
                if ((pass == 0) != ranked) continue;
                items[out++] = idx;
            }
            if (pass == 0 && out > 1)
                for (int a = 0; a < out - 1; ++a)
                    for (int b2 = a + 1; b2 < out; ++b2)
                        if (rank[items[b2]] < rank[items[a]]) {
                            const int t = items[a]; items[a] = items[b2]; items[b2] = t;
                        }
        }
        if (out != m) continue;
        if (lane.stat == 0) ++g_share->stage[2];

        bool changed = false;
        for (int j = 0; j < m; ++j) if (items[j] != slots[j]) { changed = true; break; }
        if (!changed) continue;
        if (lane.stat == 0) ++g_share->stage[3];

        // Structural check instead of an exe-stamp allow-list: the write is a permutation of whole
        // records that were just read, so it is safe exactly when every record in the lane decoded
        // as a menu entry (object pointer -> readable verb). That holds on the OpenGL and Vulkan
        // clients alike and survives rebuilds; a layout change fails it and nothing is written.
        if (decoded != n) { g_share->flags |= rtx::menu::kFlagUnverified; continue; }
        __try {
            for (int j = 0; j < m; ++j)
                std::memcpy((void*)(begin + (std::uint64_t)slots[j] * kRecSize), recs[items[j]], kRecSize);
            ++g_share->lane[lane.stat];             // which lanes actually moved
            if (lane.stat == 0) ++g_share->diag[1];
            g_share->flags &= ~rtx::menu::kFlagUnverified;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

}

bool EntryTag(std::uint64_t rec, std::uint64_t& tag, std::int32_t& prio) {
    std::uint64_t q1 = 0;
    tag = 0; prio = 0;
    if (!Rd(rec + kRecTarget, &q1, 8) || !q1) return false;
    if (!Rd(q1 + kDispTag, &tag, 8) || !tag) return false;
    return Rd(tag + kTagPrio, &prio, 4);
}

// Gated to the interface demoted class (1007); world demoted classes (1002/1003) have no proven
// kDemotedIface is declared with kPromoted above.

struct ClassObs {
    std::uint64_t cls;
    std::int32_t  ord;
    std::int32_t  prio;
    std::uint32_t verbHash[4];
    int           verbs;
};
ClassObs g_obs[24];
int      g_obsN = 0;

std::uint32_t VerbHash(const char* s) {
    std::uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

void NoteClass(std::uint64_t cls, std::int32_t ord, std::int32_t prio, const char* verb) {
    if (!cls || !verb || !verb[0] || prio >= kPromoted) return;   // only promoted classes matter
    const std::uint32_t h = VerbHash(verb);
    for (int i = 0; i < g_obsN; ++i) {
        if (g_obs[i].cls != cls) continue;
        for (int v = 0; v < g_obs[i].verbs; ++v)
            if (g_obs[i].verbHash[v] == h) return;                // already counted this verb
        if (g_obs[i].verbs < 4) g_obs[i].verbHash[g_obs[i].verbs++] = h;
        return;
    }
    if (g_obsN >= (int)(sizeof(g_obs) / sizeof(g_obs[0]))) return;
    ClassObs& o = g_obs[g_obsN++];
    o.cls = cls; o.ord = ord; o.prio = prio;
    o.verbHash[0] = h; o.verbs = 1;
}

std::uint64_t LearnedGeneric(std::int32_t ord) {
    std::uint64_t best = 0;
    int bestVerbs = 1;                       // a single-verb class is action-specific: never use it
    for (int i = 0; i < g_obsN; ++i)
        if (g_obs[i].ord == ord && g_obs[i].verbs > bestVerbs) {
            bestVerbs = g_obs[i].verbs;
            best = g_obs[i].cls;
        }
    return best;
}

std::uint64_t g_modEnd = 0;

std::uint64_t ScanCounterpart(std::uint64_t demoted, std::int32_t ord, std::int32_t& outPrio) {
    outPrio = 0;
    std::uint64_t vt = 0;
    if (!Rd(demoted, &vt, 8) || vt <= g_base || vt >= g_modEnd) return 0;
    std::uint64_t best = 0;
    std::int32_t  bestPrio = 0;
    // Class singletons sit in one .data array per entity type, 0x50-0x60 stride.
    for (std::int64_t off = -0x800; off <= 0x800; off += 0x10) {
        if (off == 0) continue;
        const std::uint64_t c = (std::uint64_t)((std::int64_t)demoted + off);
        std::uint64_t cvt = 0;
        std::int32_t  p = 0, o = -1;
        if (!Rd(c, &cvt, 8) || cvt != vt) continue;            // same concrete action type
        if (!Rd(c + kTagPrio, &p, 4) || p <= 0 || p >= kPromoted) continue;
        if (!Rd(c + 0x44, &o, 4) || o != ord) continue;        // same entity type
        if (p > bestPrio) { bestPrio = p; best = c; }
    }
    outPrio = bestPrio;
    return best;
}

void PromotePinnedEntry(std::uint64_t mgr) {
    if (!g_share) return;
    g_share->promoState = rtx::menu::kPromoIdle;
    g_share->promoPrio = 0;
    g_share->promoPartnerPrio = 0;
    g_share->promoSource = rtx::menu::kPromoSrcNone;
    g_share->promoVerb[0] = 0;
    if (!g_share->pinCount) return;
    std::uint64_t begin = 0, count = 0;
    if (!EntryVec(mgr, begin, count) || count < 2) return;

    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint64_t rec = begin + i * kRecSize;
        std::uint64_t tag = 0, obj = 0;
        std::int32_t  prio = 0, ord = -1;
        if (!EntryTag(rec, tag, prio)) continue;
        Rd(tag + 0x44, &ord, 4);
        char verb[rtx::menu::kVerbLen];
        bool hp = false;
        if (Rd(rec, &obj, 8) && obj && ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) > 0)
            NoteClass(tag, ord, prio, verb);
    }

    const std::uint64_t top = begin + (count - 1) * kRecSize;
    std::uint64_t topTag = 0;
    std::int32_t  topPrio = 0;
    if (!EntryTag(top, topTag, topPrio)) return;
    g_share->promoPrio = topPrio;
    {
        std::uint64_t obj = 0;
        char verb[rtx::menu::kVerbLen];
        bool hp = false;
        if (Rd(top, &obj, 8) && obj && ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) > 0) {
            std::strncpy(g_share->promoVerb, verb, rtx::menu::kVerbLen - 1);
            g_share->promoVerb[rtx::menu::kVerbLen - 1] = 0;
        }
    }
    if (topPrio < kPromoted) {
        g_share->promoState = rtx::menu::kPromoNotNeeded;
        return;
    }
    if (topPrio != kDemotedIface) {              // a demoted class we have not proven a partner for
        g_share->promoState = rtx::menu::kPromoOtherClass;
        return;
    }

    std::int32_t topOrd = -1;
    Rd(topTag + 0x44, &topOrd, 4);
    std::int32_t  candPrio = 0;
    std::uint64_t promoted = ScanCounterpart(topTag, topOrd, candPrio);
    g_share->promoSource = promoted ? rtx::menu::kPromoSrcStructural : rtx::menu::kPromoSrcNone;
    const std::uint64_t learned = LearnedGeneric(topOrd);
    if (learned && learned != promoted) {
        promoted = learned;
        Rd(promoted + kTagPrio, &candPrio, 4);
        g_share->promoSource = rtx::menu::kPromoSrcLearned;
    }
    g_share->promoPartnerPrio = candPrio;
    if (!promoted) {
        g_share->promoSource = rtx::menu::kPromoSrcNone;
        g_share->promoState = rtx::menu::kPromoNoPartner;
        return;
    }

    std::uint64_t disp = 0;
    if (!Rd(top + kRecTarget, &disp, 8) || !disp) {
        g_share->promoState = rtx::menu::kPromoWriteFailed;
        return;
    }
    {   // Structural check instead of the exe stamp: the slot we overwrite must hold the very tag we
        // read for this row, and the replacement must be a class object of the same concrete type.
        std::uint64_t cur = 0, vtTop = 0, vtNew = 0;
        if (!Rd(disp + kDispTag, &cur, 8) || cur != topTag ||
            !Rd(topTag, &vtTop, 8) || !Rd(promoted, &vtNew, 8) || vtTop != vtNew ||
            vtNew <= g_base || vtNew >= g_modEnd) {
            g_share->promoState = rtx::menu::kPromoUnverified;
            return;
        }
    }
    __try {
        *(volatile std::uint64_t*)(disp + kDispTag) = promoted;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_share->promoState = rtx::menu::kPromoWriteFailed;
        return;
    }
    g_share->promoState = rtx::menu::kPromoApplied;
    ++g_share->lane[5];
}

// Never write the class PRIORITY (+0x40) instead: the tag is shared by several verbs game-wide,

int g_laneLog = 0;                        // budget, refilled when the panel is opened

std::uint64_t LaneSig(std::uint64_t mgr) {
    std::uint64_t begin = 0;
    const int n = LaneCount(mgr, kVecs[1], begin);
    std::uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; ++i) {
        std::uint64_t obj = 0;
        Rd(begin + (std::uint64_t)i * kRecSize, &obj, 8);
        h = (h ^ obj) * 1099511628211ull;
    }
    return h;
}

void LogLaneTops(std::uint64_t mgr, const char* when) {
    if (g_laneLog <= 0) return;
    const bool isSnap = (when[0] == 's');
    std::uint64_t mb = 0;
    const int mcount = LaneCount(mgr, kVecs[1], mb);   // kVecs[1] = v_0x90, the drawn menu
    if (!isSnap && mcount < 3) return;

    static std::uint64_t lastPre = 0, lastPost = 0;
    const bool isPre = (when[0] == 'p' && when[1] == 'r');
    std::uint64_t& last = isPre ? lastPre : lastPost;
    const std::uint64_t sig = LaneSig(mgr);
    if (sig == last) return;                            // same menu still under the cursor
    last = sig;
    --g_laneLog;

    char who[288];
    who[0] = 0;
    {
        std::uint64_t tb = 0, te = 0;
        if (Rd(mgr + 0x1380, &tb, 8) && Rd(mgr + 0x1388, &te, 8) && tb && te > tb) {
            bool hp = false;
            if (ReadEastl(tb + 0x80, who, sizeof(who), &hp) <= 0) who[0] = 0;
        }
    }
    Log("  [%s]  n=%d  %s", when, mcount, who);
    for (const Vec& v : kVecs) {
        std::uint64_t begin = 0;
        const int n = LaneCount(mgr, v, begin);
        if (n < 1) continue;
        char line[480];
        int at = 0;
        for (int d = 0; d < n && at < (int)sizeof(line) - 40; ++d) {
            std::uint64_t obj = 0;
            if (!Rd(begin + (std::uint64_t)(n - 1 - d) * kRecSize, &obj, 8) || !obj) continue;
            char verb[rtx::menu::kVerbLen];
            bool hp = false;
            if (ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) <= 0) continue;
            at += std::snprintf(line + at, sizeof(line) - at, "%s%s", d ? " | " : "", verb);
        }
        if (at) Log("      %-9s %s", v.name, line);
    }
}

// Hover record slots mgr+0x13e0/0x13f0/0x1400/0x1410 hold {base, display}; display = base+0x20, verb @display+0x18, target @display+0x00, type @*(display+0x38)+0x44.
// The CS2 hover-info op reads 0x13e0 or 0x1410 by a settings byte; the snap rebuilds +0x1468/+0x1790 from +0x90/+0x13a0 when mgr+0x1420 is set, and empties +0x3b8 each tick.
int g_slotLog = 0;                        // budget, re-armed when the panel is opened

void DumpHoverSlots(std::uint64_t mgr) {
    if (g_slotLog <= 0) return;
    char verbs[4][64], tgt0[96];
    std::int32_t tys[4];
    static const std::uint64_t kSlots[] = { 0x13E0, 0x13F0, 0x1400, 0x1410 };
    for (int i = 0; i < 4; ++i) {
        verbs[i][0] = 0; tys[i] = -1;
        std::uint64_t q1 = 0;
        if (!Rd(mgr + kSlots[i] + 8, &q1, 8) || !q1) continue;
        bool hp = false;
        if (ReadEastl(q1 + 0x18, verbs[i], sizeof(verbs[i]), &hp) <= 0) verbs[i][0] = 0;
        if (i == 0 && ReadEastl(q1 + 0x00, tgt0, sizeof(tgt0), &hp) <= 0) tgt0[0] = 0;
        std::uint64_t tag = 0;
        if (Rd(q1 + 0x38, &tag, 8) && tag) Rd(tag + 0x44, &tys[i], 4);
    }
    std::uint64_t b90 = 0;
    const int n90 = LaneCount(mgr, kVecs[1], b90);
    char top[3][64];
    for (int t = 0; t < 3; ++t) {
        top[t][0] = 0;
        if (t >= n90) continue;
        const std::uint64_t rec = b90 + (std::uint64_t)(n90 - 1 - t) * kRecSize;
        std::uint64_t obj = 0;
        if (!Rd(rec, &obj, 8) || !obj) continue;
        char verb[48];
        bool hp = false;
        if (ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) <= 0) continue;
        std::uint64_t tag = 0;
        std::int32_t  prio = 0;
        // Op index lives in the record (display +0x50), separate from the class.
        std::uint64_t q1 = 0;
        std::int32_t  op = -1;
        if (Rd(rec + kRecTarget, &q1, 8) && q1) Rd(q1 + 0x50, &op, 4);
        if (EntryTag(rec, tag, prio))
            std::snprintf(top[t], sizeof(top[t]), "%s(p%d op%d c%llx)", verb, (int)prio, (int)op,
                          (unsigned long long)(tag & 0xFFFFFF));
        else
            std::snprintf(top[t], sizeof(top[t]), "%s(p? op%d)", verb, (int)op);
    }
    const std::uint32_t pins = g_share ? g_share->pinCount : 0;

    std::uint64_t key = 1469598103934665603ull ^ ((std::uint64_t)(unsigned)n90 << 48)
                      ^ ((std::uint64_t)pins << 56);
    for (int i = 0; i < 4; ++i)
        for (const char* p = verbs[i]; *p; ++p) key = (key ^ (unsigned char)*p) * 1099511628211ull;
    for (int t = 0; t < 3; ++t)
        for (const char* p = top[t]; *p; ++p) key = (key ^ (unsigned char)*p) * 1099511628211ull;
    static std::uint64_t lastKey = 0;
    if (key == lastKey) return;
    lastKey = key;
    --g_slotLog;

    char cnt[160];
    int at = 0;
    static const struct { std::uint64_t begin, end; const char* nm; } kCnt[] = {
        { 0x0090, 0x0098, "90" }, { 0x03B8, 0x03C0, "3b8" },
        { 0x1468, 0x1470, "1468" }, { 0x1790, 0x1798, "1790" }, { 0x13A0, 0x13A8, "13a0" },
    };
    for (const auto& v : kCnt) {
        std::uint64_t b = 0, e = 0;
        Rd(mgr + v.begin, &b, 8); Rd(mgr + v.end, &e, 8);
        const long long c = (b && e >= b) ? (long long)((e - b) >> 4) : -1;
        at += std::snprintf(cnt + at, sizeof(cnt) - at, "%s=%lld ", v.nm, c);
    }
    Log("  [slots] pins=%u %s top90: %s | %s | %s", pins, cnt, top[0], top[1], top[2]);
    for (int i = 0; i < 4; ++i) {
        if (!verbs[i][0]) { Log("      +0x%04llx  (empty)", (unsigned long long)kSlots[i]); continue; }
        Log("      +0x%04llx  ty=%d verb=\"%s\"%s%s%s", (unsigned long long)kSlots[i], tys[i],
            verbs[i], i == 0 ? " tgt=\"" : "", i == 0 ? tgt0 : "", i == 0 ? "\"" : "");
    }
}

// ---- left-click lift ----
// The game's snapshot (FUN_140166980 on 950-1) pulls every row whose class priority is below 1000 out
// of +0x90 and appends them on top, re-sorts, then copies the top rows into the left-click slots
// (+0x13f0 = top, +0x1400 = second, +0x13e0 = top or second by a setting) through a refcounted
// assign helper. That is why a demoted rule row ("Deposit all fish", class 1001) never became the
// default and "Walk here" jumped above it. Changing the class is not an option for world objects
// (the class selects which option is sent), so instead, after the snapshot, the rule's top row is
// moved to the top of +0x90 (a permutation of whole records, refcount neutral) and the slots are
// re-assigned with the game's own helper, found by scanning the snapshot body for its two calls.
typedef void(__fastcall* Assign_t)(std::uint64_t slot, std::uint64_t src);
Assign_t g_assign = nullptr;

std::uint64_t CallTargetAfter(const unsigned char* body, std::size_t n, const unsigned char* lea) {
    for (std::size_t i = 0; i + 12 <= n; ++i) {
        if (std::memcmp(body + i, lea, 7) != 0 || body[i + 7] != 0xE8) continue;
        std::int32_t rel = 0;
        std::memcpy(&rel, body + i + 8, 4);
        return (std::uint64_t)((std::int64_t)(std::uintptr_t)(body + i + 12) + rel);
    }
    return 0;
}

void ResolveAssign(std::uint64_t snap) {
    if (!snap) return;
    const unsigned char* body = (const unsigned char*)snap;
    static const unsigned char kLea13f0[] = { 0x48,0x8D,0x8F,0xF0,0x13,0x00,0x00 };   // lea rcx,[rdi+0x13f0]
    static const unsigned char kLea13e0[] = { 0x48,0x8D,0x8F,0xE0,0x13,0x00,0x00 };   // lea rcx,[rdi+0x13e0]
    std::uint64_t a = 0, b = 0;
    __try {
        a = CallTargetAfter(body, 0x1400, kLea13f0);
        b = CallTargetAfter(body, 0x1400, kLea13e0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { a = b = 0; }
    if (a && a == b && a > g_base && a < g_modEnd) g_assign = (Assign_t)a;
    Log("slot assign helper: %s", g_assign ? "found" : "NOT FOUND (left-click lift unavailable)");
    if (g_assign) Log("  assign rva 0x%llx", (unsigned long long)(a - g_base));
}

void LiftRuleTop(std::uint64_t mgr) {
    if (!g_share || !g_share->pinCount) return;
    std::uint64_t begin = 0, e = 0;
    if (!Rd(mgr + 0x90, &begin, 8) || !Rd(mgr + 0x98, &e, 8) || !begin || e <= begin) return;
    const std::uint64_t span = e - begin;
    if (span % kRecSize || span > kRecSize * rtx::menu::kMaxEntries) return;
    const int n = (int)(span / kRecSize);
    if (n < 2) return;

    unsigned char recs[rtx::menu::kMaxEntries][kRecSize];
    int  rank[rtx::menu::kMaxEntries];
    bool fixedSlot[rtx::menu::kMaxEntries];
    int  decoded = 0;
    if (!RankLane(begin, n, recs, rank, fixedSlot, &decoded) || decoded != n) return;

    int best = -1;
    for (int i = 0; i < n; ++i)
        if (!fixedSlot[i] && rank[i] != 0x7FFFFFFF && (best < 0 || rank[i] < rank[best])) best = i;
    if (best < 0 || best == n - 1) return;                   // no rule row here, or already the default
    if (!g_assign) { g_share->promoState = rtx::menu::kPromoNoAssign; return; }

    std::uint64_t old13e0 = 0, old13f0 = 0;
    Rd(mgr + 0x13E0, &old13e0, 8);
    Rd(mgr + 0x13F0, &old13f0, 8);
    const bool hoverWasTop = old13e0 == old13f0;

    __try {
        for (int j = best; j < n - 1; ++j)
            std::memcpy((void*)(begin + (std::uint64_t)j * kRecSize), recs[j + 1], kRecSize);
        std::memcpy((void*)(begin + (std::uint64_t)(n - 1) * kRecSize), recs[best], kRecSize);
        const std::uint64_t top = begin + (std::uint64_t)(n - 1) * kRecSize;
        const std::uint64_t second = begin + (std::uint64_t)(n - 2) * kRecSize;
        g_assign(mgr + 0x13F0, top);
        g_assign(mgr + 0x13E0, hoverWasTop || n < 3 ? top : second);
        if (n > 2) g_assign(mgr + 0x1400, second);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_share->promoState = rtx::menu::kPromoWriteFailed;
        return;
    }
    g_share->promoState = rtx::menu::kPromoLifted;
    bool hp = false;
    std::uint64_t obj = 0;
    std::memcpy(&obj, recs[best], 8);
    char verb[rtx::menu::kVerbLen];
    if (obj && ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) > 0) {
        std::strncpy(g_share->promoVerb, verb, rtx::menu::kVerbLen - 1);
        g_share->promoVerb[rtx::menu::kVerbLen - 1] = 0;
    }
}

void __fastcall Detour_Init(std::uint64_t mgr) {
    g_mgr = mgr;
    g_origInit(mgr);
}

typedef void(__fastcall* Build_t)(std::uint64_t, std::uint32_t, std::uint32_t, std::uint8_t);
Build_t g_origBuild = nullptr;

typedef std::uint64_t(__fastcall* Snap_t)(std::uint64_t);
Snap_t g_origSnap = nullptr;

std::uint64_t __fastcall Detour_Snap(std::uint64_t mgr) {
    if (mgr) {
        g_mgr = mgr;
        ApplyOrder(mgr);             // before the original: it is about to read the top record
        PromotePinnedEntry(mgr);
        LogLaneTops(mgr, "snap");
    }
    const std::uint64_t r = g_origSnap(mgr);
    if (mgr) LiftRuleTop(mgr);       // after the game's sort and slot copy: the rule's top row becomes the default
    if (mgr) DumpHoverSlots(mgr);
    // Publish here on the game thread; doing it from Poll() races the +0x90 rebuild.
    if (mgr && g_share && g_share->enable) Publish(mgr);
    return r;
}

// Per-tick menu builder (rcx = manager, 4 args); the array is rebuilt every tick.
void __fastcall Detour_Build(std::uint64_t ctx, std::uint32_t a2, std::uint32_t a3, std::uint8_t a4) {
    if (g_mgr) LogLaneTops(g_mgr, "pre");
    g_origBuild(ctx, a2, a3, a4);
    const std::uint64_t mgr = g_mgr;
    if (mgr) {
        ApplyOrder(mgr);
        LogLaneTops(mgr, "post");
    }
}

void __fastcall Detour_Clear(std::uint64_t mgr, char flag) {
    g_mgr = mgr;                     // reliable capture: runs on every menu open
    g_origClear(mgr, flag);
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    HMODULE gm = GetModuleHandleW(L"rs2client.exe");
    if (!gm) return false;
    g_base = (std::uint64_t)gm;
    {   // module bounds for vtable sanity checks
        auto dos = (const IMAGE_DOS_HEADER*)g_base;
        auto nt  = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
        g_modEnd = g_base + nt->OptionalHeader.SizeOfImage;
    }

    const std::uint64_t init  = Scan(kInit,  kInitMask,  sizeof(kInit));
    const std::uint64_t clear = Scan(kClear, kClearMask, sizeof(kClear));
    const std::uint64_t build = Scan(kBuild, kBuildMask, sizeof(kBuild));
    const std::uint64_t snap  = Scan(kSnap,  kSnapMask,  sizeof(kSnap));
    Log("=== RuneToolsX menu probe ===");
    Log("string-init: %s", init  ? "found" : "NOT FOUND");
    Log("clear()    : %s", clear ? "found" : "NOT FOUND");
    Log("builder    : %s%s", build ? "found" : "NOT FOUND",
        build ? "" : "  (reordering unavailable)");
    if (init)  Log("  init  rva 0x%llx", (unsigned long long)(init  - g_base));
    if (clear) Log("  clear rva 0x%llx", (unsigned long long)(clear - g_base));
    if (build) Log("  build rva 0x%llx", (unsigned long long)(build - g_base));
    if (!init && !clear) {
        Log("Neither pattern matched - the game build moved them. Nothing hooked.");
        return false;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (init)  { g_origInit  = (Init_t)init;   DetourAttach(&(PVOID&)g_origInit,  (PVOID)Detour_Init); }
    if (clear) { g_origClear = (Clear_t)clear; DetourAttach(&(PVOID&)g_origClear, (PVOID)Detour_Clear); }
    if (build) { g_origBuild = (Build_t)build; DetourAttach(&(PVOID&)g_origBuild, (PVOID)Detour_Build); }
    if (snap)  ResolveAssign(snap);     // read the original body before Detours patches its first bytes
    if (snap)  { g_origSnap  = (Snap_t)snap;   DetourAttach(&(PVOID&)g_origSnap,  (PVOID)Detour_Snap); }
    if (DetourTransactionCommit() != NO_ERROR) {
        g_origInit = nullptr; g_origClear = nullptr;
        Log("Detour commit failed - nothing hooked.");
        return false;
    }
    g_installed = true;
    g_share = MapShare();
    if (g_share) {
        g_share->magic = rtx::menu::kMagic;
        g_share->version = rtx::menu::kVersion;
        g_share->pid = GetCurrentProcessId();
        g_share->flags = rtx::menu::kFlagHooked;
        g_share->enable = 0; g_share->seq = 0; g_share->count = 0;
        g_share->pinSeq = 0; g_share->pinCount = 0;
        for (int i = 0; i < 4; ++i) { g_share->diag[i] = 0; g_share->stage[i] = 0; }
        for (auto& l : g_share->lane) l = 0;
        g_share->lastTarget[0] = 0; g_share->lastTargeted = 0;
        g_share->lastVerb[0] = 0;
        g_share->promoState = rtx::menu::kPromoIdle;
        g_share->promoPrio = 0; g_share->promoPartnerPrio = 0; g_share->promoVerb[0] = 0;
        g_share->promoSource = rtx::menu::kPromoSrcNone;
    }
    Log("Hooked. Hover something in game; the next %d menus are recorded here.", kMaxDumps);
    ScanHoverSlotRefs();
    return true;
}

void Poll() {
    if (!g_installed) return;
    const std::uint64_t mgr = g_mgr;
    if (!mgr) return;

    {
        static bool wasOn = false;
        const bool on = g_share && g_share->enable == rtx::menu::kEnablePanel;
        if (on && !wasOn) {
            g_dumps   = 0;
            g_lastSig = 0;
            g_laneLog = 12;
            g_slotLog = 24;
            Log("");
            Log("---- panel opened: recording the next %d menus ----", kMaxDumps);
        }
        wasOn = on;
    }
    if (g_dumps >= kMaxDumps) return;

    std::uint64_t sig = 0;
    bool anything = false;
    for (const Vec& v : kVecs) {
        std::uint64_t b = 0, e = 0;
        if (!Rd(mgr + v.begin, &b, 8) || !Rd(mgr + v.end, &e, 8)) continue;
        if (!b || e < b || e - b > 0x40000) continue;
        sig = sig * 1000003u + (b ^ (e - b));
        if (e > b) anything = true;
    }
    if (!anything) return;
    const std::uint32_t s32 = (std::uint32_t)(sig ^ (sig >> 32));
    if (s32 == g_lastSig) return;
    g_lastSig = s32;
    ++g_dumps;

    Log("");
    Log("===== menu %d/%d  mgr=0x%llx =====", g_dumps, kMaxDumps, (unsigned long long)mgr);
    HoverDump(mgr);
    char txt[320];
    for (const Vec& v : kVecs) {
        std::uint64_t b = 0, e = 0;
        if (!Rd(mgr + v.begin, &b, 8) || !Rd(mgr + v.end, &e, 8)) continue;
        if (!b || e <= b || e - b > 0x40000) continue;
        const std::uint64_t span = e - b;
        Log("  [%s] +0x%llx  span=%llu bytes%s", v.name, (unsigned long long)v.begin,
            (unsigned long long)span,
            v.stride ? "" : "  (stride unknown)");
        if (v.stride && span % v.stride == 0)
            Log("        = %llu entries of 0x%llx",
                (unsigned long long)(span / v.stride), (unsigned long long)v.stride);
        // 16-byte records = { action object*, line-string* }; both pointers are followed.
        if (span % 16 == 0 && span <= 16 * 64) {
            Log("        = %llu records of 16", (unsigned long long)(span / 16));
            for (std::uint64_t i = 0; i * 16 < span; ++i) {
                std::uint64_t q[2] = { 0, 0 };
                if (!Rd(b + i * 16, q, 16)) continue;
                std::uint32_t k32[4] = { 0, 0, 0, 0 };
                Rd(q[0], k32, sizeof(k32));
                std::uint64_t sp = 0, ss = 0;
                Rd(q[1], &sp, 8);
                Rd(q[1] + 8, &ss, 8);
                char line[288];
                line[0] = 0;
                if (sp > 0x10000 && (sp >> 48) == 0 && ss > 0 && ss < 250) {
                    if (!Rd(sp, line, (std::size_t)ss)) line[0] = 0; else line[ss] = 0;
                    for (std::size_t c = 0; c < (std::size_t)ss; ++c)
                        if ((unsigned char)line[c] < 0x20 || (unsigned char)line[c] > 0x7E) { line[0] = 0; break; }
                }
                if (!line[0]) {                       // inline: the text sits in place
                    char inl[0x18];
                    if (Rd(q[1], inl, sizeof(inl)) && (unsigned char)inl[0x17] <= 0x17) {
                        const int ln = 0x17 - (unsigned char)inl[0x17];
                        if (ln > 0 && ln < 0x17) { std::memcpy(line, inl, (std::size_t)ln); line[ln] = 0; }
                    }
                }
                Log("        [%llu] refs=%u  target=\"%s\"",
                    (unsigned long long)i, k32[2], line);
                Log("             obj=%016llx  tgt=%016llx", 
                    (unsigned long long)q[0], (unsigned long long)q[1]);
                char t2[288];
                for (std::uint64_t o2 = 0; o2 + 0x18 <= 0x140; o2 += 8) {
                    bool hp = false;
                    if (ReadEastl(q[0] + o2, t2, sizeof(t2), &hp) > 0)
                        Log("             obj+0x%03llx %-6s \"%s\"",
                            (unsigned long long)o2, hp ? "heap" : "inline", t2);
                }
                std::uint32_t more[16];
                if (Rd(q[0], more, sizeof(more))) {
                    char ib[380]; int w2 = 0; ib[0] = 0;
                    for (int z = 2; z < 16; ++z) {          // skip the vtable halves
                        const int kk = std::snprintf(ib + w2, sizeof(ib) - w2, "+%02x=%u ", z * 4, more[z]);
                        if (kk <= 0 || (std::size_t)(w2 + kk) >= sizeof(ib) - 1) break;
                        w2 += kk;
                    }
                    Log("             ints %s", ib);
                }
            }
            continue;
        }
        std::uint64_t lastHit = 0;
        int hits = 0;
        for (std::uint64_t off = 0; off + 0x18 <= span && hits < 64; off += 8) {
            bool heap = false;
            if (ReadEastl(b + off, txt, sizeof(txt), &heap) <= 0) continue;
            Log("        +0x%04llx %-6s \"%s\"", (unsigned long long)off,
                heap ? "heap" : "inline", txt);
            if (lastHit) Log("               (+0x%llx since previous)",
                             (unsigned long long)(off - lastHit));
            lastHit = off;
            ++hits;
        }
        if (!hits) Log("        (no strings)");
    }
    if (g_dumps >= kMaxDumps) Log("");
}

void Uninstall() {
    if (!g_installed) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (g_origInit)  DetourDetach(&(PVOID&)g_origInit,  (PVOID)Detour_Init);
    if (g_origClear) DetourDetach(&(PVOID&)g_origClear, (PVOID)Detour_Clear);
    if (g_origBuild) DetourDetach(&(PVOID&)g_origBuild, (PVOID)Detour_Build);
    if (g_origSnap)  DetourDetach(&(PVOID&)g_origSnap,  (PVOID)Detour_Snap);
    DetourTransactionCommit();
    g_installed = false;
}

}  // namespace rtx::menuprobe
