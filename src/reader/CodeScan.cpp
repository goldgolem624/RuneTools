#include "CodeScan.h"
#include "Calibrate.h"
#include "Pins.h"
#include "../../companion/Signatures.h"
#include "../../companion/MainDataOffsets.h"
#include "../../companion/ServerOps.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

namespace rtx::codescan {
namespace {

using rtx::health::kFail;
using rtx::health::kPass;
using rtx::health::kWarn;
using rtx::health::kUnchecked;
using rtx::health::Hex;

constexpr std::uint32_t kScnWrite = 0x80000000u, kScnExec = 0x20000000u;

// What the reader and the launcher compiled in for the build-specific addresses; the manifest
// holds the value per validated exe.
constexpr std::uint32_t kCompiledOpTable = 0xC70BB0;

// Displacements the hooks read out of the code around their hit, as compiled into the companion:
// the menu's targets vector (begin, end), the player display method's actor field.
constexpr std::uint32_t kMenuTargetsBegin = 0x1380, kMenuTargetsEnd = 0x1388, kPlayerDisplayField = 0x1078;

// Root fields the compiled tables do not carry, so a shift report can still name them.
struct RootField { std::uint32_t off; const char* name; };
constexpr RootField kRootFields[] = {
    { 0x18D68, "ConfigManager" }, { 0x198B8, "Connection" }, { 0x19910, "LoginManager" }, { 0x19930, "EntityLookup" },
    { 0x19980, "ScriptRunner" }, { 0x19998, "TrackerValues" }, { 0x19A48, "ServerTimeOffset" }, { 0x535C8, "WorldInfo" },
};

// Prefix a non-pass detail with its word when the branch gave it none.
void Word(const char* w, std::string& d) {
    static const char* const kWords[] = { "MOVED", "FORMAT", "GONE", "NEW", "UNVERIFIED" };
    for (const char* k : kWords) if (d.rfind(k, 0) == 0) return;
    d = std::string(w) + ": " + d;
}

using rtx::sig::kSoundSites;

// Engine ops the companion calls by name, with the ints it pushes where the handler's pop of the
// int stack is checked (-1: not checked, the call goes through a wrapper).
struct NamedOp { const char* name; int ints; const char* features; };
constexpr NamedOp kNamedOps[] = {
    { "SOUND_SYNTH", 3, "Alerts: game sounds" }, { "VIEWPORT_SETZOOM", 2, "Camera zoom" }, { "VIEWPORT_SETFOV", 2, "Camera field of view" },
    { "ACHIEVEMENT_REQSTATE", 1, "Asks: achievements" }, { "ACHIEVEMENT_ALLPREREQMET", 1, "Asks: achievements" },
    { "QUEST_FINISHED", 1, "Asks: quests" }, { "QUEST_STARTED", 1, "Asks: quests" }, { "QUEST_STATREQ_COUNT", 1, "Asks: quests" },
    { "QUEST_STATREQ_STAT", 2, "Asks: quests" }, { "QUEST_STATREQ_LEVEL", 2, "Asks: quests" }, { "QUEST_QUESTREQ_COUNT", 1, "Asks: quests" },
    { "QUEST_QUESTREQ", 2, "Asks: quests" }, { "QUEST_POINTSREQ", 1, "Asks: quests" }, { "QUEST_GETDIFFICULTY", 1, "Asks: quests" },
    { "ACHIEVEMENT_ACHIEVEMENT_REQ_COUNT", 1, "Asks: achievements" }, { "ACHIEVEMENT_TOTAL_RUNESCORE", 0, "Asks: achievements" },
    { "ACHIEVEMENT_FINDGRACED", 0, "Asks: achievements" }, { "ACHIEVEMENT_FINDNEXT", 0, "Asks: achievements" },
    { "IF_CREATECHILD", -1, "In-frame panels" }, { "CC_SETPOSITION", -1, "In-frame panels" }, { "CC_SETSIZE", -1, "In-frame panels" },
    { "CC_SETTEXT", -1, "In-frame panels" }, { "CC_SETTEXTFONT", -1, "In-frame panels" }, { "CC_SETCOLOUR", -1, "In-frame panels" },
    { "CC_SETHIDE", -1, "In-frame panels" }, { "CC_DELETEALL", -1, "In-frame panels" }, { "CC_GETTEXT", -1, "In-frame panels" },
    { "IF_SETPOSITION", -1, "In-frame panels" }, { "IF_SETSIZE", -1, "In-frame panels" }, { "IF_SETTEXT", -1, "In-frame panels" },
    { "IF_SETCOLOUR", -1, "In-frame panels" }, { "IF_SETHIDE", -1, "In-frame panels" }, { "DBQUERY_EXECUTE_COUNT", -1, "In-frame panels" },
};

// Loader exports the Vulkan compositor detours; the game must import each for the detour to see its calls.
const char* const kVkImports[] = {
    "vkGetDeviceProcAddr", "vkGetInstanceProcAddr", "vkAllocateMemory", "vkCreateBuffer", "vkCreateImage", "vkMapMemory",
    "vkGetPhysicalDeviceMemoryProperties", "vkGetPhysicalDeviceProperties",
};

struct Image {
    std::wstring key;
    std::vector<std::uint8_t> file;
    rtx::calib::Pe pe;
    bool ok = false;
    std::string error;
    std::map<std::uint32_t, std::uint32_t> handlers;   // op -> rva
    std::vector<std::uint32_t> starts;                // handler rvas, sorted
    std::vector<std::pair<std::uint32_t, std::uint32_t>> funcs;
    std::map<std::uint32_t, std::uint32_t> unwindOf;  // function start -> unwind info rva
    std::vector<std::string> imports;                 // "dll!name", lower-case dll
    bool vulkan = false, opengl = false;
    std::string version;
};

std::mutex g_mu;
std::shared_ptr<Image> g_image;

std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }

std::string Hex8(std::uint32_t v) { char b[12]; std::snprintf(b, sizeof(b), "%08x", v); return b; }

const char* CStrAt(const Image& im, std::uint32_t rva) {
    const std::uint8_t* p = rtx::calib::At(im.pe, rva, 1);
    return p ? reinterpret_cast<const char*>(p) : nullptr;
}

void LoadImports(Image& im) {
    if (!im.pe.importRva) return;
    for (std::uint32_t d = im.pe.importRva; ; d += 20) {
        const std::uint8_t* e = rtx::calib::At(im.pe, d, 20);
        if (!e) break;
        std::uint32_t ilt, name, iat;
        std::memcpy(&ilt, e, 4); std::memcpy(&name, e + 12, 4); std::memcpy(&iat, e + 16, 4);
        if (!name) break;
        const char* dll = CStrAt(im, name);
        if (!dll) break;
        const std::string dl = Lower(dll);
        if (dl == "vulkan-1.dll") im.vulkan = true;
        if (dl == "opengl32.dll") im.opengl = true;
        std::uint32_t th = ilt ? ilt : iat;
        for (int k = 0; k < 4096; ++k, th += 8) {
            const std::uint8_t* t = rtx::calib::At(im.pe, th, 8);
            if (!t) break;
            std::uint64_t v; std::memcpy(&v, t, 8);
            if (!v) break;
            if (v & 0x8000000000000000ull) continue;        // by ordinal
            const char* fn = CStrAt(im, (std::uint32_t)v + 2);
            if (fn) im.imports.push_back(dl + "!" + fn);
        }
    }
}

std::shared_ptr<Image> Load(const std::wstring& exe) {
    const std::wstring key = rtx::calib::FileKey(exe);
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_image && !key.empty() && g_image->key == key) return g_image;
    auto im = std::make_shared<Image>();
    im->key = key;
    if (key.empty() || !rtx::calib::ReadFile(exe, im->file) || !rtx::calib::ParsePe(im->file, im->pe)) {
        im->error = "client exe not readable";
        g_image = im;
        return im;
    }
    im->ok = true;
    im->version = rtx::calib::ExeBuild(exe);
    if (!rtx::calib::Handlers(im->pe, im->handlers)) im->handlers.clear();
    for (const auto& kv : im->handlers) im->starts.push_back(kv.second);
    std::sort(im->starts.begin(), im->starts.end());
    for (std::uint32_t o = 0; o + 12 <= im->pe.pdataSize; o += 12) {
        const std::uint8_t* e = rtx::calib::At(im->pe, im->pe.pdataRva + o, 12);
        if (!e) break;
        std::uint32_t b, en, u; std::memcpy(&b, e, 4); std::memcpy(&en, e + 4, 4); std::memcpy(&u, e + 8, 4);
        if (b) { im->funcs.emplace_back(b, en); im->unwindOf[b] = u; }
    }
    std::sort(im->funcs.begin(), im->funcs.end());
    LoadImports(*im);
    g_image = im;
    return im;
}

std::uint32_t FuncOf(const Image& im, std::uint32_t rva) {
    auto it = std::upper_bound(im.funcs.begin(), im.funcs.end(), std::make_pair(rva, 0xFFFFFFFFu));
    if (it == im.funcs.begin()) return rva;
    --it;
    return (rva >= it->first && rva < it->second) ? it->first : rva;
}

