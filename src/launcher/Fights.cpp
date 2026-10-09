#include "Fights.h"
#include "../reader/Hitmarks.h"
#include "../cache/vendor/zlib/zlib.h"

#include <Windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace rtx::launcher::fights {
namespace {

std::mutex g_mu;                 // the root, the flags and index.json
std::filesystem::path g_root;

// The folder is created by a write, never by a read (the setting is polled while recording is off).
std::filesystem::path root_locked(bool create = true) {
    if (g_root.empty()) {
        wchar_t up[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) > 0) g_root = std::filesystem::path(up) / L"RuneToolsX" / L"combat";
        else g_root = L"combat";
    }
    if (create) { std::error_code ec; std::filesystem::create_directories(g_root, ec); }
    return g_root;
}

// The hitmark kinds the viewer counts as damage (panel_fights_stats.js); the rest are text, zero marks,
// heals and the other players' set.
bool damage_kind(const std::string& k) {
    static const char* kinds[] = { "melee", "melee crit", "ranged", "ranged crit", "magic", "magic crit", "necromancy", "necromancy crit", "conjure", "conjure crit",
                                   "typeless", "poison", "deflect", "cannon", "split soul", "blight", "pierced shield", "pool", "shielded boss", "instant kill", "instant kill (soft)" };
    for (const char* x : kinds) if (k == x) return true;
    return false;
}

bool flag_read(const wchar_t* name) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::ifstream f(root_locked(false) / name);
    int v = 0;
    return f && (f >> v) && v != 0;
}
void flag_write(const wchar_t* name, bool on) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::ofstream f(root_locked() / name, std::ios::trunc);
    if (f) f << (on ? 1 : 0);
}

std::string jesc(const std::string& s) {
    std::string o; o.reserve(s.size() + 2);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back((char)c);
    }
    return o;
}
std::string jstr(const std::string& s) { return "\"" + jesc(s) + "\""; }

std::string safe_name(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') o.push_back((char)c);
        else if (c == ' ') o.push_back('_');
    }
    return o.empty() ? std::string("unknown") : o;
}

