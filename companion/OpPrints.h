#pragma once
// Engine operations named from their own code, so no export of the game's scripts is needed.
//
// An operation's number changes between game builds, its handler does not: the same source compiles
// to the same instructions, with only the addresses and the larger structure offsets moving. A
// handler's print is a hash of its instructions with those fields blanked: rip-relative
// displacements, branch and call targets, and displacements of 0x1000 and up (the root's fields
// move with every update, a record's own fields do not). Text the handler names goes into the hash
// as text, and a routine it calls or hands on goes in as that routine's own print.
//
// Prints are taken at three depths (2: the handler, its callees and theirs; 1; 0: the handler alone).
// Handlers that still print alike differ only in which field they read (the mouse's x and its y).
// Those get a fourth print: the shared one with the handler's place among its look-alikes, ordered
// by the blanked field offsets. The order of a structure's fields outlives the offsets themselves.
// The table (OpPrintsTable.h) holds each named operation's prints from the builds it was made
// from, and whether each was the only handler with that print there. A name is given to a handler
// when, at the deepest depth with any hit, exactly one handler carries one of its prints; a print
// that was shared in its own build is never used to name. An update that changes a handler leaves
// its name unresolved: the feature behind it is off and reported, nothing is guessed.
//
// Works on the exe as a file and as the image mapped in the running game.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtx::opprints {

// ---- image ----
struct Sec { std::uint32_t rva = 0, size = 0; const std::uint8_t* at = nullptr; };
struct Image {
    std::vector<Sec> secs;
    std::uint32_t textLo = 0, textHi = 0, rdataLo = 0, rdataHi = 0;
    std::unordered_map<std::uint32_t, std::uint32_t> ends;   // function start -> end, where the exe carries unwind data
    // The bytes at `rva`, and how many follow inside its section; null outside every section.
    const std::uint8_t* At(std::uint32_t rva, std::uint32_t* avail = nullptr) const {
        for (const Sec& s : secs)
            if (rva >= s.rva && rva < s.rva + s.size) { if (avail) *avail = s.rva + s.size - rva; return s.at + (rva - s.rva); }
        return nullptr;
    }
    bool InText(std::int64_t rva) const { return rva >= textLo && rva < textHi; }
};

// `mapped`: `base` is the loaded image (sections at their addresses), else the file's bytes.
inline bool Open(const std::uint8_t* base, std::size_t size, bool mapped, Image& im) {
    if (!base || size < 0x200 || base[0] != 'M' || base[1] != 'Z') return false;
    std::uint32_t lfanew; std::memcpy(&lfanew, base + 0x3C, 4);
    if ((std::uint64_t)lfanew + 0x108 > size || std::memcmp(base + lfanew, "PE\0\0", 4) != 0) return false;
    const std::uint8_t* nt = base + lfanew;
    std::uint16_t nsec, optSize, magic;
    std::memcpy(&nsec, nt + 6, 2); std::memcpy(&optSize, nt + 20, 2); std::memcpy(&magic, nt + 24, 2);
    if (magic != 0x20B) return false;
    std::uint32_t pdataRva, pdataSize;
    std::memcpy(&pdataRva, nt + 24 + 112 + 24, 4); std::memcpy(&pdataSize, nt + 24 + 112 + 28, 4);
    const std::uint8_t* sec = nt + 24 + optSize;
    if ((std::uint64_t)(sec - base) + (std::uint64_t)nsec * 40 > size) return false;
    for (unsigned i = 0; i < nsec; ++i, sec += 40) {
        std::uint32_t vsize, rva, rawSize, raw;
        std::memcpy(&vsize, sec + 8, 4); std::memcpy(&rva, sec + 12, 4); std::memcpy(&rawSize, sec + 16, 4); std::memcpy(&raw, sec + 20, 4);
        Sec s; s.rva = rva;
        if (mapped) { s.size = vsize; s.at = base + rva; if ((std::uint64_t)rva + vsize > size) continue; }
        else {
            if (raw >= size) continue;
            s.size = vsize && vsize < rawSize ? vsize : rawSize;
            if ((std::uint64_t)raw + s.size > size) s.size = (std::uint32_t)(size - raw);
            s.at = base + raw;
        }
        if (!s.size) continue;
        im.secs.push_back(s);
        if (std::memcmp(sec, ".text\0\0\0", 8) == 0 && !im.textHi) { im.textLo = s.rva; im.textHi = s.rva + s.size; }
        if (std::memcmp(sec, ".rdata\0\0", 8) == 0 && !im.rdataHi) { im.rdataLo = s.rva; im.rdataHi = s.rva + s.size; }
    }
    if (!im.textHi) return false;
    std::uint32_t avail = 0;
    if (const std::uint8_t* p = pdataSize ? im.At(pdataRva, &avail) : nullptr) {
        const std::uint32_t n = (pdataSize < avail ? pdataSize : avail) / 12;
        im.ends.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint32_t b, e; std::memcpy(&b, p + i * 12, 4); std::memcpy(&e, p + i * 12 + 4, 4);
            if (b && e > b) im.ends.emplace(b, e);
        }
    }
    return true;
}

