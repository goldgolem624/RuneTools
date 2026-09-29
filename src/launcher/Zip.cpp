#include "Zip.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include "../cache/vendor/zlib/zlib.h"

namespace rtx::launcher::zip {
namespace {

inline std::uint16_t rd16(const std::uint8_t* p) { return (std::uint16_t)(p[0] | (p[1] << 8)); }
inline std::uint32_t rd32(const std::uint8_t* p) {
    return (std::uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((std::uint32_t)p[3] << 24));
}

struct Entry {
    std::string   name;
    std::uint16_t method;
    std::uint32_t crc, compSize, uncompSize;
    std::size_t   start, dataOff;
};

// The server checks bundles with these same rules before signing, so what is installed is exactly what was
// reviewed: anything two zip readers could list differently (split or zip64 counts, slack around the central
// directory, local headers that disagree with it, shared bytes, colliding names) is refused.
bool ReadDirectory(const std::uint8_t* data, std::size_t len, std::vector<Entry>& entries) {
    entries.clear();
    if (!data || len < 22) return false;

    const std::size_t kMaxComment = 65557;
    std::size_t start = (len > kMaxComment) ? len - kMaxComment : 0;
    long long found = -1;
    for (std::size_t i = len - 22 + 1; i-- > start; ) {
        if (rd32(data + i) == 0x06054b50) { found = (long long)i; break; }
    }
    if (found < 0) return false;
    const std::size_t eocd = (std::size_t)found;
    const std::uint8_t* e = data + eocd;
    if (eocd + 22 + rd16(e + 20) != len) return false;             // comment must run exactly to the end
    if (rd16(e + 4) != 0 || rd16(e + 6) != 0 || rd16(e + 8) != rd16(e + 10)) return false;
    for (std::size_t k = 4; k <= 20 && k <= eocd; ++k)
        if (rd32(data + eocd - k) == 0x07064b50) return false;     // zip64 locator
    std::uint16_t total  = rd16(e + 10);
    std::uint32_t cdSize = rd32(e + 12);
    std::uint32_t cdOff  = rd32(e + 16);
    if ((std::size_t)cdOff + cdSize != eocd) return false;

    std::size_t p = cdOff;
    for (std::uint16_t n = 0; n < total; ++n) {
        if (p + 46 > eocd) return false;
        const std::uint8_t* c = data + p;
        if (rd32(c) != 0x02014b50) return false;          // central dir header
        std::uint16_t flags      = rd16(c + 8);
        std::uint16_t method     = rd16(c + 10);
        std::uint32_t crc        = rd32(c + 16);
        std::uint32_t compSize   = rd32(c + 20);
        std::uint32_t uncompSize = rd32(c + 24);
        std::uint16_t fnLen      = rd16(c + 28);
        std::uint16_t exLen      = rd16(c + 30);
        std::uint16_t cmLen      = rd16(c + 32);
        std::uint32_t loOff      = rd32(c + 42);
        std::size_t next = p + 46 + fnLen + exLen + cmLen;
        if (next > eocd || fnLen == 0) return false;
        for (std::uint16_t i = 0; i < fnLen; ++i) if (c[46 + i] < 0x20) return false;
        // Windows opens "a/b" for "a/./b", "a//b", "a./b" or "a /b" too, and a long name through its 8.3 short
        // name (NAME~1.LUA), so each name must have only one spelling on disk. A folder may end in '/' or '\'.
        const char* nm = (const char*)(c + 46);
        std::size_t nmLen = (nm[fnLen - 1] == '/' || nm[fnLen - 1] == '\\') ? fnLen - 1u : fnLen;
        for (std::size_t i = 0, seg = 0; i <= nmLen; ++i) {
            if (i == nmLen || nm[i] == '/' || nm[i] == '\\') {
                if (i == seg || nm[i - 1] == '.' || nm[i - 1] == ' ') return false;
                seg = i + 1;
            } else if (nm[i] == '~' && i + 1 < nmLen && nm[i + 1] >= '0' && nm[i + 1] <= '9') {
                return false;
            }
        }
        if (rd16(c + 34) != 0) return false;
        if (method != 0 && method != 8) return false;              // stored or deflate only
        if (method == 0 && compSize != uncompSize) return false;
        if (compSize == 0 && uncompSize != 0) return false;

        if ((std::size_t)loOff + 30 > cdOff) return false;
        const std::uint8_t* l = data + loOff;
        if (rd32(l) != 0x04034b50) return false;       // local file header
        std::uint16_t lflags = rd16(l + 6);
        std::uint16_t lfn    = rd16(l + 26);
        std::uint16_t lex    = rd16(l + 28);
        std::size_t dataOff = (std::size_t)loOff + 30 + lfn + lex;
        if (dataOff + compSize > cdOff) return false;
        if ((flags | lflags) & 0x41) return false;                 // encrypted
        if (((flags ^ lflags) & 0x08) || rd16(l + 8) != method) return false;
        if (lfn != fnLen || std::memcmp(l + 30, c + 46, fnLen) != 0) return false;
        // With a data descriptor the local fields may be left zero; otherwise they must repeat the directory's.
        const std::uint32_t loc[3] = {rd32(l + 14), rd32(l + 18), rd32(l + 22)};
        const std::uint32_t cen[3] = {crc, compSize, uncompSize};
        for (int i = 0; i < 3; ++i)
            if (loc[i] != cen[i] && !((flags & 0x08) && loc[i] == 0)) return false;

        entries.push_back({std::string((const char*)(c + 46), fnLen), method, crc, compSize, uncompSize,
                           (std::size_t)loOff, dataOff});
        p = next;
    }
    if (p != eocd) return false;

    std::vector<std::pair<std::size_t, std::size_t>> spans;
    std::vector<std::string> folded;
    for (auto& en : entries) {
        spans.emplace_back(en.start, en.dataOff + en.compSize);
        std::string f = en.name;
        for (auto& ch : f) {
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            else if (ch == '\\') ch = '/';
        }
        folded.push_back(std::move(f));
    }
    std::sort(spans.begin(), spans.end());
    for (std::size_t i = 1; i < spans.size(); ++i)
        if (spans[i - 1].second > spans[i].first) return false;
    std::sort(folded.begin(), folded.end());
    if (std::adjacent_find(folded.begin(), folded.end()) != folded.end()) return false;
    return true;
}

// Raw DEFLATE (no zlib header), hence windowBits -15.
bool inflate_raw(const std::uint8_t* in, std::size_t in_len, std::size_t out_len, std::string& out) {
    // The size is the archive's own claim, read before anything is inflated: nothing we open is near this,
    // and a header that says 4 GB must not get 4 GB allocated for it.
    if (out_len > 64u * 1024 * 1024) return false;
    // One spare byte, so a stream longer than its claim (a claim of 0 included) fails on length.
    out.assign(out_len + 1, '\0');
    z_stream s; std::memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in   = (Bytef*)in;          s.avail_in  = (uInt)in_len;
    s.next_out  = (Bytef*)out.data();  s.avail_out = (uInt)(out_len + 1);
    int rc = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    out.resize(out_len);
    return rc == Z_STREAM_END && s.total_out == out_len && s.avail_in == 0;   // no bytes after the stream
}

}  // namespace

bool ExtractFile(const std::uint8_t* data, std::size_t len,
                 const std::string& name, std::string& out) {
    out.clear();
    std::vector<Entry> entries;
    if (!ReadDirectory(data, len, entries)) return false;
    for (auto& en : entries) {
        if (en.name != name) continue;
        const std::uint8_t* cd = data + en.dataOff;
        bool ok = true;
        if (en.compSize == 0) {}                                    // empty, whatever the method
        else if (en.method == 0) out.assign((const char*)cd, en.compSize);   // stored
        else ok = inflate_raw(cd, en.compSize, en.uncompSize, out);          // deflate
        if (!ok || crc32(0, (const Bytef*)out.data(), (uInt)out.size()) != en.crc) { out.clear(); return false; }
        return true;
    }
    return false;
}

bool ListFiles(const std::uint8_t* data, std::size_t len, std::vector<std::string>& names) {
    names.clear();
    std::vector<Entry> entries;
    if (!ReadDirectory(data, len, entries)) return false;
    for (auto& en : entries)
        if (en.name.back() != '/') names.push_back(en.name);
    return true;
}

}  // namespace rtx::launcher::zip
