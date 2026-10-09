#pragma once
// Code finding on the loaded client, reading the image only.
//   FunctionStart: the function holding an address, from the exception table (chained entries
//   followed to the function's own start). For signatures taken from inside a body (kInFunction in
//   Signatures.h).
//   The anchors behind the byte signatures (Signatures.h kAnchors): a unique string and the one
//   function naming it, the server packet descriptor vector with the handlers and the framer it
//   names, the entity vtable family, and a rip-relative lea inside a handler.
// Image takes the mapped base and the base that absolute pointers inside the data are relative to
// (the same in the live process; the preferred base for a file laid out by hand, which is how the
// anchors are dry-run against an exe on disk).
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "ServerOps.h"

namespace rtx::codefind {

inline std::uint64_t FunctionStart(std::uint64_t base, std::uint64_t va) {
    if (!base || va < base) return 0;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (!dir.VirtualAddress || !dir.Size) return 0;
        const auto* rf = reinterpret_cast<const RUNTIME_FUNCTION*>(base + dir.VirtualAddress);
        const std::uint32_t n = dir.Size / sizeof(RUNTIME_FUNCTION);
        const std::uint32_t rva = (std::uint32_t)(va - base);
        std::uint32_t lo = 0, hi = n;   // sorted by start
        while (lo < hi) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            if (rf[mid].BeginAddress <= rva) lo = mid + 1; else hi = mid;
        }
        if (!lo || rva >= rf[lo - 1].EndAddress) return 0;
        const RUNTIME_FUNCTION* e = &rf[lo - 1];
        for (int guard = 0; guard < 8; ++guard) {
            std::uint32_t info = e->UnwindData;
            if (info & 1) { e = reinterpret_cast<const RUNTIME_FUNCTION*>(base + (info & ~1u)); continue; }
            const std::uint8_t* u = reinterpret_cast<const std::uint8_t*>(base + info);
            if (!((u[0] >> 3) & UNW_FLAG_CHAININFO)) break;
            const std::uint32_t codes = (u[2] + 1u) & ~1u;
            e = reinterpret_cast<const RUNTIME_FUNCTION*>(u + 4 + codes * 2);
        }
        return base + e->BeginAddress;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

struct Image {
    std::uint64_t base = 0, ptrBase = 0, size = 0;
    std::uint64_t textLo = 0, textHi = 0;      // mapped bounds
    std::uint64_t rdataLo = 0, rdataHi = 0;
    std::uint64_t dataLo = 0, dataHi = 0;
    std::uint64_t Map(std::uint64_t ptr) const { return ptr - ptrBase + base; }   // a pointer stored in the data -> mapped
    std::uint64_t Ptr(std::uint64_t mapped) const { return mapped - base + ptrBase; }
    std::uint32_t Rva(std::uint64_t mapped) const { return (std::uint32_t)(mapped - base); }
    bool PtrIn(std::uint64_t ptr, std::uint64_t lo, std::uint64_t hi) const { const std::uint64_t m = Map(ptr); return m >= lo && m < hi; }
    bool PtrText(std::uint64_t ptr) const { return PtrIn(ptr, textLo, textHi); }
    bool PtrRdata(std::uint64_t ptr) const { return PtrIn(ptr, rdataLo, rdataHi); }
    bool PtrData(std::uint64_t ptr) const { return PtrIn(ptr, dataLo, dataHi); }
    bool InText(std::uint64_t mapped) const { return mapped >= textLo && mapped < textHi; }
    bool InRdata(std::uint64_t mapped) const { return mapped >= rdataLo && mapped < rdataHi; }
    bool InData(std::uint64_t mapped) const { return mapped >= dataLo && mapped < dataHi; }
};

inline std::uint64_t Q(std::uint64_t at) { std::uint64_t v; std::memcpy(&v, reinterpret_cast<const void*>(at), 8); return v; }
inline std::int32_t  D(std::uint64_t at) { std::int32_t v; std::memcpy(&v, reinterpret_cast<const void*>(at), 4); return v; }

// Sections by name: .text, .rdata and .data (the first writable data section).
inline bool OpenImage(std::uint64_t base, std::uint64_t ptrBase, Image& out) {
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    Image im;
    im.base = base; im.ptrBase = ptrBase ? ptrBase : base; im.size = nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const std::uint64_t lo = base + sec->VirtualAddress;
        const std::uint64_t hi = lo + (sec->Misc.VirtualSize > sec->SizeOfRawData ? sec->Misc.VirtualSize : sec->SizeOfRawData);
        if (std::memcmp(sec->Name, ".text\0\0\0", 8) == 0) { im.textLo = lo; im.textHi = hi; }
        else if (std::memcmp(sec->Name, ".rdata\0\0", 8) == 0) { im.rdataLo = lo; im.rdataHi = hi; }
        else if (std::memcmp(sec->Name, ".data\0\0\0", 8) == 0) { im.dataLo = lo; im.dataHi = hi; }
    }
    if (!im.textLo || !im.rdataLo || !im.dataLo) return false;
    out = im;
    return true;
}

