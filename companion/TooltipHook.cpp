#include "TooltipHook.h"

#include <windows.h>
#include <detours.h>
#include <atomic>
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
constexpr int kStubSig[] = {
    0x4C, 0x8B, 0x89, kAny, kAny, kAny, kAny,
    0x4C, 0x8B, 0xD2,
    0xBA, kAny, kAny, kAny, kAny,
    0x49, 0x8B, 0x41, 0x08,
    0x4C, 0x8B, 0x80, kAny, kAny, kAny, kAny,
    0xB8, kAny, kAny, kAny, kAny,
    0x41, 0x80, 0xB8, kAny, kAny, kAny, kAny, 0x00,
    0x4D, 0x8B, 0xC2,
    0x0F, 0x44, 0xD0,
    0x49, 0x03, 0xD1,
    0xE9,
};
constexpr std::size_t kStubLen = sizeof(kStubSig) / sizeof(kStubSig[0]);

// Hover object: three strings of 24 bytes each, the library's small-string layout. The last byte
// has its top bit set when the text lives on the heap (pointer at +0, length at +8), otherwise
// the text is inline. The interface slot the hover is over sits after them.
constexpr std::size_t kObjSize = 0x2E8;
constexpr std::size_t kTarget = 0x00, kStrFlag = 0x17, kStrLen = 0x08, kStrCap = 0x10;
constexpr std::size_t kSlot = 0x4C, kComp = 0x50;
constexpr std::uint64_t kHeapFlag = 0x8000000000000000ull;

using Pusher = void* (*)(void* ctx, void* entry, void* vm);
Pusher g_orig = nullptr;
bool   g_installed = false;

// Written by the render thread, read by the script thread.
struct Wanted {
    std::atomic<std::uint32_t> seq{ 0 };
    bool          on = false;
    std::int32_t  slot = 0;
    std::uint32_t comp = 0;
    char          text[kTextMax + 1] = {};
};
Wanted g_want;

bool ReadWanted(std::int32_t& slot, std::uint32_t& comp, char* text) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t s1 = g_want.seq.load();
        if (s1 & 1u) continue;
        const bool on = g_want.on;
        slot = g_want.slot; comp = g_want.comp;
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

        std::int32_t slot; std::uint32_t comp; char text[kTextMax + 1];
        if (!ReadWanted(slot, comp, text)) return false;
        std::int32_t objSlot; std::uint32_t objComp;
        std::memcpy(&objSlot, obj + kSlot, 4);
        std::memcpy(&objComp, obj + kComp, 4);
        if (objSlot != slot || objComp != comp) return false;      // the hover has moved on since the launcher looked

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

void Update(bool on, std::int32_t slot, std::uint32_t comp, const char* text) {
    // nothing to do when it is what is already there, which is nearly every frame
    if (g_want.on == on && g_want.slot == slot && g_want.comp == comp &&
        std::strncmp(g_want.text, text ? text : "", kTextMax) == 0) return;
    g_want.seq.fetch_add(1);
    g_want.on = on; g_want.slot = slot; g_want.comp = comp;
    std::strncpy(g_want.text, text ? text : "", kTextMax);
    g_want.text[kTextMax] = 0;
    g_want.seq.fetch_add(1);
}

}  // namespace rtx::tooltip
