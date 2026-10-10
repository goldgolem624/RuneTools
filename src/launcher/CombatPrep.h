#pragma once
// The upload body of a combat log: what leaves this PC when an upload setting is on. Header only and
// standard library only, so the standalone test builds it without the launcher.
//
// From a log object (a compacted log, or a live head with an empty event list) it writes the same object
// with: dict.seqs emptied and dict.seqinfo (struct ids and tags per animation id) added as the last dict key,
// the hitmark name field removed, every name shaped like an internal developer name blanked, spawn names of
// the form lower_case_words replaced by a readable one, other players renamed "Player 1", "Player 2", ...
// (unless names are kept) with their names cleared from NPC names, mark texts and channel names, and the
// experience rows removed. Every other value is copied as the text it was read as; events are copied line
// by line and only a cleared text or a removed row changes them.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rtx::launcher::combatprep {

struct Options {
    bool keepNames = false;
    // When set: the pictures the log names (ability and buff sprites 's', items 'i') as small PNG data URLs in
    // dict.icons, so a shared report can show them; "" for one it does not have.
    std::function<std::string(char kind, int id)> icon;
};
struct Stats {
    int playersRenamed = 0, npcNamesFixed = 0, seqNamesDropped = 0, hitmarkNamesDropped = 0, seqinfo = 0,
        devNamesDropped = 0, nameStringsCleared = 0, xpDropped = 0, actorsRenumbered = 0;
    long long events = 0;
};

namespace detail {

// ---- a JSON tree that remembers the text of every scalar, so unchanged values are written back as read ----
struct Node {
    enum Kind : unsigned char { Null, Bool, Num, Str, Arr, Obj };
    Kind k = Null;
    std::string raw;                 // scalars: the source text (strings with their quotes and escapes)
    std::string s;                   // Str: the decoded text, UTF-8
    double num = 0;
    std::string key, keyRaw;         // set when this node is a member of an object
    std::vector<Node> c;             // array items or object members, in source order

    Node* get(const char* name) { for (auto& m : c) if (m.key == name) return &m; return nullptr; }
    const Node* get(const char* name) const { for (const auto& m : c) if (m.key == name) return &m; return nullptr; }
    bool remove(const char* name) {
        for (std::size_t i = 0; i < c.size(); ++i) if (c[i].key == name) { c.erase(c.begin() + (std::ptrdiff_t)i); return true; }
        return false;
    }
    std::string str(const char* name) const { const Node* v = get(name); return (v && v->k == Str) ? v->s : std::string(); }
    bool num_of(const char* name, long long& out) const {
        const Node* v = get(name);
        if (!v || v->k != Num) return false;
        out = (long long)v->num; return true;
    }
};

inline std::string quote(const std::string& s) {
    std::string o; o.reserve(s.size() + 2); o.push_back('"');
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o.push_back('\\'); o.push_back((char)ch); }
        else if (ch < 0x20) { static const char* hx = "0123456789abcdef"; o += "\\u00"; o.push_back(hx[ch >> 4]); o.push_back(hx[ch & 15]); }
        else o.push_back((char)ch);
    }
    o.push_back('"');
    return o;
}
inline void set_str(Node& n, const std::string& v) { n.k = Node::Str; n.s = v; n.raw = quote(v); n.c.clear(); }
inline void copy_value(Node& n, const Node& from) {      // the value of `from`, keeping n's own key
    const std::string key = n.key, keyRaw = n.keyRaw;
    n = from; n.key = key; n.keyRaw = keyRaw;
}
inline Node member(const std::string& key, Node v) { v.key = key; v.keyRaw = quote(key); return v; }
inline Node make_obj() { Node n; n.k = Node::Obj; return n; }
inline Node make_arr() { Node n; n.k = Node::Arr; return n; }
inline Node make_num(long long v) { Node n; n.k = Node::Num; n.num = (double)v; n.raw = std::to_string(v); return n; }
inline Node make_bool(bool v) { Node n; n.k = Node::Bool; n.raw = v ? "true" : "false"; n.num = v ? 1 : 0; return n; }

inline void put_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
}

