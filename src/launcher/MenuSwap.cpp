#include "MenuSwap.h"
#include "../../companion/MenuShare.h"

#include <Windows.h>

#include <cstring>
#include <iterator>
#include <string>

namespace rtx::launcher::menuswap {
namespace {

// Open + map for one call. The panel polls at UI rate, so the open/close cost is irrelevant and
// it avoids holding a view across a client's exit.
struct View {
    HANDLE            map = nullptr;
    rtx::menu::Share* sh  = nullptr;

    explicit View(std::uint32_t pid) {
        if (!pid) return;
        wchar_t name[64];
        rtx::menu::MakeSectionName(pid, name);
        map = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!map) return;
        sh = (rtx::menu::Share*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                                              sizeof(rtx::menu::Share));
        // A section that exists but isn't initialised (or speaks another version) must not be
        // written through: treat it as absent.
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

void append_escaped(std::string& out, const char* s, std::size_t cap) {
    for (std::size_t i = 0; i < cap && s[i]; ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if ((unsigned char)c < 0x20)  out += ' ';      // menu text is plain; drop controls
        else out.push_back(c);
    }
}


}  // namespace

bool SetEnabled(std::uint32_t pid, bool on) {
    View v(pid);
    if (!v) return false;
    v.sh->enable = on ? 1u : 0u;
    return true;
}

// Rules arrive one per line as "verb<TAB>target" - an empty target means "any object".
// Order is significant: line i draws above line i+1.
bool SetPins(std::uint32_t pid, const std::string& rules) {
    View v(pid);
    if (!v) return false;
    std::uint32_t n = 0;
    std::size_t i = 0;
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
    v.sh->pinSeq = v.sh->pinSeq + 1;
    return true;
}

std::string StatusJson(std::uint32_t pid) {
    View v(pid);
    if (!v) return "{}";
    const auto* sh = v.sh;
    std::string out = "{\"ok\":true";
    out += ",\"hooked\":";  out += (sh->flags & rtx::menu::kFlagHooked) ? "true" : "false";
    out += ",\"enabled\":"; out += sh->enable ? "true" : "false";
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
    // Class-promotion verdict, so the panel can explain a refusal on its own.
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
            // Display only - the panel keeps the authoritative rules itself. A separator that
            // is not a control character, because a raw tab inside a JSON string is invalid and
            // would fail the parse for the whole payload.
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
