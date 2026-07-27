#include "InputStream.h"

namespace rtx::cache {

int InputStream::ReadByte() {
    if (offset_ >= (int)buf_.size()) return 0;
    return buf_[offset_++];
}

int InputStream::ReadShort() {
    int v = (ReadUnsignedByte() << 8) | ReadUnsignedByte();
    if (v > 0x7FFF) v -= 0x10000;
    return v;
}

int InputStream::ReadUnsignedShort() {
    return (ReadUnsignedByte() << 8) | ReadUnsignedByte();
}

int InputStream::ReadInt() {
    return (ReadUnsignedByte() << 24) | (ReadUnsignedByte() << 16) |
           (ReadUnsignedByte() <<  8) |  ReadUnsignedByte();
}

long long InputStream::ReadLong() {
    long long hi = (unsigned)ReadInt();
    long long lo = (unsigned)ReadInt();
    return (hi << 32) | lo;
}

int InputStream::Read24BitInt() {
    return (ReadUnsignedByte() << 16) | (ReadUnsignedByte() << 8) | ReadUnsignedByte();
}

std::string InputStream::ReadString() {
    // Jagex cache strings are CP-1252, not UTF-8. Decode to UTF-8 so the
    // result is safe to embed in JSON for the WebView bridge: one lone high
    // byte is invalid UTF-8 and makes JSStringCreateWithUTF8CString reject
    // the whole payload. 0x00-0x7F pass through; 0x80-0xFF map via CP-1252.
    static const std::uint16_t kCp1252High[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
    };
    std::string out;
    int b;
    while ((b = ReadByte()) != 0) {
        unsigned ch = (unsigned)(b & 0xff);
        std::uint32_t cp = (ch < 0x80) ? ch
                         : (ch < 0xA0) ? kCp1252High[ch - 0x80]
                                       : ch;   // 0xA0-0xFF == Latin-1 code point
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

int InputStream::ReadBigSmart() {
    if (offset_ >= (int)buf_.size()) return -1;
    if (buf_[offset_] & 0x80) return (int)((unsigned)ReadInt() & 0x7FFFFFFFu);
    int v = ReadUnsignedShort();
    return (v == 0x7FFF) ? -1 : v;
}

int InputStream::ReadUnsignedSmart() {
    if (offset_ >= (int)buf_.size()) return 0;
    if ((buf_[offset_] & 0xff) >= 128) return ReadUnsignedShort() - 0x8000;
    return ReadUnsignedByte();
}

int InputStream::ReadSignedSmart() {
    if (offset_ >= (int)buf_.size()) return 0;
    if ((buf_[offset_] & 0xff) < 128) return ReadUnsignedByte() - 64;
    return ReadUnsignedShort() - 0xC000;
}

int InputStream::ReadUnsignedShortSmart() {
    if (offset_ >= (int)buf_.size()) return 0;
    if ((buf_[offset_] & 0xff) < 128) return ReadUnsignedByte();
    return ReadUnsignedShort() - 0x8000;
}

}  // namespace rtx::cache
