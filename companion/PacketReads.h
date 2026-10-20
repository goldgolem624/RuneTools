#pragma once
// Server packet field layouts read from the handlers' code.
//
// The server reorders and re-encodes the fields of its fixed-length packets with every build (byte
// order, which byte is negated or offset by 0x80, which field comes first); the handler that reads a
// packet says how. A handler gets the buffer object in rdx (+0x10 data, +0x18 position). A symbolic
// run of its straight-line code (forward jumps followed, calls that receive the buffer followed two
// deep) makes every payload byte read a part (offset, transforms, shift) of a value; the values the
// reads build are the fields, each with the places it goes: stored at a displacement, passed in an
// argument register, used as an index, compared, masked, shifted. The places survive an update while
// the wire order does not, so the fields of two builds pair by width and by where they go (Pair).
// A handler that branches on a payload byte (a kind, a form tag, a version) reads the bytes after
// it in one way per branch: a form. The byte is followed at register width (sign, mask, shift,
// offset) so each branch is evaluated for all 256 values of the byte; a form is the set of values
// of every such byte that leads to it, and it has a program of its own. A packet's forms pair with
// the reference build's forms whose byte values overlap; a payload picks its form by its bytes.
// Decoders read fields by their index in the reference build's program (PacketReadsRef.h), each field
// evaluated with the byte transforms of the running build.
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "OpPrints.h"
#include "PacketIds.h"

namespace rtx::preads {

// ---- instructions, with their operands ----
enum Mn : std::uint8_t {
    MOther, MMov, MMovzx, MMovsx, MMovbe, MLea, MAdd, MOr, MAdc, MSbb, MAnd, MSub, MXor, MCmp, MTest,
    MShl, MShr, MSar, MRol, MRor, MNeg, MNot, MImul, MMulDiv, MBswap, MCall, MJmp, MJcc, MRet, MInt3,
    MPush, MPop, MNop, MWrite, MInc, MDec, MCdqe, MCqo, MXchg
};
enum : std::uint8_t { kNoOp, kReg, kMem, kImm };
constexpr int kRip = 16;
struct Opnd {
    std::uint8_t kind = kNoOp, size = 0;
    std::int8_t reg = -1;            // kReg: 0..15
    bool hi8 = false;                // ah ch dh bh
    std::int8_t base = -1, index = -1; std::uint8_t scale = 1; std::int32_t disp = 0;   // kMem; base kRip for rip-relative
    std::int64_t imm = 0;            // kImm
};
struct Ins { std::uint8_t len = 0; Mn mn = MOther; Opnd a, b; std::uint32_t target = 0; std::int8_t w1 = -1, w2 = -1; bool has3 = false; std::int64_t imm3 = 0; std::int8_t cc = -1; };

inline bool DecodeIns(const std::uint8_t* p, std::size_t n, std::uint32_t rva, Ins& o) {
    o = Ins{};
    std::size_t i = 0; bool op66 = false, rex = false, rW = false, rR = false, rX = false, rB = false;
    for (;; ++i) {
        if (i >= n || i >= 14) return false;
        const std::uint8_t c = p[i];
        if (c == 0x66) op66 = true;
        else if (c != 0x67 && c != 0xF0 && c != 0xF2 && c != 0xF3 && c != 0x2E && c != 0x36 && c != 0x3E && c != 0x26 && c != 0x64 && c != 0x65) break;
    }
    bool f2f3 = false;
    for (std::size_t k = 0; k < i; ++k) if (p[k] == 0xF2 || p[k] == 0xF3) f2f3 = true;
    if ((p[i] & 0xF0) == 0x40) { rex = true; rW = p[i] & 8; rR = p[i] & 4; rX = p[i] & 2; rB = p[i] & 1; if (++i >= n) return false; }
    const std::size_t opAt = i;
    std::uint8_t op = p[i++];
    int map = 0;
    if (op == 0x0F) { if (i >= n) return false; op = p[i++]; map = 1; if (op == 0x38 || op == 0x3A) { map = op == 0x38 ? 2 : 3; if (i >= n) return false; op = p[i++]; } }
    const std::uint8_t osz = rW ? 8 : op66 ? 2 : 4;
    int modReg = -1;
    auto regOp = [&](int r, std::uint8_t sz) {
        Opnd x; x.kind = kReg; x.size = sz; x.reg = (std::int8_t)r;
        if (sz == 1 && !rex && r >= 4 && r < 8) { x.hi8 = true; x.reg = (std::int8_t)(r - 4); }
        return x;
    };
    auto modrm = [&](std::uint8_t sz, Opnd& rm) -> bool {
        if (i >= n) return false;
        const std::uint8_t m = p[i++], mod = m >> 6;
        modReg = ((m >> 3) & 7) | (rR ? 8 : 0);
        int r = m & 7;
        if (mod == 3) { rm = regOp(r | (rB ? 8 : 0), sz); return true; }
        rm = Opnd{}; rm.kind = kMem; rm.size = sz;
        if (r == 4) {
            if (i >= n) return false;
            const std::uint8_t s = p[i++];
            rm.scale = (std::uint8_t)(1 << (s >> 6));
            const int idx = ((s >> 3) & 7) | (rX ? 8 : 0);
            rm.index = idx == 4 ? -1 : (std::int8_t)idx;
            const int b = s & 7;
            if (b == 5 && mod == 0) { if (i + 4 > n) return false; std::memcpy(&rm.disp, p + i, 4); i += 4; rm.base = -1; return true; }
            rm.base = (std::int8_t)(b | (rB ? 8 : 0));
        } else if (r == 5 && mod == 0) {
            if (i + 4 > n) return false; std::memcpy(&rm.disp, p + i, 4); i += 4; rm.base = kRip; return true;
        } else rm.base = (std::int8_t)(r | (rB ? 8 : 0));
        if (mod == 1) { if (i >= n) return false; rm.disp = (std::int8_t)p[i++]; }
        else if (mod == 2) { if (i + 4 > n) return false; std::memcpy(&rm.disp, p + i, 4); i += 4; }
        return true;
    };
    auto immOp = [&](int bytes, std::uint8_t sz) -> bool {
        if (i + (std::size_t)bytes > n) return false;
        std::int64_t v = 0;
        if (bytes == 1) v = (std::int8_t)p[i]; else if (bytes == 2) { std::int16_t t; std::memcpy(&t, p + i, 2); v = t; }
        else if (bytes == 4) { std::int32_t t; std::memcpy(&t, p + i, 4); v = t; } else { std::memcpy(&v, p + i, 8); }
        i += (std::size_t)bytes;
        o.b = Opnd{}; o.b.kind = kImm; o.b.size = sz; o.b.imm = v; return true;
    };
    const int z = op66 ? 2 : 4;
    static const Mn kAlu[8] = { MAdd, MOr, MAdc, MSbb, MAnd, MSub, MXor, MCmp };
    static const Mn kShift[8] = { MRol, MRor, MOther, MOther, MShl, MShr, MShl, MSar };
    bool known = true;
    if (map == 0) {
        if (op < 0x40 && (op & 7) < 6 && op != 0x26 && op != 0x2E && op != 0x36 && op != 0x3E) {
            o.mn = kAlu[op >> 3];
            const int f = op & 7;
            const std::uint8_t sz = (f & 1) ? osz : 1;
            if (f < 4) {
                Opnd rm; if (!modrm(sz, rm)) return false;
                Opnd g = regOp(modReg, sz);
                if (f < 2) { o.a = rm; o.b = g; } else { o.a = g; o.b = rm; }
            } else { o.a = regOp(0, sz); if (!immOp(f == 4 ? 1 : z, sz)) return false; }
        }
        else if (op >= 0x50 && op <= 0x57) { o.mn = MPush; }
        else if (op >= 0x58 && op <= 0x5F) { o.mn = MPop; o.a = regOp((op & 7) | (rB ? 8 : 0), 8); }
        else if (op == 0x63) { o.mn = MMovsx; Opnd rm; if (!modrm(4, rm)) return false; o.a = regOp(modReg, osz); o.b = rm; }
        else if (op == 0x68) { o.mn = MPush; if (!immOp(z, 8)) return false; }
        else if (op == 0x6A) { o.mn = MPush; if (!immOp(1, 8)) return false; }
        else if (op == 0x69 || op == 0x6B) { o.mn = MImul; Opnd rm; if (!modrm(osz, rm)) return false; o.a = regOp(modReg, osz); if (!immOp(op == 0x69 ? z : 1, osz)) return false; o.imm3 = o.b.imm; o.has3 = true; o.b = rm; }
        else if (op >= 0x70 && op <= 0x7F) { o.mn = MJcc; o.cc = (std::int8_t)(op & 0xF); if (i >= n) return false; o.target = (std::uint32_t)((std::int64_t)rva + (std::int64_t)i + 1 + (std::int8_t)p[i]); i += 1; }
        else if (op >= 0x80 && op <= 0x83 && op != 0x82) {
            const std::uint8_t sz = op == 0x80 ? 1 : osz;
            Opnd rm; if (!modrm(sz, rm)) return false;
            o.mn = kAlu[modReg & 7]; o.a = rm;
            if (!immOp(op == 0x81 ? z : 1, sz)) return false;
        }
        else if (op == 0x84 || op == 0x85) { o.mn = MTest; const std::uint8_t sz = op == 0x84 ? 1 : osz; Opnd rm; if (!modrm(sz, rm)) return false; o.a = rm; o.b = regOp(modReg, sz); }
        else if (op == 0x86 || op == 0x87) { o.mn = MXchg; const std::uint8_t sz = op == 0x86 ? 1 : osz; Opnd rm; if (!modrm(sz, rm)) return false; o.a = rm; o.b = regOp(modReg, sz); }
        else if (op >= 0x88 && op <= 0x8B) {
            o.mn = MMov; const std::uint8_t sz = (op & 1) ? osz : 1;
            Opnd rm; if (!modrm(sz, rm)) return false;
            Opnd g = regOp(modReg, sz);
            if (op < 0x8A) { o.a = rm; o.b = g; } else { o.a = g; o.b = rm; }
        }
        else if (op == 0x8D) { o.mn = MLea; Opnd rm; if (!modrm(osz, rm)) return false; o.a = regOp(modReg, osz); o.b = rm; }
        else if (op == 0x90 && !rB) { o.mn = MNop; }
        else if (op >= 0x90 && op <= 0x97) { o.mn = MXchg; o.a = regOp(0, osz); o.b = regOp((op & 7) | (rB ? 8 : 0), osz); }
        else if (op == 0x98) { o.mn = MCdqe; }
        else if (op == 0x99) { o.mn = MCqo; }
        else if (op == 0xA8 || op == 0xA9) { o.mn = MTest; o.a = regOp(0, op == 0xA8 ? 1 : osz); if (!immOp(op == 0xA8 ? 1 : z, o.a.size)) return false; }
        else if (op >= 0xB0 && op <= 0xB7) { o.mn = MMov; o.a = regOp((op & 7) | (rB ? 8 : 0), 1); if (!immOp(1, 1)) return false; }
        else if (op >= 0xB8 && op <= 0xBF) { o.mn = MMov; o.a = regOp((op & 7) | (rB ? 8 : 0), osz); if (!immOp(rW ? 8 : z, osz)) return false; }
        else if (op == 0xC0 || op == 0xC1 || (op >= 0xD0 && op <= 0xD3)) {
            const std::uint8_t sz = (op & 1) ? osz : 1;
            Opnd rm; if (!modrm(sz, rm)) return false;
            o.mn = kShift[modReg & 7]; o.a = rm;
            if (op == 0xC0 || op == 0xC1) { if (!immOp(1, 1)) return false; }
            else if (op <= 0xD1) { o.b = Opnd{}; o.b.kind = kImm; o.b.size = 1; o.b.imm = 1; }
            else { o.b = regOp(1, 1); }
            if (o.mn == MOther) { o.mn = MWrite; o.w1 = rm.kind == kReg ? rm.reg : -1; }
        }
        else if (op == 0xC2) { o.mn = MRet; i += 2; }
        else if (op == 0xC3) { o.mn = MRet; }
        else if (op == 0xCC) { o.mn = MInt3; }
        else if (op == 0xC6 || op == 0xC7) { o.mn = MMov; const std::uint8_t sz = op == 0xC6 ? 1 : osz; Opnd rm; if (!modrm(sz, rm)) return false; o.a = rm; if (!immOp(op == 0xC6 ? 1 : z, sz)) return false; }
        else if (op == 0xE8 || op == 0xE9) { o.mn = op == 0xE8 ? MCall : MJmp; if (i + 4 > n) return false; std::int32_t r; std::memcpy(&r, p + i, 4); i += 4; o.target = (std::uint32_t)((std::int64_t)rva + (std::int64_t)i + r); }
        else if (op == 0xEB) { o.mn = MJmp; if (i >= n) return false; o.target = (std::uint32_t)((std::int64_t)rva + (std::int64_t)i + 1 + (std::int8_t)p[i]); i += 1; }
        else if (op == 0xF6 || op == 0xF7) {
            const std::uint8_t sz = op == 0xF6 ? 1 : osz;
            Opnd rm; if (!modrm(sz, rm)) return false;
            const int g = modReg & 7; o.a = rm;
            if (g < 2) { o.mn = MTest; if (!immOp(op == 0xF6 ? 1 : z, sz)) return false; }
            else if (g == 2) o.mn = MNot; else if (g == 3) o.mn = MNeg; else o.mn = MMulDiv;
        }
        else if (op == 0xFE || op == 0xFF) {
            const std::uint8_t sz = op == 0xFE ? 1 : osz;
            Opnd rm; if (!modrm(sz, rm)) return false;
            const int g = modReg & 7; o.a = rm;
            if (g == 0) o.mn = MInc; else if (g == 1) o.mn = MDec;
            else if (g == 2 && op == 0xFF) o.mn = MCall;
            else if (g == 4 && op == 0xFF) o.mn = MJmp;
            else if (g == 6 && op == 0xFF) o.mn = MPush;
            else o.mn = MOther;
        }
        else known = false;
    } else if (map == 1) {
        if (op == 0x1F) { o.mn = MNop; Opnd rm; if (!modrm(osz, rm)) return false; }
        else if (op >= 0x40 && op <= 0x4F) { o.mn = MWrite; o.cc = (std::int8_t)(op & 0xF); Opnd rm; if (!modrm(osz, rm)) return false; o.w1 = (std::int8_t)modReg; }
        else if (op >= 0x80 && op <= 0x8F) { o.mn = MJcc; o.cc = (std::int8_t)(op & 0xF); if (i + 4 > n) return false; std::int32_t r; std::memcpy(&r, p + i, 4); i += 4; o.target = (std::uint32_t)((std::int64_t)rva + (std::int64_t)i + r); }
        else if (op >= 0x90 && op <= 0x9F) { o.mn = MWrite; o.cc = (std::int8_t)(op & 0xF); Opnd rm; if (!modrm(1, rm)) return false; o.w1 = rm.kind == kReg ? rm.reg : -1; }
        else if (op == 0xAF) { o.mn = MImul; Opnd rm; if (!modrm(osz, rm)) return false; o.a = regOp(modReg, osz); o.b = rm; }
        else if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF) {
            o.mn = (op == 0xB6 || op == 0xB7) ? MMovzx : MMovsx;
            Opnd rm; if (!modrm((op & 1) ? 2 : 1, rm)) return false;
            o.a = regOp(modReg, osz); o.b = rm;
        }
        else if (op >= 0xC8 && op <= 0xCF) { o.mn = MBswap; o.a = regOp((op & 7) | (rB ? 8 : 0), osz); }
        else known = false;
    } else if (map == 2 && (op == 0xF0 || op == 0xF1) && !f2f3) {
        o.mn = MMovbe; Opnd rm; if (!modrm(osz, rm)) return false;
        if (op == 0xF0) { o.a = regOp(modReg, osz); o.b = rm; } else { o.a = rm; o.b = regOp(modReg, osz); }
    } else known = false;
    if (!known) {
        // anything else: its length, and the general registers its ModRM names are taken as written
        rtx::opprints::Insn L;
        if (!rtx::opprints::Decode(p, n < 15 ? n : 15, L)) return false;
        o = Ins{}; o.len = L.len; o.mn = MWrite;
        bool hasModrm = L.dispAt != 0 || (map != 0 && L.len > opAt + (map == 1 ? 2u : 3u));
        // vector instructions write vector registers; only these write a general one, by ModRM reg
        // (cvt to integer, movmsk, pextrw, pmovmskb, crc32) or by ModRM rm (movd/movq out, pextr)
        bool wReg = map == 0, wRm = map == 0;
        if (map == 1) {
            const bool vec = (op >= 0x10 && op <= 0x17) || (op >= 0x28 && op <= 0x2F) || (op >= 0x50 && op <= 0x7F) || op == 0xC2 || (op >= 0xC4 && op <= 0xC6) || op >= 0xD0;
            wReg = !vec || op == 0x2C || op == 0x2D || op == 0x50 || op == 0xC5 || op == 0xD7;
            wRm = !vec || (op == 0x7E && op66);
            if (op == 0x7E && op66) wReg = false;
        } else if (map == 2) { wReg = op == 0xF0 || op == 0xF1; wRm = false; }
        else if (map == 3) { wReg = false; wRm = op == 0x14 || op == 0x16 || op == 0x17; }
        if (hasModrm) {
            std::size_t mAt = opAt + (map == 0 ? 1 : map == 1 ? 2 : 3);
            if (mAt < L.len) {
                const std::uint8_t m = p[mAt];
                if (wReg) o.w1 = (std::int8_t)(((m >> 3) & 7) | (rR ? 8 : 0));
                if (wRm && (m >> 6) == 3) o.w2 = (std::int8_t)((m & 7) | (rB ? 8 : 0));
            }
        }
        return true;
    }
    if (i > 15 || i > n) return false;
    o.len = (std::uint8_t)i;
    return true;
}

