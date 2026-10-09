// Every chat line goes through the game's message store twice: an add routine writes the record,
// then the caller hands that record to a notify routine, which lists it for the chat window and
// evicts the oldest of its kind past 200. NPC chatter comes as type 116 with the text
// "Name: <col=..>what it said". A muted sender's line stays in the store but is never listed, so
// the chat window never draws it. Game instructions from the same NPC come as type 0 and pass.
//
// NOTIFY(store=rcx, record+8=rdx). Found by its prologue, which the last two builds share.

#include "ChatFilter.h"
#include "Signatures.h"
#include "ChatShare.h"

#include <windows.h>
#include <detours.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace rtx::chatfilter {
namespace {

typedef void(__fastcall* Notify_t)(std::uint64_t store, std::uint64_t rec8);

Notify_t                 g_orig      = nullptr;
rtx::chatmute::Share*    g_share     = nullptr;
bool                     g_installed = false;
std::uint64_t            g_base      = 0;

constexpr std::int32_t kNpcType = 116;

constexpr const auto& kNotify = rtx::sig::kChatNotify;

std::uint64_t FindNotify() {
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
        const std::size_t n = sizeof(kNotify);
        std::uint64_t hit = 0;
        for (std::uint64_t i = 0; i + n < ts; ++i) {
            if (b[i] != kNotify[0] || std::memcmp(b + i, kNotify, n) != 0) continue;
            if (hit) return 0;                          // two of them: not the routine this knows
            hit = tb + i;
        }
        return hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// No C++ objects in these: they must have nothing to unwind on a fault.
bool ReadI32(std::uint64_t at, std::int32_t* out) {
    __try { *out = *(const std::int32_t*)at; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadI64(std::uint64_t at, std::int64_t* out) {
    __try { *out = *(const std::int64_t*)at; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The store's second index: a tree at +0x20 whose size sits at +0x40. It holds a few of the lines
// (8 of 140 seen live, of several types) and its size held while lines arrived, so the unlink below
// leaves it alone; the size is watched on every line, and an unlink is refused while the tree
// follows the lines. Judged from 40 lines on: the tree fills over the first lines of a session.
std::atomic<std::uint32_t> g_treeLines{ 0 }, g_treeChanges{ 0 };
std::atomic<std::int64_t>  g_treeFirst{ 0 }, g_treeLast{ 0 };
void NoteTree(std::uint64_t store) {
    std::int64_t size = 0;
    if (!ReadI64(store + 0x40, &size)) return;
    const std::uint32_t lines = g_treeLines.load(std::memory_order_relaxed);
    if (lines == 0) g_treeFirst.store(size, std::memory_order_relaxed);
    else if (size != g_treeLast.load(std::memory_order_relaxed)) g_treeChanges.fetch_add(1, std::memory_order_relaxed);
    g_treeLast.store(size, std::memory_order_relaxed);
    g_treeLines.store(lines + 1, std::memory_order_relaxed);
}
bool TreeFollowsLines() {
    const std::uint32_t lines = g_treeLines.load(std::memory_order_relaxed);
    return lines >= 40 && g_treeChanges.load(std::memory_order_relaxed) * 2 >= lines;
}
// A string as the store keeps it: inline while it fits, with the room left at +0x17, else a
// pointer at +0 and a length at +8.
bool ReadText(std::uint64_t str, char* out, std::size_t cap) {
    __try {
        const unsigned char tag = *(const unsigned char*)(str + 0x17);
        const char* data; std::size_t len;
        if (tag & 0x80) {
            data = *(const char* const*)str;
            len  = (std::size_t)*(const std::uint64_t*)(str + 8);
            if (!data) return false;
        } else {
            if (tag > 0x17) return false;
            data = (const char*)str; len = 0x17 - tag;
        }
        if (len >= cap) len = cap - 1;
        std::memcpy(out, data, len); out[len] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// "Name: ..." -> "name"; false when the line does not start that way.
bool Sender(const char* text, char* who) {
    int n = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == ':') {
            if (n == 0 || !(p[1] == ' ' || p[1] == '<')) return false;
            who[n] = 0; return true;
        }
        if (n >= rtx::chatmute::kNameChars - 1) return false;
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        who[n++] = c;
    }
    return false;
}

// Under the launcher's seqlock; a torn read declines to mute.
bool IsMuted(const char* who) {
    const std::uint32_t s0 = g_share->nameSeq;
    if (s0 & 1u) return false;
    std::uint32_t n = g_share->nameCount;
    if (n > (std::uint32_t)rtx::chatmute::kMaxNames) return false;
    bool hit = false;
    for (std::uint32_t i = 0; i < n && !hit; ++i) {
        const char* m = g_share->names[i];
        if ((m[0] == '*' && m[1] == 0) || std::strncmp(m, who, rtx::chatmute::kNameChars) == 0) hit = true;
    }
    return hit && g_share->nameSeq == s0;
}

void NoteHeard(const char* who) {
    const std::uint32_t seq = g_share->recentSeq;
    if (seq && std::strncmp(g_share->recent[(seq - 1) % rtx::chatmute::kMaxRecent], who, rtx::chatmute::kNameChars) == 0) return;
    std::strncpy(g_share->recent[seq % rtx::chatmute::kMaxRecent], who, rtx::chatmute::kNameChars - 1);
    g_share->recent[seq % rtx::chatmute::kMaxRecent][rtx::chatmute::kNameChars - 1] = 0;
    MemoryBarrier();
    g_share->recentSeq = seq + 1;
}

// The store: a hash map of records keyed by id at store+0x878 (buckets +0x880, count of buckets
// +0x888, records +0x890), the id the next line takes at +0x18, each record chained through
// +0xB8, and a doubly linked id chain through the records' +0x8 (next id) and +0xC (previous id).
// The chat window starts from the newest id and follows the chain back, and a new line looks its
// predecessor up by id, so a muted line is undone as if never added: out of the map and the chain,
// and its id handed back for the next line. Its memory is left: a freed record could be handed to
// the caller's next use of it. The tree at +0x20 is not touched (NoteTree above).
std::uint64_t FindRecord(std::uint64_t store, std::int32_t key) {
    const std::uint64_t buckets = *(const std::uint64_t*)(store + 0x880);
    const std::uint32_t nb = *(const std::uint32_t*)(store + 0x888);
    if (!buckets || !nb) return 0;
    std::uint64_t node = *(const std::uint64_t*)(buckets + ((std::uint64_t)(std::uint32_t)key % nb) * 8);
    for (int guard = 0; node > 0x10000 && guard < 256; ++guard) {
        if (*(const std::int32_t*)node == key) return node;
        node = *(const std::uint64_t*)(node + 0xB8);
    }
    return 0;
}
// 0 when done, else the step that stopped it (diag[7]); diag[6] keeps the counter's distance from the key.
int Unlink(std::uint64_t store, std::uint64_t rec) {
    if (TreeFollowsLines()) return 5;                       // the line is in an index this does not unlink from
    __try {
        // The add takes the counter as the key and leaves the counter at key + 1 (seen live in the hook:
        // counter - key = 1). A record's +0x8 starts out as key + 1, the key the next line takes, and
        // +0xC is the previous line's key, so the line just added has not changed its predecessor.
        // Only that newest line is undone; anything else is the game's own order, left alone.
        const std::int32_t key = *(const std::int32_t*)rec;
        g_share->diag[6] = (std::uint32_t)(*(const std::int32_t*)(store + 0x18) - key);
        if (*(const std::int32_t*)(store + 0x18) != key + 1) return 1;
        const std::uint64_t buckets = *(const std::uint64_t*)(store + 0x880);
        const std::uint32_t nb = *(const std::uint32_t*)(store + 0x888);
        if (!buckets || !nb) return 2;
        std::uint64_t* slot = (std::uint64_t*)(buckets + ((std::uint64_t)(std::uint32_t)key % nb) * 8);
        std::uint64_t node = *slot;
        if (node == rec) { *slot = *(const std::uint64_t*)(rec + 0xB8); }
        else {
            int guard = 0;
            while (node > 0x10000 && *(const std::uint64_t*)(node + 0xB8) != rec && guard++ < 256) node = *(const std::uint64_t*)(node + 0xB8);
            if (node <= 0x10000 || guard >= 256) return 3;
            *(std::uint64_t*)(node + 0xB8) = *(const std::uint64_t*)(rec + 0xB8);
        }
        *(std::int64_t*)(store + 0x890) -= 1;
        *(std::int32_t*)(store + 0x18) = key;               // the next line takes this line's key
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 4; }
}

void __fastcall Detour_Notify(std::uint64_t store, std::uint64_t rec8) {
    if (store > 0x10000 && rec8 > 0x10000) NoteTree(store);
    if (g_share && g_share->enable && rec8 > 0x10000) {
        ++g_share->diag[0];
        std::int32_t type = 0;
        if (ReadI32(rec8 + 0x18, &type) && type == kNpcType) {       // the record's +0x20
            ++g_share->diag[1];
            char text[96], who[rtx::chatmute::kNameChars];
            if (ReadText(rec8 + 0x88, text, sizeof(text)) && Sender(text, who)) {   // its +0x90
                NoteHeard(who);
                const std::uint32_t mode = g_share->mode;
                if (mode >= rtx::chatmute::kModeSkip && IsMuted(who)) {
                    ++g_share->diag[2];
                    if (mode >= rtx::chatmute::kModeUnlink) { const int st = Unlink(store, rec8 - 8); if (!st) ++g_share->diag[4]; else { ++g_share->diag[5]; g_share->diag[7] = (std::uint32_t)st; } }
                    return;
                }
            } else {
                ++g_share->diag[3];
            }
        }
    }
    g_orig(store, rec8);
}

rtx::chatmute::Share* MapShare() {
    wchar_t name[rtx::ipc::kNameChars];
    rtx::chatmute::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  (DWORD)sizeof(rtx::chatmute::Share), name);
    if (!h) return nullptr;
    return (rtx::chatmute::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::chatmute::Share));
}

void Reset(rtx::chatmute::Share* sh, std::uint32_t flags, std::uint32_t rva) {
    sh->magic = rtx::chatmute::kMagic; sh->version = rtx::chatmute::kVersion; sh->pid = GetCurrentProcessId();
    sh->flags = flags; sh->enable = 0; sh->mode = rtx::chatmute::kModeSkip; sh->notifyRva = rva;
    sh->nameSeq = 0; sh->nameCount = 0; sh->recentSeq = 0;
    for (int i = 0; i < 8; ++i) sh->diag[i] = 0;
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    HMODULE gm = GetModuleHandleW(L"rs2client.exe");
    if (!gm) return false;
    g_base = (std::uint64_t)gm;
    g_share = MapShare();
    if (!g_share) return false;
    Reset(g_share, 0, 0);

    const std::uint64_t fn = FindNotify();
    if (!fn) return false;                          // pattern moved: feature off
    g_share->notifyRva = (std::uint32_t)(fn - g_base);
    g_orig = (Notify_t)fn;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_orig, (PVOID)Detour_Notify);
    if (DetourTransactionCommit() != NO_ERROR) { g_orig = nullptr; return false; }
    g_share->flags |= rtx::chatmute::kFlagHooked;
    g_installed = true;
    return true;
}

// The hook stays; only the published view moves. The old view stays mapped: the hook reads it
// from the game thread.
void Rebind() {
    static std::uint32_t s_gen = 0;
    if (!rtx::ipc::SessionChanged(s_gen)) return;
    if (!g_installed) return;
    rtx::chatmute::Share* fresh = MapShare();
    if (!fresh) return;
    Reset(fresh, g_share ? g_share->flags : 0, g_share ? g_share->notifyRva : 0);
    g_share = fresh;
}

void TreeStats(std::uint32_t& lines, std::uint32_t& changes, std::int64_t& first, std::int64_t& last) {
    lines = g_treeLines.load(std::memory_order_relaxed); changes = g_treeChanges.load(std::memory_order_relaxed);
    first = g_treeFirst.load(std::memory_order_relaxed); last = g_treeLast.load(std::memory_order_relaxed);
}

void Uninstall() {
    if (!g_installed) return;
    if (g_share) g_share->enable = 0;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)g_orig, (PVOID)Detour_Notify);
    DetourTransactionCommit();
    g_installed = false;
    if (g_share) g_share->flags &= ~rtx::chatmute::kFlagHooked;
}

}  // namespace rtx::chatfilter
