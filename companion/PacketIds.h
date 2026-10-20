#pragma once
// Server packets by identity, so an update that renumbers the opcodes needs no re-pin.
//
// The client handles each server packet with a callback whose compiler name it keeps (a type
// descriptor ".?AV<lambda_...>@@"). Chain, by structure only: descriptor -> the 8-byte getter
// `lea rax,[descriptor]; ret` -> slot 3 of the callback's wrapper vtable (slot 2 is the routine it
// runs) -> the registrar site `lea rax,[vtable] ... lea rdx,[packet descriptor + 0x10]` -> the
// packet descriptor global, which a tiny static initialiser constructs with the opcode and the wire
// length as constants (`sub rsp,28; mov edx,OP; lea rcx,[descriptor]; lea r8d,[rdx+N]; call ctor`).
//
// A callback whose source did not change keeps its name across builds; the ones between two named
// ones keep their order inside the registrar. So a packet of a reference build (PacketRefTable.h)
// is found in another build by name, else by its place between named neighbours of equal count.
// Measured: 950-1 to 951-1 and to the plugin beta, 219 of 221 carried, every one with its length.
//
// ServerOps.h keeps the reference build's numbering as the packets' stable ids: the companion
// translates each live opcode to its reference id where it captures it, so every decoder downstream
// reads one numbering. Works on the exe's file bytes; the reader and the companion each run it.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtx::pktid {

struct Row {
    int op = -1, len = 0;
    std::string name;               // the callback's type descriptor name
    std::uint32_t builder = 0;      // the registrar function (rva)
    std::uint32_t order = 0;        // the registrar site (rva): the order inside the registrar
    std::uint32_t ctor = 0;         // the descriptor constructor (rva); the main family is the server packets
    std::uint32_t invoke = 0;       // the routine the callback runs (rva)
};

struct RefRow { int op, len; const char* name; int group, pos; };

// Zone sub-packets: the items a zone update carries, each `[sub id][body]`. Their descriptors are a
// second family of the same static initialisers ({id, wire length}, ids 0..n-1). At its registrar
// site a sub either takes a server packet's handler (`mov rcx,[packet descriptor + 0x48]`: the twin,
// whose body it shares) or gets its own callback (`lea rax,[vtable]`, named like a packet's).
struct ZoneRow {
    int sub = -1, len = 0, twin = -1; std::string own; std::uint32_t invoke = 0;
    std::string name;           // the callback the site binds: its own, or the twin packet's
    std::uint32_t callee = 0;   // the routine the handler ends in (rva), 0 when none
};
struct ZoneRef { int sub, len, twin; const char* own; };
// Zone subs added after the reference build, under ids above its table (the ids the decoders use).
// A live sub is one of these by the callback its site binds, else by ending in the routine the live
// sub carried as `variantOf` ends in (the plain form it extends). Variable-length bodies; the walk
// reads them in EventZone.h.
struct ZoneAdded { int sub, len; const char* name; int variantOf; };
inline constexpr ZoneAdded kZoneAdded[] = {
    { 18, -1, ".?AV<lambda_aa290a1721078ce0eb15a6fb73fa0766>@@", 0 },   // obj_add, smart quantity
    { 19, -1, ".?AV<lambda_695e5d3ef468e5b0afe30c9acbdae4fd>@@", 9 },   // obj_add with owner, smart quantity
    { 20, -1, ".?AV<lambda_5128eaef7de8006c9346e55911f06f52>@@", 6 },   // obj_count, smart quantities
};
inline constexpr std::size_t kZoneAddedCount = sizeof(kZoneAdded) / sizeof(kZoneAdded[0]);
// Zone subs with their own handler are read like packets under the id kZoneOwn + sub.
inline constexpr int kZoneOwn = 0x200;
// The fixed head of a variable-length packet (a zone update's plane and offsets) is read like a
// packet of its own under kHead + the packet's id, through the packet's own handler.
inline constexpr int kHead = 0x300;