// The function holding rva, followed through chained unwind entries to its own start, as the
// companion resolves a kInFunction signature; 0 when no entry holds it.
std::uint32_t FunctionOf(const Image& im, std::uint32_t rva) {
    auto it = std::upper_bound(im.funcs.begin(), im.funcs.end(), std::make_pair(rva, 0xFFFFFFFFu));
    if (it == im.funcs.begin()) return 0;
    --it;
    if (rva < it->first || rva >= it->second) return 0;
    std::uint32_t b = it->first;
    auto u = im.unwindOf.find(b);
    std::uint32_t info = u == im.unwindOf.end() ? 0 : u->second;
    for (int guard = 0; guard < 8 && info; ++guard) {
        const std::uint8_t* chained = nullptr;
        if (info & 1) chained = rtx::calib::At(im.pe, info & ~1u, 12);
        else {
            const std::uint8_t* x = rtx::calib::At(im.pe, info, 4);
            if (!x) return 0;
            if (!((x[0] >> 3) & 4)) break;                 // not chained
            chained = rtx::calib::At(im.pe, info + 4 + ((x[2] + 1u) & ~1u) * 2, 12);
        }
        if (!chained) return 0;
        std::memcpy(&b, chained, 4); std::memcpy(&info, chained + 8, 4);
    }
    return b;
}

bool InWritableData(const Image& im, std::uint32_t rva) {
    const rtx::calib::Section* s = rtx::calib::SectionOf(im.pe, rva);
    return s && (s->flags & kScnWrite) && !(s->flags & kScnExec);
}

bool InData(const Image& im, std::uint32_t rva) {
    const rtx::calib::Section* s = rtx::calib::SectionOf(im.pe, rva);
    return s && !(s->flags & kScnExec);
}

// A pattern as ints, -1 = any byte. Byte forms take their mask; the first byte is always fixed.
std::vector<int> PatternOf(const rtx::sig::Sig& s) {
    std::vector<int> v(s.len);
    for (std::size_t j = 0; j < s.len; ++j) {
        if (s.ints) v[j] = s.ints[j];
        else v[j] = (!s.mask || s.mask[j] || j == 0) ? (int)s.bytes[j] : -1;
    }
    return v;
}

std::vector<int> PatternOf(const unsigned char* b, std::size_t n) {
    std::vector<int> v(n);
    for (std::size_t j = 0; j < n; ++j) v[j] = b[j];
    return v;
}

std::vector<int> PatternOf(const int* b, std::size_t n) { return std::vector<int>(b, b + n); }

bool MatchAt(const std::uint8_t* p, const std::vector<int>& pat) {
    for (std::size_t k = 0; k < pat.size(); ++k)
        if (pat[k] >= 0 && p[k] != (std::uint8_t)pat[k]) return false;
    return true;
}

// Every hit of `pat` in the executable sections (the first one only when `firstExec`), searched by
// its longest fixed run and verified in full.
std::vector<std::uint32_t> FindAll(const Image& im, const std::vector<int>& pat, bool firstExec, std::size_t cap = 32) {
    std::vector<std::uint32_t> hits;
    std::size_t bestAt = 0, bestLen = 0;
    for (std::size_t i = 0; i < pat.size(); ) {
        if (pat[i] < 0) { ++i; continue; }
        std::size_t j = i;
        while (j < pat.size() && pat[j] >= 0) ++j;
        if (j - i > bestLen) { bestLen = j - i; bestAt = i; }
        i = j;
    }
    if (!bestLen) return hits;
    std::string needle;
    for (std::size_t k = 0; k < bestLen; ++k) needle.push_back((char)(std::uint8_t)pat[bestAt + k]);
    const std::boyer_moore_horspool_searcher<std::string::const_iterator> searcher(needle.begin(), needle.end());
    for (const auto& s : im.pe.sections) {
        if (!(s.flags & kScnExec)) continue;
        const char* b = reinterpret_cast<const char*>(im.pe.base + s.raw);
        const std::size_t n = std::min<std::size_t>(s.rawSize, s.vsize ? s.vsize : s.rawSize);
        const char* it = b;
        while (true) {
            auto r = std::search(it, b + n, searcher);
            if (r == b + n) break;
            const std::size_t at = (std::size_t)(r - b);
            if (at >= bestAt && at - bestAt + pat.size() <= n &&
                MatchAt(reinterpret_cast<const std::uint8_t*>(b + at - bestAt), pat)) {
                hits.push_back(s.rva + (std::uint32_t)(at - bestAt));
                if (hits.size() >= cap) return hits;
            }
            it = r + 1;
        }
        if (firstExec) break;
    }
    return hits;
}

std::int32_t I32(const Image& im, std::uint32_t rva) {
    const std::uint8_t* p = rtx::calib::At(im.pe, rva, 4);
    std::int32_t v = 0; if (p) std::memcpy(&v, p, 4);
    return v;
}
std::uint32_t U32(const Image& im, std::uint32_t rva) { return (std::uint32_t)I32(im, rva); }
const std::uint8_t* Bytes(const Image& im, std::uint32_t rva, std::size_t n) { return rtx::calib::At(im.pe, rva, n); }

// rva of the first match of `pat` in [from, from + span), or 0
std::uint32_t FindNear(const Image& im, std::uint32_t from, std::size_t span, const std::vector<int>& pat) {
    const std::uint8_t* p = Bytes(im, from, span + pat.size());
    if (!p) return 0;
    for (std::size_t o = 0; o < span; ++o) if (MatchAt(p + o, pat)) return from + (std::uint32_t)o;
    return 0;
}

std::vector<std::uint32_t> CallSitesTo(const Image& im, std::uint32_t target) {
    std::vector<std::uint32_t> out;
    for (const auto& s : im.pe.sections) {
        if (!(s.flags & kScnExec)) continue;
        const std::uint8_t* b = im.pe.base + s.raw;
        for (std::uint32_t i = 0; i + 5 <= s.rawSize; ++i) {
            if (b[i] != 0xE8) continue;
            std::int32_t rel; std::memcpy(&rel, b + i + 1, 4);
            if ((std::int64_t)s.rva + i + 5 + rel == (std::int64_t)target) out.push_back(s.rva + i + 5);
        }
    }
    return out;
}

// Displacements of [rcx+disp32] reads and writes in every op handler (rcx = MainData on entry),
// kept when at least two handlers use them: the shape of MainData as the client itself sees it.
std::set<std::uint32_t> HandlerDisps(const Image& im) {
    std::map<std::uint32_t, int> count;
    static const std::uint8_t kOps[] = { 0x8B, 0x8D, 0x89, 0x88, 0x39, 0x3B, 0x80, 0x81, 0x83, 0xC7, 0xC6, 0x63 };
    for (std::size_t i = 0; i < im.starts.size(); ++i) {
        const std::uint32_t h = im.starts[i];
        std::uint32_t end = i + 1 < im.starts.size() ? im.starts[i + 1] : h + 0x200;
        if (end > h + 0x200 || end <= h) end = h + 0x200;
        const std::uint8_t* b = Bytes(im, h, end - h);
        if (!b) continue;
        const std::size_t n = end - h;
        std::set<std::uint32_t> seen;
        for (std::size_t k = 0; k + 7 < n; ++k) {
            std::size_t j = k;
            if (b[j] >= 0x40 && b[j] <= 0x4F) ++j;
            std::uint8_t op = b[j];
            bool known = std::find(std::begin(kOps), std::end(kOps), op) != std::end(kOps);
            if (op == 0x0F && (b[j + 1] == 0xB6 || b[j + 1] == 0xB7 || b[j + 1] == 0xBE || b[j + 1] == 0xBF)) { ++j; known = true; }
            if (!known || j + 6 > n) continue;
            if ((b[j + 1] & 0xC7) != 0x81) continue;
            std::uint32_t d; std::memcpy(&d, b + j + 2, 4);
            if (d >= 0x100 && d < 0x60000) seen.insert(d);
        }
        for (std::uint32_t d : seen) ++count[d];
    }
    std::set<std::uint32_t> out;
    for (const auto& kv : count) if (kv.second >= 2) out.insert(kv.first);
    return out;
}

std::vector<std::uint32_t> ParseHexList(const std::string& s) {
    std::vector<std::uint32_t> v;
    std::size_t at = 0;
    while (at < s.size()) {
        std::size_t e = s.find(',', at);
        if (e == std::string::npos) e = s.size();
        if (e > at) v.push_back((std::uint32_t)std::strtoul(s.c_str() + at, nullptr, 0));
        at = e + 1;
    }
    return v;
}

