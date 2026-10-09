// Right-click menu probe rvas: string-init 0x161C10, clear(bool) 0x164880, add 0x160AB0, finalize 0x1661E0, item decoder 0x2DCDF0. Poll() reads on the companion thread while ApplyOrder writes on the game thread, so lane reads can tear.
// mgr+0x90/+0x98 entries: 16-byte {control block*, entry*}, entry = control block + 0x20 unless menus merge (see kRecTarget), REVERSE display order. mgr+0x1380/+0x1388 stride 0x2E8 hover-target list; subset lanes +0x0d30 all-but-Cancel, +0x06e0 targeted; hover block +0x000 = entity handle ((x<<16)|y world object, index actor). Control block: +0x08 refcount, +0x0c = 1, +0x20 EASTL target, +0x38 EASTL verb.

#include "SceneOffsets.h"
#include "MenuProbe.h"
#include "Signatures.h"
#include "MainDataOffsets.h"
#include "MenuShare.h"

#include <windows.h>
#include <detours.h>
#include "CodeFind.h"

#include <cstdarg>
#include <intrin.h>
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

// Layout self-check (boot record line `check: menu-record`): what the signature hits named against
// the compiled displacements, then whether the live records read as the layout this file knows.
// Until the live records pass, nothing is written into the menu (no reorder, no left-click lift).
std::mutex    g_checkMu;
char          g_check[700] = {};
bool          g_recordOk = false;           // the live records decoded on the last judged menus
bool          g_recordJudged = false;       // at least ten rows seen
std::uint32_t g_rowsSeen = 0, g_rowsVerb = 0, g_rowsTag = 0, g_menusSeen = 0;
std::int32_t  g_lastBadOrd = -1;
char          g_static[400] = {};           // what the hits named
const char*   g_staticKind = "";            // "moved" when a hit named another displacement than compiled
std::uint32_t g_tgtBegin = rtx::sig::kMenuTargetsBegin, g_tgtEnd = rtx::sig::kMenuTargetsEnd;   // the hover-target vector, from the menu-clear hit
void SayCheck(const char* status, const char* kind, const char* exp, const char* got, const char* need, const char* detail) {
    std::lock_guard<std::mutex> lk(g_checkMu);
    std::snprintf(g_check, sizeof(g_check), "check: menu-record %s kind=%s exp=%s got=%s features=%s need=%s ; %s", status,
                  kind && kind[0] ? kind : "-", exp && exp[0] ? exp : "-", got && got[0] ? got : "-",
                  "Menu swaps|Menu swaps: reordering|Menu swaps: left-click option|Menu panel", need && need[0] ? need : "-", detail);
}

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
constexpr const auto& kInit = rtx::sig::kMenuInit;
constexpr const auto& kInitMask = rtx::sig::kMenuInitMask;
static_assert(sizeof(kInit) == sizeof(kInitMask), "init pattern/mask length mismatch");

// clear(): found by its [rcx+0x1388] end / [rcx+0x1380] begin loads; the hook is the function holding them.
constexpr const auto& kClear = rtx::sig::kMenuClear;
constexpr const auto& kClearMask = rtx::sig::kMenuClearMask;
static_assert(sizeof(kClear) == sizeof(kClearMask), "clear pattern/mask length mismatch");

// Per-tick menu builder: the clear() caller touching +0x90/+0x98 (not the 0x160AB0 caller).
constexpr const auto& kBuild = rtx::sig::kMenuBuild;
constexpr const auto& kBuildMask = rtx::sig::kMenuBuildMask;
static_assert(sizeof(kBuild) == sizeof(kBuildMask), "build pattern/mask length mismatch");

// Snapshot (rcx = manager): copies the last +0x90 record into mgr+0x13f0, second-from-top into 0x13e0.
// Found inside its body; the hook is the function holding the match.
constexpr const auto& kSnap = rtx::sig::kMenuSnap;
constexpr const auto& kSnapMask = rtx::sig::kMenuSnapMask;
static_assert(sizeof(kSnap) == sizeof(kSnapMask), "snapshot pattern/mask length mismatch");

// Menu action executor (950-1 rva 0x167c80): rcx = manager, rdx = 16-byte record {base, display}, r8 = click pos.
// Every way of running a menu row (left-click, right-click select, script select) ends here.
// Found inside its body; the hook is the function holding the match.
constexpr const auto& kExec = rtx::sig::kMenuExec;
constexpr const auto& kExecMask = rtx::sig::kMenuExecMask;
static_assert(sizeof(kExec) == sizeof(kExecMask), "executor pattern/mask length mismatch");

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
// Read the target through the record's entry (+8); the control block's +0x20 diverges from it when menus merge.
constexpr std::uint64_t kRecTarget    = 8;
constexpr std::uint64_t kObjTarget    = 0x20;
constexpr std::uint64_t kObjVerb      = 0x38;