// ---- the program ----
enum Xop : std::uint8_t { XNeg = 1, XNot, XAdd, XSub, XXor, XRsub };
// Operations a single-byte value goes through at register width after its read, kept so a branch
// on it can be evaluated for every value of the byte (the Xop codes above with their 32-bit
// immediates, then these).
enum SelOp : std::uint8_t { SSign = 16, SAnd, SShr, SSar, SShl, SWrap };   // SWrap: an 8-bit operation's wrap, not a mask of the handler's
using Chain = std::vector<std::pair<std::uint8_t, std::int32_t>>;
// byte offset -> the values of that byte which reach a place in the code
using SelKey = std::map<int, std::bitset<256>>;
inline std::string KeyText(const SelKey& k);   // below, with the text forms
inline std::int64_t SelEval(const Chain& chain, std::uint8_t b, int bytes) {
    std::int64_t x = b;
    for (const auto& op : chain) {
        switch (op.first) {
        case XNeg: x = -x; break; case XNot: x = ~x; break; case XAdd: x += op.second; break; case XSub: x -= op.second; break;
        case XXor: x ^= op.second; break; case XRsub: x = op.second - x; break;
        case SSign: x = (std::int64_t)(std::int8_t)x; break; case SAnd: x &= (std::int64_t)(std::uint32_t)op.second; break;
        case SShr: x = (std::int64_t)(((std::uint64_t)x & 0xFFFFFFFFull) >> (op.second & 31)); break;
        case SSar: x = (std::int64_t)((std::int32_t)x >> (op.second & 31)); break;
        case SShl: x = (std::int64_t)((std::uint64_t)x << (op.second & 63)); break;
        case SWrap: x &= 0xFF; break;
        default: break;
        }
    }
    const std::uint64_t m = bytes >= 8 ? ~0ull : ((1ull << (8 * bytes)) - 1);
    return (std::int64_t)((std::uint64_t)x & m);
}
inline bool HasSelOp(const Chain& c) { for (const auto& op : c) if (op.first >= SAnd && op.first != SWrap) return true; return false; }
// Whether condition code `cc` holds after cmp a,b at `bytes` width (the flags of cmp only).
inline bool CcKnown(int cc) { return (cc >= 2 && cc <= 7) || (cc >= 0xC && cc <= 0xF); }
inline bool CcHolds(int cc, std::int64_t a, std::int64_t b, int bytes) {
    const std::uint64_t m = bytes >= 8 ? ~0ull : ((1ull << (8 * bytes)) - 1);
    const std::uint64_t ua = (std::uint64_t)a & m, ub = (std::uint64_t)b & m;
    const int sh = bytes >= 8 ? 0 : 64 - 8 * bytes;
    const std::int64_t sa = (std::int64_t)(ua << sh) >> sh, sb = (std::int64_t)(ub << sh) >> sh;
    switch (cc) {
    case 2: return ua < ub; case 3: return ua >= ub; case 4: return ua == ub; case 5: return ua != ub;
    case 6: return ua <= ub; case 7: return ua > ub; case 0xC: return sa < sb; case 0xD: return sa >= sb;
    case 0xE: return sa <= sb; case 0xF: return sa > sb; default: return false;
    }
}
inline constexpr std::size_t kMaxPaths = 64;   // ways through one handler followed, selector forks included
struct Part {
    std::int16_t off = 0; std::uint8_t shift = 0, nx = 0;
    std::uint8_t op[3] = {}, imm[3] = {};
    bool operator==(const Part& o) const { return off == o.off && shift == o.shift && nx == o.nx && !std::memcmp(op, o.op, nx) && !std::memcmp(imm, o.imm, nx); }
};
struct Field {
    std::vector<Part> parts;
    int width = 1;                 // bytes: the highest shift / 8 + 1
    bool sign = false;
    std::set<std::string> sinks;   // where the value goes
    std::int64_t rank = INT64_MIN; // a field it is added to as an index (its displacement), else none
    std::uint64_t mask = 0;        // the payload bytes it is made of
};
// `forms`: more than one way through the handler, each keyed by the bytes it branches on; then the
// top-level fields are empty and `why` names the bytes. A single way: no forms, as before.
struct Program { std::vector<Field> fields; std::uint64_t read = 0; int len = 0; bool ok = false; std::string why; SelKey key; std::vector<Program> forms; };

namespace detail {
// kBufPos, kBufData: a pointer to the buffer's position or data field (lea r,[buf+0x18])
enum K : std::uint8_t { kNone, kBuf, kData, kPos, kPtr, kConst, kVal, kFld, kBufPos, kBufData };
struct SV { K k = kNone; std::int64_t n = 0; int v = -1; };
// `pend`: a left shift not yet put into the parts. A value shifted and then joined with another byte
// is a wider value (the shift goes into its parts); a value shifted and left alone is that value
// scaled (fine coordinates, sign extension) and reads as it was before the shift.
// `chain`: every operation since the read, for a single-byte value compared by a branch.
struct Val { std::vector<Part> parts; bool sign = false; int pend = 0; Chain chain; };
inline Val Placed(const Val& v, int bytes = 8) {
    Val o; o.sign = v.sign; o.chain = v.chain;
    for (const auto& p : v.parts) { if (p.shift + v.pend >= 8 * bytes) continue; Part q = p; q.shift = (std::uint8_t)(p.shift + v.pend); o.parts.push_back(q); }
    return o;
}
// Byte lanes: a value's parts by the byte they sit in; xor of two values is the difference of their
// parts (a part met twice cancels), so byte swaps written with shifts, masks and xor come out as the
// bytes they move. A part with offset -1 is a byte of sign the value was shifted right with.
inline Val Xor(const Val& a0, const Val& b0, int bytes) {
    const Val a = Placed(a0, bytes), b = Placed(b0, bytes);
    Val o; std::vector<char> used(b.parts.size(), 0);
    for (const auto& p : a.parts) {
        bool hit = false;
        for (std::size_t k = 0; k < b.parts.size(); ++k) if (!used[k] && b.parts[k] == p) { used[k] = 1; hit = true; break; }
        if (!hit) o.parts.push_back(p);
    }
    for (std::size_t k = 0; k < b.parts.size(); ++k) if (!used[k]) o.parts.push_back(b.parts[k]);
    return o;
}
inline bool ByteMask(std::uint64_t m) { for (int k = 0; k < 8; ++k) { const std::uint8_t b = (std::uint8_t)(m >> (8 * k)); if (b != 0 && b != 0xFF) return false; } return true; }

inline int Ctz(std::uint64_t m) { int k = 0; while (k < 64 && !((m >> k) & 1)) ++k; return k; }
inline int Pop(std::uint64_t m) { int k = 0; while (m) { m &= m - 1; ++k; } return k; }
inline std::uint64_t MaskOf(const Val& v) { std::uint64_t m = 0; for (const auto& p : v.parts) if (p.off >= 0 && p.off < 64) m |= 1ull << p.off; return m; }
inline bool Disjoint(const Val& a0, const Val& b0, int bytes = 8) {
    const Val a = Placed(a0, bytes), b = Placed(b0, bytes);
    std::uint64_t ba = 0, bb = 0;
    for (const auto& p : a.parts) { if (p.shift >= 64) return false; ba |= 0xFFull << p.shift; }
    for (const auto& p : b.parts) { if (p.shift >= 64) return false; bb |= 0xFFull << p.shift; }
    return !(ba & bb);
}

// One way through the code: the registers, the stack slots, the buffer position and the values of
// the payload bytes the way was chosen by.
struct Path { std::uint32_t pc = 0; SV st[16]; std::int64_t mempos = 0; SV stack[64]; bool stackSet[64] = {}; bool anyVal = false; SelKey sel; };

struct Run {
    const rtx::pktid::detail::Pe* pe = nullptr;
    std::vector<Val> vals;
    std::vector<std::pair<std::uint64_t, int>> made;   // (mask, value index) in the order made
    std::vector<int> madeRank;                          // the way through the code each was made on
    std::vector<SelKey> madeKey;                        // the byte values that way was chosen by
    std::vector<SelKey> ends;                           // the key of every way that ended
    const SelKey* cur = nullptr;                        // the key of the way being run
    std::set<std::string> queued;                       // forks taken already: place, position and key
    // queues the far side of a jump unless a way from the same place with the same position and
    // key is already on the list (the null checks before a read fork alike many times over)
    bool Fork(std::vector<Path>* forks, const Path& q, std::size_t cap) {
        if (!forks || forks->size() >= cap) return false;
        const std::string id = std::to_string(q.pc) + ":" + std::to_string(q.mempos) + ":" + KeyText(q.sel);
        if (!queued.insert(id).second) return true;   // as good as queued
        forks->push_back(q);
        return true;
    }
    int rank = 0, ranks = 0;
    std::map<std::uint64_t, std::vector<std::pair<SelKey, std::string>>> sinks;   // per value mask: the places, each with the key of the way that found it
    std::map<std::uint64_t, std::int64_t> idxRank;
    std::uint64_t read = 0;
    bool bad = false;
    std::string* trace = nullptr;   // a line per instruction with the registers after it (diagnostics)

