#include "Zip.h"

#include <cstring>
#include "../cache/vendor/zlib/zlib.h"

namespace rtx::launcher::zip {
namespace {

inline std::uint16_t rd16(const std::uint8_t* p) { return (std::uint16_t)(p[0] | (p[1] << 8)); }
inline std::uint32_t rd32(const std::uint8_t* p) {
    return (std::uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((std::uint32_t)p[3] << 24));
}

// Raw DEFLATE (no zlib header) -- ZIP stores raw streams, hence windowBits -15.
bool inflate_raw(const std::uint8_t* in, std::size_t in_len, std::size_t out_len, std::string& out) {
    out.assign(out_len, '\0');
    if (out_len == 0) return true;
    z_stream s; std::memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in   = (Bytef*)in;          s.avail_in  = (uInt)in_len;
    s.next_out  = (Bytef*)out.data();  s.avail_out = (uInt)out_len;
    int rc = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    return rc == Z_STREAM_END && s.total_out == out_len;
}

}  // namespace

bool ExtractFile(const std::uint8_t* data, std::size_t len,
                 const std::string& name, std::string& out) {
    out.clear();
    if (!data || len < 22) return false;

    // Locate the End Of Central Directory record (sig 0x06054b50), scanning back.
    const std::size_t kMaxComment = 65557;
    std::size_t start = (len > kMaxComment) ? len - kMaxComment : 0;
    long long eocd = -1;
    for (std::size_t i = len - 22 + 1; i-- > start; ) {
        if (rd32(data + i) == 0x06054b50) { eocd = (long long)i; break; }
    }
    if (eocd < 0 || (std::size_t)eocd + 22 > len) return false;

    const std::uint8_t* e = data + eocd;
    std::uint16_t total  = rd16(e + 10);
    std::uint32_t cdSize = rd32(e + 12);
    std::uint32_t cdOff  = rd32(e + 16);
    if ((std::size_t)cdOff + cdSize > len) return false;

    std::size_t p = cdOff;
    for (std::uint16_t n = 0; n < total; ++n) {
        if (p + 46 > len) return false;
        const std::uint8_t* c = data + p;
        if (rd32(c) != 0x02014b50) return false;          // central dir header
        std::uint16_t method     = rd16(c + 10);
        std::uint32_t compSize   = rd32(c + 20);
        std::uint32_t uncompSize = rd32(c + 24);
        std::uint16_t fnLen      = rd16(c + 28);
        std::uint16_t exLen      = rd16(c + 30);
        std::uint16_t cmLen      = rd16(c + 32);
        std::uint32_t loOff      = rd32(c + 42);
        if (p + 46 + fnLen > len) return false;
        std::string fn((const char*)(c + 46), fnLen);
        std::size_t next = p + 46 + fnLen + exLen + cmLen;

        if (fn == name) {
            if ((std::size_t)loOff + 30 > len) return false;
            const std::uint8_t* l = data + loOff;
            if (rd32(l) != 0x04034b50) return false;       // local file header
            std::uint16_t lfn = rd16(l + 26);
            std::uint16_t lex = rd16(l + 28);
            std::size_t dataOff = (std::size_t)loOff + 30 + lfn + lex;
            if (dataOff + compSize > len) return false;
            const std::uint8_t* cd = data + dataOff;
            if (method == 0) {                              // stored
                if (compSize != uncompSize) return false;
                out.assign((const char*)cd, compSize);
                return true;
            }
            if (method == 8) return inflate_raw(cd, compSize, uncompSize, out);  // deflate
            return false;                                   // unsupported method
        }
        p = next;
    }
    return false;
}

}  // namespace rtx::launcher::zip
