// Right-click menu probe (see MenuProbe.h).
//
// --- what is already established -----------------------------------------------------------
// The menu manager was located by xreffing the per-LANGUAGE POINTER TABLE rather than the
// strings themselves. Earlier work searched for LEA xrefs to "Cancel"/"Walk here", found none,
// and concluded static analysis was a dead end; the client reaches those through
// `(&PTR_s_Cancel_...)[lang]`, so the reference is on the table slot, not the string.
//
//   string-init  rva 0x161C10   caches the localized labels into mgr+0x10 .. +0x60
//   clear(bool)  rva 0x164880   destructs entries, then sets end = begin
//   add path     rva 0x160AB0
//   finalize     rva 0x1661E0   computes (end - begin) / 0x2E8
//
//   mgr + 0x1380 / +0x1388, stride 0x2E8 = the HOVER TARGET list, one element per entity under
//   the cursor. It was the obvious first guess (clear() gives it the only per-element
//   destructor) and a live dump disproved it: a bank-booth right-click produced elements named
//   "Bank booth", not the six menu verbs.
//
//   mgr + 0x0090 / +0x0098 = THE MENU ENTRIES, 16 bytes each. Established from live counts:
//   a 6-option bank booth gave span 96 (= 6), a 3-option plant gave 48 (= 3). The record holds
//   no inline text, so the label is behind a pointer - which is what the dump now chases.
//
//   Two parallel lists track subsets of the same menu (bank booth numbers in brackets):
//     mgr + 0x0d30  5 of 6  - everything except Cancel
//     mgr + 0x06e0  4 of 6  - the entries that have a target
//   Both are unconfirmed readings of one sample; treat them as leads, not facts.
//
//   RECORD (16 bytes) = { q0 -> action object, q1 -> target string }
//   ACTION OBJECT:
//       +0x08  refcount (one object is held by several of these vectors at once, 3..6 seen)
//       +0x0c  always 1
//       +0x20  EASTL string: the TARGET, e.g. "<col=00ffff>Bank booth". Empty for Walk here
//              and Cancel.
//       +0x38  EASTL string: the VERB, e.g. "Bank" / "Load Last Preset from" / "Examine".
//
//   THE ARRAY IS STORED IN REVERSE DISPLAY ORDER. Verified against a bank booth whose six
//   options draw as Bank / Collect / Load Last Preset from / Walk here / Examine / Cancel and
//   whose records read [0]=Cancel [1]=Examine [2]=Walk here [3]=Load Last Preset from
//   [4]=Collect [5]=Bank. index 0 is the BOTTOM line.
//
//   THE POLL-SIDE DUMP RACES OUR OWN WRITES. Poll() runs on the companion's thread while
//   ApplyOrder permutes the lanes on the GAME's thread, so a dump can read a half-written array:
//   observed live as a lane listing one verb twice and dropping another, and as one action
//   object reading "Travel Ports District" in one lane and "Walk here" in another - impossible
//   for a single object, so it is a torn read and not corruption. The game never sees it (our
//   write is inside its own builder hook). Do not treat poll-side lane contents as evidence;
//   LogLaneTops exists for that and runs on the game thread.
//
//   Two cautions for anything that reads this live:
//     * Overlapping entities MERGE into one menu (a booth under a clerk gave 11 records), so
//       the count is not the option count of any single entity.
//     * Records are visible mid-teardown - refcount 0 and a zeroed +0x18 - so a reader must
//       tolerate a partially destructed record rather than assume every slot is valid.
//
// --- what is open ---------------------------------------------------------------------------
// LEFT-CLICK / hover does NOT come from the menu manager. Reordering three of its parallel
// lists (0x90, 0x6e0, 0xd30) moved the drawn menu every time while hover kept the ORIGINAL
// option 0 - confirmed on an item (menu "Teleport" first, hover still "Wear") and an NPC (menu
// "Check Titles" first, hover still "Talk to"). Those three are ruled out; do not re-test them.
//
// It is option 0 of the decoded CONFIG, which matches an independent earlier finding that
// inventory items use inventoryOptions[0] from cache. The item config decoder is FUN_1402dcdf0
// (rva 0x2DCDF0) - the only function whose switch covers option opcodes 30-34 and the members
// overrides 150-154 - but its decompile does not settle types, so the option offsets are not
// known statically.
//
// Two empirical attempts are now RETIRED; neither should be rebuilt.
//
//   * Walking an action object's pointer fields for a block holding several of the menu's verbs
//     only ever rediscovered the action-object POOL, which is contiguous - the hits sat at a
//     fixed 0x150 pitch and the first pointer aliased the object it started from.
//
//   * The hover-target block does NOT hold an option index. It was dumped as raw words across
//     eight entities and only two fields ever vary: +0x000 and one pointer at +0x0a0. +0x000 is
//     the ENTITY HANDLE - packed (x<<16)|y for world objects (Menaphos read 3226..3236 /
//     2777..2788, matching where the samples were taken) and a plain index for actors (an NPC
//     22514, a player 1421). +0x0a0 takes only three distinct values and is not stable per
//     entity - the same stall got two of them - so it is a recycled pool slot, not a config.
//     Everything past +0x0a8 is zero. The block says WHICH ENTITY, never which action.
//
// What has NOT been established, despite being assumed: that hover reads a lane we never move.
// The lane counters record that a write was ISSUED, not that it survived, so "lanes:
// menu+target+nocanc" was never evidence either way. LogLaneTops samples the lanes before the
// build and after our write to settle it.

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
volatile std::uint64_t g_mgr = 0;     // captured manager, written by either detour
std::uint32_t g_lastSig   = 0;        // a held-open menu dumps once, not every poll
int           g_dumps     = 0;

// Enough to map the layout several times over; after this the probe is silent for the session
// so it can ship enabled without turning into a log spammer.
constexpr int kMaxDumps = 12;