// The handler's pop of the int stack: add dword [reg+0x10A0], -n ; sub dword [reg+0x10A0], n ;
// dec dword [reg+0x10A0]; or the pointer loaded into a register (mov r32,[reg+0x10A0], or
// [reg+0xFA0] off a base kept at state+0x100) and adjusted there (dec r32, sub r32,n, lea r32,[r32-n],
// add r32,-n) before the store. -1 when none is in the first bytes.
int PopCount(const Image& im, std::uint32_t h) {
    const std::uint8_t* b = Bytes(im, h, 0x70);
    if (!b) return -1;
    for (int k = 0; k + 7 <= 0x70; ++k) {
        int j = k;
        if ((b[j] & 0xF0) == 0x40) ++j;   // a REX prefix: the register halves only matter below
        if ((b[j] == 0x83 || b[j] == 0xFF) && (b[j + 1] & 0xC0) == 0x80 && b[j + 2] == 0xA0 && b[j + 3] == 0x10 && b[j + 4] == 0 && b[j + 5] == 0) {
            const int reg = (b[j + 1] >> 3) & 7;
            if (b[j] == 0xFF && reg == 1) return 1;                              // dec
            if (b[j] == 0x83 && reg == 0) return -(int)(std::int8_t)b[j + 6];   // add -n
            if (b[j] == 0x83 && reg == 5) return (int)(std::int8_t)b[j + 6];    // sub n
        }
        // mov r32,[reg+disp32] with disp 0x10A0 or 0xFA0, then the adjust within the next 30 bytes
        if (b[j] == 0x8B && (b[j + 1] & 0xC0) == 0x80 && (b[j + 1] & 7) != 4 &&
            ((b[j + 2] == 0xA0 && b[j + 3] == 0x10) || (b[j + 2] == 0xA0 && b[j + 3] == 0x0F)) && b[j + 4] == 0 && b[j + 5] == 0) {
            const int r = (b[j + 1] >> 3) & 7;
            for (int m = j + 6; m + 3 <= 0x70 && m < j + 30; ++m) {
                int q = m;
                if ((b[q] & 0xF0) == 0x40) ++q;
                if (b[q] == 0xFF && b[q + 1] == (0xC8 | r)) return 1;                                   // dec r32
                if (b[q] == 0x83 && b[q + 1] == (0xE8 | r)) return (int)(std::int8_t)b[q + 2];         // sub r32,n
                if (b[q] == 0x83 && b[q + 1] == (0xC0 | r)) return -(int)(std::int8_t)b[q + 2];        // add r32,-n
                if (b[q] == 0x8D && b[q + 1] == (0x40 | (r << 3) | r)) return -(int)(std::int8_t)b[q + 2];   // lea r32,[r32-n]
            }
        }
    }
    return -1;
}

// Setter wrapper: the routine the component setter hands its dispatcher (as the companion finds it).
std::uint32_t WrapperCallback(const Image& im, std::uint32_t h) {
    const std::uint8_t* p = Bytes(im, h, 0x40);
    if (!p) return 0;
    using namespace rtx::sig;
    if (p[0] == 0x40) { ++h; ++p; }
    if (std::memcmp(p, kCcWrapHead, sizeof(kCcWrapHead)) || std::memcmp(p + kCcWrapMidAt, kCcWrapMid, sizeof(kCcWrapMid)) ||
        std::memcmp(p + kCcWrapTailAt, kCcWrapTail, sizeof(kCcWrapTail))) return 0;
    std::int32_t drel; std::memcpy(&drel, p + 45, 4);
    const std::uint32_t disp = h + 49 + drel;
    static const std::vector<int> slots = PatternOf(kCcWrapSlots, sizeof(kCcWrapSlots) / sizeof(int));
    if (!FindNear(im, disp, 0x20, slots)) return 0;
    std::int32_t rel; std::memcpy(&rel, p + 25, 4);
    return h + 29 + rel;
}

std::vector<int> ParseFp(const char* s) {
    std::vector<int> v;
    for (const char* p = s; *p; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?') { v.push_back(-1); p += 2; continue; }
        char two[3] = { p[0], p[1], 0 };
        v.push_back((int)std::strtoul(two, nullptr, 16)); p += 2;
    }
    return v;
}

struct SigOut { int ok = kFail; std::string detail, got, exp; std::uint32_t rva = 0; };

// Where the companion attaches for a signature with its one hit, which is also what the manifest
// records: the hit, the function holding it, or the routine its call names. 0 when that is not there.
std::uint32_t HookPoint(const Image& im, const rtx::sig::Sig& s, std::uint32_t hit) {
    switch (s.hook) {
        case rtx::sig::kInFunction: return FunctionOf(im, hit);
        case rtx::sig::kAtCall: {
            const std::uint8_t* b = Bytes(im, hit + s.callAt, 5);
            if (!b || b[0] != 0xE8) return 0;
            return (std::uint32_t)((std::int64_t)hit + s.callAt + 5 + I32(im, hit + s.callAt + 1));
        }
        default: return hit;
    }
}

// The arrow manager's frame call site: both managers and the size of their slots.
struct ArrowSite { bool ok = false; std::uint32_t at = 0, arrow = 0, trail = 0, slot = 0; };
ArrowSite FindArrowSite(const Image& im) {
    ArrowSite a;
    const auto hits = FindAll(im, PatternOf(rtx::sig::kArrowFrame, sizeof(rtx::sig::kArrowFrame) / sizeof(int)), false);
    if (hits.size() != 1) return a;
    const std::uint8_t* b = Bytes(im, hits[0], 48);
    if (!b) return a;
    a.ok = true; a.at = hits[0];
    a.arrow = U32(im, hits[0] + (std::uint32_t)rtx::sig::kArrowFrameArrow);
    a.trail = U32(im, hits[0] + (std::uint32_t)rtx::sig::kArrowFrameTrail);
    a.slot = rtx::sig::ArrowSlotSize(b);
    return a;
}

// The server packet descriptor table as the file holds it: an inline-buffer vector {begin, end,
// capacity, ..., buffer at +0x28}. begin == self + 0x28 validates the shape; (capacity - begin) / 8
// is the number of descriptors, so the framer's bound is read without probing past the end.
struct DescTable { bool ok = false; std::uint32_t begin = 0, count = 0; };
DescTable ReadDescTable(const Image& im, std::uint32_t tblRva) {
    DescTable t;
    const std::uint8_t* p = Bytes(im, tblRva, 0x18);
    if (!p) return t;
    std::uint64_t b, e, c; std::memcpy(&b, p, 8); std::memcpy(&e, p + 8, 8); std::memcpy(&c, p + 16, 8);
    const std::uint64_t self = im.pe.imageBase + tblRva;
    if (b != self + 0x28 || c < b || (c - b) % 8 || (c - b) / 8 > 4096) return t;
    t.ok = true; t.begin = (std::uint32_t)(b - im.pe.imageBase); t.count = (std::uint32_t)((c - b) / 8);
    return t;
}

// Every instruction in the executable sections whose rel32 names an address in [lo, hi): the
// address of the instruction's rel32 field.
std::vector<std::uint32_t> RelRefsTo(const Image& im, std::uint32_t lo, std::uint32_t hi) {
    std::vector<std::uint32_t> out;
    for (const auto& s : im.pe.sections) {
        if (!(s.flags & kScnExec)) continue;
        const std::uint8_t* b = im.pe.base + s.raw;
        const std::size_t n = std::min<std::size_t>(s.rawSize, s.vsize ? s.vsize : s.rawSize);
        for (std::size_t i = 0; i + 7 <= n; ++i) {
            // lea/mov r64,[rip+disp32]: REX.W (0x48/0x4C) 8B|8D modrm(mod 0, rm 101)
            if ((b[i] != 0x48 && b[i] != 0x4C) || (b[i + 1] != 0x8B && b[i + 1] != 0x8D) || (b[i + 2] & 0xC7) != 0x05) continue;
            std::int32_t rel; std::memcpy(&rel, b + i + 3, 4);
            const std::int64_t target = (std::int64_t)s.rva + (std::int64_t)i + 7 + rel;
            if (target >= lo && target < hi) out.push_back(s.rva + (std::uint32_t)i + 3);
        }
    }
    return out;
}

