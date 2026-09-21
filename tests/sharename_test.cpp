#include "ShareName.h"   // companion/ShareName.h, via the -I in run-tests.cmd
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

static int fails = 0;
static void check(bool c, const char* what) {
    std::printf("%-58s %s\n", what, c ? "ok" : "FAIL");
    if (!c) ++fails;
}

int main() {
    const wchar_t* pre = L"Local\\RuneToolsXFrame_v2_";

    // Normal build
    wchar_t buf[rtx::ipc::kNameChars];
    check(rtx::ipc::BuildName(buf, pre, 1234u), "builds with a normal pid");
    check(std::wcscmp(buf, L"Local\\RuneToolsXFrame_v2_1234") == 0, "produces the expected name");

    // pid 0 and max pid
    check(rtx::ipc::BuildName(buf, pre, 0u), "builds with pid 0");
    check(std::wcscmp(buf, L"Local\\RuneToolsXFrame_v2_0") == 0, "pid 0 renders as 0");
    check(rtx::ipc::BuildName(buf, pre, 4294967295u), "builds with max uint32 pid");
    check(std::wcscmp(buf, L"Local\\RuneToolsXFrame_v2_4294967295") == 0, "max pid renders fully");

    // Exact fit: prefix(25) + "1234"(4) + NUL = 30
    wchar_t tight[30];
    check(rtx::ipc::BuildName(tight, 30, pre, 1234u), "succeeds when the buffer fits exactly");
    check(std::wcscmp(tight, L"Local\\RuneToolsXFrame_v2_1234") == 0, "exact fit is not truncated");

    // One short: must refuse and empty, never truncate
    wchar_t shortb[29];
    for (int i = 0; i < 29; ++i) shortb[i] = L'X';
    check(!rtx::ipc::BuildName(shortb, 29, pre, 1234u), "refuses when one char too small");
    check(shortb[0] == 0, "leaves an empty string rather than a truncated name");

    // Too small even for the prefix
    wchar_t tiny[4];
    for (int i = 0; i < 4; ++i) tiny[i] = L'X';
    check(!rtx::ipc::BuildName(tiny, 4, pre, 1234u), "refuses when smaller than the prefix");
    check(tiny[0] == 0, "empties on prefix overflow");

    // Degenerate inputs
    check(!rtx::ipc::BuildName(buf, 0, pre, 1u), "refuses zero capacity");
    check(!rtx::ipc::BuildName(nullptr, 10, pre, 1u), "refuses a null destination");
    check(!rtx::ipc::BuildName(buf, rtx::ipc::kNameChars, nullptr, 1u), "refuses a null prefix");

    // No write past the end
    struct { wchar_t b[30]; wchar_t canary; } g;
    g.canary = L'#';
    rtx::ipc::BuildName(g.b, 30, pre, 1234u);
    check(g.canary == L'#', "does not write past the declared capacity");

    // ---- session key ----
    check(!rtx::ipc::HasSessionKey(1234u), "starts with no session key");
    std::uint32_t g0 = rtx::ipc::SessionGeneration();

    std::uint8_t key[16];
    for (int i = 0; i < 16; ++i) key[i] = (std::uint8_t)(i * 17);
    std::uint32_t g1 = rtx::ipc::SetSessionKey(1234u, key, sizeof(key));
    check(rtx::ipc::HasSessionKey(1234u), "records the session key");
    check(g1 == g0 + 1, "setting a key bumps the generation");
    check(rtx::ipc::SetSessionKey(1234u, key, sizeof(key)) == g1, "setting the same key does not bump");

    check(rtx::ipc::BuildName(buf, pre, 1234u), "builds a keyed name");
    check(std::wcscmp(buf, L"Local\\RuneToolsXFrame_v2_1234_00112233445566778899aabbccddeeff") == 0,
          "keyed name is prefix, pid, then the key in hex");

    check(rtx::ipc::SetSessionKey(1234u, key, 15) == g1, "refuses a short key");
    check(rtx::ipc::SetSessionKey(1234u, nullptr, 16) == g1, "refuses a null key");

    const wchar_t* longest = L"Local\\RuneToolsXGroundItems_v1_";
    wchar_t big[rtx::ipc::kNameChars];
    check(rtx::ipc::BuildName(big, longest, 4294967295u), "longest keyed name fits in kNameChars");

    wchar_t nokey[30];
    check(!rtx::ipc::BuildName(nokey, 30, pre, 1234u), "refuses when the key does not fit");
    check(nokey[0] == 0, "does not silently fall back to an unkeyed name");

    std::uint32_t g2 = rtx::ipc::ClearSessionKey(1234u);
    check(!rtx::ipc::HasSessionKey(1234u), "clears the session key");
    check(g2 == g1 + 1, "clearing bumps the generation");
    check(rtx::ipc::BuildName(buf, pre, 1234u) &&
          std::wcscmp(buf, L"Local\\RuneToolsXFrame_v2_1234") == 0,
          "reverts to the legacy name once cleared");

    // ---- per pid isolation ----
    std::uint8_t kA[16], kB[16];
    for (int i = 0; i < 16; ++i) { kA[i] = (std::uint8_t)i; kB[i] = (std::uint8_t)(200 + i); }
    rtx::ipc::SetSessionKey(1111u, kA, sizeof(kA));
    rtx::ipc::SetSessionKey(2222u, kB, sizeof(kB));

    wchar_t a[rtx::ipc::kNameChars], b[rtx::ipc::kNameChars];
    rtx::ipc::BuildName(a, pre, 1111u);
    rtx::ipc::BuildName(b, pre, 2222u);
    check(std::wcscmp(a, b) != 0, "two clients get different names");
    check(std::wcsstr(a, L"000102") != nullptr, "first client uses its own key");
    check(std::wcsstr(b, L"c8c9ca") != nullptr, "second client uses its own key");

    check(!rtx::ipc::HasSessionKey(3333u), "an unkeyed pid stays unkeyed");
    wchar_t c[rtx::ipc::kNameChars];
    rtx::ipc::BuildName(c, pre, 5678u);
    check(std::wcscmp(c, L"Local\\RuneToolsXFrame_v2_5678") == 0, "unkeyed pid keeps the legacy name");

    rtx::ipc::ClearSessionKey(1111u);
    check(!rtx::ipc::HasSessionKey(1111u), "clearing one client");
    check(rtx::ipc::HasSessionKey(2222u), "does not clear the other");
    rtx::ipc::ClearSessionKey(2222u);

    // ---- table pressure ----
    // More clients than slots must still name channels correctly: the oldest
    // entry is reclaimed rather than the new client being refused a key.
    for (std::uint32_t i = 0; i < 40; ++i) {
        std::uint8_t k[16];
        for (int j = 0; j < 16; ++j) k[j] = (std::uint8_t)(i + j);
        rtx::ipc::SetSessionKey(9000u + i, k, sizeof(k));
    }
    check(rtx::ipc::HasSessionKey(9039u), "the newest client always gets a key");
    check(!rtx::ipc::HasSessionKey(9000u), "the oldest client was reclaimed");

    wchar_t last[rtx::ipc::kNameChars];
    check(rtx::ipc::BuildName(last, pre, 9039u), "newest client builds a name");
    check(std::wcsstr(last, L"_") != nullptr, "newest client name carries a key");
    for (std::uint32_t i = 0; i < 40; ++i) rtx::ipc::ClearSessionKey(9000u + i);

    std::printf("\n%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
