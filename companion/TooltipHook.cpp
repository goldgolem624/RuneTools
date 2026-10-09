#include "TooltipHook.h"
#include "Signatures.h"

#include <windows.h>
#include <detours.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace rtx::tooltip {
namespace {

constexpr int kAny = -1;

// The op stub that picks the hover entry and jumps to the routine that pushes its strings:
//   mov r9, [rcx+off]          4C 8B 89 <off32>
//   mov r10, rdx               4C 8B D2
//   mov edx, imm               BA <imm32>
//   mov rax, [r9+8]            49 8B 41 08
//   mov r8, [rax+off]          4C 8B 80 <off32>
//   mov eax, imm               B8 <imm32>
//   cmp byte ptr [r8+off], 0   41 80 B8 <off32> 00
//   mov r8, r10                4D 8B C2
//   cmove edx, eax             0F 44 D0
//   add rdx, r9                49 03 D1
//   jmp rel32                  E9 <rel32>            <- the routine
// Two ops share the routine through the same stub; every match has to name the same target.
constexpr const auto& kStubSig = rtx::sig::kTooltipStub;
constexpr std::size_t kStubLen = sizeof(kStubSig) / sizeof(kStubSig[0]);

// Hover object: three strings of 24 bytes each, the library's small-string layout. The last byte
// has its top bit set when the text lives on the heap (pointer at +0, length at +8), otherwise
// the text is inline. The interface slot the hover is over sits after them.
constexpr std::size_t kObjSize = 0x2E8;
constexpr std::size_t kTarget = 0x00, kStrFlag = 0x17, kStrLen = 0x08, kStrCap = 0x10;
constexpr std::size_t kRef = 0x48, kSlot = 0x4C, kComp = 0x50;
constexpr std::uint64_t kHeapFlag = 0x8000000000000000ull;

using Pusher = void* (*)(void* ctx, void* entry, void* vm);
Pusher g_orig = nullptr;
bool   g_installed = false;

// Layout self-check (boot record line `check: hover-object`): every hover object the routine is
// handed must read as the layout above (three strings, then the interface slot). Counted on the
// script thread, judged by TakeLog; a layout that fails turns the text injection off.
std::atomic<std::uint32_t> g_hoverSeen{ 0 }, g_hoverBad{ 0 }, g_hoverWhy{ 0 };   // why bits: 1 target, 2 verb, 4 third string, 8 ref, 16 slot, 32 comp
std::atomic<bool> g_layoutOk{ true };

bool StringReads(const std::uint8_t* s) {
    const std::uint8_t flag = s[kStrFlag];
    if (!(flag & 0x80)) return flag <= 0x17;
    const char* p = *reinterpret_cast<const char* const*>(s);
    std::uint64_t len; std::memcpy(&len, s + kStrLen, sizeof(len));
    if (!p || len > 4096) return false;
    volatile char c = p[0]; (void)c;   // a heap string must be readable
    return true;
}
// The hover object against the layout; false when it does not read as one (counted).
bool ObjectReads(const std::uint8_t* obj) {
    unsigned bad = 0;
    if (!StringReads(obj + kTarget)) bad |= 1;
    if (!StringReads(obj + 0x18)) bad |= 2;
    if (!StringReads(obj + 0x30)) bad |= 4;
    std::int32_t ref, slot; std::uint32_t comp;
    std::memcpy(&ref, obj + kRef, 4); std::memcpy(&slot, obj + kSlot, 4); std::memcpy(&comp, obj + kComp, 4);
    if (ref < -1 || ref > 0xFFFF) bad |= 8;
    if (slot < -1 || slot > 100000) bad |= 16;
    if ((comp >> 16) > 4096) bad |= 32;
    g_hoverSeen.fetch_add(1, std::memory_order_relaxed);
    if (bad) { g_hoverBad.fetch_add(1, std::memory_order_relaxed); g_hoverWhy.fetch_or(bad, std::memory_order_relaxed); }
    return bad == 0;
}

// Written by the render thread, read by the script thread.
struct Wanted {
    std::atomic<std::uint32_t> seq{ 0 };
    bool          on = false;
    std::int32_t  slot = 0;
    std::uint32_t comp = 0;
    std::int32_t  ref = -1;
    char          text[kTextMax + 1] = {};
};
Wanted g_want;

bool ReadWanted(std::int32_t& slot, std::uint32_t& comp, std::int32_t& ref, char* text) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t s1 = g_want.seq.load();
        if (s1 & 1u) continue;
        const bool on = g_want.on;
        slot = g_want.slot; comp = g_want.comp; ref = g_want.ref;
        std::memcpy(text, g_want.text, sizeof(g_want.text));
        if (g_want.seq.load() != s1) continue;
        text[kTextMax] = 0;
        return on && text[0] != 0;
    }
    return false;
}

