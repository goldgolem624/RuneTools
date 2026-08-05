#include "Sprite.h"

#include "JagexContainer.h"
#include "SqliteIndexFile.h"
#include "vendor/zlib/zlib.h"

#include <cmath>
#include <cstring>

namespace rtx::cache {

namespace {

struct Frame {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

class Reader {
public:
    Reader(const std::uint8_t* d, std::size_t n) : d_(d), n_(n) {}
    bool ok(std::size_t need) const { return pos_ + need <= n_; }
    void seek_from_end(long long from_end) {
        long long t = (long long)n_ + from_end;
        pos_ = (t < 0) ? 0 : (std::size_t)t;
    }
    std::uint8_t  u8()   { return d_[pos_++]; }
    std::uint16_t u16be(){ std::uint16_t v = ((std::uint16_t)d_[pos_] << 8) | d_[pos_+1]; pos_ += 2; return v; }
    std::size_t   pos() const { return pos_; }
    void          set_pos(std::size_t p) { pos_ = p; }
    const std::uint8_t* at(std::size_t p) const { return d_ + p; }
private:
    const std::uint8_t* d_;
    std::size_t n_;
    std::size_t pos_ = 0;
};

bool looks_like_png(const std::vector<std::uint8_t>& b) {
    return b.size() >= 8 && b[0]==0x89 && b[1]==0x50 && b[2]==0x4E && b[3]==0x47;
}

// Decode the legacy footer-based sprite formats (0 = paletted, 1 = raw RGB)
// into a single RGBA frame. Mirrors the SpriteType decoder.
bool decode_frame(const std::vector<std::uint8_t>& bytes, Frame& out) {
    if (bytes.size() < 2) return false;
    Reader br(bytes.data(), bytes.size());

    br.seek_from_end(-2);
    std::uint16_t footer = br.u16be();
    int format = (footer >> 15) & 1;
    int count  = footer & 0x7FFF;

    if (format == 0) {
        if (count <= 0) return false;
        const long long header_len = 7 + (long long)count * 8;
        if ((long long)bytes.size() < header_len + 2) return false;

        br.seek_from_end(-header_len);
        (void)br.u16be();                  // big_width (reference only)
        (void)br.u16be();                  // big_height
        std::uint8_t palette_count = br.u8();

        std::vector<int> widths(count), heights(count);
        for (int i = 0; i < count; ++i) br.u16be();   // min_xs
        for (int i = 0; i < count; ++i) br.u16be();   // min_ys
        for (int i = 0; i < count; ++i) widths[i]  = br.u16be();
        for (int i = 0; i < count; ++i) heights[i] = br.u16be();

        long long pal_start = -header_len - (long long)palette_count * 3;
        if ((long long)bytes.size() + pal_start < 0) return false;
        br.seek_from_end(pal_start);
        if (!br.ok((std::size_t)palette_count * 3)) return false;
        std::vector<std::uint8_t> palette((std::size_t)palette_count * 3);
        for (auto& p : palette) p = br.u8();

        // Only the first frame is needed for an icon.
        br.set_pos(0);
        int w = widths[0], h = heights[0];
        std::size_t pixels = (std::size_t)w * h;
        if (pixels == 0 || !br.ok(1)) return false;
        std::uint8_t flags = br.u8();
        bool transposed = (flags & 0x1) != 0;
        bool has_alpha  = (flags & 0x2) != 0;
        if (!br.ok(pixels)) return false;
        const std::uint8_t* base = br.at(br.pos()); br.set_pos(br.pos() + pixels);
        const std::uint8_t* alpha = nullptr;
        if (has_alpha) {
            if (!br.ok(pixels)) return false;
            alpha = br.at(br.pos()); br.set_pos(br.pos() + pixels);
        }

        out.width = w; out.height = h;
        out.rgba.assign(pixels * 4, 0);
        for (std::size_t i = 0; i < pixels; ++i) {
            int x = transposed ? (int)(i / h) : (int)(i % w);
            int y = transposed ? (int)(i % h) : (int)(i / w);
            std::uint8_t* px = &out.rgba[((std::size_t)y * w + x) * 4];
            std::uint8_t idx = base[i];
            if (idx == 0) { px[0]=px[1]=px[2]=px[3]=0; }
            else {
                int p = (idx - 1) * 3;
                if (p + 2 < (int)palette.size()) {
                    px[0]=palette[p]; px[1]=palette[p+1]; px[2]=palette[p+2];
                }
                px[3] = alpha ? alpha[i] : 255;
            }
        }
        return true;
    }

    if (format == 1) {
        br.set_pos(0);
        if (!br.ok(1 + 1 + 4)) return false;
        if (br.u8() != 0) return false;
        std::uint8_t flags = br.u8();
        bool has_alpha = (flags & 0x1) != 0;
        int w = br.u16be(), h = br.u16be();
        std::size_t pixels = (std::size_t)w * h;
        if (pixels == 0 || !br.ok(pixels * 3)) return false;
        out.width = w; out.height = h;
        out.rgba.assign(pixels * 4, 255);
        for (std::size_t i = 0; i < pixels; ++i) {
            out.rgba[i*4+0] = br.u8();
            out.rgba[i*4+1] = br.u8();
            out.rgba[i*4+2] = br.u8();
        }
        if (has_alpha) {
            if (!br.ok(pixels)) return false;
            for (std::size_t i = 0; i < pixels; ++i) out.rgba[i*4+3] = br.u8();
        }
        return true;
    }
    return false;
}

void put_be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back((x>>24)&0xFF); v.push_back((x>>16)&0xFF);
    v.push_back((x>>8)&0xFF);  v.push_back(x&0xFF);
}

void put_chunk(std::vector<std::uint8_t>& png, const char tag[4],
               const std::vector<std::uint8_t>& data) {
    put_be32(png, (std::uint32_t)data.size());
    std::size_t crc_begin = png.size();
    png.insert(png.end(), tag, tag + 4);
    png.insert(png.end(), data.begin(), data.end());
    std::uint32_t crc = (std::uint32_t)crc32(0L, png.data() + crc_begin,
                                             (uInt)(png.size() - crc_begin));
    put_be32(png, crc);
}

// ---- Minimal deflate (fixed Huffman + LZ77) --------------------------------------------------
// The vendored zlib carries only the INFLATE half, so there is no compressor to call. This is
// enough of one for PNG: a single fixed-Huffman block (RFC 1951 3.2.6) over an LZ77 pass with a
// hash-chain matcher. Every decoder handles it - it is ordinary deflate, just without dynamic
// code tables, which costs a few percent and saves the whole Huffman-tree builder.
//
// Bit order is the easy thing to get wrong: deflate packs bits LSB-first, but a Huffman code is
// emitted MSB-first. Both forms are separate helpers below for that reason. The algorithm was
// prototyped and round-tripped against a reference inflate before being written here.
class BitWriter {
public:
    void bits(std::uint32_t v, int n) {          // LSB-first: block header, extra bits
        for (int i = 0; i < n; ++i) put((v >> i) & 1);
    }
    void huff(std::uint32_t code, int n) {       // MSB-first: Huffman symbols
        for (int i = n - 1; i >= 0; --i) put((code >> i) & 1);
    }
    std::vector<std::uint8_t> finish() {
        if (nbits_) out_.push_back(acc_);
        nbits_ = 0; acc_ = 0;
        return std::move(out_);
    }
private:
    void put(std::uint32_t b) {
        acc_ |= (std::uint8_t)(b << nbits_);
        if (++nbits_ == 8) { out_.push_back(acc_); acc_ = 0; nbits_ = 0; }
    }
    std::vector<std::uint8_t> out_;
    std::uint8_t acc_ = 0;
    int nbits_ = 0;
};

const std::uint16_t kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,
                                    115,131,163,195,227,258};
const std::uint8_t  kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const std::uint16_t kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
                                     1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const std::uint8_t  kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

void emit_lit(BitWriter& w, int sym) {           // fixed literal/length code table
    if (sym <= 143)      w.huff(0x30 + sym, 8);
    else if (sym <= 255) w.huff(0x190 + sym - 144, 9);
    else if (sym <= 279) w.huff(sym - 256, 7);
    else                 w.huff(0xC0 + sym - 280, 8);
}

std::vector<std::uint8_t> deflate_fixed(const std::vector<std::uint8_t>& d) {
    constexpr int kWindow = 32768, kWMask = kWindow - 1;
    constexpr int kMinMatch = 3, kMaxMatch = 258, kMaxChain = 32;
    constexpr int kHashBits = 15, kHashSize = 1 << kHashBits;
    BitWriter w;
    w.bits(1, 1);                                 // BFINAL
    w.bits(1, 2);                                 // BTYPE = 01 (fixed Huffman)
    const std::ptrdiff_t n = (std::ptrdiff_t)d.size();
    // head/prev are WINDOW-sized, not input-sized: a per-byte chain would be tens of MB on a
    // 2048x2048 image, this is 160KB whatever the input.
    std::vector<std::int32_t> head((std::size_t)kHashSize, -1), prev((std::size_t)kWindow, -1);
    auto h3 = [&](std::ptrdiff_t p) {
        return (int)(((std::uint32_t)d[p] << 10) ^ ((std::uint32_t)d[p + 1] << 5) ^ d[p + 2]) & (kHashSize - 1);
    };
    auto insert = [&](std::ptrdiff_t p) {
        int hh = h3(p);
        prev[(std::size_t)(p & kWMask)] = head[(std::size_t)hh];
        head[(std::size_t)hh] = (std::int32_t)p;
    };
    std::ptrdiff_t i = 0;
    while (i < n) {
        std::ptrdiff_t bestLen = 0, bestDist = 0;
        if (i + kMinMatch <= n) {
            std::ptrdiff_t lim = (n - i < kMaxMatch) ? (n - i) : kMaxMatch;
            std::int32_t j = head[(std::size_t)h3(i)];
            for (int chain = 0; j >= 0 && (i - j) <= kWindow && chain < kMaxChain; ++chain) {
                if (d[(std::size_t)j] == d[(std::size_t)i] &&
                    d[(std::size_t)j + 1] == d[(std::size_t)i + 1] &&
                    d[(std::size_t)j + 2] == d[(std::size_t)i + 2]) {
                    std::ptrdiff_t ln = 3;
                    while (ln < lim && d[(std::size_t)(j + ln)] == d[(std::size_t)(i + ln)]) ++ln;
                    if (ln > bestLen) { bestLen = ln; bestDist = i - j; if (ln >= lim) break; }
                }
                std::int32_t nj = prev[(std::size_t)(j & kWMask)];
                if (nj >= j) break;               // slot reused by a newer position: chain ends
                j = nj;
            }
            insert(i);
        }
        if (bestLen >= kMinMatch) {
            int li = 28;
            while (li > 0 && bestLen < (std::ptrdiff_t)kLenBase[li]) --li;
            emit_lit(w, 257 + li);
            if (kLenExtra[li]) w.bits((std::uint32_t)(bestLen - kLenBase[li]), kLenExtra[li]);
            int di = 29;
            while (di > 0 && bestDist < (std::ptrdiff_t)kDistBase[di]) --di;
            w.huff((std::uint32_t)di, 5);
            if (kDistExtra[di]) w.bits((std::uint32_t)(bestDist - kDistBase[di]), kDistExtra[di]);
            for (std::ptrdiff_t k = 1; k < bestLen; ++k)
                if (i + k + kMinMatch <= n) insert(i + k);
            i += bestLen;
        } else {
            emit_lit(w, d[(std::size_t)i]);
            ++i;
        }
    }
    emit_lit(w, 256);                             // end of block
    return w.finish();
}

std::vector<std::uint8_t> zlib_deflate(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);         // zlib header (deflate, 32K window)
    auto body = deflate_fixed(raw);
    z.insert(z.end(), body.begin(), body.end());
    put_be32(z, (std::uint32_t)adler32(1L, raw.data(), (uInt)raw.size()));
    return z;
}