namespace detail {
struct Sec { char name[9]; std::uint32_t va, vsize, raw, rawSize; };
struct Pe {
    const std::uint8_t* d = nullptr; std::size_t n = 0; std::uint64_t base = 0;
    std::vector<Sec> secs;
    const Sec* Find(const char* nm) const { for (const auto& s : secs) if (!std::strcmp(s.name, nm)) return &s; return nullptr; }
    std::int64_t FoOf(std::uint32_t rva) const {
        for (const auto& s : secs) if (rva >= s.va && rva < s.va + s.rawSize) return (std::int64_t)s.raw + (rva - s.va);
        return -1;
    }
    std::int64_t RvaOf(std::size_t fo) const {
        for (const auto& s : secs) if (fo >= s.raw && fo < (std::size_t)s.raw + s.rawSize) return (std::int64_t)s.va + (std::int64_t)(fo - s.raw);
        return -1;
    }
};
inline bool Open(const std::uint8_t* d, std::size_t n, Pe& pe) {
    if (n < 0x400 || d[0] != 'M' || d[1] != 'Z') return false;
    std::uint32_t lf; std::memcpy(&lf, d + 0x3C, 4);
    if ((std::uint64_t)lf + 0x108 > n || std::memcmp(d + lf, "PE\0\0", 4)) return false;
    std::uint16_t nsec, opt; std::memcpy(&nsec, d + lf + 6, 2); std::memcpy(&opt, d + lf + 20, 2);
    std::memcpy(&pe.base, d + lf + 24 + 24, 8);
    std::size_t off = lf + 24 + opt;
    for (unsigned i = 0; i < nsec && off + 40 <= n; ++i, off += 40) {
        Sec s{}; std::memcpy(s.name, d + off, 8); s.name[8] = 0;
        std::memcpy(&s.vsize, d + off + 8, 4); std::memcpy(&s.va, d + off + 12, 4);
        std::memcpy(&s.rawSize, d + off + 16, 4); std::memcpy(&s.raw, d + off + 20, 4);
        if ((std::uint64_t)s.raw + s.rawSize > n) s.rawSize = s.raw < n ? (std::uint32_t)(n - s.raw) : 0;
        pe.secs.push_back(s);
    }
    pe.d = d; pe.n = n;
    return pe.Find(".text") && pe.Find(".rdata") && pe.Find(".data") && pe.Find(".pdata");
}
inline std::int32_t S32(const std::uint8_t* p) { std::int32_t v; std::memcpy(&v, p, 4); return v; }
}  // namespace detail

