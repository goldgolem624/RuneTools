#include "PngDecode.h"

#include "../cache/vendor/zlib/zlib.h"

#include <cstdlib>
#include <cstring>

namespace rtx::launcher::png {

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return ((std::uint32_t)p[0] << 24) | ((std::uint32_t)p[1] << 16) | ((std::uint32_t)p[2] << 8) | p[3];
}

std::uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return (std::uint8_t)a;
    if (pb <= pc) return (std::uint8_t)b;
    return (std::uint8_t)c;
}

bool inflate_all(const std::vector<std::uint8_t>& in, std::size_t expect, std::vector<std::uint8_t>& out) {
    out.assign(expect, 0);
    z_stream s; std::memset(&s, 0, sizeof(s));
    if (inflateInit(&s) != Z_OK) return false;
    s.next_in = const_cast<Bytef*>(in.data()); s.avail_in = (uInt)in.size();
    s.next_out = out.data(); s.avail_out = (uInt)out.size();
    int rc = inflate(&s, Z_FINISH);
    bool ok = (rc == Z_STREAM_END || rc == Z_OK || rc == Z_BUF_ERROR) && s.avail_out == 0;
    inflateEnd(&s);
    return ok;
}

}  // namespace

bool Decode(const std::uint8_t* d, std::size_t n, Image& out) {
    out = Image{};
    static const std::uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n < 8 + 25 || std::memcmp(d, sig, 8) != 0) return false;

    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    std::vector<std::uint8_t> idat, plte, trns;
    bool haveIhdr = false;
    std::size_t pos = 8;
    while (pos + 12 <= n) {
        std::uint32_t len = be32(d + pos);
        const std::uint8_t* type = d + pos + 4;
        if (len > n - pos - 12) return false;
        const std::uint8_t* body = d + pos + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return false;
            w = (int)be32(body); h = (int)be32(body + 4);
            depth = body[8]; ctype = body[9]; interlace = body[12];
            haveIhdr = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            plte.assign(body, body + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(body, body + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }
    if (!haveIhdr || w <= 0 || h <= 0 || w > 4096 || h > 4096 || interlace != 0 || idat.empty()) return false;

    // channels per pixel and bits per pixel for the supported layouts
    int channels = 0;
    switch (ctype) {
        case 0: channels = 1; break;   // gray
        case 2: channels = 3; break;   // rgb
        case 3: channels = 1; break;   // palette
        case 4: channels = 2; break;   // gray + alpha
        case 6: channels = 4; break;   // rgba
        default: return false;
    }
    if (ctype == 3) { if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return false; if (plte.empty()) return false; }
    else if (depth != 8) return false;   // 16-bit and sub-byte gray are not in the pack
    const int bpp = channels * depth;                       // bits per pixel
    const std::size_t stride = ((std::size_t)w * bpp + 7) / 8;
    const int fbytes = (bpp + 7) / 8;                        // filter unit (bytes)
    std::vector<std::uint8_t> raw;
    if (!inflate_all(idat, (stride + 1) * (std::size_t)h, raw)) return false;

    // unfilter in place (filters 0-4), row by row
    std::vector<std::uint8_t> prev(stride, 0), cur(stride);
    out.w = w; out.h = h; out.rgba.assign((std::size_t)w * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = raw.data() + (std::size_t)y * (stride + 1);
        const int f = row[0];
        const std::uint8_t* src = row + 1;
        for (std::size_t i = 0; i < stride; ++i) {
            int a = i >= (std::size_t)fbytes ? cur[i - fbytes] : 0;
            int b = prev[i];
            int c = i >= (std::size_t)fbytes ? prev[i - fbytes] : 0;
            int x = src[i];
            switch (f) {
                case 0: break;
                case 1: x += a; break;
                case 2: x += b; break;
                case 3: x += (a + b) / 2; break;
                case 4: x += paeth(a, b, c); break;
                default: return false;
            }
            cur[i] = (std::uint8_t)x;
        }
        std::uint8_t* dst = out.rgba.data() + (std::size_t)y * w * 4;
        for (int x = 0; x < w; ++x, dst += 4) {
            if (ctype == 3) {
                int idx;
                if (depth == 8) idx = cur[x];
                else {
                    int bitpos = x * depth;
                    idx = (cur[bitpos / 8] >> (8 - depth - (bitpos % 8))) & ((1 << depth) - 1);
                }
                if ((std::size_t)idx * 3 + 2 >= plte.size()) return false;
                dst[0] = plte[idx * 3]; dst[1] = plte[idx * 3 + 1]; dst[2] = plte[idx * 3 + 2];
                dst[3] = (std::size_t)idx < trns.size() ? trns[idx] : 255;
            } else if (ctype == 0) {
                dst[0] = dst[1] = dst[2] = cur[x]; dst[3] = 255;
                if (trns.size() >= 2 && cur[x] == trns[1]) dst[3] = 0;
            } else if (ctype == 4) {
                dst[0] = dst[1] = dst[2] = cur[x * 2]; dst[3] = cur[x * 2 + 1];
            } else if (ctype == 2) {
                dst[0] = cur[x * 3]; dst[1] = cur[x * 3 + 1]; dst[2] = cur[x * 3 + 2]; dst[3] = 255;
                if (trns.size() >= 6 && dst[0] == trns[1] && dst[1] == trns[3] && dst[2] == trns[5]) dst[3] = 0;
            } else {
                std::memcpy(dst, cur.data() + (std::size_t)x * 4, 4);
            }
        }
        prev.swap(cur);
    }
    return true;
}

}  // namespace rtx::launcher::png