// Uncompressed ("stored") zlib stream. Still used for ICONS: they are capped at 64px, so the
// deflate above would save a few hundred bytes per sprite for a per-icon CPU cost. The map
// render is the case where compression matters and it uses zlib_deflate.
std::vector<std::uint8_t> zlib_store(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);          // zlib header
    std::size_t off = 0;
    while (off < raw.size() || raw.empty()) {
        std::size_t block = raw.size() - off;
        if (block > 0xFFFF) block = 0xFFFF;
        bool final = (off + block >= raw.size());
        z.push_back(final ? 1 : 0);                // BFINAL, BTYPE=00 stored
        z.push_back(block & 0xFF); z.push_back((block >> 8) & 0xFF);
        std::uint16_t nlen = (std::uint16_t)~block;
        z.push_back(nlen & 0xFF); z.push_back((nlen >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + block);
        off += block;
        if (raw.empty()) break;
    }
    std::uint32_t a = (std::uint32_t)adler32(1L, raw.data(), (uInt)raw.size());
    put_be32(z, a);
    return z;
}

std::vector<std::uint8_t> encode_png(const Frame& f) {
    if (f.width <= 0 || f.height <= 0) return {};
    // Scanlines with a leading filter byte (0 = none) per row. Sprites are flat colour with big
    // transparent margins, which LZ77 matches directly - MEASURED, filtering those rows makes the
    // result 1.5-2x LARGER because it breaks up exactly the runs deflate wants. (The map render
    // is the opposite case - smooth gradients - and does filter; see EncodePngRgb.)
    std::vector<std::uint8_t> raw;
    raw.reserve((std::size_t)f.height * (1 + (std::size_t)f.width * 4));
    for (int y = 0; y < f.height; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = &f.rgba[(std::size_t)y * f.width * 4];
        raw.insert(raw.end(), row, row + (std::size_t)f.width * 4);
    }

    std::vector<std::uint8_t> png = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    std::vector<std::uint8_t> ihdr;
    put_be32(ihdr, (std::uint32_t)f.width);
    put_be32(ihdr, (std::uint32_t)f.height);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // colour type RGBA
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);  // deflate/none/none
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", zlib_deflate(raw));
    put_chunk(png, "IEND", {});
    return png;
}

}  // namespace