bool read_file(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}
bool write_file(const std::filesystem::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

// ---- a small JSON reader for the header, actor, fight and dict lines and index.json ----
struct JV {
    enum K { Null, Bool, Num, Str, Arr, Obj } k = Null;
    bool b = false; double num = 0; std::string s; std::vector<JV> a; std::vector<std::pair<std::string, JV>> o;
    const JV* get(const char* key) const { for (const auto& kv : o) if (kv.first == key) return &kv.second; return nullptr; }
    long long i(const char* key, long long def = 0) const { const JV* v = get(key); return (v && v->k == Num) ? (long long)v->num : def; }
    std::string str(const char* key) const { const JV* v = get(key); return (v && v->k == Str) ? v->s : std::string(); }
};
struct JP {
    const char* p; const char* e; int depth = 0;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\') {
                ++p; if (p >= e) return false;
                switch (*p) {
                case 'n': out.push_back('\n'); break; case 't': out.push_back('\t'); break; case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break;
                case 'u': {
                    if (p + 4 >= e) return false;
                    unsigned cp = (unsigned)std::strtoul(std::string(p + 1, p + 5).c_str(), nullptr, 16); p += 4;
                    if (cp < 0x80) out.push_back((char)cp);
                    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                    else { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                    break;
                }
                default: out.push_back(*p); break;
                }
                ++p;
            } else out.push_back(*p++);
        }
        if (p >= e) return false;
        ++p; return true;
    }
    bool val(JV& v) {
        ws();
        if (p >= e || ++depth > 64) return false;
        struct D { int& d; ~D() { --d; } } _d{ depth };
        if (*p == '{') {
            ++p; v.k = JV::Obj;
            ws(); if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws(); std::string k; if (!str(k)) return false;
                ws(); if (p >= e || *p != ':') return false; ++p;
                JV c; if (!val(c)) return false;
                v.o.emplace_back(std::move(k), std::move(c));
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[') {
            ++p; v.k = JV::Arr;
            ws(); if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                JV c; if (!val(c)) return false;
                v.a.push_back(std::move(c));
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"') { v.k = JV::Str; return str(v.s); }
        if (e - p >= 4 && std::strncmp(p, "true", 4) == 0) { p += 4; v.k = JV::Bool; v.b = true; return true; }
        if (e - p >= 5 && std::strncmp(p, "false", 5) == 0) { p += 5; v.k = JV::Bool; v.b = false; return true; }
        if (e - p >= 4 && std::strncmp(p, "null", 4) == 0) { p += 4; v.k = JV::Null; return true; }
        char* end = nullptr;
        const double d = std::strtod(p, &end);
        if (!end || end == p || end > e) return false;
        v.k = JV::Num; v.num = d; p = end; return true;
    }
};
bool parse_json(const std::string& t, JV& out) {
    JP jp{ t.data(), t.data() + t.size() };
    if (!jp.val(out)) return false;
    jp.ws();
    return jp.p == jp.e;
}
// Serialises a parsed value back (compact), for dict objects carried through the compaction.
void emit_json(const JV& v, std::string& o) {
    switch (v.k) {
    case JV::Null: o += "null"; break;
    case JV::Bool: o += v.b ? "true" : "false"; break;
    case JV::Num: { char b[40]; if (v.num == (double)(long long)v.num) std::snprintf(b, sizeof(b), "%lld", (long long)v.num); else std::snprintf(b, sizeof(b), "%.10g", v.num); o += b; break; }
    case JV::Str: o += jstr(v.s); break;
    case JV::Arr: o += "["; for (std::size_t i = 0; i < v.a.size(); ++i) { if (i) o += ","; emit_json(v.a[i], o); } o += "]"; break;
    case JV::Obj: o += "{"; for (std::size_t i = 0; i < v.o.size(); ++i) { if (i) o += ","; o += jstr(v.o[i].first) + ":"; emit_json(v.o[i].second, o); } o += "}"; break;
    }
}

// The integers of an event line "[t,c,f...]" up to its first string, if any.
void event_ints(const std::string& line, std::vector<long long>& out) {
    out.clear();
    const char* p = line.c_str() + 1; const char* e = line.c_str() + line.size();
    while (p < e) {
        while (p < e && (*p == ',' || *p == ' ')) ++p;
        if (p >= e || *p == ']' || *p == '"') break;
        char* end = nullptr;
        const long long v = std::strtoll(p, &end, 10);
        if (!end || end == p) break;
        out.push_back(v); p = end;
    }
}

// ---- gzip: a DEFLATE encoder (LZ77 + dynamic Huffman blocks) and the gzip framing; the launcher's zlib
// carries only the inflate half ----
struct BitOut {
    std::string& out; std::uint64_t acc = 0; int n = 0;
    explicit BitOut(std::string& o) : out(o) {}
    void put(std::uint32_t v, int bits) { acc |= (std::uint64_t)v << n; n += bits; while (n >= 8) { out.push_back((char)(acc & 0xFF)); acc >>= 8; n -= 8; } }
    void code(std::uint32_t c, int len) { std::uint32_t r = 0; for (int i = 0; i < len; ++i) { r = (r << 1) | (c & 1); c >>= 1; } put(r, len); }
    void flush() { if (n > 0) { out.push_back((char)(acc & 0xFF)); acc = 0; n = 0; } }
};
const int kLenBase[29]  = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const int kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const int kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
const int kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
const int kClOrder[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

// Huffman code lengths from frequencies, no longer than `limit` (frequencies are halved until they fit).
void huff_lengths(std::vector<std::uint32_t> freq, int limit, std::vector<std::uint8_t>& lens) {
    const std::size_t n = freq.size();
    lens.assign(n, 0);
    std::vector<std::size_t> used;
    for (std::size_t i = 0; i < n; ++i) if (freq[i]) used.push_back(i);
    if (used.empty()) return;
    if (used.size() == 1) { lens[used[0]] = 1; lens[used[0] == 0 ? 1 : 0] = 1; return; }
    for (;;) {
        std::vector<int> parent(2 * n, -1);
        using Q = std::pair<std::uint64_t, int>;
        std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
        for (std::size_t i : used) q.push({ freq[i], (int)i });
        int next = (int)n;
        while (q.size() > 1) {
            Q a = q.top(); q.pop(); Q b = q.top(); q.pop();
            parent[(std::size_t)a.second] = next; parent[(std::size_t)b.second] = next;
            q.push({ a.first + b.first, next }); ++next;
        }
        int maxLen = 0;
        for (std::size_t i : used) {
            int d = 0; for (int p = parent[i]; p >= 0; p = parent[(std::size_t)p]) ++d;
            lens[i] = (std::uint8_t)d; if (d > maxLen) maxLen = d;
        }
        if (maxLen <= limit) return;
        for (std::size_t i : used) freq[i] = (freq[i] + 1) / 2;
    }
}
void huff_codes(const std::vector<std::uint8_t>& lens, std::vector<std::uint32_t>& codes) {
    codes.assign(lens.size(), 0);
    std::uint32_t blCount[16] = {}, nextCode[16] = {};
    for (std::uint8_t l : lens) if (l) ++blCount[l];
    std::uint32_t code = 0;
    for (int bits = 1; bits < 16; ++bits) { code = (code + blCount[bits - 1]) << 1; nextCode[bits] = code; }
    for (std::size_t i = 0; i < lens.size(); ++i) if (lens[i]) codes[i] = nextCode[lens[i]]++;
}
struct Tok { std::uint16_t v; std::uint16_t dist; };   // dist 0: literal v; else match of length v

void deflate_block(BitOut& bo, const std::vector<Tok>& toks, bool last) {
    std::vector<std::uint32_t> lf(286, 0), df(30, 0);
    for (const Tok& t : toks) {
        if (!t.dist) { ++lf[t.v]; continue; }
        int lc = 28; while (lc > 0 && kLenBase[lc] > t.v) --lc;
        ++lf[257 + lc];
        int dc = 29; while (dc > 0 && kDistBase[dc] > t.dist) --dc;
        ++df[dc];
    }
    ++lf[256];
    std::vector<std::uint8_t> ll, dl;
    huff_lengths(lf, 15, ll);
    huff_lengths(df, 15, dl);
    if (dl.empty() || std::count_if(dl.begin(), dl.end(), [](std::uint8_t x) { return x != 0; }) == 0) { dl.assign(30, 0); dl[0] = 1; dl[1] = 1; }
    int hlit = 286; while (hlit > 257 && !ll[(std::size_t)hlit - 1]) --hlit;
    int hdist = 30; while (hdist > 1 && !dl[(std::size_t)hdist - 1]) --hdist;
    std::vector<std::uint8_t> seq(ll.begin(), ll.begin() + hlit); seq.insert(seq.end(), dl.begin(), dl.begin() + hdist);
    // run-length form: 16 repeats the previous length 3..6 times, 17 = 3..10 zeros, 18 = 11..138 zeros
    std::vector<std::pair<int, int>> cl;   // (symbol, extra value)
    for (std::size_t i = 0; i < seq.size();) {
        const std::uint8_t v = seq[i]; std::size_t run = 1;
        while (i + run < seq.size() && seq[i + run] == v) ++run;
        if (v == 0 && run >= 3) {
            std::size_t r = std::min<std::size_t>(run, 138);
            if (r >= 11) cl.push_back({ 18, (int)r - 11 }); else cl.push_back({ 17, (int)r - 3 });
            i += r; continue;
        }
        if (v != 0 && run >= 4) {
            cl.push_back({ v, 0 }); std::size_t left = run - 1, used = 1;
            while (left >= 3) { std::size_t r = std::min<std::size_t>(left, 6); cl.push_back({ 16, (int)r - 3 }); left -= r; used += r; }
            i += used; continue;
        }
        cl.push_back({ v, 0 }); ++i;
    }
    std::vector<std::uint32_t> cf(19, 0);
    for (const auto& c : cl) ++cf[(std::size_t)c.first];
    std::vector<std::uint8_t> cll; huff_lengths(cf, 7, cll);
    int hclen = 19; while (hclen > 4 && !cll[(std::size_t)kClOrder[hclen - 1]]) --hclen;
    std::vector<std::uint32_t> lcodes, dcodes, ccodes;
    huff_codes(ll, lcodes); huff_codes(dl, dcodes); huff_codes(cll, ccodes);
    bo.put(last ? 1 : 0, 1); bo.put(2, 2);
    bo.put((std::uint32_t)(hlit - 257), 5); bo.put((std::uint32_t)(hdist - 1), 5); bo.put((std::uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; ++i) bo.put(cll[(std::size_t)kClOrder[i]], 3);
    for (const auto& c : cl) {
        bo.code(ccodes[(std::size_t)c.first], cll[(std::size_t)c.first]);
        if (c.first == 16) bo.put((std::uint32_t)c.second, 2);
        else if (c.first == 17) bo.put((std::uint32_t)c.second, 3);
        else if (c.first == 18) bo.put((std::uint32_t)c.second, 7);
    }
    for (const Tok& t : toks) {
        if (!t.dist) { bo.code(lcodes[t.v], ll[t.v]); continue; }
        int lc = 28; while (lc > 0 && kLenBase[lc] > t.v) --lc;
        bo.code(lcodes[(std::size_t)257 + lc], ll[(std::size_t)257 + lc]);
        if (kLenExtra[lc]) bo.put((std::uint32_t)(t.v - kLenBase[lc]), kLenExtra[lc]);
        int dc = 29; while (dc > 0 && kDistBase[dc] > t.dist) --dc;
        bo.code(dcodes[(std::size_t)dc], dl[(std::size_t)dc]);
        if (kDistExtra[dc]) bo.put((std::uint32_t)(t.dist - kDistBase[dc]), kDistExtra[dc]);
    }
    bo.code(lcodes[256], ll[256]);
}

void deflate(const std::string& in, std::string& out) {
    BitOut bo(out);
    const std::size_t n = in.size();
    const unsigned char* s = reinterpret_cast<const unsigned char*>(in.data());
    constexpr int kWin = 32768, kHash = 1 << 15, kChain = 48, kBlockToks = 32768;
    std::vector<int> head((std::size_t)kHash, -1), prev((std::size_t)kWin, -1);
    auto hash3 = [&](std::size_t i) { return (int)(((std::uint32_t)s[i] * 0x9E3779B1u ^ ((std::uint32_t)s[i + 1] << 11) ^ ((std::uint32_t)s[i + 2] << 22)) >> 17) & (kHash - 1); };
    auto insert = [&](std::size_t i) { if (i + 2 < n) { const int h = hash3(i); prev[i & (kWin - 1)] = head[(std::size_t)h]; head[(std::size_t)h] = (int)i; } };
    std::vector<Tok> toks; toks.reserve((std::size_t)kBlockToks);
    std::size_t i = 0;
    if (n == 0) { deflate_block(bo, toks, true); bo.flush(); return; }
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[(std::size_t)hash3(i)];
            for (int c = 0; c < kChain && cand >= 0 && (std::size_t)cand < i && i - (std::size_t)cand <= (std::size_t)kWin - 1; ++c) {
                const std::size_t maxLen = std::min<std::size_t>(258, n - i);
                std::size_t l = 0;
                while (l < maxLen && s[(std::size_t)cand + l] == s[i + l]) ++l;
                if ((int)l > bestLen) { bestLen = (int)l; bestDist = (int)(i - (std::size_t)cand); if (l == maxLen) break; }
                cand = prev[(std::size_t)cand & (kWin - 1)];
            }
        }
        if (bestLen >= 3) {
            toks.push_back({ (std::uint16_t)bestLen, (std::uint16_t)bestDist });
            for (int k = 0; k < bestLen; ++k) insert(i + (std::size_t)k);
            i += (std::size_t)bestLen;
        } else {
            toks.push_back({ (std::uint16_t)s[i], 0 });
            insert(i); ++i;
        }
        if ((int)toks.size() >= kBlockToks || i >= n) { deflate_block(bo, toks, i >= n); toks.clear(); }
    }
    bo.flush();
}

std::uint32_t crc32_of(const std::string& d) {
    static std::uint32_t table[256]; static bool init = false;
    if (!init) { for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---- index.json ----
std::filesystem::path index_path_locked(bool create = false) { return root_locked(create) / L"index.json"; }

bool row_from_json(const JV& v, IndexRow& r) {
    if (v.k != JV::Obj) return false;
    r.id = v.str("id"); r.character = v.str("character"); r.file = v.str("file");
    r.startedAt = v.i("startedAt"); r.endedAt = v.i("endedAt"); r.bytes = v.i("bytes"); r.events = v.i("events"); r.version = (int)v.i("version", 1);
    if (const JV* fs = v.get("fights"); fs && fs->k == JV::Arr) {
        for (const JV& f : fs->a) {
            IndexFight x; x.n = (int)f.i("n"); x.start = f.i("start"); x.end = f.i("end"); x.kind = f.str("kind"); x.boss = f.str("boss");
            x.kills = (int)f.i("kills"); x.deaths = (int)f.i("deaths");
            if (const JV* s = f.get("summary"); s && s->k == JV::Obj) {
                x.summary.durMs = s->i("durMs"); x.summary.dealt = s->i("dealt"); x.summary.taken = s->i("taken"); x.summary.healed = s->i("healed");
                x.summary.hits = s->i("hits"); x.summary.crits = s->i("crits"); x.summary.maxHit = s->i("maxHit"); x.summary.blocked = s->i("blocked");
                x.summary.deaths = s->i("deaths"); x.summary.kills = s->i("kills");
                const JV* d = s->get("dps"); x.summary.dps = (d && d->k == JV::Num) ? d->num : 0; d = s->get("dpm"); x.summary.dpm = (d && d->k == JV::Num) ? d->num : 0;
            }
            r.fights.push_back(std::move(x));
        }
    }
    return !r.id.empty();
}
std::vector<IndexRow> index_read_locked() {
    std::vector<IndexRow> rows;
    std::string text; JV v;
    if (read_file(index_path_locked(), text) && parse_json(text, v) && v.k == JV::Arr)
        for (const JV& e : v.a) { IndexRow r; if (row_from_json(e, r)) rows.push_back(std::move(r)); }
    return rows;
}
void index_write_locked(std::vector<IndexRow>& rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const IndexRow& a, const IndexRow& b) { return a.startedAt > b.startedAt; });
    std::string o = "[";
    for (std::size_t i = 0; i < rows.size(); ++i) { if (i) o += ",\n"; o += RowJson(rows[i]); }
    o += "]\n";
    write_file(index_path_locked(true), o);
}
void index_put_locked(const IndexRow& r) {
    auto rows = index_read_locked();
    bool found = false;
    for (auto& x : rows) if (x.id == r.id) { x = r; found = true; }
    if (!found) rows.push_back(r);
    index_write_locked(rows);
}

std::string summary_json(const Summary& s) {
    char b[320];
    std::snprintf(b, sizeof(b), "{\"durMs\":%lld,\"dealt\":%lld,\"taken\":%lld,\"healed\":%lld,\"dps\":%.2f,\"dpm\":%.1f,\"hits\":%lld,\"crits\":%lld,\"maxHit\":%lld,\"blocked\":%lld,\"deaths\":%lld,\"kills\":%lld}",
                  s.durMs, s.dealt, s.taken, s.healed, s.dps, s.dpm, s.hits, s.crits, s.maxHit, s.blocked, s.deaths, s.kills);
    return b;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((std::size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((std::size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

std::filesystem::path Root() { std::lock_guard<std::mutex> lk(g_mu); return root_locked(); }
void SetRoot(const std::filesystem::path& p) { std::lock_guard<std::mutex> lk(g_mu); g_root = p; }

bool RecordEnabled() { return flag_read(L"combat_record.txt"); }
void SetRecordEnabled(bool on) { flag_write(L"combat_record.txt", on); }
bool UploadAuto() { return flag_read(L"combat_upload.txt"); }
void SetUploadAuto(bool on) { flag_write(L"combat_upload.txt", on); }
bool KeepNames() { return flag_read(L"combat_names.txt"); }
void SetKeepNames(bool on) { flag_write(L"combat_names.txt", on); }

std::string NewLogId() {
    unsigned char b[16];
    if (BCryptGenRandom(nullptr, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        const std::uint64_t t = (std::uint64_t)GetTickCount64() ^ ((std::uint64_t)GetCurrentProcessId() << 40) ^ (std::uint64_t)std::time(nullptr);
        for (int i = 0; i < 16; ++i) b[i] = (unsigned char)((t >> ((i % 8) * 8)) ^ (unsigned)(std::rand() & 0xFF));
    }
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string o;
    for (int i = 0; i < 16; i += 3) {
        const std::uint32_t v = ((std::uint32_t)b[i] << 16) | ((std::uint32_t)(i + 1 < 16 ? b[i + 1] : 0) << 8) | (i + 2 < 16 ? b[i + 2] : 0);
        o.push_back(k[(v >> 18) & 63]); o.push_back(k[(v >> 12) & 63]);
        if (i + 1 < 16) o.push_back(k[(v >> 6) & 63]);
        if (i + 2 < 16) o.push_back(k[v & 63]);
    }
    return o;
}

// ---- the open log ----
bool OpenLog::Open(const std::string& character, std::uint32_t pid) {
    Close();
    std::filesystem::path dir;
    { std::lock_guard<std::mutex> lk(g_mu); dir = root_locked() / to_wide(safe_name(character)); }
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    path_ = dir / (L"current_" + std::to_wstring(pid) + L".jsonl");
    if (_wfopen_s(&f_, path_.c_str(), L"wb") != 0) { f_ = nullptr; return false; }
    return true;
}
bool OpenLog::Append(const std::vector<std::string>& lines) {
    if (!f_) return false;
    for (const auto& l : lines) { std::fwrite(l.data(), 1, l.size(), f_); std::fputc('\n', f_); }
    std::fflush(f_);
    return true;
}
void OpenLog::Close() {
    if (f_) { std::fclose(f_); f_ = nullptr; }
}

std::string RowJson(const IndexRow& r) {
    std::string o = "{\"id\":" + jstr(r.id) + ",\"character\":" + jstr(r.character) + ",\"file\":" + jstr(r.file) +
                    ",\"startedAt\":" + std::to_string(r.startedAt) + ",\"endedAt\":" + std::to_string(r.endedAt) +
                    ",\"bytes\":" + std::to_string(r.bytes) + ",\"events\":" + std::to_string(r.events) + ",\"fights\":[";
    for (std::size_t i = 0; i < r.fights.size(); ++i) {
        const IndexFight& f = r.fights[i];
        if (i) o += ",";
        o += "{\"n\":" + std::to_string(f.n) + ",\"start\":" + std::to_string(f.start) + ",\"end\":" + std::to_string(f.end) +
             ",\"kind\":" + jstr(f.kind) + ",\"boss\":" + (f.boss.empty() ? std::string("null") : jstr(f.boss)) +
             ",\"kills\":" + std::to_string(f.kills) + ",\"deaths\":" + std::to_string(f.deaths) + ",\"summary\":" + summary_json(f.summary) + "}";
    }
    o += "],\"upload\":null,\"version\":" + std::to_string(r.version) + "}";
    return o;
}

// ---- compaction ----
bool CompactText(const std::string& jsonl, const char* endBy, std::string& outJson, IndexRow& row) {
    outJson.clear(); row = IndexRow{};
    JV header; bool haveHeader = false;
    std::map<int, JV> actors;                                          // i -> the row object
    std::vector<JV> fights;
    std::map<std::string, std::map<long long, std::string>> dict;      // kind -> id -> json
    JV endObj; bool haveEnd = false;
    struct E { long long c; std::size_t at; };
    std::vector<E> order; std::vector<std::string> events;
    std::size_t pos = 0;
    while (pos < jsonl.size()) {
        std::size_t nl = jsonl.find('\n', pos);
        if (nl == std::string::npos) nl = jsonl.size();
        std::string line = jsonl.substr(pos, nl - pos);
        pos = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '{') { if (!haveHeader && parse_json(line, header)) haveHeader = true; continue; }
        if (line.size() < 3 || line[0] != '[') continue;
        if (line[1] == '"') {
            JV v;
            if (!parse_json(line, v) || v.k != JV::Arr || v.a.size() < 2 || v.a[0].k != JV::Str) continue;
            const std::string& kind = v.a[0].s;
            if (kind == "actor" && v.a[1].k == JV::Obj) actors[(int)v.a[1].i("i")] = v.a[1];
            else if (kind == "fight" && v.a[1].k == JV::Obj) fights.push_back(v.a[1]);
            else if (kind == "dict" && v.a.size() >= 4 && v.a[1].k == JV::Str && v.a[2].k == JV::Num) { std::string j; emit_json(v.a[3], j); dict[v.a[1].s][(long long)v.a[2].num] = j; }
            else if (kind == "end" && v.a[1].k == JV::Obj) { endObj = v.a[1]; haveEnd = true; }
            else if (kind == "mech" && v.a.size() >= 7 && v.a[1].k == JV::Num) { order.push_back({ (long long)v.a[1].num, events.size() }); events.push_back(std::move(line)); }
            continue;
        }
        std::vector<long long> f; event_ints(line, f);
        if (f.size() < 2) continue;
        order.push_back({ f[1], events.size() });
        events.push_back(std::move(line));
    }
    if (!haveHeader) return false;
    std::stable_sort(order.begin(), order.end(), [](const E& a, const E& b) { return a.c < b.c; });
    const JV* log = header.get("log"); const JV* clock = header.get("clock");
    row.id = log ? log->str("id") : std::string(); row.character = log ? log->str("character") : std::string();
    row.startedAt = log ? log->i("startedAt") : 0;
    row.endedAt = haveEnd ? endObj.i("endedAt") : row.startedAt;
    row.events = (long long)events.size();
    const long long c0 = clock ? clock->i("c0") : 0, wall0 = clock ? clock->i("wall0") : row.startedAt;
    int phase = clock ? (int)clock->i("phase", -1) : -1;
    if (haveEnd && endObj.i("phase", -1) >= 0) phase = (int)endObj.i("phase", -1);
    auto toMs = [&](long long c) { return wall0 + (c - c0) * 20; };
    // actor last rows and the per-fight summaries
    std::map<int, long long> lastC; std::map<int, long long> lpMaxSeen;
    struct HM { std::string kind; bool other = false, crit = false; };
    std::unordered_map<long long, HM> hm;
    for (const auto& kv : dict["hitmarks"]) { JV v; HM x; if (parse_json(kv.second, v)) { x.kind = v.str("kind"); const JV* o = v.get("other"); x.other = o && o->k == JV::Bool && o->b; o = v.get("crit"); x.crit = o && o->k == JV::Bool && o->b; } hm[kv.first] = x; }
    auto hmInfo = [&](long long id) -> HM {
        auto it = hm.find(id); if (it != hm.end()) return it->second;
        HM x; const auto* info = rtx::hitmarks::Find((int)id);
        x.kind = info ? info->kind : "unknown"; x.other = info && info->other; x.crit = x.kind.find("crit") != std::string::npos;
        return hm[id] = x;
    };
    std::vector<IndexFight> ifs;
    for (const JV& f : fights) {
        IndexFight x; x.n = (int)f.i("n"); x.start = f.i("start"); x.end = f.i("end"); x.kind = f.str("kind"); x.boss = f.str("boss");
        x.kills = (int)f.i("kills"); x.deaths = (int)f.i("deaths");
        x.summary.durMs = (x.end - x.start) * 20; x.summary.kills = x.kills; x.summary.deaths = x.deaths;
        ifs.push_back(std::move(x));
    }
    std::vector<long long> f;
    for (const E& e : order) {
        event_ints(events[e.at], f);
        if (f.size() < 2) continue;                                    // ["mech", ...]
        const long long type = f[0], c = f[1];
        if ((type == 0 || type == 2 || type == 3 || type == 4 || type == 10 || type == 11 || type == 13 || type == 17 || type == 18) && f.size() >= 3) {
            lastC[(int)f[2]] = c;
            if (type == 4 && f.size() >= 5 && f[4] > lpMaxSeen[(int)f[2]]) lpMaxSeen[(int)f[2]] = f[4];
        }
        if (type != 0 || f.size() < 5) continue;
        for (IndexFight& x : ifs) {
            if (c < x.start || c > x.end) continue;
            const HM info = hmInfo(f[3]);
            auto ai = actors.find((int)f[2]);
            const std::string atype = ai != actors.end() ? ai->second.str("type") : std::string();
            const long long value = f[4];
            Summary& s = x.summary;
            if (info.other) continue;
            const bool heal = info.kind == "heal" || info.kind == "uber heal";
            const bool zero = info.kind == "blocked" || info.kind == "absorbed" || info.kind == "hidden";
            if (atype == "self") {
                if (heal) s.healed += value;
                else if (zero) ++s.blocked;
                else if (damage_kind(info.kind)) { s.taken += value; if (value == 0) ++s.blocked; }
            } else if (!heal && damage_kind(info.kind)) {
                s.dealt += value; ++s.hits; if (info.crit) ++s.crits; if (value > s.maxHit) s.maxHit = value;
            }
        }
    }
    for (IndexFight& x : ifs) {
        if (x.summary.durMs > 0) { x.summary.dps = (double)x.summary.dealt * 1000.0 / (double)x.summary.durMs; x.summary.dpm = x.summary.dps * 60.0; }
    }
    row.fights = ifs;
    // the log object
    std::string o = "{\"format\":1,\"log\":{\"id\":" + jstr(row.id) + ",\"character\":" + jstr(row.character) +
                    ",\"launcher\":" + jstr(log ? log->str("launcher") : std::string()) + ",\"client\":" + jstr(log ? log->str("client") : std::string()) +
                    ",\"startedAt\":" + std::to_string(row.startedAt) + ",\"endedAt\":" + std::to_string(row.endedAt) +
                    ",\"endBy\":" + jstr(haveEnd ? endObj.str("endBy") : std::string(endBy ? endBy : "")) +
                    ",\"anonymised\":false,\"companion\":false,\"readFails\":" + std::to_string(haveEnd ? endObj.i("readFails") : 0) +
                    ",\"gaps\":" + std::to_string(haveEnd ? endObj.i("gaps") : 0) + "},\"clock\":{\"c0\":" + std::to_string(c0) +
                    ",\"wall0\":" + std::to_string(wall0) + ",\"phase\":" + std::to_string(phase) + ",\"tick0\":-1},\"actors\":[";
    bool first = true;
    for (const auto& kv : actors) {
        const JV& a = kv.second;
        const int i = kv.first;
        long long lpMax = a.i("lpMax", -1); if (lpMax < 0 && lpMaxSeen.count(i)) lpMax = lpMaxSeen[i];
        o += first ? "" : ","; first = false;
        o += "{\"i\":" + std::to_string(i) + ",\"type\":" + jstr(a.str("type")) + ",\"uid\":" + std::to_string(a.i("uid", -1)) + ",\"id\":" + std::to_string(a.i("id", -1)) +
             ",\"name\":" + jstr(a.str("name")) + ",\"first\":" + std::to_string(a.i("first")) + ",\"last\":" + std::to_string(lastC.count(i) ? lastC[i] : a.i("first")) +
             ",\"lpMax\":" + std::to_string(lpMax) + ",\"vis\":" + std::to_string(a.i("vis", -1)) + "}";
    }
    o += "],\"dict\":{";
    static const char* kinds[] = { "abilities", "buffs", "hitmarks", "seqs", "encounters", "trackers", "mechs" };
    for (int k = 0; k < 7; ++k) {
        if (k) o += ",";
        o += "\""; o += kinds[k]; o += "\":{";
        bool f1 = true;
        for (const auto& kv : dict[kinds[k]]) { o += f1 ? "" : ","; f1 = false; o += "\"" + std::to_string(kv.first) + "\":" + kv.second; }
        o += "}";
    }
    o += "},\"fights\":[";
    for (std::size_t i = 0; i < ifs.size(); ++i) {
        const IndexFight& x = ifs[i]; const JV& f0 = fights[i];
        if (i) o += ",";
        std::string targets; if (const JV* t = f0.get("targets")) emit_json(*t, targets); else targets = "[]";
        o += "{\"n\":" + std::to_string(x.n) + ",\"start\":" + std::to_string(x.start) + ",\"end\":" + std::to_string(x.end) + ",\"startMs\":" + std::to_string(toMs(x.start)) +
             ",\"kind\":" + jstr(x.kind) + ",\"boss\":" + (x.boss.empty() ? std::string("null") : jstr(x.boss)) + ",\"targets\":" + targets +
             ",\"kills\":" + std::to_string(x.kills) + ",\"deaths\":" + std::to_string(x.deaths) + ",\"startBy\":" + jstr(f0.str("startBy")) +
             ",\"endBy\":" + jstr(f0.str("endBy")) + ",\"summary\":" + summary_json(x.summary) + "}";
    }
    o += "],\"schema\":{\"0\":[\"hit\",\"actor\",\"hm\",\"value\",\"hm2\",\"value2\",\"delay\"],\"1\":[\"cast\",\"struct\",\"ready\",\"src\"],"
         "\"2\":[\"anim\",\"actor\",\"seq\"],\"3\":[\"target\",\"actor\",\"target\"],\"4\":[\"lp\",\"actor\",\"lp\",\"lpMax\"],\"5\":[\"adren\",\"value\"],"
         "\"6\":[\"prayer\",\"points\",\"level\"],\"7\":[\"buff\",\"struct\",\"on\",\"start\",\"end\",\"stacks\"],\"8\":[\"channel\",\"side\",\"ticks\",\"name\"],"
         "\"9\":[\"tracker\",\"group\",\"row\",\"col\",\"value\"],\"10\":[\"death\",\"actor\",\"how\"],\"11\":[\"actor\",\"actor\",\"present\"],"
         "\"12\":[\"encounter\",\"struct\"],\"13\":[\"gfx\",\"actor\",\"gfx\"],\"14\":[\"proj\",\"from\",\"to\",\"gfx\"],\"15\":[\"xp\",\"skill\",\"xp\"],"
         "\"16\":[\"mark\",\"kind\",\"text\"],\"17\":[\"bar\",\"actor\",\"slot\",\"fill\"],\"18\":[\"stat\",\"actor\",\"idx\",\"cur\",\"base\"],"
         "\"19\":[\"sound\",\"id\",\"area\"],\"mech\":[\"mech\",\"boss\",\"key\",\"kind\",\"id\",\"actor\"]},\"events\":[";
    for (std::size_t i = 0; i < order.size(); ++i) { if (i) o += ",\n"; o += events[order[i].at]; }
    o += "]}\n";
    outJson = std::move(o);
    return true;
}

bool Compact(const std::filesystem::path& jsonl, const char* endBy, IndexRow* rowOut) {
    std::string text;
    if (!read_file(jsonl, text)) return false;
    std::string json; IndexRow row;
    std::error_code ec;
    if (!CompactText(text, endBy, json, row) || row.id.empty()) { std::filesystem::remove(jsonl, ec); return false; }
    if (row.fights.empty()) { std::filesystem::remove(jsonl, ec); return false; }
    std::string gz;
    if (!Gzip(json, gz)) return false;
    char stamp[40] = "log";
    { const std::time_t t = (std::time_t)(row.startedAt / 1000); std::tm tm{}; if (localtime_s(&tm, &t) == 0) std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm); }
    const std::string name = std::string(stamp) + "_" + safe_name(row.id) + ".json.gz";
    const std::filesystem::path out = jsonl.parent_path() / to_wide(name);
    if (!write_file(out, gz)) return false;
    row.bytes = (long long)gz.size();
    row.file = to_utf8(jsonl.parent_path().filename().wstring()) + "/" + name;
    { std::lock_guard<std::mutex> lk(g_mu); index_put_locked(row); }
    std::filesystem::remove(jsonl, ec);
    if (rowOut) *rowOut = row;
    return true;
}

void Recover() {
    std::vector<std::filesystem::path> found;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        std::error_code ec;
        for (const auto& d : std::filesystem::directory_iterator(root_locked(false), ec)) {
            if (!d.is_directory(ec)) continue;
            for (const auto& f : std::filesystem::directory_iterator(d.path(), ec)) {
                const std::wstring n = f.path().filename().wstring();
                if (f.is_regular_file(ec) && n.rfind(L"current_", 0) == 0 && n.size() > 6 && n.compare(n.size() - 6, 6, L".jsonl") == 0) found.push_back(f.path());
            }
        }
    }
    for (const auto& p : found) Compact(p, "recovered");
}

void Retention() {
    struct F { std::filesystem::path p; long long mtime; std::uintmax_t size; };
    std::vector<F> files;
    std::lock_guard<std::mutex> lk(g_mu);
    std::error_code ec;
    for (const auto& d : std::filesystem::directory_iterator(root_locked(false), ec)) {
        if (!d.is_directory(ec) || d.path().filename() == L"export") continue;   // exported copies are the user's to keep
        for (const auto& f : std::filesystem::directory_iterator(d.path(), ec)) {
            const std::wstring n = f.path().filename().wstring();
            if (!f.is_regular_file(ec) || n.size() < 8 || n.compare(n.size() - 8, 8, L".json.gz") != 0) continue;
            const auto t = std::filesystem::last_write_time(f.path(), ec);
            files.push_back({ f.path(), (long long)t.time_since_epoch().count(), f.file_size(ec) });
        }
    }
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.mtime < b.mtime; });
    std::uintmax_t total = 0; for (const auto& f : files) total += f.size;
    const auto now = std::filesystem::file_time_type::clock::now();
    const long long dayTicks = (long long)std::chrono::duration_cast<std::filesystem::file_time_type::duration>(std::chrono::hours(24)).count();
    bool changed = false;
    for (const auto& f : files) {
        const bool old = (long long)now.time_since_epoch().count() - f.mtime > 90 * dayTicks;
        if (!old && total <= 2ull * 1024 * 1024 * 1024) break;
        if (std::filesystem::remove(f.p, ec)) { total -= f.size; changed = true; }
    }
    auto rows = index_read_locked();
    const std::size_t before = rows.size();
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const IndexRow& r) { return !std::filesystem::exists(root_locked() / to_wide(r.file), ec); }), rows.end());
    if (changed || rows.size() != before) index_write_locked(rows);
}

