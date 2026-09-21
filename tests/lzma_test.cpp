// LZMA container decoding, as the cache layer uses it for the indexes stored that way.
// The source under test is compiled straight into this file; nothing else is linked.
#include "../src/cache/Lzma.cpp"

#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// 275 bytes of structured binary data, compressed the way the cache stores it: five
// property bytes followed by the stream.
static const unsigned char kPacked[] = {
    0x5d, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x7a, 0xea, 0x3c, 0xb7, 0xd0, 0xfe, 0xa3, 0x5c,
    0x45, 0x9d, 0x52, 0x6c, 0x3a, 0x23, 0x64, 0x51, 0xdf, 0xc6, 0x3a, 0x98, 0xe1, 0x39, 0xbd, 0x82,
    0x10, 0xec, 0x26, 0x11, 0x32, 0x22, 0x73, 0xb6, 0xf4, 0x82, 0xeb, 0xcb, 0x86, 0x10, 0xf0, 0x57,
    0xca, 0xb1, 0xeb, 0x52, 0x5c, 0x6f, 0xb7, 0x19, 0xe2, 0xe0, 0x98, 0xb4, 0xec, 0x43, 0xe9, 0x23,
    0xc4, 0x64, 0x8d, 0xab, 0xac, 0x88, 0x3b, 0xd3, 0x53, 0x63, 0xe2, 0xcb, 0x67, 0x6e, 0xe6, 0x93,
    0xbe, 0x94, 0x13, 0xfc, 0xed, 0x7e, 0x0c, 0xcf, 0xd8, 0x0f, 0xff, 0xfa, 0xa6, 0x10, 0x00,
};
constexpr std::size_t kPlainSize = 275;

int main() {
    auto plain = rtx::cache::LzmaDecompress(kPacked, sizeof(kPacked), kPlainSize);
    CHECK(plain.size() == kPlainSize);
    if (plain.size() == kPlainSize) {
        CHECK(plain[0] == 2 && plain[1] == 5 && plain[2] == 0x0f);     // the header the data starts with
        unsigned sum = 0;
        for (unsigned char b : plain) sum = sum * 31u + b;
        CHECK(sum == 1845194835u);
    }
    // a stream cut short yields nothing rather than garbage of the right length
    CHECK(rtx::cache::LzmaDecompress(kPacked, sizeof(kPacked) / 2, kPlainSize).empty());
    // nor does one that claims more than it holds
    CHECK(rtx::cache::LzmaDecompress(kPacked, sizeof(kPacked), kPlainSize + 64).empty());
    // malformed property byte and absurd sizes are refused outright
    unsigned char bad[16] = { 0xFF };
    CHECK(rtx::cache::LzmaDecompress(bad, sizeof(bad), 16).empty());
    CHECK(rtx::cache::LzmaDecompress(nullptr, 0, 16).empty());

    if (g_fail) { std::printf("  %d check(s) failed\n", g_fail); return 1; }
    std::printf("  lzma ok\n");
    return 0;
}