// Every server packet the exe's registrars bind a callback to. False with `why` when the chain is not
// recognised (no main family found).
inline bool Table(const std::uint8_t* file, std::size_t size, std::vector<Row>& out, std::string* why = nullptr, std::vector<ZoneRow>* zone = nullptr) {
    using namespace detail;
    out.clear();
    if (zone) zone->clear();
    Pe pe;
    if (!Open(file, size, pe)) { if (why) *why = "not a PE image with .text/.rdata/.data/.pdata"; return false; }
    const Sec& T = *pe.Find(".text"); const Sec& R = *pe.Find(".rdata"); const Sec& D = *pe.Find(".data"); const Sec& P = *pe.Find(".pdata");
    const std::uint8_t* text = file + T.raw; const std::size_t tn = T.rawSize;
    // function bounds from the unwind data
    std::vector<std::pair<std::uint32_t, std::uint32_t>> funcs;
    for (std::size_t fo = P.raw; fo + 12 <= (std::size_t)P.raw + std::min(P.vsize, P.rawSize); fo += 12) {
        std::uint32_t b, e; std::memcpy(&b, file + fo, 4); std::memcpy(&e, file + fo + 4, 4);
        if (b && e > b) funcs.emplace_back(b, e);
    }
    std::sort(funcs.begin(), funcs.end());
    auto funcOf = [&](std::uint32_t rva) -> std::uint32_t {
        auto it = std::upper_bound(funcs.begin(), funcs.end(), std::make_pair(rva, 0xFFFFFFFFu));
        if (it == funcs.begin()) return 0;
        --it;
        return rva < it->second ? it->first : 0;
    };
    // type descriptor names: ".?A[VUW]<printable>@@\0", the descriptor 16 bytes before the name
    std::unordered_map<std::uint32_t, std::string> tds;
    for (std::size_t i = 0; i + 8 < size; ++i) {
        if (file[i] != '.' || file[i + 1] != '?' || file[i + 2] != 'A' || (file[i + 3] != 'V' && file[i + 3] != 'U' && file[i + 3] != 'W')) continue;
        std::size_t j = i + 4, lim = std::min(size - 3, i + 4 + 2000);
        bool ok = false;
        for (; j < lim; ++j) {
            const std::uint8_t c = file[j];
            if (c == '@' && file[j + 1] == '@' && file[j + 2] == 0 && j > i + 4) { ok = true; break; }
            if (c < 0x21 || c > 0x7E) break;
        }
        if (!ok) continue;
        const std::int64_t r = pe.RvaOf(i);
        if (r >= 16) tds[(std::uint32_t)r - 16] = std::string((const char*)file + i, j + 2 - i);
        i = j + 2;
    }
    // getters: `lea rax,[descriptor]; ret`
    std::unordered_map<std::uint32_t, std::uint32_t> getterTd;   // getter rva -> descriptor
    for (std::size_t i = 0; i + 8 <= tn; ++i) {
        if ((text[i] != 0x48 && text[i] != 0x4C) || text[i + 1] != 0x8D || (text[i + 2] & 0xC7) != 0x05) continue;
        const std::uint32_t tgt = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)i + 7 + S32(text + i + 3));
        if (text[i + 7] == 0xC3 && tds.count(tgt)) getterTd[T.va + (std::uint32_t)i] = tgt;
    }
    // wrapper vtables: an .rdata slot holding a getter is slot 3; the lowest per descriptor
    std::map<std::uint32_t, std::uint32_t> tdVt;   // descriptor -> vtable
    for (std::size_t fo = R.raw; fo + 8 <= (std::size_t)R.raw + R.rawSize; fo += 8) {
        std::uint64_t q; std::memcpy(&q, file + fo, 8);
        if (q < pe.base + T.va || q >= pe.base + T.va + T.vsize) continue;
        auto g = getterTd.find((std::uint32_t)(q - pe.base));
        if (g == getterTd.end()) continue;
        const std::int64_t slot = pe.RvaOf(fo);
        if (slot < 24) continue;
        const std::uint32_t vt = (std::uint32_t)slot - 24;
        auto it = tdVt.find(g->second);
        if (it == tdVt.end() || vt < it->second) tdVt[g->second] = vt;
    }
    std::unordered_map<std::uint32_t, std::uint32_t> vtTd;
    for (const auto& kv : tdVt) vtTd[kv.second] = kv.first;
    // static initialisers: the descriptor object -> (opcode, length, constructor), when there is one
    struct Init { int op, len; std::uint32_t ctor; int n; };
    std::unordered_map<std::uint32_t, Init> inits;
    for (std::size_t i = 0; i + 40 <= tn; ++i) {
        if (text[i] != 0x48 || text[i + 1] != 0x83 || text[i + 2] != 0xEC || text[i + 3] != 0x28) continue;
        const std::uint8_t nx = text[i + 4];
        if (nx != 0xBA && nx != 0x41 && nx != 0x33 && nx != 0x45 && nx != 0x44 && nx != 0x83 && nx != 0x48) continue;
        bool hasEdx = false, hasR8 = false; std::int64_t edx = 0, r8 = 0; std::uint32_t rcx = 0; bool hasRcx = false;
        std::size_t k = i + 4;
        for (int step = 0; step < 12 && k + 8 <= tn; ++step) {
            const std::uint8_t* b = text + k;
            if (b[0] == 0xBA) { edx = S32(b + 1); hasEdx = true; k += 5; }
            else if (b[0] == 0x41 && b[1] == 0xB8) { r8 = S32(b + 2); hasR8 = true; k += 6; }
            else if (b[0] == 0x33 && b[1] == 0xD2) { edx = 0; hasEdx = true; k += 2; }
            else if (b[0] == 0x45 && b[1] == 0x33 && b[2] == 0xC0) { r8 = 0; hasR8 = true; k += 3; }
            else if (b[0] == 0x44 && b[1] == 0x8D && b[2] == 0x42 && hasEdx) { r8 = edx + (std::int8_t)b[3]; hasR8 = true; k += 4; }
            else if (b[0] == 0x44 && b[1] == 0x8D && b[2] == 0x82 && hasEdx) { r8 = edx + S32(b + 3); hasR8 = true; k += 7; }
            else if (b[0] == 0x44 && b[1] == 0x8B && b[2] == 0xC2 && hasEdx) { r8 = edx; hasR8 = true; k += 3; }   // mov r8d,edx: length = id
            else if (b[0] == 0x41 && b[1] == 0x8D && b[2] == 0x50 && hasR8) { edx = r8 + (std::int8_t)b[3]; hasEdx = true; k += 4; }
            else if (b[0] == 0x41 && b[1] == 0x8D && b[2] == 0x90 && hasR8) { edx = r8 + S32(b + 3); hasEdx = true; k += 7; }
            else if (b[0] == 0x41 && b[1] == 0x83 && b[2] == 0xC8 && b[3] == 0xFF) { r8 = -1; hasR8 = true; k += 4; }
            else if (b[0] == 0x83 && b[1] == 0xCA && b[2] == 0xFF) { edx = -1; hasEdx = true; k += 3; }
            else if (b[0] == 0x48 && b[1] == 0x8D && b[2] == 0x0D) { rcx = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)k + 7 + S32(b + 3)); hasRcx = true; k += 7; }
            else if (b[0] == 0xE8) {
                const std::uint32_t callee = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)k + 5 + S32(b + 1));
                if (hasRcx && hasEdx && hasR8 && rcx >= D.va && rcx < D.va + D.vsize) {
                    auto& in = inits[rcx];
                    if (!in.n) in = Init{ (int)edx, (int)r8, callee, 0 };
                    ++in.n;
                }
                break;
            }
            else break;
        }
    }
    // registrar sites: `lea rax,[vtable]` then `lea rdx,[descriptor + 0x10]` within 48 bytes
    std::map<std::uint32_t, int> ctorCount;
    std::vector<Row> rows;
    for (std::size_t i = 0; i + 7 <= tn; ++i) {
        if (text[i] != 0x48 || text[i + 1] != 0x8D || text[i + 2] != 0x05) continue;
        const std::uint32_t vt = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)i + 7 + S32(text + i + 3));
        auto v = vtTd.find(vt);
        if (v == vtTd.end()) continue;
        std::int64_t at = -1;
        for (std::size_t j = i + 7; j + 7 <= std::min(tn, i + 55); ++j)
            if (text[j] == 0x48 && text[j + 1] == 0x8D && text[j + 2] == 0x15) { at = (std::int64_t)j; break; }
        if (at < 0) continue;
        const std::uint32_t arg = (std::uint32_t)((std::int64_t)T.va + at + 7 + S32(text + at + 3));
        auto in = inits.find(arg - 0x10);
        if (in == inits.end() || in->second.n != 1) continue;
        Row r;
        r.op = in->second.op; r.len = in->second.len; r.ctor = in->second.ctor;
        r.name = tds[v->second]; r.order = T.va + (std::uint32_t)i; r.builder = funcOf(r.order);
        const std::int64_t vfo = pe.FoOf(vt);
        if (vfo >= 0 && (std::size_t)vfo + 24 <= size) { std::uint64_t q; std::memcpy(&q, file + vfo + 16, 8); r.invoke = (std::uint32_t)(q - pe.base); }
        rows.push_back(r);
        ++ctorCount[r.ctor];
    }
    std::uint32_t main = 0; int best = 0;
    for (const auto& kv : ctorCount) if (kv.second > best) { best = kv.second; main = kv.first; }
    for (auto& r : rows) if (r.ctor == main) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) { return a.op < b.op; });
    if (out.size() < 100) { if (why) *why = "only " + std::to_string(out.size()) + " packet callbacks recognised"; return false; }
    if (zone) {
        // the zone family: the largest other family whose ids run 0..n-1
        std::unordered_map<std::uint32_t, int> serverOp;   // server packet descriptor -> opcode
        std::map<std::uint32_t, std::vector<std::uint32_t>> fam;
        for (const auto& kv : inits) {
            if (kv.second.n != 1) continue;
            if (kv.second.ctor == main) serverOp[kv.first] = kv.second.op;
            else fam[kv.second.ctor].push_back(kv.first);
        }
        const std::vector<std::uint32_t>* zf = nullptr;
        for (const auto& kv : fam) {
            if (kv.second.size() < 8 || kv.second.size() > 64) continue;
            std::vector<int> ids; for (std::uint32_t o : kv.second) ids.push_back(inits[o].op);
            std::sort(ids.begin(), ids.end());
            bool dense = true; for (std::size_t k = 0; k < ids.size(); ++k) if (ids[k] != (int)k) dense = false;
            if (dense && (!zf || kv.second.size() > zf->size())) zf = &kv.second;
        }
        if (zf) {
            // the routine a handler ends in: the last call or jump out of its function, thunks
            // (`add rcx,8; jmp`) followed first
            auto lastCallee = [&](std::uint32_t rva) -> std::uint32_t {
                for (int hop = 0; hop < 4; ++hop) {
                    const std::int64_t fo = pe.FoOf(rva);
                    if (fo < 0 || (std::size_t)fo + 9 > size) return 0;
                    const std::uint8_t* b = file + fo;
                    if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xC1 && b[3] == 0x08 && b[4] == 0xE9) rva = (std::uint32_t)((std::int64_t)rva + 9 + S32(b + 5));
                    else if (b[0] == 0xE9) rva = (std::uint32_t)((std::int64_t)rva + 5 + S32(b + 1));
                    else break;
                }
                auto f = std::upper_bound(funcs.begin(), funcs.end(), std::make_pair(rva, 0xFFFFFFFFu));
                if (f == funcs.begin()) return 0;
                --f;
                if (rva >= f->second) return 0;
                std::uint32_t out = 0;
                for (std::uint32_t k = f->first; k + 5 <= f->second; ++k) {
                    const std::int64_t fo = pe.FoOf(k);
                    if (fo < 0) break;
                    const std::uint8_t* b = file + fo;
                    if (b[0] != 0xE8 && b[0] != 0xE9) continue;
                    const std::uint32_t t = (std::uint32_t)((std::int64_t)k + 5 + S32(b + 1));
                    if ((t >= f->first && t < f->second) || t < T.va || t >= T.va + T.vsize) continue;
                    out = t;
                }
                return out;
            };
            std::unordered_map<int, const Row*> byOp;
            for (const auto& r : out) byOp[r.op] = &r;
            std::unordered_map<std::uint32_t, ZoneRow> byDesc;
            for (std::uint32_t o : *zf) { ZoneRow z; z.sub = inits[o].op; z.len = inits[o].len; byDesc[o] = z; }
            std::unordered_set<std::uint32_t> linked;
            for (std::size_t i = 0; i + 7 <= tn; ++i) {
                if (text[i] != 0x48 || text[i + 1] != 0x8D || text[i + 2] != 0x15) continue;   // lea rdx,[zone descriptor + 0x10]
                const std::uint32_t z = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)i + 7 + S32(text + i + 3)) - 0x10;
                auto it = byDesc.find(z);
                if (it == byDesc.end() || linked.count(z)) continue;
                for (std::size_t j = i; j-- > (i > 0x50 ? i - 0x50 : 0); ) {
                    if (text[j] == 0x48 && text[j + 1] == 0x8B && text[j + 2] == 0x0D) {        // mov rcx,[server descriptor + 0x48]
                        const std::uint32_t src = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)j + 7 + S32(text + j + 3)) - 0x48;
                        auto s = serverOp.find(src);
                        if (s != serverOp.end()) {
                            it->second.twin = s->second;
                            auto p = byOp.find(s->second);
                            if (p != byOp.end()) { it->second.name = p->second->name; it->second.callee = lastCallee(p->second->invoke); }
                            linked.insert(z); break;
                        }
                    }
                    if (text[j] == 0x48 && text[j + 1] == 0x8D && text[j + 2] == 0x05) {        // lea rax,[vtable]: its own callback
                        const std::uint32_t vt = (std::uint32_t)((std::int64_t)T.va + (std::int64_t)j + 7 + S32(text + j + 3));
                        auto v = vtTd.find(vt);
                        if (v != vtTd.end()) {
                            it->second.own = it->second.name = tds[v->second];
                            const std::int64_t vfo = pe.FoOf(vt);
                            if (vfo >= 0 && (std::size_t)vfo + 24 <= size) { std::uint64_t q; std::memcpy(&q, file + vfo + 16, 8); it->second.invoke = (std::uint32_t)(q - pe.base); }
                            it->second.callee = lastCallee(it->second.invoke);
                            linked.insert(z); break;
                        }
                    }
                }
            }
            for (const auto& kv : byDesc) zone->push_back(kv.second);
            std::sort(zone->begin(), zone->end(), [](const ZoneRow& a, const ZoneRow& b) { return a.sub < b.sub; });
        }
    }
    return true;
}