    int NewVal(Val v) { vals.push_back(std::move(v)); made.emplace_back(MaskOf(vals.back()), (int)vals.size() - 1); madeRank.push_back(rank); madeKey.push_back(cur ? *cur : SelKey{}); return (int)vals.size() - 1; }
    void Sink(const SV& s, const std::string& tag) {
        if (s.k != kVal) return;
        auto& v = sinks[MaskOf(vals[(std::size_t)s.v])];
        const SelKey& k = cur ? *cur : SelKey{};
        for (const auto& e : v) if (e.second == tag && e.first == k) return;
        v.emplace_back(k, tag);
    }

    // runs `P` to its first ret (rax returned) or to where it cannot follow; with `forks`, the far side
    // of every forward conditional jump becomes a path of its own. A jump on a payload byte forks
    // with the byte's values split between the two sides, and is not taken at all when no value
    // reaches its far side; a jump on constants is decided.
    SV Go(Path& P, int depth, int maxIns, std::vector<Path>* forks) {
        SV* st = P.st; SV* stack = P.stack; bool* stackSet = P.stackSet;
        std::int64_t& mempos = P.mempos; std::uint32_t& pc = P.pc; bool& anyVal = P.anyVal;
        const SelKey* outer = cur; cur = &P.sel;
        SV ret;
        // the client's byte order check (cmp with 0x03020100) holds on every x64 host: the jump
        // after it is never taken, its far side the other byte order
        bool orderCheck = false;
        std::vector<SV> compared;   // the values the last compare read, until a jump, cmov or setcc says how
        int cmpSize = 0; bool cmpImm = false, cmpConst = false; std::int64_t cmpRhs = 0, cmpLhs = 0;   // the last compare's right side as a number, left side when constant
        for (int count = 0; count < maxIns; ++count) {
            const std::int64_t fo = pe->FoOf(pc);
            if (fo < 0) break;
            Ins in;
            if (!DecodeIns(pe->d + fo, pe->n - (std::size_t)fo, pc, in) || !in.len) break;
            const std::uint32_t at = pc;
            pc += in.len;
            struct TraceLine {
                Run* r; const SV* st; std::uint32_t at; const Ins& in; int depth;
                ~TraceLine() {
                    if (!r->trace) return;
                    char t[96]; std::snprintf(t, sizeof(t), "%*s%08X mn%-2d len%-2d", depth * 2, "", at, (int)in.mn, (int)in.len); *r->trace += t;
                    static const char* rn[16] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
                    for (int k = 0; k < 16; ++k) {
                        const SV& v = st[k];
                        if (v.k == kNone) continue;
                        static const char* kn[] = { "", "buf", "data", "pos", "ptr", "const", "val", "fld", "bufpos", "bufdata" };
                        std::snprintf(t, sizeof(t), " %s=%s", rn[k], kn[v.k]); *r->trace += t;
                        if (v.k == kVal) { std::uint64_t m = MaskOf(r->vals[(std::size_t)v.v]); std::snprintf(t, sizeof(t), "[%llx]", (unsigned long long)m); *r->trace += t; }
                        else if (v.k != kBuf && v.k != kData) { std::snprintf(t, sizeof(t), "(%lld)", (long long)v.n); *r->trace += t; }
                    }
                    *r->trace += "\n";
                }
            } traceLine{ this, st, at, in, depth };
            auto get = [&](const Opnd& o) -> SV { if (o.kind == kReg && !o.hi8) return st[o.reg]; if (o.kind == kImm) { SV c; c.k = kConst; c.n = o.imm; return c; } return SV{}; };
            auto set = [&](const Opnd& o, SV v) { if (o.kind != kReg) return; if (o.hi8) { st[o.reg] = SV{}; return; } st[o.reg] = v; if (v.k == kVal) anyVal = true; };
            auto memSV = [&](int r) -> SV { return r >= 0 && r < 16 ? st[r] : SV{}; };
            // where a memory operand points in the payload, or false
            auto payload = [&](const Opnd& m, std::int64_t& off) -> bool {
                if (m.kind != kMem) return false;
                const SV b = memSV(m.base), x = memSV(m.index);
                if (b.k == kData && x.k == kPos && m.scale == 1) { off = x.n + m.disp; return true; }
                if (x.k == kData && b.k == kPos && m.scale == 1) { off = b.n + m.disp; return true; }
                if (b.k == kPtr && m.index < 0) { off = b.n + m.disp; return true; }
                if (x.k == kPtr && m.base < 0 && m.scale == 1) { off = x.n + m.disp; return true; }
                return false;
            };
            // the buffer's field at `disp`, through the buffer or through a pointer to the field
            auto isBuf = [&](const Opnd& m, std::int32_t disp) {
                if (m.kind != kMem || m.index >= 0 || m.base < 0 || m.base >= 16) return false;
                const SV& b = st[m.base];
                if (b.k == kBuf) return m.disp == disp;
                return m.disp == 0 && ((b.k == kBufPos && disp == 0x18) || (b.k == kBufData && disp == 0x10));
            };
            auto readVal = [&](std::int64_t off, int w, bool sign) -> SV {
                Val v; v.sign = sign;
                if (sign && w == 1) v.chain.push_back({ SSign, 0 });
                for (int k = 0; k < w; ++k) {
                    if (off + k < 0 || off + k >= 64) { bad = true; continue; }
                    Part p; p.off = (std::int16_t)(off + k); p.shift = (std::uint8_t)(8 * k); v.parts.push_back(p);
                    read |= 1ull << (off + k);
                }
                SV s; s.k = kVal; s.v = NewVal(std::move(v)); return s;
            };
            // a byte transform of the wire byte while the value is still the byte as read; a value
            // already masked or shifted is an index or a count, the offset goes to its chain only
            auto withX = [&](const SV& s, std::uint8_t xop, std::uint8_t imm, std::int32_t imm32) -> SV {
                Val v = vals[(std::size_t)s.v];
                if (v.parts.size() != 1 || v.pend) return SV{};
                if (!HasSelOp(v.chain)) {
                    if (v.parts[0].nx >= 3) return SV{};
                    Part& p = v.parts[0]; p.op[p.nx] = xop; p.imm[p.nx] = imm; ++p.nx;
                }
                v.chain.push_back({ xop, imm32 });
                if (in.a.kind == kReg && in.a.size == 1) v.chain.push_back({ SWrap, 0 });   // an 8-bit operation wraps
                SV r; r.k = kVal; r.v = NewVal(std::move(v)); return r;
            };
            // the index of every memory operand is a place a value goes
            for (const Opnd* o : { &in.a, &in.b }) {
                if (o->kind != kMem || o->index < 0) continue;
                const SV x = st[o->index];
                if (x.k != kVal) continue;
                Sink(x, "idx:" + std::to_string(o->scale));
                const SV b = memSV(o->base);
                if (b.k == kFld) idxRank[MaskOf(vals[(std::size_t)x.v])] = b.n;
            }
            switch (in.mn) {
            case MRet: case MInt3: ret = st[0]; cur = outer; return ret;
            case MJmp:
                if (in.a.kind == kNoOp && in.target) {
                    if (!anyVal && count < 4) { pc = in.target; continue; }   // a thunk
                    if (in.target > at) { pc = in.target; continue; }           // the join after an if
                }
                cur = outer; return ret;
            case MJcc: {
                bool decided = false, take = false;
                if (!orderCheck && cmpConst && CcKnown(in.cc)) { decided = true; take = CcHolds(in.cc, cmpLhs, cmpRhs, cmpSize); }
                else if (!orderCheck && cmpImm && compared.size() == 1 && CcKnown(in.cc)) {
                    // a payload byte decides: the values on which the jump is taken go to the far side
                    const Val& v = vals[(std::size_t)compared[0].v];
                    if (v.parts.size() == 1 && v.parts[0].off >= 0 && v.parts[0].off < 64 && !v.pend) {
                        const int off = v.parts[0].off;
                        std::bitset<256> have; have.set();
                        auto it = P.sel.find(off); if (it != P.sel.end()) have = it->second;
                        std::bitset<256> t;
                        for (int b = 0; b < 256; ++b) if (have[(std::size_t)b] && CcHolds(in.cc, SelEval(v.chain, (std::uint8_t)b, cmpSize), cmpRhs, cmpSize)) t.set((std::size_t)b);
                        const std::bitset<256> f = have & ~t;
                        if (t.none()) { decided = true; take = false; }
                        else if (f.none()) { decided = true; take = true; }
                        else {
                            // the far side goes on with the values that take the jump, this way with
                            // the rest (whether or not the far side can still be queued)
                            Path q = P; q.pc = in.target; q.sel[off] = t; Fork(forks, q, kMaxPaths);
                            P.sel[off] = f; decided = true; take = false;
                        }
                    }
                }
                for (const SV& v : compared) if (in.cc != 4 && in.cc != 5) Sink(v, "ord");
                compared.clear(); cmpImm = cmpConst = false;
                if (decided) { orderCheck = false; if (take) pc = in.target; continue; }
                if (!orderCheck && in.target > at) { Path f = P; f.pc = in.target; Fork(forks, f, 24); }
                orderCheck = false;
                continue;
            }
            case MPush: case MNop: continue;
            case MCall: {
                bool buf = false;
                for (int a : { 1, 2, 8, 9 }) { if (st[a].k == kBuf) buf = true; Sink(st[a], std::string("arg:") + (a == 1 ? "rcx" : a == 2 ? "rdx" : a == 8 ? "r8" : "r9")); }
                SV r;
                bool kept[16] = {};   // argument registers the routine followed left as they were
                if (buf && depth < 2 && in.a.kind == kNoOp && in.target) {
                    // a routine that reads the buffer: its reads are the packet's; the far sides of its
                    // branches are run for their reads (and may fork again on a payload byte), the
                    // caller goes on from its first way through
                    Path sub; sub.pc = in.target; sub.mempos = mempos; sub.sel = P.sel;
                    for (int a : { 1, 2, 8, 9, 10, 11 }) if (st[a].k != kVal) sub.st[a] = st[a];
                    std::vector<Path> side;
                    r = Go(sub, depth + 1, 160, &side);
                    const int keep = rank;
                    for (std::size_t k = 0; k < side.size() && k < kMaxPaths; ++k) { Path q = side[k]; rank = ++ranks; Go(q, depth + 1, 160, &side); ends.push_back(q.sel); }
                    rank = keep;
                    mempos = sub.mempos;
                    P.sel = sub.sel;
                    // the buffer or a pointer the routine did not write stays in its register (the
                    // compiler keeps a caller-saved register across a call it knows does not touch it)
                    for (int a : { 1, 2, 8, 9, 10, 11 }) kept[a] = st[a].k != kNone && st[a].k != kVal && sub.st[a].k == st[a].k && sub.st[a].n == st[a].n && sub.st[a].v == st[a].v;
                }
                for (int a : { 0, 1, 2, 8, 9, 10, 11 }) if (!kept[a]) st[a] = SV{};
                if (r.k == kVal) { st[0] = r; anyVal = true; }
                continue;
            }
            case MPop: set(in.a, SV{}); continue;
            case MWrite:
                if (in.cc >= 0) { for (const SV& v : compared) if (in.cc != 4 && in.cc != 5) Sink(v, "ord"); compared.clear(); }
                if (in.w1 >= 0) st[in.w1] = SV{}; if (in.w2 >= 0) st[in.w2] = SV{}; continue;
            case MXchg: { if (in.a.kind == kReg) st[in.a.reg] = SV{}; if (in.b.kind == kReg) st[in.b.reg] = SV{}; continue; }
            case MCqo: st[2] = SV{}; continue;
            case MCdqe: if (st[0].k == kVal) { Val v = vals[(std::size_t)st[0].v]; v.sign = true; SV s; s.k = kVal; s.v = NewVal(std::move(v)); st[0] = s; } continue;
            case MMulDiv: st[0] = SV{}; st[2] = SV{}; continue;
            case MCmp: case MTest: {
                orderCheck = in.mn == MCmp && in.b.kind == kImm && in.b.imm == 0x03020100;   // against the global or a register loaded from it
                if (orderCheck) continue;
                std::int64_t poff;
                compared.clear(); cmpImm = cmpConst = false; cmpSize = in.a.size;
                const SV rb = get(in.b);
                if (in.b.kind == kImm) { cmpImm = true; cmpRhs = in.b.imm; } else if (rb.k == kConst) { cmpImm = true; cmpRhs = rb.n; }
                if (in.a.kind == kMem && payload(in.a, poff)) {   // a payload byte compared where it lies
                    const SV v = readVal(poff, in.a.size, false);
                    Sink(v, cmpImm ? "cmp:" + std::to_string(cmpRhs & ((1ll << (8 * in.a.size)) - 1)) : std::string("cmp:r"));
                    if (in.mn == MCmp) compared.push_back(v);
                    continue;
                }
                const SV a = get(in.a);
                if (in.mn == MTest) {
                    // test r,r compares with zero; test r,imm masks first
                    if (a.k == kVal && in.a.kind == kReg && in.b.kind == kReg && !in.a.hi8 && !in.b.hi8 && in.b.reg == in.a.reg) { Sink(a, "cmp:r"); compared.push_back(a); cmpImm = true; cmpRhs = 0; }
                    else if (a.k == kVal && in.b.kind == kImm) { Val x = vals[(std::size_t)a.v]; x.chain.push_back({ SAnd, (std::int32_t)in.b.imm }); SV m; m.k = kVal; m.v = NewVal(std::move(x)); Sink(a, "cmp:r"); compared.push_back(m); cmpImm = true; cmpRhs = 0; }
                    else { if (a.k == kVal) Sink(a, "cmp:r"); if (rb.k == kVal) Sink(rb, "cmp:r"); }
                    continue;
                }
                if (a.k == kVal) { Sink(a, cmpImm ? "cmp:" + std::to_string(cmpRhs) : std::string("cmp:r")); compared.push_back(a); }
                else if (a.k == kConst && cmpImm) { cmpConst = true; cmpLhs = a.n; }
                if (rb.k == kVal) Sink(rb, "cmp:r");
                continue;
            }
            default: break;
            }
            if (in.mn != MMov && in.mn != MMovzx && in.mn != MMovsx && in.mn != MMovbe && in.mn != MLea && in.mn != MBswap) { orderCheck = false; compared.clear(); }
            // stores and memory destinations
            if (in.a.kind == kMem) {
                if (isBuf(in.a, 0x18)) {
                    if (in.mn == MAdd && in.b.kind == kImm) mempos += in.b.imm;
                    else if (in.mn == MSub && in.b.kind == kImm) mempos -= in.b.imm;
                    else if (in.mn == MInc) mempos += 1;
                    else if (in.mn == MMov && in.b.kind == kReg && st[in.b.reg].k == kPos) mempos = st[in.b.reg].n;
                    continue;
                }
                if (in.mn == MMov && in.b.kind == kReg && !in.b.hi8) {
                    const SV v = st[in.b.reg];
                    if (in.a.base == 4 && in.a.index < 0) {
                        const int s = in.a.disp / 8;
                        if (in.a.disp >= 0 && s < 64 && !(in.a.disp & 7)) { stack[s] = v; stackSet[s] = true; }
                        Sink(v, "stack");
                    }
                    else if (in.a.base == 5) Sink(v, "stack");
                    else if (in.a.base == kRip) Sink(v, "global");
                    else { char t[24]; std::snprintf(t, sizeof(t), "st:0x%x", (unsigned)in.a.disp); Sink(v, t); }
                } else if (in.b.kind == kReg) Sink(st[in.b.reg], "arith");
                continue;
            }
            if (in.a.kind != kReg) continue;
            const Opnd& D = in.a;
            const SV dv = get(D);
            switch (in.mn) {
            case MMov: case MMovzx: case MMovsx: case MMovbe: {
                const Opnd& S = in.b;
                if (S.kind == kImm) { SV c; c.k = kConst; c.n = S.imm; set(D, c); continue; }
                if (S.kind == kReg) {
                    SV v = get(S);
                    if (v.k == kVal && (in.mn == MMovzx || in.mn == MMovsx) && S.size < D.size) {
                        // the source's low bytes only: lanes above them and bytes of sign are gone
                        Val x = Placed(vals[(std::size_t)v.v], S.size);
                        std::vector<Part> keep; for (const auto& p : x.parts) if (p.off >= 0) keep.push_back(p); x.parts = keep;
                        x.sign = in.mn == MMovsx;
                        if (in.mn == MMovsx && S.size == 1) x.chain.push_back({ SSign, 0 });
                        v.v = NewVal(std::move(x));
                    }
                    else if (v.k == kVal && in.mn == MMov && D.size == 4 && S.size == 4) {
                        // a 32-bit move keeps the low four bytes
                        const Val& x0 = vals[(std::size_t)v.v];
                        bool high = false; for (const auto& p : x0.parts) if (p.shift + x0.pend >= 32) high = true;
                        if (high) { Val x = Placed(x0); Val o; o.sign = false; for (const auto& p : x.parts) if (p.shift < 32) o.parts.push_back(p); v.v = NewVal(std::move(o)); }
                    }
                    set(D, v); continue;
                }
                if (isBuf(S, 0x18)) { SV p; p.k = kPos; p.n = mempos; set(D, p); continue; }
                if (isBuf(S, 0x10)) { SV p; p.k = kData; set(D, p); continue; }
                if (S.base == 4 && S.index < 0 && S.disp >= 0 && !(S.disp & 7) && S.disp / 8 < 64 && stackSet[S.disp / 8] && S.size >= 4) { set(D, stack[S.disp / 8]); continue; }
                std::int64_t off;
                if (payload(S, off)) {
                    SV v = readVal(off, S.size, in.mn == MMovsx);
                    if (in.mn == MMovbe) { Val x = vals[(std::size_t)v.v]; for (auto& p : x.parts) p.shift = (std::uint8_t)(8 * (S.size - 1) - p.shift); v.v = NewVal(std::move(x)); }
                    set(D, v); continue;
                }
                if (S.base >= 0 && S.base < 16 && S.index < 0 && st[S.base].k != kBuf && st[S.base].k != kData && st[S.base].k != kPos && st[S.base].k != kPtr && st[S.base].k != kBufPos && st[S.base].k != kBufData && S.base != 4 && S.base != 5) { SV f; f.k = kFld; f.n = S.disp; set(D, f); continue; }
                set(D, SV{}); continue;
            }
            case MLea: {
                const Opnd& S = in.b;
                const SV b = memSV(S.base), x = memSV(S.index);
                if (b.k == kBuf && S.index < 0 && (S.disp == 0x18 || S.disp == 0x10)) { SV r; r.k = S.disp == 0x18 ? kBufPos : kBufData; set(D, r); continue; }   // a pointer to the position or data field
                if ((b.k == kPos || b.k == kPtr) && S.index < 0) { SV r; r.k = b.k; r.n = b.n + S.disp; set(D, r); continue; }
                if (b.k == kData && x.k == kPos && S.scale == 1) { SV r; r.k = kPtr; r.n = x.n + S.disp; set(D, r); continue; }
                if (x.k == kData && b.k == kPos && S.scale == 1) { SV r; r.k = kPtr; r.n = b.n + S.disp; set(D, r); continue; }
                if (b.k == kVal && x.k == kVal && S.scale == 1 && S.disp == 0) {
                    const Val& va = vals[(std::size_t)b.v]; const Val& vb = vals[(std::size_t)x.v];
                    if (MaskOf(va) != MaskOf(vb) && Disjoint(va, vb, D.size)) { Val c = Placed(va, D.size); const Val d = Placed(vb, D.size); c.parts.insert(c.parts.end(), d.parts.begin(), d.parts.end()); c.sign = false; SV r; r.k = kVal; r.v = NewVal(std::move(c)); set(D, r); continue; }
                    Sink(b, MaskOf(va) == MaskOf(vb) ? "mul" : "arith"); set(D, SV{}); continue;
                }
                if (b.k == kVal && S.index < 0 && S.disp == 0) { set(D, b); continue; }
                if (b.k == kVal && S.index < 0) {   // lea r,[v+imm]: a byte offset
                    const SV r = withX(b, XAdd, (std::uint8_t)(S.disp & 0xFF), S.disp);
                    if (r.k == kVal) { set(D, r); continue; }
                }
                if (b.k == kVal) Sink(b, "arith");
                set(D, SV{}); continue;
            }
            case MBswap: {
                if (dv.k != kVal) { set(D, SV{}); continue; }
                Val x = Placed(vals[(std::size_t)dv.v]);
                for (auto& p : x.parts) { if (p.shift >= 8 * D.size) { bad = true; continue; } p.shift = (std::uint8_t)(8 * (D.size - 1) - p.shift); }
                SV r; r.k = kVal; r.v = NewVal(std::move(x)); set(D, r); continue;
            }
            case MRol: case MRor:
                if (dv.k == kVal && in.b.kind == kImm && in.b.imm % 8 == 0 && in.b.imm > 0 && in.b.imm < 8 * D.size) {
                    Val x = Placed(vals[(std::size_t)dv.v], D.size);
                    const int bits = 8 * D.size, r0 = in.mn == MRol ? (int)in.b.imm : bits - (int)in.b.imm;
                    bool lanes = true; for (const auto& p : x.parts) if (p.shift % 8 || p.shift >= bits) lanes = false;
                    if (lanes) {
                        for (auto& p : x.parts) p.shift = (std::uint8_t)((p.shift + r0) % bits);
                        SV r; r.k = kVal; r.v = NewVal(std::move(x)); set(D, r); continue;
                    }
                }
                if (dv.k == kVal) Sink(dv, "arith");
                set(D, SV{}); continue;
            case MNeg: case MNot:
                if (dv.k == kVal) { SV r = withX(dv, in.mn == MNeg ? XNeg : XNot, 0, 0); if (r.k != kVal) Sink(dv, "arith"); set(D, r); continue; }
                if (dv.k == kConst) { SV c = dv; c.n = in.mn == MNeg ? -c.n : ~c.n; set(D, c); continue; }
                set(D, SV{}); continue;
            case MImul: {
                // a multiply by a power of two is a shift (a byte put in place)
                const SV s = in.b.kind == kReg ? get(in.b) : SV{};
                SV val; std::int64_t k = 0; bool known = false;
                if (in.has3) { val = s; k = in.imm3; known = true; }
                else if (dv.k == kVal && s.k == kConst) { val = dv; k = s.n; known = true; }
                else if (dv.k == kConst && s.k == kVal) { val = s; k = dv.n; known = true; }
                if (known && val.k == kVal && k > 1 && !(k & (k - 1))) {
                    int sh = 0; while ((1ll << sh) < k) ++sh;
                    Val x = vals[(std::size_t)val.v]; x.pend += sh;
                    SV r; r.k = kVal; r.v = NewVal(std::move(x)); set(D, r); continue;
                }
                if (dv.k == kVal) Sink(dv, "mul");
                if (s.k == kVal) Sink(s, "mul");
                set(D, SV{}); continue;
            }
            case MInc: case MDec:
                if (dv.k == kPos || dv.k == kPtr || dv.k == kConst) { SV r = dv; r.n += in.mn == MInc ? 1 : -1; set(D, r); continue; }
                if (dv.k == kVal) Sink(dv, "arith");
                set(D, SV{}); continue;
            case MAdd: case MSub: case MXor: case MOr: case MAnd: case MShl: case MShr: case MSar: case MAdc: case MSbb: {
                Opnd S = in.b;
                if (S.kind == kReg && !S.hi8 && st[S.reg].k == kConst && !(in.mn == MXor && S.reg == D.reg)) { const std::int64_t c = st[S.reg].n; S = Opnd{}; S.kind = kImm; S.size = in.b.size; S.imm = c; }
                if (S.kind == kImm) {
                    const std::int64_t imm = S.imm;
                    if ((dv.k == kPos || dv.k == kPtr) && (in.mn == MAdd || in.mn == MSub)) { SV r = dv; r.n += in.mn == MAdd ? imm : -imm; set(D, r); continue; }
                    if (dv.k == kVal) {
                        if (in.mn == MShl) {
                            Val x = vals[(std::size_t)dv.v];
                            x.pend += (int)imm;
                            x.chain.push_back({ SShl, (std::int32_t)imm });
                            if (x.pend >= 64) { Sink(dv, "arith"); set(D, SV{}); continue; }
                            SV r; r.k = kVal; r.v = NewVal(std::move(x)); set(D, r); continue;
                        }
                        char t[32];
                        if (in.mn == MAnd && ByteMask((std::uint64_t)imm & (D.size >= 8 ? ~0ull : ((1ull << (8 * D.size)) - 1)))) {
                            const std::uint64_t m = (std::uint64_t)imm & (D.size >= 8 ? ~0ull : ((1ull << (8 * D.size)) - 1));
                            const Val x = Placed(vals[(std::size_t)dv.v], D.size);
                            Val o; o.sign = false; bool dropped = false;
                            for (const auto& p : x.parts) { if (p.shift % 8 == 0 && ((m >> p.shift) & 0xFF)) o.parts.push_back(p); else if (p.shift % 8 == 0) dropped = true; else { dropped = false; o.parts = x.parts; break; } }
                            if (dropped) {
                                if (o.parts.empty()) { SV z; z.k = kConst; z.n = 0; set(D, z); continue; }
                                SV r; r.k = kVal; r.v = NewVal(std::move(o)); set(D, r); continue;
                            }
                        }
                        if ((in.mn == MShr || in.mn == MSar) && vals[(std::size_t)dv.v].pend == 0 && imm % 8 == 0 && imm > 0 && imm < 64) {
                            const Val& x = vals[(std::size_t)dv.v];
                            bool lanes = true; for (const auto& p : x.parts) if (p.shift % 8) lanes = false;
                            if (lanes && x.parts.size() > 1) {
                                Val o; o.sign = x.sign;
                                for (const auto& p : x.parts) if (p.shift >= imm) { Part q = p; q.shift = (std::uint8_t)(p.shift - imm); o.parts.push_back(q); }
                                if (in.mn == MSar) for (int b = 8 * D.size - (int)imm; b < 8 * D.size; b += 8) { Part q; q.off = -1; q.shift = (std::uint8_t)b; o.parts.push_back(q); }
                                SV r; r.k = kVal; r.v = NewVal(std::move(o)); set(D, r); continue;
                            }
                        }
                        if ((in.mn == MShr || in.mn == MSar) && vals[(std::size_t)dv.v].pend >= imm) {
                            Val x = vals[(std::size_t)dv.v]; x.pend -= (int)imm; if (in.mn == MSar) x.sign = true;
                            SV r; r.k = kVal; r.v = NewVal(std::move(x)); set(D, r); continue;
                        }
                        if (in.mn == MAnd || in.mn == MShr || in.mn == MSar) {
                            std::snprintf(t, sizeof(t), "%s:0x%llx", in.mn == MAnd ? "and" : in.mn == MShr ? "shr" : "sar", (unsigned long long)(imm & 0xFFFFFFFF));
                            // the value stays (what the mask or shift keeps is the decoder's business),
                            // its chain remembers the operation for the branches that read it
                            Sink(dv, t);
                            Val x = vals[(std::size_t)dv.v]; x.chain.push_back({ (std::uint8_t)(in.mn == MAnd ? SAnd : in.mn == MShr ? SShr : SSar), (std::int32_t)imm });
                            SV r; r.k = kVal; r.v = NewVal(std::move(x)); set(D, r); continue;
                        }
                        if ((in.mn == MAdd || in.mn == MSub || in.mn == MXor) && vals[(std::size_t)dv.v].parts.size() == 1) {
                            SV r = withX(dv, in.mn == MAdd ? XAdd : in.mn == MSub ? XSub : XXor, (std::uint8_t)(imm & 0xFF), (std::int32_t)imm);
                            if (r.k == kVal) { set(D, r); continue; }
                        }
                        std::snprintf(t, sizeof(t), "%s:0x%llx", in.mn == MOr ? "or" : "arith", (unsigned long long)(imm & 0xFFFFFFFF));
                        Sink(dv, t); set(D, SV{}); continue;
                    }
                    if (dv.k == kConst) {
                        SV c = dv;
                        switch (in.mn) {
                        case MAdd: c.n += imm; break; case MSub: c.n -= imm; break; case MXor: c.n ^= imm; break; case MOr: c.n |= imm; break;
                        case MAnd: c.n &= imm; break; case MShl: c.n <<= imm; break; case MShr: case MSar: c.n >>= imm; break; default: c = SV{}; break;
                        }
                        set(D, c); continue;
                    }
                    set(D, SV{}); continue;
                }
                if (S.kind == kReg) {
                    const SV s = get(S);
                    if (in.mn == MXor && dv.k == kVal && s.k == kVal && !(S.reg == D.reg && !S.hi8)) {
                        Val c = Xor(vals[(std::size_t)dv.v], vals[(std::size_t)s.v], D.size);
                        if (c.parts.empty()) { SV z; z.k = kConst; z.n = 0; set(D, z); continue; }
                        SV r; r.k = kVal; r.v = NewVal(std::move(c)); set(D, r); continue;
                    }
                    if ((in.mn == MAdd || in.mn == MOr) && dv.k == kVal && s.k == kVal) {
                        const Val& va = vals[(std::size_t)dv.v]; const Val& vb = vals[(std::size_t)s.v];
                        if (Disjoint(va, vb, D.size)) { Val c = Placed(va, D.size); const Val d = Placed(vb, D.size); c.parts.insert(c.parts.end(), d.parts.begin(), d.parts.end()); c.sign = false; SV r; r.k = kVal; r.v = NewVal(std::move(c)); set(D, r); continue; }
                        Sink(dv, "arith"); Sink(s, "arith"); set(D, SV{}); continue;
                    }
                    if (dv.k == kConst && s.k == kVal && (in.mn == MSub || in.mn == MAdd || in.mn == MXor)) {
                        // c - byte, c + byte, c ^ byte: a byte transform with the constant first
                        const SV r = withX(s, in.mn == MSub ? XRsub : in.mn == MAdd ? XAdd : XXor, (std::uint8_t)(dv.n & 0xFF), (std::int32_t)dv.n);
                        if (r.k == kVal) { set(D, r); continue; }
                    }
                    if (in.mn == MAdd && dv.k == kData && s.k == kPos) { SV r; r.k = kPtr; r.n = s.n; set(D, r); continue; }
                    if (in.mn == MAdd && s.k == kData && dv.k == kPos) { SV r; r.k = kPtr; r.n = dv.n; set(D, r); continue; }
                    if (in.mn == MXor && S.reg == D.reg && !S.hi8) { SV c; c.k = kConst; c.n = 0; set(D, c); continue; }
                    if (dv.k == kVal) Sink(dv, "arith");
                    if (s.k == kVal) Sink(s, "arith");
                    set(D, SV{}); continue;
                }
                if (S.kind == kMem) {
                    if (in.mn == MAdd && dv.k == kData && isBuf(S, 0x18)) { SV r; r.k = kPtr; r.n = mempos; set(D, r); continue; }
                    std::int64_t off;
                    if (payload(S, off) && S.size == 1 && dv.k == kConst && (in.mn == MSub || in.mn == MAdd || in.mn == MXor)) {
                        SV v = readVal(off, 1, false);
                        SV r = withX(v, in.mn == MSub ? XRsub : in.mn == MAdd ? XAdd : XXor, (std::uint8_t)(dv.n & 0xFF), (std::int32_t)dv.n);
                        set(D, r); continue;
                    }
                    if (dv.k == kVal) Sink(dv, "arith");
                    set(D, SV{}); continue;
                }
                set(D, SV{}); continue;
            }
            default:
                set(D, SV{}); continue;
            }
        }
        cur = outer;
        return ret;
    }
};
}  // namespace detail

