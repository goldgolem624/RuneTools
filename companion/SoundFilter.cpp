// In-client sound observation and muting (see SoundFilter.h / SoundShare.h).
//
// Hook target is the engine's shared sound-play function, reached from every CS2 op that starts
// a sound:
//
//   PLAY(subsystem, kind, id, loops, volume, group, a7, ..., delay)   // 13 args, returns void
//          rcx      dx   r8d  r9d    +0x20   +0x28
//
// `id` is the js5 archive id - the same id the Sounds panel lists and plays - and `group` is
// the engine's own source tag: 6 = sound effects (js5-14), 8 = vorbis/music (js5-40).
//
// MUTING SETS volume TO 0 AND CALLS THROUGH. Nothing is written into the client's memory. A
// zero volume is not a special case invented here: SOUND_SYNTH hardcodes 0xFF while
// SOUND_SYNTH_VOLUME passes whatever the script chose, so the engine already drives the full
// range of this exact argument. All of its own bookkeeping still runs.
//
// This replaced an earlier hook on the audio MIXER that zeroed decoded sample buffers. That
// crashed the client twice - it wrote over fields that are only a right-channel buffer for
// STEREO chunks, and js5-14 effects are mono - and it could not have worked anyway: the id
// available at the mix point is a per-voice handle (six-digit values against a js5-14 space
// ending near 7.4k), so muted sounds leaked through the voices that were not muted.
//
// --- how the target is located -------------------------------------------------------------
// NOT by opcode number: the CS2 opcode scramble was re-shuffled after build 949, so the old
// numbers name different ops now (SOUND_SYNTH was opcode 1522, it is 168 in the current build).
// NOT by a hardcoded address: those move every build.
//
// Instead, scan for the SOUND_SYNTH handler's body - byte-identical between 949 and the current
// build - and decode the call it makes. That self-locates PLAY even when PLAY itself moves. The
// pattern is 125 bytes and matches EXACTLY ONCE in .text (verified against the live binary).
//
// Everything here runs on whichever thread started the sound. No allocation, no locks: while
// the panel is closed the detour is one relaxed load and a branch.

#include "SoundFilter.h"
#include "SoundShare.h"

#include <windows.h>
#include <detours.h>

#include <cstdint>
#include <cstring>