// Reference rows from a table: group = the registrar's rank by address, pos = the place inside it.
inline std::vector<RefRow> Groups(const std::vector<Row>& t, std::vector<std::string>& names) {
    std::set<std::uint32_t> bs; for (const auto& r : t) bs.insert(r.builder);
    std::vector<std::uint32_t> bl(bs.begin(), bs.end());
    std::vector<const Row*> sorted; for (const auto& r : t) sorted.push_back(&r);
    std::sort(sorted.begin(), sorted.end(), [](const Row* a, const Row* b) { return a->order < b->order; });
    std::map<std::uint32_t, int> pos;
    std::vector<RefRow> out;
    names.clear(); names.reserve(t.size());
    for (const Row* r : sorted) names.push_back(r->name);
    std::size_t k = 0;
    for (const Row* r : sorted) {
        const int g = (int)(std::lower_bound(bl.begin(), bl.end(), r->builder) - bl.begin());
        out.push_back(RefRow{ r->op, r->len, names[k++].c_str(), g, pos[r->builder]++ });
    }
    return out;
}

// Where each reference packet sits in the live table: by name, then by place between named
// neighbours (the registrar pair is the one most of a registrar's names went to; a gap pairs only
// when it holds as many packets in both builds; last, a length only one leftover has on each side of
// the pair). `toLive[refOp]` and `toRef[liveOp]` are -1 where nothing carried; `byName`, `byOrder`
// and `byLength` count how. A carried packet whose length differs is dropped
// and counted in `lenDiffer`.
struct Carried { int toLive[512]; int toRef[512]; int byName = 0, byOrder = 0, byLength = 0, lenDiffer = 0, refCount = 0; };
inline Carried Carry(const RefRow* ref, std::size_t n, const std::vector<Row>& live) {
    Carried c;
    for (int i = 0; i < 512; ++i) c.toLive[i] = c.toRef[i] = -1;
    c.refCount = (int)n;
    std::unordered_map<std::string, const Row*> ln;
    for (const auto& r : live) ln[r.name] = &r;
    auto set = [&](const RefRow& a, const Row& b, bool name) {
        if (a.op < 0 || a.op >= 512 || b.op < 0 || b.op >= 512 || c.toLive[a.op] >= 0 || c.toRef[b.op] >= 0) return;
        if (a.len != b.len) { ++c.lenDiffer; return; }
        c.toLive[a.op] = b.op; c.toRef[b.op] = a.op;
        if (name) ++c.byName; else ++c.byOrder;
    };
    for (std::size_t i = 0; i < n; ++i) { auto it = ln.find(ref[i].name); if (it != ln.end()) set(ref[i], *it->second, true); }
    // registrar pairs by majority of names
    std::map<std::pair<int, std::uint32_t>, int> votes;
    for (std::size_t i = 0; i < n; ++i) { auto it = ln.find(ref[i].name); if (it != ln.end()) ++votes[{ ref[i].group, it->second->builder }]; }
    std::map<int, std::pair<std::uint32_t, int>> pairOf;
    for (const auto& kv : votes) {
        auto& p = pairOf[kv.first.first];
        if (kv.second > p.second) p = { kv.first.second, kv.second };
    }
    for (const auto& gp : pairOf) {
        std::vector<const RefRow*> sa;
        for (std::size_t i = 0; i < n; ++i) if (ref[i].group == gp.first) sa.push_back(&ref[i]);
        std::sort(sa.begin(), sa.end(), [](const RefRow* x, const RefRow* y) { return x->pos < y->pos; });
        std::vector<const Row*> sb;
        for (const auto& r : live) if (r.builder == gp.second.first) sb.push_back(&r);
        std::sort(sb.begin(), sb.end(), [](const Row* x, const Row* y) { return x->order < y->order; });
        std::unordered_map<std::string, int> posb;
        for (std::size_t j = 0; j < sb.size(); ++j) posb[sb[j]->name] = (int)j;
        std::vector<std::pair<int, int>> edges{ { -1, -1 } };
        for (std::size_t i = 0; i < sa.size(); ++i) { auto it = posb.find(sa[i]->name); if (it != posb.end()) edges.push_back({ (int)i, it->second }); }
        edges.push_back({ (int)sa.size(), (int)sb.size() });
        for (std::size_t e = 0; e + 1 < edges.size(); ++e) {
            const int i1 = edges[e].first, j1 = edges[e].second, i2 = edges[e + 1].first, j2 = edges[e + 1].second;
            const int ga = i2 - i1 - 1, gb = j2 - j1 - 1;
            if (ga <= 0 || ga != gb || j2 <= j1) continue;
            for (int k = 0; k < ga; ++k) set(*sa[(std::size_t)(i1 + 1 + k)], *sb[(std::size_t)(j1 + 1 + k)], false);
        }
        // what is left in the pair: a length only one packet has on each side pairs those two
        std::map<int, std::vector<const RefRow*>> la; std::map<int, std::vector<const Row*>> lb;
        for (const RefRow* a : sa) if (c.toLive[a->op] < 0) la[a->len].push_back(a);
        for (const Row* b : sb) if (c.toRef[b->op] < 0) lb[b->len].push_back(b);
        for (const auto& kv : la) {
            auto it = lb.find(kv.first);
            if (kv.second.size() != 1 || it == lb.end() || it->second.size() != 1) continue;
            const int before = c.byOrder;
            set(*kv.second[0], *it->second[0], false);
            if (c.byOrder != before) { --c.byOrder; ++c.byLength; }
        }
    }
    return c;
}

