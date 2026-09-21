#include "Lzma.h"

// LZMA1 decoder, written from the published format specification. The output size is always
// known here, so the whole output buffer doubles as the dictionary and no end marker is needed.

namespace rtx::cache {

namespace {

constexpr int           kNumBitModelTotalBits = 11;
constexpr std::uint16_t kProbInit = (1u << kNumBitModelTotalBits) / 2;
constexpr int           kNumMoveBits = 5;
constexpr std::uint32_t kTopValue = 1u << 24;

constexpr int kNumStates = 12;
constexpr int kNumPosBitsMax = 4;
constexpr int kNumLenToPosStates = 4;
constexpr int kNumAlignBits = 4;
constexpr int kStartPosModelIndex = 4;
constexpr int kEndPosModelIndex = 14;
constexpr int kNumFullDistances = 1 << (kEndPosModelIndex >> 1);
constexpr int kMatchMinLen = 2;

struct RangeDecoder {
    const std::uint8_t* p = nullptr;
    const std::uint8_t* end = nullptr;
    std::uint32_t range = 0xFFFFFFFFu;
    std::uint32_t code = 0;
    bool overrun = false;

    std::uint8_t next() {
        if (p < end) return *p++;
        overrun = true;
        return 0;
    }
    bool init() {
        if (next() != 0) return false;
        for (int i = 0; i < 4; ++i) code = (code << 8) | next();
        return !overrun && code != range;
    }
    void normalize() {
        if (range < kTopValue) { range <<= 8; code = (code << 8) | next(); }
    }
    std::uint32_t direct(int bits) {
        std::uint32_t res = 0;
        do {
            range >>= 1;
            code -= range;
            std::uint32_t t = 0u - (code >> 31);
            code += range & t;
            normalize();
            res = (res << 1) + t + 1;
        } while (--bits);
        return res;
    }
    unsigned bit(std::uint16_t& prob) {
        std::uint32_t bound = (range >> kNumBitModelTotalBits) * prob;
        unsigned symbol;
        if (code < bound) {
            prob = (std::uint16_t)(prob + (((1u << kNumBitModelTotalBits) - prob) >> kNumMoveBits));
            range = bound;
            symbol = 0;
        } else {
            prob = (std::uint16_t)(prob - (prob >> kNumMoveBits));
            code -= bound;
            range -= bound;
            symbol = 1;
        }
        normalize();
        return symbol;
    }
};

unsigned tree(RangeDecoder& rc, std::uint16_t* probs, int bits) {
    unsigned m = 1;
    for (int i = 0; i < bits; ++i) m = (m << 1) + rc.bit(probs[m]);
    return m - (1u << bits);
}
unsigned tree_reverse(RangeDecoder& rc, std::uint16_t* probs, int bits) {
    unsigned m = 1, sym = 0;
    for (int i = 0; i < bits; ++i) {
        unsigned b = rc.bit(probs[m]);
        m = (m << 1) + b;
        sym |= b << i;
    }
    return sym;
}

struct LenDecoder {
    std::uint16_t choice = kProbInit, choice2 = kProbInit;
    std::uint16_t low[1 << kNumPosBitsMax][8];
    std::uint16_t mid[1 << kNumPosBitsMax][8];
    std::uint16_t high[256];
    LenDecoder() {
        for (auto& r : low) for (auto& v : r) v = kProbInit;
        for (auto& r : mid) for (auto& v : r) v = kProbInit;
        for (auto& v : high) v = kProbInit;
    }
    unsigned decode(RangeDecoder& rc, unsigned pos_state) {
        if (rc.bit(choice) == 0) return tree(rc, low[pos_state], 3);
        if (rc.bit(choice2) == 0) return 8 + tree(rc, mid[pos_state], 3);
        return 16 + tree(rc, high, 8);
    }
};

void fill(std::uint16_t* p, std::size_t n) { for (std::size_t i = 0; i < n; ++i) p[i] = kProbInit; }

unsigned state_lit(unsigned s)      { return s < 4 ? 0 : (s < 10 ? s - 3 : s - 6); }
unsigned state_match(unsigned s)    { return s < 7 ? 7 : 10; }
unsigned state_rep(unsigned s)      { return s < 7 ? 8 : 11; }
unsigned state_shortrep(unsigned s) { return s < 7 ? 9 : 11; }

}  // namespace

std::vector<std::uint8_t> LzmaDecompress(const std::uint8_t* data, std::size_t size,
                                         std::size_t decoded_size) {
    if (!data || size < 5 + 5 || decoded_size > (std::size_t)256 * 1024 * 1024) return {};
    unsigned d = data[0];
    if (d >= 9 * 5 * 5) return {};
    const unsigned lc = d % 9; d /= 9;
    const unsigned lp = d % 5;
    const unsigned pb = d / 5;

    RangeDecoder rc;
    rc.p = data + 5;
    rc.end = data + size;
    if (!rc.init()) return {};

    std::vector<std::uint16_t> lit((std::size_t)0x300 << (lc + lp), kProbInit);
    std::uint16_t is_match[kNumStates << kNumPosBitsMax];
    std::uint16_t is_rep[kNumStates], is_rep_g0[kNumStates], is_rep_g1[kNumStates], is_rep_g2[kNumStates];
    std::uint16_t is_rep0_long[kNumStates << kNumPosBitsMax];
    std::uint16_t pos_slot[kNumLenToPosStates][64];
    std::uint16_t pos_decoders[1 + kNumFullDistances - kEndPosModelIndex];
    std::uint16_t align[1 << kNumAlignBits];
    fill(is_match, sizeof(is_match) / 2);
    fill(is_rep, kNumStates); fill(is_rep_g0, kNumStates); fill(is_rep_g1, kNumStates); fill(is_rep_g2, kNumStates);
    fill(is_rep0_long, sizeof(is_rep0_long) / 2);
    for (auto& r : pos_slot) fill(r, 64);
    fill(pos_decoders, sizeof(pos_decoders) / 2);
    fill(align, sizeof(align) / 2);
    LenDecoder len_dec, rep_len_dec;

    std::vector<std::uint8_t> out(decoded_size);
    std::size_t pos = 0;
    std::uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    const unsigned pb_mask = (1u << pb) - 1, lp_mask = (1u << lp) - 1;

    while (pos < decoded_size) {
        if (rc.overrun) return {};
        const unsigned pos_state = (unsigned)pos & pb_mask;
        if (rc.bit(is_match[(state << kNumPosBitsMax) + pos_state]) == 0) {
            const unsigned prev = pos ? out[pos - 1] : 0;
            std::uint16_t* probs = &lit[(std::size_t)0x300 * ((((unsigned)pos & lp_mask) << lc) + (prev >> (8 - lc)))];
            unsigned symbol = 1;
            if (state >= 7) {
                if ((std::size_t)rep0 + 1 > pos) return {};
                unsigned match_byte = out[pos - rep0 - 1];
                do {
                    unsigned match_bit = (match_byte >> 7) & 1;
                    match_byte <<= 1;
                    unsigned b = rc.bit(probs[((1 + match_bit) << 8) + symbol]);
                    symbol = (symbol << 1) | b;
                    if (match_bit != b) break;
                } while (symbol < 0x100);
            }
            while (symbol < 0x100) symbol = (symbol << 1) | rc.bit(probs[symbol]);
            out[pos++] = (std::uint8_t)symbol;
            state = state_lit(state);
            continue;
        }
        unsigned len;
        if (rc.bit(is_rep[state]) != 0) {
            if (pos == 0) return {};
            if (rc.bit(is_rep_g0[state]) == 0) {
                if (rc.bit(is_rep0_long[(state << kNumPosBitsMax) + pos_state]) == 0) {
                    if ((std::size_t)rep0 + 1 > pos) return {};
                    out[pos] = out[pos - rep0 - 1];
                    ++pos;
                    state = state_shortrep(state);
                    continue;
                }
            } else {
                std::uint32_t dist;
                if (rc.bit(is_rep_g1[state]) == 0) {
                    dist = rep1;
                } else {
                    if (rc.bit(is_rep_g2[state]) == 0) {
                        dist = rep2;
                    } else {
                        dist = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = rep_len_dec.decode(rc, pos_state);
            state = state_rep(state);
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = len_dec.decode(rc, pos_state);
            state = state_match(state);
            unsigned len_state = len < kNumLenToPosStates - 1 ? len : kNumLenToPosStates - 1;
            unsigned slot = tree(rc, pos_slot[len_state], 6);
            if (slot < kStartPosModelIndex) {
                rep0 = slot;
            } else {
                int direct_bits = (int)(slot >> 1) - 1;
                std::uint32_t dist = (2 | (slot & 1)) << direct_bits;
                if (slot < kEndPosModelIndex) {
                    dist += tree_reverse(rc, pos_decoders + dist - slot, direct_bits);
                } else {
                    dist += rc.direct(direct_bits - kNumAlignBits) << kNumAlignBits;
                    dist += tree_reverse(rc, align, kNumAlignBits);
                }
                rep0 = dist;
            }
            if (rep0 == 0xFFFFFFFFu) break;          // end marker
        }
        len += kMatchMinLen;
        if ((std::size_t)rep0 + 1 > pos) return {};
        if (len > decoded_size - pos) len = (unsigned)(decoded_size - pos);
        for (unsigned i = 0; i < len; ++i, ++pos) out[pos] = out[pos - rep0 - 1];
    }
    if (pos != decoded_size) return {};
    return out;
}

}  // namespace rtx::cache