namespace rtx::soundfilter {
namespace {

// 13 args, void return - taken from decompiling the callee itself, not guessed from a call
// site. All 13 are forwarded verbatim.
typedef void(__fastcall* Play_t)(std::uint64_t, std::uint32_t, std::int32_t, std::int32_t,
                                 std::int32_t, std::int32_t, std::int32_t, std::uint64_t,
                                 std::int32_t, std::uint64_t, std::int32_t, std::int32_t,
                                 std::int32_t);

Play_t              g_orig      = nullptr;
rtx::sound::Share*  g_share     = nullptr;
bool                g_installed = false;
std::uint64_t       g_base      = 0;

// SOUND_SYNTH handler body. Address-bearing operands are wildcarded: the two rip-relative
// disp32s and the JZ displacement.
//
//   sub  rsp,0x78
//   add  dword [rdx+0x10A0],-3        <- pops 3 ints; the -3 is what identifies THIS op
//   mov  eax,[rdx+0x10A0]
//   mov  rcx,[rcx+0x199F0]            <- audio subsystem, off the engine context
//   lea  r8,[rdx+rax*4] / test rcx,rcx / jz
//   mov  eax,[r8+0x108] / r9d,[r8+0x104] / r8d,[r8+0x100]   <- delay, loops, ID
//   movzx edx,[rip+..] / lea rax,[rip+..]
//   mov  [rsp+0x58],0xFF / [rsp+0x50],-1 / [rsp+0x48],rax / [rsp+0x40],0
//   mov  [rsp+0x30],4 / [rsp+0x28],6 / [rsp+0x20],0xFF      <- a7, group, volume
const unsigned char kSynth[] = {
    0x48,0x83,0xEC,0x78, 0x83,0x82,0xA0,0x10,0x00,0x00,0xFD,
    0x8B,0x82,0xA0,0x10,0x00,0x00, 0x48,0x8B,0x89,0xF0,0x99,0x01,0x00,
    0x4C,0x8D,0x04,0x82, 0x48,0x85,0xC9, 0x74,0x61,
    0x41,0x8B,0x80,0x08,0x01,0x00,0x00, 0x45,0x8B,0x88,0x04,0x01,0x00,0x00,
    0x45,0x8B,0x80,0x00,0x01,0x00,0x00,
    0x0F,0xB7,0x15,0x00,0x00,0x00,0x00, 0x89,0x44,0x24,0x60,
    0x48,0x8D,0x05,0x00,0x00,0x00,0x00,
    0xC7,0x44,0x24,0x58,0xFF,0x00,0x00,0x00, 0xC7,0x44,0x24,0x50,0xFF,0xFF,0xFF,0xFF,
    0x48,0x89,0x44,0x24,0x48, 0xC7,0x44,0x24,0x40,0x00,0x00,0x00,0x00,
    0xC7,0x44,0x24,0x30,0x04,0x00,0x00,0x00, 0xC7,0x44,0x24,0x28,0x06,0x00,0x00,0x00,
    0xC7,0x44,0x24,0x20,0xFF,0x00,0x00,0x00,
};
const unsigned char kSynthMask[] = {
    1,1,1,1, 1,1,1,1,1,1,1,
    1,1,1,1,1,1, 1,1,1,1,1,1,1,
    1,1,1,1, 1,1,1, 1,0,
    1,1,1,1,1,1,1, 1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,
    1,1,1,0,0,0,0, 1,1,1,1,
    1,1,1,0,0,0,0,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
};
static_assert(sizeof(kSynth) == sizeof(kSynthMask), "pattern and mask must match in length");

// Find the SOUND_SYNTH handler, then decode the CALL that follows its argument setup to get the
// shared play function. Returns 0 if either step fails, in which case nothing is hooked and the
// client is left untouched.
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
            if (ok) { hit = tb + i; break; }
        }
        if (!hit) return 0;
        // PLAY is called immediately after the argument setup the pattern covers.
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

// `group` is the engine's own source tag on the play call. Key packing itself lives in
// SoundShare.h so the companion, the launcher and the panel cannot drift apart on it.
inline std::int32_t IndexOf(std::int32_t group) {
    return (group == 8) ? rtx::sound::kIndexMusic : rtx::sound::kIndexEffects;
}

// Binary search the launcher-written list under its seqlock. A torn read declines to mute
// rather than retrying: at worst one sound plays that a moment later would not have.
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

// One ring entry per playback START. Unlike the old mixer hook this fires once per sound rather
// than once per audio chunk, so it needs no rate limiting and the ids never repeat spuriously.
void NoteObserved(std::int32_t id, std::int32_t idx, bool muted) {
    const std::uint32_t seq = g_share->recentSeq;
    rtx::sound::RecentEntry e;
    e.id    = id;
    e.idx   = (std::int16_t)idx;
    e.muted = muted ? 1 : 0;
    e.ms    = (std::uint32_t)GetTickCount64();
    g_share->recent[seq % rtx::sound::kMaxRecent] = e;
    MemoryBarrier();                              // slot must land before the seq bump
    g_share->recentSeq = seq + 1;                 // publish only once the slot is written
    ++g_share->diag[2];
}

void __fastcall Detour_Play(std::uint64_t subsystem, std::uint32_t kind, std::int32_t id,
                            std::int32_t loops, std::int32_t volume, std::int32_t group,
                            std::int32_t a7, std::uint64_t a8, std::int32_t a9,
                            std::uint64_t a10, std::int32_t a11, std::int32_t a12,
                            std::int32_t delay) {
    if (g_share && g_share->enable) {
        ++g_share->diag[0];
        const std::int32_t idx = IndexOf(group);
        const bool muted = IsMuted(rtx::sound::MakeKey(idx, id));
        NoteObserved(id, idx, muted);
        if (muted) {
            volume = 0;                            // the engine's own silent value
            ++g_share->diag[1];
        }
    }
    g_orig(subsystem, kind, id, loops, volume, group, a7, a8, a9, a10, a11, a12, delay);
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
    if (!fn) return false;                          // pattern moved -> feature off, client fine
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
