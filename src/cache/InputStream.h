#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::cache {

// Read-only big-endian binary reader over a fixed byte buffer. Mirrors the
// subset of RS3 NXT cache primitives our decoders actually use (smart /
// big-smart integers, null-terminated strings). Out-of-range reads return
// 0 / "" rather than throwing so a partial archive doesn't kill a lookup.
class InputStream {
public:
    explicit InputStream(std::vector<std::uint8_t> bytes)
        : buf_(std::move(bytes)) {}

    int  remaining() const { return (int)buf_.size() - offset_; }
    int  offset()    const { return offset_; }
    // Clamped to [0, size] so remaining() is never negative on hostile lengths.
    void skip(int n)       { seek(offset_ + n); }
    void seek(int p)       { offset_ = p < 0 ? 0 : (p > (int)buf_.size() ? (int)buf_.size() : p); }

    int  ReadByte();          // 0 if past end
    int  ReadUnsignedByte() { return ReadByte() & 0xff; }
    int  ReadShort();
    int  ReadUnsignedShort();
    int  ReadInt();
    long long ReadLong();
    int  Read24BitInt();
    std::string ReadString();

    // Variable-width readers used by RS3 opcode payloads.
    int  ReadBigSmart();             // big-endian 16/32-bit length-prefixed
    int  ReadUnsignedSmart();        // 8/16 with bit-7 discriminator
    int  ReadSignedSmart();          // also called "smart3"
    int  ReadUnsignedShortSmart();

private:
    std::vector<std::uint8_t> buf_;
    int                       offset_ = 0;
};

}  // namespace rtx::cache