// The running exe's carry, one per binary: the reader sets it at attach, the companion at boot.
// Without one (the chain not recognised), opcodes pass through as they are, the compiled numbering.
// Zone subs carry like packets: by twin (the live twin's reference id is the reference sub's twin;
// two subs on one twin go by length), then by own callback name, then by a length only one leftover
// has on each side. A carried sub keeps its length or is dropped. Then the subs added after the
// reference build (kZoneAdded): by the callback bound, else by the routine the handler ends in.
struct ZoneCarry {
    bool ok = false; std::string why;
    int liveCount = 0, refCount = 0, byTwin = 0, byName = 0, byLength = 0, lenDiffer = 0, byAdded = 0;
    int toRef[64]; int toLive[64]; int liveLen[64]; int twinRef[64];   // twinRef: per reference sub
    std::string newSubs;   // live subs with no reference counterpart
    std::string addedSubs; // the added subs found, "ref (live)"
};
inline void CarryZone(const ZoneRef* ref, std::size_t n, const std::vector<ZoneRow>& live, int (*refOf)(int), ZoneCarry& z) {
    for (int i = 0; i < 64; ++i) z.toRef[i] = z.toLive[i] = z.liveLen[i] = z.twinRef[i] = -1;
    z.refCount = (int)n; z.liveCount = (int)live.size();
    for (std::size_t i = 0; i < n; ++i) if (ref[i].sub >= 0 && ref[i].sub < 64) z.twinRef[ref[i].sub] = ref[i].twin;
    for (const auto& l : live) if (l.sub >= 0 && l.sub < 64) z.liveLen[l.sub] = l.len;
    auto set = [&](const ZoneRef& a, const ZoneRow& b) {
        if (a.sub < 0 || a.sub >= 64 || b.sub < 0 || b.sub >= 64 || z.toLive[a.sub] >= 0 || z.toRef[b.sub] >= 0) return false;
        if (a.len != b.len) { ++z.lenDiffer; return false; }
        z.toLive[a.sub] = b.sub; z.toRef[b.sub] = a.sub; return true;
    };
    for (std::size_t i = 0; i < n; ++i) {   // by twin
        if (ref[i].twin < 0) continue;
        std::vector<const ZoneRow*> c;
        for (const auto& l : live) if (l.twin >= 0 && refOf(l.twin) == ref[i].twin && l.sub >= 0 && l.sub < 64 && z.toRef[l.sub] < 0) c.push_back(&l);
        if (c.size() > 1) { std::vector<const ZoneRow*> c2; for (auto* p : c) if (p->len == ref[i].len) c2.push_back(p); c.swap(c2); }
        if (c.size() == 1 && set(ref[i], *c[0])) ++z.byTwin;
    }
    for (std::size_t i = 0; i < n; ++i) {   // by own callback name
        if (!ref[i].own || !*ref[i].own || ref[i].sub < 0 || ref[i].sub >= 64 || z.toLive[ref[i].sub] >= 0) continue;
        for (const auto& l : live) if (l.own == ref[i].own) { if (set(ref[i], l)) ++z.byName; break; }
    }
    std::map<int, std::vector<const ZoneRef*>> la; std::map<int, std::vector<const ZoneRow*>> lb;   // by a fixed length only one leftover has
    for (std::size_t i = 0; i < n; ++i) if (ref[i].sub >= 0 && ref[i].sub < 64 && z.toLive[ref[i].sub] < 0) la[ref[i].len].push_back(&ref[i]);
    for (const auto& l : live) if (l.sub >= 0 && l.sub < 64 && z.toRef[l.sub] < 0) lb[l.len].push_back(&l);
    for (const auto& kv : la) {
        if (kv.first < 0) continue;   // a variable length says nothing about the body
        auto it = lb.find(kv.first);
        if (kv.second.size() == 1 && it != lb.end() && it->second.size() == 1 && set(*kv.second[0], *it->second[0])) ++z.byLength;
    }
    for (std::size_t i = 0; i < kZoneAddedCount; ++i) {   // added after the reference build
        const ZoneAdded& a = kZoneAdded[i];
        if (a.sub < 0 || a.sub >= 64 || z.toLive[a.sub] >= 0) continue;
        const ZoneRef as{ a.sub, a.len, -1, a.name };
        const ZoneRow* hit = nullptr;
        for (const auto& l : live) if (l.sub >= 0 && l.sub < 64 && z.toRef[l.sub] < 0 && l.name == a.name) { hit = &l; break; }
        if (!hit && a.variantOf >= 0 && a.variantOf < 64 && z.toLive[a.variantOf] >= 0) {
            std::uint32_t callee = 0;
            for (const auto& l : live) if (l.sub == z.toLive[a.variantOf]) callee = l.callee;
            int found = 0;
            for (const auto& l : live) if (callee && l.callee == callee && l.sub >= 0 && l.sub < 64 && z.toRef[l.sub] < 0 && l.len == a.len) { hit = &l; ++found; }
            if (found != 1) hit = nullptr;
        }
        if (hit && set(as, *hit)) {
            ++z.byAdded;
            z.addedSubs += (z.addedSubs.empty() ? "" : ", ") + std::to_string(a.sub) + " (live " + std::to_string(hit->sub) + ")";
        }
    }
    for (const auto& l : live) if (l.sub >= 0 && l.sub < 64 && z.toRef[l.sub] < 0) z.newSubs += (z.newSubs.empty() ? "" : ", ") + std::to_string(l.sub);
    z.ok = z.liveCount > 0;
    if (!z.ok) z.why = "no zone sub-packet descriptors recognised";
}