struct FakeEntry { std::uint64_t first; std::uint8_t* obj; };

// Fills `copy` and `fake` so that the routine sees the hovered object with our text on the end
// of its target. False leaves everything to the game.
bool BuildFake(void* entry, std::uint8_t* copy, char* joined, std::size_t joinedCap, FakeEntry& fake) {
    __try {
        if (!entry) return false;
        const std::uint8_t* obj = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(entry) + 8);
        if (!obj) return false;
        if (!ObjectReads(obj) || !g_layoutOk.load(std::memory_order_relaxed)) return false;

        std::int32_t slot, ref; std::uint32_t comp; char text[kTextMax + 1];
        if (!ReadWanted(slot, comp, ref, text)) return false;
        std::int32_t objSlot, objRef; std::uint32_t objComp;
        std::memcpy(&objSlot, obj + kSlot, 4);
        std::memcpy(&objComp, obj + kComp, 4);
        std::memcpy(&objRef, obj + kRef, 4);
        if (objSlot != slot || objComp != comp) return false;
        if (ref >= 0 && objRef != ref) return false;                 // another NPC: every NPC has slot and comp 0      // the hover has moved on since the launcher looked

        const char* target;
        std::size_t len;
        if (obj[kTarget + kStrFlag] & 0x80) {
            target = *reinterpret_cast<const char* const*>(obj + kTarget);
            std::memcpy(&len, obj + kTarget + kStrLen, sizeof(len));
        } else {
            target = reinterpret_cast<const char*>(obj + kTarget);
            len = (std::size_t)(0x17 - obj[kTarget + kStrFlag]);
        }
        if (!target || len == 0 || len > 250) return false;
        const std::size_t extra = std::strlen(text);
        if (len + extra + 1 > joinedCap) return false;
        std::memcpy(joined, target, len);
        std::memcpy(joined + len, text, extra + 1);

        std::memcpy(copy, obj, kObjSize);
        const std::uint64_t total = len + extra, cap = kHeapFlag | (std::uint64_t)(joinedCap - 1);
        const char* p = joined;
        std::memcpy(copy + kTarget, &p, sizeof(p));
        std::memcpy(copy + kTarget + kStrLen, &total, sizeof(total));
        std::memcpy(copy + kTarget + kStrCap, &cap, sizeof(cap));

        fake.first = *reinterpret_cast<std::uint64_t*>(entry);
        fake.obj = copy;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* Hook(void* ctx, void* entry, void* vm) {
    // per thread: the routine copies the text out before it returns, and scripts run one at a time
    thread_local std::uint8_t copy[kObjSize];
    thread_local char joined[512];
    FakeEntry fake{};
    if (BuildFake(entry, copy, joined, sizeof(joined), fake)) return g_orig(ctx, &fake, vm);
    return g_orig(ctx, entry, vm);
}

std::uint8_t* FindPusher() {
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const std::uint8_t* target = nullptr;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const std::uint8_t* p = base + sec->VirtualAddress;
        const std::size_t n = sec->Misc.VirtualSize;
        if (n < kStubLen + 4) continue;
        for (std::size_t o = 0; o + kStubLen + 4 <= n; ++o) {
            if (p[o] != 0x4C || p[o + 1] != 0x8B || p[o + 2] != 0x89) continue;
            std::size_t k = 3;
            while (k < kStubLen && (kStubSig[k] == kAny || p[o + k] == (std::uint8_t)kStubSig[k])) ++k;
            if (k != kStubLen) continue;
            std::int32_t rel;
            std::memcpy(&rel, p + o + kStubLen, 4);
            const std::uint8_t* t = p + o + kStubLen + 4 + rel;
            if (t < p || t >= p + n) return nullptr;
            if (target && target != t) return nullptr;             // the stubs disagree: not the code this knows
            target = t;
        }
    }
    return const_cast<std::uint8_t*>(target);
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    std::uint8_t* pusher = FindPusher();
    if (!pusher) return false;
    g_orig = reinterpret_cast<Pusher>(pusher);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(g_orig), reinterpret_cast<PVOID>(Hook));
    g_installed = DetourTransactionCommit() == NO_ERROR;
    if (!g_installed) g_orig = nullptr;
    return g_installed;
}