// The wire lengths of every server opcode, from the descriptor initializers in the file: each is a
// call to the descriptor constructor with the opcode in edx and the length in r8d. The constructor is
// the routine, other than the framer, that both references the descriptor table and is called a few
// hundred times. Empty when the shape is not found.
std::map<int, int> StaticPacketLengths(const Image& im, std::uint32_t tblRva, std::uint32_t framerFn) {
    std::map<int, int> out;
    std::set<std::uint32_t> fns;
    for (std::uint32_t at : RelRefsTo(im, tblRva, tblRva + 0x28)) {
        const std::uint32_t f = FunctionOf(im, at);
        if (f && f != framerFn) fns.insert(f);
    }
    std::uint32_t ctor = 0; std::vector<std::uint32_t> sites;
    for (std::uint32_t f : fns) {
        auto s = CallSitesTo(im, f);
        if (s.size() >= 100 && s.size() > sites.size()) { ctor = f; sites = std::move(s); }
    }
    if (!ctor) return out;
    for (std::uint32_t ret : sites) {
        // the setups sit within the 40 bytes before the call; the last of each register wins
        constexpr std::uint32_t kWin = 40;
        const std::uint32_t from = ret - 5 - kWin;
        const std::uint8_t* b = Bytes(im, from, kWin);
        if (!b) continue;
        int op = -1, len = -1; bool haveOp = false, haveLen = false, lenFromOp = false, opFromR8 = false; std::int32_t d = 0;
        // a match steps over its immediate: `mov edx,0xBA` carries the byte 0xBA inside its operand
        for (std::uint32_t k = 0; k + 2 <= kWin; ++k) {
            if (b[k] == 0xBA && k + 5 <= kWin) { std::int32_t v; std::memcpy(&v, b + k + 1, 4); op = v; haveOp = true; opFromR8 = false; k += 4; continue; }             // mov edx,imm32
            if (b[k] == 0x33 && b[k + 1] == 0xD2) { op = 0; haveOp = true; opFromR8 = false; k += 1; continue; }                                                           // xor edx,edx
            if (b[k] == 0x41 && b[k + 1] == 0xB8 && k + 6 <= kWin) { std::int32_t v; std::memcpy(&v, b + k + 2, 4); len = v; haveLen = true; lenFromOp = false; k += 5; continue; }   // mov r8d,imm32
            if (b[k] == 0x44 && b[k + 1] == 0x8D && b[k + 2] == 0x42 && k + 4 <= kWin) { d = (std::int8_t)b[k + 3]; haveLen = true; lenFromOp = true; k += 3; continue; }       // lea r8d,[rdx+d8]
            if (b[k] == 0x44 && b[k + 1] == 0x8D && b[k + 2] == 0x82 && k + 7 <= kWin) { std::memcpy(&d, b + k + 3, 4); haveLen = true; lenFromOp = true; k += 6; continue; }    // lea r8d,[rdx+d32]
            if (b[k] == 0x45 && b[k + 1] == 0x33 && b[k + 2] == 0xC0) { len = 0; haveLen = true; lenFromOp = false; k += 2; continue; }                                    // xor r8d,r8d
            if (b[k] == 0x41 && b[k + 1] == 0x8D && b[k + 2] == 0x50 && k + 4 <= kWin) { op = (std::int8_t)b[k + 3]; haveOp = true; opFromR8 = true; k += 3; continue; }    // lea edx,[r8+d8]
            if (b[k] == 0x41 && b[k + 1] == 0x8D && b[k + 2] == 0x90 && k + 7 <= kWin) { std::int32_t v; std::memcpy(&v, b + k + 3, 4); op = v; haveOp = true; opFromR8 = true; k += 6; continue; }   // lea edx,[r8+d32]
        }
        if (!haveOp || !haveLen) continue;
        if (opFromR8 && len != 0) continue;             // lea edx,[r8+d] only means d when r8d is 0
        if (lenFromOp) len = op + d;
        if (op < 0 || op > 4095 || len < -2 || len > 100000) continue;
        if (!out.count(op)) out[op] = len;
    }
    return out;
}

}  // namespace

std::vector<std::uint32_t> SoundCallSites(const std::wstring& exePath, std::uint32_t playRva) {
    auto im = Load(exePath);
    if (!im->ok || !playRva) return {};
    std::vector<std::uint32_t> sites = CallSitesTo(*im, playRva);
    std::sort(sites.begin(), sites.end());
    return sites;
}

ExeFacts Facts(const std::wstring& exePath) {
    ExeFacts f;
    auto im = Load(exePath);
    if (!im->ok) return f;
    f.ok = true;
    f.stamp = im->pe.stamp;
    f.sizeOfImage = im->pe.sizeOfImage;
    f.flavour = im->vulkan ? "vulkan" : im->opengl ? "opengl" : "unknown";
    f.version = im->version;
    for (const auto& s : rtx::sig::kTable) {
        if (std::strcmp(s.name, "framer") != 0) continue;
        auto hits = FindAll(*im, PatternOf(s), s.scope == rtx::sig::kFirstExec);
        if (hits.size() != 1) break;
        const std::uint32_t cmp = FindNear(*im, hits[0], 0x400, { 0x81, 0xFA, -1, -1, -1, -1, 0x0F, 0x83, -1, -1, -1, -1, 0x48, 0x8B, 0x05 });
        if (!cmp) break;
        f.opMax = (int)U32(*im, cmp + 2) - 1;
        f.optableRva = (std::uint32_t)((std::int64_t)cmp + 12 + 7 + I32(*im, cmp + 15));
    }
    return f;
}

