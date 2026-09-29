#include "Calibrate.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>

#pragma comment(lib, "version.lib")

namespace rtx::calib {
namespace {

// A rule: in the handler of `op`, the first instruction of the given shape whose displacement is
// in [lo, hi) is the offset, as long as no later one reads a different displacement. `head` is the
// instruction's bytes up to the displacement (opcode and ModRM with the root register as base and a
// 32-bit displacement); `nth` picks a later match when the first is a different field.
struct Rule { const char* name; const char* op; const char* head; std::uint32_t lo, hi; int nth; };

// Root register rcx is the first argument of every handler; rax after `mov rax,[rcx+disp]` holds
// the object the offset points at, which the inner rules read through.
const Rule kRules[] = {
    // MainData fields
    { "kOffClientClock",   "CLIENTCLOCK",             "8B 91",    0x100,   0x2000,  0 },   // mov edx,[rcx+disp]
    { "kOffWorld",         "MAP_WORLD",               "48 8B 81", 0x18000, 0x60000, 0 },   // mov rax,[rcx+disp]
    { "kOffStatus",        "WORLDLIST_FETCH",         "8B 81",    0x18000, 0x60000, 0 },   // mov eax,[rcx+disp]
    { "kOffStats",         "STAT_BASE",               "48 8B 81", 0x18000, 0x60000, 0 },
    { "kStatsInner",       "STAT_BASE",               "48 8B 90", 0x1000,  0x10000, 0 },   // mov rdx,[rax+disp]
    { "kOffGE",            "STOCKMARKET_GETOFFERITEM","49 8B 82", 0x18000, 0x60000, 0 },   // mov rax,[r10+disp]
    { "kOffAccount",       "PLAYERMEMBER",            "48 8B 81", 0x18000, 0x60000, 0 },
    { "kOffVarcStore",     "GET_MOUSEX",              "48 8B 81", 0x18000, 0x60000, 0 },   // the store the varcs and the mouse share
};

struct Cache { std::wstring key; std::vector<Found> found; std::string report; };
std::mutex g_mu;
Cache g_cache;

bool ReadFile(const std::wstring& path, std::vector<std::uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (256ll << 20)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0; std::size_t at = 0;
    while (at < out.size()) {
        if (!::ReadFile(h, out.data() + at, (DWORD)(out.size() - at), &got, nullptr) || !got) { CloseHandle(h); return false; }
        at += got;
    }
    CloseHandle(h);
    return true;
}

std::wstring FileKey(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return L"";
    wchar_t buf[64];
    _snwprintf_s(buf, _TRUNCATE, L"|%u|%u|%u", a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);
    return path + buf;
}

struct Pe { const std::uint8_t* base; std::size_t size; std::uint64_t imageBase; std::uint32_t textRva, textRaw, textSize; };
bool ParsePe(const std::vector<std::uint8_t>& f, Pe& pe) {
    if (f.size() < 0x200 || f[0] != 'M' || f[1] != 'Z') return false;
    std::uint32_t e_lfanew; std::memcpy(&e_lfanew, f.data() + 0x3C, 4);
    if (e_lfanew + 0x108 > f.size() || std::memcmp(f.data() + e_lfanew, "PE\0\0", 4) != 0) return false;
    const std::uint8_t* nt = f.data() + e_lfanew;
    std::uint16_t nsec; std::memcpy(&nsec, nt + 6, 2);
    std::uint16_t optSize; std::memcpy(&optSize, nt + 20, 2);
    std::memcpy(&pe.imageBase, nt + 24 + 24, 8);
    const std::uint8_t* sec = nt + 24 + optSize;
    for (unsigned i = 0; i < nsec; ++i, sec += 40) {
        if ((const std::uint8_t*)(sec + 40) > f.data() + f.size()) return false;
        if (std::memcmp(sec, ".text\0\0\0", 8) != 0) continue;
        std::memcpy(&pe.textSize, sec + 8, 4); std::memcpy(&pe.textRva, sec + 12, 4); std::memcpy(&pe.textRaw, sec + 20, 4);
        std::uint32_t rawSize; std::memcpy(&rawSize, sec + 16, 4);
        if (pe.textSize > rawSize) pe.textSize = rawSize;
        if ((std::uint64_t)pe.textRaw + pe.textSize > f.size()) return false;
        pe.base = f.data(); pe.size = f.size();
        return true;
    }
    return false;
}

// Every registered handler by scrambled number: mov r8w, imm16 ; lea rdx, [rip+rel32]. The
// registrar is the one routine holding thousands of these; anything else matching the two
// instructions by chance is drowned by the requirement that the numbers form one dense run.
bool Handlers(const Pe& pe, std::map<std::uint32_t, std::uint32_t>& outByOp /* op -> rva */) {
    const std::uint8_t* t = pe.base + pe.textRaw;
    for (std::size_t i = 0; i + 12 <= pe.textSize; ++i) {
        if (t[i] != 0x66 || t[i + 1] != 0x41 || t[i + 2] != 0xB8 || t[i + 5] != 0x48 || t[i + 6] != 0x8D || t[i + 7] != 0x15) continue;
        std::uint16_t op; std::memcpy(&op, t + i + 3, 2);
        std::int32_t rel; std::memcpy(&rel, t + i + 8, 4);
        const std::int64_t rva = (std::int64_t)pe.textRva + (std::int64_t)i + 12 + rel;
        if (rva < pe.textRva || rva >= (std::int64_t)pe.textRva + pe.textSize) continue;
        auto it = outByOp.find(op);
        if (it != outByOp.end() && it->second != (std::uint32_t)rva) return false;   // the same number twice with different handlers: not the registrar
        outByOp[op] = (std::uint32_t)rva;
    }
    if (outByOp.size() < 1500) return false;
    // dense: the numbers run 1..N with few gaps
    std::uint32_t maxOp = outByOp.rbegin()->first;
    return outByOp.size() * 10 >= (std::size_t)maxOp * 9;
}

// name -> scrambled number, from the launcher's export: [{"op":N,"id":..,"name":"X"},...]
bool OpTable(const std::wstring& path, std::map<std::string, std::uint32_t>& out) {
    std::vector<std::uint8_t> f;
    if (!ReadFile(path, f)) return false;
    std::string s(f.begin(), f.end());
    std::size_t at = 0;
    while ((at = s.find("\"op\":", at)) != std::string::npos) {
        at += 5;
        const std::uint32_t op = (std::uint32_t)std::strtoul(s.c_str() + at, nullptr, 10);
        const std::size_t end = s.find('}', at);
        if (end == std::string::npos) break;
        const std::size_t nm = s.find("\"name\":\"", at);
        if (nm != std::string::npos && nm < end) {
            const std::size_t q = s.find('"', nm + 8);
            if (q != std::string::npos && q < end) out[s.substr(nm + 8, q - nm - 8)] = op;
        }
        at = end;
    }
    return out.size() > 500;
}

// The build in the exe's version resource ("950.1.0.0"), the form the launcher writes beside the
// operation table when it extracts it.
std::string ExeBuild(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!sz) return {};
    std::vector<std::uint8_t> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return {};
    VS_FIXEDFILEINFO* ffi = nullptr; UINT n = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &n) || !ffi || n < sizeof(*ffi)) return {};
    char v[48];
    std::snprintf(v, sizeof(v), "%u.%u.%u.%u",
                  (unsigned)HIWORD(ffi->dwFileVersionMS), (unsigned)LOWORD(ffi->dwFileVersionMS),
                  (unsigned)HIWORD(ffi->dwFileVersionLS), (unsigned)LOWORD(ffi->dwFileVersionLS));
    return v;
}

