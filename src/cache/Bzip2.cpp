#include "Bzip2.h"

#include <array>

namespace rtx::cache {

namespace {

// MSB-first bit reader over the raw bzip2 stream.
struct BitReader {
    const std::uint8_t* d;
    std::size_t         len;
    std::size_t         pos = 0;
    std::uint32_t       buf = 0;
    int                 navail = 0;
    bool                err = false;

    int bit() {
        if (navail == 0) {
            if (pos >= len) { err = true; return 0; }
            buf = d[pos++]; navail = 8;
        }
        --navail;
        return (buf >> navail) & 1;
    }
    std::uint32_t bits(int k) {
        std::uint32_t v = 0;
        for (int i = 0; i < k; ++i) v = (v << 1) | (std::uint32_t)bit();
        return v;
    }
    std::uint64_t bits48() {
        std::uint64_t hi = bits(24), lo = bits(24);
        return (hi << 24) | lo;
    }
};

// Canonical-Huffman decode table (bzip2 limit/base/perm form).
struct Huff {
    int minLen = 0, maxLen = 0;
    int base[25]  = {0};
    int limit[25] = {0};
    int perm[258] = {0};
};

void build(Huff& h, const int* lengths, int alpha) {
    int mn = 32, mx = 0;
    for (int i = 0; i < alpha; ++i) {
        if (lengths[i] < mn) mn = lengths[i];
        if (lengths[i] > mx) mx = lengths[i];
    }
    h.minLen = mn; h.maxLen = mx;
    int pp = 0;
    for (int L = mn; L <= mx; ++L)
        for (int s = 0; s < alpha; ++s)
            if (lengths[s] == L) h.perm[pp++] = s;
    int pcnt[25] = {0};
    for (int i = 0; i < alpha; ++i) pcnt[lengths[i]]++;
    int code = 0, idx = 0;
    for (int L = mn; L <= mx; ++L) {
        h.base[L]  = code - idx;
        idx  += pcnt[L];
        code += pcnt[L];
        h.limit[L] = code - 1;
        code <<= 1;
    }
}

int decode(BitReader& br, const Huff& h) {
    int L = h.minLen;
    int code = (int)br.bits(L);
    while (L <= h.maxLen && code > h.limit[L]) {
        code = (code << 1) | br.bit();
        ++L;
    }
    if (L > h.maxLen) return -1;
    int p = code - h.base[L];
    if (p < 0 || p >= 258) return -1;
    return h.perm[p];
}

}  // namespace

std::vector<std::uint8_t> Bzip2Decompress(const std::uint8_t* data, std::size_t len,
                                          std::size_t orig_size) {
    std::vector<std::uint8_t> out;
    out.reserve(orig_size ? orig_size : len * 4);
    BitReader br{ data, len };

    while (true) {
        std::uint64_t magic = br.bits48();
        if (br.err) return {};
        if (magic == 0x177245385090ULL) break;             // end-of-stream block
        if (magic != 0x314159265359ULL) return {};          // not a block boundary
        br.bits(32);                                        // block CRC (unverified)
        if (br.bit() != 0) return {};                       // randomised blocks (deprecated)
        std::uint32_t origPtr = br.bits(24);

        // Used-symbol map: 16-bit group mask, then a 16-bit mask per used group.
        std::uint32_t used16 = br.bits(16);
        int symMap[256]; int nInUse = 0;
        for (int i = 0; i < 16; ++i) {
            if (used16 & (0x8000u >> i)) {
                std::uint32_t bm = br.bits(16);
                for (int j = 0; j < 16; ++j)
                    if (bm & (0x8000u >> j)) symMap[nInUse++] = i * 16 + j;
            }
        }
        if (nInUse == 0) return {};
        int alpha = nInUse + 2;                             // + RUNA/RUNB + EOB

        int nGroups = (int)br.bits(3);
        int nSel    = (int)br.bits(15);
        if (nGroups < 2 || nGroups > 6 || nSel < 1) return {};

        // Selectors: unary-coded MTF over the group indices.
        std::vector<std::uint8_t> selectors((std::size_t)nSel);
        std::uint8_t posg[6];
        for (int i = 0; i < nGroups; ++i) posg[i] = (std::uint8_t)i;
        for (int i = 0; i < nSel; ++i) {
            int j = 0;
            while (br.bit()) { if (++j >= nGroups) return {}; }
            std::uint8_t v = posg[j];
            for (int z = j; z > 0; --z) posg[z] = posg[z - 1];
            posg[0] = v;
            selectors[(std::size_t)i] = v;
        }

        // Huffman code lengths per group (delta-coded).
        Huff tables[6];
        for (int g = 0; g < nGroups; ++g) {
            int lens[258]; int c = (int)br.bits(5);
            for (int s = 0; s < alpha; ++s) {
                while (br.bit()) c += br.bit() ? -1 : 1;
                if (c < 1 || c > 23) return {};
                lens[s] = c;
            }
            build(tables[g], lens, alpha);
        }

        const int EOB = alpha - 1;
        std::uint8_t mtf[256];
        for (int i = 0; i < nInUse; ++i) mtf[i] = (std::uint8_t)symMap[i];

        // Decode the MTF/RLE2 symbol stream into the BWT buffer.
        std::vector<std::uint8_t> bwt;
        bwt.reserve(orig_size ? orig_size : (std::size_t)1 << 16);
        int gpos = 0, gidx = -1;
        const Huff* cur = nullptr;
        std::uint64_t run = 0, N = 0;
        while (true) {
            if (gpos == 0) {
                if (++gidx >= nSel) return {};
                cur = &tables[selectors[(std::size_t)gidx]];
                gpos = 50;
            }
            --gpos;
            int sym = decode(br, *cur);
            if (sym < 0 || br.err) return {};
            if (sym == 0 || sym == 1) {                     // RUNA / RUNB
                if (N == 0) N = 1;
                run += (sym == 0) ? N : 2 * N;
                N <<= 1;
                continue;
            }
            if (run) {                                      // flush a pending run of mtf[0]
                std::uint8_t b = mtf[0];
                for (std::uint64_t r = 0; r < run; ++r) bwt.push_back(b);
                run = 0; N = 0;
            }
            if (sym == EOB) break;
            int idx = sym - 1;                              // move-to-front
            std::uint8_t v = mtf[idx];
            for (int z = idx; z > 0; --z) mtf[z] = mtf[z - 1];
            mtf[0] = v;
            bwt.push_back(v);
        }

        // Inverse Burrows-Wheeler transform via the next-index chain.
        std::size_t n = bwt.size();
        if (n == 0 || origPtr >= n) return {};
        std::array<std::uint32_t, 256> cnt{}; cnt.fill(0);
        for (std::size_t i = 0; i < n; ++i) cnt[bwt[i]]++;
        std::array<std::uint32_t, 256> base{}; std::uint32_t acc = 0;
        for (int i = 0; i < 256; ++i) { base[i] = acc; acc += cnt[i]; }
        std::vector<std::uint32_t> nxt(n);
        std::array<std::uint32_t, 256> c2{}; c2.fill(0);
        for (std::size_t i = 0; i < n; ++i) {
            std::uint8_t b = bwt[i];
            nxt[base[b] + c2[b]++] = (std::uint32_t)i;
        }
        std::uint32_t p = nxt[origPtr];
        std::vector<std::uint8_t> dec(n);
        for (std::size_t i = 0; i < n; ++i) { dec[i] = bwt[p]; p = nxt[p]; }

        // Final RLE: a run of 4 equal bytes is followed by a count of extras.
        std::size_t i = 0;
        while (i < n) {
            std::uint8_t b = dec[i]; std::size_t rl = 1;
            while (i + rl < n && rl < 4 && dec[i + rl] == b) ++rl;
            for (std::size_t r = 0; r < rl; ++r) out.push_back(b);
            i += rl;
            if (rl == 4 && i < n) {
                std::uint8_t extra = dec[i++];
                for (int r = 0; r < extra; ++r) out.push_back(b);
            }
        }
    }
    return out;
}

}  // namespace rtx::cache