void Uninstall() {
    if (!g_installed) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(g_orig), reinterpret_cast<PVOID>(Hook));
    DetourTransactionCommit();
    g_installed = false;
}

// The check line, once per state: nothing hovered yet, the layout holds, or it does not.
bool TakeLog(char* out, std::size_t cap) {
    static int s_state = -1;
    if (cap == 0) return false;
    const char* F = "Tooltip text (item prices, levels)|Thieving levels";
    const std::uint32_t seen = g_hoverSeen.load(std::memory_order_relaxed), bad = g_hoverBad.load(std::memory_order_relaxed);
    int state; char detail[300]; char got[32];
    if (!g_installed) { state = 3; std::snprintf(detail, sizeof(detail), "GONE: the hover entry op stub was not recognised; tooltip text off"); }
    else if (seen < 5) { state = 0; std::snprintf(detail, sizeof(detail), "%u hover objects seen so far", seen); }
    else if (bad * 20 > seen) {
        state = 2;
        const unsigned why = g_hoverWhy.load(std::memory_order_relaxed);
        std::snprintf(detail, sizeof(detail), "FORMAT: hover object 0x%zx: %u of %u objects failed (%s%s%s%s%s%s); tooltip text off", kObjSize, bad, seen,
                      why & 1 ? "target string " : "", why & 2 ? "verb string " : "", why & 4 ? "third string " : "",
                      why & 8 ? "ref " : "", why & 16 ? "slot " : "", why & 32 ? "interface id " : "");
        g_layoutOk.store(false, std::memory_order_relaxed);
    } else {
        state = 1;
        std::snprintf(detail, sizeof(detail), "%u hover objects: three strings readable, ref, slot and interface id at +0x48..+0x50 in range%s", seen, bad ? " (a few mid-change)" : "");
        g_layoutOk.store(true, std::memory_order_relaxed);
    }
    if (state == s_state) return false;
    s_state = state;
    std::snprintf(got, sizeof(got), "%u/%u", seen - bad, seen);
    std::snprintf(out, cap, "check: hover-object %s kind=%s exp=0x%zx got=%s features=%s need=%s ; %s",
                  state == 1 ? "OK" : state == 0 ? "SKIP" : "FAIL", state == 2 ? "format" : state == 3 ? "gone" : "-",
                  kObjSize, state == 0 ? "-" : got, F, state == 0 ? "hover an item, NPC or object" : "-", detail);
    return true;
}

void Update(bool on, std::int32_t slot, std::uint32_t comp, std::int32_t ref, const char* text) {
    // nothing to do when it is what is already there, which is nearly every frame
    if (g_want.on == on && g_want.slot == slot && g_want.comp == comp && g_want.ref == ref &&
        std::strncmp(g_want.text, text ? text : "", kTextMax) == 0) return;
    g_want.seq.fetch_add(1);
    g_want.on = on; g_want.slot = slot; g_want.comp = comp; g_want.ref = ref;
    std::strncpy(g_want.text, text ? text : "", kTextMax);
    g_want.text[kTextMax] = 0;
    g_want.seq.fetch_add(1);
}

}  // namespace rtx::tooltip