// Text of a form's key: per byte, the values that lead to it. A set that depends on the byte's low
// bits only prints with the mask ("b0&0x1f=2..6"), one that also leaves out a run of values at an
// end of the byte's range adds the bound ("b2&0x7<7 b2>=1"); a run prints as a bound or a range.
inline std::string KeyText(const SelKey& k) {
    std::string s;
    // a set of values 0..top as text after "bN" or "bN&M"
    auto ranges = [](const std::bitset<256>& set, int top) {
        char t[48]; std::string o;
        int lo = -1, hi = -1;
        for (int b = 0; b <= top; ++b) if (set[(std::size_t)b]) { if (lo < 0) lo = b; hi = b; }
        bool run = lo >= 0; for (int b = 0; b <= top && run; ++b) if (set[(std::size_t)b] != (b >= lo && b <= hi)) run = false;
        if (run && lo == hi) { std::snprintf(t, sizeof(t), "=%d", lo); return std::string(t); }
        if (run && lo == 0 && hi < top) { std::snprintf(t, sizeof(t), "<%d", hi + 1); return std::string(t); }
        if (run && hi == top && lo > 0) { std::snprintf(t, sizeof(t), ">=%d", lo); return std::string(t); }
        o = "=";
        bool first = true;
        for (int b = 0; b <= top; ++b) {
            if (!set[(std::size_t)b]) continue;
            int e = b; while (e + 1 <= top && set[(std::size_t)(e + 1)]) ++e;
            if (e > b) std::snprintf(t, sizeof(t), "%s%d..%d", first ? "" : ",", b, e); else std::snprintf(t, sizeof(t), "%s%d", first ? "" : ",", b);
            o += t; first = false; b = e;
        }
        return o;
    };
    for (const auto& kv : k) {
        const std::bitset<256>& set = kv.second;
        char t[48];
        if (!s.empty()) s += ' ';
        std::snprintf(t, sizeof(t), "b%d", kv.first);
        const std::string name = t;
        std::string best;
        for (int m = 1; m <= 0xFF; m = m * 2 + 1) {
            // the values by the low bits, then the raw values the set leaves out of those: none, or
            // a run at either end of 0..255; the shortest text of the masks that fit is kept
            std::bitset<256> low; for (int b = 0; b < 256; ++b) if (set[(std::size_t)b]) low.set((std::size_t)(b & m));
            if (m != 0xFF && (int)low.count() == m + 1) continue;   // the low bits say nothing
            int missLo = 256, missHi = -1, missing = 0;
            for (int b = 0; b < 256; ++b) if (low[(std::size_t)(b & m)] && !set[(std::size_t)b]) { ++missing; missLo = std::min(missLo, b); missHi = std::max(missHi, b); }
            const bool prefix = missing && missLo == 0 && missHi - missLo + 1 == missing, suffix = missing && missHi == 255 && missHi - missLo + 1 == missing;
            if (missing && !prefix && !suffix) continue;
            std::string o;
            if (m == 0xFF) o = name + ranges(set, 255);
            else {
                std::snprintf(t, sizeof(t), "&0x%x", m); o = name + t + ranges(low, m);
                if (prefix) { std::snprintf(t, sizeof(t), " %s>=%d", name.c_str(), missHi + 1); o += t; }
                if (suffix) { std::snprintf(t, sizeof(t), " %s<%d", name.c_str(), missLo); o += t; }
            }
            if (best.empty() || o.size() < best.size()) best = o;
        }
        s += best;
    }
    return s;
}
// The key from its text; false when the text is not one.
inline bool ParseKey(const std::string& t, SelKey& out) {
    out.clear();
    std::size_t k = 0;
    while (k < t.size()) {
        while (k < t.size() && t[k] == ' ') ++k;
        if (k >= t.size()) break;
        if (t[k] != 'b') return false;
        char* e = nullptr;
        const int off = (int)std::strtol(t.c_str() + k + 1, &e, 10); k = (std::size_t)(e - t.c_str());
        int mask = 0xFF;
        if (t.compare(k, 1, "&") == 0) { mask = (int)std::strtol(t.c_str() + k + 1, &e, 16); k = (std::size_t)(e - t.c_str()); }
        std::bitset<256> over;   // over the masked value
        if (t.compare(k, 2, ">=") == 0) { const int v = (int)std::strtol(t.c_str() + k + 2, &e, 10); k = (std::size_t)(e - t.c_str()); for (int b = v; b <= mask; ++b) over.set((std::size_t)b); }
        else if (t.compare(k, 1, "<") == 0) { const int v = (int)std::strtol(t.c_str() + k + 1, &e, 10); k = (std::size_t)(e - t.c_str()); for (int b = 0; b < v && b <= mask; ++b) over.set((std::size_t)b); }
        else if (t.compare(k, 1, "=") == 0) {
            ++k;
            for (;;) {
                const int a = (int)std::strtol(t.c_str() + k, &e, 10); k = (std::size_t)(e - t.c_str());
                int b2 = a;
                if (t.compare(k, 2, "..") == 0) { b2 = (int)std::strtol(t.c_str() + k + 2, &e, 10); k = (std::size_t)(e - t.c_str()); }
                for (int b = a; b <= b2 && b <= mask; ++b) over.set((std::size_t)b);
                if (k < t.size() && t[k] == ',') { ++k; continue; }
                break;
            }
        } else return false;
        std::bitset<256> set; for (int b = 0; b < 256; ++b) if (over[(std::size_t)(b & mask)]) set.set((std::size_t)b);
        auto it = out.find(off);
        if (it == out.end()) out[off] = set; else it->second &= set;   // a second clause on the byte narrows it
    }
    return !out.empty();
}
inline std::string PartsText(const Field& f);   // below, with the text forms