void Check(const std::wstring& exePath, rtx::health::Run& run) {
    const char* G = "Client code";
    auto im = Load(exePath);
    if (!im->ok) {
        run.Add(G, "code.exe", "Client exe", kFail, "GONE: " + im->error, "Every code check");
        return;
    }
    const Image& I = *im;
    const std::uint32_t stamp = I.pe.stamp;
    const bool vulkan = I.vulkan;
    const std::string st = Hex8(stamp);
    run.Add(G, "code.exe", "Client exe", kPass, (vulkan ? "Vulkan" : I.opengl ? "OpenGL" : "unknown renderer") + std::string(" exe, stamp ") + st +
            ", " + std::to_string(I.handlers.size()) + " engine ops");
    run.Fact("exe.stamp", st);
    run.Fact("exe.size", Hex(I.pe.sizeOfImage));
    run.Fact("exe.handlers", std::to_string(I.handlers.size()));

    // signatures, anchors first
    std::uint32_t framerRva = 0, synthRva = 0;
    const ArrowSite arrowSite = FindArrowSite(I);   // the message routines must name its managers
    for (const auto& s : rtx::sig::kTable) {
        const std::vector<int> pat = PatternOf(s);
        const auto hits = FindAll(I, pat, s.scope == rtx::sig::kFirstExec);
        // what the manifest recorded for this exe ("n@rva"): another count means the signature or the manifest changed
        const rtx::pins::Line pin0 = rtx::pins::Find("sig", s.name);
        const std::string was0 = pin0.kind.empty() ? std::string() : rtx::pins::StampValue(pin0.expect, stamp);
        const int wasHits = was0.empty() ? -1 : std::atoi(was0.c_str());
        auto recorded = [&](int& ok, std::string& exp, std::string& d) {
            if (wasHits < 0 || wasHits == (int)hits.size()) return;
            const std::string n = std::to_string(wasHits) + (wasHits == 1 ? " hit" : " hits");
            ok = kFail;
            exp = n + " (recorded for this exe)";
            d += "; the manifest recorded " + n + " for this exe: the signature or the manifest changed";
            Word((int)hits.size() > wasHits ? "NEW" : "GONE", d);
        };
        const std::string id = std::string("code.sig.") + s.name;
        const std::string key = std::string("Signature ") + s.name;
        run.Fact(std::string("sig.") + s.name + ".hits", std::to_string(hits.size()));
        const std::uint32_t hook0 = hits.size() == 1 ? HookPoint(I, s, hits[0]) : hits.empty() ? 0 : hits[0];
        if (!hits.empty()) run.Fact(std::string("sig.") + s.name + ".rva", Hex(hook0 ? hook0 : hits[0]));
        if (hits.size() == 1 && hook0 != hits[0]) run.Fact(std::string("sig.") + s.name + ".hit", Hex(hits[0]));
        int ok = kPass; std::string d, exp = "1 hit", got = std::to_string(hits.size()) + (hits.size() == 1 ? " hit" : " hits");
        const bool openGlOnly = s.expect == rtx::sig::kOpenGlOnce;
        if (openGlOnly && vulkan) {
            exp = "0 hits on Vulkan";
            ok = hits.empty() ? kPass : kWarn;
            d = hits.empty() ? "absent on Vulkan, as expected" : "NEW: found on Vulkan (unexpected)";
            recorded(ok, exp, d);
            run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
            continue;
        }
        if (s.expect == rtx::sig::kSameTarget) {
            exp = "1 or more hits naming one routine";
            std::set<std::uint32_t> targets;
            for (std::uint32_t h : hits) targets.insert((std::uint32_t)((std::int64_t)h + s.len + 4 + I32(I, h + (std::uint32_t)s.len)));
            if (hits.empty()) { ok = kFail; d = "GONE: 0 hits: pattern moved"; }
            else if (targets.size() != 1) { ok = kFail; d = "NEW: " + std::to_string(hits.size()) + " hits name " + std::to_string(targets.size()) + " routines"; }
            else { d = std::to_string(hits.size()) + " hits, routine " + Hex(*targets.begin()); run.Fact(std::string("sig.") + s.name + ".target", Hex(*targets.begin())); }
            recorded(ok, exp, d);
            run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
            continue;
        }
        if (hits.size() != 1) {
            ok = kFail;
            d = hits.empty() ? "GONE: 0 hits: pattern moved" : "NEW: " + std::to_string(hits.size()) + " hits: ambiguous, the hook refuses it";
            recorded(ok, exp, d);
            run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
            continue;
        }
        const std::uint32_t at = hits[0];
        const std::uint32_t where = hook0;   // the hook point
        const std::uint32_t fn = s.hook == rtx::sig::kInFunction ? where :
            (s.scope == rtx::sig::kFirstExec && s.bytes && (!s.mask || s.hook == rtx::sig::kHitInFunction)) ? FuncOf(I, at) : at;
        d = "1 hit at " + Hex(at) + (fn != at ? " (function " + Hex(fn) + ")" : "") + (s.hook == rtx::sig::kAtCall ? " (calls " + Hex(where) + ")" : "");
        if (!where) {
            ok = kFail;
            d += s.hook == rtx::sig::kAtCall ? "; no call where the routine should be named" : "; no function holds it";
            Word("GONE", d);
            recorded(ok, exp, d);
            run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
            continue;
        }
        // the values the hook takes out of the code around the hit
        const std::string nm = s.name;
        if (nm == "main-anchor") {
            const std::uint32_t f0 = at - 32;
            const std::uint32_t mov = FindNear(I, f0, 0x200, { 0x48, 0x89, 0x05 });
            const std::uint32_t g = mov ? (std::uint32_t)((std::int64_t)mov + 7 + I32(I, mov + 3)) : 0;
            run.Fact("anchor.main.rva", Hex(at));
            run.Fact("anchor.main.global", Hex(g));
            if (!g || !InWritableData(I, g)) { ok = kFail; d += "; the root global is not decoded into .data"; Word("FORMAT", d); }
            else d += ", root global " + Hex(g);
        } else if (nm == "tick-anchor") {
            std::uint32_t g = 0;
            const std::uint8_t* b = Bytes(I, at - 0x40, 0x40);
            for (int i = 0x40 - 7; b && i >= 0; --i)
                if (b[i] == 0x48 && b[i + 1] == 0x8B && b[i + 2] == 0x0D) { g = (std::uint32_t)((std::int64_t)(at - 0x40 + i) + 7 + I32(I, at - 0x40 + i + 3)); break; }
            run.Fact("anchor.tick.rva", Hex(at));
            run.Fact("anchor.tick.global", Hex(g));
            if (!g || !InWritableData(I, g)) { ok = kFail; d += "; the tick owner global is not decoded"; Word("FORMAT", d); }
        } else if (nm == "scene-blank") {
            const std::uint8_t* b = Bytes(I, fn + 0x266, 1);
            const int op = b ? *b : -1;
            run.Fact("sig.scene-blank.jcc", Hex((std::uint32_t)op));
            if (op != 0x74 && op != 0x75) { ok = kFail; d += "; byte at +0x266 is " + Hex((std::uint32_t)op) + ", not a Jcc"; Word("FORMAT", d); }
        } else if (nm == "menu-clear") {
            // the targets vector the left-click lift walks: end at +10, begin at +20 of the hit
            const std::uint32_t end = U32(I, at + 10), begin = U32(I, at + 20);
            run.Fact("sig.menu-clear.disp", Hex(begin) + "," + Hex(end));
            exp = "targets vector " + Hex(kMenuTargetsBegin) + "/" + Hex(kMenuTargetsEnd);
            if (begin != kMenuTargetsBegin || end != kMenuTargetsEnd) { ok = kWarn; d += "; targets vector disp " + Hex(kMenuTargetsBegin) + "/" + Hex(kMenuTargetsEnd) + " compiled, " + Hex(begin) + "/" + Hex(end) + " in the hit"; Word("MOVED", d); }
            else d += ", targets vector " + Hex(begin) + "/" + Hex(end);
        } else if (nm == "menu-exec") {
            const std::uint32_t disp = U32(I, at + 15);   // cmp dword [rax+status],0x28
            run.Fact("sig.menu-exec.disp", Hex(disp));
            exp = "status " + Hex(rtx::md::kStatus);
            if (disp != rtx::md::kStatus) { ok = kWarn; d += "; status compare reads MainData+" + Hex(disp) + ", compiled " + Hex(rtx::md::kStatus); Word("MOVED", d); }
            else d += ", status " + Hex(disp);
        } else if (nm == "menu-init") {
            const std::uint32_t disp = U32(I, at + 35);   // mov rdx,[rax+language]
            run.Fact("sig.menu-init.disp", Hex(disp));
            exp = "language " + Hex(rtx::md::kLanguage);
            if (disp != rtx::md::kLanguage) { ok = kWarn; d += "; language index at MainData+" + Hex(disp) + ", compiled " + Hex(rtx::md::kLanguage); Word("MOVED", d); }
            else d += ", language " + Hex(disp);
        } else if (nm == "player-display") {
            const std::uint32_t disp = U32(I, at + 3);    // mov rax,[rcx+field]
            run.Fact("sig.player-display.disp", Hex(disp));
            exp = "actor field " + Hex(kPlayerDisplayField);
            if (disp != kPlayerDisplayField) { ok = kWarn; d += "; actor field at +" + Hex(disp) + ", compiled " + Hex(kPlayerDisplayField); Word("MOVED", d); }
            else d += ", actor field " + Hex(disp);
        } else if (nm == "menu-snap") {
            // the left-click slot assigns in the snapshot body, found as the companion finds them
            const std::uint8_t* body = Bytes(I, where, rtx::sig::kMenuSnapSpan + 12);
            const rtx::sig::MenuAssign m = body ? rtx::sig::FindMenuAssign(body, rtx::sig::kMenuSnapSpan) : rtx::sig::MenuAssign{};
            const std::uint32_t ta = m.ok ? (std::uint32_t)((std::int64_t)where + m.target) : 0;
            run.Fact("menu.assign", Hex(ta));
            if (m.ok) run.Fact("menu.slot", Hex(m.slot));
            if (!ta) { ok = kFail; d += "; the left-click slot assign calls do not name one routine (left-click option off)"; Word("GONE", d); }
            else d += ", assign routine " + Hex(ta);
        } else if (nm == "outline-switch" || nm == "outline-table") {
            const bool sw = nm == "outline-switch";
            const std::uint32_t dispAt = sw ? 2 : 24, end = sw ? 7 : 28;
            const std::uint32_t t = (std::uint32_t)((std::int64_t)at + end + I32(I, at + dispAt));
            run.Fact(std::string("sig.") + nm + ".target", Hex(t));
            if (!(sw ? InWritableData(I, t) : InData(I, t))) { ok = kFail; d += "; target " + Hex(t) + " is not data"; Word("FORMAT", d); }
            else d += ", target " + Hex(t);
        } else if (nm == "arrow-message") {
            const std::uint8_t* after = Bytes(I, at + (std::uint32_t)s.len, rtx::sig::kArrowLoadSpan);
            const std::uint32_t disp = after ? rtx::sig::RootFieldLoad(after, rtx::sig::kArrowLoadSpan) : 0;
            run.Fact("markers.arrowMgr", Hex(disp));
            exp = "manager " + Hex(rtx::md::kArrowMgr);
            if (!disp) { ok = kFail; d += "; the arrow manager load is not there"; Word("GONE", d); }
            else if (arrowSite.ok && disp != arrowSite.arrow) { ok = kFail; d += "; arrow manager at MainData+" + Hex(disp) + ", the frame call site names " + Hex(arrowSite.arrow); Word("MOVED", d); }
            else if (disp != rtx::md::kArrowMgr) { ok = kWarn; d += "; arrow manager at MainData+" + Hex(disp) + ", compiled " + Hex(rtx::md::kArrowMgr); Word("MOVED", d); }
            else d += ", manager MainData+" + Hex(disp);
        } else if (nm == "trail-message") {
            auto mgr = FindNear(I, at + (std::uint32_t)s.len, 0xA0 - s.len, PatternOf(rtx::sig::kTrailManager, sizeof(rtx::sig::kTrailManager) / sizeof(int)));
            const std::uint32_t disp = mgr ? U32(I, mgr + 3) : 0;
            run.Fact("markers.trailMgr", Hex(disp));
            if (!mgr) { ok = kFail; d += "; the trail manager load is not there"; Word("GONE", d); }
            else if (arrowSite.ok && disp != arrowSite.trail) { ok = kFail; d += "; trail manager at MainData+" + Hex(disp) + ", the frame call site names " + Hex(arrowSite.trail); Word("MOVED", d); }
            else if (disp != rtx::md::kTrailMgr) { ok = kWarn; d += "; trail manager at MainData+" + Hex(disp) + ", compiled " + Hex(rtx::md::kTrailMgr); Word("MOVED", d); }
            else d += ", manager MainData+" + Hex(disp);
        } else if (nm == "arrow-frame") {
            run.Fact("markers.slotSize", std::to_string(arrowSite.slot));
            if (arrowSite.slot != rtx::sig::kArrowSlotSize) { ok = kWarn; d += "; manager slots are " + std::to_string(arrowSite.slot) + " bytes, the markers know " + std::to_string(rtx::sig::kArrowSlotSize) + " (markers off, the frame hook stays)"; Word("FORMAT", d); }
            auto r = FindNear(I, where, 0x200, PatternOf(rtx::sig::kArrowRefresh, sizeof(rtx::sig::kArrowRefresh) / sizeof(int)));
            std::uint32_t t = 0;
            if (r) {
                const std::uint8_t* b = Bytes(I, r + 6, 0x20);
                for (int c = 0; b && c < 0x20; ++c) if (b[c] == 0xE8) { t = (std::uint32_t)((std::int64_t)r + 6 + c + 5 + I32(I, r + 6 + c + 1)); break; }
            }
            const std::uint8_t* tp = t ? Bytes(I, t, 3) : nullptr;
            if (!tp || tp[0] != 0x48 || tp[1] != 0x89 || tp[2] != 0x5C) { ok = kWarn; d += "; node refresh call not found (markers move without refresh)"; Word("GONE", d); }
        } else if (nm == "tile-draw") {
            const std::uint32_t flags = U32(I, at + 45);
            run.Fact("markers.passFlags", Hex(flags));
            auto sub = FindNear(I, at + (std::uint32_t)s.len, 0x90 - s.len, PatternOf(rtx::sig::kTileSubmit, sizeof(rtx::sig::kTileSubmit) / sizeof(int)));
            if (!sub) { ok = kWarn; d += "; submit call not found (tile outline off)"; Word("GONE", d); }
            else d += ", pass flags field " + Hex(flags);
        } else if (nm == "sound-synth") {
            const std::uint8_t* b = Bytes(I, at + (std::uint32_t)s.len, 24);
            for (int k = 0; b && k < 24; ++k) if (b[k] == 0xE8) { synthRva = (std::uint32_t)((std::int64_t)at + s.len + k + 5 + I32(I, at + (std::uint32_t)s.len + k + 1)); break; }
            run.Fact("sound.play", Hex(synthRva));
            const std::uint32_t ctx = U32(I, at + 23);   // mov rcx,[rcx+soundctx]
            run.Fact("sig.sound-synth.disp", Hex(ctx));
            if (!synthRva) { ok = kFail; d += "; the play call after it is gone"; Word("GONE", d); }
            else d += ", play routine " + Hex(synthRva);
            if (ctx != rtx::md::kSoundCtx) { if (ok == kPass) ok = kWarn; d += "; sound context at MainData+" + Hex(ctx) + ", compiled " + Hex(rtx::md::kSoundCtx); Word("MOVED", d); }
        } else if (nm == "framer") {
            framerRva = at;
        }
        // where this exe had it when the manifest was made
        if (!was0.empty()) {
            const std::size_t atp = was0.find('@');
            const std::uint32_t wasRva = atp == std::string::npos ? 0 : (std::uint32_t)std::strtoul(was0.c_str() + atp + 1, nullptr, 0);
            if (wasRva && wasRva != where) { run.Fact(std::string("sig.") + nm + ".moved", Hex(wasRva) + " -> " + Hex(where)); d += "; moved from " + Hex(wasRva); }
        }
        recorded(ok, exp, d);
        run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
    }

    // packet framer: the opcode bound and the descriptor table global, the table's own shape, and
    // the wire lengths the file's descriptor initializers give (no process needed)
    {
        std::string d; int ok = kPass; std::string got;
        std::uint32_t tblRva = 0;
        if (!framerRva) { ok = kUnchecked; d = "UNVERIFIED: framer not found, the bound and table are read through it"; }
        else {
            const std::uint32_t cmp = FindNear(I, framerRva, 0x400, { 0x81, 0xFA, -1, -1, -1, -1, 0x0F, 0x83, -1, -1, -1, -1, 0x48, 0x8B, 0x05 });
            if (!cmp) { ok = kFail; d = "GONE: bound check and table load not found in the framer"; }
            else {
                const int bound = (int)U32(I, cmp + 2) - 1;
                const std::uint32_t tbl = (std::uint32_t)((std::int64_t)cmp + 12 + 7 + I32(I, cmp + 15));
                tblRva = tbl;
                run.Fact("packets.opMax", Hex((std::uint32_t)bound));
                run.Fact("packets.tableRva", Hex(tbl));
                got = "max " + Hex((std::uint32_t)bound) + ", table " + Hex(tbl);
                const std::uint32_t pinned = rtx::pins::PinnedRva("optable", stamp);
                d = "opcodes 0.." + Hex((std::uint32_t)bound) + ", table global " + Hex(tbl);
                const DescTable dt = ReadDescTable(I, tbl);
                if (dt.ok) { run.Fact("packets.descriptors", std::to_string(dt.count)); got += ", " + std::to_string(dt.count) + " descriptors"; }
                if (bound != rtx::sops::kOpMax) { ok = kFail; d += "; compiled bound " + Hex((std::uint32_t)rtx::sops::kOpMax) + " (opcodes reshuffled: re-pin ServerOps.h)"; Word("FORMAT", d); }
                if (!dt.ok) { if (ok == kPass) ok = kWarn; d += "; the table global is not an inline-buffer vector (begin != self+0x28)"; Word("FORMAT", d); }
                else if ((int)dt.count != bound + 1) { ok = kFail; d += "; descriptor vector holds " + std::to_string(dt.count) + ", compiled bound " + Hex((std::uint32_t)rtx::sops::kOpMax); Word("FORMAT", d); }
                else d += ", " + std::to_string(dt.count) + " descriptors";
                if (tbl == kCompiledOpTable) d += " (compiled)";
                else if (pinned == tbl) d += " (pinned for this exe)";
                else { if (ok == kPass) ok = kWarn; d += pinned ? "; the pinned " + Hex(pinned) + " is stale" : "; not the compiled " + Hex(kCompiledOpTable) + ": packet reads fall back to a scan"; Word("MOVED", d); }
            }
        }
        run.Add(G, "code.packets", "Packet framer bound and table", ok, d, "Packet decoders|Server opcodes|Events channel", "max " + Hex((std::uint32_t)rtx::sops::kOpMax), got, "code.sig.framer");
        // the lengths on disk against the compiled expectations
        if (tblRva) {
            const std::map<int, int> lens = StaticPacketLengths(I, tblRva, FunctionOf(I, framerRva));
            int ok2 = kPass; std::string d2, bad; int good = 0, total = 0;
            if (lens.size() < 100) { ok2 = kUnchecked; d2 = "UNVERIFIED: descriptor initializers not recognised (" + std::to_string(lens.size()) + " decoded)"; }
            else {
                for (const auto& kv : lens) run.Fact("pktlen." + Hex((std::uint32_t)kv.first), std::to_string(kv.second));
                for (const auto& e : rtx::sops::kExpected) {
                    ++total;
                    auto it = lens.find(e.op);
                    if (it != lens.end() && it->second == e.len) { ++good; continue; }
                    std::string cands;
                    for (const auto& kv : lens) if (kv.second == e.len && kv.first != e.op) { if (!cands.empty()) cands += ","; cands += Hex((std::uint32_t)kv.first); if (cands.size() > 40) { cands += ",.."; break; } }
                    bad += std::string(bad.empty() ? "" : "; ") + e.name + " " + Hex((std::uint32_t)e.op) + ": " +
                           (it == lens.end() ? std::string("no initializer") : "length " + std::to_string(it->second) + ", expected " + std::to_string(e.len)) +
                           (cands.empty() ? "" : " (that length sits on " + cands + ")");
                }
                d2 = std::to_string(lens.size()) + " descriptors decoded from the file, " + std::to_string(good) + "/" + std::to_string(total) + " expected opcodes carry their wire length";
                if (good != total) { ok2 = kFail; d2 += "; " + bad; Word(good * 2 < total ? "MOVED" : "FORMAT", d2); }
            }
            run.Add(G, "code.pktlens", "Packet lengths on disk", ok2, d2, "Packet decoders|Server opcodes|Events channel|Zone events", std::to_string(total) + " lengths", std::to_string(good), "code.packets").kind =
                ok2 == kUnchecked ? "unrecorded" : std::string();
            if (lens.size() >= 100) {   // every opcode against the compiled table, so a resized packet is named with the game closed
                const int all = rtx::sops::kOpMax + 1; int agree = 0, missing = 0; std::string diff, none;
                for (int op = 0; op < all; ++op) {
                    auto it = lens.find(op);
                    if (it == lens.end()) { ++missing; if (none.size() < 80) none += std::string(none.empty() ? "" : ", ") + Hex((std::uint32_t)op); continue; }   // an initializer shape the decode does not read: the live row pkt.lengths covers it
                    if (it->second == rtx::sops::kAllLengths[op]) { ++agree; continue; }
                    if (diff.size() < 240) diff += std::string(diff.empty() ? "" : ", ") + Hex((std::uint32_t)op) + " " + std::to_string(it->second) + "/" + std::to_string(rtx::sops::kAllLengths[op]);
                }
                const bool ok = diff.empty();
                run.Add(G, "code.pktlens.all", "Packet lengths on disk, every opcode", ok ? kPass : kFail,
                        (ok ? std::string() : std::string("FORMAT: ")) + std::to_string(agree) + "/" + std::to_string(all - missing) + " decoded opcodes carry their compiled wire length (file/compiled)" + (diff.empty() ? "" : "; " + diff) + (missing ? "; " + std::to_string(missing) + " not decoded from the file: " + none : ""),
                        "Packet decoders|Events channel", std::to_string(all) + " lengths", std::to_string(agree) + " agree, " + std::to_string(missing) + " not decoded", "code.pktlens");
            }
        }
    }

    // engine ops the companion calls by their fixed number, recognised by their first bytes
    if (I.handlers.empty()) {
        run.Add(G, "code.ops", "Engine op registrar", kFail, "GONE: registrar not recognised (no routine registers a dense run of operation numbers)", "Engine ops|Calibration|In-frame labels", "", "", "code.exe");
    } else {
        for (const auto& oh : rtx::sig::kOpHeads) {
            std::vector<std::uint32_t> carriers;
            for (const auto& kv : I.handlers) {
                const std::uint8_t* b = Bytes(I, kv.second, oh.len);
                if (b && std::memcmp(b, oh.head, oh.len) == 0) carriers.push_back(kv.first);
            }
            int ok = kPass; std::string d;
            const bool atNumber = std::find(carriers.begin(), carriers.end(), oh.op) != carriers.end();
            if (carriers.size() == 1 && atNumber) d = "op " + std::to_string(oh.op);
            else if (carriers.empty()) { ok = kFail; d = "GONE: no handler starts with this head (op " + std::to_string(oh.op) + " changed)"; }
            else if (!atNumber) { ok = kFail; d = "MOVED: head now carried by op " + std::to_string(carriers[0]) + ", compiled " + std::to_string(oh.op); }
            else { ok = kWarn; d = "NEW: " + std::to_string(carriers.size()) + " ops share this head"; }
            if (oh.op == rtx::sig::kOpProject && ok == kPass) {
                const std::uint32_t h = I.handlers.at(oh.op);
                const bool k3 = FindNear(I, h, 0x90, PatternOf(rtx::sig::kOpProjectKind3, sizeof(rtx::sig::kOpProjectKind3))) != 0;
                const bool vw = FindNear(I, h, 0x90, PatternOf(rtx::sig::kOpProjectView, sizeof(rtx::sig::kOpProjectView))) != 0;
                if (!k3 || !vw) { ok = kFail; d += vw ? "; position kind test gone" : "; no longer reads MainData+0x199D0"; Word("FORMAT", d); }
            }
            run.Fact(std::string("op.") + oh.name, carriers.empty() ? std::string("none") : std::to_string(carriers[0]));
            run.Add(G, std::string("code.op.") + oh.name, std::string("Engine op ") + oh.name, ok, d, oh.features,
                    std::to_string(oh.op), carriers.empty() ? std::string("none") : std::to_string(carriers[0]), "code.exe");
        }
        // component setters, by the bytes each alone contains
        {
            std::map<std::uint32_t, std::uint32_t> byRva;
            for (const auto& kv : I.handlers) byRva[kv.second] = kv.first;
            const std::map<std::string, std::uint32_t> ops = rtx::calib::OpNames();
            const bool table = !ops.empty();
            int good = 0; std::string bad;
            for (const auto& cc : rtx::sig::kCcOps) {
                std::vector<std::uint32_t> found;
                if (cc.text) found = rtx::calib::HandlersNaming(I.pe, I.handlers, cc.text);
                else {
                    const auto fp = ParseFp(cc.fingerprint);
                    const auto fp2 = cc.fingerprint2 ? ParseFp(cc.fingerprint2) : std::vector<int>();
                    for (const auto& kv : I.handlers) {
                        std::uint32_t cb = WrapperCallback(I, kv.second);
                        if (!cb) cb = kv.second;
                        const std::uint8_t* b = Bytes(I, cb, 0x81);
                        if (!b) continue;
                        std::size_t len = 0x80;
                        for (std::size_t k = 0; k + 1 < 0x80; ++k) if (b[k] == 0xC3 && b[k + 1] == 0xCC) { len = k + 1; break; }
                        auto has = [&](const std::vector<int>& p) {
                            for (std::size_t o = 0; o + p.size() <= len; ++o) if (MatchAt(b + o, p)) return true;
                            return false;
                        };
                        if (!has(fp) || (!fp2.empty() && !has(fp2))) continue;
                        found.push_back(kv.first);
                    }
                }
                std::string upper = cc.name;
                for (char& c : upper) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                auto named = ops.find(upper);
                if (found.size() != 1) { bad += std::string(bad.empty() ? "" : "; ") + cc.name + (found.empty() ? " not found" : " matches " + std::to_string(found.size()) + " ops"); continue; }
                if (named != ops.end() && named->second != found[0]) { bad += std::string(bad.empty() ? "" : "; ") + cc.name + " is op " + std::to_string(found[0]) + ", named op " + std::to_string(named->second); continue; }
                run.Fact(std::string("cc.") + cc.name, std::to_string(found[0]));
                ++good;
            }
            const int total = (int)(sizeof(rtx::sig::kCcOps) / sizeof(rtx::sig::kCcOps[0]));
            if (!bad.empty()) Word(bad.find("not found") != std::string::npos ? "GONE" : bad.find("is op") != std::string::npos ? "MOVED" : "NEW", bad);
            run.Add(G, "code.cc", "Component setters", bad.empty() ? kPass : kFail,
                    bad.empty() ? std::to_string(good) + "/" + std::to_string(total) + " found, each unique" + (table ? " and agreeing with the op names" : "") : bad,
                    rtx::sig::kCcFeatures, std::to_string(total), std::to_string(good), "code.exe");
        }
        // ops called by name: each has a handler under its name, and pops what is pushed
        {
            const std::map<std::string, std::uint32_t> ops = rtx::calib::OpNames();
            if (ops.empty()) {
                run.Add(G, "code.named", "Engine ops by name", kFail, "GONE: no operation named in this exe (see Calibration)", "Asks|Sounds|Camera|In-frame panels", "", "", "calib.table");
            } else {
                int good = 0, total = 0; std::string bad, unverified; bool gone = false, format = false;
                for (const auto& n : kNamedOps) {
                    ++total;
                    auto it = ops.find(n.name);
                    if (it == ops.end()) { gone = true; bad += std::string(bad.empty() ? "" : "; ") + n.name + " not recognised by its code"; continue; }
                    auto h = I.handlers.find(it->second);
                    if (h == I.handlers.end()) { gone = true; bad += std::string(bad.empty() ? "" : "; ") + n.name + " has no handler"; continue; }
                    if (n.ints > 0) {   // an op that pushes nothing has nothing to pop
                        const int pops = PopCount(I, h->second);
                        if (pops < 0) unverified += std::string(unverified.empty() ? "" : ", ") + n.name;
                        else if (pops != n.ints) { format = true; bad += std::string(bad.empty() ? "" : "; ") + n.name + " pops " + std::to_string(pops) + " ints, " + std::to_string(n.ints) + " pushed"; continue; }
                    }
                    ++good;
                }
                int ok = bad.empty() ? kPass : kFail; std::string d;
                if (!bad.empty()) { d = bad; Word(gone ? "GONE" : format ? "FORMAT" : "MOVED", d); }
                else d = std::to_string(good) + "/" + std::to_string(total) + " resolve, int pops agree";
                if (!unverified.empty()) { if (ok == kPass) { ok = kWarn; d = "UNVERIFIED: no int-stack adjust in " + unverified + ", pop count not checked; " + d; } else d += "; no int-stack adjust in " + unverified; }
                run.Add(G, "code.named", "Engine ops by name", ok, d,
                        "Asks|Sounds|Camera|In-frame panels", std::to_string(total), std::to_string(good), "calib.table");
            }
        }
        // MainData shape: the handlers' displacement set against the one recorded for a validated build
        {
            const std::set<std::uint32_t> now = HandlerDisps(I);
            run.Fact("disp.count", std::to_string(now.size()));
            std::vector<std::uint32_t> rec;
            const rtx::pins::Line exact = rtx::pins::Find("disp", Hex8(stamp));
            std::string from = Hex8(stamp);
            if (!exact.kind.empty()) rec = ParseHexList(exact.expect);
            else {
                // a new exe: the newest validated one stands in
                const auto lines = rtx::pins::Lines();
                for (const auto& l : *lines) if (l.kind == "disp") { rec = ParseHexList(l.expect); from = l.key; }
            }
            if (rec.empty()) {
                run.Add(G, "code.shift", "MainData layout", kUnchecked, "UNVERIFIED: no displacement set recorded for any exe: record one with --sigs-record", "Every MainData read", "", std::to_string(now.size()), "code.ops").kind = "unrecorded";
            } else {
                std::vector<std::uint32_t> missing;
                for (std::uint32_t d : rec) if (!now.count(d)) missing.push_back(d);
                std::string d; int ok = kPass;
                if (missing.empty()) d = std::to_string(rec.size()) + "/" + std::to_string(rec.size()) + " recorded displacements in place (from " + from + ")";
                else {
                    // the delta that puts back the most missing displacements, then the next, while
                    // one explains at least three: a block of MainData that grew or shrank
                    std::vector<std::pair<std::pair<std::uint32_t, std::uint32_t>, std::int32_t>> shifts;
                    std::size_t explained = 0;
                    std::set<std::uint32_t> remaining(missing.begin(), missing.end());
                    while (!remaining.empty()) {
                        std::int32_t bestDelta = 0; std::size_t best = 0;
                        for (std::int32_t delta = -0x1000; delta <= 0x1000; delta += 8) {
                            if (!delta) continue;
                            std::size_t n = 0;
                            for (std::uint32_t d0 : remaining) if (now.count(d0 + delta)) ++n;
                            if (n > best) { best = n; bestDelta = delta; }
                        }
                        if (best < 3) break;
                        std::uint32_t lo = 0xFFFFFFFFu, hi = 0;
                        for (auto it = remaining.begin(); it != remaining.end(); ) {
                            if (now.count(*it + bestDelta)) { lo = std::min(lo, *it); hi = std::max(hi, *it); it = remaining.erase(it); }
                            else ++it;
                        }
                        shifts.push_back({ { lo, hi }, bestDelta });
                        explained += best;
                    }
                    if (!shifts.empty()) {
                        ok = kFail;
                        for (const auto& sh : shifts) {
                            std::string names; int n = 0;
                            for (const auto& e : rtx::md::kTable) {
                                if (e.off < sh.first.first || e.off > sh.first.second) continue;
                                ++n;
                                if (n <= 6) names += std::string(names.empty() ? "" : ", ") + e.name;
                            }
                            for (const auto& e : kRootFields) {
                                if (e.off < sh.first.first || e.off > sh.first.second) continue;
                                ++n;
                                if (n <= 8) names += std::string(names.empty() ? "" : ", ") + e.name;
                            }
                            char b[200];
                            std::snprintf(b, sizeof(b), "%s0x%X..0x%X moved %s0x%X", d.empty() ? "" : "; ", sh.first.first, sh.first.second,
                                          sh.second < 0 ? "-" : "+", (unsigned)(sh.second < 0 ? -sh.second : sh.second));
                            d += b;
                            d += ", " + std::to_string(n) + " known fields affected" + (names.empty() ? std::string() : " (" + names + (n > 8 ? ", ..." : "") + ")");
                            run.Fact("disp.shift." + Hex(sh.first.first), (sh.second < 0 ? "-" : "+") + Hex((std::uint32_t)(sh.second < 0 ? -sh.second : sh.second)));
                        }
                        Word("MOVED", d);
                    }
                    const std::size_t unexplained = missing.size() - explained;
                    if (unexplained) {
                        if (unexplained * 10 > rec.size() && ok == kPass) ok = kWarn;
                        d += std::string(d.empty() ? "" : "; ") + std::to_string(unexplained) + " of " + std::to_string(rec.size()) + " recorded displacements gone without a pattern";
                        if (ok != kPass) Word("GONE", d);
                    }
                    run.Fact("disp.missing", std::to_string(missing.size()));
                }
                if (from != Hex8(stamp) && ok == kPass) d += " (recorded for " + from + ", this exe has no set of its own)";
                run.Add(G, "code.shift", "MainData layout", ok, d, "Every MainData read", std::to_string(rec.size()) + " in place", std::to_string(rec.size() - missing.size()), "code.ops");
            }
        }
    }

    // sound origin labels: the return addresses of every call to the play routine
    if (synthRva) {
        // The origins are told by the order of the calls: the routine is called from the same places in
        // the same order in every client of a build, at addresses that differ. So the labels hold when
        // the exe has as many calls as there are labels.
        const auto sites = CallSitesTo(I, synthRva);
        const int total = (int)(sizeof(kSoundSites) / sizeof(kSoundSites[0]));
        const int matched = (int)sites.size() == total ? total : 0;
        std::string list;
        for (std::size_t k = 0; k < sites.size() && k < 12; ++k) list += (k ? "," : "") + Hex(sites[k]);
        run.Fact("sound.callers", list);
        run.Add(G, "code.soundsites", "Sound origin labels", matched == total ? kPass : kWarn,
                matched == total ? std::to_string(total) + " calls of the play routine, each labelled by its place among them"
                                 : "NEW: " + std::to_string(sites.size()) + " calls of the play routine, " + std::to_string(total) + " labelled (origins show as \"other\" on this exe)",
                "Sounds panel: origin column", std::to_string(total), std::to_string(matched), "code.sig.sound-synth");
    }

    // the outline methods the scenery hover calls by slot: a method table holding both
    {
        const auto sites = FindAll(I, PatternOf(rtx::sig::kHoverProlog, sizeof(rtx::sig::kHoverProlog)), false);
        const std::set<std::uint32_t> methods(sites.begin(), sites.end());
        const std::uint64_t ib = I.pe.imageBase;
        const std::size_t gap = rtx::sig::kKeepSlot - rtx::sig::kHoverSlot;
        auto methodAt = [&](const std::uint8_t* q) -> std::uint32_t {
            std::uint64_t v; std::memcpy(&v, q, 8);
            if (v < ib || v - ib > 0xFFFFFFFFull) return 0;
            return methods.count((std::uint32_t)(v - ib)) ? (std::uint32_t)(v - ib) : 0;
        };
        std::set<std::uint32_t> hovered, kept;
        int tables = 0;
        if (!methods.empty())
            for (const auto& sec : I.pe.sections) {
                if (sec.flags & kScnExec) continue;
                const std::uint8_t* b = I.pe.base + sec.raw;
                const std::size_t n = std::min<std::size_t>(sec.rawSize, sec.vsize ? sec.vsize : sec.rawSize);
                for (std::size_t o = 0; o + gap + 8 <= n; o += 8) {
                    const std::uint32_t hv = methodAt(b + o);
                    if (!hv) continue;
                    const std::uint32_t kp = methodAt(b + o + gap);
                    if (!kp || kp == hv) continue;
                    ++tables; hovered.insert(hv); kept.insert(kp);
                }
            }
        int ok = kPass; std::string d;
        if (sites.empty()) { ok = kFail; d = "GONE: no routine starts with the outline method prolog"; }
        else if (!tables) { ok = kFail; d = "MOVED: " + std::to_string(sites.size()) + " prolog sites, but no method table holds two of them at +" + Hex(rtx::sig::kHoverSlot) + "/+" + Hex(rtx::sig::kKeepSlot) + " (slots moved)"; }
        else if (hovered.size() != 1 || kept.size() != 1) { ok = kWarn; d = "NEW: " + std::to_string(tables) + " method tables name " + std::to_string(hovered.size()) + " hovered and " + std::to_string(kept.size()) + " setter methods"; }
        else d = "hovered " + Hex(*hovered.begin()) + " at +" + Hex(rtx::sig::kHoverSlot) + ", setter " + Hex(*kept.begin()) + " at +" + Hex(rtx::sig::kKeepSlot) + " (" + std::to_string(tables) + (tables == 1 ? " table)" : " tables)");
        run.Fact("hover.sites", std::to_string(sites.size()));
        run.Fact("hover.tables", std::to_string(tables));
        if (!hovered.empty()) run.Fact("hover.method", Hex(*hovered.begin()));
        run.Add(G, "code.hover", "Outline hover methods", ok, d, "Scenery hover outline|Outline pulse hold", "1 table", std::to_string(tables), "code.exe");
    }

    // the Vulkan compositor detours loader exports the game must import
    if (vulkan) {
        std::string missing; int have = 0;
        for (const char* n : kVkImports) {
            const std::string want = std::string("vulkan-1.dll!") + n;
            if (std::find(I.imports.begin(), I.imports.end(), want) != I.imports.end()) ++have;
            else missing += std::string(missing.empty() ? "" : ", ") + n;
        }
        int vk = 0;
        for (const auto& s : I.imports) if (s.rfind("vulkan-1.dll!", 0) == 0) ++vk;
        run.Fact("exe.vkImports", std::to_string(vk));
        const int total = (int)(sizeof(kVkImports) / sizeof(kVkImports[0]));
        run.Add(G, "code.vkimports", "Vulkan imports", missing.empty() ? kPass : kFail,
                missing.empty() ? std::to_string(have) + "/" + std::to_string(total) + " detoured loader exports imported (" + std::to_string(vk) + " vk imports)"
                                : "GONE: no longer imported: " + missing,
                "In-game UI frame (Vulkan)|Overlays (Vulkan)", std::to_string(total), std::to_string(have), "code.exe");
    }
}