// Whole NUL-terminated string in .rdata (preceded by a NUL or the section start): mapped address,
// 0 when absent or not unique.
inline std::uint64_t FindString(const Image& im, const char* text) {
    const std::size_t tl = std::strlen(text);
    if (!tl) return 0;
    const auto* b = reinterpret_cast<const std::uint8_t*>(im.rdataLo);
    const std::size_t n = (std::size_t)(im.rdataHi - im.rdataLo);
    std::uint64_t found = 0;
    for (std::size_t i = 0; i + tl + 1 <= n; ++i) {
        if (b[i] != (std::uint8_t)text[0] || b[i + tl] != 0 || std::memcmp(b + i, text, tl) != 0) continue;
        if (i && b[i - 1] != 0) continue;
        if (found) return 0;
        found = im.rdataLo + i;
    }
    return found;
}

// Code sites whose rip-relative operand names `target` (lea, mov, cmp and movzx/movsx forms),
// appended as mapped instruction addresses.
inline void RipRefs(const Image& im, std::uint64_t target, std::vector<std::uint64_t>& out) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(im.textLo);
    const std::size_t n = (std::size_t)(im.textHi - im.textLo);
    for (std::size_t i = 0; i + 8 <= n; ++i) {
        std::size_t j = i;
        if (b[j] >= 0x40 && b[j] <= 0x4F) ++j;
        const std::uint8_t op = b[j];
        std::size_t dispAt, end;
        if ((op == 0x8D || op == 0x8B || op == 0x89 || op == 0x3B || op == 0x39 || op == 0x63) && (b[j + 1] & 0xC7) == 0x05) { dispAt = j + 2; end = j + 6; }
        else if (op == 0x0F && (b[j + 1] == 0xB6 || b[j + 1] == 0xB7 || b[j + 1] == 0xBE || b[j + 1] == 0xBF) && (b[j + 2] & 0xC7) == 0x05) { dispAt = j + 3; end = j + 7; }
        else continue;
        if (end + 1 > n) continue;
        std::int32_t disp; std::memcpy(&disp, b + dispAt, 4);
        if (im.textLo + end + (std::int64_t)disp == target) out.push_back(im.textLo + i);
    }
}

// The one function whose code names the unique string, 0 when none or several; nFunctions tells.
inline std::uint64_t StringUser(const Image& im, const char* text, int* nFunctions = nullptr) {
    if (nFunctions) *nFunctions = 0;
    const std::uint64_t s = FindString(im, text);
    if (!s) return 0;
    std::vector<std::uint64_t> sites;
    RipRefs(im, s, sites);
    std::vector<std::uint64_t> fns;
    for (std::uint64_t at : sites) {
        const std::uint64_t f = FunctionStart(im.base, at);
        if (f && std::find(fns.begin(), fns.end(), f) == fns.end()) fns.push_back(f);
    }
    if (nFunctions) *nFunctions = (int)fns.size();
    return fns.size() == 1 ? fns[0] : 0;
}