// ---- instruction lengths and fields ----
enum : std::uint8_t { kOther, kCall, kJmp, kJcc, kRet, kJmpOther };
struct Insn { std::uint8_t len = 0, dispAt = 0, dispSize = 0, immAt = 0, immSize = 0, kind = kOther; bool rip = false; };

// One 64-bit instruction at `p` (at most `n` bytes there). False for an encoding it does not know,
// which ends a print where it stands.
inline bool Decode(const std::uint8_t* p, std::size_t n, Insn& o) {
    o = Insn{};
    std::size_t i = 0; bool op66 = false, a67 = false, rexW = false;
    for (;; ++i) {
        if (i >= n || i >= 14) return false;
        const std::uint8_t b = p[i];
        if (b == 0x66) op66 = true;
        else if (b == 0x67) a67 = true;
        else if (b != 0xF0 && b != 0xF2 && b != 0xF3 && b != 0x2E && b != 0x36 && b != 0x3E && b != 0x26 && b != 0x64 && b != 0x65) break;
    }
    if ((p[i] & 0xF0) == 0x40) { rexW = (p[i] & 8) != 0; if (++i >= n) return false; }
    int map = 0;
    std::uint8_t op = p[i++];
    auto next = [&](std::uint8_t& out) { if (i >= n) return false; out = p[i++]; return true; };
    if (op == 0xC5) { std::uint8_t x; if (!next(x) || !next(op)) return false; map = 1; }
    else if (op == 0xC4) { std::uint8_t x, y; if (!next(x) || !next(y) || !next(op)) return false; map = x & 0x1F; if (map < 1 || map > 3) return false; }
    else if (op == 0x0F) {
        if (!next(op)) return false;
        map = 1;
        if (op == 0x38) { map = 2; if (!next(op)) return false; }
        else if (op == 0x3A) { map = 3; if (!next(op)) return false; }
    }
    bool modrm = false; int imm = 0;
    const int z = op66 ? 2 : 4;
    if (map == 0) {
        if (op < 0x40) { const int k = op & 7; if (k < 4) modrm = true; else if (k == 4) imm = 1; else if (k == 5) imm = z; }
        else if (op < 0x50) return false;
        else if (op < 0x60) {}
        else if (op < 0x70) {
            if (op < 0x63) return false;
            if (op == 0x63) modrm = true;
            else if (op == 0x68) imm = z;
            else if (op == 0x69) { modrm = true; imm = z; }
            else if (op == 0x6A) imm = 1;
            else if (op == 0x6B) { modrm = true; imm = 1; }
            else if (op < 0x68) return false;
        }
        else if (op < 0x80) imm = 1;
        else if (op < 0x90) { if (op == 0x82) return false; modrm = true; if (op == 0x80 || op == 0x83) imm = 1; else if (op == 0x81) imm = z; }
        else if (op < 0xA0) { if (op == 0x9A) return false; }
        else if (op < 0xB0) { if (op <= 0xA3) imm = a67 ? 4 : 8; else if (op == 0xA8) imm = 1; else if (op == 0xA9) imm = z; }
        else if (op < 0xB8) imm = 1;
        else if (op < 0xC0) imm = rexW ? 8 : z;
        else switch (op) {
            case 0xC0: case 0xC1: case 0xC6: modrm = true; imm = 1; break;
            case 0xC7: modrm = true; imm = z; break;
            case 0xC2: case 0xCA: imm = 2; break;
            case 0xC8: imm = 3; break;
            case 0xCD: imm = 1; break;
            case 0xC3: case 0xC9: case 0xCB: case 0xCC: case 0xCE: case 0xCF: case 0xD7: break;
            case 0xD0: case 0xD1: case 0xD2: case 0xD3: modrm = true; break;
            case 0xD4: case 0xD5: case 0xD6: case 0xEA: return false;
            case 0xE8: case 0xE9: imm = 4; break;
            case 0xEB: imm = 1; break;
            case 0xF6: case 0xF7: if (i >= n) return false; modrm = true; if (((p[i] >> 3) & 7) < 2) imm = op == 0xF6 ? 1 : z; break;
            case 0xFE: case 0xFF: modrm = true; break;
            default:
                if (op >= 0xD8 && op <= 0xDF) modrm = true;
                else if (op >= 0xE0 && op <= 0xE7) imm = 1;
                break;   // EC..EF, F1, F4, F5, F8..FD: no operand bytes
        }
        if (op == 0xE8) o.kind = kCall; else if (op == 0xE9) o.kind = kJmp; else if (op == 0xEB) o.kind = kJmpOther;
        else if (op == 0xC3 || op == 0xC2) o.kind = kRet;
        else if (op == 0xFF && i < n && (((p[i] >> 3) & 7) == 4)) o.kind = kJmpOther;
    } else if (map == 1) {
        modrm = !(op == 0x05 || op == 0x06 || op == 0x07 || op == 0x08 || op == 0x09 || op == 0x0B || op == 0x0E || (op >= 0x30 && op <= 0x37) ||
                  op == 0x77 || op == 0xA0 || op == 0xA1 || op == 0xA2 || op == 0xA8 || op == 0xA9 || op == 0xAA || (op >= 0xC8 && op <= 0xCF));
        if (op >= 0x80 && op <= 0x8F) { modrm = false; imm = 4; o.kind = kJcc; }
        else if (op == 0x70 || op == 0x71 || op == 0x72 || op == 0x73 || op == 0xA4 || op == 0xAC || op == 0xBA || op == 0xC2 || op == 0xC4 || op == 0xC5 || op == 0xC6) imm = 1;
    } else { modrm = true; if (map == 3) imm = 1; }
    if (modrm) {
        if (i >= n) return false;
        const std::uint8_t m = p[i++], mod = m >> 6, rm = m & 7;
        if (mod != 3) {
            std::uint8_t base = rm;
            const bool sib = rm == 4;
            if (sib) { if (i >= n) return false; base = p[i++] & 7; }
            if (mod == 0 && !sib && rm == 5) { o.dispSize = 4; o.rip = true; }
            else if (mod == 0 && sib && base == 5) o.dispSize = 4;
            else if (mod == 1) o.dispSize = 1;
            else if (mod == 2) o.dispSize = 4;
            o.dispAt = (std::uint8_t)i; i += o.dispSize;
        }
    }
    if (imm) { o.immAt = (std::uint8_t)i; o.immSize = (std::uint8_t)imm; i += imm; }
    if (i > n || i > 15) return false;
    o.len = (std::uint8_t)i;
    return true;
}