// Operation numbers are reshuffled between game builds, so a table from another build names the
// wrong handlers and every rule would read some other field. Only a table extracted from this build
// is used. The launcher writes the build label when an extraction that rewrote the table ends: the
// build when the run succeeded and the game did not update during it, else an empty label. It gives
// a build label the table's write time. A label newer than its table was not written for that table
// (older launchers wrote it when an extraction started), so the table also has to be at least as
// new as its label. Empty when the table can be used, else why not.
std::string TableBuildMismatch(const std::wstring& exePath, const std::wstring& opcodesJson, const std::wstring& buildTxt) {
    std::vector<std::uint8_t> b;
    std::string table;
    if (ReadFile(buildTxt, b)) table.assign(b.begin(), b.end());
    while (!table.empty() && (table.back() == '\r' || table.back() == '\n' || table.back() == ' ')) table.pop_back();
    const std::string running = ExeBuild(exePath);
    // worded "not found" like a rule that found nothing, so the health row reads it as a failure
    char line[200];
    if (running.empty() || table != running) {
        std::snprintf(line, sizeof(line), "calibrate: operation table for build %s not found (cs2\\opcodes.json is from build %s)\n",
                      running.empty() ? "(unknown)" : running.c_str(), table.empty() ? "(unknown)" : table.substr(0, 40).c_str());
        return line;
    }
    WIN32_FILE_ATTRIBUTE_DATA t{}, v{};
    if (!GetFileAttributesExW(opcodesJson.c_str(), GetFileExInfoStandard, &t) ||
        !GetFileAttributesExW(buildTxt.c_str(), GetFileExInfoStandard, &v) ||
        CompareFileTime(&t.ftLastWriteTime, &v.ftLastWriteTime) < 0) {
        std::snprintf(line, sizeof(line), "calibrate: operation table for build %s not found (the last extraction did not rewrite cs2\\opcodes.json)\n",
                      running.c_str());
        return line;
    }
    return {};
}