// clear() resets SEVEN vectors on the manager. 0x1380 is the only one with a per-element
// destructor, which is why it was the obvious first guess - but a live dump showed it holds one
// element per ENTITY UNDER THE CURSOR (a coloured target name and nothing else), i.e. the hover
// target list, not the menu. The verbs live in one of the others, so dump them all and let the
// data say which.
constexpr std::uint64_t kTargetStride = 0x2E8;
struct Vec { std::uint64_t begin, end; std::uint64_t stride; const char* name; };
const Vec kVecs[] = {
    { 0x1380, 0x1388, kTargetStride, "targets" },  // known: one per entity under the cursor
    { 0x0090, 0x0098, 0,     "v_0x90"   },
    { 0x03B8, 0x03C0, 0,     "v_0x3b8"  },
    { 0x06E0, 0x06E8, 0,     "v_0x6e0"  },
    { 0x0A08, 0x0A10, 0,     "v_0xa08"  },
    { 0x0D30, 0x0D38, 0,     "v_0xd30"  },
    { 0x1058, 0x1060, 0,     "v_0x1058" },
    { 0x13A0, 0x13A8, 0,     "v_0x13a0" },
    // Two more 16-byte-record vectors, from the HOVER branch of the tooltip builder
    // FUN_14012eb30: it takes their element counts ((end-begin)>>4) and hands them to a script
    // trigger. Never examined before - the dump range stopped at 0x13a0. Observation only for now;
    // do NOT add them to kLanes without evidence, because reordering a list the game positions
    // itself is exactly what caused the display/dispatch desync.
    { 0x1468, 0x1470, 0,     "v_0x1468" },
    { 0x1790, 0x1798, 0,     "v_0x1790" },
};

// Its own log, so there is nothing to grep out of the busy scene log.
std::mutex g_logMu;
void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    char path[MAX_PATH] = "C:\\rtx_menu.log";
    char home[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
    if (n > 0 && n < sizeof(home)) std::snprintf(path, sizeof(path), "%s\\rtx_menu.log", home);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    std::fprintf(f, "%s\n", msg);
    std::fclose(f);
}

// ---- patterns -----------------------------------------------------------------------------
// Every address operand is wildcarded: rel32 branches, the RIP-relative image-base load, and
// the large displacements that are really .data RVAs of the language tables. All of those move
// per build; what remains still matches EXACTLY ONCE in .text (verified against the binary).