std::string Record(const std::wstring& exePath) {
    auto im = Load(exePath);
    if (!im->ok) return "# " + im->error + "\n";
    const Image& I = *im;
    const std::string st = Hex8(I.pe.stamp);
    std::ostringstream o;
    o << "build\t" << st << "\tflavour=" << (I.vulkan ? "vulkan" : I.opengl ? "opengl" : "unknown") << ";version=" << I.version
      << ";size=" << Hex(I.pe.sizeOfImage) << "\n";
    for (const auto& s : rtx::sig::kTable) {
        const auto hits = FindAll(I, PatternOf(s), s.scope == rtx::sig::kFirstExec);
        o << "sig\t" << s.name << "\t" << st << "=" << hits.size();
        const std::uint32_t where = hits.size() == 1 ? HookPoint(I, s, hits[0]) : hits.empty() ? 0 : hits[0];
        if (!hits.empty()) o << "@" << Hex(where ? where : hits[0]);
        o << "\t" << s.features << "\n";
    }
    const ExeFacts f = Facts(exePath);
    if (f.optableRva) o << "rva\toptable\t" << st << "=" << Hex(f.optableRva) << "\tPacket decoders, server opcodes\n";
    if (!I.starts.empty()) {
        const auto disps = HandlerDisps(I);
        o << "disp\t" << st << "\t";
        bool first = true;
        for (std::uint32_t d : disps) { o << (first ? "" : ",") << Hex(d); first = false; }
        o << "\tMainData layout\n";
    }
    return o.str();
}

std::string CheckText(const std::wstring& exePath) {
    rtx::health::Run run;
    Check(exePath, run);
    return rtx::health::SummaryText(run.Json("", 0));
}

}  // namespace rtx::codescan