struct Parser {
    const char* p = nullptr; const char* e = nullptr; int depth = 0;

    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    static int hexv(char h) { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; }
    bool hex4(std::uint32_t& v) {
        if (e - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) { const int h = hexv(p[i]); if (h < 0) return false; v = (v << 4) | (std::uint32_t)h; }
        p += 4; return true;
    }
    // At a '"': the decoded text into `out` (nullptr: only skip).
    bool str(std::string* out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            const unsigned char ch = (unsigned char)*p;
            if (ch < 0x20) return false;
            if (ch != '\\') { if (out) out->push_back((char)ch); ++p; continue; }
            ++p; if (p >= e) return false;
            const char esc = *p++;
            switch (esc) {
            case '"': if (out) out->push_back('"'); break;
            case '\\': if (out) out->push_back('\\'); break;
            case '/': if (out) out->push_back('/'); break;
            case 'b': if (out) out->push_back('\b'); break;
            case 'f': if (out) out->push_back('\f'); break;
            case 'n': if (out) out->push_back('\n'); break;
            case 'r': if (out) out->push_back('\r'); break;
            case 't': if (out) out->push_back('\t'); break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    const char* save = p; p += 2;
                    std::uint32_t lo = 0;
                    if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else p = save;
                }
                if (out) put_utf8(*out, cp);
                break;
            }
            default: return false;
            }
        }
        if (p >= e) return false;
        ++p; return true;
    }
    bool number() {
        const char* s = p;
        if (p < e && *p == '-') ++p;
        if (p >= e || *p < '0' || *p > '9') { p = s; return false; }
        if (*p == '0') ++p; else while (p < e && *p >= '0' && *p <= '9') ++p;
        if (p < e && *p == '.') { ++p; if (p >= e || *p < '0' || *p > '9') return false; while (p < e && *p >= '0' && *p <= '9') ++p; }
        if (p < e && (*p == 'e' || *p == 'E')) {
            ++p; if (p < e && (*p == '+' || *p == '-')) ++p;
            if (p >= e || *p < '0' || *p > '9') return false;
            while (p < e && *p >= '0' && *p <= '9') ++p;
        }
        return true;
    }
    bool literal(const char* w) {
        const std::size_t n = std::strlen(w);
        if ((std::size_t)(e - p) < n || std::strncmp(p, w, n) != 0) return false;
        p += n; return true;
    }
    // Skips one value; its text is [start, p).
    bool skip() {
        ws();
        if (p >= e) return false;
        if (*p == '"') return str(nullptr);
        if (*p == '{' || *p == '[') {
            if (++depth > 64) return false;
            const char close = *p == '{' ? '}' : ']';
            const bool obj = *p == '{';
            ++p; ws();
            if (p < e && *p == close) { ++p; --depth; return true; }
            for (;;) {
                if (obj) { ws(); if (!str(nullptr)) return false; ws(); if (p >= e || *p != ':') return false; ++p; }
                if (!skip()) return false;
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == close) { ++p; --depth; return true; }
                return false;
            }
        }
        if (literal("true") || literal("false") || literal("null")) return true;
        return number();
    }
    bool val(Node& n) {
        ws();
        if (p >= e) return false;
        if (*p == '{' || *p == '[') {
            if (++depth > 64) return false;
            const bool obj = *p == '{';
            const char close = obj ? '}' : ']';
            n.k = obj ? Node::Obj : Node::Arr;
            ++p; ws();
            if (p < e && *p == close) { ++p; --depth; return true; }
            for (;;) {
                Node m;
                if (obj) {
                    ws();
                    const char* ks = p;
                    if (!str(&m.key)) return false;
                    m.keyRaw.assign(ks, p);
                    ws(); if (p >= e || *p != ':') return false; ++p;
                }
                if (!val(m)) return false;
                n.c.push_back(std::move(m));
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == close) { ++p; --depth; return true; }
                return false;
            }
        }
        const char* s = p;
        if (*p == '"') { n.k = Node::Str; if (!str(&n.s)) return false; n.raw.assign(s, p); return true; }
        if (literal("true")) { n.k = Node::Bool; n.num = 1; n.raw = "true"; return true; }
        if (literal("false")) { n.k = Node::Bool; n.raw = "false"; return true; }
        if (literal("null")) { n.k = Node::Null; n.raw = "null"; return true; }
        if (!number()) return false;
        n.k = Node::Num; n.raw.assign(s, p);
        n.num = std::strtod(n.raw.c_str(), nullptr);
        return true;
    }
};

inline void write(const Node& n, std::string& o) {
    switch (n.k) {
    case Node::Arr:
        o.push_back('[');
        for (std::size_t i = 0; i < n.c.size(); ++i) { if (i) o.push_back(','); write(n.c[i], o); }
        o.push_back(']');
        break;
    case Node::Obj:
        o.push_back('{');
        for (std::size_t i = 0; i < n.c.size(); ++i) { if (i) o.push_back(','); o += n.c[i].keyRaw; o.push_back(':'); write(n.c[i], o); }
        o.push_back('}');
        break;
    default: o += n.raw; break;
    }
}