// RGBA in, PNG (colour type 2, no alpha) out, deflate-compressed.
//
// Three separate savings, in order of size: the UP filter (each byte stored as its difference
// from the pixel above), then deflate, then dropping the alpha channel the map never uses.
// Filtering is what does most of the work here - the render is smooth blended terrain, so
// vertical neighbours differ by very little and the differences compress far better than the
// colours do. Measured on representative terrain: ~4.3x smaller than the raw RGBA this replaces,
// where deflate alone (unfiltered) gave only ~1.5x.
std::vector<std::uint8_t> EncodePngRgb(const std::uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return {};
    const std::size_t stride = (std::size_t)w * 3;
    std::vector<std::uint8_t> raw;
    raw.reserve((std::size_t)h * (1 + stride));
    std::vector<std::uint8_t> cur(stride), prevRow(stride, 0);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = rgba + (std::size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            cur[(std::size_t)x * 3 + 0] = row[(std::size_t)x * 4 + 0];
            cur[(std::size_t)x * 3 + 1] = row[(std::size_t)x * 4 + 1];
            cur[(std::size_t)x * 3 + 2] = row[(std::size_t)x * 4 + 2];
        }
        raw.push_back(2);                                  // filter 2 (Up)
        for (std::size_t i = 0; i < stride; ++i)
            raw.push_back((std::uint8_t)(cur[i] - prevRow[i]));
        prevRow.swap(cur);
    }
    std::vector<std::uint8_t> png = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    std::vector<std::uint8_t> ihdr;
    put_be32(ihdr, (std::uint32_t)w);
    put_be32(ihdr, (std::uint32_t)h);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(2);   // colour type RGB (no alpha)
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);  // deflate/none/none
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", zlib_deflate(raw));
    put_chunk(png, "IEND", {});
    return png;
}