// Record qword 1 = display object: target +0x00, verb +0x18, +0x4C/+0x50 = tile x/z on loc and walk rows (other row kinds not read), class tag +0x38 (priority at tag+0x40); priority < 1000 rows are re-inserted at top each tick.
constexpr std::uint64_t kDispTag  = 0x38;
constexpr std::uint64_t kTagPrio  = 0x40;
constexpr std::int32_t  kPromoted = 1000;
constexpr std::int32_t  kDemotedIface = 1007;   // interface demoted class: the only one with a proven promoted partner

bool EntryTag(std::uint64_t rec, std::uint64_t& tag, std::int32_t& prio);
int RuleOrder(std::uint64_t begin, int n, const int* rank, const bool* fixedSlot, int* order);

rtx::menu::Share* MapShare() {
    wchar_t name[rtx::ipc::kNameChars];
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
    if (!Rd(mgr + g_tgtBegin, &b, 8) || !Rd(mgr + g_tgtEnd, &e, 8)) return;
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
        if (Rd(mgr + g_tgtBegin, &tb, 8) && Rd(mgr + g_tgtEnd, &te, 8) && tb && te > tb) {
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
// Permute only the drawn menu (+0x90) and +0x3b8, which the routine at exe+0x12D660 copies over +0x90 wholesale for interface/inventory menus.
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
    static char rowVerb[rtx::menu::kMaxEntries][rtx::menu::kVerbLen];
    static char rowTgt[rtx::menu::kMaxEntries][rtx::menu::kTargetLen];
    bool rowOk[rtx::menu::kMaxEntries] = {};
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
        std::strncpy(rowVerb[i], verb, rtx::menu::kVerbLen - 1);
        rowVerb[i][rtx::menu::kVerbLen - 1] = 0;
        std::strncpy(rowTgt[i], tgt, rtx::menu::kTargetLen - 1);
        rowTgt[i][rtx::menu::kTargetLen - 1] = 0;
        rowOk[i] = true;
        if (i == 0 || (anyTargeted && !tgt[0])) { fixedSlot[i] = true; continue; }
    }
    // Rules only reorder the options of the object the game's top targeted row names, the one the
    // left-click acts on. A rule for another object further down the same menu (a guard standing
    // behind the door under the cursor) must not take the left-click over, and neither may a rule
    // that names no object. A menu with no targeted row at all has no such object: no limit.
    const char* topTgt = nullptr;
    for (int i = n - 1; i >= 0 && !topTgt; --i)
        if (rowOk[i] && rowTgt[i][0]) topTgt = rowTgt[i];
    // Pins arrive as rules separated by a group marker (verb "\x1d"). Objects that share a name
    // ("Fishing spot" npc 321 Harpoon/Cage, npc 322 Harpoon/Net) cannot be told apart by name, so a
    // rule only applies when every option the menu offers for that target is one the rule names: the
    // 321 rule has no Net, so it leaves the 322 menu alone. Options the rule names but the menu lacks
    // are fine ("Deposit all fish" only shows while carrying fish). The first rule that qualifies wins.
    int  pinGroup[rtx::menu::kMaxPins];
    bool groupBad[rtx::menu::kMaxPins + 1] = {};
    {
        int g = 0;
        for (std::uint32_t p = 0; p < pins; ++p) {
            if ((unsigned char)pinsLocal[p].verb[0] == 0x1D) { ++g; pinGroup[p] = -1; continue; }
            pinGroup[p] = g;
        }
        for (std::uint32_t p = 0; p < pins; ++p) {
            const int pg = pinGroup[p];
            // one check per group: its first targeted pin (a rule names a single target)
            if (pg < 0 || groupBad[pg] || !pinsLocal[p].target[0]) continue;
            if (p > 0 && pinGroup[p - 1] == pg && pinsLocal[p - 1].target[0]) continue;
            const char* tg = pinsLocal[p].target;
            for (int i = 0; i < n && !groupBad[pg]; ++i) {
                if (!rowOk[i] || std::strncmp(rowTgt[i], tg, rtx::menu::kTargetLen) != 0) continue;
                bool named = false;
                for (std::uint32_t q = p; q < pins && pinGroup[q] == pg && !named; ++q)
                    if (std::strncmp(rowVerb[i], pinsLocal[q].verb, rtx::menu::kVerbLen) == 0) named = true;
                if (!named) groupBad[pg] = true;
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!rowOk[i] || fixedSlot[i]) continue;
        if (topTgt && std::strncmp(rowTgt[i], topTgt, rtx::menu::kTargetLen) != 0) continue;
        for (std::uint32_t p = 0; p < pins; ++p) {
            if (pinGroup[p] < 0) continue;
            if (std::strncmp(rowVerb[i], pinsLocal[p].verb, rtx::menu::kVerbLen) != 0) continue;
            if (pinsLocal[p].target[0] &&
                (groupBad[pinGroup[p]] ||
                 std::strncmp(rowTgt[i], pinsLocal[p].target, rtx::menu::kTargetLen) != 0)) continue;
            rank[i] = (int)p;
            ++g_share->stage[1];
            break;
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
    if (!g_share || !g_recordOk) return;
    ++g_share->diag[2];
    if (!g_share->pinCount) return;
    ++g_share->diag[3];

    unsigned char recs[rtx::menu::kMaxEntries][kRecSize];
    int  rank[rtx::menu::kMaxEntries];
    bool fixedSlot[rtx::menu::kMaxEntries];

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

        int order[rtx::menu::kMaxEntries];
        if (lane.stat == 0) ++g_share->stage[2];
        if (RuleOrder(begin, n, rank, fixedSlot, order) < 0) continue;
        if (lane.stat == 0) ++g_share->stage[3];

        // Structural check instead of an exe-stamp allow-list: the write is a permutation of whole
        // records that were just read, so it is safe exactly when every record in the lane decoded
        // as a menu entry (object pointer -> readable verb). That holds on the OpenGL and Vulkan
        // clients alike and survives rebuilds; a layout change fails it and nothing is written.
        if (decoded != n) { g_share->flags |= rtx::menu::kFlagUnverified; continue; }
        __try {
            for (int k = 0; k < n; ++k)
                std::memcpy((void*)(begin + (std::uint64_t)(n - 1 - k) * kRecSize), recs[order[k]], kRecSize);
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

// Rule order for one menu list (index 0 = bottom row). Fills order[] top to bottom with record
// indices and returns the rule's first row, or -1 when no row is ranked or nothing would change.
// The rule's first row becomes the top (the left-click). Every other row keeps the game's layout:
// rule rows only swap places with rule rows of the same kind (action options above "Walk here",
// Examine-type options below it), and rows the rule does not name (Walk here, Cancel, another
// object's options) stay where the game put them. A rule saved on one "Fishing spot" also applies
// to a same-named spot with other options, so it must not drag Examine above Walk here or strand
// that spot's own options below it.
int RuleOrder(std::uint64_t begin, int n, const int* rank, const bool* fixedSlot, int* order) {
    int best = -1;
    for (int i = 0; i < n; ++i)
        if (!fixedSlot[i] && rank[i] != 0x7FFFFFFF && (best < 0 || rank[i] < rank[best])) best = i;
    if (best < 0) return -1;
    bool demoted[rtx::menu::kMaxEntries];
    for (int i = 0; i < n; ++i) {
        std::uint64_t tag = 0;
        std::int32_t prio = 0;
        demoted[i] = EntryTag(begin + (std::uint64_t)i * kRecSize, tag, prio) && prio >= kPromoted;
    }
    int m = 0;
    order[m++] = best;
    for (int i = n - 1; i >= 0; --i)
        if (i != best) order[m++] = i;
    for (int group = 0; group < 2; ++group) {
        int pos[rtx::menu::kMaxEntries], rows[rtx::menu::kMaxEntries];
        int c = 0;
        for (int k = 1; k < n; ++k) {
            const int r = order[k];
            if (fixedSlot[r] || rank[r] == 0x7FFFFFFF || demoted[r] != (group == 1)) continue;
            pos[c] = k; rows[c] = r; ++c;
        }
        for (int a = 1; a < c; ++a)                          // stable insertion sort by rule rank
            for (int b = a; b > 0 && rank[rows[b]] < rank[rows[b - 1]]; --b) {
                const int t = rows[b]; rows[b] = rows[b - 1]; rows[b - 1] = t;
            }
        for (int a = 0; a < c; ++a) order[pos[a]] = rows[a];
    }
    for (int k = 0; k < n; ++k) if (order[k] != n - 1 - k) return best;
    return -1;                                               // already in rule order
}


// Gated to the interface demoted class (1007). Loc classes 1001/1002 have ordinary twins 6/7 with the
// same click callable, which the game only uses inside the box flagged at wv+0xE8428 (set by server
// packet 216; the swap has not been observed); 1003 not examined.
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

// True when the top record is the row a rule chose, the one RuleOrder lifts: every row decoded,
// and the top is a movable row holding the best rule rank of the list.
bool TopIsRuleRow(std::uint64_t begin, int n) {
    if (n < 2 || n > rtx::menu::kMaxEntries) return false;
    unsigned char recs[rtx::menu::kMaxEntries][kRecSize];
    int  rank[rtx::menu::kMaxEntries];
    bool fixedSlot[rtx::menu::kMaxEntries];
    int  decoded = 0;
    if (!RankLane(begin, n, recs, rank, fixedSlot, &decoded) || decoded != n) return false;
    const int top = n - 1;
    if (fixedSlot[top] || rank[top] == 0x7FFFFFFF) return false;
    for (int i = 0; i < top; ++i)
        if (!fixedSlot[i] && rank[i] < rank[top]) return false;
    return true;
}

void PromotePinnedEntry(std::uint64_t mgr) {
    if (!g_share) return;
    g_share->promoState = rtx::menu::kPromoIdle;
    g_share->promoPrio = 0;
    g_share->promoPartnerPrio = 0;
    g_share->promoSource = rtx::menu::kPromoSrcNone;
    g_share->promoVerb[0] = 0;
    if (!g_share->pinCount) return;
    if (!g_recordOk) { g_share->promoState = rtx::menu::kPromoUnverified; return; }
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
    // A demoted top row no rule asked for is the game's own default: its class stays as it is.
    if (!TopIsRuleRow(begin, (int)count)) {
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
        if (Rd(mgr + g_tgtBegin, &tb, 8) && Rd(mgr + g_tgtEnd, &te, 8) && tb && te > tb) {
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
        // Display +0x50 = tile z on loc and walk rows (other row kinds not read).
        std::uint64_t q1 = 0;
        std::int32_t  z = -1;
        if (Rd(rec + kRecTarget, &q1, 8) && q1) Rd(q1 + 0x50, &z, 4);
        if (EntryTag(rec, tag, prio))
            std::snprintf(top[t], sizeof(top[t]), "%s(p%d z%d c%llx)", verb, (int)prio, (int)z,
                          (unsigned long long)(tag & 0xFFFFFF));
        else
            std::snprintf(top[t], sizeof(top[t]), "%s(p? z%d)", verb, (int)z);
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
// The game's snapshot (exe+0x166980 on 950-1) pulls every row whose class priority is below 1000 out
// of +0x90 and appends them on top, re-sorts, then copies the top rows into the left-click slots
// (+0x13f0 = top, +0x1400 = second, +0x13e0 = top or second by a setting) through a refcounted
// assign helper. That is why a demoted rule row ("Deposit all fish", class 1001) never became the
// default and "Walk here" jumped above it. Changing the class is not an option for NPC, player and
// ground-item rows (the class selects which option is sent), so instead, after the snapshot, the
// rule's top row is moved to the top of +0x90 (a permutation of whole records, refcount neutral) and
// the slots are re-assigned with the game's own helper, found by scanning the snapshot body for its
// two calls.
typedef void(__fastcall* Assign_t)(std::uint64_t slot, std::uint64_t src);
Assign_t g_assign = nullptr;

// Call site of the left-click slot assign inside the snapshot: `lea rcx,[reg+slot]; call rel32`,
// slot 0x13e0 (0x13e8 on the plugin client).
std::uint64_t g_siteCall = 0;          // address of the E8 byte
std::uint32_t g_siteSlot = 0x13E0;     // the slot that call assigns, from its lea
std::int32_t  g_siteRel  = 0;          // original rel32, restored on uninstall
void*         g_siteTramp = nullptr;   // near trampoline: mov rax, imm64; jmp rax
bool          g_sitePatched = false;

void ResolveAssign(std::uint64_t snap) {
    if (!snap) return;
    const unsigned char* body = (const unsigned char*)snap;
    rtx::sig::MenuAssign m;
    __try {
        m = rtx::sig::FindMenuAssign(body, rtx::sig::kMenuSnapSpan);
    } __except (EXCEPTION_EXECUTE_HANDLER) { m = rtx::sig::MenuAssign{}; }
    const std::uint64_t a = m.ok ? (std::uint64_t)((std::int64_t)snap + m.target) : 0;
    if (a && a > g_base && a < g_modEnd) { g_assign = (Assign_t)a; g_siteCall = snap + m.site; g_siteSlot = m.slot; }
    Log("slot assign helper: %s", g_assign ? "found" : "NOT FOUND (left-click lift unavailable)");
    if (g_assign) Log("  assign rva 0x%llx, left-click site rva 0x%llx, slot 0x%x", (unsigned long long)(a - g_base),
                      (unsigned long long)(g_siteCall - g_base), g_siteSlot);
}

// Moves the rule's top-ranked row to the top of +0x90 (a permutation of whole records, refcount
// neutral). Returns true when a row moved.
bool RotateRuleTop(std::uint64_t mgr) {
    if (!g_share || !g_share->pinCount || !g_recordOk) return false;
    std::uint64_t begin = 0, e = 0;
    if (!Rd(mgr + 0x90, &begin, 8) || !Rd(mgr + 0x98, &e, 8) || !begin || e <= begin) return false;
    const std::uint64_t span = e - begin;
    if (span % kRecSize || span > kRecSize * rtx::menu::kMaxEntries) return false;
    const int n = (int)(span / kRecSize);
    if (n < 2) return false;

    unsigned char recs[rtx::menu::kMaxEntries][kRecSize];
    int  rank[rtx::menu::kMaxEntries];
    bool fixedSlot[rtx::menu::kMaxEntries];
    int  decoded = 0;
    if (!RankLane(begin, n, recs, rank, fixedSlot, &decoded) || decoded != n) return false;

    int order[rtx::menu::kMaxEntries];
    const int best = RuleOrder(begin, n, rank, fixedSlot, order);
    if (best < 0) return false;                              // no rule row, or already in order

    __try {
        for (int k = 0; k < n; ++k)
            std::memcpy((void*)(begin + (std::uint64_t)(n - 1 - k) * kRecSize), recs[order[k]], kRecSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_share->promoState = rtx::menu::kPromoWriteFailed;
        return false;
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
    return true;
}

// Replaces the snapshot's `call assign(mgr+0x13e0, src)`. The snapshot has just sorted +0x90 and
// points src at its top (or second) record, and the click is processed later in the same call from
// these slots. Rotating the list here, before the three slot assigns, makes the game itself copy the
// rule's row into every slot and run it on click. src is an address inside +0x90 and the rotation
// keeps addresses, so passing it through unchanged picks the same position.
void __fastcall Hook_Assign13e0(std::uint64_t slot, std::uint64_t src) {
    const std::uint64_t mgr = slot - g_siteSlot;
    std::uint64_t begin = 0, e = 0;
    if (g_share && g_share->enable && Rd(mgr + 0x90, &begin, 8) && Rd(mgr + 0x98, &e, 8) &&
        src >= begin && src < e)
        RotateRuleTop(mgr);
    g_assign(slot, src);
}

// Patches the call site on the game thread (called from the snapshot detour before the original
// runs, so the site is never mid-execution while its rel32 is rewritten).
void PatchAssignSite() {
    static bool tried = false;
    if (tried || g_sitePatched || !g_siteCall || !g_assign || !g_recordOk) return;
    tried = true;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const std::uint64_t gran = si.dwAllocationGranularity;
    const std::uint64_t home = g_siteCall & ~(gran - 1);
    for (std::uint64_t d = gran; d < 0x7FF00000ull && !g_siteTramp; d += gran) {
        if (home > d)
            g_siteTramp = VirtualAlloc((void*)(home - d), 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_siteTramp)
            g_siteTramp = VirtualAlloc((void*)(home + d), 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    if (!g_siteTramp) { Log("left-click site: no trampoline memory"); return; }
    const std::int64_t rel = (std::int64_t)(std::uint64_t)(std::uintptr_t)g_siteTramp - (std::int64_t)(g_siteCall + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) { Log("left-click site: trampoline out of range"); return; }
    unsigned char t[12] = { 0x48,0xB8, 0,0,0,0,0,0,0,0, 0xFF,0xE0 };   // mov rax, imm64; jmp rax
    const std::uint64_t fn = (std::uint64_t)&Hook_Assign13e0;
    std::memcpy(t + 2, &fn, 8);
    std::memcpy(g_siteTramp, t, sizeof(t));
    DWORD old = 0;
    // written once: from here on it only runs, so it is not left writable
    VirtualProtect(g_siteTramp, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), g_siteTramp, sizeof(t));
    if (!VirtualProtect((void*)g_siteCall, 5, PAGE_EXECUTE_READWRITE, &old)) { Log("left-click site: protect failed"); return; }
    std::memcpy(&g_siteRel, (void*)(g_siteCall + 1), 4);
    const std::int32_t r32 = (std::int32_t)rel;
    std::memcpy((void*)(g_siteCall + 1), &r32, 4);
    VirtualProtect((void*)g_siteCall, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)g_siteCall, 5);
    g_sitePatched = true;
    Log("left-click site: patched");
}

void UnpatchAssignSite() {
    if (!g_sitePatched) return;
    DWORD old = 0;
    if (VirtualProtect((void*)g_siteCall, 5, PAGE_EXECUTE_READWRITE, &old)) {
        std::memcpy((void*)(g_siteCall + 1), &g_siteRel, 4);
        VirtualProtect((void*)g_siteCall, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)g_siteCall, 5);
        g_sitePatched = false;
    }
    // The trampoline stays allocated: a game thread could still be returning through it.
}

// The live records against the layout this file knows: every row with a live refcount decodes a
// verb through its control block, and its display object carries a class tag in the image whose
// ordinal is an entity type. Judged over the first ten rows and again as menus come; a layout that
// fails keeps every write off.
void RecordCheck(std::uint64_t mgr) {
    std::uint64_t begin = 0, count = 0;
    if (!EntryVec(mgr, begin, count) || count < 2) return;
    ++g_menusSeen;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint64_t rec = begin + i * kRecSize;
        std::uint64_t obj = 0;
        std::uint32_t hdr[4] = { 0, 0, 0, 0 };
        if (!Rd(rec, &obj, 8) || !obj || !Rd(obj, hdr, sizeof(hdr)) || !hdr[2]) continue;   // mid-teardown rows are not judged
        ++g_rowsSeen;
        char verb[rtx::menu::kVerbLen];
        bool hp = false;
        if (ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) > 0) ++g_rowsVerb;
        std::uint64_t tag = 0;
        std::int32_t prio = 0, ord = -1;
        if (EntryTag(rec, tag, prio) && tag > g_base && tag < g_modEnd && prio >= 0 && prio < 5000 && Rd(tag + 0x44, &ord, 4) && ord >= 0 && ord < 32) ++g_rowsTag;
        else g_lastBadOrd = ord;
    }
    if (g_rowsSeen < 10) return;
    const bool ok = g_rowsVerb * 20 >= g_rowsSeen * 19 && g_rowsTag * 20 >= g_rowsSeen * 19;
    const bool changed = !g_recordJudged || ok != g_recordOk;
    g_recordJudged = true;
    g_recordOk = ok;
    if (!changed) return;
    char exp[24], got[32], detail[600];
    std::snprintf(exp, sizeof(exp), "%u", g_rowsSeen);
    std::snprintf(got, sizeof(got), "%u/%u", g_rowsVerb, g_rowsTag);
    if (ok) {
        std::snprintf(detail, sizeof(detail), "%s%u rows over %u menus: verbs at control block +0x38 readable, class tags in the image with ordinals 0..31; %s",
                      g_staticKind[0] ? "MOVED: " : "", g_rowsSeen, g_menusSeen, g_static);
        SayCheck("OK", g_staticKind, exp, got, "", detail);
    } else {
        std::snprintf(detail, sizeof(detail), "FORMAT: %u of %u rows decode a verb, %u carry a class tag (last ordinal read %d); reorder and left-click lift off; %s",
                      g_rowsVerb, g_rowsSeen, g_rowsTag, g_lastBadOrd, g_static);
        SayCheck("FAIL", "format", exp, got, "", detail);
    }
}

// The displacements the hits carry, read before Detours patches anything: hover-target vector
// begin and end, snapshot counter and base, the game state field, the language index field.
bool ReadHitDisps(std::uint64_t clearHit, std::uint64_t snapHit, std::uint64_t execHit, std::uint64_t init, std::uint32_t d[6]) {
    __try {
        d[0] = clearHit ? rtx::sig::HitDisp((const unsigned char*)clearHit, rtx::sig::kMenuClearBeginAt) : 0;
        d[1] = clearHit ? rtx::sig::HitDisp((const unsigned char*)clearHit, rtx::sig::kMenuClearEndAt) : 0;
        d[2] = snapHit ? rtx::sig::HitDisp((const unsigned char*)snapHit, rtx::sig::kMenuSnapCounterAt) : 0;
        d[3] = snapHit ? rtx::sig::HitDisp((const unsigned char*)snapHit, rtx::sig::kMenuSnapBaseAt) : 0;
        d[4] = execHit ? rtx::sig::HitDisp((const unsigned char*)execHit, rtx::sig::kMenuExecStatusAt) : 0;
        d[5] = init ? rtx::sig::HitDisp((const unsigned char*)init, rtx::sig::kMenuInitLanguageAt) : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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
        RecordCheck(mgr);            // the layout gate: nothing below writes until the records pass
        ApplyOrder(mgr);             // before the original: it is about to read the top record
        PromotePinnedEntry(mgr);
        LogLaneTops(mgr, "snap");
    }
    if (mgr && g_share && g_share->enable) {
        // a pinned rule wants the lift, and this build has no slot-assign helper to carry it
        if (!g_assign && g_share->pinCount && g_share->promoState == rtx::menu::kPromoIdle)
            g_share->promoState = rtx::menu::kPromoNoAssign;
        PatchAssignSite();
    }
    const std::uint64_t r = g_origSnap(mgr);
    // The left-click lift runs inside the snapshot, at its slot assign (Hook_Assign13e0).
    if (mgr) DumpHoverSlots(mgr);
    // Publish here on the game thread; doing it from Poll() races the +0x90 rebuild.
    if (mgr && g_share && g_share->enable) Publish(mgr);
    return r;
}

// Diagnostic: log every executed menu row with its caller, so a left-click that runs a different
// row than the default slot shows up in the log.
typedef void(__fastcall* Exec_t)(std::uint64_t, std::uint64_t, std::uint64_t);
Exec_t g_origExec = nullptr;
int g_execLog = 0;

void DescribeRec(std::uint64_t rec, char* out, std::size_t cap) {
    out[0] = 0;
    std::uint64_t obj = 0, disp = 0, tag = 0;
    std::int32_t prio = 0;
    char verb[rtx::menu::kVerbLen] = { 0 };
    bool hp = false;
    __try {
        if (Rd(rec, &obj, 8) && obj) ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp);
        if (Rd(rec + 8, &disp, 8) && disp && Rd(disp + kDispTag, &tag, 8) && tag) Rd(tag + kTagPrio, &prio, 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    std::snprintf(out, cap, "\"%s\" p%d disp=%llx cls=%llx", verb, (int)prio,
                  (unsigned long long)disp, (unsigned long long)(tag ? tag - g_base : 0));
}

void __fastcall Detour_Exec(std::uint64_t mgr, std::uint64_t rec, std::uint64_t pos) {
    if (g_execLog < 200) {
        ++g_execLog;
        char a[160], b[160];
        DescribeRec(rec, a, sizeof(a));
        DescribeRec(mgr + 0x13E0, b, sizeof(b));
        const std::uint64_t ret = (std::uint64_t)_ReturnAddress();
        Log("[exec] caller rva 0x%llx rec=%s%s | slot13e0=%s",
            (unsigned long long)(ret - g_base), a,
            rec == mgr + 0x13E0 ? " (slot 13e0)" : rec == mgr + 0x1400 ? " (slot 1400)" :
            rec == mgr + 0x17B0 ? " (deferred 17b0)" : "", b);
    }
    g_origExec(mgr, rec, pos);
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
    // clear, snapshot and executor are matched inside their bodies: hook the functions holding them
    const std::uint64_t clearHit = Scan(kClear, kClearMask, sizeof(kClear));
    const std::uint64_t snapHit  = Scan(kSnap,  kSnapMask,  sizeof(kSnap));
    const std::uint64_t execHit  = Scan(kExec,  kExecMask,  sizeof(kExec));
    const std::uint64_t clear = rtx::codefind::FunctionStart(g_base, clearHit);
    const std::uint64_t build = Scan(kBuild, kBuildMask, sizeof(kBuild));
    const std::uint64_t snap  = rtx::codefind::FunctionStart(g_base, snapHit);
    const std::uint64_t exec  = rtx::codefind::FunctionStart(g_base, execHit);
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
        SayCheck("FAIL", "gone", "hits", "0", "", "GONE: neither the menu string-init nor the clear() pattern was found; menu swaps and the menu panel off");
        return false;
    }
    // The displacements the hits carry against the compiled ones (read before Detours patches
    // anything): the hover-target vector is taken from its hit, the rest is reported.
    {
        std::uint32_t d[6] = {};
        ReadHitDisps(clearHit, snapHit, execHit, init, d);
        char s[400]; std::size_t at = 0; bool moved = false;
        auto note = [&](const char* what, std::uint32_t hit, std::uint32_t compiled) {
            if (!hit || hit == compiled) return;
            moved = true;
            if (at < sizeof(s) - 48) at += (std::size_t)std::snprintf(s + at, sizeof(s) - at, "%s%s 0x%x in the hit, compiled 0x%x", at ? "; " : "", what, hit, compiled);
        };
        note("hover-target vector", d[0], rtx::sig::kMenuTargetsBegin);
        if (d[0] && d[1] == d[0] + 8 && d[0] < 0x4000) { g_tgtBegin = d[0]; g_tgtEnd = d[1]; }
        note("snapshot counter", d[2], rtx::sig::kMenuSnapCounter);
        note("snapshot base", d[3], rtx::sig::kMenuSnapBase);
        note("game state field", d[4], rtx::md::kStatus);
        note("language index field", d[5], rtx::md::kLanguage);
        g_staticKind = moved ? "moved" : "";
        std::snprintf(g_static, sizeof(g_static), "%s", moved ? s : "the hits name the compiled displacements");
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (init)  { g_origInit  = (Init_t)init;   DetourAttach(&(PVOID&)g_origInit,  (PVOID)Detour_Init); }
    if (clear) { g_origClear = (Clear_t)clear; DetourAttach(&(PVOID&)g_origClear, (PVOID)Detour_Clear); }
    if (build) { g_origBuild = (Build_t)build; DetourAttach(&(PVOID&)g_origBuild, (PVOID)Detour_Build); }
    if (snap)  ResolveAssign(snap);     // read the original body before Detours patches its first bytes
    if (snap)  { g_origSnap  = (Snap_t)snap;   DetourAttach(&(PVOID&)g_origSnap,  (PVOID)Detour_Snap); }
    Log("executor   : %s", exec ? "found" : "NOT FOUND");
    if (exec)  { g_origExec  = (Exec_t)exec;   DetourAttach(&(PVOID&)g_origExec,  (PVOID)Detour_Exec); }
    if (DetourTransactionCommit() != NO_ERROR) {
        g_origInit = nullptr; g_origClear = nullptr;
        Log("Detour commit failed - nothing hooked.");
        SayCheck("FAIL", "gone", "hooks", "0", "", "GONE: the menu hooks did not attach; menu swaps and the menu panel off");
        return false;
    }
    g_installed = true;
    if (g_assign && g_siteSlot != rtx::sig::kMenuLeftClickSlot) {
        const std::size_t at = std::strlen(g_static);
        if (at < sizeof(g_static) - 64) std::snprintf(g_static + at, sizeof(g_static) - at, "%sleft-click slot 0x%x in the snapshot, compiled 0x%x (the snapshot's in use)", g_staticKind[0] ? "; " : "", g_siteSlot, rtx::sig::kMenuLeftClickSlot);
        g_staticKind = "moved";
    }
    {
        char d[520];
        std::snprintf(d, sizeof(d), "%s%s%s; the live records are judged on the first menus", g_staticKind[0] ? "MOVED: " : "", g_static,
                      !snap ? "; no snapshot routine: left-click lift off" : !g_assign ? "; no slot-assign helper: left-click lift off" : "");
        SayCheck("SKIP", g_staticKind, "rows", "-", "open a right-click menu", d);
    }
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

// Re-create the share under the current session names. The hooks stay where
// they are; only the published view moves. The old view is left mapped because
// the detours write through it from their own threads.
void Rebind() {
    static std::uint32_t s_gen = 0;
    if (!rtx::ipc::SessionChanged(s_gen)) return;
    if (!g_installed) return;              // nothing published yet

    rtx::menu::Share* fresh = MapShare();
    if (!fresh) return;
    fresh->magic = rtx::menu::kMagic;
    fresh->version = rtx::menu::kVersion;
    fresh->pid = GetCurrentProcessId();
    fresh->flags = rtx::menu::kFlagHooked;
    fresh->enable = 0; fresh->seq = 0; fresh->count = 0;
    fresh->pinSeq = 0; fresh->pinCount = 0;
    for (int i = 0; i < 4; ++i) { fresh->diag[i] = 0; fresh->stage[i] = 0; }
    for (auto& l : fresh->lane) l = 0;
    fresh->lastTarget[0] = 0; fresh->lastTargeted = 0;
    fresh->lastVerb[0] = 0;
    fresh->promoState = rtx::menu::kPromoIdle;
    fresh->promoPrio = 0; fresh->promoPartnerPrio = 0; fresh->promoVerb[0] = 0;
    fresh->promoSource = rtx::menu::kPromoSrcNone;

    g_share = fresh;                       // publish last, fully built
    Log("session: menu share rebound");
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_checkMu);
    if (!g_check[0] || cap == 0) return false;
    std::snprintf(out, cap, "%s", g_check);
    g_check[0] = 0;
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
        // 16-byte records = { control block*, entry* (target string first) }; both pointers are followed.
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
    if (g_origExec)  DetourDetach(&(PVOID&)g_origExec,  (PVOID)Detour_Exec);
    UnpatchAssignSite();
    DetourTransactionCommit();
    g_installed = false;
}

}  // namespace rtx::menuprobe