// One event "[type, c, ...]": the text spans of its items (offsets into the line).
struct EventView {
    int type = -1;                   // -1: not an event; 1000: a "mech" row
    std::vector<std::pair<std::size_t, std::size_t>> items;
};
inline bool view_event(const char* b, std::size_t n, EventView& v) {
    v.type = -1; v.items.clear();
    Parser ps; ps.p = b; ps.e = b + n;
    ps.ws();
    if (ps.p >= ps.e || *ps.p != '[') return false;
    ++ps.p; ps.ws();
    if (ps.p < ps.e && *ps.p == ']') return false;
    for (;;) {
        ps.ws();
        const char* s = ps.p;
        if (!ps.skip()) return false;
        v.items.push_back({ (std::size_t)(s - b), (std::size_t)(ps.p - b) });
        ps.ws();
        if (ps.p < ps.e && *ps.p == ',') { ++ps.p; continue; }
        if (ps.p < ps.e && *ps.p == ']') break;
        return false;
    }
    const auto& f = v.items[0];
    const std::string first(b + f.first, b + f.second);
    if (first == "\"mech\"") { v.type = 1000; return true; }
    if (first.empty() || first.size() > 4) return false;
    for (char ch : first) if (ch < '0' || ch > '9') return false;
    v.type = std::atoi(first.c_str());
    return true;
}
inline bool decode_string(const char* b, std::size_t n, std::string& out) {
    out.clear();
    Parser ps; ps.p = b; ps.e = b + n;
    return ps.str(&out) && ps.p == ps.e;
}

// ---- the name rules ----
inline bool is_space_byte(unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' || ch == '\r'; }

// ^[A-Z][A-Z0-9]*(_[A-Z0-9]+)+$
inline bool dev_shaped(const std::string& s) {
    if (s.empty() || s[0] < 'A' || s[0] > 'Z' || s.back() == '_') return false;
    bool under = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch == '_') { if (i > 0 && s[i - 1] == '_') return false; under = true; continue; }
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) return false;
    }
    return under;
}
// A fight's "Name & Name|mode" boss text with a developer-shaped name or mode in it.
inline bool dev_boss(const std::string& s) {
    const std::size_t bar = s.find('|');
    const std::string name = s.substr(0, bar);
    if (bar != std::string::npos && dev_shaped(s.substr(bar + 1))) return true;
    std::size_t a = 0;
    for (;;) {
        const std::size_t amp = name.find(" & ", a);
        std::string part = name.substr(a, amp == std::string::npos ? std::string::npos : amp - a);
        std::size_t lo = 0, hi = part.size();
        while (lo < hi && (part[lo] == ' ' || part[lo] == '\t' || part[lo] == '\n' || part[lo] == '\v' || part[lo] == '\f' || part[lo] == '\r')) ++lo;
        while (hi > lo && (part[hi - 1] == ' ' || part[hi - 1] == '\t' || part[hi - 1] == '\n' || part[hi - 1] == '\v' || part[hi - 1] == '\f' || part[hi - 1] == '\r')) --hi;
        if (dev_shaped(part.substr(lo, hi - lo))) return true;
        if (amp == std::string::npos) return false;
        a = amp + 3;
    }
}
// ^[a-z0-9_]+$ holding a '_'
inline bool spawn_shaped(const std::string& s) {
    if (s.empty()) return false;
    bool under = false;
    for (char ch : s) {
        if (ch == '_') { under = true; continue; }
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) return false;
    }
    return under;
}
// ^Player \d+$
inline bool player_n(const std::string& s) {
    if (s.size() < 8 || s.compare(0, 7, "Player ") != 0) return false;
    for (std::size_t i = 7; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
// Length the way a page counts it (UTF-16 units).
inline std::size_t text_len(const std::string& s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char ch = (unsigned char)s[i];
        if ((ch & 0xC0) == 0x80) continue;
        n += ch >= 0xF0 ? 2 : 1;
    }
    return n;
}
// Lower case, every run of space, '_', '-' and no-break space as one space.
inline std::string fold(const std::string& s) {
    std::string o; o.reserve(s.size());
    bool run = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = (unsigned char)s[i];
        bool sep = ch == ' ' || ch == '_' || ch == '-';
        if (!sep && ch == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA0) { sep = true; ++i; }
        if (sep) { if (!run) o.push_back(' '); run = true; continue; }
        run = false;
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        o.push_back((char)ch);
    }
    return o;
}
inline bool alnum_lower(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); }
// `folded` holds one of `names` (each folded) as a whole word.
inline bool holds_name(const std::string& folded, const std::vector<std::string>& names) {
    for (const auto& p : names) {
        if (p.empty()) continue;
        for (std::size_t at = folded.find(p); at != std::string::npos; at = folded.find(p, at + 1)) {
            const bool left = at == 0 || !alnum_lower(folded[at - 1]);
            const std::size_t end = at + p.size();
            const bool right = end >= folded.size() || !alnum_lower(folded[end]);
            if (left && right) return true;
        }
    }
    return false;
}
inline std::vector<std::string> fold_names(const std::vector<std::string>& names) {
    std::vector<std::string> o;
    for (const auto& n : names) if (text_len(n) >= 3) o.push_back(fold(n));
    return o;
}

inline std::string npc_label(long long id) { return id < 0 ? std::string("NPC") : "NPC " + std::to_string(id); }