int ParseHex(const char* pat, std::uint8_t* out, int cap) {
    int n = 0;
    for (const char* p = pat; *p && n < cap; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char two[3] = { p[0], p[1], 0 };
        out[n++] = (std::uint8_t)std::strtoul(two, nullptr, 16); p += 2;
    }
    return n;
}

// The instruction of shape `head` + disp32 in the first 0x140 bytes of the handler, never past
// `endRva` (the next handler), with the displacement in range; `nth` skips earlier matches. 0 when
// absent, or when a later match reads a different displacement (`ambiguous`): a guess would read
// the wrong field with no sign of it, where a rule that finds nothing is reported and keeps the
// compiled constant.
std::uint32_t FindDisp(const Pe& pe, std::uint32_t handlerRva, std::uint32_t endRva, const Rule& r, bool& ambiguous) {
    ambiguous = false;
    std::uint8_t head[8]; const int hn = ParseHex(r.head, head, 8);
    const bool headRex = (head[0] & 0xF0) == 0x40;
    const std::uint8_t* p = pe.base + pe.textRaw + (handlerRva - pe.textRva);
    std::size_t len = pe.textSize - (handlerRva - pe.textRva);
    if (len > 0x140) len = 0x140;
    if (endRva > handlerRva && endRva - handlerRva < len) len = endRva - handlerRva;
    int seen = 0; bool have = false; std::uint32_t got = 0;
    for (std::size_t i = 0; i + hn + 4 <= len; ++i) {
        if (std::memcmp(p + i, head, hn) != 0) continue;
        // a head without its own REX byte also matches one byte into the REX form, which is
        // another instruction (other register or width)
        if (!headRex && i > 0 && (p[i - 1] & 0xF0) == 0x40) continue;
        std::uint32_t disp; std::memcpy(&disp, p + i + hn, 4);
        if (disp < r.lo || disp >= r.hi) continue;
        if (seen++ < r.nth) continue;
        if (!have) { have = true; got = disp; continue; }
        if (disp != got) { ambiguous = true; return 0; }   // the same field read again is fine
    }
    return have ? got : 0;
}

}  // namespace

