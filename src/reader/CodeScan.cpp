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
// dec dword [reg+0x10A0]. -1 when none is in the first bytes.
int PopCount(const Image& im, std::uint32_t h) {
    const std::uint8_t* b = Bytes(im, h, 0x60);
    if (!b) return -1;
    for (int k = 0; k + 7 <= 0x60; ++k) {
        int j = k;
        if (b[j] == 0x41) ++j;
        if ((b[j] == 0x83 || b[j] == 0xFF) && (b[j + 1] & 0xC0) == 0x80 && b[j + 2] == 0xA0 && b[j + 3] == 0x10 && b[j + 4] == 0 && b[j + 5] == 0) {
            const int reg = (b[j + 1] >> 3) & 7;
            if (b[j] == 0xFF && reg == 1) return 1;                              // dec
            if (b[j] == 0x83 && reg == 0) return -(int)(std::int8_t)b[j + 6];   // add -n
            if (b[j] == 0x83 && reg == 5) return (int)(std::int8_t)b[j + 6];    // sub n
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

}  // namespace

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
        run.Add(G, "code.exe", "Client exe", kFail, im->error, "Every code check");
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
            d = hits.empty() ? "absent on Vulkan, as expected" : "found on Vulkan (unexpected)";
            recorded(ok, exp, d);
            run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
            continue;
        }
        if (s.expect == rtx::sig::kSameTarget) {
            exp = "1 or more hits naming one routine";
            std::set<std::uint32_t> targets;
            for (std::uint32_t h : hits) targets.insert((std::uint32_t)((std::int64_t)h + s.len + 4 + I32(I, h + (std::uint32_t)s.len)));
            if (hits.empty()) { ok = kFail; d = "0 hits: pattern moved"; }
            else if (targets.size() != 1) { ok = kFail; d = std::to_string(hits.size()) + " hits name " + std::to_string(targets.size()) + " routines"; }
            else { d = std::to_string(hits.size()) + " hits, routine " + Hex(*targets.begin()); run.Fact(std::string("sig.") + s.name + ".target", Hex(*targets.begin())); }
            recorded(ok, exp, d);
            run.Add(G, id, key, ok, d, s.features, exp, got, "code.exe");
            continue;
        }
        if (hits.size() != 1) {
            ok = kFail;
            d = hits.empty() ? "0 hits: pattern moved" : std::to_string(hits.size()) + " hits: ambiguous, the hook refuses it";
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
            if (!g || !InWritableData(I, g)) { ok = kFail; d += "; the root global is not decoded into .data"; }
            else d += ", root global " + Hex(g);
        } else if (nm == "tick-anchor") {
            std::uint32_t g = 0;
            const std::uint8_t* b = Bytes(I, at - 0x40, 0x40);
            for (int i = 0x40 - 7; b && i >= 0; --i)
                if (b[i] == 0x48 && b[i + 1] == 0x8B && b[i + 2] == 0x0D) { g = (std::uint32_t)((std::int64_t)(at - 0x40 + i) + 7 + I32(I, at - 0x40 + i + 3)); break; }
            run.Fact("anchor.tick.rva", Hex(at));
            run.Fact("anchor.tick.global", Hex(g));
            if (!g || !InWritableData(I, g)) { ok = kFail; d += "; the tick owner global is not decoded"; }
        } else if (nm == "scene-blank") {
            const std::uint8_t* b = Bytes(I, fn + 0x266, 1);
            const int op = b ? *b : -1;
            run.Fact("sig.scene-blank.jcc", Hex((std::uint32_t)op));
            if (op != 0x74 && op != 0x75) { ok = kFail; d += "; byte at +0x266 is " + Hex((std::uint32_t)op) + ", not a Jcc"; }
        } else if (nm == "menu-snap") {
            // the left-click slot assigns in the snapshot body, found as the companion finds them
            const std::uint8_t* body = Bytes(I, where, rtx::sig::kMenuSnapSpan + 12);
            const rtx::sig::MenuAssign m = body ? rtx::sig::FindMenuAssign(body, rtx::sig::kMenuSnapSpan) : rtx::sig::MenuAssign{};
            const std::uint32_t ta = m.ok ? (std::uint32_t)((std::int64_t)where + m.target) : 0;
            run.Fact("menu.assign", Hex(ta));
            if (m.ok) run.Fact("menu.slot", Hex(m.slot));
            if (!ta) { ok = kFail; d += "; the left-click slot assign calls do not name one routine (left-click option off)"; }
            else d += ", assign routine " + Hex(ta);
        } else if (nm == "outline-switch" || nm == "outline-table") {
            const bool sw = nm == "outline-switch";
            const std::uint32_t dispAt = sw ? 2 : 24, end = sw ? 7 : 28;
            const std::uint32_t t = (std::uint32_t)((std::int64_t)at + end + I32(I, at + dispAt));
            run.Fact(std::string("sig.") + nm + ".target", Hex(t));
            if (!(sw ? InWritableData(I, t) : InData(I, t))) { ok = kFail; d += "; target " + Hex(t) + " is not data"; }
            else d += ", target " + Hex(t);
        } else if (nm == "arrow-message") {
            const std::uint8_t* after = Bytes(I, at + (std::uint32_t)s.len, rtx::sig::kArrowLoadSpan);
            const std::uint32_t disp = after ? rtx::sig::RootFieldLoad(after, rtx::sig::kArrowLoadSpan) : 0;
            run.Fact("markers.arrowMgr", Hex(disp));
            exp = "manager " + Hex(rtx::md::kArrowMgr);
            if (!disp) { ok = kFail; d += "; the arrow manager load is not there"; }
            else if (arrowSite.ok && disp != arrowSite.arrow) { ok = kFail; d += "; arrow manager at MainData+" + Hex(disp) + ", the frame call site names " + Hex(arrowSite.arrow); }
            else if (disp != rtx::md::kArrowMgr) { ok = kWarn; d += "; arrow manager at MainData+" + Hex(disp) + ", compiled " + Hex(rtx::md::kArrowMgr); }
            else d += ", manager MainData+" + Hex(disp);
        } else if (nm == "trail-message") {
            auto mgr = FindNear(I, at + (std::uint32_t)s.len, 0xA0 - s.len, PatternOf(rtx::sig::kTrailManager, sizeof(rtx::sig::kTrailManager) / sizeof(int)));
            const std::uint32_t disp = mgr ? U32(I, mgr + 3) : 0;
            run.Fact("markers.trailMgr", Hex(disp));
            if (!mgr) { ok = kFail; d += "; the trail manager load is not there"; }
            else if (arrowSite.ok && disp != arrowSite.trail) { ok = kFail; d += "; trail manager at MainData+" + Hex(disp) + ", the frame call site names " + Hex(arrowSite.trail); }
            else d += ", manager MainData+" + Hex(disp);
        } else if (nm == "arrow-frame") {
            run.Fact("markers.slotSize", std::to_string(arrowSite.slot));
            if (arrowSite.slot != rtx::sig::kArrowSlotSize) { ok = kWarn; d += "; manager slots are " + std::to_string(arrowSite.slot) + " bytes, the markers know " + std::to_string(rtx::sig::kArrowSlotSize) + " (markers off, the frame hook stays)"; }
            auto r = FindNear(I, where, 0x200, PatternOf(rtx::sig::kArrowRefresh, sizeof(rtx::sig::kArrowRefresh) / sizeof(int)));
            std::uint32_t t = 0;
            if (r) {
                const std::uint8_t* b = Bytes(I, r + 6, 0x20);
                for (int c = 0; b && c < 0x20; ++c) if (b[c] == 0xE8) { t = (std::uint32_t)((std::int64_t)r + 6 + c + 5 + I32(I, r + 6 + c + 1)); break; }
            }
            const std::uint8_t* tp = t ? Bytes(I, t, 3) : nullptr;
            if (!tp || tp[0] != 0x48 || tp[1] != 0x89 || tp[2] != 0x5C) { ok = kWarn; d += "; node refresh call not found (markers move without refresh)"; }
        } else if (nm == "tile-draw") {
            const std::uint32_t flags = U32(I, at + 45);
            run.Fact("markers.passFlags", Hex(flags));
            auto sub = FindNear(I, at + (std::uint32_t)s.len, 0x90 - s.len, PatternOf(rtx::sig::kTileSubmit, sizeof(rtx::sig::kTileSubmit) / sizeof(int)));
            if (!sub) { ok = kWarn; d += "; submit call not found (tile outline off)"; }
            else d += ", pass flags field " + Hex(flags);
        } else if (nm == "sound-synth") {
            const std::uint8_t* b = Bytes(I, at + (std::uint32_t)s.len, 24);
            for (int k = 0; b && k < 24; ++k) if (b[k] == 0xE8) { synthRva = (std::uint32_t)((std::int64_t)at + s.len + k + 5 + I32(I, at + (std::uint32_t)s.len + k + 1)); break; }
            run.Fact("sound.play", Hex(synthRva));
            if (!synthRva) { ok = kFail; d += "; the play call after it is gone"; }
            else d += ", play routine " + Hex(synthRva);
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

    // packet framer: the opcode bound and the descriptor table global
    {
        std::string d; int ok = kPass; std::string got;
        if (!framerRva) { ok = kUnchecked; d = "framer not found"; }
        else {
            const std::uint32_t cmp = FindNear(I, framerRva, 0x400, { 0x81, 0xFA, -1, -1, -1, -1, 0x0F, 0x83, -1, -1, -1, -1, 0x48, 0x8B, 0x05 });
            if (!cmp) { ok = kFail; d = "bound check and table load not found in the framer"; }
            else {
                const int bound = (int)U32(I, cmp + 2) - 1;
                const std::uint32_t tbl = (std::uint32_t)((std::int64_t)cmp + 12 + 7 + I32(I, cmp + 15));
                run.Fact("packets.opMax", Hex((std::uint32_t)bound));
                run.Fact("packets.tableRva", Hex(tbl));
                got = "max " + Hex((std::uint32_t)bound) + ", table " + Hex(tbl);
                const std::uint32_t pinned = rtx::pins::PinnedRva("optable", stamp);
                d = "opcodes 0.." + Hex((std::uint32_t)bound) + ", table global " + Hex(tbl);
                if (bound != rtx::sops::kOpMax) { ok = kFail; d += "; compiled bound " + Hex((std::uint32_t)rtx::sops::kOpMax) + " (opcodes reshuffled: re-pin ServerOps.h)"; }
                if (tbl == kCompiledOpTable) d += " (compiled)";
                else if (pinned == tbl) d += " (pinned for this exe)";
                else { if (ok == kPass) ok = kWarn; d += pinned ? "; the pinned " + Hex(pinned) + " is stale" : "; not the compiled " + Hex(kCompiledOpTable) + ": packet reads fall back to a scan"; }
            }
        }
        run.Add(G, "code.packets", "Packet framer bound and table", ok, d, "Packet decoders|Server opcodes|Events channel", "max " + Hex((std::uint32_t)rtx::sops::kOpMax), got, "code.sig.framer");
    }

    // engine ops the companion calls by their fixed number, recognised by their first bytes
    if (I.handlers.empty()) {
        run.Add(G, "code.ops", "Engine op registrar", kFail, "registrar not recognised", "Engine ops|Calibration|In-frame labels", "", "", "code.exe");
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
            else if (carriers.empty()) { ok = kFail; d = "no handler starts with this head (op " + std::to_string(oh.op) + " changed)"; }
            else if (!atNumber) { ok = kFail; d = "head now carried by op " + std::to_string(carriers[0]) + ", compiled " + std::to_string(oh.op); }
            else { ok = kWarn; d = std::to_string(carriers.size()) + " ops share this head"; }
            if (oh.op == rtx::sig::kOpProject && ok == kPass) {
                const std::uint32_t h = I.handlers.at(oh.op);
                const bool k3 = FindNear(I, h, 0x90, PatternOf(rtx::sig::kOpProjectKind3, sizeof(rtx::sig::kOpProjectKind3))) != 0;
                const bool vw = FindNear(I, h, 0x90, PatternOf(rtx::sig::kOpProjectView, sizeof(rtx::sig::kOpProjectView))) != 0;
                if (!k3 || !vw) { ok = kFail; d += vw ? "; position kind test gone" : "; no longer reads MainData+0x199D0"; }
            }
            run.Fact(std::string("op.") + oh.name, carriers.empty() ? std::string("none") : std::to_string(carriers[0]));
            run.Add(G, std::string("code.op.") + oh.name, std::string("Engine op ") + oh.name, ok, d, oh.features,
                    std::to_string(oh.op), carriers.empty() ? std::string("none") : std::to_string(carriers[0]), "code.exe");
        }
        // component setters, by the bytes each alone contains
        {
            std::map<std::uint32_t, std::uint32_t> byRva;
            for (const auto& kv : I.handlers) byRva[kv.second] = kv.first;
            std::map<std::string, std::uint32_t> ops;
            const bool table = rtx::calib::Table().usable;
            if (table) {
                wchar_t up[MAX_PATH] = {};
                if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) rtx::calib::OpTable(std::wstring(up) + L"\\RuneToolsX\\cs2\\opcodes.json", ops);
            }
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
                if (named != ops.end() && named->second != found[0]) { bad += std::string(bad.empty() ? "" : "; ") + cc.name + " is op " + std::to_string(found[0]) + ", the table says " + std::to_string(named->second); continue; }
                run.Fact(std::string("cc.") + cc.name, std::to_string(found[0]));
                ++good;
            }
            const int total = (int)(sizeof(rtx::sig::kCcOps) / sizeof(rtx::sig::kCcOps[0]));
            run.Add(G, "code.cc", "Component setters", bad.empty() ? kPass : kFail,
                    bad.empty() ? std::to_string(good) + "/" + std::to_string(total) + " found, each unique" + (table ? " and agreeing with the op table" : "") : bad,
                    rtx::sig::kCcFeatures, std::to_string(total), std::to_string(good), "code.exe");
        }
        // ops called by name: resolve in the table, and pop what is pushed
        {
            const bool table = rtx::calib::Table().usable;
            std::map<std::string, std::uint32_t> ops;
            if (table) {
                wchar_t up[MAX_PATH] = {};
                if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) rtx::calib::OpTable(std::wstring(up) + L"\\RuneToolsX\\cs2\\opcodes.json", ops);
            }
            if (!table || ops.empty()) {
                run.Add(G, "code.named", "Engine ops by name", kUnchecked, "operation table not usable (see Calibration)", "Asks|Sounds|Camera|In-frame panels", "", "", "calib.table");
            } else {
                int good = 0, total = 0; std::string bad;
                for (const auto& n : kNamedOps) {
                    ++total;
                    auto it = ops.find(n.name);
                    if (it == ops.end()) { bad += std::string(bad.empty() ? "" : "; ") + n.name + " not in the table"; continue; }
                    auto h = I.handlers.find(it->second);
                    if (h == I.handlers.end()) { bad += std::string(bad.empty() ? "" : "; ") + n.name + " has no handler"; continue; }
                    if (n.ints >= 0) {
                        const int pops = PopCount(I, h->second);
                        if (pops >= 0 && pops != n.ints) { bad += std::string(bad.empty() ? "" : "; ") + n.name + " pops " + std::to_string(pops) + " ints, " + std::to_string(n.ints) + " pushed"; continue; }
                    }
                    ++good;
                }
                run.Add(G, "code.named", "Engine ops by name", bad.empty() ? kPass : kFail,
                        bad.empty() ? std::to_string(good) + "/" + std::to_string(total) + " resolve, int pops agree" : bad,
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
                run.Add(G, "code.shift", "MainData layout", kUnchecked, "no displacement set recorded", "Every MainData read", "", std::to_string(now.size()), "code.ops");
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
                            char b[200];
                            std::snprintf(b, sizeof(b), "%s0x%X..0x%X moved %s0x%X", d.empty() ? "" : "; ", sh.first.first, sh.first.second,
                                          sh.second < 0 ? "-" : "+", (unsigned)(sh.second < 0 ? -sh.second : sh.second));
                            d += b;
                            d += ", " + std::to_string(n) + " compiled offsets affected" + (names.empty() ? std::string() : " (" + names + (n > 6 ? ", ..." : "") + ")");
                            run.Fact("disp.shift." + Hex(sh.first.first), (sh.second < 0 ? "-" : "+") + Hex((std::uint32_t)(sh.second < 0 ? -sh.second : sh.second)));
                        }
                    }
                    const std::size_t unexplained = missing.size() - explained;
                    if (unexplained) {
                        if (unexplained * 10 > rec.size() && ok == kPass) ok = kWarn;
                        d += std::string(d.empty() ? "" : "; ") + std::to_string(unexplained) + " of " + std::to_string(rec.size()) + " recorded displacements gone without a pattern";
                    }
                    run.Fact("disp.missing", std::to_string(missing.size()));
                }
                run.Add(G, "code.shift", "MainData layout", ok, d, "Every MainData read", std::to_string(rec.size()) + " in place", std::to_string(rec.size() - missing.size()), "code.ops");
            }
        }
    }

    // sound origin labels: the return addresses of every call to the play routine
    if (synthRva) {
        const auto sites = CallSitesTo(I, synthRva);
        int matched = 0;
        for (const auto& s : kSoundSites) if (std::find(sites.begin(), sites.end(), s.ret) != sites.end()) ++matched;
        const int total = (int)(sizeof(kSoundSites) / sizeof(kSoundSites[0]));
        std::string list;
        for (std::size_t k = 0; k < sites.size() && k < 12; ++k) list += (k ? "," : "") + Hex(sites[k]);
        run.Fact("sound.callers", list);
        run.Add(G, "code.soundsites", "Sound origin labels", matched == total ? kPass : kWarn,
                std::to_string(matched) + "/" + std::to_string(total) + " labelled call sites are calls to the play routine" +
                (matched == total ? "" : " (origins show as \"other\" on this exe)"),
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
        if (sites.empty()) { ok = kFail; d = "no routine starts with the outline method prolog"; }
        else if (!tables) { ok = kFail; d = std::to_string(sites.size()) + " prolog sites, but no method table holds two of them at +" + Hex(rtx::sig::kHoverSlot) + "/+" + Hex(rtx::sig::kKeepSlot) + " (slots moved)"; }
        else if (hovered.size() != 1 || kept.size() != 1) { ok = kWarn; d = std::to_string(tables) + " method tables name " + std::to_string(hovered.size()) + " hovered and " + std::to_string(kept.size()) + " setter methods"; }
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
                                : "no longer imported: " + missing,
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