std::string ListJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    auto rows = index_read_locked();
    std::stable_sort(rows.begin(), rows.end(), [](const IndexRow& a, const IndexRow& b) { return a.startedAt > b.startedAt; });
    std::string o = "[";
    for (std::size_t i = 0; i < rows.size(); ++i) { if (i) o += ","; o += RowJson(rows[i]); }
    return o + "]";
}

static bool find_row(const std::string& logId, IndexRow& out, std::filesystem::path& file) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (const auto& r : index_read_locked()) if (r.id == logId) { out = r; file = root_locked() / to_wide(r.file); return true; }
    return false;
}

std::string LoadJson(const std::string& logId) {
    IndexRow r; std::filesystem::path file;
    if (!find_row(logId, r, file)) return {};
    std::string gz, json;
    if (!read_file(file, gz) || !Gunzip(gz, json, 8u * 1024 * 1024)) return {};
    return json;
}

bool Delete(const std::string& logId) {
    IndexRow r; std::filesystem::path file;
    if (!find_row(logId, r, file)) return false;
    std::error_code ec; std::filesystem::remove(file, ec);
    std::lock_guard<std::mutex> lk(g_mu);
    auto rows = index_read_locked();
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const IndexRow& x) { return x.id == logId; }), rows.end());
    index_write_locked(rows);
    return true;
}