// ---- prints ----
constexpr std::uint32_t kHandlerSpan = 0x200, kCalleeSpan = 0x100;
constexpr int kDepths = 4;   // 0, 1, 2, and 3: the place among handlers alike at depth 2
constexpr int kDeepest = 2;  // the deepest print of the code alone

struct Scan {
    const Image* im = nullptr;
    std::vector<std::uint32_t> starts;   // sorted: every known function start (unwind data, the handlers)
    std::unordered_map<std::uint64_t, std::uint64_t> memo;
    bool IsStart(std::uint32_t rva) const { return std::binary_search(starts.begin(), starts.end(), rva); }
};
inline void Feed(std::uint64_t& h, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001B3ull; }
}
// Printable text of 4 to 199 characters at `rva` in .rdata; 0 when there is none.
inline std::size_t TextAt(const Image& im, std::int64_t rva, const std::uint8_t*& out) {
    if (rva < im.rdataLo || rva >= im.rdataHi) return 0;
    std::uint32_t avail = 0;
    out = im.At((std::uint32_t)rva, &avail);
    if (!out) return 0;
    std::size_t n = 0;
    while (n < avail && n < 200 && out[n]) { if (out[n] < 32 || out[n] > 126) return 0; ++n; }
    return n >= 4 && n < 200 && n < avail ? n : 0;
}
inline std::uint64_t Print(Scan& s, std::uint32_t rva, std::uint32_t cap, int depth) {
    const std::uint64_t key = (std::uint64_t)rva | ((std::uint64_t)cap << 32) | ((std::uint64_t)depth << 48);
    if (auto it = s.memo.find(key); it != s.memo.end()) return it->second;
    s.memo[key] = 0x52;                                    // a routine reached again from itself
    const Image& im = *s.im;
    std::uint64_t h = 0xCBF29CE484222325ull;
    std::uint32_t avail = 0;
    const std::uint8_t* p = im.At(rva, &avail);
    const auto e = im.ends.find(rva);
    const bool known = e != im.ends.end();
    const std::uint32_t len = known && e->second - rva < cap ? e->second - rva : cap;
    std::uint32_t at = 0; bool flowEnd = false;
    while (p && at < len) {
        // a routine without unwind data ends at the next routine, or at the padding after its last return
        if (!known && at > 0 && (s.IsStart(rva + at) || (flowEnd && at < avail && p[at] == 0xCC))) break;
        Insn in;
        if (at >= avail || !Decode(p + at, avail - at < 15 ? avail - at : 15, in)) { Feed(h, "?", 1); break; }
        std::uint8_t b[15]; std::memcpy(b, p + at, in.len);
        std::int64_t child = -1;
        if (in.dispSize == 4) {
            std::int32_t d; std::memcpy(&d, b + in.dispAt, 4);
            if (in.rip || (std::uint32_t)d >= 0x1000) std::memset(b + in.dispAt, 0, 4);
            if (in.rip) {
                const std::int64_t t = (std::int64_t)rva + at + in.len + d;
                const std::uint8_t* text = nullptr;
                if (const std::size_t n = TextAt(im, t, text)) { Feed(h, "S", 1); Feed(h, text, n); }
                else if (depth > 0 && im.InText(t)) child = t;
            }
        }
        if (in.immSize == 4 && (in.kind == kCall || in.kind == kJmp || in.kind == kJcc)) {
            std::int32_t rel; std::memcpy(&rel, b + in.immAt, 4);
            std::memset(b + in.immAt, 0, 4);
            const std::int64_t t = (std::int64_t)rva + at + in.len + rel;
            if (depth > 0 && im.InText(t) && (in.kind == kCall || (in.kind == kJmp && !(t >= rva && t < (std::int64_t)rva + len)))) child = t;
        }
        Feed(h, b, in.len);
        if (child >= 0) { const std::uint64_t c = Print(s, (std::uint32_t)child, kCalleeSpan, depth - 1); Feed(h, "C", 1); Feed(h, &c, 8); }
        at += in.len;
        flowEnd = in.kind == kRet || in.kind == kJmp || in.kind == kJmpOther;
    }
    s.memo[key] = h;
    return h;
}