// True when the first `span` bytes of `fn` hold a compare against `imm` (cmp r32,imm8 / imm32,
// cmp eax,imm32).
inline bool ComparesWith(const Image& im, std::uint64_t fn, std::size_t span, std::uint32_t imm) {
    if (!im.InText(fn)) return false;
    if (fn + span > im.textHi) span = (std::size_t)(im.textHi - fn);
    const auto* b = reinterpret_cast<const std::uint8_t*>(fn);
    for (std::size_t i = 0; i + 6 <= span; ++i) {
        if (b[i] == 0x83 && (b[i + 1] & 0xF8) == 0xF8 && imm < 0x80 && b[i + 2] == (std::uint8_t)imm) return true;
        if (b[i] == 0x81 && (b[i + 1] & 0xF8) == 0xF8) { std::uint32_t v; std::memcpy(&v, b + i + 2, 4); if (v == imm) return true; }
        if (b[i] == 0x3D) { std::uint32_t v; std::memcpy(&v, b + i + 1, 4); if (v == imm) return true; }
    }
    return false;
}
inline bool NonLeaf(const Image& im, std::uint64_t fn, std::size_t span) {
    if (!im.InText(fn)) return false;
    if (fn + span > im.textHi) span = (std::size_t)(im.textHi - fn);
    const auto* b = reinterpret_cast<const std::uint8_t*>(fn);
    for (std::size_t i = 0; i + 5 <= span; ++i) if (b[i] == 0xE8) return true;
    return false;
}

// The server packet descriptor vector (ServerOps.h layout): {begin, end, capacity} with the
// descriptor buffer at +0x28, descriptors in opcode order. Several vectors of that shape exist (the
// zone sub-packets, a login table); the server one is the large one whose referencing non-leaf
// function compares against its count: that function is the framer. When no referencing function
// carries the compare, the largest vector is taken and framer stays 0.
struct ProtTable {
    std::uint64_t vec = 0, begin = 0;   // mapped
    int count = 0, capacity = 0;        // descriptors present (0 in a file image) and the buffer size
    std::uint64_t framer = 0;           // mapped function start, 0 when not named
};
inline bool DescriptorsInOrder(const Image& im, std::uint64_t begin, int probe) {
    for (int k = 0; k < probe && k < 8; ++k) {
        const std::uint64_t d = Q(begin + (std::uint64_t)k * 8);
        if (!d || !(im.PtrData(d) || im.PtrRdata(d))) return false;
        if (D(im.Map(d) + rtx::sops::kDescOp) != k) return false;
    }
    return true;
}
inline bool FindProtTable(const Image& im, ProtTable& out) {
    struct Cand { std::uint64_t vec; int count, capacity; };
    std::vector<Cand> cands;
    for (std::uint64_t r = im.dataLo; r + 0x30 <= im.dataHi; r += 8) {
        const std::uint64_t begin = Q(r);
        if (begin != im.Ptr(r + rtx::sops::kProtBuffer)) continue;
        const std::uint64_t end = Q(r + 8), cap = Q(r + 16);
        if (end < begin || cap < end || ((end - begin) & 7) || ((cap - begin) & 7) || cap - begin > 0x4000) continue;
        const int count = (int)((end - begin) / 8), capacity = (int)((cap - begin) / 8);
        if (capacity < 8 || capacity <= 100) continue;
        if (count && !DescriptorsInOrder(im, r + rtx::sops::kProtBuffer, count)) continue;
        if (!count && Q(r + rtx::sops::kProtBuffer) != 0) continue;   // a file image holds an empty vector
        cands.push_back({ r, count, capacity });
    }
    if (cands.empty()) return false;
    ProtTable best; int bestCap = 0;
    for (const Cand& c : cands) {
        std::vector<std::uint64_t> sites;
        RipRefs(im, c.vec, sites);
        std::uint64_t framer = 0; int framers = 0;
        std::vector<std::uint64_t> fns;
        for (std::uint64_t at : sites) {
            const std::uint64_t f = FunctionStart(im.base, at);
            if (!f || std::find(fns.begin(), fns.end(), f) != fns.end()) continue;
            fns.push_back(f);
            const std::uint32_t n = (std::uint32_t)(c.count ? c.count : c.capacity);
            if (NonLeaf(im, f, 0x800) && (ComparesWith(im, f, 0x800, n) || ComparesWith(im, f, 0x800, n - 1))) { framer = f; ++framers; }
        }
        if (framers == 1) { out = ProtTable{ c.vec, c.vec + rtx::sops::kProtBuffer, c.count, c.capacity, framer }; return true; }
        if (c.capacity > bestCap) { bestCap = c.capacity; best = ProtTable{ c.vec, c.vec + rtx::sops::kProtBuffer, c.count, c.capacity, 0 }; }
    }
    out = best;
    return bestCap > 0;
}