// The abilities a fallback table names, for animation ids whose ability row is not in the log.
struct Fallback { int s; const char* name; };
inline const std::vector<Fallback>& fallback_abilities() {
    static const std::vector<Fallback> k = {
        { 48296, "Touch of Death" }, { 48297, "Finger of Death" }, { 48298, "Soul Sap" }, { 48299, "Soul Strike" },
        { 48301, "Volley of Souls" }, { 48308, "Bloat" }, { 48309, "Blood Siphon" }, { 48311, "Spectral Scythe" },
        { 48314, "Death Skulls" }, { 48324, "Living Death" }, { 33965, "Conjure Undead Army" }, { -3, "Command" },
        { 48303, "Command Skeleton Warrior" }, { 48307, "Command Vengeful Ghost" }, { 32342, "Command Phantom Guardian" },
        { 48305, "Command Putrid Zombie" }, { 48302, "Conjure Skeleton Warrior" }, { 48304, "Conjure Putrid Zombie" },
        { 48306, "Conjure Vengeful Ghost" }, { 31820, "Conjure Phantom Guardian" }, { 14881, "Global cooldown" },
        { 14882, "Global cooldown" } };
    return k;
}
inline const char* conjure_name(long long id) {
    switch (id) {
    case 30265: return "Skeleton Warrior";
    case 30266: return "Putrid Zombie";
    case 30267: return "Vengeful Ghost";
    case 31142: return "Phantom Guardian";
    default: return nullptr;
    }
}

inline bool parse_int_key(const std::string& k, long long& out) {
    if (k.empty() || k.size() > 19) return false;
    std::size_t i = k[0] == '-' ? 1 : 0;
    if (i >= k.size()) return false;
    for (std::size_t j = i; j < k.size(); ++j) if (k[j] < '0' || k[j] > '9') return false;
    out = std::strtoll(k.c_str(), nullptr, 10);
    return true;
}

}  // namespace detail

// The token rule of the stats module: upper case, a leading "GREATER " dropped, every "(...)" group dropped,
// every run of characters outside A-Z and 0-9 turned into one '_', '_' trimmed at both ends.
inline std::string Token(const std::string& name) {
    std::string u(name);
    for (char& ch : u) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (u.compare(0, 7, "GREATER") == 0) {
        std::size_t i = 7, n = 0;
        for (;;) {
            if (i < u.size() && detail::is_space_byte((unsigned char)u[i])) { ++i; ++n; continue; }
            if (i + 1 < u.size() && (unsigned char)u[i] == 0xC2 && (unsigned char)u[i + 1] == 0xA0) { i += 2; ++n; continue; }
            break;
        }
        if (n > 0) u.erase(0, i);
    }
    // shortest "(...)" groups, left to right; a group never spans a line break
    std::string v; v.reserve(u.size());
    for (std::size_t i = 0; i < u.size();) {
        if (u[i] == '(') {
            std::size_t j = i + 1; bool found = false;
            for (; j < u.size(); ++j) {
                if (u[j] == '\n' || u[j] == '\r') break;
                if (j + 2 < u.size() && (unsigned char)u[j] == 0xE2 && (unsigned char)u[j + 1] == 0x80 &&
                    ((unsigned char)u[j + 2] == 0xA8 || (unsigned char)u[j + 2] == 0xA9)) break;
                if (u[j] == ')') { found = true; break; }
            }
            if (found) { i = j + 1; continue; }
        }
        v.push_back(u[i]); ++i;
    }
    std::string o; o.reserve(v.size());
    bool run = false;
    for (char ch : v) {
        const bool keep = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (keep) { o.push_back(ch); run = false; }
        else if (!run) { o.push_back('_'); run = true; }
    }
    std::size_t a = 0, b = o.size();
    while (a < b && o[a] == '_') ++a;
    while (b > a && o[b - 1] == '_') --b;
    return o.substr(a, b - a);
}

// Player names (not yet "Player N") plus log.character of a log object or a live head.
inline std::vector<std::string> NamesForFilter(const std::string& headJson) {
    std::vector<std::string> out;
    detail::Node root; detail::Parser ps; ps.p = headJson.data(); ps.e = headJson.data() + headJson.size();
    if (!ps.val(root) || root.k != detail::Node::Obj) return out;
    if (const detail::Node* actors = root.get("actors"); actors && actors->k == detail::Node::Arr)
        for (const auto& a : actors->c)
            if (a.k == detail::Node::Obj && a.str("type") == "player") {
                const std::string n = a.str("name");
                if (!n.empty() && !detail::player_n(n)) out.push_back(n);
            }
    if (const detail::Node* log = root.get("log"); log && log->k == detail::Node::Obj) {
        const std::string ch = log->str("character");
        if (!ch.empty()) out.push_back(ch);
    }
    return out;
}