// The field offsets Print blanks in a routine's own body, in the order it reads them.
inline std::vector<std::uint32_t> Fields(const Scan& s, std::uint32_t rva, std::uint32_t cap) {
    std::vector<std::uint32_t> out;
    const Image& im = *s.im;
    std::uint32_t avail = 0;
    const std::uint8_t* p = im.At(rva, &avail);
    const auto e = im.ends.find(rva);
    const bool known = e != im.ends.end();
    const std::uint32_t len = known && e->second - rva < cap ? e->second - rva : cap;
    std::uint32_t at = 0; bool flowEnd = false;
    while (p && at < len) {
        if (!known && at > 0 && (s.IsStart(rva + at) || (flowEnd && at < avail && p[at] == 0xCC))) break;
        Insn in;
        if (at >= avail || !Decode(p + at, avail - at < 15 ? avail - at : 15, in)) break;
        if (in.dispSize == 4 && !in.rip) {
            std::uint32_t d; std::memcpy(&d, p + at + in.dispAt, 4);
            if (d >= 0x1000) out.push_back(d);
        }
        at += in.len;
        flowEnd = in.kind == kRet || in.kind == kJmp || in.kind == kJmpOther;
    }
    return out;
}

// Every handler's print at every depth: print -> the operations carrying it.
struct Prints {
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> at[kDepths];
    std::map<std::uint32_t, std::uint64_t> of[kDepths];   // operation -> its print
};
inline void Take(const Image& im, const std::map<std::uint32_t, std::uint32_t>& handlers /* number -> rva */, Prints& out) {
    Scan s; s.im = &im;
    s.starts.reserve(im.ends.size() + handlers.size());
    for (const auto& kv : im.ends) s.starts.push_back(kv.first);
    for (const auto& kv : handlers) s.starts.push_back(kv.second);
    std::sort(s.starts.begin(), s.starts.end());
    s.starts.erase(std::unique(s.starts.begin(), s.starts.end()), s.starts.end());
    for (int d = 0; d <= kDeepest; ++d)
        for (const auto& kv : handlers) {
            const std::uint64_t p = Print(s, kv.second, kHandlerSpan, d);
            out.at[d][p].push_back(kv.first);
            out.of[d][kv.first] = p;
        }
    // look-alikes, each by its place in the order of the fields they read; a group in which two read
    // the same fields has no order and gets no such print
    for (const auto& g : out.at[kDeepest]) {
        if (g.second.size() < 2) continue;
        std::vector<std::pair<std::vector<std::uint32_t>, std::uint32_t>> order;
        for (const std::uint32_t op : g.second) order.emplace_back(Fields(s, handlers.at(op), kHandlerSpan), op);
        std::sort(order.begin(), order.end());
        bool distinct = true;
        for (std::size_t i = 1; i < order.size(); ++i) if (order[i].first == order[i - 1].first) distinct = false;
        if (!distinct) continue;
        for (std::size_t i = 0; i < order.size(); ++i) {
            std::uint64_t h = g.first;
            const std::uint32_t place[2] = { (std::uint32_t)order.size(), (std::uint32_t)i };
            Feed(h, place, sizeof(place));
            out.at[kDepths - 1][h].push_back(order[i].second);
            out.of[kDepths - 1][order[i].second] = h;
        }
    }
}

