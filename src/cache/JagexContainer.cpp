#include "JagexContainer.h"

#include "Bzip2.h"
#include "vendor/zlib/zlib.h"

namespace rtx::cache {

namespace {

std::vector<std::uint8_t> InflateImpl(const std::uint8_t* in, std::size_t in_len,
                                      std::size_t initial_out_hint, int window_bits) {
    std::vector<std::uint8_t> out;
    // Clamp the hint: a bogus header size must not allocate gigabytes up front.
    constexpr std::size_t kMaxInitial = (std::size_t)256 * 1024 * 1024;
    if (initial_out_hint > kMaxInitial) initial_out_hint = kMaxInitial;
    out.resize(initial_out_hint > 0 ? initial_out_hint : (std::size_t)64 * 1024);

    z_stream s{};
    int rc0 = (window_bits == 0) ? inflateInit(&s)
                                 : inflateInit2(&s, window_bits);
    if (rc0 != Z_OK) return {};
    s.next_in  = const_cast<Bytef*>(in);
    s.avail_in = (uInt)in_len;

    std::size_t written = 0;
    constexpr std::size_t kHardCap = 32 * 1024 * 1024;
    int rc;
    do {
        if (written == out.size()) {
            if (out.size() >= kHardCap) { inflateEnd(&s); return {}; }
            out.resize(out.size() * 2);
        }
        s.next_out  = out.data() + written;
        s.avail_out = (uInt)(out.size() - written);
        const std::size_t before = written;
        rc = inflate(&s, Z_NO_FLUSH);
        written = s.total_out;
        if (rc == Z_STREAM_ERROR || rc == Z_DATA_ERROR || rc == Z_MEM_ERROR) {
            inflateEnd(&s); return {};
        }
        // Truncated input: Z_BUF_ERROR with no progress would loop forever.
        if (rc == Z_BUF_ERROR || (rc != Z_STREAM_END && written == before && s.avail_in == 0)) {
            inflateEnd(&s); return {};
        }
    } while (rc != Z_STREAM_END);

    inflateEnd(&s);
    out.resize(written);
    return out;
}

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t o) {
    return ((std::uint32_t)b[o] << 24) | ((std::uint32_t)b[o+1] << 16) |
           ((std::uint32_t)b[o+2] <<  8) |  (std::uint32_t)b[o+3];
}

}  // namespace

std::vector<std::uint8_t> Decompress(const std::vector<std::uint8_t>& raw) {
    if (raw.size() < 9) return {};
    // Legacy "ZL"-prefixed format: bytes 0..1 magic, 4..7 uncompressed
    // size, 8+ zlib stream. NXT writes its js5-* archives this way.
    if (raw[0] == 0x5A && raw[1] == 0x4C) {
        return InflateImpl(raw.data() + 8, raw.size() - 8, be32(raw, 4), 0);
    }
    return {};
}

std::vector<std::uint8_t> DecompressStandard(const std::vector<std::uint8_t>& raw) {
    if (raw.size() < 5) return {};
    std::uint8_t  type      = raw[0];
    std::uint32_t comp_size = be32(raw, 1);
    if (type == 0) {
        std::size_t end = 5 + (std::size_t)comp_size;
        if (end > raw.size()) end = raw.size();
        return std::vector<std::uint8_t>(raw.begin() + 5, raw.begin() + end);
    }
    if (raw.size() < 9) return {};
    std::uint32_t orig_size = be32(raw, 5);
    if (type == 1) {
        // bzip2. RS3 strips the 4-byte "BZh1" stream header, so the body begins
        // at the first block magic. Newer (Necromancy-era) sprites use this.
        return Bzip2Decompress(raw.data() + 9, raw.size() - 9, orig_size);
    }
    if (type == 2) {
        // window_bits 47 = auto-detect zlib OR gzip header.
        return InflateImpl(raw.data() + 9, raw.size() - 9, orig_size, 47);
    }
    return {};  // lzma unsupported
}

}  // namespace rtx::cache