// Panel icons render in small boxes but interface sprites can be huge; cap the longest
// side to keep the uncompressed-PNG data URL small. AREA-AVERAGE (box) filter: each
// destination pixel integrates the exact source rectangle it covers (fractional edges
// included), with colours alpha-weighted so transparent texels don't darken edges.
// The earlier nearest-sample here visibly jaggied detailed sprites (the 80px Prifddinas
// clan crests were the reported case) before Ultralight blurred them further.
void downscale_to(Frame& f, int max_side) {
    if (f.width <= max_side && f.height <= max_side) return;
    double s = (double)max_side / (f.width > f.height ? f.width : f.height);
    int nw = (int)(f.width * s + 0.5), nh = (int)(f.height * s + 0.5);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    std::vector<std::uint8_t> out((std::size_t)nw * nh * 4);
    const double xr = (double)f.width / nw, yr = (double)f.height / nh;
    for (int y = 0; y < nh; ++y) {
        const double sy0 = y * yr, sy1 = (y + 1) * yr;
        const int iy0 = (int)sy0;
        int iy1 = (int)std::ceil(sy1); if (iy1 > f.height) iy1 = f.height;
        for (int x = 0; x < nw; ++x) {
            const double sx0 = x * xr, sx1 = (x + 1) * xr;
            const int ix0 = (int)sx0;
            int ix1 = (int)std::ceil(sx1); if (ix1 > f.width) ix1 = f.width;
            double r = 0, g = 0, b = 0, a = 0, area = 0;
            for (int yy = iy0; yy < iy1; ++yy) {
                const double cy0 = yy < sy0 ? sy0 : yy, cy1 = (yy + 1) > sy1 ? sy1 : (yy + 1);
                const double hcov = cy1 - cy0;
                for (int xx = ix0; xx < ix1; ++xx) {
                    const double cx0 = xx < sx0 ? sx0 : xx, cx1 = (xx + 1) > sx1 ? sx1 : (xx + 1);
                    const double cov = (cx1 - cx0) * hcov;
                    const std::uint8_t* p = &f.rgba[((std::size_t)yy * f.width + xx) * 4];
                    const double pa = p[3] * cov;
                    r += p[0] * pa; g += p[1] * pa; b += p[2] * pa;
                    a += pa; area += cov;
                }
            }
            std::uint8_t* q = &out[((std::size_t)y * nw + x) * 4];
            if (a > 0.0) {
                q[0] = (std::uint8_t)(r / a + 0.5);
                q[1] = (std::uint8_t)(g / a + 0.5);
                q[2] = (std::uint8_t)(b / a + 0.5);
            } else {
                q[0] = q[1] = q[2] = 0;
            }
            q[3] = (std::uint8_t)(a / (area > 0.0 ? area : 1.0) + 0.5);
        }
    }
    f.width = nw; f.height = nh; f.rgba = std::move(out);
}