struct Live { bool ok = false; Carried c{}; int liveCount = 0; std::string why; std::uint32_t invoke[kHead + 0x100] = {}; ZoneCarry zone{}; };
inline Live& Current() { static Live l; return l; }
inline int RefOf(int liveOp);
inline void SetCurrent(const std::uint8_t* file, std::size_t size, const RefRow* ref, std::size_t refN, const ZoneRef* zref = nullptr, std::size_t zrefN = 0) {
    Live L;
    std::vector<Row> t; std::vector<ZoneRow> zt;
    if (Table(file, size, t, &L.why, &zt)) {
        L.c = Carry(ref, refN, t);
        L.liveCount = (int)t.size();
        for (const auto& r : t) if (r.op >= 0 && r.op < 512) L.invoke[r.op] = r.invoke;
        for (const auto& r : t) if (r.op >= 0 && r.op < 0x100) L.invoke[kHead + r.op] = r.invoke;
        L.ok = L.c.byName > 0;
        if (!L.ok) L.why = "no packet of the reference build found by name";
    }
    Current() = L;   // the zone carry asks RefOf of this table
    if (L.ok && zref && zrefN) {
        CarryZone(zref, zrefN, zt, RefOf, Current().zone);
        for (const auto& z : zt) if (z.invoke && z.sub >= 0 && z.sub < 64) Current().invoke[kZoneOwn + z.sub] = z.invoke;
    }
}
// The live opcode of a reference packet; -1 when it did not carry. kZoneOwn + sub: the live zone
// sub's pseudo id, the same way.
inline int LiveOf(int refOp) {
    const Live& L = Current();
    if (refOp >= kHead && refOp < kHead + 0x100) {
        const int live = LiveOf(refOp - kHead);
        return live >= 0 && live < 0x100 ? kHead + live : -1;
    }
    if (refOp >= kZoneOwn && refOp < kZoneOwn + 64) {
        if (!L.zone.ok) return refOp;
        const int s = L.zone.toLive[refOp - kZoneOwn];
        return s >= 0 ? kZoneOwn + s : -1;
    }
    if (!L.ok) return refOp;
    return refOp >= 0 && refOp < 512 ? L.c.toLive[refOp] : -1;
}
// The reference id of a live zone sub, -1 when the reference build has none (the live id itself
// when the zone carry did not run). And a reference sub's twin packet (reference id), -1 none.
inline int ZoneRefOf(int liveSub) {
    const Live& L = Current();
    if (!L.zone.ok) return liveSub;
    return liveSub >= 0 && liveSub < 64 ? L.zone.toRef[liveSub] : -1;
}
inline int ZoneTwinRef(int refSub) {
    const Live& L = Current();
    return L.zone.ok && refSub >= 0 && refSub < 64 ? L.zone.twinRef[refSub] : -1;
}
// The reference id of a live opcode: a packet the reference build has no counterpart for gets
// 0x100 + its live opcode, outside every reference id.
inline int RefOf(int liveOp) {
    const Live& L = Current();
    if (!L.ok || liveOp < 0 || liveOp >= 512) return liveOp;
    const int r = L.c.toRef[liveOp];
    return r >= 0 ? r : 0x100 + liveOp;
}

