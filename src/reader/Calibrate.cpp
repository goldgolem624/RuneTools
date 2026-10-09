#include "Calibrate.h"
#include "../../companion/MainDataOffsets.h"
#include "../../companion/SceneOffsets.h"
#include "../../companion/Signatures.h"

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
// 32-bit displacement); `nth` picks a later match when the first is a different field. `compiled`
// is set for a reference rule: an offset a compiled table carries, reported against that value and
// never applied (the reader's own offsets take theirs through Use()).
// `wrap`: the handler is a component setter wrapper and the shape sits in the callback it hands on.
struct Rule { const char* name; const char* op; const char* head; std::uint32_t lo, hi; int nth; std::uint32_t compiled = 0; bool wrap = false; const char* what = nullptr; };
constexpr std::uint32_t kMdLo = 0x18000, kMdHi = 0x60000, kInLo = 0x1000, kInHi = 0x10000;
// The varp hash map sits inside the player var store: derived from md::kVarpMgr, never pinned alone.
constexpr std::uint32_t kVarpHashFromMgr = rtx::md::kVarpHashFromMgr;

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
    // references: MainDataOffsets.h
    { "md::kTracker",      "TELEMETRY_GET_GRID_VALUE",         "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kTracker },
    { "md::kChatStore",    "CHAT_LASTUID",                     "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kChatStore },
    { "md::kClanSettings", "ACTIVECLANSETTINGS_FIND_LISTENED", "48 8B 91", kMdLo, kMdHi, 0, rtx::md::kClanSettings },
    { "md::kMapMgr",       "MAP_LOADEDPERCENT",                "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kMapMgr },
    { "md::kInputReport",  "AUTOSETUP_SETCUSTOM",              "48 8B 83", kMdLo, kMdHi, 0, rtx::md::kInputReport },   // mov rax,[rbx+disp]
    { "md::kIfaceOwner",   "IF_CLOSE",                         "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kIfaceOwner },
    { "md::kInputProc",    "MINIMENUOPEN",                     "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kInputProc },
    { "md::kPlayerGroup",  "PLAYER_GROUP_MEMBER_COUNT",        "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kPlayerGroup },
    { "md::kPlayers",      "COORD",                            "48 8B 88", kMdLo, kMdHi, 0, rtx::md::kPlayers },        // mov rcx,[rax+disp]
    { "md::kFriends",      "FRIEND_COUNT",                     "4C 8B 89", kMdLo, kMdHi, 0, rtx::md::kFriends },
    { "md::kContainers",   "INV_TOTALCAT",                     "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kContainers },
    { "md::kSceneViews",   "UNKNOWN_COMMAND_500",              "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kSceneViews },
    { "md::kCutscene",     "CUTSCENE2D_STOP",                  "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kCutscene },
    { "md::kSoundCtx",     "SOUND_SYNTH",                      "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kSoundCtx },
    { "md::kLanguage",     "MAP_LANG",                         "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kLanguage },
    { "md::kVarpMgr",      "QUEST_FINISHED",                   "48 8D 97", kMdLo, kMdHi, 0, rtx::md::kVarpMgr },        // lea rdx,[rdi+disp]
    { "md::kOptions",      "CLIENTOPTION_GET",                 "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kOptions },
    { "md::kConfigs",      "QUEST_GETDIFFICULTY",              "4C 8B 81", kMdLo, kMdHi, 0, rtx::md::kConfigs },        // mov r8,[rcx+disp]
    { "md::kDisplay",      "WINDOW_GETINSETS",                 "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kDisplay },
    { "md::kConnection",   "ABORT_DIALOG",                     "48 8B 91", kMdLo, kMdHi, 0, rtx::md::kConnection },
    { "md::kFriendsChat",  "CLAN_GETCHATCOUNT",                "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kFriendsChat },
    { "md::kLoginMgr",     "LOGIN_REPLY",                      "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kLoginMgr },
    { "md::kWorldMapMgr",  "WORLDMAP_GETCURRENTMAP",           "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kWorldMapMgr },
    { "md::kServerTimeOffset", "DATE_RUNEDAY",                 "48 F7 A9", kMdLo, kMdHi, 0, rtx::md::kServerTimeOffset },   // imul [rcx+disp]
    { "md::kCountry",      "PLAYERCOUNTRY",                    "8B 91",    kMdLo, kMdHi, 0, rtx::md::kCountry },        // mov edx,[rcx+disp]
    { "md::kWorldInfo",    "REBOOTTIMER",                      "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kWorldInfo },
    // references: offsets written out in the reader and the companion
    { "dbMgr",             "DB_FIND_GET",                      "48 8B B1", kMdLo, kMdHi, 0, 0x19980, false, "ScriptRunner" },
    { "dbOther",           "DB_LISTALL",                       "48 8B 89", kMdLo, kMdHi, 0, 0x198C0, false, "DbDatabase" },
    // the script string stack: cc_settext's callback adds the stack base to the state and counts the pointer down
    { "stringStack",       "CC_SETTEXT",                       "49 81 C1", kInLo, kInHi, 0, 0x10A8, true, "string stack base in the script state" },
    { "stringSp",          "CC_SETTEXT",                       "41 FF 89", kInLo, kInHi, 0, 0x8DA8, true, "string stack pointer in the script state" },
    { "varcMouseX",        "GET_MOUSEX",                       "44 0F 2C 80", kInLo, kInHi, 0, 0x46D8 },   // cvttss2si r8d,[rax+disp], in the store
    { "varcMouseY",        "GET_MOUSEY",                       "44 0F 2C 80", kInLo, kInHi, 0, 0x46DC },
    { "varcButtons",       "GET_MOUSEBUTTONS",                 "44 38 80", kInLo, kInHi, 0, 0x46E8 },
    { "varcClan",          "CLANPROFILE_FIND",                 "48 39 90", kInLo, kInHi, 0, 0x77B0 },
    { "stateEntityRef",    "GET_ENTITY_BOUNDING_BOX",          "48 8B 9A", kInLo, kInHi, 0, 0xC3B0 },      // the script state (rdx)
    { "stateEntityObj",    "GET_ENTITY_OVERLAY_HEIGHT",        "48 8B 8A", kInLo, kInHi, 0, 0xC3B8 },
    { "scn::kCombat",      "COMLEVEL_ACTIVE",                  "8B 91",    kInLo, kInHi, 0, (std::uint32_t)rtx::scn::kCombat },   // in the player's actor
};

// Handlers that name themselves in their error text: the string, and the table name whose number
// must be one of the handlers taking its address. One that disagrees means the table numbers
// another build's handlers.
struct Spot { const char* text; const char* op; };
const Spot kSpots[] = {
    { "db_getfield",                    "dbrow_getfield" },
    { "array_sort",                     "ARRAY_SORT" },
    { "_minimenuopen",                  "MINIMENUOPEN" },
    { "_deeplink_get",                  "DEEPLINK_GET" },
    { "_shop_applypendingtransactions", "SHOP_APPLYPENDINGTRANSACTIONS" },
};

struct Cache {
    std::wstring key; std::vector<Found> found; std::string report, why;
    TableState table; std::vector<SpotCheck> spots;
};
std::mutex g_mu;
Cache g_cache;

// The build in the exe's version resource ("950.1.0.0"), the form the launcher writes beside the
// operation table when it extracts it.
std::string ExeBuildImpl(const std::wstring& path) {
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
// is used. The launcher writes the build label ("950.1.0.0", then the exe's time stamp) when an
// extraction that rewrote the table ends: the build when the run succeeded and the game did not
// update during it, else an empty label. It gives a build label the table's write time. A label
// newer than its table was not written for that table (older launchers wrote it when an extraction
// started), so the table also has to be at least as new as its label. The time stamp is not
// required to match: the OpenGL and Vulkan clients of one build share their numbers, and the
// self-naming handlers settle whether this exe's numbers are the table's. Empty when the table can
// be used, else why not.
std::string TableBuildMismatch(const std::wstring& exePath, const std::wstring& opcodesJson, const std::wstring& buildTxt,
                               TableState& ts) {
    std::vector<std::uint8_t> b;
    std::string table;
    if (ReadFile(buildTxt, b)) table.assign(b.begin(), b.end());
    while (!table.empty() && (table.back() == '\r' || table.back() == '\n' || table.back() == ' ')) table.pop_back();
    ts.label = table;
    const std::size_t sp = table.find(' ');
    const std::string version = sp == std::string::npos ? table : table.substr(0, sp);
    const std::string running = ExeBuildImpl(exePath);
    ts.exeVersion = running;
    char line[200];
    if (running.empty() || version != running) {
        std::snprintf(line, sizeof(line), "operation table is for build %s, the client is %s",
                      version.empty() ? "(unknown)" : version.substr(0, 40).c_str(), running.empty() ? "(unknown)" : running.c_str());
        return line;
    }
    WIN32_FILE_ATTRIBUTE_DATA t{}, v{};
    if (!GetFileAttributesExW(opcodesJson.c_str(), GetFileExInfoStandard, &t) ||
        !GetFileAttributesExW(buildTxt.c_str(), GetFileExInfoStandard, &v) ||
        CompareFileTime(&t.ftLastWriteTime, &v.ftLastWriteTime) < 0) {
        std::snprintf(line, sizeof(line), "operation table for build %s was not rewritten by the last extraction", running.c_str());
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

// Every hit of a pattern (-1 = any byte) in the executable sections, at most `cap`.
std::vector<std::uint32_t> ScanCode(const Pe& pe, const int* pat, std::size_t n, std::size_t cap) {
    std::vector<std::uint32_t> hits;
    for (const auto& s : pe.sections) {
        if (!(s.flags & 0x20000000u)) continue;
        const std::uint8_t* b = pe.base + s.raw;
        const std::size_t len = std::min<std::size_t>(s.rawSize, s.vsize ? s.vsize : s.rawSize);
        for (std::size_t i = 0; i + n <= len; ++i) {
            if (b[i] != (std::uint8_t)pat[0]) continue;
            std::size_t k = 1;
            while (k < n && (pat[k] < 0 || b[i + k] == (std::uint8_t)pat[k])) ++k;
            if (k < n) continue;
            hits.push_back(s.rva + (std::uint32_t)i);
            if (hits.size() >= cap) return hits;
        }
    }
    return hits;
}

// The root global: the MainData constructor (32 bytes before the anchor) publishes it with its
// first mov [rip+disp],rax.
std::uint32_t RootGlobal(const Pe& pe) {
    int anchor[sizeof(rtx::sig::kMainAnchor)];
    for (std::size_t i = 0; i < sizeof(anchor) / sizeof(int); ++i) anchor[i] = rtx::sig::kMainAnchor[i];
    const auto hits = ScanCode(pe, anchor, sizeof(anchor) / sizeof(int), 2);
    if (hits.size() != 1 || hits[0] < 32) return 0;
    const std::uint32_t f0 = hits[0] - 32;
    const std::uint8_t* p = At(pe, f0, 0x200 + 7);
    if (!p) return 0;
    for (std::uint32_t i = 0; i < 0x200; ++i) {
        if (p[i] != 0x48 || p[i + 1] != 0x89 || p[i + 2] != 0x05) continue;
        std::int32_t rel; std::memcpy(&rel, p + i + 3, 4);
        return (std::uint32_t)((std::int64_t)f0 + i + 7 + rel);
    }
    return 0;
}

// A fourth clock read: mov rax,[clock] ; add rax,20000 (a connection's ping deadline, 20 s ahead).
constexpr int kClockReadD[] = { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x05, 0x20, 0x4E, 0x00, 0x00 };

// Where the engine clock sits after the root global, from the routines that read it; 0 when none
// is found or two disagree (`why` says which).
std::uint32_t EngineClock(const Pe& pe, std::string& why) {
    const std::uint32_t g = RootGlobal(pe);
    if (!g) { why = "root global not found"; return 0; }
    std::uint32_t off = 0;
    auto take = [&](const int* pat, std::size_t len) {
        const auto hits = ScanCode(pe, pat, len, 2);
        if (hits.size() != 1) return true;
        const std::uint8_t* p = At(pe, hits[0] + rtx::sig::kClockDispAt, 4);
        if (!p) return true;
        std::int32_t rel; std::memcpy(&rel, p, 4);
        const std::int64_t d = (std::int64_t)hits[0] + rtx::sig::kClockDispAt + 4 + rel - g;
        if (d <= 0 || d >= rtx::sig::kClockMax) return true;
        if (off && off != (std::uint32_t)d) { why = "the clock reads name two places"; return false; }
        off = (std::uint32_t)d;
        return true;
    };
    for (const auto& r : rtx::sig::kClockReads) if (!take(r.pat, r.len)) return 0;
    if (!take(kClockReadD, sizeof(kClockReadD) / sizeof(int))) return 0;
    if (!off) why = "no clock read of a known shape";
    return off;
}

// A component setter handler that is a wrapper: the callback it hands its dispatcher, else 0.
std::uint32_t WrapperCallback(const Pe& pe, std::uint32_t h) {
    using namespace rtx::sig;
    const std::uint8_t* p = At(pe, h, 0x40);
    if (!p) return 0;
    if (p[0] == 0x40) { ++h; ++p; }
    if (std::memcmp(p, kCcWrapHead, sizeof(kCcWrapHead)) || std::memcmp(p + kCcWrapMidAt, kCcWrapMid, sizeof(kCcWrapMid)) ||
        std::memcmp(p + kCcWrapTailAt, kCcWrapTail, sizeof(kCcWrapTail))) return 0;
    std::int32_t rel; std::memcpy(&rel, p + 25, 4);
    const std::int64_t cb = (std::int64_t)h + 29 + rel;
    if (cb < pe.textRva || cb >= (std::int64_t)pe.textRva + pe.textSize) return 0;
    return (std::uint32_t)cb;
}

}  // namespace

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

bool ParsePe(const std::vector<std::uint8_t>& f, Pe& pe) {
    if (f.size() < 0x200 || f[0] != 'M' || f[1] != 'Z') return false;
    std::uint32_t e_lfanew; std::memcpy(&e_lfanew, f.data() + 0x3C, 4);
    if ((std::uint64_t)e_lfanew + 0x108 > f.size() || std::memcmp(f.data() + e_lfanew, "PE\0\0", 4) != 0) return false;
    const std::uint8_t* nt = f.data() + e_lfanew;
    std::uint16_t nsec; std::memcpy(&nsec, nt + 6, 2);
    std::memcpy(&pe.stamp, nt + 8, 4);
    std::uint16_t optSize; std::memcpy(&optSize, nt + 20, 2);
    const std::uint8_t* opt = nt + 24;
    std::uint16_t magic; std::memcpy(&magic, opt, 2);
    if (magic != 0x20B) return false;
    std::memcpy(&pe.imageBase, opt + 24, 8);
    std::memcpy(&pe.sizeOfImage, opt + 56, 4);
    std::memcpy(&pe.importRva, opt + 112 + 8, 4); std::memcpy(&pe.importSize, opt + 112 + 12, 4);
    std::memcpy(&pe.pdataRva, opt + 112 + 24, 4); std::memcpy(&pe.pdataSize, opt + 112 + 28, 4);
    const std::uint8_t* sec = nt + 24 + optSize;
    bool text = false;
    pe.sections.clear();
    for (unsigned i = 0; i < nsec; ++i, sec += 40) {
        if ((const std::uint8_t*)(sec + 40) > f.data() + f.size()) return false;
        Section s;
        char nm[9] = {}; std::memcpy(nm, sec, 8); s.name = nm;
        std::memcpy(&s.vsize, sec + 8, 4); std::memcpy(&s.rva, sec + 12, 4);
        std::memcpy(&s.rawSize, sec + 16, 4); std::memcpy(&s.raw, sec + 20, 4);
        std::memcpy(&s.flags, sec + 36, 4);
        if ((std::uint64_t)s.raw + s.rawSize > f.size()) s.rawSize = s.raw < f.size() ? (std::uint32_t)(f.size() - s.raw) : 0;
        pe.sections.push_back(s);
        if (!text && s.name == ".text") {
            pe.textRva = s.rva; pe.textRaw = s.raw;
            pe.textSize = s.vsize > s.rawSize ? s.rawSize : s.vsize;
            text = true;
        }
    }
    pe.base = f.data(); pe.size = f.size();
    return text;
}

const Section* SectionOf(const Pe& pe, std::uint32_t rva) {
    for (const auto& s : pe.sections)
        if (rva >= s.rva && rva < s.rva + (s.vsize > s.rawSize ? s.vsize : s.rawSize)) return &s;
    return nullptr;
}

const std::uint8_t* At(const Pe& pe, std::uint32_t rva, std::size_t n) {
    const Section* s = SectionOf(pe, rva);
    if (!s) return nullptr;
    const std::uint32_t off = rva - s->rva;
    if ((std::uint64_t)off + n > s->rawSize) return nullptr;
    return pe.base + s->raw + off;
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

std::string ExeBuild(const std::wstring& path) { return ExeBuildImpl(path); }

std::vector<std::uint32_t> HandlersNaming(const Pe& pe, const std::map<std::uint32_t, std::uint32_t>& byOp, const char* text) {
    std::vector<std::uint32_t> out;
    const std::size_t tl = std::strlen(text);
    std::uint32_t str = 0;
    for (const auto& s : pe.sections) {
        if (s.name != ".rdata") continue;
        const std::uint8_t* b = pe.base + s.raw;
        for (std::size_t i = 0; i + tl + 1 <= s.rawSize; ++i) {
            if (b[i] != (std::uint8_t)text[0] || std::memcmp(b + i, text, tl) != 0 || b[i + tl] != 0) continue;
            std::size_t st = i;
            while (st > 0 && b[st - 1] != 0) --st;                 // the whole string ends in the text
            if (str) return out;                                   // more than one: not checkable
            str = s.rva + (std::uint32_t)st;
        }
    }
    if (!str) return out;
    std::map<std::uint32_t, std::uint32_t> seen;
    for (const auto& kv : byOp) {
        const std::uint8_t* h = At(pe, kv.second, 0x400);
        if (!h) continue;
        for (std::size_t i = 0; i + 7 <= 0x400; ++i) {
            if ((h[i] == 0x48 || h[i] == 0x4C) && h[i + 1] == 0x8D && (h[i + 2] & 0xC7) == 0x05) {
                std::int32_t rel; std::memcpy(&rel, h + i + 3, 4);
                if ((std::int64_t)kv.second + (std::int64_t)i + 7 + rel == (std::int64_t)str) { out.push_back(kv.first); break; }
            }
        }
    }
    return out;
}

static const std::vector<Found>& RunLocked(const std::wstring& exePath, const std::wstring& opcodesJson, bool needLabel) {
    // the build label the launcher keeps beside the table
    const std::size_t slash = opcodesJson.find_last_of(L"\\/");
    const std::wstring buildTxt = (slash == std::wstring::npos ? std::wstring() : opcodesJson.substr(0, slash + 1)) + L"client_version.txt";
    const std::wstring key = FileKey(exePath) + L"#" + FileKey(opcodesJson) + L"#" + FileKey(buildTxt) + (needLabel ? L"" : L"#nolabel");
    if (!g_cache.key.empty() && g_cache.key == key) return g_cache.found;
    g_cache = Cache{}; g_cache.key = key;
    std::ostringstream rep;
    std::vector<std::uint8_t> f; Pe pe{};
    std::map<std::uint32_t, std::uint32_t> handlers; std::map<std::string, std::uint32_t> ops;
    std::string clockLine;
    auto give_up = [&](const std::string& why) -> const std::vector<Found>& {
        g_cache.why = why;
        g_cache.report = "calibrate: " + why + "\n" + clockLine;
        return g_cache.found;
    };
    if (!ReadFile(exePath, f) || !ParsePe(f, pe)) return give_up("client exe not readable");
    g_cache.table.exeStamp = pe.stamp;
    {   // the engine clock comes from code, not from the operation table
        std::string why;
        Found fd{ "kEngineClock", 0, EngineClock(pe, why), "engine clock reads", Outcome::NotFound };
        fd.status = fd.found ? Outcome::Found : Outcome::NotFound;
        char line[160];
        if (fd.found) std::snprintf(line, sizeof(line), "  kEngineClock: root global + 0x%X (from engine clock reads)\n", fd.found);
        else std::snprintf(line, sizeof(line), "  kEngineClock: not found (%s)\n", why.c_str());
        clockLine = line;
        g_cache.found.push_back(fd);
    }
    if (!Handlers(pe, handlers)) return give_up("operation registrar not recognised");
    g_cache.table.handlers = (int)handlers.size();
    if (!OpTable(opcodesJson, ops)) return give_up("operation table (cs2\\opcodes.json) not readable");
    g_cache.table.names = (int)ops.size();
    const std::string mismatch = TableBuildMismatch(exePath, opcodesJson, buildTxt, g_cache.table);
    if (needLabel && !mismatch.empty()) return give_up(mismatch);
    std::string spotFail;
    for (const Spot& sp : kSpots) {
        SpotCheck sc; sc.text = sp.text; sc.op = sp.op;
        auto o = ops.find(sp.op);
        const std::vector<std::uint32_t> owners = HandlersNaming(pe, handlers, sp.text);
        for (std::size_t i = 0; i < owners.size(); ++i) sc.owners += (i ? "," : "") + std::to_string(owners[i]);
        if (o != ops.end()) sc.number = (int)o->second;
        if (o == ops.end() || owners.empty()) sc.ok = -1;
        else sc.ok = std::find(owners.begin(), owners.end(), o->second) != owners.end() ? 1 : 0;
        if (sc.ok == 0 && spotFail.empty())
            spotFail = std::string("operation table names ") + sp.op + " " + std::to_string(sc.number) +
                       ", the handler naming " + sp.text + " is op " + sc.owners;
        g_cache.spots.push_back(sc);
    }
    if (!spotFail.empty()) return give_up(spotFail);
    g_cache.table.usable = true;
    rep << "calibrate: " << handlers.size() << " handlers, " << ops.size() << " named operations" << (mismatch.empty() ? "" : " (" + mismatch + ")") << "\n" << clockLine;
    // handler entry points in address order: a handler's scan stops where the next one begins
    std::vector<std::uint32_t> starts;
    for (const auto& kv : handlers) starts.push_back(kv.second);
    std::sort(starts.begin(), starts.end());
    for (const Rule& r : kRules) {
        Found fd{ r.name, r.compiled, 0, r.op, Outcome::NotFound };
        fd.ref = r.compiled != 0;
        fd.what = r.what;
        auto o = ops.find(r.op);
        if (o == ops.end()) { fd.status = Outcome::NoOp; rep << "  " << r.name << ": operation " << r.op << " not in the table\n"; g_cache.found.push_back(fd); continue; }
        auto h = handlers.find(o->second);
        if (h == handlers.end()) { fd.status = Outcome::NoHandler; rep << "  " << r.name << ": no handler for " << r.op << " (number " << o->second << ")\n"; g_cache.found.push_back(fd); continue; }
        auto next = std::upper_bound(starts.begin(), starts.end(), h->second);
        std::uint32_t at = h->second, end = next != starts.end() ? *next : pe.textRva + pe.textSize;
        if (r.wrap) {
            // the shape sits in the callback the wrapper hands on; a handler that is no wrapper is read as is
            if (const std::uint32_t cb = WrapperCallback(pe, at)) { at = cb; end = cb + 0x140; }
        }
        bool ambiguous = false;
        fd.found = FindDisp(pe, at, end, r, ambiguous);
        fd.status = ambiguous ? Outcome::Ambiguous : fd.found ? Outcome::Found : Outcome::NotFound;
        if (fd.ref && fd.status == Outcome::Found && fd.found != fd.compiled) fd.status = Outcome::Moved;
        char line[200];
        if (ambiguous) std::snprintf(line, sizeof(line), "  %s: not found, more than one candidate (from %s)\n", r.name, r.op);
        else if (fd.ref) std::snprintf(line, sizeof(line), "  %s: %s0x%X (from %s; compiled 0x%X)\n", r.name, fd.found ? "" : "not found ", fd.found, r.op, fd.compiled);
        else std::snprintf(line, sizeof(line), "  %s: %s0x%X (from %s)\n", r.name, fd.found ? "" : "not found, wanted ", fd.found, r.op);
        rep << line;
        g_cache.found.push_back(fd);
    }
    {   // the varp hash map, derived from the varp manager the QUEST_FINISHED handler names
        Found fd{ "kOffVarpHash", 0, 0, "QUEST_FINISHED (varp manager + 0x1C0C8)", Outcome::NotFound };
        for (const auto& f : g_cache.found)
            if (std::strcmp(f.name, "md::kVarpMgr") == 0 && f.found) { fd.found = f.found + kVarpHashFromMgr; fd.status = Outcome::Found; }
        char line[160];
        if (fd.found) std::snprintf(line, sizeof(line), "  kOffVarpHash: 0x%X (varp manager + 0x%X)\n", fd.found, kVarpHashFromMgr);
        else std::snprintf(line, sizeof(line), "  kOffVarpHash: not found (the varp manager was not)\n");
        rep << line;
        g_cache.found.push_back(fd);
    }
    g_cache.report = rep.str();
    return g_cache.found;
}

const std::vector<Found>& Run(const std::wstring& exePath, const std::wstring& opcodesJson) {
    std::lock_guard<std::mutex> lk(g_mu);
    return RunLocked(exePath, opcodesJson, true);
}

std::uint32_t Use(const char* name, std::uint32_t compiled) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& fd : g_cache.found) {
        if (std::strcmp(fd.name, name) != 0) continue;
        fd.compiled = compiled;
        fd.applied = true;
        if (fd.status == Outcome::Found && fd.found != compiled) fd.status = Outcome::Moved;
        else if (fd.status == Outcome::Moved && fd.found == compiled) fd.status = Outcome::Found;
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
            std::snprintf(line, sizeof(line), "  %s MOVED: compiled 0x%X, client 0x%X, %s\n", fd.name, fd.compiled, fd.found,
                          fd.ref && !fd.applied ? "the compiled copy still reads the old one" : "using the client's");
            r += line;
        }
    return r;
}

std::vector<Found> Results() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Found> out;
    for (const auto& fd : g_cache.found) if (!fd.ref || fd.applied) out.push_back(fd);
    return out;
}

std::vector<Found> References() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Found> out;
    for (const auto& fd : g_cache.found) if (fd.ref && !fd.applied) out.push_back(fd);
    return out;
}

std::string CheckText(const std::wstring& exePath, const std::wstring& opcodesJson) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cache = Cache{};
    RunLocked(exePath, opcodesJson, false);
    std::ostringstream o;
    o << "exe stamp " << std::hex << g_cache.table.exeStamp << std::dec << ", version " << g_cache.table.exeVersion
      << ", table label " << (g_cache.table.label.empty() ? "(none)" : g_cache.table.label) << "\n";
    o << "table " << (g_cache.table.usable ? "usable" : "NOT usable: " + g_cache.why) << ", " << g_cache.table.handlers
      << " handlers, " << g_cache.table.names << " named operations\n";
    for (const auto& s : g_cache.spots)
        o << "spot " << s.op << " = " << s.number << ", handlers naming " << s.text << ": " << s.owners
          << (s.ok == 1 ? " (agrees)" : s.ok == 0 ? " (DISAGREES)" : " (not checkable)") << "\n";
    static const char* kStatus[] = { "found", "MOVED", "not found", "ambiguous", "no op", "no handler" };
    for (const auto& fd : g_cache.found) {
        char line[220];
        std::snprintf(line, sizeof(line), "%-18s %-34s %-10s found 0x%X", fd.name, fd.op, kStatus[(int)fd.status], fd.found);
        o << line;
        if (fd.ref) { std::snprintf(line, sizeof(line), " compiled 0x%X", fd.compiled); o << line; }
        if (fd.what) o << " (" << fd.what << ")";
        o << "\n";
    }
    return o.str();
}

std::string Why() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.why;
}

TableState Table() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.table;
}

std::vector<SpotCheck> SpotChecks() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.spots;
}

std::map<std::uint32_t, std::uint32_t> ExeHandlers(const std::wstring& exePath) {
    std::map<std::uint32_t, std::uint32_t> out;
    std::vector<std::uint8_t> f; Pe pe{};
    if (!ReadFile(exePath, f) || !ParsePe(f, pe) || !Handlers(pe, out)) out.clear();
    return out;
}

}  // namespace rtx::calib