// The handler the descriptor for `op` names (thunks followed), mapped; 0 when the table has none.
// lenOut takes the descriptor's wire length.
inline std::uint64_t ProtHandler(const Image& im, const ProtTable& t, int op, int* lenOut = nullptr) {
    if (op < 0 || op >= t.count) return 0;
    const std::uint64_t d = Q(t.begin + (std::uint64_t)op * 8);
    if (!d || !(im.PtrData(d) || im.PtrRdata(d))) return 0;
    const std::uint64_t dm = im.Map(d);
    if (D(dm + rtx::sops::kDescOp) != op) return 0;
    if (lenOut) *lenOut = D(dm + rtx::sops::kDescLen);
    const std::uint64_t vt = Q(dm + rtx::sops::kDescVtbl);
    if (!im.PtrRdata(vt)) return 0;
    const std::uint64_t fn = Q(im.Map(vt) + rtx::sops::kVtblHandler);
    if (!im.PtrText(fn)) return 0;
    std::uint64_t f = im.Map(fn);
    for (int hop = 0; hop < 2 && im.InText(f) && f + 16 <= im.textHi; ++hop) {
        const auto* b = reinterpret_cast<const std::uint8_t*>(f);
        std::int32_t rel;
        if (b[0] == 0xE9) { std::memcpy(&rel, b + 1, 4); f = f + 5 + rel; }
        else if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xC1 && b[4] == 0xE9) { std::memcpy(&rel, b + 5, 4); f = f + 9 + rel; }
        else break;
    }
    return im.InText(f) ? f : 0;
}
// The descriptor's wire length alone (-1 var-byte, -2 var-short), 0x7FFF when the table has none.
inline int ProtLength(const Image& im, const ProtTable& t, int op) {
    if (op < 0 || op >= t.count) return 0x7FFF;
    const std::uint64_t d = Q(t.begin + (std::uint64_t)op * 8);
    if (!d || !(im.PtrData(d) || im.PtrRdata(d))) return 0x7FFF;
    const std::uint64_t dm = im.Map(d);
    return D(dm + rtx::sops::kDescOp) == op ? D(dm + rtx::sops::kDescLen) : 0x7FFF;
}