const std::vector<Found>& Run(const std::wstring& exePath, const std::wstring& opcodesJson) {
    std::lock_guard<std::mutex> lk(g_mu);
    // the build label the launcher keeps beside the table
    const std::size_t slash = opcodesJson.find_last_of(L"\\/");
    const std::wstring buildTxt = (slash == std::wstring::npos ? std::wstring() : opcodesJson.substr(0, slash + 1)) + L"client_version.txt";
    const std::wstring key = FileKey(exePath) + L"#" + FileKey(opcodesJson) + L"#" + FileKey(buildTxt);
    if (!g_cache.key.empty() && g_cache.key == key) return g_cache.found;
    g_cache = Cache{}; g_cache.key = key;
    std::ostringstream rep;
    std::vector<std::uint8_t> f; Pe pe{};
    std::map<std::uint32_t, std::uint32_t> handlers; std::map<std::string, std::uint32_t> ops;
    if (!ReadFile(exePath, f) || !ParsePe(f, pe)) { rep << "calibrate: client exe not readable\n"; g_cache.report = rep.str(); return g_cache.found; }
    if (!Handlers(pe, handlers)) { rep << "calibrate: operation registrar not recognised\n"; g_cache.report = rep.str(); return g_cache.found; }
    if (!OpTable(opcodesJson, ops)) { rep << "calibrate: operation table (cs2\\opcodes.json) not readable\n"; g_cache.report = rep.str(); return g_cache.found; }
    const std::string mismatch = TableBuildMismatch(exePath, opcodesJson, buildTxt);
    if (!mismatch.empty()) { g_cache.report = mismatch; return g_cache.found; }
    rep << "calibrate: " << handlers.size() << " handlers, " << ops.size() << " named operations\n";
    // handler entry points in address order: a handler's scan stops where the next one begins
    std::vector<std::uint32_t> starts;
    for (const auto& kv : handlers) starts.push_back(kv.second);
    std::sort(starts.begin(), starts.end());
    for (const Rule& r : kRules) {
        Found fd{ r.name, 0, 0, r.op };
        auto o = ops.find(r.op);
        if (o == ops.end()) { rep << "  " << r.name << ": operation " << r.op << " not in the table\n"; g_cache.found.push_back(fd); continue; }
        auto h = handlers.find(o->second);
        if (h == handlers.end()) { rep << "  " << r.name << ": no handler for " << r.op << " (number " << o->second << ")\n"; g_cache.found.push_back(fd); continue; }
        auto next = std::upper_bound(starts.begin(), starts.end(), h->second);
        const std::uint32_t end = next != starts.end() ? *next : pe.textRva + pe.textSize;
        bool ambiguous = false;
        fd.found = FindDisp(pe, h->second, end, r, ambiguous);
        char line[160];
        if (ambiguous) std::snprintf(line, sizeof(line), "  %s: not found, more than one candidate (from %s)\n", r.name, r.op);
        else std::snprintf(line, sizeof(line), "  %s: %s0x%X (from %s)\n", r.name, fd.found ? "" : "not found, wanted ", fd.found, r.op);
        rep << line;
        g_cache.found.push_back(fd);
    }
    g_cache.report = rep.str();
    return g_cache.found;
}

std::uint32_t Use(const char* name, std::uint32_t compiled) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& fd : g_cache.found) {
        if (std::strcmp(fd.name, name) != 0) continue;
        fd.compiled = compiled;
        return fd.found ? fd.found : compiled;
    }
    return compiled;
}

std::string Report() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string r = g_cache.report;
    for (const auto& fd : g_cache.found)
        if (fd.found && fd.compiled && fd.found != fd.compiled) {
            char line[160];
            std::snprintf(line, sizeof(line), "  %s MOVED: compiled 0x%X, client 0x%X, using the client's\n", fd.name, fd.compiled, fd.found);
            r += line;
        }
    return r;
}

}  // namespace rtx::calib
