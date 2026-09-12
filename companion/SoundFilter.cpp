// PLAY(subsystem=rcx, ctx=rdx, kind=r8w, id=r9d, loops=+0x20, volume=+0x28, group=+0x30, a8, ..., flag): 15 args, returns the sound object.
// id = js5 archive id; group = engine source tag, 6 = effects (js5-14), 8 = vorbis/music (js5-40).

#include "SoundFilter.h"
#include "SoundShare.h"

#include <windows.h>
#include <detours.h>
#include <intrin.h>

#include <cstdint>
#include <cstring>

namespace rtx::soundfilter {
namespace {

typedef std::uint64_t(__fastcall* Play_t)(std::uint64_t, std::uint64_t, std::uint32_t,
                                          std::int32_t, std::int32_t, std::int32_t,
                                          std::int32_t, std::int32_t, std::int32_t,
                                          std::int32_t, std::uint64_t, std::int32_t,
                                          std::int32_t, std::int32_t, std::uint8_t);

Play_t              g_orig      = nullptr;
rtx::sound::Share*  g_share     = nullptr;
bool                g_installed = false;
std::uint64_t       g_base      = 0;

const unsigned char kSynth[] = {
    0x48,0x81,0xEC,0x88,0x00,0x00,0x00,
    0x83,0x82,0xA0,0x10,0x00,0x00,0xFD,
    0x8B,0x82,0xA0,0x10,0x00,0x00,
    0x48,0x8B,0x89,0x30,0x9A,0x01,0x00,
    0x4C,0x8D,0x04,0x82,
    0x48,0x85,0xC9,
    0x0F,0x84,0x00,0x00,0x00,0x00,
    0x41,0x8B,0x80,0x04,0x01,0x00,0x00,
    0x48,0x8D,0x15,0x00,0x00,0x00,0x00,
    0x45,0x8B,0x88,0x00,0x01,0x00,0x00,
    0xC6,0x44,0x24,0x70,0x00,
    0xC7,0x44,0x24,0x60,0xFF,0x00,0x00,0x00,
    0xC7,0x44,0x24,0x58,0xFF,0xFF,0xFF,0xFF,
    0x48,0x89,0x54,0x24,0x50,
    0x33,0xD2,
    0x89,0x54,0x24,0x48,
    0x89,0x54,0x24,0x40,
    0x48,0x8B,0xD1,
    0xC7,0x44,0x24,0x38,0x04,0x00,0x00,0x00,
    0xC7,0x44,0x24,0x30,0x06,0x00,0x00,0x00,
    0x48,0x89,0x9C,0x24,0x80,0x00,0x00,0x00,
    0x41,0x8B,0x98,0x08,0x01,0x00,0x00,
    0x44,0x0F,0xB7,0x05,0x00,0x00,0x00,0x00,
    0xC7,0x44,0x24,0x28,0xFF,0x00,0x00,0x00,
    0x89,0x44,0x24,0x20,
};
const unsigned char kSynthMask[] = {
    1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,
    1,1,1,1,1,1,
    1,1,1,1,1,1,1,
    1,1,1,1,
    1,1,1,
    1,1,0,0,0,0,
    1,1,1,1,1,1,1,
    1,1,1,0,0,0,0,
    1,1,1,1,1,1,1,
    1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,
    1,1,
    1,1,1,1,
    1,1,1,1,
    1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,
    1,1,1,1,0,0,0,0,
    1,1,1,1,1,1,1,1,
    1,1,1,1,
};
static_assert(sizeof(kSynth) == sizeof(kSynthMask), "pattern and mask must match in length");

std::uint64_t FindPlayFn() {
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
        const std::size_t n = sizeof(kSynth);
        std::uint64_t hit = 0;
        for (std::uint64_t i = 0; i + n + 32 < ts; ++i) {
            if (b[i] != kSynth[0]) continue;
            bool ok = true;
            for (std::size_t j = 1; j < n; ++j)
                if (kSynthMask[j] && b[i + j] != kSynth[j]) { ok = false; break; }
            if (ok) { if (hit) return 0; hit = tb + i; }
        }
        if (!hit) return 0;
        const unsigned char* p = (const unsigned char*)(hit + sizeof(kSynth));
        for (int k = 0; k < 24; ++k) {
            if (p[k] != 0xE8) continue;
            std::int32_t rel = 0;
            std::memcpy(&rel, p + k + 1, 4);
            std::uint64_t tgt = (std::uint64_t)(p + k + 5) + (std::int64_t)rel;
            if (tgt > tb && tgt < tb + ts) return tgt;
            break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

inline std::int32_t IndexOf(std::int32_t group) {
    return (group == 8) ? rtx::sound::kIndexMusic : rtx::sound::kIndexEffects;
}

// Binary search under the launcher's seqlock; a torn read declines to mute rather than retry.
bool IsMuted(std::int32_t key) {
    const std::uint32_t s0 = g_share->blockSeq;
    if (s0 & 1u) return false;
    std::uint32_t n = g_share->blockCount;
    if (n == 0 || n > rtx::sound::kMaxBlocked) return false;
    std::uint32_t lo = 0, hi = n;
    bool hit = false;
    while (lo < hi) {
        std::uint32_t mid = lo + (hi - lo) / 2;
        std::int32_t v = g_share->blocked[mid];
        if (v == key) { hit = true; break; }
        if (v < key) lo = mid + 1; else hi = mid;
    }
    return hit && g_share->blockSeq == s0;
}

void NoteObserved(std::int32_t id, std::int32_t idx, bool muted, std::uint32_t caller, int x, int y, std::uint8_t group, std::uint8_t kind) {
    const std::uint32_t seq = g_share->recentSeq;
    rtx::sound::RecentEntry e;
    e.id    = id;
    e.idx   = (std::int16_t)idx;
    e.muted = muted ? 1 : 0;
    e.ms    = (std::uint32_t)GetTickCount64();
    e.caller = caller; e.x = (std::int16_t)x; e.y = (std::int16_t)y; e.group = group; e.kind = kind; e.pad = 0;
    g_share->recent[seq % rtx::sound::kMaxRecent] = e;
    MemoryBarrier();                              // slot lands before the seq bump
    g_share->recentSeq = seq + 1;
    ++g_share->diag[2];
}

std::uint64_t __fastcall Detour_Play(std::uint64_t subsystem, std::uint64_t ctx, std::uint32_t kind,
                                     std::int32_t id, std::int32_t loops, std::int32_t volume,
                                     std::int32_t group, std::int32_t a8, std::int32_t a9,
                                     std::int32_t a10, std::uint64_t a11, std::int32_t a12,
                                     std::int32_t a13, std::int32_t a14, std::uint8_t a15) {
    if (g_share && g_share->enable) {
        ++g_share->diag[0];
        const std::int32_t idx = IndexOf(group);
        const bool muted = IsMuted(rtx::sound::MakeKey(idx, id));
        // Who asked: the return address names the subsystem (script op, server packet, zone sound,
        // actor animation, engine). Positioned sounds (mode a8 != 4) carry a fine-unit position at a11.
        const std::uint64_t ret = (std::uint64_t)_ReturnAddress();
        const std::uint32_t caller = (ret >= g_base && ret < g_base + 0x2000000) ? (std::uint32_t)(ret - g_base) : 0;
        int tx = -1, ty = -1;
        if (a8 != 4 && a11) {
            __try { const float* pos = (const float*)a11; tx = (int)pos[0] >> 9; ty = (int)pos[2] >> 9; }
            __except (EXCEPTION_EXECUTE_HANDLER) { tx = ty = -1; }
        }
        NoteObserved(id, idx, muted, caller, tx, ty, (std::uint8_t)group, (std::uint8_t)kind);
        if (muted) {
            volume = 0;
            ++g_share->diag[1];
        }
    }
    return g_orig(subsystem, ctx, kind, id, loops, volume, group, a8, a9, a10, a11, a12, a13, a14, a15);
}

rtx::sound::Share* MapShare() {
    wchar_t name[64];
    rtx::sound::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  (DWORD)sizeof(rtx::sound::Share), name);
    if (!h) return nullptr;
    return (rtx::sound::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0,
                                             sizeof(rtx::sound::Share));
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    HMODULE gm = GetModuleHandleW(L"rs2client.exe");
    if (!gm) return false;
    g_base = (std::uint64_t)gm;

    g_share = MapShare();
    if (!g_share) return false;
    g_share->magic   = rtx::sound::kMagic;
    g_share->version = rtx::sound::kVersion;
    g_share->pid     = GetCurrentProcessId();
    g_share->flags   = 0;
    g_share->enable  = 0;
    g_share->playRva = 0;
    g_share->blockSeq = 0; g_share->blockCount = 0;
    g_share->recentSeq = 0;
    for (int i = 0; i < 8; ++i) g_share->diag[i] = 0;

    std::uint64_t fn = FindPlayFn();
    if (!fn) return false;                          // pattern moved: feature off
    g_share->playRva = (std::uint32_t)(fn - g_base);

    g_orig = (Play_t)fn;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_orig, (PVOID)Detour_Play);
    if (DetourTransactionCommit() != NO_ERROR) { g_orig = nullptr; return false; }

    g_share->flags |= rtx::sound::kFlagHooked;
    g_installed = true;
    return true;
}

void Uninstall() {
    if (!g_installed) return;
    if (g_share) g_share->enable = 0;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)g_orig, (PVOID)Detour_Play);
    DetourTransactionCommit();
    g_installed = false;
    if (g_share) g_share->flags &= ~rtx::sound::kFlagHooked;
}

}  // namespace rtx::soundfilter