// PacketRefTable.h for a reference build: every packet with its name, registrar rank and place.
inline std::string RefTableText(const std::vector<Row>& t, const std::string& from, const std::vector<ZoneRow>* zone = nullptr) {
    std::vector<std::string> names;
    const std::vector<RefRow> g = Groups(t, names);
    std::string s = "#pragma once\n// Generated by RuneToolsXLauncher --pkt-ref from " + from + ". Do not edit.\n"
                    "// The reference build's server packets (PacketIds.h): opcode, wire length, callback name,\n"
                    "// registrar rank and place inside it. ServerOps.h numbers packets the way this build does.\n"
                    "#include \"PacketIds.h\"\n\nnamespace rtx::pktid {\n\ninline constexpr RefRow kRef[] = {\n";
    char line[256];
    for (const auto& r : g) {
        std::snprintf(line, sizeof(line), "    { 0x%02X, %3d, \"%s\", %d, %d },\n", r.op, r.len, r.name, r.group, r.pos);
        s += line;
    }
    s += "};\ninline constexpr std::size_t kRefCount = sizeof(kRef) / sizeof(kRef[0]);\n\n";
    s += "// The reference build's zone sub-packets: sub id, wire length, twin packet (-1 none), own callback name.\n"
         "inline constexpr ZoneRef kZoneRef[] = {\n";
    if (zone && !zone->empty())
        for (const auto& z : *zone) {
            std::snprintf(line, sizeof(line), "    { %2d, %3d, %4d, \"%s\" },\n", z.sub, z.len, z.twin, z.own.c_str());
            s += line;
        }
    else s += "    { -1, 0, -1, \"\" },\n";
    s += "};\ninline constexpr std::size_t kZoneRefCount = ";
    s += zone && !zone->empty() ? "sizeof(kZoneRef) / sizeof(kZoneRef[0])" : "0";
    s += ";\n\n}  // namespace rtx::pktid\n";
    return s;
}

}  // namespace rtx::pktid