// ---- the table and the names it gives ----
struct Ref { const char* name; std::uint8_t depth, unique; std::uint64_t print; };   // rows of one name together

struct Named {
    std::map<std::string, std::uint32_t> byName;                 // one handler, and no other name claims it
    std::map<std::string, std::vector<std::uint32_t>> shared;    // handlers alike in code: any of them, not which
    int names = 0, ambiguous = 0, clashes = 0;                   // names in the table; with several candidates; given to a handler another name took
};
inline void Resolve(const Prints& pr, const Ref* refs, std::size_t n, Named& out) {
    std::map<std::uint32_t, std::vector<std::string>> claimed;
    for (std::size_t i = 0; i < n; ) {
        std::size_t j = i;
        while (j < n && std::strcmp(refs[j].name, refs[i].name) == 0) ++j;
        ++out.names;
        bool done = false;
        for (int d = kDepths - 1; d >= 0 && !done; --d) {
            std::set<std::uint32_t> cands;
            for (std::size_t k = i; k < j; ++k) {
                if (refs[k].depth != d || !refs[k].unique) continue;
                if (auto it = pr.at[d].find(refs[k].print); it != pr.at[d].end()) cands.insert(it->second.begin(), it->second.end());
            }
            if (cands.size() == 1) { out.byName[refs[i].name] = *cands.begin(); claimed[*cands.begin()].push_back(refs[i].name); done = true; }
            else if (cands.size() > 1) { ++out.ambiguous; done = true; }
        }
        if (out.byName.find(refs[i].name) == out.byName.end()) {
            // alike in code with others where the table was made: every handler of that code
            std::set<std::uint32_t> all;
            for (std::size_t k = i; k < j; ++k) {
                if (refs[k].depth != kDeepest || refs[k].unique) continue;
                if (auto it = pr.at[kDeepest].find(refs[k].print); it != pr.at[kDeepest].end()) all.insert(it->second.begin(), it->second.end());
            }
            if (!all.empty()) out.shared[refs[i].name].assign(all.begin(), all.end());
        }
        i = j;
    }
    for (const auto& kv : claimed) {
        if (kv.second.size() < 2) continue;
        for (const auto& nm : kv.second) { out.byName.erase(nm); ++out.clashes; }
    }
}

