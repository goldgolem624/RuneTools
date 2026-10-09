#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::cache {

// Stop codes a decoder reports beside an unknown opcode (1..255): the record did not end the way a
// clean one does. Every record ends with opcode 0 exactly at its last byte, so each code names one
// way the bytes were read with the wrong shape.
constexpr int kStopOverrun  = 256;   // ran off the end before opcode 0 (a field read wider than it is)
constexpr int kStopSchema   = 257;   // dbrow disagrees with its table schema
constexpr int kStopTrailing = 258;   // opcode 0 with bytes still unread (a field read narrower than it is)
constexpr int kStopMisfit   = 259;   // a length-prefixed block did not end where its length said

// Big-endian reader over a fixed buffer. Out-of-range reads return 0 / "" rather than throwing and
// count as an overrun, which the decoders report as kStopOverrun.
class InputStream {
public:
    explicit InputStream(std::vector<std::uint8_t> bytes)
        : buf_(std::move(bytes)) {}

    int  remaining() const { return (int)buf_.size() - offset_; }
    int  offset()    const { return offset_; }
    bool overran()   const { return overrun_ > 0; }   // some read went past the end
    int  peek(int abs) const { return (abs >= 0 && abs < (int)buf_.size()) ? (buf_[abs] & 0xff) : -1; }   // byte at an absolute offset, no advance
    void skip(int n)       { if (n > remaining()) ++overrun_; seek(offset_ + n); }
    void seek(int p)       { offset_ = p < 0 ? 0 : (p > (int)buf_.size() ? (int)buf_.size() : p); }

    int  ReadByte();          // 0 if past end
    int  ReadUnsignedByte() { return ReadByte() & 0xff; }
    int  ReadShort();
    int  ReadUnsignedShort();
    int  ReadInt();
    long long ReadLong();
    int  Read24BitInt();
    std::string ReadString();

    int  ReadBigSmart();             // big-endian 16/32-bit length-prefixed
    int  ReadUnsignedSmart();        // 8/16 with bit-7 discriminator
    int  ReadSignedSmart();          // also called "smart3"
    int  ReadUnsignedShortSmart();

private:
    std::vector<std::uint8_t> buf_;
    int                       offset_ = 0;
    int                       overrun_ = 0;
};

// Runs one record's opcode loop with the shared end protocol. `body(op)` reads the payload of a
// known opcode and returns false for one it does not know. Returns 0 for a clean record, the
// unknown opcode, or a kStop* code; `last` is the last opcode read before the stop.
template <class F>
int WalkOps(InputStream& s, int& last, F&& body) {
    for (;;) {
        const int op = s.ReadUnsignedByte();
        if (s.overran()) return kStopOverrun;
        if (op == 0) return s.remaining() > 0 ? kStopTrailing : 0;
        last = op;
        const bool known = body(op);
        if (s.overran()) return kStopOverrun;
        if (!known) return op;
    }
}

}  // namespace rtx::cache
