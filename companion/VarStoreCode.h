#pragma once
// A var store's hash map, read from the store's own lookup routine rather than from a compiled
// offset. Every var store class (player, client, clan, clan settings, group) is a hash table whose
// lookup takes the bucket count and the bucket array off the store, divides the var id by the count
// and walks the chain: `mov r32,[store+N+8]; mov r64,[store+N]; ... div r64; ... mov rax,[rax+next]`.
// N is where the bucket array sits in the store, N + 8 the bucket count, N + 0x10 the element count.
// Slot 1 of a store's vtable is that lookup; slot 2 sets a var and also writes the change-stamp map
// (player store only), the same shape on another N.
//
// Shared by the reader (bytes read from the process) and the companion (the mapped image).
#include "OpPrints.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace rtx::varcode {

// One hash lookup in a routine: bucket array at +buckets off the register it was read from, the
// chain link at +next in a node (0 when the walk was not seen).
struct Lookup { std::uint32_t buckets = 0, next = 0; int base = -1; };

// The hash lookups in the routine at `code` (n bytes; the scan stops where the routine ends, see
// below, or at an encoding the decoder does not know). A lookup counts when a 64-bit
// load of [reg+N] and a load of [reg+N+8] off the same register come within the last eight loads
// before an unsigned 64-bit divide. The routine ends at the first return or jump past every forward
// branch seen so far (its own code needs no unwind data to bound it). `thunk`, when set, receives the target of a leading
// `jmp rel32` (a slot that forwards: the caller reads that routine instead), relative to `code`.
inline std::vector<Lookup> Lookups(const std::uint8_t* code, std::size_t n, std::int64_t* thunk = nullptr) {
    std::vector<Lookup> out;
    struct Load { int base, dst; std::uint32_t disp; bool w; };
    Load recent[8]; int nrec = 0;
    int open = -1;   // the lookup whose chain walk is still to be seen
    std::size_t at = 0, reach = 0;
    for (int k = 0; at < n && k < 400; ++k) {
        rtx::opprints::Insn in;
        if (!rtx::opprints::Decode(code + at, n - at < 15 ? n - at : 15, in)) break;
        const std::uint8_t* p = code + at;
        std::size_t i = 0;
        while (i < in.len && (p[i] == 0x66 || p[i] == 0x67 || p[i] == 0xF2 || p[i] == 0xF3)) ++i;
        std::uint8_t rex = 0;
        if (i < in.len && (p[i] & 0xF0) == 0x40) rex = p[i++];
        const std::uint8_t op = i < in.len ? p[i] : 0;
        if (k < 3 && op == 0xE9 && in.len == i + 5 && thunk) {
            std::int32_t rel; std::memcpy(&rel, p + i + 1, 4);
            *thunk = (std::int64_t)(at + in.len) + rel;
            return out;
        }
        // mov r, [base + disp8/disp32] (8B /r, mod 1 or 2, no SIB unless the base is rsp/r12)
        if (op == 0x8B && i + 1 < in.len) {
            const std::uint8_t m = p[i + 1], mod = m >> 6;
            int rm = m & 7;
            const int reg = ((m >> 3) & 7) | ((rex & 4) ? 8 : 0);
            if ((mod == 1 || mod == 2) && rm != 4 && in.dispSize) {
                rm |= (rex & 1) ? 8 : 0;
                std::uint32_t d = 0;
                if (in.dispSize == 1) d = (std::uint32_t)(std::int32_t)(std::int8_t)p[in.dispAt];
                else std::memcpy(&d, p + in.dispAt, 4);
                if (open >= 0 && !out[(std::size_t)open].next && rm == reg && d < 0x100) out[(std::size_t)open].next = d;
                if (nrec == 8) { std::memmove(recent, recent + 1, sizeof(Load) * 7); --nrec; }
                recent[nrec++] = Load{ rm, reg, d, (rex & 8) != 0 };
            }
        }
        // div r64: REX.W F7 /6, register operand
        if (op == 0xF7 && (rex & 8) && i + 1 < in.len && (p[i + 1] >> 6) == 3 && ((p[i + 1] >> 3) & 7) == 6) {
            for (int a = 0; a < nrec; ++a) {
                if (!recent[a].w) continue;
                for (int b = 0; b < nrec; ++b)
                    if (b != a && recent[b].base == recent[a].base && recent[b].disp == recent[a].disp + 8) {
                        bool dup = false;
                        for (const auto& l : out) if (l.buckets == recent[a].disp && l.base == recent[a].base) dup = true;
                        if (!dup) { out.push_back(Lookup{ recent[a].disp, 0, recent[a].base }); open = (int)out.size() - 1; }
                        a = nrec; break;
                    }
            }
            nrec = 0;
        }
        // the routine ends at a return or jump no earlier branch reaches past
        std::int64_t target = -1;
        if ((op >= 0x70 && op <= 0x7F) || op == 0xEB) target = (std::int64_t)(at + in.len) + (std::int8_t)p[in.len - 1];
        else if (op == 0xE9) { std::int32_t rel; std::memcpy(&rel, p + in.len - 4, 4); target = (std::int64_t)(at + in.len) + rel; }
        else if (op == 0x0F && i + 1 < in.len && p[i + 1] >= 0x80 && p[i + 1] <= 0x8F) { std::int32_t rel; std::memcpy(&rel, p + in.len - 4, 4); target = (std::int64_t)(at + in.len) + rel; }
        if (target > (std::int64_t)reach && target < (std::int64_t)n) reach = (std::size_t)target;
        const bool leaves = in.kind == rtx::opprints::kRet || op == 0xE9 || op == 0xEB || (op == 0xFF && i + 1 < in.len && ((p[i + 1] >> 3) & 7) == 4);
        at += in.len;
        if (leaves && at > reach) break;
    }
    return out;
}

// The store's maps from its vtable: `read(addr, buf, n)` fetches bytes, `vt` is the vtable. `values`
// gets the bucket offset of the var values (slot 1) and `valueNext` its node chain link; `stamps` the
// change-stamp map's (slot 2, another N than the values'; 0 when the class keeps none) and `stampNext`
// its chain link. False when slot 1 shows no lookup.
template <class Read>
bool StoreMaps(Read&& read, std::uint64_t vt, std::uint32_t& values, std::uint32_t& valueNext, std::uint32_t* stamps = nullptr,
               std::uint32_t* stampNext = nullptr) {
    values = valueNext = 0;
    if (stamps) *stamps = 0;
    if (stampNext) *stampNext = 0;
    auto routine = [&](int slot, std::vector<Lookup>& out) {
        std::uint64_t fn = 0;
        if (!read(vt + (std::uint64_t)slot * 8, &fn, 8) || !fn) return false;
        for (int hop = 0; hop < 2; ++hop) {
            std::uint8_t buf[0x600];
            if (!read(fn, buf, sizeof(buf))) return false;
            std::int64_t thunk = 0;
            out = Lookups(buf, sizeof(buf), &thunk);
            if (!thunk) return true;
            fn = fn + (std::uint64_t)thunk;
        }
        return false;
    };
    std::vector<Lookup> get;
    if (!routine(1, get) || get.size() != 1) return false;
    values = get[0].buckets; valueNext = get[0].next;
    if (stamps) {
        std::vector<Lookup> set;
        if (routine(2, set)) {
            const Lookup* other = nullptr; int n = 0;
            for (const auto& l : set) if (l.buckets != values) { other = &l; ++n; }
            if (n == 1) { *stamps = other->buckets; if (stampNext) *stampNext = other->next; }
        }
    }
    return values != 0;
}

}  // namespace rtx::varcode