// The table's rows for one build whose names are known (name -> number), added to `rows`:
// "name\tdepth\tunique\tprint". Rows of several builds merge by being the same text.
inline void RowsOf(const Prints& pr, const std::map<std::string, std::uint32_t>& names, std::set<std::string>& rows) {
    for (const auto& kv : names) {
        if (kv.first.empty() || kv.first.find_first_of("\"\\\t") != std::string::npos) continue;
        for (int d = 0; d < kDepths; ++d) {
            const auto p = pr.of[d].find(kv.second);
            if (p == pr.of[d].end()) continue;
            const auto who = pr.at[d].find(p->second);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "\t%d\t%d\t%016llx", d, who != pr.at[d].end() && who->second.size() == 1 ? 1 : 0, (unsigned long long)p->second);
            rows.insert(kv.first + buf);
        }
    }
}
// The table as a header, from merged rows.
inline std::string TableText(const std::set<std::string>& rows, const std::string& from) {
    std::string o = "#pragma once\n// Generated by the launcher (--op-prints), not written by hand. Made from: " + from + "\n#include \"OpPrints.h\"\n\nnamespace rtx::opprints {\n\ninline constexpr Ref kRefs[] = {\n";
    for (const std::string& r : rows) {
        const std::size_t a = r.find('\t'), b = r.find('\t', a + 1), c = r.find('\t', b + 1);
        o += "    { \"" + r.substr(0, a) + "\", " + r.substr(a + 1, b - a - 1) + ", " + r.substr(b + 1, c - b - 1) + ", 0x" + r.substr(c + 1) + "ull },\n";
    }
    o += "};\ninline constexpr std::size_t kRefCount = sizeof(kRefs) / sizeof(kRefs[0]);\n\n}  // namespace rtx::opprints\n";
    return o;
}

}  // namespace rtx::opprints