std::vector<std::uint8_t> SpriteAsPng(SqliteIndexFile& sprites, int sprite_id) {
    return SpriteAsPngScaled(sprites, sprite_id, 64);
}

std::vector<std::uint8_t> SpriteAsPngScaled(SqliteIndexFile& sprites, int sprite_id, int max_side) {
    auto raw = sprites.ReadRawArchive(sprite_id);
    if (raw.empty()) return {};
    auto bytes = DecompressStandard(raw);
    if (bytes.empty()) return {};
    if (looks_like_png(bytes)) return bytes;   // already a PNG, pass through
    Frame f;
    if (!decode_frame(bytes, f)) return {};
    if (max_side < 8) max_side = 8;
    downscale_to(f, max_side);
    return encode_png(f);
}

// Raw RGBA of a sprite's first frame, for compositing into the map render (no PNG round-trip).
// Returns empty (w=h=0) on failure or if the cache stores it as a PNG (map-scene icons are raw frames).
std::vector<std::uint8_t> SpriteRawRgba(SqliteIndexFile& sprites, int sprite_id, int& w, int& h) {
    w = h = 0;
    auto raw = sprites.ReadRawArchive(sprite_id);
    if (raw.empty()) return {};
    auto bytes = DecompressStandard(raw);
    if (bytes.empty()) return {};
    if (looks_like_png(bytes)) return {};
    Frame f;
    if (!decode_frame(bytes, f)) return {};
    w = f.width; h = f.height;
    return std::move(f.rgba);
}

}  // namespace rtx::cache