// Manager string-init: the +0x19ad0 language-index load, the [rax*8] table index, and the run
// of stores into mgr+0x10/0x18/0x20/0x28/0x30/0x38.
const unsigned char kInit[] = {
    0x40,0x57, 0x48,0x83,0xEC,0x30, 0x48,0x8B,0x41,0x08, 0x48,0x8B,0xF9,
    0x48,0x8D,0x0D,0,0,0,0,
    0x48,0x89,0x5C,0x24,0x40, 0x48,0x8D,0x99,0,0,0,0,
    0x48,0x8B,0x90,0xD0,0x9A,0x01,0x00, 0x48,0x63,0x02,
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

// clear(). This one is self-validating: it literally contains the three constants the probe
// depends on -- mov rsi,[rcx+0x1388] (end), mov rbx,[rcx+0x1380] (begin), add rbx,0x2E8
// (stride). If a build changes any of them the pattern stops matching, so the probe goes
// quiet rather than reading a stale layout as if it were valid.
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

// The per-tick MENU builder. Found via clear()'s callers: of the three, only this one also
// touches the entry vector (+0x90 seven times, +0x98 four), and clear() demonstrably runs on
// every rebuild.
//
// An earlier attempt hooked the caller of 0x160AB0 instead. That function was found by scanning
// for code touching 0x1380/0x1388 - the HOVER TARGET vector, not the menu - so it never fired
// for menu building at all (diag[2] stayed 0 live). Do not confuse the two vectors again.
const unsigned char kBuild[] = {
    0x44,0x88,0x4C,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xC8,0x48,0x81,0xEC,0x38,0x01,
    0x00,0x00,0x48,0x8B,0xF9,0xE8,0xE2,0xBF,0x5A,0x00,0x48,0x8B,0xC8,0xE8,0xAA,0xC5,0x5A,0x00,
    0x48,0x8B,0x47,0x08,0x4C,0x8B,0x3D,0x6F,0x48,0xBF,0x00,0x4C,0x8B,0xA0,0x58,0x98,0x01,0x00,
    0x4C,0x8B,0xA8,0x28,0x8D,0x01,0x00,0x48,0x8B,0xB0,0x18,0x8D,0x01,0x00,0x48,0x8B,0x98,0x40,
    0x8D,0x01,0x00,0x4C,0x89,0xA5,0x80,0x00,0x00,0x00,0x4C,0x89,0x6C,0x24,0x30,0xE8,0x42,0xC4,
    0x71,0x00,0x4C,0x8B,0xF0,0xE8,0x1E,0xC4,0x71,0x00,0x49,0x81,0xFE,0x80,0x96,0x98,0x00,
};
const unsigned char kBuildMask[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,0,0,0,0,
};
static_assert(sizeof(kBuild) == sizeof(kBuildMask), "build pattern/mask length mismatch");

// ---- the snapshot function ------------------------------------------------------------------
// FUN_14012d660 (rva 0x12d660 in the live build) is what actually decides the default action. It
// copies the LAST record of the +0x90/+0x98 menu vector - the TOP of the displayed menu - into
// the hover fields, via a smart-pointer assign taking the field ADDRESS:
//
//     FUN_140029610(mgr + 0x13e0, <second-from-top record>);
//     FUN_140029610(mgr + 0x13f0, begin + (span & ~0xF) - 0x10);   // top record
//
// Passing the address is why a disp32 scan for 0x13F8 found only a constructor and two readers
// and no writer: the store happens inside the helper, through [rcx].
//
// So the default IS taken from the top of our menu - the reorder was simply running too late.
// Hooking this at ENTRY and reordering there means its own snapshot picks up our order, which
// needs no code patching at all. The menu vector is already built when it runs, since it reads
// +0x90/+0x98 itself.
//
// Pattern: the prologue through the first few body instructions. The two large displacements are
// wildcarded (they are structure offsets that move between builds); what is left is the register
// save sequence and the stack frame, which is stable.
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
        for (std::uint64_t i = 0; i + n < ts; ++i) {
            if (b[i] != pat[0]) continue;
            bool ok = true;
            for (std::size_t j = 1; j < n; ++j)
                if (mask[j] && b[i + j] != pat[j]) { ok = false; break; }
            if (ok) return tb + i;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// Find every instruction whose disp32 is 0x000013F8 - the hover slot. Writing the slot from the
// builder hook is NOT enough: the log shows our value replaced on every single tick (five
// consecutive writes of the same object, where a write that stuck would log once and then find
// the slot already ours). Something repopulates it after the builder returns, so the fix is to
// run after THAT, which means identifying it first.
//
// A raw displacement scan rather than a pattern: the writer is unknown, and disp32 0x13F8 is
// distinctive enough in .text to enumerate by hand. Logs the bytes around each hit so the
// addressing form (store vs load vs lea) can be read off directly.
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
            // Match the whole sub-struct, not just the slot. Ghidra shows 0x13c4..0x1420 is one
            // contiguous block, so the per-tick write almost certainly goes through a POINTER to
            // its base with a small displacement - invisible to a 0x13F8-only scan, which is why
            // that scan found only a constructor and readers and no writer.
            if (b[i+2] != 0x00 || b[i+3] != 0x00) continue;
            const unsigned int disp = (unsigned int)b[i] | ((unsigned int)b[i+1] << 8);
            if (disp < 0x13C0 || disp > 0x1420) continue;
            // Keep only ModRM disp32 forms: the byte before the displacement must encode mod=10.
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

// SEH-guarded read, kept free of C++ objects (MSVC forbids __try where unwinding is needed).
// Nothing in this file writes to the client.
bool Rd(std::uint64_t a, void* out, std::size_t n) {
    __try { std::memcpy(out, (const void*)a, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Printable(const char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if ((unsigned char)p[i] < 0x20 || (unsigned char)p[i] > 0x7E) return false;
    return true;
}

// EASTL short-string optimisation: 24 bytes, and the LAST byte holds the remaining capacity
// (0x17 when empty) with the high bit set once the string has spilled to the heap. The add path
// initialising a fresh entry string with `[0x37] = 0x17` is what pins this.
// Returns the text length, or -1 when `s` is not a string.
int ReadEastl(std::uint64_t s, char* out, std::size_t cap, bool* heap) {
    unsigned char raw[0x18];
    if (!Rd(s, raw, sizeof(raw))) return -1;
    const unsigned char flag = raw[0x17];
    if (!(flag & 0x80)) {                       // inline
        if (flag > 0x17) return -1;
        const int len = 0x17 - flag;
        if (len <= 0 || (std::size_t)len >= cap) return -1;
        if (!Printable((const char*)raw, (std::size_t)len)) return -1;
        if (raw[len] != 0) return -1;           // inline strings stay NUL-terminated
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

// ---- live publish + swap ------------------------------------------------------------------
rtx::menu::Share* g_share = nullptr;

constexpr std::uint64_t kEntriesBegin = 0x90;   // the MENU entries (not 0x1380, see above)
constexpr std::uint64_t kEntriesEnd   = 0x98;
constexpr std::uint64_t kRecSize      = 16;
// The record's SECOND qword is the entry's own target string. The action object also carries a
// target at +0x20, and for a single-entity menu the two agree - which is why reading the wrong
// one went unnoticed. They DIVERGE when entities overlap and their menus merge: three NPCs in
// one menu had Follow/Examine attributed to the wrong names. Always read the record's.
constexpr std::uint64_t kRecTarget    = 8;      // record + 8 -> target string
constexpr std::uint64_t kObjTarget    = 0x20;   // the action object's own copy (do not use)
constexpr std::uint64_t kObjVerb      = 0x38;

// The record's second qword is also the DISPLAY object (the target string sits at its +0x00,
// the verb at its +0x18 - i.e. display = base + 0x20). Its +0x38 holds the action-class TAG: a
// shared global, one per action type, carrying the class priority at +0x40.
//
// The snapshot re-sorts the menu by that priority before it takes the default: every entry with
// priority < 1000 is pulled OUT of the vector and re-inserted at the TOP, in order (the
// partition loop and the two vector-insert calls in FUN_14012d650; the comparator FUN_1400dfdc0
// states the same 1000 split). That is exactly why a pin among ordinary verbs holds while Drop
// and Examine never rise: the promoted class is lifted above them again on every tick.
constexpr std::uint64_t kDispTag  = 0x38;
constexpr std::uint64_t kTagPrio  = 0x40;
constexpr std::int32_t  kPromoted = 1000;

// Defined below, next to the promotion code; Publish needs it to stamp each entry's type.
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

// Publish in DISPLAY order: the array is stored bottom-line-first, and flipping once here keeps
// every consumer from having to know that.
// LATCHES the last non-empty menu. A right-click menu closes the moment the cursor leaves it,
// so clearing on empty would blank the panel before you could reach it - which made the list
// impossible to interact with. Keeping the last capture means you can right-click, let the menu
// go, and then take your time choosing.
// ---- hover-target dump --------------------------------------------------------------------
// The hover verb is NOT text in the hover-target block: 744 bytes hold exactly ONE EASTL string
// (the entity name) and no verb. So the block stores an option INDEX that is resolved against
// the config when the tooltip is drawn - which is why reordering the manager's lists moved the
// menu and never the hover.
//
// If that index is here, it can be written exactly like the menu permutation, and it fixes the
// tooltip and the left click together - no config patching, no allocation.
//
// This lane was never tested for it. It was twice mistaken for the MENU vector and then avoided
// on that account, so "eliminated" only ever covered +0x90 / +0x6e0 / +0xd30.
//
// Raw words, because the field is an integer and there is nothing to match on by name. Hover two
// entities whose default action differs and diff the two dumps: the option index is a small
// value that tracks the default, next to the entity type and id.
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
            // +0x80 is the name string; its bytes are text, not values worth reading as words.
            if (o >= 0x80 && o < 0x98) continue;
            // 0x2E8 is not a multiple of 32, so the last row is short - print only what was
            // actually read rather than eight words with stale zeros on the end.
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

    // STAGE, then COMMIT. Everything is gathered into locals and copied into the share only
    // once the capture is ACCEPTED. The first version filled g_share->entries in place and then
    // bailed on the acceptance guards - which left the share holding the REJECTED menu's rows
    // under the previous capture's count. Seen live on empty ground: Walk here + Cancel written
    // over a 3-row latch produced a phantom third row, the stale bottom entry - a second
    // "Cancel", slot 0, drawn "fixed by the game" twice.
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
        {   // the class's ordinal is the entity type; the panel keys rules differently per type
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
    // Count the entries that carry a target, and note the object they belong to.
    std::uint32_t targeted = 0;
    const char* first = nullptr;
    for (std::uint32_t k = 0; k < out; ++k)
        if (stage[k].target[0]) {
            ++targeted;
            if (!first) first = stage[k].target;
        }
    if (!targeted) return;                          // empty ground: Cancel + Walk here only

    // Same object, fewer options = the menu decaying as the cursor leaves it. Keep the fuller
    // capture; a different object always wins.
    const bool sameObject = first && g_share->lastTarget[0] &&
                            std::strncmp(first, g_share->lastTarget, rtx::menu::kTargetLen) == 0;
    if (sameObject && targeted < g_share->lastTargeted) return;

    // Accepted: commit the whole capture, handle included. The handle is committed HERE, with
    // the entries it belongs to, not kept-last-nonzero - or an item menu inherits the last
    // world object's handle and its rule silently keys on a stale identity.
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

// Strip colour tags: the record's target reads "<col=00ffff>Event noticeboard" but a rule is
// written against the plain name, so both sides have to compare the same thing.
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

// Reorder the entries to match the rule list, in the DRAWN menu.
//
// The array is rebuilt every tick, so this cannot be a one-shot: it runs from inside the
// builder hook, right after the client has finished producing the list, and therefore applies
// to every frame the menu is drawn.
//
// Permuting whole 16-byte records is REFCOUNT-NEUTRAL: every pointer inside stays owned exactly
// once, just at a different index. Nothing is allocated, nothing freed.
//
// Display order is the REVERSE of storage, so "pull to the top of the menu" means "move to the
// end of the array". The reorder is STABLE: anything not pinned keeps its relative order.
// The parallel lists clear() resets, all holding the same {action-object, target} records.
// 0x90 is the drawn menu. The others are subsets - for a 6-option bank booth: 0xd30 had 5
// (all but Cancel), 0x6e0 had 4 (only those with a target), 0x13a0 had 3.
//
// The LEFT-CLICK / hover action does not come from 0x90: reordering it moved the menu while
// hover still read the old verb. It is read through the hover slot the drainer fills, so one of
// these siblings is the likely source. Reorder them all and record which ones actually change,
// so the source is identified from evidence rather than picked.
struct Lane { std::uint64_t begin, end; int stat; const char* name; };
// ONLY the drawn menu and the source that can replace it. Nothing else.
//
// The parallel lanes (+0x6e0, +0xa08, +0xd30, +0x13a0) were reordered on the theory that hover
// read one of them. That is disproven - hover comes from FUN_14012d660's snapshot of the TOP of
// +0x90 - and permuting them independently actively BREAKS the menu: they are different SUBSETS
// (+0x90 has Cancel, +0xd30 does not, +0x6e0 holds only entries with a target). Structural
// entries are left "in place" by rank, and "in place" is a different index in a list that
// contains Cancel than in one that does not, so the lists drift apart. The game then draws from
// one and dispatches from another: clicking row i ran row i+1, live, on both an item and a world
// object. Do not re-add them.
//
// +0x3b8 stays because FUN_14012d660 copies it OVER +0x90 wholesale when it is non-empty, which
// is the interface/inventory case - it is a source of the same menu, not a parallel subset.
const Lane kLanes[] = {
    { 0x0090, 0x0098, 0, "menu"  },
    { 0x03B8, 0x03C0, 4, "iface" },
};

// Rank each record by the rule list and mark the STRUCTURAL slots. Returns false if the lane
// is unusable. A structural row (Cancel / Walk here) must keep its exact storage INDEX, not
// merely its relative order - see the fixed-slot walk in ApplyOrder.
// The config id of whatever is under the cursor RIGHT NOW, or 0 when it is not known.
//
// The id cannot be derived here (a loc id comes from the map cache, which lives in the
// launcher), so the launcher publishes handle -> id pairs and this looks up the handle the
// manager is holding at this instant. That is what makes the poll-rate lag harmless: a table
// that still describes the previous entity simply fails the lookup, and an id-scoped pin then
// declines to match rather than applying to the wrong thing.
bool RankLane(std::uint64_t begin, int n, unsigned char recs[][kRecSize], int* rank,
              bool* fixedSlot) {
    // Does this menu have ANY targeted entry? That is what decides whether a targetless entry is
    // structural (world menu) or an ordinary option (interface menu).
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
    const std::uint32_t pins = g_share->pinCount > (std::uint32_t)rtx::menu::kMaxPins
                             ? (std::uint32_t)rtx::menu::kMaxPins : g_share->pinCount;
    for (int i = 0; i < n; ++i) {
        if (!Rd(begin + (std::uint64_t)i * kRecSize, recs[i], kRecSize)) return false;
        std::uint64_t obj = 0;
        std::memcpy(&obj, recs[i], 8);
        rank[i] = 0x7FFFFFFF;
        fixedSlot[i] = false;
        bool hp = false;
        if (!obj || ReadEastl(obj + kObjVerb, verb, sizeof(verb), &hp) <= 0) continue;
        std::strncpy(g_share->lastVerb, verb, rtx::menu::kVerbLen - 1);
        g_share->lastVerb[rtx::menu::kVerbLen - 1] = 0;
        tgt[0] = 0;
        std::uint64_t tstr = 0;
        std::memcpy(&tstr, recs[i] + kRecTarget, 8);     // already copied out with the record
        if (tstr && ReadEastl(tstr, raw, sizeof(raw), &hp) > 0) StripTags(raw, tgt);
        // Structural = the bottom entry, plus any TARGETLESS entry in a menu that HAS targets.
        //
        // Neither half alone is right. "Has a target" (the first rule) broke interface menus,
        // where World Map / Lodestone Network / Teleport Menaphos legitimately carry no target and
        // the whole menu became unreorderable. "Bottom entry only" (the second) broke world menus,
        // because the game REPOSITIONS "Walk here" when it draws - move it in the array and the
        // drawn order stops matching the dispatch order, which showed up live as Walk here running
        // Examine and Examine running Walk here.
        //
        // The two cases separate cleanly: in a world menu the targetless entries are exactly the
        // repositioned ones (Walk here, Cancel) and everything else has a target; in an interface
        // menu NOTHING has a target, so there is nothing to distinguish and only Cancel is fixed.
        if (i == 0 || (anyTargeted && !tgt[0])) { fixedSlot[i] = true; continue; }
        for (std::uint32_t p = 0; p < pins; ++p) {
            if (std::strncmp(verb, g_share->pins[p].verb, rtx::menu::kVerbLen) != 0) continue;
            if (g_share->pins[p].target[0] &&
                std::strncmp(tgt, g_share->pins[p].target, rtx::menu::kTargetLen) != 0) continue;
            rank[i] = (int)p;
            ++g_share->stage[1];
            break;
        }
    }
    // TOP-VERB GATE. A rule is a FULL order captured from one menu; the same name can present a
    // different menu elsewhere (an item's backpack vs bank vs worn menus). Applied there, the
    // only stored verb that exists is often Examine, which ranked alone and rose to the top --
    // the owner-reported "everything forces Examine" on an item that had a rule. A rule's first
    // pin is its author's top choice: if THAT verb is not in this menu, this is not the menu the
    // rule was written for, and every pin of that target is ignored rather than applied in part.
    // A rule whose author genuinely put Examine first still applies everywhere, which is what
    // putting Examine first means.
    {
        bool pinMatched[rtx::menu::kMaxPins] = {};
        for (int i = 0; i < n; ++i)
            if (rank[i] != 0x7FFFFFFF) pinMatched[rank[i]] = true;
        for (int i = 0; i < n; ++i) {
            if (rank[i] == 0x7FFFFFFF) continue;
            const char* rt = g_share->pins[rank[i]].target;
            int first = -1;
            for (std::uint32_t p = 0; p < pins; ++p)
                if (std::strncmp(rt, g_share->pins[p].target, rtx::menu::kTargetLen) == 0) { first = (int)p; break; }
            if (first >= 0 && !pinMatched[first]) rank[i] = 0x7FFFFFFF;
        }
    }
    return true;
}

// Record count of a lane, and its base. Shared by the hover slot and the lane logging.
int LaneCount(std::uint64_t mgr, const Vec& v, std::uint64_t& begin) {
    std::uint64_t e = 0;
    if (!Rd(mgr + v.begin, &begin, 8) || !Rd(mgr + v.end, &e, 8)) return 0;
    if (!begin || e <= begin) return 0;
    const std::uint64_t span = e - begin;
    if (span % kRecSize || span > kRecSize * rtx::menu::kMaxEntries) return 0;
    return (int)(span / kRecSize);
}

// HOVERING IS THE GESTURE, not right-clicking: the builder runs every tick, so simply resting
// the cursor on something populates every lane, and it captures them at the moment the tooltip
// resolves its verb. A right-click is not needed and tells us less.
//
// That means a sample must be deduped by COMPOSITION. A hover is held for many ticks, so an
// undeduped budget is spent entirely on the first thing hovered and never reaches the second
// case. `pre` and `post` are tracked separately so a frame where our write changes the order
// still records both halves.
// The hover slot at mgr+0x13F8 is NOT written directly. An earlier attempt did exactly that and
// it was wrong twice over: the snapshot rewrites both 0x13f0 and 0x13f8 as a PAIR through the
// engine's own smart-pointer assign, so writing one of them alone can desync the pair; and it is
// unnecessary, because reordering before the snapshot makes it choose our entry by itself.
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
        if (!RankLane(begin, n, recs, rank, fixedSlot)) continue;

        // A structural row keeps its exact storage INDEX; the movable records permute among the
        // movable slots only. Keeping structural rows merely in RELATIVE order is not enough:
        // ranked-block-on-top pulled a pinned entry ACROSS "Walk here" whenever the pin sat
        // below it in the default menu, which shifted Walk here's index by one. The menu DREW
        // that order faithfully, but the game imposes its own placement of the targetless rows
        // on the DISPATCH side - seen live at a Shifting tombs entrance, where ranking Examine
        // (storage 1, below Walk here at 2) drew Examine above Walk here and clicking either one
        // ran the other. Every earlier test pinned verbs already above Walk here, so the
        // relative-order version passed by accident.
        //
        // Display order is the REVERSE of storage, so the movable slots are walked top-of-menu
        // first: ranked entries fill them in rule order, then everything unranked in its
        // existing display order.
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

        __try {
            for (int j = 0; j < m; ++j)
                std::memcpy((void*)(begin + (std::uint64_t)slots[j] * kRecSize), recs[items[j]], kRecSize);
            ++g_share->lane[lane.stat];             // which lanes actually moved
            if (lane.stat == 0) ++g_share->diag[1];
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

}

// An entry's action-class tag and its priority.
bool EntryTag(std::uint64_t rec, std::uint64_t& tag, std::int32_t& prio) {
    std::uint64_t q1 = 0;
    tag = 0; prio = 0;
    if (!Rd(rec + kRecTarget, &q1, 8) || !q1) return false;
    if (!Rd(q1 + kDispTag, &tag, 8) || !tag) return false;
    return Rd(tag + kTagPrio, &prio, 4);
}

// Give the pinned entry the PROMOTED CLASS for the duration of the snapshot, so the class
// re-sort cannot sink it.
//
// Permuting the vector is not enough for a demoted verb: the snapshot lifts the whole promoted
// class above everything else afterwards, which is why pinning Drop on a potato cactus left the
// hover default at Use while pinning Use or Activate on a cape worked (same class, so the
// permutation survived). Rather than fight the sort - which would mean hooking the re-insert on
// a hot generic container helper - hand our entry the same class the sort is about to promote:
// equal priority puts it in the same bucket, the extraction preserves relative order, and
// ApplyOrder has just placed it at the top of storage, so ours comes out on top.
//
// EQUALISING rather than out-bidding is deliberate: it does not depend on knowing which
// direction the priority compares, only on the class split the binary states outright.
//
// The priority lives on a SHARED GLOBAL (one per action type, not per entry), so this must be
// the narrowest possible window: written on the game thread immediately before the original
// runs and restored immediately after, with no menu built in between. Never leave it written.
// Give the pinned entry a PROMOTED CLASS, the way the client does it for shift-to-drop.
//
// The class is chosen at BUILD time by op index - the interface builder adds ops 1-5 with the
// promoted class and ops 6-10 with the demoted one - so Drop and Examine are demoted before the
// menu vector ever exists, and no amount of reordering afterwards can lift them.
//
// The client's own shift-to-drop does not edit any number: holding shift makes the SAME entry
// (same component, same verb) carry a different class pointer. Live A/B, one hover apart:
//     no shift   Use(p25 c..a8b0) | Drop(p1007 c..a9c0) | Examine(p1007 c..a9c0)
//     shift held Drop(p57 c..a970) | Use(p25 c..a8b0) | Examine(p1007 c..a9c0)
// Only the pointer moved, so the class carries priority and click behaviour while the op and
// component ids - the dispatch identity - live in other fields and are untouched.
//
// THE CLASS MUST BE THE GENERIC PROMOTED OP CLASS, NOT "whatever the menu's default uses".
// A first attempt copied the class of the menu's best promoted row and, on a potato cactus,
// that row was Use - so the click stopped opening the menu (the eligibility half worked) and
// cheerfully performed USE. Classes therefore carry dispatch as well as priority, and the only
// safe source is the one shift-to-drop itself picks: the generic class the builder gives
// interface ops 1-5, seen live at priority 57 immediately below the demoted one.
//
// It is DERIVED AND VERIFIED rather than hardcoded: these singletons sit in one .data array on a
// 0x50 stride (demoted 1007 at X, promoted 57 at X-0x50, live-confirmed a9c0 -> a970), and the
// candidate is only accepted if it carries EXACTLY the fingerprint the live capture showed
// (priority 57, ordinal 1). If a build ever moves them the check fails and nothing is promoted -
// the feature goes quiet instead of dispatching the wrong action.
//
// Gated to the interface demoted class (priority 1007). World menus have their own demoted
// classes (Examine 1002/1003) whose promoted counterpart is NOT established, and guessing there
// is exactly how the Use mis-dispatch happened.
constexpr std::int32_t  kDemotedIface   = 1007;

// ---- learning which class is the GENERIC op class -------------------------------------------
// The address arithmetic below is an adjacency coincidence, not a layout rule (classes in a
// family are 0x60 apart; the demoted interface class just happens to sit 0x50 above the generic
// one), so it cannot be the primary source - a game update that reorders .data would silently
// invalidate it.
//
// The build-independent signal is behavioural: the GENERIC op class is shared by every ordinary
// option, so it is seen carrying MANY DIFFERENT VERBS (Wield, Activate, Teleport, Break...),
// while an action-specific class like Use only ever carries its own verb. That is exactly the
// property that made copying Use's class dispatch Use. So: watch the menus that go past, and
// treat "promoted, right entity type, seen with at least two distinct verbs" as the generic one.
//
// Learning needs one hover of any item with two or more ordinary options (most equipment and
// food); until then the arithmetic fallback covers the cold start, and the panel says which
// source was used.
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

// The learned generic class for an entity type: promoted, and shared by the most distinct verbs.
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

// The promoted counterpart, found STRUCTURALLY and available on the very first hover - no
// learning gesture, no address arithmetic.
//
// A demoted op class and its promoted twin are two instances of ONE action type that differ only
// in rank, so they share a vtable; an action-specific class like Use is a different C++ type with
// a different vtable. That is exactly the distinction that copying Use's class got wrong, and it
// is readable straight from memory: same vtable + same entity ordinal + promoted band. Among
// those, take the highest rank in the promoted band - the generic op class sits at the top of it
// (57), above the specific actions.
//
// Everything here is verified against live memory rather than assumed: the vtable must point
// into the module, and a candidate that fails any check is skipped rather than guessed at.
std::uint64_t g_modEnd = 0;

std::uint64_t ScanCounterpart(std::uint64_t demoted, std::int32_t ord, std::int32_t& outPrio) {
    outPrio = 0;
    std::uint64_t vt = 0;
    if (!Rd(demoted, &vt, 8) || vt <= g_base || vt >= g_modEnd) return 0;
    std::uint64_t best = 0;
    std::int32_t  bestPrio = 0;
    // The family is contiguous (one array per entity type, 0x50-0x60 stride), so a window either
    // side of the demoted class covers it several times over.
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
    // Every exit reports itself: a refusal has to be readable in the panel, not only in a log.
    g_share->promoState = rtx::menu::kPromoIdle;
    g_share->promoPrio = 0;
    g_share->promoPartnerPrio = 0;
    g_share->promoSource = rtx::menu::kPromoSrcNone;
    g_share->promoVerb[0] = 0;
    if (!g_share->pinCount) return;
    std::uint64_t begin = 0, count = 0;
    if (!EntryVec(mgr, begin, count) || count < 2) return;

    // Learn from whatever menu is on screen before deciding anything: which class is generic is
    // read off the verbs that share it.
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

    // ApplyOrder has just run, so the last record is the row we want as the default.
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

    // STRUCTURAL first - it works on the first hover, with no learning gesture required. The
    // behavioural signal (the class several verbs share) is kept only as a cross-check: if it
    // has been observed AND disagrees, it is direct evidence and wins, and the panel says so.
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
    __try {
        *(volatile std::uint64_t*)(disp + kDispTag) = promoted;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_share->promoState = rtx::menu::kPromoWriteFailed;
        return;
    }
    g_share->promoState = rtx::menu::kPromoApplied;
    ++g_share->lane[5];
}

// ---- REMOVED: writing the class priority (do not reinstate) ---------------------------------
// An attempt to lift a demoted verb by writing the promoted priority onto its tag is retired.
// It was modelled on the client's own shift-to-drop, and it was the wrong model twice over:
//
//  * THE TAG IS SHARED BY SEVERAL VERBS, not one per verb. Live: an inventory item reads
//    Use(p25) Drop(p1007) Examine(p1007) - Drop and Examine are ONE class. Writing 25 onto
//    "Drop's tag" produced Drop(p25) Use(p25) Examine(p25): Examine was promoted as collateral,
//    game-wide, for as long as the write was live.
//  * IT DOES NOT MOVE THE CLICK. With the write applied the hover slots read exactly what we
//    wanted - 0x13e0 AND 0x13f0 both "Drop" - and left-click STILL opened the Choose Option
//    menu. So the click gate gets its answer from something other than these records.
//
// And the client does NOT implement shift-to-drop this way: a scan of the whole .text finds no
// instruction writing 57 (or 1007) into a +0x40 field. Under shift the Drop entry reads p57
// while Examine still reads p1007 - the two verbs no longer share a class - so the client gives
// the Drop entry a DIFFERENT action-class object rather than editing a number. The class is the
// dispatch identity, which is why the engine's version also switches the click behaviour and a
// priority edit never could.
//
// The reorder itself (ApplyOrder) stays: it works for verbs that already share the promoted
// class, which is every ordinary option. Lifting a demoted verb needs the entry to be built with
// the promoted CLASS, so the intervention point is wherever that class is chosen - not here.

// Log the full DISPLAY order of every lane. The lane counters only record that a write was
// ISSUED, never that it survived, so "lanes: menu+target+nocanc" has never been evidence that
// hover reads a lane we do not move. Sampling the same lanes before the build (what is left over
// from the previous frame) and after our write separates the two possibilities that remain:
// hover reads a lane we never touch, or it reads one we do touch and something re-sorts it.
//
// Storage is reverse display order, so this walks the records backwards.
//
// GATED ON A REAL MENU. The builder runs every tick, so an ungated budget is spent within a
// fraction of a second on empty ground - the first attempt logged 40 samples of the idle
// two-entry menu and never reached the right-click. Anything with fewer than three entries is
// Walk here + Cancel and is skipped WITHOUT consuming budget.
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
    // The snapshot sample must record even a SMALL or empty menu: "never called" and "called with
    // nothing to snapshot" are different answers for the interface case, and skipping the second
    // makes them indistinguishable. Everywhere else, a two-entry menu is idle ground and noise.
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

    // Name what is under the cursor, or the two hover cases are indistinguishable in the log.
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

// ---- hover-slot dump -----------------------------------------------------------------------
// The four 16-byte record slots at mgr+0x13e0/0x13f0/0x1400/0x1410 hold {base, display} pairs;
// display = base+0x20, with the verb at display+0x18, the target at display+0x00 and the TYPE
// int at *(display+0x38)+0x44 (1 iface / 2 ground / 3 loc / 4 npc / 7 player - the same codes
// CS2's hover-tooltip script switches on). Static analysis of the 949 build (session 2026-08-07):
// the CS2 hover-info op reads 0x13e0 OR 0x1410 by a settings byte; the snap function writes
// 0x13e0 (top-or-second of +0x90) and rebuilds the +0x1468/+0x1790 lanes from +0x90/+0x13a0
// when mgr+0x1420 is set; +0x3b8 is consumed and EMPTIED inside the snap every tick, so entry
// samples never see it. What no static path explains is where the ITEM hover default comes
// from, so this dump reads all four slots AFTER the original snap has run - the one moment the
// answer is guaranteed to be present somewhere.
int g_slotLog = 0;                        // budget, re-armed when the panel is opened

void DumpHoverSlots(std::uint64_t mgr) {
    if (g_slotLog <= 0) return;
    // Gather first, dedup on CONTENT. The first version keyed on the display POINTER, and the
    // menu's objects are reallocated every tick, so one held hover burned the whole budget on
    // 23 identical lines and the states that mattered (after a pin was set) never got captured.
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
    // The +0x90 TOP THREE (display order), post-snap: whether ApplyOrder's order SURVIVED the
    // snap's internal rebuild is the question for item menus, and only this shows it.
    std::uint64_t b90 = 0;
    const int n90 = LaneCount(mgr, kVecs[1], b90);
    // Verb + CLASS PRIORITY per row. The priority is what the snapshot sorts on (< 1000 =
    // promoted above everything else), so "Use(p12) | Drop(p1600)" reads the rule off directly -
    // and it is the one-glance test for the client's own shift-to-drop setting: turn it on, hold
    // shift, and watch whether Drop's number crosses 1000 (the engine promoting it the same way
    // we do) or stays put (some other mechanism entirely).
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
        // The OP INDEX travels beside the class, in the record itself (the builder passes both
        // to the add-entry call as separate arguments). Logging them together is what decides
        // whether the class can be swapped safely: if shift changes Drop's CLASS while its op
        // index stays put, then class carries priority/behaviour only, dispatch stays with the
        // op index, and handing a demoted entry the promoted class is exactly what the client
        // does for itself.
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

// The manager's string-init. Only a fallback capture: it runs once when the manager is built,
// which is usually before the companion attaches, so clear() is what reliably provides it.
void __fastcall Detour_Init(std::uint64_t mgr) {
    g_mgr = mgr;
    g_origInit(mgr);
}

typedef void(__fastcall* Build_t)(std::uint64_t, std::uint32_t, std::uint32_t, std::uint8_t);
Build_t g_origBuild = nullptr;

// The snapshot function: it copies the top record of the menu vector into the hover fields, so
// the default action is decided HERE, not by the builder. Reorder at ENTRY and its own snapshot
// takes our order - which is why this needs no code patch and no write to the slot.
typedef std::uint64_t(__fastcall* Snap_t)(std::uint64_t);
Snap_t g_origSnap = nullptr;

std::uint64_t __fastcall Detour_Snap(std::uint64_t mgr) {
    if (mgr) {
        g_mgr = mgr;                 // it takes the manager in rcx, same as the other hooks
        ApplyOrder(mgr);             // BEFORE the original: it is about to read the top record
        // Reordering alone cannot lift a demoted verb (Drop / Examine); the class re-sort inside
        // the original would sink it again. Hand it a promoted class first, as shift-to-drop does.
        PromotePinnedEntry(mgr);
        // Hooking this fixed world objects but NOT inventory or interface items, and no other
        // function in the binary writes the hover block - so either this does not run for an
        // interface target, or it runs and takes the branch that CLEARS the fields instead of
        // snapshotting. Sampling here says which, without guessing at the outer condition.
        LogLaneTops(mgr, "snap");
    }
    const std::uint64_t r = g_origSnap(mgr);
    // AFTER the original: the +0x90 rebuild, the hover-record snapshot and the lane rebuild
    // have all happened - the only moment the item default is guaranteed readable somewhere.
    if (mgr) DumpHoverSlots(mgr);
    // Capture for the panel HERE, on the game thread, once the rebuild is finished. Publish used
    // to run from Poll() on the companion thread, which reads +0x90 while this function is
    // emptying it into the buckets and refilling it - the panel showed a menu missing its real
    // entries and carrying TWO "Walk here" and TWO "Cancel", which is a half-rebuilt vector, not
    // the game's menu. The same race was already known to corrupt the diagnostic dump; the
    // panel's own capture had it too.
    if (mgr && g_share && g_share->enable) Publish(mgr);
    return r;
}

// The per-tick menu builder (sole caller of the add path). Reordering here rather than from the
// poll is what makes a change stick: a permutation written outside this is overwritten by the
// next rebuild.
void __fastcall Detour_Build(std::uint64_t ctx, std::uint32_t a2, std::uint32_t a3, std::uint8_t a4) {
    // Sample BEFORE the build runs: this is what the previous frame's write left behind, after
    // everything the game did with it.
    if (g_mgr) LogLaneTops(g_mgr, "pre");
    g_origBuild(ctx, a2, a3, a4);
    // Reorder AFTER the build: the array is rebuilt every tick, so anything written outside this
    // is overwritten on the next frame. ctx is the manager here (it calls clear() on itself),
    // but use the captured pointer so a signature surprise cannot send the write somewhere else.
    const std::uint64_t mgr = g_mgr;
    if (mgr) {
        ApplyOrder(mgr);
        LogLaneTops(mgr, "post");       // what we actually left in each lane
    }
}

void __fastcall Detour_Clear(std::uint64_t mgr, char flag) {
    g_mgr = mgr;                     // the reliable capture: runs on every menu open
    g_origClear(mgr, flag);
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    HMODULE gm = GetModuleHandleW(L"rs2client.exe");
    if (!gm) return false;
    g_base = (std::uint64_t)gm;
    {   // module bounds, so a candidate class's vtable can be sanity-checked before it is trusted
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
    ScanHoverSlotRefs();     // one-shot: who else touches the hover slot
    return true;
}

void Poll() {
    if (!g_installed) return;
    const std::uint64_t mgr = g_mgr;
    if (!mgr) return;
    // Publishing happens in the snapshot hook, on the game thread - see Detour_Snap. Doing it
    // from here races the menu rebuild.

    // Re-arm the dump budget whenever the panel is opened. The cap is per session, so once it is
    // spent the probe is silent until the client restarts - which cost a restart per capture, and
    // deleting the log does not reset it (the counter lives here, not in the file). The panel
    // already drives `enable` off->on when you switch to the tab, so switching away and back is
    // the re-arm gesture and no new plumbing is needed.
    //
    // kEnablePanel exactly, not any truthy enable: publishing now also runs in the background so
    // stored rules keep applying with the panel closed, and counting that as "panel opened" would
    // burn the whole dump budget on the first menu after login.
    {
        static bool wasOn = false;
        const bool on = g_share && g_share->enable == rtx::menu::kEnablePanel;
        if (on && !wasOn) {
            g_dumps   = 0;
            g_lastSig = 0;      // else a menu still on screen counts as already dumped
            g_laneLog = 12;     // 6 pre/post pairs of a REAL menu (idle frames are free)
            g_slotLog = 24;     // post-snap hover-slot dumps (deduped per hover)
            Log("");
            Log("---- panel opened: recording the next %d menus ----", kMaxDumps);
        }
        wasOn = on;
    }
    if (g_dumps >= kMaxDumps) return;

    // Signature over every vector, so a dump happens once per distinct menu.
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
    // Where does the DEFAULT action come from? HuntConfig is retired: every "candidate" it
    // reported was the action-object POOL, which is allocated contiguously - the hits sat at a
    // fixed 0x150 pitch and the first pointer aliased the object it started from. It cannot
    // distinguish a pool from an option array, so it answered the same way for every menu.
    // The hover-target block is the live lead instead.
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
        // Walk the whole span for strings. The OFFSET SPACING between consecutive hits is what
        // reveals the stride when it is not known - six verbs at a fixed pitch is the signature
        // we are looking for.
        // 16-byte records = { action object*, line-string* }. The record holds no text itself,
        // so both pointers are followed.
        if (span % 16 == 0 && span <= 16 * 64) {
            Log("        = %llu records of 16", (unsigned long long)(span / 16));
            for (std::uint64_t i = 0; i * 16 < span; ++i) {
                std::uint64_t q[2] = { 0, 0 };
                if (!Rd(b + i * 16, q, 16)) continue;
                // q0 = action object {vtable, refcount, kind, ...}; q1 = the line's string.
                // Print the action object's head and the string BOTH ways - a live run showed
                // some records resolve as an EASTL heap string and some as a raw {ptr,size},
                // and guessing which cost a pass.
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
                // +0x08 is a REFCOUNT, not a type: the same action object sits in several of
                // these vectors at once and the value tracks how many hold it. +0x0c is always
                // 1. Neither is a "kind" - an earlier pass labelled them wrongly.
                Log("        [%llu] refs=%u  target=\"%s\"",
                    (unsigned long long)i, k32[2], line);
                Log("             obj=%016llx  tgt=%016llx", 
                    (unsigned long long)q[0], (unsigned long long)q[1]);
                // The VERB lives inside the action object, not in the record: obj+0x10 and
                // obj+0x18 point at obj+0x80 / obj+0xa0, and 0x80 is where the target records
                // keep their strings too. Scan the whole object for strings rather than
                // guessing which offset.
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