// The read program of the handler at `rva` for a packet of `len` bytes (fixed length only). A
// handler that branches on payload bytes gives one program per form in `out.forms`.
inline bool Extract(const rtx::pktid::detail::Pe& pe, std::uint32_t rva, int len, Program& out, std::string* trace = nullptr) {
    using namespace detail;
    out = Program{}; out.len = len;
    if (len <= 0 || len > 60) { out.why = "not a fixed length"; return false; }
    Run r; r.pe = &pe; r.trace = trace;
    std::vector<Path> work(1);
    work[0].pc = rva; work[0].st[2].k = kBuf;
    for (std::size_t w = 0; w < work.size() && w < kMaxPaths; ++w) { Path P = work[w]; if (trace) { *trace += "-- path "; *trace += KeyText(P.sel); *trace += "\n"; } r.rank = w ? ++r.ranks : 0; r.Go(P, 0, 300, &work); r.ends.push_back(P.sel); }
    const std::uint64_t all = len >= 64 ? ~0ull : ((1ull << len) - 1);
    // the forms: the distinct keys the ways through the code ended with. A value belongs to a form
    // when the way that made it was chosen by a superset of the form's byte values.
    std::vector<SelKey> keys;
    for (const auto& k : r.ends) if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    if (keys.empty()) keys.push_back(SelKey{});
    auto within = [](const SelKey& form, const SelKey& made) {
        for (const auto& kv : made) { auto it = form.find(kv.first); if (it == form.end() || (it->second & ~kv.second).any()) return false; }
        return true;
    };
    // the program of one form: the latest value of every byte set inside the payload, the fields
    // are the sets no other holds; where two such share a byte (two ways through the code read it
    // differently) the first way's reading stands
    auto build = [&](const SelKey& key, Program& p, bool single) {
        p = Program{}; p.len = len; p.key = key;
        std::map<std::uint64_t, int> last, lastRank;
        for (std::size_t mi = 0; mi < r.made.size(); ++mi) {
            if (!within(key, r.madeKey[mi])) continue;
            const auto& m = r.made[mi];
            const Val& v = r.vals[(std::size_t)m.second];
            bool in = false; std::uint64_t lanes = 0;
            for (const auto& q : v.parts) {
                if (q.off == -1) continue;
                if (q.off < 0 || q.off >= len || q.shift >= 64) { in = false; break; }
                in = true;
                const std::uint64_t bits = 0xFFull << q.shift;
                if (lanes & bits) { in = false; break; }   // two bytes in one place: a step of a swap, not a value
                lanes |= bits;
            }
            if (!in) continue;
            p.read |= m.first & all;
            const int rk = r.madeRank[mi];
            auto lr = lastRank.find(m.first);
            if (lr != lastRank.end() && lr->second < rk) continue;   // an earlier way through made it already
            last[m.first] = m.second; lastRank[m.first] = rk;
        }
        std::vector<std::uint64_t> cand;
        for (const auto& kv : last) {
            bool sub = false;
            for (const auto& o : last) if (o.first != kv.first && (kv.first & o.first) == kv.first) { sub = true; break; }
            if (!sub) cand.push_back(kv.first);
        }
        std::sort(cand.begin(), cand.end(), [&](std::uint64_t a, std::uint64_t b) {
            if (lastRank[a] != lastRank[b]) return lastRank[a] < lastRank[b];
            if (Pop(a) != Pop(b)) return Pop(a) > Pop(b);
            return a < b;
        });
        std::vector<std::uint64_t> masks;
        std::uint64_t seen = 0;
        for (std::uint64_t m : cand) { if (seen & m) continue; masks.push_back(m); seen |= m; }
        std::sort(masks.begin(), masks.end(), [](std::uint64_t a, std::uint64_t b) { return Ctz(a) < Ctz(b); });
        for (std::uint64_t m : masks) {
            const Val& v = r.vals[(std::size_t)last[m]];
            Field f; f.sign = v.sign; f.mask = m;
            for (const auto& q : v.parts) { if (q.off == -1) f.sign = true; else f.parts.push_back(q); }
            int hi = 0; for (const auto& q : v.parts) hi = std::max(hi, (int)q.shift);
            f.width = hi / 8 + 1;
            std::sort(f.parts.begin(), f.parts.end(), [](const Part& a, const Part& b) { return a.off < b.off; });
            auto it = r.sinks.find(m);
            if (it != r.sinks.end()) for (const auto& e : it->second) if (within(key, e.first)) f.sinks.insert(e.second);
            f.sinks.erase("stack");
            auto rk = r.idxRank.find(m); if (rk != r.idxRank.end()) f.rank = rk->second;
            p.fields.push_back(std::move(f));
        }
        if (r.bad) { p.why = "a read the run could not place"; return false; }
        if ((seen & p.read) != p.read) { p.why = "two ways through the code read the bytes differently"; return false; }
        // a single way must read every byte; a form reads what it needs, the rest is the packet's room for the others
        if (single && p.read != all) { char t[64]; std::snprintf(t, sizeof(t), "%d of %d bytes read", Pop(p.read), len); p.why = t; return false; }
        p.ok = true;
        return true;
    };
    if (keys.size() == 1) { const bool ok = build(keys[0], out, true); out.key.clear(); return ok; }   // one way: every payload
    std::vector<Program> forms;
    for (const auto& k : keys) { Program f; build(k, f, false); forms.push_back(std::move(f)); }
    // a byte every value of which leads to the form says nothing, nor does one past the payload
    auto trim = [&](Program& f) { for (auto it = f.key.begin(); it != f.key.end();) { if (it->first >= len || it->second.all()) it = f.key.erase(it); else ++it; } };
    for (auto& f : forms) trim(f);
    // the bytes and transforms a form reads, without sign or places
    auto shape = [](const Program& p) { std::string s; for (const auto& f : p.fields) { s += PartsText(f); s += '|'; } return s; };
    // forms that read the same bytes the same way are one form: their byte values join (until
    // nothing joins any more, as a join can free a byte and make two more alike)
    for (bool joined = true; joined;) {
        joined = false;
        for (std::size_t i = 0; i < forms.size(); ++i)
            for (std::size_t j = i + 1; j < forms.size();) {
                Program& a = forms[i]; const Program& b = forms[j];
                bool same = a.key.size() == b.key.size() && shape(a) == shape(b);
                for (const auto& kv : b.key) if (same && !a.key.count(kv.first)) same = false;
                if (!same) { ++j; continue; }
                for (const auto& kv : b.key) a.key[kv.first] |= kv.second;
                for (std::size_t f = 0; f < a.fields.size(); ++f) { a.fields[f].sign = a.fields[f].sign || b.fields[f].sign; a.fields[f].sinks.insert(b.fields[f].sinks.begin(), b.fields[f].sinks.end()); }
                if (!b.ok) { a.ok = false; if (a.why.empty()) a.why = b.why; }
                forms.erase(forms.begin() + (std::ptrdiff_t)j);
                trim(a);
                joined = true;
            }
    }
    // a way that ended with another form's byte values among its own is the way to that form (an
    // early exit before the fork), not a form
    for (std::size_t i = 0; i < forms.size();) {
        bool trunk = false;
        for (std::size_t j = 0; j < forms.size() && !trunk; ++j) if (j != i && forms[j].key != forms[i].key && within(forms[j].key, forms[i].key)) trunk = true;
        if (trunk) forms.erase(forms.begin() + (std::ptrdiff_t)i); else ++i;
    }
    for (std::size_t i = 0; i < forms.size(); ++i)
        for (std::size_t j = i + 1; j < forms.size();) {
            if (forms[i].key != forms[j].key) { ++j; continue; }
            if (forms[j].fields.size() > forms[i].fields.size()) forms[i] = forms[j];   // the way that read on
            forms.erase(forms.begin() + (std::ptrdiff_t)j);
        }
    if (forms.size() == 1) {
        out = forms[0]; out.key.clear(); out.forms.clear();
        if (out.ok && out.read != all) { char t[64]; std::snprintf(t, sizeof(t), "%d of %d bytes read", Pop(out.read), len); out.why = t; out.ok = false; }
        return out.ok;
    }
    out.forms = forms;
    std::string by; for (const auto& kv : out.forms[0].key) { if (!by.empty()) by += ", "; by += "b" + std::to_string(kv.first); }
    out.why = std::to_string(out.forms.size()) + " forms by " + by;
    out.read = 0; for (const auto& f : out.forms) out.read |= f.read;
    out.ok = true; for (const auto& f : out.forms) if (!f.ok) out.ok = false;
    return out.ok;
}