// Every rip-relative lea target inside .rdata (vtable starts among other constants), as sorted
// unique mapped addresses.
inline void LeaTargetsInRdata(const Image& im, std::vector<std::uint64_t>& out) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(im.textLo);
    const std::size_t n = (std::size_t)(im.textHi - im.textLo);
    for (std::size_t i = 0; i + 7 <= n; ++i) {
        if ((b[i] != 0x48 && b[i] != 0x4C) || b[i + 1] != 0x8D || (b[i + 2] & 0xC7) != 0x05) continue;
        std::int32_t disp; std::memcpy(&disp, b + i + 3, 4);
        const std::uint64_t t = im.textLo + i + 7 + (std::int64_t)disp;
        if (im.InRdata(t)) out.push_back(t);
        i += 6;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// The entity vtable family: the vtables in .rdata (lea-referenced, slot 0 in .text) that share
// slots 1, 2, 5 and 6, in the group holding the 69-slot NPC class. A vtable's slot count runs to
// the next referenced start or the first non-code qword.
struct Family {
    struct Member { std::uint64_t vt; int slots; std::uint64_t slot7; };   // mapped
    std::vector<Member> members;
    std::uint64_t shared[4] = {};
    int groups = 0;   // tuple groups of 12 or more considered
};
inline bool EntityFamily(const Image& im, Family& out) {
    std::vector<std::uint64_t> starts;
    LeaTargetsInRdata(im, starts);
    struct Key { std::uint64_t s[4]; bool operator==(const Key& o) const { return std::memcmp(s, o.s, sizeof(s)) == 0; } };
    struct KeyHash { std::size_t operator()(const Key& k) const { std::uint64_t h = 1469598103934665603ull; for (std::uint64_t v : k.s) { h ^= v; h *= 1099511628211ull; } return (std::size_t)h; } };
    std::unordered_map<Key, std::vector<std::uint64_t>, KeyHash> groups;
    for (std::uint64_t v : starts) {
        if (v & 7) continue;
        if (v + 7 * 8 + 8 > im.rdataHi || !im.PtrText(Q(v))) continue;
        Key k{ { Q(v + 8), Q(v + 16), Q(v + 40), Q(v + 48) } };
        if (!im.PtrText(k.s[0]) || !im.PtrText(k.s[1]) || !im.PtrText(k.s[2]) || !im.PtrText(k.s[3])) continue;
        groups[k].push_back(v);
    }
    auto slotsOf = [&](std::uint64_t v) {
        int n = 0;
        for (std::uint64_t p = v; p + 8 <= im.rdataHi && n < 300; p += 8, ++n) {
            if (n && std::binary_search(starts.begin(), starts.end(), p)) break;
            if (!im.PtrText(Q(p))) break;
        }
        return n;
    };
    Family best; int found = 0;
    for (const auto& g : groups) {
        if (g.second.size() < 12) continue;
        Family f;
        int mx = 0;
        for (std::uint64_t v : g.second) {
            const int n = slotsOf(v);
            if (n > mx) mx = n;
            f.members.push_back({ v, n, n > 7 ? im.Map(Q(v + 7 * 8)) : 0 });
        }
        ++best.groups;
        if (mx < 60) continue;
        std::memcpy(f.shared, g.first.s, sizeof(f.shared));
        f.groups = best.groups;
        best.members = f.members; std::memcpy(best.shared, f.shared, sizeof(best.shared));
        ++found;
    }
    if (found != 1) return false;
    out = best;
    return true;
}
// The slot 7 functions of the members with `slots` slots; returns how many there are (cap kept).
inline int FamilySlot7(const Family& f, int slots, std::uint64_t* out, int cap) {
    int n = 0;
    for (const auto& m : f.members) if (m.slots == slots) { if (n < cap) out[n] = m.slot7; ++n; }
    return n;
}

// First `lea r64,[rip+disp]` within `span` bytes of `fn` whose target lies in writable data,
// mapped; 0 when none.
inline std::uint64_t LeaIntoData(const Image& im, std::uint64_t fn, std::size_t span) {
    if (!im.InText(fn)) return 0;
    if (fn + span > im.textHi) span = (std::size_t)(im.textHi - fn);
    const auto* b = reinterpret_cast<const std::uint8_t*>(fn);
    for (std::size_t i = 0; i + 7 <= span; ++i) {
        if ((b[i] != 0x48 && b[i] != 0x4C) || b[i + 1] != 0x8D || (b[i + 2] & 0xC7) != 0x05) continue;
        std::int32_t disp; std::memcpy(&disp, b + i + 3, 4);
        const std::uint64_t t = fn + i + 7 + (std::int64_t)disp;
        if (im.InData(t)) return t;
    }
    return 0;
}

}  // namespace rtx::codefind
