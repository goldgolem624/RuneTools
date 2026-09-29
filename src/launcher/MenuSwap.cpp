#include "MenuSwap.h"
#include "../../companion/MenuShare.h"

#include <Windows.h>

#include <cstring>
#include <iterator>
#include <string>

namespace rtx::launcher::menuswap {
namespace {

struct View {
    HANDLE            map = nullptr;
    rtx::menu::Share* sh  = nullptr;

    explicit View(std::uint32_t pid) {
        if (!pid) return;
        wchar_t name[rtx::ipc::kNameChars];
        rtx::menu::MakeSectionName(pid, name);
        map = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!map) return;
        sh = (rtx::menu::Share*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                                              sizeof(rtx::menu::Share));
        // Uninitialised or wrong-version section: treat as absent.
        if (sh && (sh->magic != rtx::menu::kMagic || sh->version != rtx::menu::kVersion)) {
            UnmapViewOfFile(sh);
            sh = nullptr;
        }
    }
    ~View() {
        if (sh) UnmapViewOfFile(sh);
        if (map) CloseHandle(map);
    }
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    explicit operator bool() const { return sh != nullptr; }
};

// Length of the well-formed UTF-8 sequence starting at s[i] and ending within cap, 0 if none.
std::size_t utf8_len(const char* s, std::size_t i, std::size_t cap) {
    const unsigned char c = (unsigned char)s[i];
    std::size_t n = 0;
    unsigned char lo = 0x80, hi = 0xBF;       // allowed range of the second byte
    if (c < 0x80) return 1;
    else if (c >= 0xC2 && c <= 0xDF) n = 2;
    else if (c >= 0xE0 && c <= 0xEF) { n = 3; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 4; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
    else return 0;
    if (i + n > cap) return 0;
    const unsigned char c1 = (unsigned char)s[i + 1];
    if (c1 < lo || c1 > hi) return 0;
    for (std::size_t k = 2; k < n; ++k)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 0;
    return n;
}

// The page gets this JSON as UTF-8, and one malformed byte empties the whole string there. So only
// whole, well-formed characters are copied: a stray byte, or a character cut short by the fixed
// field size, is left out.
void append_escaped(std::string& out, const char* src, std::size_t cap) {
    // The game side rewrites these fields while this runs: check and copy from one local snapshot,
    // so a byte cannot change between being checked and being appended.
    char s[rtx::menu::kTargetLen];
    if (cap > sizeof(s)) cap = sizeof(s);
    std::memcpy(s, src, cap);
    for (std::size_t i = 0; i < cap && s[i];) {
        const std::size_t n = utf8_len(s, i, cap);
        if (n == 0) { ++i; continue; }
        const char c = s[i];
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if ((unsigned char)c < 0x20)  out += ' ';      // menu text is plain; drop controls
        else out.append(s + i, n);
        i += n;
    }
}


}  // namespace

bool SetEnabled(std::uint32_t pid, std::uint32_t mode) {
    View v(pid);
    if (!v) return false;
    if (mode > rtx::menu::kEnableBackground) mode = rtx::menu::kEnablePanel;
    v.sh->enable = mode;
    return true;
}

// Rules: one per line as "verb<TAB>target" (empty target = any object); line i draws above i+1.
bool SetPins(std::uint32_t pid, const std::string& rules) {
    View v(pid);
    if (!v) return false;
    std::uint32_t n = 0;
    std::size_t i = 0;
    // Seqlock: odd while the pin rows are being rewritten.
    v.sh->pinSeq = v.sh->pinSeq + 1;        // odd: mid-update
    MemoryBarrier();
    while (i <= rules.size() && n < (std::uint32_t)rtx::menu::kMaxPins) {
        std::size_t e = rules.find('\n', i);
        if (e == std::string::npos) e = rules.size();
        if (e > i) {
            const std::string line = rules.substr(i, e - i);
            const std::size_t tab = line.find('\t');
            const std::string verb = line.substr(0, tab == std::string::npos ? line.size() : tab);
            const std::string tgt = (tab == std::string::npos) ? std::string()
                                                                : line.substr(tab + 1);
            if (!verb.empty() && verb.size() < (std::size_t)rtx::menu::kVerbLen
                              && tgt.size()  < (std::size_t)rtx::menu::kTargetLen) {
                std::memcpy(v.sh->pins[n].verb, verb.data(), verb.size());
                v.sh->pins[n].verb[verb.size()] = 0;
                std::memcpy(v.sh->pins[n].target, tgt.data(), tgt.size());
                v.sh->pins[n].target[tgt.size()] = 0;
                ++n;
            }
        }
        if (e == rules.size()) break;
        i = e + 1;
    }
    v.sh->pinCount = n;                     // contents first, then the count
    MemoryBarrier();
    v.sh->pinSeq = v.sh->pinSeq + 1;        // even: complete
    return true;
}

std::string StatusJson(std::uint32_t pid) {
    View v(pid);
    if (!v) return "{}";
    const auto* sh = v.sh;
    std::string out = "{\"ok\":true";
    out += ",\"hooked\":";  out += (sh->flags & rtx::menu::kFlagHooked) ? "true" : "false";
    out += ",\"enabled\":"; out += sh->enable ? "true" : "false";
    out += ",\"unverified\":"; out += (sh->flags & rtx::menu::kFlagUnverified) ? "true" : "false";
    out += ",\"pinCount\":" + std::to_string(sh->pinCount);
    out += ",\"seq\":"     + std::to_string(sh->seq);
    out += ",\"handle\":"  + std::to_string(sh->handle);
    out += ",\"reordered\":" + std::to_string(sh->diag[1]);
    out += ",\"diag\":[";
    for (int i = 0; i < 4; ++i) { if (i) out += ','; out += std::to_string(sh->diag[i]); }
    out += "],\"stage\":[";
    for (int i = 0; i < 4; ++i) { if (i) out += ','; out += std::to_string(sh->stage[i]); }
    out += "],\"lane\":[";
    for (std::size_t i = 0; i < std::size(sh->lane); ++i) { if (i) out += ','; out += std::to_string(sh->lane[i]); }
    out += "],\"lastVerb\":\"";
    append_escaped(out, sh->lastVerb, rtx::menu::kVerbLen);
    out += '"';
    out += ",\"promo\":"      + std::to_string(sh->promoState);
    out += ",\"promoPrio\":"  + std::to_string(sh->promoPrio);
    out += ",\"promoPartner\":" + std::to_string(sh->promoPartnerPrio);
    out += ",\"promoSrc\":"     + std::to_string(sh->promoSource);
    out += ",\"promoVerb\":\"";
    append_escaped(out, sh->promoVerb, rtx::menu::kVerbLen);
    out += '"';
    out += ",\"pins\":[";
    {
        std::uint32_t pn = sh->pinCount;
        if (pn > (std::uint32_t)rtx::menu::kMaxPins) pn = rtx::menu::kMaxPins;
        for (std::uint32_t i = 0; i < pn; ++i) {
            if (i) out += ',';
            out += '"';
            append_escaped(out, sh->pins[i].verb, rtx::menu::kVerbLen);
            out += " on ";
            append_escaped(out, sh->pins[i].target, rtx::menu::kTargetLen);
            out += '"';
        }
    }
    out += ']';
    out += ",\"entries\":[";
    std::uint32_t n = sh->count;
    if (n > (std::uint32_t)rtx::menu::kMaxEntries) n = rtx::menu::kMaxEntries;
    for (std::uint32_t i = 0; i < n; ++i) {
        const auto& e = sh->entries[i];
        if (i) out += ',';
        out += "{\"verb\":\"";
        append_escaped(out, e.verb, rtx::menu::kVerbLen);
        out += "\",\"target\":\"";
        append_escaped(out, e.target, rtx::menu::kTargetLen);
        out += "\",\"slot\":" + std::to_string(e.slot);
        out += ",\"type\":"   + std::to_string(e.type);
        out += ",\"refs\":"   + std::to_string(e.refs) + '}';
    }
    out += "]}";
    return out;
}

}  // namespace rtx::launcher::menuswap