// A field's value in a payload: every byte through its transforms, shifted into place, then the
// width's sign when the handler reads it signed.
inline bool Value(const Field& f, const std::uint8_t* b, std::uint32_t n, std::int64_t& v) {
    std::uint64_t u = 0;
    for (const auto& p : f.parts) {
        if (p.off < 0 || (std::uint32_t)p.off >= n) return false;
        std::uint8_t x = b[p.off];
        for (int k = 0; k < p.nx; ++k) {
            switch (p.op[k]) {
            case XNeg: x = (std::uint8_t)(0 - x); break;
            case XNot: x = (std::uint8_t)~x; break;
            case XAdd: x = (std::uint8_t)(x + p.imm[k]); break;
            case XSub: x = (std::uint8_t)(x - p.imm[k]); break;
            case XXor: x = (std::uint8_t)(x ^ p.imm[k]); break;
            case XRsub: x = (std::uint8_t)(p.imm[k] - x); break;
            }
        }
        u |= (std::uint64_t)x << p.shift;
    }
    const int bits = f.width * 8;
    if (bits < 64) {
        u &= (1ull << bits) - 1;
        if (f.sign && (u >> (bits - 1)) & 1) u |= ~((1ull << bits) - 1);
    }
    v = (std::int64_t)u;
    return true;
}

// ---- text form (the reference table, logs) ----
inline std::string PartsText(const Field& f) {
    std::string s;
    std::vector<Part> ps = f.parts;
    std::sort(ps.begin(), ps.end(), [](const Part& a, const Part& b) { return a.shift > b.shift; });
    for (const auto& p : ps) {
        if (!s.empty()) s += ' ';
        char t[48]; std::snprintf(t, sizeof(t), "b%d", p.off); s += t;
        for (int k = 0; k < p.nx; ++k) {
            static const char* nm[] = { "", "neg", "not", "add", "sub", "xor", "rsub" };
            if (p.op[k] == XNeg || p.op[k] == XNot) s += std::string(".") + nm[p.op[k]];
            else { std::snprintf(t, sizeof(t), ".%s%02X", nm[p.op[k]], p.imm[k]); s += t; }
        }
        if (p.shift) { std::snprintf(t, sizeof(t), "<<%d", p.shift); s += t; }
    }
    return s;
}
inline std::string FieldText(const Field& f) {
    std::string s = std::to_string(f.width) + (f.sign ? "s " : "u ") + PartsText(f) + " ->";
    for (const auto& t : f.sinks) s += " " + t;
    if (f.rank != INT64_MIN) { char r[24]; std::snprintf(r, sizeof(r), " @0x%llx", (unsigned long long)f.rank); s += r; }
    return s;
}

// A reference field: width and where it goes, as the reference table keeps it.
struct RefField { int width; std::set<std::string> sinks; std::int64_t rank; };