namespace detail {
// A mark (16) or channel (8) row's text item, or nullptr.
inline const std::pair<std::size_t, std::size_t>* text_item(const EventView& v, const char* b) {
    const std::size_t at = v.type == 16 ? 3 : v.type == 8 ? 4 : 0;
    if (!at || v.items.size() != at + 1) return nullptr;
    if (b[v.items[at].first] != '"') return nullptr;
    return &v.items[at];
}
// One event line through the rules: false = drop it; `out` = the line to keep (changed or not).
inline bool filter_event(const char* b, std::size_t n, const std::vector<std::string>& folded, const Options& opt,
                         std::string& out, Stats& st, EventView& v) {
    out.assign(b, n);
    if (!view_event(b, n, v)) return true;
    if (v.type == 15) { ++st.xpDropped; return false; }
    const auto* t = text_item(v, b);
    if (!t) return true;
    std::string text;
    if (!decode_string(b + t->first, t->second - t->first, text) || text.empty()) return true;
    bool clear = false;
    if (dev_shaped(text)) { clear = true; ++st.devNamesDropped; }
    else if (!opt.keepNames && holds_name(fold(text), folded)) { clear = true; ++st.nameStringsCleared; }
    if (clear) out = std::string(b, t->first) + "\"\"" + std::string(b + t->second, n - t->second);
    return true;
}
// Actor index items of an event row: positions in the array, and whether -1 (or a uid code <= -2) may stand there.
struct ActorItem { std::size_t at; bool optional; };
inline std::vector<ActorItem> actor_items(int type) {
    switch (type) {
    case 0: case 2: case 4: case 10: case 11: case 17: case 18: return { { 2, false } };
    case 3: return { { 2, false }, { 3, true } };
    case 13: case 23: return { { 2, true } };
    case 14: return { { 2, true }, { 3, true } };
    case 1000: return { { 6, true } };
    default: return {};
    }
}
// One event line with its actor indexes renumbered through `m` (old -> new). An optional index without a row
// becomes -1; a required one drops the line (false).
inline bool remap_actors(const char* b, std::size_t n, const std::unordered_map<long long, long long>& m,
                         std::string& out, EventView& v) {
    out.assign(b, n);
    if (!view_event(b, n, v)) return true;
    const std::vector<ActorItem> items = actor_items(v.type);
    if (items.empty()) return true;
    std::string o;
    std::size_t done = 0;
    for (const ActorItem& it : items) {
        if (it.at >= v.items.size()) continue;
        const auto sp = v.items[it.at];
        const std::string text(b + sp.first, sp.second - sp.first);
        char* end = nullptr;
        const long long old = std::strtoll(text.c_str(), &end, 10);
        if (text.empty() || !end || *end) continue;
        if (old < 0) continue;
        long long now = -1;
        if (auto f = m.find(old); f != m.end()) now = f->second;
        else if (!it.optional) return false;
        o.append(b + done, sp.first - done);
        o += std::to_string(now);
        done = sp.second;
    }
    if (!done) return true;
    o.append(b + done, n - done);
    out = std::move(o);
    return true;
}
}  // namespace detail

// Live chunk lines (one event array per line): type 15 rows dropped; mark and channel texts shaped like a
// developer name, or (unless keepNames) holding one of `names`, cleared. Other lines are copied as they are.
inline void FilterEventLines(const std::vector<std::string>& lines, const std::vector<std::string>& names,
                             const Options& opt, std::vector<std::string>& out, Stats& st) {
    const std::vector<std::string> folded = detail::fold_names(names);
    detail::EventView v; std::string line;
    for (const auto& l : lines) {
        if (!detail::filter_event(l.data(), l.size(), folded, opt, line, st, v)) continue;
        out.push_back(std::move(line));
        ++st.events;
    }
}

