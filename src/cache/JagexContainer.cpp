#include "JagexContainer.h"

#include "Bzip2.h"
#include "Lzma.h"
#include "vendor/zlib/zlib.h"

namespace rtx::cache {

namespace {

std::vector<std::uint8_t> InflateImpl(const std::uint8_t* in, std::size_t in_len,
                                      std::size_t initial_out_hint, int window_bits) {
    std::vector<std::uint8_t> out;
    constexpr std::size_t kMaxInitial = (std::size_t)32 * 1024 * 1024;
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
        const uInt in_before = s.avail_in;
        rc = inflate(&s, Z_NO_FLUSH);
        written = s.total_out;
        // Anything but Z_OK / Z_STREAM_END is fatal here. Z_NEED_DICT in particular: cache
        // streams never use a preset dictionary, and inflate keeps returning it without
        // consuming input, so carrying on would spin forever.
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&s); return {};
        }
        // A call that wrote nothing and either had no input left or read none cannot finish.
        if (rc == Z_OK && written == before && (s.avail_in == 0 || s.avail_in == in_before)) {
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
    // "ZL" format: bytes 0..1 magic, 4..7 uncompressed size, 8+ zlib stream.
    if (raw[0] == 0x5A && raw[1] == 0x4C) {
        return InflateImpl(raw.data() + 8, raw.size() - 8, be32(raw, 4), 0);
    }
    // Anything else is the standard container (type byte 0..3). The model index is stored that
    // way, LZMA throughout, and every archive read comes through here.
    if (raw[0] <= 3) return DecompressStandard(raw);
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
        return Bzip2Decompress(raw.data() + 9, raw.size() - 9, orig_size);
    }
    if (type == 2) {
        return InflateImpl(raw.data() + 9, raw.size() - 9, orig_size, 47);
    }
    if (type == 3) {
        return LzmaDecompress(raw.data() + 9, raw.size() - 9, orig_size);
    }
    return {};
}

}  // namespace rtx::cache