// Pairs every reference field with a live field of its width, by most places in common (Jaccard),
// then by the order of the displacements they index with, then by read order. `map[i]` is the live
// field of reference field i. False when a field has no partner. `ambiguous` counts reference fields
// whose partner a swap with another of their width would not make worse.
inline bool Pair(const std::vector<RefField>& ref, const std::vector<Field>& live, std::vector<int>& map, int* ambiguous, std::string* why) {
    const std::size_t n = ref.size(), m = live.size();
    map.assign(n, -1);
    if (ambiguous) *ambiguous = 0;
    if (n != m) { if (why) *why = std::to_string(m) + " fields, the reference has " + std::to_string(n); return false; }
    auto jac = [](const std::set<std::string>& a, const std::set<std::string>& b) -> double {
        if (a.empty() && b.empty()) return 0.5;
        std::size_t inter = 0; for (const auto& t : a) inter += b.count(t);
        return (double)inter / (double)(a.size() + b.size() - inter);
    };
    // rank order of the indexed displacements inside each side, among fields of the same width
    auto rankPos = [&](auto get, std::size_t cnt, std::size_t i) -> int {
        const std::int64_t r = get(i); if (r == INT64_MIN) return -1;
        int pos = 0; for (std::size_t k = 0; k < cnt; ++k) { const std::int64_t o = get(k); if (k != i && o != INT64_MIN && o < r) ++pos; }
        return pos;
    };
    std::vector<std::vector<double>> score(n, std::vector<double>(n, -1e9)), core(n, std::vector<double>(n, -1e9));
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j < n; ++j) {
        if (ref[i].width != live[j].width) continue;
        double s = jac(ref[i].sinks, live[j].sinks);
        const int ri = rankPos([&](std::size_t k) { return ref[k].width == ref[i].width ? ref[k].rank : INT64_MIN; }, n, i);
        const int rj = rankPos([&](std::size_t k) { return live[k].width == live[j].width ? live[k].rank : INT64_MIN; }, n, j);
        if (ri >= 0 && rj >= 0) s += ri == rj ? 0.25 : -0.25;
        core[i][j] = s;
        score[i][j] = s + 0.001 * (1.0 - std::abs((double)i - (double)j) / (double)n);
    }
    // assignment, best total (Hungarian on the costs)
    const double INF = 1e18;
    std::vector<double> u(n + 1), v(n + 1); std::vector<int> p(n + 1), way(n + 1);
    for (std::size_t i = 1; i <= n; ++i) {
        p[0] = (int)i; std::size_t j0 = 0;
        std::vector<double> minv(n + 1, INF); std::vector<char> used(n + 1, 0);
        do {
            used[j0] = 1; const std::size_t i0 = (std::size_t)p[j0]; double delta = INF; std::size_t j1 = 0;
            for (std::size_t j = 1; j <= n; ++j) if (!used[j]) {
                const double cur = -score[i0 - 1][j - 1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = (int)j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (std::size_t j = 0; j <= n; ++j) { if (used[j]) { u[(std::size_t)p[j]] += delta; v[j] -= delta; } else minv[j] -= delta; }
            j0 = j1;
        } while (p[j0] != 0);
        do { const std::size_t j1 = (std::size_t)way[j0]; p[j0] = p[j1]; j0 = j1; } while (j0);
    }
    for (std::size_t j = 1; j <= n; ++j) if (p[j]) map[(std::size_t)p[j] - 1] = (int)j - 1;
    for (std::size_t i = 0; i < n; ++i) if (map[i] < 0 || core[i][(std::size_t)map[i]] < -1e8) { if (why) *why = "field " + std::to_string(i) + " has no partner of its width"; return false; }
    if (ambiguous) {
        std::vector<char> amb(n, 0);
        for (std::size_t i = 0; i < n; ++i) for (std::size_t k = i + 1; k < n; ++k) {
            const std::size_t a = (std::size_t)map[i], b = (std::size_t)map[k];
            if (ref[i].width != ref[k].width) continue;
            if (core[i][b] + core[k][a] >= core[i][a] + core[k][b] - 1e-9) amb[i] = amb[k] = 1;
        }
        for (char c : amb) *ambiguous += c;
    }
    return true;
}

// ---- the running build's layouts, one per binary ----
// kPaired: both programs whole and paired, payloads translate to the reference form.
// kSame: the reference program could not be read whole, and the live one reads the same as far as
// both go: the bytes stand as they come. kOff: neither; the decoders leave the packet raw.
// A packet with forms is kPaired when at least one form paired; each form says for itself.
enum State : std::uint8_t { kUnknown, kPaired, kSame, kOff };
// One form of a packet: the live program with its key, paired with a reference form or not.
struct Form {
    bool ok = false;                // derived and paired
    bool identical = false;         // paired with every field reading the same bytes the same way
    int ambiguous = 0;
    std::vector<int> map;           // reference field -> live field
    std::vector<Field> ref;         // the reference fields, with their parts
    // a field the handler only compares with one constant (a flag): the live build's constant and
    // the reference build's, per reference field; equal when there is nothing to map
    std::vector<std::pair<std::int64_t, std::int64_t>> flag;
    Program prog;                   // the live program
    std::string keyText, refKeyText;   // the live form's byte values, and the reference form's it paired with
    std::string why;
    // whether a payload is of this form
    bool Holds(const std::uint8_t* b, std::uint32_t n) const {
        for (const auto& kv : prog.key) if (kv.first < 0 || (std::uint32_t)kv.first >= n || !kv.second[b[kv.first]]) return false;
        return true;
    }
    // reference field i read from `b`; false when the form or the payload does not have it
    bool Get(int i, const std::uint8_t* b, std::uint32_t n, std::int64_t& v) const {
        if (!ok || i < 0 || (std::size_t)i >= map.size()) return false;
        return Value(prog.fields[(std::size_t)map[(std::size_t)i]], b, n, v);
    }
    // the one live field that goes to `sink` (a store displacement, an argument register), read
    // from `b`; false when none or several do. Reads a packet whose wire form changed by where its
    // values end up, which an update keeps.
    bool BySink(const char* sink, const std::uint8_t* b, std::uint32_t n, std::int64_t& v) const {
        const Field* hit = nullptr;
        for (const auto& f : prog.fields) if (f.sinks.count(sink)) { if (hit) return false; hit = &f; }
        return hit && Value(*hit, b, n, v);
    }
};
struct Layout {
    State state = kUnknown;
    std::vector<Form> forms;        // one for a packet read a single way, else one per live form
    std::vector<std::string> unpairedRef;   // reference forms no live form took
    std::string why;
    const Form* Pick(const std::uint8_t* b, std::uint32_t n) const { for (const auto& f : forms) if (f.Holds(b, n)) return &f; return nullptr; }
};
struct Layouts { bool ran = false; std::map<int, Layout> byRef; int known = 0, derived = 0, paired = 0, changed = 0, ambiguous = 0; std::string why, detail; };
inline Layouts& Current() { static Layouts l; return l; }
inline const Layout* Of(int refOp) { auto& L = Current(); auto it = L.byRef.find(refOp); return it == L.byRef.end() || it->second.state != kPaired ? nullptr : &it->second; }
// the live form a payload of reference packet `refOp` is in, paired or not; null without a layout
inline const Form* Pick(int refOp, const std::uint8_t* b, std::uint32_t n) { auto& L = Current(); auto it = L.byRef.find(refOp); return it == L.byRef.end() ? nullptr : it->second.Pick(b, n); }
inline State StateOf(int refOp) { auto& L = Current(); auto it = L.byRef.find(refOp); return it == L.byRef.end() ? kUnknown : it->second.state; }

// The reference build's programs (PacketReadsRef.h): opcode, length, whole (every byte read, or a
// form followed to its end), fields as FieldText separated by '|'. A form's row starts its text with
// the key in brackets; the rows of one opcode's forms follow each other.
struct RefProgram { int op; int len; bool whole; const char* fields; };

// The parts of a field from its text: "b2<<24 b3.neg<<16 b4.add80".
inline bool ParseParts(const std::string& t, std::vector<Part>& out) {
    out.clear();
    static const char* nm[] = { "", "neg", "not", "add", "sub", "xor", "rsub" };
    std::size_t k = 0;
    while (k < t.size()) {
        while (k < t.size() && t[k] == ' ') ++k;
        if (k >= t.size()) break;
        if (t[k] != 'b') return false;
        Part p; char* e = nullptr;
        p.off = (std::int16_t)std::strtol(t.c_str() + k + 1, &e, 10);
        k = (std::size_t)(e - t.c_str());
        while (k < t.size() && t[k] == '.') {
            ++k;
            int op = 0;
            for (int q = 6; q >= 1; --q) { const std::size_t L = std::strlen(nm[q]); if (t.compare(k, L, nm[q]) == 0) { op = q; k += L; break; } }
            if (!op || p.nx >= 3) return false;
            std::uint8_t imm = 0;
            if (op != XNeg && op != XNot) { imm = (std::uint8_t)std::strtoul(t.substr(k, 2).c_str(), nullptr, 16); k += 2; }
            p.op[p.nx] = (std::uint8_t)op; p.imm[p.nx] = imm; ++p.nx;
        }
        if (t.compare(k, 2, "<<") == 0) { p.shift = (std::uint8_t)std::strtoul(t.c_str() + k + 2, &e, 10); k = (std::size_t)(e - t.c_str()); }
        out.push_back(p);
    }
    return !out.empty();
}

inline std::vector<RefField> ParseRef(const char* text, std::vector<std::string>* shown = nullptr, std::vector<Field>* full = nullptr, SelKey* key = nullptr) {
    std::vector<RefField> out;
    std::string s = text ? text : "";
    if (key) key->clear();
    if (!s.empty() && s[0] == '[') { const std::size_t e = s.find(']'); if (e == std::string::npos) return out; if (key) ParseKey(s.substr(1, e - 1), *key); s = s.substr(e + 1); while (!s.empty() && s[0] == ' ') s.erase(0, 1); }
    std::size_t at = 0;
    while (at <= s.size() && !s.empty()) {
        std::size_t bar = s.find('|', at); if (bar == std::string::npos) bar = s.size();
        const std::string f = s.substr(at, bar - at);
        if (shown) shown->push_back(f);
        RefField r{}; r.width = std::atoi(f.c_str()); r.rank = INT64_MIN;
        const std::size_t arrow = f.find("->");
        if (full) {
            Field x; x.width = r.width;
            const std::size_t sp = f.find(' ');
            x.sign = sp != std::string::npos && sp > 0 && f[sp - 1] == 's';
            if (sp != std::string::npos) ParseParts(f.substr(sp + 1, (arrow == std::string::npos ? f.size() : arrow) - sp - 1), x.parts);
            for (const auto& p : x.parts) if (p.off >= 0 && p.off < 64) x.mask |= 1ull << p.off;
            full->push_back(std::move(x));
        }
        if (arrow != std::string::npos) {
            std::size_t k = arrow + 2;
            while (k < f.size()) {
                while (k < f.size() && f[k] == ' ') ++k;
                std::size_t e = f.find(' ', k); if (e == std::string::npos) e = f.size();
                const std::string t = f.substr(k, e - k);
                if (!t.empty() && t[0] == '@') r.rank = (std::int64_t)std::strtoull(t.c_str() + 1, nullptr, 16);
                else if (!t.empty()) r.sinks.insert(t);
                k = e;
            }
        }
        out.push_back(r);
        if (bar >= s.size()) break;
        at = bar + 1;
    }
    return out;
}
inline std::string ProgramText(const Program& p) {
    std::string s;
    for (std::size_t i = 0; i < p.fields.size(); ++i) { if (i) s += '|'; s += FieldText(p.fields[i]); }
    return s;
}

// Pairs one reference program with one live program (fields, flags, identical); false with `why`.
inline bool PairForm(const RefProgram& R, const Program& live, Form& f) {
    f = Form{}; f.prog = live; f.keyText = KeyText(live.key);
    std::vector<std::string> shown;
    SelKey rk;
    const std::vector<RefField> rf = ParseRef(R.fields, &shown, &f.ref, &rk);
    f.refKeyText = KeyText(rk);
    if (!Pair(rf, f.prog.fields, f.map, &f.ambiguous, &f.why)) return false;
    f.ok = true;
    f.flag.assign(rf.size(), { 0, 0 });
    for (std::size_t k = 0; k < rf.size(); ++k) {
        auto only = [](const std::set<std::string>& t, std::int64_t& c) {
            int n = 0; for (const auto& x : t) { if (x == "ord") return false; if (x.rfind("cmp:", 0) == 0 && x != "cmp:r") { c = std::atoll(x.c_str() + 4); ++n; } }
            return n == 1;
        };
        std::int64_t a = 0, b = 0;
        if (rf[k].width <= 2 && only(rf[k].sinks, b) && only(f.prog.fields[(std::size_t)f.map[k]].sinks, a) && a != b) f.flag[k] = { a, b };
    }
    f.identical = true;   // every paired field reads the same bytes the same way
    for (std::size_t k = 0; k < shown.size(); ++k) {
        const std::string a = shown[k], b = FieldText(f.prog.fields[(std::size_t)f.map[k]]);
        if (a.substr(0, a.find("->")) != b.substr(0, b.find("->"))) f.identical = false;
    }
    for (const auto& fl : f.flag) if (fl.first != fl.second) f.identical = false;   // a flag to map
    return true;
}
// Every reference packet's layout in the exe `file`, its handlers at `invokeOf[liveOp]`: extracted,
// paired with the reference, kept in Current(). The rows of one opcode are its forms: each pairs
// with a live form whose byte values overlap its own and whose fields pair.
inline void Derive(const std::uint8_t* file, std::size_t size, const RefProgram* ref, std::size_t refN, const std::uint32_t* invokeOf, std::size_t invokeN) {
    Layouts L; L.ran = true;
    rtx::pktid::detail::Pe pe;
    if (!rtx::pktid::detail::Open(file, size, pe)) { L.why = "exe not readable"; Current() = L; return; }
    for (std::size_t i = 0; i < refN;) {
        std::size_t j = i; while (j < refN && ref[j].op == ref[i].op) ++j;   // the rows of this opcode
        const RefProgram& R = ref[i];
        ++L.known;
        Layout lay;
        const int live = rtx::pktid::LiveOf(R.op);
        lay.state = kOff;
        if (live < 0 || (std::size_t)live >= invokeN || !invokeOf[live]) { lay.why = "packet not in this exe"; L.byRef[R.op] = lay; i = j; continue; }
        Program prog;
        const bool whole = Extract(pe, invokeOf[live], R.len, prog);
        const bool refForms = R.fields && R.fields[0] == '[';
        if (!refForms && prog.forms.empty() && !R.whole) {
            // the reference was not read to its last byte: the bytes stand only where the live
            // handler reads exactly what the reference did, as far as either goes
            const std::string a = R.fields ? R.fields : "";
            std::string b; for (std::size_t k = 0; k < prog.fields.size(); ++k) { if (k) b += '|'; b += FieldText(prog.fields[k]); }
            auto bare = [](const std::string& t) { std::string o; std::size_t at = 0; while (at <= t.size()) { std::size_t bar = t.find('|', at); if (bar == std::string::npos) bar = t.size(); const std::string f = t.substr(at, bar - at); o += f.substr(0, f.find("->")) + '|'; at = bar + 1; } return o; };
            Form f; f.prog = prog; lay.forms.push_back(f);
            if (!a.empty() && bare(a) == bare(b)) { lay.state = kSame; lay.why = "reads the same as the reference as far as both go"; }
            else lay.why = "the reference is not read whole and the live handler reads otherwise";
            L.byRef[R.op] = lay; i = j; continue;
        }
        // the live forms (one, keyless, when the handler reads a single way) and the reference rows
        std::vector<Program> liveForms = prog.forms.empty() ? std::vector<Program>{ prog } : prog.forms;
        if (!whole && prog.forms.empty()) { lay.why = prog.why.empty() ? "handler not read" : prog.why; Form f; f.prog = prog; lay.forms.push_back(f); L.byRef[R.op] = lay; i = j; continue; }
        ++L.derived;
        std::vector<Form> forms(liveForms.size());
        std::vector<char> taken(liveForms.size(), 0);
        std::vector<int> gap(liveForms.size(), 1 << 30);   // per live form: how far in fields the nearest reference form that failed to pair was
        for (std::size_t k = 0; k < liveForms.size(); ++k) { forms[k].prog = liveForms[k]; forms[k].keyText = KeyText(liveForms[k].key); forms[k].why = liveForms[k].ok ? "no reference form for " + forms[k].keyText : liveForms[k].why; }
        for (std::size_t q = i; q < j; ++q) {
            // a reference form takes the live form with overlapping byte values whose fields pair
            // best (fewest changed); without one it is noted with the reason the nearest gave
            SelKey rk; const std::size_t refFields = ParseRef(ref[q].fields, nullptr, nullptr, &rk).size();
            int best = -1, bestChanged = 1 << 30; Form bestForm; std::string nearest;
            for (std::size_t k = 0; k < liveForms.size(); ++k) {
                if (taken[k] || !liveForms[k].ok) continue;
                bool compatible = true;
                for (const auto& kv : rk) { auto it = liveForms[k].key.find(kv.first); if (it != liveForms[k].key.end() && (it->second & kv.second).none()) compatible = false; }
                if (!compatible) continue;
                Form f;
                if (!PairForm(ref[q], liveForms[k], f)) {
                    if (nearest.empty()) nearest = f.why;
                    const int g = std::abs((int)refFields - (int)liveForms[k].fields.size());
                    if (g < gap[k]) { gap[k] = g; forms[k].why = f.why; }   // the reference form nearest in fields gives the reason
                    continue;
                }
                int changed = 0; for (std::size_t m = 0; m < f.map.size(); ++m) { const std::string a = FieldText(f.ref[m]), b = FieldText(f.prog.fields[(std::size_t)f.map[m]]); if (a.substr(0, a.find("->")) != b.substr(0, b.find("->"))) ++changed; }
                if (changed < bestChanged) { best = (int)k; bestChanged = changed; bestForm = f; }
            }
            if (best < 0) { lay.unpairedRef.push_back(KeyText(rk) + (nearest.empty() ? "" : " (" + nearest + ")")); continue; }
            forms[(std::size_t)best] = bestForm; taken[(std::size_t)best] = 1;
            if (!bestForm.identical) ++L.changed;
            if (bestForm.ambiguous) ++L.ambiguous;
        }
        lay.forms = forms;
        int paired = 0; for (const auto& f : forms) if (f.ok) ++paired;
        if (paired) { lay.state = kPaired; ++L.paired; }
        // the reason, form by form: a live form without a partner is read by its sinks only
        if (forms.size() == 1 && !paired) lay.why = forms[0].why;
        else {
            lay.why = std::to_string(paired) + " of " + std::to_string(forms.size()) + " live forms paired";
            for (const auto& f : forms) if (!f.ok) lay.why += "; live form [" + f.keyText + "]: " + f.why;
            for (const auto& u : lay.unpairedRef) lay.why += "; reference form [" + u + "] has no live form";
        }
        L.byRef[R.op] = lay;
        i = j;
    }
    Current() = L;
}

// A packet of this build in the reference build's wire form: the form its bytes pick, every
// reference field taking the value its partner makes of the live bytes, written back through the
// reference parts (each transform undone, last first). False, `out` untouched, when the packet has
// no paired form or does not fit.
inline bool Translate(int refOp, const std::uint8_t* b, std::uint32_t n, std::uint8_t* out, std::uint32_t cap) {
    const Layout* lay = Of(refOp);
    const Form* L = lay ? lay->Pick(b, n) : nullptr;
    if (!L || !L->ok || (int)n != L->prog.len || n > cap) return false;
    if (L->identical) { std::memcpy(out, b, n); return true; }
    std::uint8_t tmp[64] = {};
    if (n > sizeof(tmp)) return false;
    for (std::size_t i = 0; i < L->ref.size(); ++i) {
        std::int64_t v;
        if (!Value(L->prog.fields[(std::size_t)L->map[i]], b, n, v)) return false;
        if (i < L->flag.size() && L->flag[i].first != L->flag[i].second) {
            // a flag: the live constant reads as the reference's, and the reference's as the live one
            const int bits = L->ref[i].width * 8; const std::uint64_t m = (1ull << bits) - 1;
            const std::uint64_t u = (std::uint64_t)v & m, a = (std::uint64_t)L->flag[i].first & m, r = (std::uint64_t)L->flag[i].second & m;
            v = (std::int64_t)(u == a ? r : u == r ? a : u);
        }
        for (const auto& p : L->ref[i].parts) {
            if (p.off < 0 || (std::uint32_t)p.off >= n || p.shift >= 64) return false;
            std::uint8_t x = (std::uint8_t)((std::uint64_t)v >> p.shift);
            for (int k = p.nx - 1; k >= 0; --k) {
                switch (p.op[k]) {
                case XNeg: x = (std::uint8_t)(0 - x); break;
                case XNot: x = (std::uint8_t)~x; break;
                case XAdd: x = (std::uint8_t)(x - p.imm[k]); break;
                case XSub: x = (std::uint8_t)(x + p.imm[k]); break;
                case XXor: x = (std::uint8_t)(x ^ p.imm[k]); break;
                case XRsub: x = (std::uint8_t)(p.imm[k] - x); break;
                }
            }
            tmp[p.off] = x;
        }
    }
    std::memcpy(out, tmp, n);
    return true;
}

// The variable-length packets whose fixed head the decoders read field by field, with its length:
// a zone update's plane and offsets.
struct HeadOf { int op, len; };
inline constexpr HeadOf kHeads[] = { { 0x31, 3 } };

// PacketReadsRef.h from a reference exe: every fixed-length packet's program, then each fixed-length
// zone sub-packet with its own handler under rtx::pktid::kZoneOwn + its sub id, then each head in
// kHeads under rtx::pktid::kHead + the packet's id.
inline std::string RefReadsText(const std::uint8_t* file, std::size_t size, const std::string& from, std::string& log) {
    std::vector<rtx::pktid::Row> t; std::vector<rtx::pktid::ZoneRow> zt; std::string why;
    if (!rtx::pktid::Table(file, size, t, &why, &zt)) { log = "no packet table: " + why + "\n"; return std::string(); }
    rtx::pktid::detail::Pe pe;
    if (!rtx::pktid::detail::Open(file, size, pe)) { log = "not a PE image\n"; return std::string(); }
    std::sort(t.begin(), t.end(), [](const rtx::pktid::Row& a, const rtx::pktid::Row& b) { return a.op < b.op; });
    std::string s = "#pragma once\n// Generated by RuneToolsXLauncher --pkt-reads from " + from + ". Do not edit.\n"
                    "// The reference build's fixed-length server packets as their handlers read them (PacketReads.h):\n"
                    "// opcode, length, then each field as width, sign, parts and where the value goes.\n"
                    "#include \"PacketReads.h\"\n\nnamespace rtx::preads {\n\ninline constexpr RefProgram kRefReads[] = {\n";
    int ok = 0, skipped = 0;
    for (const auto& r : t) {
        if (r.len <= 0) continue;
        Program p;
        char head[48];
        const bool whole = Extract(pe, r.invoke, r.len, p);
        if (!p.forms.empty()) {
            // one row per form, the key first
            for (const auto& f : p.forms) {
                if (!f.ok) { ++skipped; std::snprintf(head, sizeof(head), "partial 0x%02X: ", r.op); log += head + ("form [" + KeyText(f.key) + "] " + f.why) + "\n"; } else ++ok;
                std::snprintf(head, sizeof(head), "    { 0x%02X, %2d, %s, \"[", r.op, r.len, f.ok ? "true " : "false");
                s += head; s += KeyText(f.key); s += "] "; s += ProgramText(f); s += "\" },\n";
            }
            continue;
        }
        if (!whole) {
            ++skipped; std::snprintf(head, sizeof(head), "partial 0x%02X: ", r.op); log += head + p.why + "\n";
            if (p.fields.empty()) continue;
        } else ++ok;
        std::snprintf(head, sizeof(head), "    { 0x%02X, %2d, %s, \"", r.op, r.len, whole ? "true " : "false");
        s += head; s += ProgramText(p); s += "\" },\n";
    }
    for (const auto& z : zt) {
        if (z.len <= 0 || !z.invoke) continue;
        Program p;
        char head[48];
        const int op = rtx::pktid::kZoneOwn + z.sub;
        const bool whole = Extract(pe, z.invoke, z.len, p);
        if (!whole) {
            ++skipped; std::snprintf(head, sizeof(head), "partial 0x%02X: ", op); log += head + p.why + "\n";
            if (p.fields.empty()) continue;
        } else ++ok;
        std::snprintf(head, sizeof(head), "    { 0x%02X, %2d, %s, \"", op, z.len, whole ? "true " : "false");
        s += head; s += ProgramText(p); s += "\" },\n";
    }
    for (const auto& h : kHeads) {
        const auto r = std::find_if(t.begin(), t.end(), [&](const rtx::pktid::Row& x) { return x.op == h.op; });
        if (r == t.end() || r->len > 0 || !r->invoke) { char w[64]; std::snprintf(w, sizeof(w), "head of 0x%02X: not a variable-length packet here\n", h.op); log += w; continue; }
        Program p;
        char head[48];
        const int op = rtx::pktid::kHead + h.op;
        const bool whole = Extract(pe, r->invoke, h.len, p);
        if (!whole) {
            ++skipped; std::snprintf(head, sizeof(head), "partial 0x%02X: ", op); log += head + p.why + "\n";
            if (p.fields.empty()) continue;
        } else ++ok;
        std::snprintf(head, sizeof(head), "    { 0x%02X, %2d, %s, \"", op, h.len, whole ? "true " : "false");
        s += head; s += ProgramText(p); s += "\" },\n";
    }
    s += "};\ninline constexpr std::size_t kRefReadsCount = sizeof(kRefReads) / sizeof(kRefReads[0]);\n\n}  // namespace rtx::preads\n";
    log += std::to_string(ok) + " programs read whole, " + std::to_string(skipped) + " in part\n";
    return s;
}

// Both builds' programs side by side and how the fields paired: for a look at an update by hand.
inline std::string DiffText(const std::uint8_t* refFile, std::size_t refSize, const std::uint8_t* liveFile, std::size_t liveSize,
                            const rtx::pktid::RefRow* kref, std::size_t krefN, const rtx::pktid::ZoneRef* zref = nullptr, std::size_t zrefN = 0) {
    std::string log;
    const std::string gen = RefReadsText(refFile, refSize, "the reference exe", log);
    // the reference programs straight from the generated text
    struct Row { int op, len; bool whole; std::string f; };
    std::vector<Row> rows;
    std::size_t at = 0;
    while ((at = gen.find("    { 0x", at)) != std::string::npos) {
        Row r;
        r.op = (int)std::strtol(gen.c_str() + at + 8, nullptr, 16);
        const std::size_t c1 = gen.find(',', at), c2 = gen.find(',', c1 + 1), q1 = gen.find('"', at), q2 = gen.find('"', q1 + 1);
        r.len = std::atoi(gen.c_str() + c1 + 1);
        r.whole = gen.compare(c2 + 2, 4, "true") == 0;
        r.f = gen.substr(q1 + 1, q2 - q1 - 1);
        rows.push_back(r);
        at = q2;
    }
    std::vector<RefProgram> ref;
    for (const auto& r : rows) ref.push_back(RefProgram{ r.op, r.len, r.whole, r.f.c_str() });
    rtx::pktid::SetCurrent(liveFile, liveSize, kref, krefN, zref, zrefN);
    const auto& P = rtx::pktid::Current();
    Derive(liveFile, liveSize, ref.data(), ref.size(), P.invoke, sizeof(P.invoke) / sizeof(P.invoke[0]));
    const Layouts& L = Current();
    std::string s = log;
    char line[200];
    std::snprintf(line, sizeof(line), "%d reference programs: %d read in the live exe, %d paired, %d changed, %d with a field a swap would not change\n",
                  L.known, L.derived, L.paired, L.changed, L.ambiguous);
    s += line;
    {   // the zone sub-packets
        const auto& Z = P.zone;
        std::snprintf(line, sizeof(line), "zone subs: %d live, %d reference; carried %d by twin, %d by name, %d by length; %d length differ; added: %s; new: %s\n",
                      Z.liveCount, Z.refCount, Z.byTwin, Z.byName, Z.byLength, Z.lenDiffer, Z.addedSubs.empty() ? "none" : Z.addedSubs.c_str(), Z.newSubs.empty() ? "none" : Z.newSubs.c_str());
        s += line;
        for (int r = 0; r < 64; ++r) if (Z.ok && Z.toLive[r] >= 0) {
            std::snprintf(line, sizeof(line), "    ref sub %d -> live %d (len %d, twin %d)\n", r, Z.toLive[r], Z.liveLen[Z.toLive[r]], Z.twinRef[r]);
            s += line;
        }
    }
    // round trip: random live payloads translated, every reference field read back from the result
    // must hold the value its partner reads from the live bytes
    {
        std::uint64_t x = 0x9E3779B97F4A7C15ull; int tried = 0, failed = 0; std::string bad;
        for (const auto& kv : L.byRef) {
            const Layout& lay = kv.second;
            if (lay.state != kPaired) continue;
            for (const Form& f : lay.forms) {
                if (!f.ok) continue;
                const std::uint32_t n = (std::uint32_t)f.prog.len;
                for (int t = 0; t < 200; ++t) {
                    std::uint8_t in[64], out[64];
                    for (std::uint32_t k = 0; k < n; ++k) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; in[k] = (std::uint8_t)x; }
                    // the bytes the form is chosen by take a value of the form's set
                    for (const auto& sk : f.prog.key) {
                        if (sk.first < 0 || (std::uint32_t)sk.first >= n) continue;
                        int v = in[sk.first];
                        for (int q = 0; q < 256 && !sk.second[(std::size_t)v]; ++q) v = (v + 1) & 0xFF;
                        in[sk.first] = (std::uint8_t)v;
                    }
                    ++tried;
                    bool ok = lay.Pick(in, n) == &f && Translate(kv.first, in, n, out, sizeof(out));
                    for (std::size_t i = 0; ok && i < f.ref.size(); ++i) {
                        std::int64_t a, b;
                        if (!Value(f.prog.fields[(std::size_t)f.map[i]], in, n, a) || !Value(f.ref[i], out, n, b)) { ok = false; break; }
                        const int bits = f.ref[i].width * 8;
                        const std::uint64_t m = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
                        if (i < f.flag.size() && f.flag[i].first != f.flag[i].second) {
                            const std::uint64_t u = (std::uint64_t)a & m, fa = (std::uint64_t)f.flag[i].first & m, fr = (std::uint64_t)f.flag[i].second & m;
                            a = (std::int64_t)(u == fa ? fr : u == fr ? fa : u);
                        }
                        if (((std::uint64_t)a & m) != ((std::uint64_t)b & m)) ok = false;
                    }
                    if (!ok) { ++failed; if (bad.size() < 200) { char t2[16]; std::snprintf(t2, sizeof(t2), " 0x%02X", kv.first); if (bad.find(t2) == std::string::npos) bad += t2; } }
                }
            }
        }
        std::snprintf(line, sizeof(line), "round trip: %d payloads, %d wrong%s\n\n", tried, failed, failed ? " in" : "");
        s += line; s += bad; if (failed) s += "\n\n";
    }
    for (const auto& R : ref) {
        auto it = L.byRef.find(R.op);
        if (it == L.byRef.end()) continue;
        const Layout& lay = it->second;
        SelKey rk; std::vector<std::string> shown; ParseRef(R.fields, &shown, nullptr, &rk);
        const std::string rkt = KeyText(rk);
        const Form* f = nullptr;
        for (const auto& x : lay.forms) if (x.ok && x.refKeyText == rkt) f = &x;
        const std::string state = f ? std::string(f->identical ? "paired, unchanged" : "paired, CHANGED") : lay.state == kSame ? "as read: " + lay.why : "OFF: " + lay.why;
        std::snprintf(line, sizeof(line), "0x%02X len %d%s%s -> live 0x%02X: ", R.op, R.len, rkt.empty() ? "" : " form ", rkt.c_str(), rtx::pktid::LiveOf(R.op));
        s += line; s += state; s += f && f->ambiguous ? " (ambiguous)\n" : "\n";
        for (std::size_t k = 0; k < shown.size(); ++k) {
            s += "    ref  " + shown[k] + "\n";
            if (f) s += "    live " + FieldText(f->prog.fields[(std::size_t)f->map[k]]) + "\n";
        }
        if (!f) for (const auto& x : lay.forms) { if (!x.keyText.empty()) s += "    live form [" + x.keyText + "]: " + x.why + "\n"; for (const auto& fl : x.prog.fields) s += "    live? " + FieldText(fl) + "\n"; }
    }
    return s;
}

}  // namespace rtx::preads