std::string Export(const std::string& logId, bool pick, void* ownerHwnd) {
    IndexRow r; std::filesystem::path file;
    if (!find_row(logId, r, file)) return {};
    const std::wstring name = file.filename().wstring();
    std::filesystem::path dest;
    if (pick) {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        IFileSaveDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
            static const COMDLG_FILTERSPEC kTypes[] = { { L"Combat log (*.json.gz)", L"*.json.gz" } };
            dlg->SetFileTypes(1, kTypes);
            dlg->SetTitle(L"Export combat log");
            dlg->SetFileName(name.c_str());
            dlg->SetDefaultExtension(L"json.gz");
            DWORD opts = 0; dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_DONTADDTORECENT);
            if (SUCCEEDED(dlg->Show(static_cast<HWND>(ownerHwnd)))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item))) {
                    PWSTR psz = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) { dest = psz; CoTaskMemFree(psz); }
                    item->Release();
                }
            }
            dlg->Release();
        }
        if (init == S_OK || init == S_FALSE) CoUninitialize();
        if (dest.empty()) return {};
    } else {
        std::filesystem::path dir;
        { std::lock_guard<std::mutex> lk(g_mu); dir = root_locked() / L"export"; }
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        dest = dir / name;
    }
    std::error_code ec;
    std::filesystem::copy_file(file, dest, std::filesystem::copy_options::overwrite_existing, ec);
    return ec ? std::string() : to_utf8(dest.wstring());
}