// The upload body from a log object's JSON text. False (with `err`) for a document that is not a format 1
// log with a 22 character id.
inline bool Prepare(const std::string& json, const Options& opt, std::string& out, Stats& st, std::string& err) {
    using detail::Node;
    out.clear(); err.clear(); st = Stats{};
    // the top level by hand: every member parsed, except the events, which are only cut into lines
    detail::Parser ps; ps.p = json.data(); ps.e = json.data() + json.size();
    Node root; root.k = Node::Obj;
    std::vector<std::pair<std::size_t, std::size_t>> events;   // spans into `json`
    std::size_t eventsAt = (std::size_t)-1;                     // index in root.c of the events placeholder
    ps.ws();
    if (ps.p >= ps.e || *ps.p != '{') { err = "not a log object"; return false; }
    ++ps.p; ps.ws();
    if (ps.p < ps.e && *ps.p == '}') { err = "not a log object"; return false; }
    for (;;) {
        Node m;
        ps.ws();
        const char* ks = ps.p;
        if (!ps.str(&m.key)) { err = "bad json"; return false; }
        m.keyRaw.assign(ks, ps.p);
        ps.ws(); if (ps.p >= ps.e || *ps.p != ':') { err = "bad json"; return false; }
        ++ps.p; ps.ws();
        if (m.key == "events" && ps.p < ps.e && *ps.p == '[' && eventsAt == (std::size_t)-1) {
            ++ps.p; ps.ws();
            if (ps.p < ps.e && *ps.p == ']') ++ps.p;
            else for (;;) {
                ps.ws();
                const char* s = ps.p;
                if (!ps.skip()) { err = "bad json"; return false; }
                events.push_back({ (std::size_t)(s - json.data()), (std::size_t)(ps.p - s) });
                ps.ws();
                if (ps.p < ps.e && *ps.p == ',') { ++ps.p; continue; }
                if (ps.p < ps.e && *ps.p == ']') { ++ps.p; break; }
                err = "bad json"; return false;
            }
            m.k = Node::Arr;
            eventsAt = root.c.size();
        } else if (!ps.val(m)) { err = "bad json"; return false; }
        root.c.push_back(std::move(m));
        ps.ws();
        if (ps.p < ps.e && *ps.p == ',') { ++ps.p; continue; }
        if (ps.p < ps.e && *ps.p == '}') { ++ps.p; break; }
        err = "bad json"; return false;
    }
    ps.ws();
    if (ps.p != ps.e) { err = "bad json"; return false; }

    const Node* format = root.get("format");
    if (!format || format->k != Node::Num || format->raw != "1") { err = "not format 1"; return false; }
    Node* log = root.get("log");
    if (!log || log->k != Node::Obj) { err = "no log"; return false; }
    {
        const std::string id = log->str("id");
        bool ok = id.size() == 22;
        for (char ch : id) if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) ok = false;
        if (!ok) { err = "bad log id"; return false; }
    }
    Node* actors = root.get("actors");
    if (!actors || actors->k != Node::Arr) { err = "no actors"; return false; }
    Node* dict = root.get("dict");
    if (!dict || dict->k != Node::Obj) { err = "no dict"; return false; }

    // 0. actor rows are written when first used, so a log can skip indexes; the rows are renumbered to their
    //    positions and every index in the events and fights follows
    std::unordered_map<long long, long long> remap;
    {
        bool gaps = false;
        for (std::size_t k = 0; k < actors->c.size(); ++k) {
            long long i = -1;
            if (actors->c[k].k != Node::Obj || !actors->c[k].num_of("i", i) || i != (long long)k) gaps = true;
        }
        if (gaps) {
            for (std::size_t k = 0; k < actors->c.size(); ++k) {
                Node& a = actors->c[k];
                if (a.k != Node::Obj) continue;
                long long i = -1;
                if (a.num_of("i", i) && !remap.count(i)) remap[i] = (long long)k;
                if (i != (long long)k) ++st.actorsRenumbered;
                if (Node* in = a.get("i")) { const std::string key = in->key, keyRaw = in->keyRaw; *in = detail::make_num((long long)k); in->key = key; in->keyRaw = keyRaw; }
                else a.c.insert(a.c.begin(), detail::member("i", detail::make_num((long long)k)));
            }
            if (Node* fights = root.get("fights"); fights && fights->k == Node::Arr)
                for (auto& f : fights->c) {
                    Node* t = f.k == Node::Obj ? f.get("targets") : nullptr;
                    if (!t || t->k != Node::Arr) continue;
                    std::vector<Node> keep;
                    for (const auto& x : t->c) {
                        if (x.k != Node::Num) continue;
                        if (auto it = remap.find((long long)x.num); it != remap.end()) keep.push_back(detail::make_num(it->second));
                    }
                    t->c = std::move(keep);
                }
        }
    }

    // 1. seqinfo from the animation names, then the names go
    {
        Node* seqs = dict->get("seqs");
        bool anyName = false;
        if (seqs && seqs->k == Node::Obj) for (const auto& m : seqs->c) if (m.k == Node::Str && !m.s.empty()) anyName = true;
        Node info = detail::make_obj();
        if (anyName) {
            std::map<long long, std::string> cand;                 // struct -> ability name
            std::unordered_map<long long, std::vector<long long>> byAnim;
            if (const Node* ab = dict->get("abilities"); ab && ab->k == Node::Obj)
                for (const auto& m : ab->c) {
                    long long s = 0;
                    if (!detail::parse_int_key(m.key, s)) continue;
                    cand[s] = m.k == Node::Obj ? m.str("name") : std::string();
                    long long anim = 0;
                    if (m.k == Node::Obj && m.num_of("anim", anim)) {
                        const Node* an = m.get("anim");
                        if (an && an->num == (double)anim) byAnim[anim].push_back(s);
                    }
                }
            for (const auto& f : detail::fallback_abilities()) if (!cand.count(f.s)) cand[f.s] = f.name;
            std::unordered_map<std::string, std::vector<long long>> byTok;
            std::size_t maxTok = 0;
            for (const auto& kv : cand) {
                const std::string t = Token(kv.second);
                if (t.empty()) continue;
                byTok[t].push_back(kv.first);
                maxTok = std::max(maxTok, t.size());
            }
            for (const auto& m : seqs->c) {
                const std::string name = m.k == Node::Str ? m.s : std::string();
                std::set<long long> ab;
                long long sid = 0;
                if (detail::parse_int_key(m.key, sid)) if (auto it = byAnim.find(sid); it != byAnim.end()) ab.insert(it->second.begin(), it->second.end());
                std::vector<std::string> parts;
                {
                    std::size_t a = 0;
                    for (;;) { const std::size_t u = name.find('_', a); parts.push_back(name.substr(a, u == std::string::npos ? std::string::npos : u - a)); if (u == std::string::npos) break; a = u + 1; }
                }
                if (!name.empty()) {
                    for (std::size_t i = 0; i < parts.size(); ++i) {
                        std::string run;
                        for (std::size_t j = i; j < parts.size(); ++j) {
                            if (j > i) run.push_back('_');
                            run += parts[j];
                            if (run.size() > maxTok) break;
                            if (auto it = byTok.find(run); it != byTok.end()) ab.insert(it->second.begin(), it->second.end());
                        }
                    }
                }
                int tag = name.find("ATTACK") != std::string::npos ? 1 : 0;
                if (std::find(parts.begin(), parts.end(), "DEATH") != parts.end()) tag |= 2;
                if (ab.empty() && tag == 0) continue;
                Node row = detail::make_obj();
                Node arr = detail::make_arr();
                for (long long s : ab) arr.c.push_back(detail::make_num(s));
                row.c.push_back(detail::member("ab", std::move(arr)));
                row.c.push_back(detail::member("tag", detail::make_num(tag)));
                row.key = m.key; row.keyRaw = m.keyRaw;
                info.c.push_back(std::move(row));
            }
        }
        if (seqs) {
            if (seqs->k == Node::Obj) st.seqNamesDropped = (int)seqs->c.size();
            const std::string key = seqs->key, keyRaw = seqs->keyRaw;
            *seqs = detail::make_obj(); seqs->key = key; seqs->keyRaw = keyRaw;
        } else dict->c.push_back(detail::member("seqs", detail::make_obj()));
        if (anyName) {
            dict->remove("seqinfo");
            st.seqinfo = (int)info.c.size();
            dict->c.push_back(detail::member("seqinfo", std::move(info)));
        } else if (const Node* old = dict->get("seqinfo")) {
            st.seqinfo = old->k == Node::Obj ? (int)old->c.size() : 0;
        } else dict->c.push_back(detail::member("seqinfo", detail::make_obj()));
    }

    // 2. hitmark names
    if (Node* hm = dict->get("hitmarks"); hm && hm->k == Node::Obj)
        for (auto& m : hm->c) if (m.k == Node::Obj && m.remove("name")) ++st.hitmarkNamesDropped;

    // 3. developer-shaped names blanked
    auto guard = [&](Node* n) { if (n && n->k == Node::Str && detail::dev_shaped(n->s)) { detail::set_str(*n, ""); ++st.devNamesDropped; } };
    for (auto& a : actors->c) if (a.k == Node::Obj) guard(a.get("name"));
    for (const char* kind : { "abilities", "buffs" })
        if (Node* d = dict->get(kind); d && d->k == Node::Obj) for (auto& m : d->c) if (m.k == Node::Obj) guard(m.get("name"));
    if (Node* d = dict->get("encounters"); d && d->k == Node::Obj) for (auto& m : d->c) guard(&m);
    if (Node* d = dict->get("trackers"); d && d->k == Node::Obj)
        for (auto& m : d->c) {
            if (m.k != Node::Obj) continue;
            guard(m.get("name"));
            if (Node* cols = m.get("cols"); cols && cols->k == Node::Obj) for (auto& col : cols->c) guard(&col);
        }
    if (Node* d = dict->get("mechs"); d && d->k == Node::Obj)
        for (auto& boss : d->c) if (boss.k == Node::Obj) for (auto& k : boss.c) if (k.k == Node::Obj) guard(k.get("label"));
    // a fight's boss text is a copy of an encounter name: an empty or developer-shaped one becomes null
    if (Node* fights = root.get("fights"); fights && fights->k == Node::Arr)
        for (auto& f : fights->c) {
            Node* b = f.k == Node::Obj ? f.get("boss") : nullptr;
            if (!b || b->k != Node::Str) continue;
            const bool dev = detail::dev_boss(b->s);
            if (!dev && !b->s.empty()) continue;
            if (dev) ++st.devNamesDropped;
            const std::string key = b->key, keyRaw = b->keyRaw;
            *b = Node{}; b->raw = "null"; b->key = key; b->keyRaw = keyRaw;
        }

    // 4. spawn names of the form lower_case_words
    std::set<std::size_t> fixed;
    std::vector<std::string> folded;                               // the other players' names, folded (5.)
    {
        std::vector<Node*> npcs;
        for (auto& a : actors->c) if (a.k == Node::Obj && a.str("type") == "npc") npcs.push_back(&a);
        std::vector<std::pair<std::size_t, Node>> repl;            // actor position -> new name value
        for (std::size_t i = 0; i < npcs.size(); ++i) {
            Node* nm = npcs[i]->get("name");
            if (!nm || nm->k != Node::Str || !detail::spawn_shaped(nm->s)) continue;
            long long id = -1; npcs[i]->num_of("id", id);
            const Node* sib = nullptr;
            for (std::size_t j = 0; j < npcs.size() && !sib; ++j) {
                if (j == i) continue;
                long long jd = -1;
                if (!npcs[j]->num_of("id", jd) || jd != id) continue;
                const Node* jn = npcs[j]->get("name");
                if (jn && jn->k == Node::Str && !jn->s.empty() && !detail::spawn_shaped(jn->s)) sib = jn;
            }
            Node v;
            if (sib) { v = *sib; }
            else { set_str(v, detail::conjure_name(id) ? std::string(detail::conjure_name(id)) : detail::npc_label(id)); }
            repl.push_back({ i, v });
        }
        for (auto& r : repl) {
            Node* nm = npcs[r.first]->get("name");
            detail::copy_value(*nm, r.second);
            fixed.insert(r.first);
        }
        st.npcNamesFixed = (int)fixed.size();
        // 5. other players: their names out of NPC names, marks and channels, then "Player N"
        if (!opt.keepNames) {
            std::vector<std::string> names;
            for (auto& a : actors->c)
                if (a.k == Node::Obj && a.str("type") == "player") { const std::string n = a.str("name"); if (!detail::player_n(n)) names.push_back(n); }
            folded = detail::fold_names(names);
            if (!folded.empty())
                for (std::size_t i = 0; i < npcs.size(); ++i) {
                    Node* nm = npcs[i]->get("name");
                    if (!nm || nm->k != Node::Str || !detail::holds_name(detail::fold(nm->s), folded)) continue;
                    long long id = -1; npcs[i]->num_of("id", id);
                    detail::set_str(*nm, detail::npc_label(id));
                    fixed.insert(i);
                }
            st.npcNamesFixed = (int)fixed.size();
            int k = 0;
            for (auto& a : actors->c) {
                if (a.k != Node::Obj || a.str("type") != "player") continue;
                const std::string want = "Player " + std::to_string(++k);
                Node* nm = a.get("name");
                if (!nm) { a.c.push_back(detail::member("name", Node{})); nm = &a.c.back(); }
                if (nm->k != Node::Str || nm->s != want) { detail::set_str(*nm, want); ++st.playersRenamed; }
            }
        }
    }
    if (Node* an = log->get("anonymised")) { const std::string key = an->key, keyRaw = an->keyRaw; *an = detail::make_bool(!opt.keepNames); an->key = key; an->keyRaw = keyRaw; }
    else log->c.push_back(detail::member("anonymised", detail::make_bool(!opt.keepNames)));

    // icons: every sprite and item the dictionary names, once each, capped
    dict->remove("icons");
    if (opt.icon) {
        std::set<std::pair<char, long long>> want;
        auto addNum = [&](const Node* n, char kind) { if (n && n->k == Node::Num && n->num > 0 && n->num < 2147483647.0) want.insert({ kind, (long long)n->num }); };
        if (const Node* ab = dict->get("abilities"); ab && ab->k == Node::Obj) for (const auto& m : ab->c) if (m.k == Node::Obj) addNum(m.get("icon"), 's');
        if (const Node* bf = dict->get("buffs"); bf && bf->k == Node::Obj) for (const auto& m : bf->c) if (m.k == Node::Obj) { addNum(m.get("icon"), 's'); addNum(m.get("item"), 'i'); }
        if (const Node* it = dict->get("items"); it && it->k == Node::Obj) for (const auto& m : it->c) { const long long id = std::atoll(m.key.c_str()); if (id > 0) want.insert({ 'i', id }); }
        Node icons = detail::make_obj();
        for (const auto& w : want) {
            if (icons.c.size() >= 300) break;
            const std::string url = opt.icon(w.first, (int)w.second);
            if (url.empty() || url.size() > 8000 || url.compare(0, 22, "data:image/png;base64,") != 0) continue;
            Node v; detail::set_str(v, url);
            icons.c.push_back(detail::member(std::string(1, w.first) + ":" + std::to_string(w.second), std::move(v)));
        }
        dict->c.push_back(detail::member("icons", std::move(icons)));
    }

    // write; the events: experience rows out, mark and channel texts through the same rules
    out.reserve(json.size());
    out.push_back('{');
    detail::EventView v; std::string line;
    for (std::size_t i = 0; i < root.c.size(); ++i) {
        if (i) out.push_back(',');
        out += root.c[i].keyRaw; out.push_back(':');
        if (i != eventsAt) { detail::write(root.c[i], out); continue; }
        out.push_back('[');
        bool first = true;
        std::string moved;
        for (const auto& sp : events) {
            if (!detail::filter_event(json.data() + sp.first, sp.second, folded, opt, line, st, v)) continue;
            if (!remap.empty()) {
                if (!detail::remap_actors(line.data(), line.size(), remap, moved, v)) continue;
                line.swap(moved);
            }
            if (!first) out += ",\n";
            first = false;
            out += line;
            ++st.events;
        }
        out.push_back(']');
    }
    out += "}\n";
    return true;
}

}  // namespace rtx::launcher::combatprep