void OpenFolder() {
    const std::filesystem::path dir = Root();
    ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool Gzip(const std::string& in, std::string& out) {
    out.clear();
    static const char hdr[10] = { '\x1f', '\x8b', 8, 0, 0, 0, 0, 0, 0, '\x0b' };
    out.append(hdr, 10);
    deflate(in, out);
    const std::uint32_t crc = crc32_of(in), len = (std::uint32_t)in.size();
    for (int i = 0; i < 4; ++i) out.push_back((char)((crc >> (8 * i)) & 0xFF));
    for (int i = 0; i < 4; ++i) out.push_back((char)((len >> (8 * i)) & 0xFF));
    return true;
}

bool Gunzip(const std::string& in, std::string& out, std::size_t cap) {
    out.clear();
    z_stream s{};
    if (inflateInit2(&s, 15 + 16) != Z_OK) return false;
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data())); s.avail_in = (uInt)in.size();
    std::vector<char> buf(256 * 1024);
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        s.next_out = reinterpret_cast<Bytef*>(buf.data()); s.avail_out = (uInt)buf.size();
        rc = inflate(&s, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&s); out.clear(); return false; }
        out.append(buf.data(), buf.size() - s.avail_out);
        if (out.size() > cap) { inflateEnd(&s); out.clear(); return false; }
    }
    inflateEnd(&s);
    return true;
}

}  // namespace rtx::launcher::fights
