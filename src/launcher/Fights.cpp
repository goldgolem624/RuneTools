#include "Fights.h"
#include "CombatPrep.h"
#include "Http.h"
#include "Link.h"
#include "IconCache.h"
#include "../cache/CacheReader.h"
#include "../reader/Hitmarks.h"
#include "../shared/Log.h"
#include "../cache/vendor/zlib/zlib.h"

#include <Windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <set>
#include <thread>
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

namespace rtx::launcher { std::string running_version(); }   // Update.cpp

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
// A flag file's second number ("1 <ms>"), else the file's change time in ms; 0 when off or absent.
long long flag_since(const wchar_t* name) {
    std::lock_guard<std::mutex> lk(g_mu);
    const std::filesystem::path p = root_locked(false) / name;
    std::ifstream f(p);
    long long on = 0, since = 0;
    if (!f || !(f >> on) || on == 0) return 0;
    if (f >> since && since > 0) return since;
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return 0;
    const unsigned long long t = ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    return t > 116444736000000000ull ? (long long)((t - 116444736000000000ull) / 10000) : 0;
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
// The whole file or the old one, never a cut one: a temporary file flushed to disk, then moved over it.
bool write_file_atomic(const std::filesystem::path& p, const std::string& data) {
    std::filesystem::path tmp = p; tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == (DWORD)data.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
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

// The site's answer to an accepted upload, before anything is stored: a 12 character id and exactly
// https://runetools.io/combat/<that id>.
bool id_chars(const std::string& s, std::size_t n) {
    if (s.size() != n) return false;
    for (char c : s) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}
bool upload_ok(const UploadInfo& u) {
    return id_chars(u.id, 12) && u.url == "https://runetools.io/combat/" + u.id &&
           (u.visibility == "private" || u.visibility == "unlisted" || u.visibility == "public");
}

// ---- index.json ----
std::filesystem::path index_path_locked(bool create = false) { return root_locked(create) / L"index.json"; }

bool row_from_json(const JV& v, IndexRow& r) {
    if (v.k != JV::Obj) return false;
    r.id = v.str("id"); r.character = v.str("character"); r.file = v.str("file");
    r.startedAt = v.i("startedAt"); r.endedAt = v.i("endedAt"); r.bytes = v.i("bytes"); r.events = v.i("events"); r.version = (int)v.i("version", 1);
    if (const std::string ua = v.str("uploadAs"); ValidVisibility(ua)) r.uploadAs = ua;
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
    if (const JV* u = v.get("upload"); u && u->k == JV::Obj) {
        UploadInfo x; x.id = u->str("id"); x.url = u->str("url"); x.visibility = u->str("visibility"); x.at = u->i("at");
        if (upload_ok(x)) r.upload = x;
    }
    if (const JV* u = v.get("uploadState"); u && u->k == JV::Obj) {
        UploadState x; x.state = u->str("state"); x.error = u->str("error"); x.code = u->str("code");
        x.tries = (int)u->i("tries"); x.next = u->i("next");
        const std::string mode = u->str("mode"); x.manual = mode == "manual"; x.final = mode == "final";
        static const char* states[] = { "queued", "sending", "failed", "unlinked", "skipped" };
        for (const char* st : states) if (x.state == st) { r.state = x; break; }
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
    write_file_atomic(index_path_locked(true), o);
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
void SetUploadAuto(bool on) {
    if (!on) { flag_write(L"combat_upload.txt", false); return; }
    if (flag_read(L"combat_upload.txt")) {                        // already on: keep its start, written out
        const long long since = flag_since(L"combat_upload.txt");
        std::lock_guard<std::mutex> lk(g_mu);
        std::ofstream f(root_locked() / L"combat_upload.txt", std::ios::trunc);
        if (f) f << "1 " << (since > 0 ? since : (long long)std::time(nullptr) * 1000);
        return;
    }
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lk(g_mu);
    std::ofstream f(root_locked() / L"combat_upload.txt", std::ios::trunc);
    if (f) f << "1 " << now;
}
long long UploadAutoSince() { return flag_since(L"combat_upload.txt"); }
bool UploadLive() { return flag_read(L"combat_live.txt"); }
void SetUploadLive(bool on) { flag_write(L"combat_live.txt", on); }
bool KeepNames() { return flag_read(L"combat_names.txt"); }
void SetKeepNames(bool on) { flag_write(L"combat_names.txt", on); }
bool ValidVisibility(const std::string& v) { return v == "private" || v == "unlisted" || v == "public"; }
std::string UploadVisibility() {
    std::string v;
    { std::lock_guard<std::mutex> lk(g_mu); std::ifstream f(root_locked(false) / L"combat_visibility.txt"); if (f) f >> v; }
    return ValidVisibility(v) ? v : std::string("private");
}
void SetUploadVisibility(const std::string& v) {
    if (!ValidVisibility(v)) return;
    std::lock_guard<std::mutex> lk(g_mu);
    std::ofstream f(root_locked() / L"combat_visibility.txt", std::ios::trunc);
    if (f) f << v;
}

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
    o += "],\"upload\":";
    if (r.upload.id.empty()) o += "null";
    else o += "{\"id\":" + jstr(r.upload.id) + ",\"url\":" + jstr(r.upload.url) + ",\"at\":" + std::to_string(r.upload.at) +
              ",\"visibility\":" + jstr(r.upload.visibility) + "}";
    o += ",\"uploadState\":";
    if (r.state.state.empty()) o += "null";
    else o += "{\"state\":" + jstr(r.state.state) + ",\"tries\":" + std::to_string(r.state.tries) + ",\"next\":" + std::to_string(r.state.next) +
              ",\"error\":" + jstr(r.state.error) + ",\"code\":" + jstr(r.state.code) +
              ",\"mode\":" + jstr(r.state.final ? "final" : r.state.manual ? "manual" : "auto") + "}";
    if (!r.uploadAs.empty()) o += ",\"uploadAs\":" + jstr(r.uploadAs);
    o += ",\"version\":" + std::to_string(r.version) + "}";
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
    if (const std::string ua = log ? log->str("uploadAs") : std::string(); ValidVisibility(ua)) row.uploadAs = ua;
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
                    (haveEnd && endObj.i("reads") > 0 ? ",\"reads\":" + std::to_string(endObj.i("reads")) : std::string()) +
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
    static const char* kinds[] = { "abilities", "buffs", "hitmarks", "seqs", "encounters", "trackers", "mechs", "items", "perks" };
    for (int k = 0; k < 9; ++k) {
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
         "\"19\":[\"sound\",\"id\",\"area\"],\"20\":[\"item\",\"container\",\"slot\",\"item\",\"count\"],"
         "\"21\":[\"perks\",\"slot\",\"p1\",\"r1\",\"p2\",\"r2\",\"p3\",\"r3\",\"p4\",\"r4\"],\"22\":[\"special\",\"container\",\"slot\",\"item\"],"
         "\"mech\":[\"mech\",\"boss\",\"key\",\"kind\",\"id\",\"actor\"]},\"events\":[";
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
    for (const auto& p : found) { IndexRow row; if (Compact(p, "recovered", &row)) AfterCompact(row); }
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
    // a log waiting for its upload stays
    std::set<std::filesystem::path> busy;
    for (const auto& r : index_read_locked()) if (r.state.state == "queued" || r.state.state == "sending") busy.insert(root_locked(false) / to_wide(r.file));
    files.erase(std::remove_if(files.begin(), files.end(), [&](const F& f) { return busy.count(f.p) > 0; }), files.end());
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

bool Gunzip(const std::string& in, std::string& out, std::size_t cap, bool* capped) {
    out.clear();
    if (capped) *capped = false;
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
        if (out.size() > cap) { inflateEnd(&s); out.clear(); if (capped) *capped = true; return false; }
    }
    inflateEnd(&s);
    return true;
}

// ---- uploads: one thread sends queued logs and the chunks of open logs, one request at a time ----
namespace {

constexpr wchar_t kUploadHost[] = L"runetools.io";    // the only host an upload or the account token goes to
constexpr wchar_t kLogsPath[] = L"/api/client/combat/logs";
constexpr wchar_t kLivePath[] = L"/api/client/combat/live";
constexpr std::size_t kMaxBody = 2u * 1024 * 1024;     // gzip body
constexpr std::size_t kMaxRaw = 16u * 1024 * 1024;     // inflated log
constexpr std::size_t kChunkEvents = 20000;
constexpr std::size_t kLivePending = 50000;            // event lines held for an open log before live stops

long long wall_now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
long long tick_now() { return (long long)GetTickCount64(); }

bool code_ok(const std::string& c) {
    if (c.empty() || c.size() > 24) return false;
    for (char ch : c) if (!((ch >= 'a' && ch <= 'z') || ch == '_')) return false;
    return true;
}
// The launcher's own words for a code; the site's error text is never shown.
std::string error_text(const std::string& code) {
    if (code == "too_large") return "Log is too large to upload.";
    if (code == "unlinked") return "Link this PC to a RuneTools account first.";
    if (code == "deleted") return "Deleted on runetools.io.";
    if (code == "storage_full") return "Your runetools.io storage is full.";
    if (code == "daily_limit") return "Daily upload limit reached.";
    if (code == "disabled") return "Uploads are paused on runetools.io.";
    if (code == "live_too_long") return "Log is too long to upload live; it uploads when it ends.";
    if (code == "gap") return "Live upload paused; the log uploads when it ends.";
    if (code == "final") return "This log is already finished on runetools.io.";
    if (code == "seq" || code == "head") return "Live upload lost its place; the log uploads when it ends.";
    return "Upload failed.";
}

struct Answer {
    bool ok = false; int status = 0;
    std::string code;                  // the site's code, when well formed
    long long retryAfter = -1;         // seconds
    std::string body;
    JV json; bool haveJson = false;
};
Answer to_answer(const http::Response& r) {
    Answer a; a.ok = r.ok; a.status = r.status; a.body = r.body;
    if (!r.body.empty() && parse_json(r.body, a.json) && a.json.k == JV::Obj) {
        a.haveJson = true;
        const std::string c = a.json.str("code");
        if (code_ok(c)) a.code = c;
    }
    const std::string ra = r.header("Retry-After");
    if (!ra.empty() && ra.size() <= 7 && std::all_of(ra.begin(), ra.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
        a.retryAfter = std::min<long long>(std::atoll(ra.c_str()), 2 * 86400);
    return a;
}
bool site_answer(const Answer& a, UploadInfo& u) {
    if (!a.haveJson) return false;
    UploadInfo x; x.id = a.json.str("id"); x.url = a.json.str("url"); x.visibility = a.json.str("visibility"); x.at = wall_now();
    if (!upload_ok(x)) return false;
    u = x; return true;
}

// What an answer to a finished log's upload means for its row.
struct Outcome { std::string state, code; long long delayMs = 0; bool retry = false, verify = false, sent = false; };
Outcome classify(const Answer& a, int tries, UploadInfo& info) {
    Outcome o;
    if (a.ok && (a.status == 200 || a.status == 201)) {
        if (site_answer(a, info)) { o.sent = true; return o; }
        o.state = "failed"; o.code = "bad_answer"; return o;
    }
    if (a.ok && a.status == 401) { o.state = "unlinked"; o.code = "unlinked"; o.verify = true; return o; }
    if (a.ok && (a.status == 400 || a.status == 413 || a.status == 415 || a.status == 409)) { o.state = "failed"; o.code = a.code.empty() ? "rejected" : a.code; return o; }
    if (a.ok && a.status == 410) { o.state = "skipped"; o.code = "deleted"; return o; }
    if (a.ok && a.status == 429) { o.state = "queued"; o.code = a.code; o.delayMs = (a.retryAfter >= 0 ? a.retryAfter : 60) * 1000; return o; }
    // a busy site that says when to come back: wait, without using up a try
    if (a.ok && a.status == 503 && a.retryAfter >= 0) { o.state = "queued"; o.code = a.code.empty() ? "busy" : a.code; o.delayMs = std::max<long long>(a.retryAfter, 5) * 1000; return o; }
    if (!a.ok || a.status >= 500) {
        o.retry = true; o.code = a.ok ? (a.code.empty() ? "unavailable" : a.code) : "network";
        if (tries + 1 >= 4) { o.state = "failed"; return o; }
        static const long long backoff[3] = { 60, 300, 1800 };
        o.state = "queued"; o.delayMs = (a.retryAfter >= 0 ? a.retryAfter : backoff[tries < 0 ? 0 : tries > 2 ? 2 : tries]) * 1000;
        return o;
    }
    o.state = "failed"; o.code = a.code.empty() ? "bad_answer" : a.code;
    return o;
}

// visibility: what a new row on the site starts as; "" leaves it to the account's default
std::vector<http::Header> upload_headers(const std::string& auth, const std::string& logId, const char* mode, const std::string& visibility = std::string()) {
    const std::string ver = rtx::launcher::running_version();
    std::vector<http::Header> h = { { "Authorization", auth }, { "Content-Type", "application/x-rtx-combatlog+gzip" }, { "X-RTX-Log-Id", logId },
                                    { "X-RTX-Version", ver }, { "User-Agent", "RuneToolsX/" + ver }, { "X-RTX-Upload", mode } };
    if (ValidVisibility(visibility)) h.push_back({ "X-RTX-Visibility", visibility });
    return h;
}

// A stored .json.gz as the gzip upload body.
bool prepare_body(const std::string& gz, bool keep, std::string& body, combatprep::Stats& st, std::string& err) {
    std::string json, out;
    bool capped = false;
    if (!Gunzip(gz, json, kMaxRaw, &capped)) { err = capped ? "too_large" : "unreadable"; return false; }
    combatprep::Options opt; opt.keepNames = keep;
    // a shared report shows the log's own pictures: ability and buff sprites at 24 px, item icons as the game draws them
    opt.icon = [](char kind, int id) { return kind == 's' ? rtx::cache::SpriteDataUrlScaled(id, 24) : rtx::launcher::icons::ItemIconDataUrl(id); };
    if (!combatprep::Prepare(json, opt, out, st, err)) return false;
    return Gzip(out, body);
}

template <class F> bool update_row(const std::string& id, F f) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto rows = index_read_locked();
    for (auto& r : rows) if (r.id == id) { f(r); index_write_locked(rows); return true; }
    return false;
}

std::mutex g_up_mu;
std::condition_variable g_up_cv;
bool g_up_started = false, g_up_kick = false;
std::atomic<bool> g_headless{ false };
long long g_lastFinalTick = -100000;
std::size_t g_unlinkedAuth = 0;                        // the token a 401 came back for (hashed); upload thread only

void kick() { { std::lock_guard<std::mutex> lk(g_up_mu); g_up_kick = true; } g_up_cv.notify_one(); }

// ---- live: an open log rebuilt from its lines, cut into chunks ----
struct Chunk { int seq = 0; long long first = 0, count = 0; std::string gz; };

// The actor indexes an event refers to (the highest; -1 none).
long long max_ref(const std::string& line, combatprep::detail::EventView& v) {
    if (!combatprep::detail::view_event(line.data(), line.size(), v)) return -1;
    auto at = [&](std::size_t k) -> long long {
        if (k >= v.items.size()) return -1;
        return std::strtoll(std::string(line, v.items[k].first, v.items[k].second - v.items[k].first).c_str(), nullptr, 10);
    };
    switch (v.type) {
    case 0: case 2: case 4: case 10: case 11: case 13: case 17: case 18: return at(2);
    case 3: case 14: return std::max(at(2), at(3));
    case 1000: return at(6);
    default: return -1;
    }
}

const char* kSchemaJson =
    "{\"0\":[\"hit\",\"actor\",\"hm\",\"value\",\"hm2\",\"value2\",\"delay\"],\"1\":[\"cast\",\"struct\",\"ready\",\"src\"],"
    "\"2\":[\"anim\",\"actor\",\"seq\"],\"3\":[\"target\",\"actor\",\"target\"],\"4\":[\"lp\",\"actor\",\"lp\",\"lpMax\"],\"5\":[\"adren\",\"value\"],"
    "\"6\":[\"prayer\",\"points\",\"level\"],\"7\":[\"buff\",\"struct\",\"on\",\"start\",\"end\",\"stacks\"],\"8\":[\"channel\",\"side\",\"ticks\",\"name\"],"
    "\"9\":[\"tracker\",\"group\",\"row\",\"col\",\"value\"],\"10\":[\"death\",\"actor\",\"how\"],\"11\":[\"actor\",\"actor\",\"present\"],"
    "\"12\":[\"encounter\",\"struct\"],\"13\":[\"gfx\",\"actor\",\"gfx\"],\"14\":[\"proj\",\"from\",\"to\",\"gfx\"],\"15\":[\"xp\",\"skill\",\"xp\"],"
    "\"16\":[\"mark\",\"kind\",\"text\"],\"17\":[\"bar\",\"actor\",\"slot\",\"fill\"],\"18\":[\"stat\",\"actor\",\"idx\",\"cur\",\"base\"],"
    "\"19\":[\"sound\",\"id\",\"area\"],\"20\":[\"item\",\"container\",\"slot\",\"item\",\"count\"],"
    "\"21\":[\"perks\",\"slot\",\"p1\",\"r1\",\"p2\",\"r2\",\"p3\",\"r3\",\"p4\",\"r4\"],\"22\":[\"special\",\"container\",\"slot\",\"item\"],"
    "\"mech\":[\"mech\",\"boss\",\"key\",\"kind\",\"id\",\"actor\"]}";

struct LiveLog {
    std::string visibility;                             // the header's uploadAs; none = the account's default
    bool haveHeader = false; JV log, clock;
    long long c0 = 0, wall0 = 0;
    std::map<int, std::string> actors;                                 // i -> object
    // actor rows are written when first used, so the recorder's indexes can skip numbers: they are renumbered in
    // the order the rows arrive, which keeps the head's actor list dense and only ever appended to
    std::unordered_map<long long, long long> remap;                    // recorder index -> sent index
    std::map<std::string, std::map<long long, std::string>> dict;      // kind -> id -> value
    struct Fight { std::string json; std::vector<long long> targets; };
    std::vector<Fight> closed;
    bool open = false; long long openStart = 0; std::string openBy;
    long long curEnc = -2, openEnc = -2;                               // the encounter now and at the open fight's start
    bool ended = false; JV end;
    std::deque<std::string> pending;                                   // event lines not sent yet
    bool headDirty = false;
    // chunks
    int seq = 0; long long first = 0;                                  // the next chunk's seq and first event
    bool haveInflight = false, haveAccepted = false, resendPrev = false;
    Chunk inflight, accepted;
    int acceptedCount = 0, netFails = 0;
    long long nextTry = 0, lastSend = -1000000, fightEndAt = 0;
    std::string siteId, siteUrl;
    bool stopped = false; std::string stopCode;

    void feed(const std::vector<std::string>& lines) {
        combatprep::detail::EventView v;
        for (const std::string& line : lines) {
            if (line.size() < 2) continue;
            if (line[0] == '{') {
                JV h;
                if (haveHeader || !parse_json(line, h)) continue;
                const JV* l = h.get("log"); const JV* c = h.get("clock");
                if (!l || l->k != JV::Obj || !c || c->k != JV::Obj) continue;
                log = *l; clock = *c; c0 = c->i("c0"); wall0 = c->i("wall0", log.i("startedAt"));
                if (ValidVisibility(log.str("uploadAs"))) visibility = log.str("uploadAs");
                haveHeader = true; headDirty = true;
                continue;
            }
            if (line[0] != '[') continue;
            if (line[1] == '"' && line.compare(0, 7, "[\"mech\"") != 0) {
                JV a;
                if (!parse_json(line, a) || a.k != JV::Arr || a.a.size() < 2 || a.a[0].k != JV::Str) continue;
                const std::string& kind = a.a[0].s;
                if (kind == "actor" && a.a[1].k == JV::Obj) {
                    JV row = a.a[1];
                    const long long raw = row.i("i", -1);
                    auto it = remap.find(raw);
                    const long long k = it != remap.end() ? it->second : (long long)remap.size();
                    if (it == remap.end()) remap[raw] = k;
                    for (auto& kv : row.o) if (kv.first == "i") { kv.second = JV{}; kv.second.k = JV::Num; kv.second.num = (double)k; }
                    std::string j; emit_json(row, j); actors[(int)k] = j; headDirty = true;
                }
                else if (kind == "dict" && a.a.size() >= 4 && a.a[1].k == JV::Str && a.a[2].k == JV::Num) { std::string j; emit_json(a.a[3], j); dict[a.a[1].s][(long long)a.a[2].num] = j; headDirty = true; }
                else if (kind == "fight" && a.a[1].k == JV::Obj) { add_fight(a.a[1]); open = false; fightEndAt = tick_now(); headDirty = true; }
                else if (kind == "end" && a.a[1].k == JV::Obj) { end = a.a[1]; ended = true; headDirty = true; }
                continue;
            }
            if (combatprep::detail::view_event(line.data(), line.size(), v) && v.type == 16 && v.items.size() == 4 &&
                line.compare(v.items[2].first, v.items[2].second - v.items[2].first, "0") == 0) {
                std::string by;
                combatprep::detail::decode_string(line.data() + v.items[3].first, v.items[3].second - v.items[3].first, by);
                open = true; openStart = std::strtoll(std::string(line, v.items[1].first, v.items[1].second - v.items[1].first).c_str(), nullptr, 10); openBy = by;
                openEnc = curEnc;
                headDirty = true;
            } else if (v.type == 12 && v.items.size() >= 3) {
                // the encounter row can follow the start mark within the same tick
                const long long ec = std::strtoll(std::string(line, v.items[1].first, v.items[1].second - v.items[1].first).c_str(), nullptr, 10);
                curEnc = std::strtoll(std::string(line, v.items[2].first, v.items[2].second - v.items[2].first).c_str(), nullptr, 10);
                if (open && ec >= openStart && ec - openStart <= 30 && openEnc != curEnc) { openEnc = curEnc; headDirty = true; }
            }
            std::string moved;
            if (!combatprep::detail::remap_actors(line.data(), line.size(), remap, moved, v)) continue;
            pending.push_back(std::move(moved));
        }
    }
    void add_fight(const JV& f) {
        Fight x;
        std::string targets = "[";
        if (const JV* t = f.get("targets"); t && t->k == JV::Arr)
            for (std::size_t i = 0; i < t->a.size(); ++i) {
                auto it = remap.find((long long)t->a[i].num);
                if (it == remap.end()) continue;
                if (!x.targets.empty()) targets += ",";
                targets += std::to_string(it->second); x.targets.push_back(it->second);
            }
        targets += "]";
        const long long start = f.i("start");
        x.json = "{\"n\":" + std::to_string(f.i("n")) + ",\"start\":" + std::to_string(start) + ",\"end\":" + std::to_string(f.i("end")) +
                 ",\"startMs\":" + std::to_string(wall0 + (start - c0) * 20) + ",\"kind\":" + jstr(f.str("kind")) +
                 ",\"boss\":" + (f.str("boss").empty() ? std::string("null") : jstr(f.str("boss"))) + ",\"targets\":" + targets +
                 ",\"kills\":" + std::to_string(f.i("kills")) + ",\"deaths\":" + std::to_string(f.i("deaths")) +
                 ",\"startBy\":" + jstr(f.str("startBy")) + ",\"endBy\":" + jstr(f.str("endBy")) + "}";
        closed.push_back(std::move(x));
    }
    int dense() const { int k = 0; while (actors.count(k)) ++k; return k; }
    bool idle() const { return pending.empty() && !headDirty; }

    // The log object so far (actors up to the first missing index; no events).
    std::string head_text(int d) const {
        std::string o = "{\"format\":1,\"log\":{\"id\":" + jstr(log.str("id")) + ",\"character\":" + jstr(log.str("character")) +
                        ",\"launcher\":" + jstr(log.str("launcher")) + ",\"client\":" + jstr(log.str("client")) +
                        ",\"startedAt\":" + std::to_string(log.i("startedAt"));
        if (ended) o += ",\"endedAt\":" + std::to_string(end.i("endedAt")) + ",\"endBy\":" + jstr(end.str("endBy"));
        o += ",\"anonymised\":false,\"companion\":false";
        if (ended) o += ",\"readFails\":" + std::to_string(end.i("readFails")) + (end.i("reads") > 0 ? ",\"reads\":" + std::to_string(end.i("reads")) : std::string()) +
                        ",\"gaps\":" + std::to_string(end.i("gaps"));
        o += "},\"clock\":";
        emit_json(clock, o);
        o += ",\"actors\":[";
        for (int k = 0; k < d; ++k) { if (k) o += ","; o += actors.at(k); }
        o += "],\"dict\":{";
        static const char* kinds[] = { "abilities", "buffs", "hitmarks", "seqs", "encounters", "trackers", "mechs" };
        for (int k = 0; k < 7; ++k) {
            if (k) o += ",";
            o += "\""; o += kinds[k]; o += "\":{";
            bool f1 = true;
            if (auto it = dict.find(kinds[k]); it != dict.end())
                for (const auto& kv : it->second) { o += f1 ? "" : ","; f1 = false; o += "\"" + std::to_string(kv.first) + "\":" + kv.second; }
            o += "}";
        }
        o += "},\"fights\":[";
        // a closed fight shows once every target it names is in the actor list; later ones wait behind it
        std::size_t n = 0;
        for (; n < closed.size(); ++n) {
            bool ok = true;
            for (long long t : closed[n].targets) if (t < 0 || t >= d) ok = false;
            if (!ok) break;
            if (n) o += ",";
            o += closed[n].json;
        }
        if (n == closed.size() && open) {
            if (n) o += ",";
            std::string kind = "\"kills\"", boss = "null";
            if (openEnc >= 0)
                if (auto e = dict.find("encounters"); e != dict.end())
                    if (auto nm = e->second.find(openEnc); nm != e->second.end() && nm->second.size() > 2 && nm->second[0] == '"') { kind = "\"encounter\""; boss = nm->second; }
            o += "{\"n\":" + std::to_string(n) + ",\"start\":" + std::to_string(openStart) + ",\"end\":-1,\"startMs\":" + std::to_string(wall0 + (openStart - c0) * 20) +
                 ",\"kind\":" + kind + ",\"boss\":" + boss + ",\"targets\":[],\"kills\":0,\"deaths\":0,\"startBy\":" + jstr(openBy) + ",\"endBy\":\"\"}";
        }
        o += "],\"schema\":"; o += kSchemaJson; o += ",\"events\":[]}";
        return o;
    }

    // The next chunk from what is pending; false when there is nothing new to send.
    bool build(bool keepNames, Chunk& out) {
        if (!haveHeader) return false;
        const int d = dense();
        std::vector<std::string> take;
        combatprep::detail::EventView v;
        for (const std::string& l : pending) {
            if (take.size() >= kChunkEvents) break;
            if (max_ref(l, v) >= d) break;                              // its actor row is not out yet
            take.push_back(l);
        }
        if (take.empty() && !headDirty) return false;
        const std::string raw = head_text(d);
        combatprep::Options opt; opt.keepNames = keepNames;
        combatprep::Stats st; std::string prepared, err;
        if (!combatprep::Prepare(raw, opt, prepared, st, err)) return false;
        combatprep::detail::Node doc; combatprep::detail::Parser ps; ps.p = prepared.data(); ps.e = prepared.data() + prepared.size();
        if (!ps.val(doc) || doc.k != combatprep::detail::Node::Obj) return false;
        std::string head = "{";
        bool f1 = true;
        for (const auto& m : doc.c) {
            if (m.key == "format" || m.key == "events") continue;
            if (!f1) head += ",";
            f1 = false;
            head += m.keyRaw; head += ":"; combatprep::detail::write(m, head);
        }
        head += "}";
        std::vector<std::string> lines;
        combatprep::FilterEventLines(take, combatprep::NamesForFilter(raw), opt, lines, st);
        std::string body = "{\"format\":1,\"live\":1,\"seq\":" + std::to_string(seq) + ",\"first\":" + std::to_string(first) + ",\"head\":" + head + ",\"events\":[";
        for (std::size_t i = 0; i < lines.size(); ++i) { if (i) body += ",\n"; body += lines[i]; }
        body += "]}\n";
        out = Chunk{}; out.seq = seq; out.first = first; out.count = (long long)lines.size();
        if (!Gzip(body, out.gz)) return false;
        pending.erase(pending.begin(), pending.begin() + (std::ptrdiff_t)take.size());
        headDirty = false;
        return true;
    }

    void stop(const std::string& code) { stopped = true; stopCode = code; haveInflight = false; resendPrev = false; pending.clear(); }

    // One answer to the chunk just sent (`prev`: the resend of the last accepted chunk).
    void answer(const Answer& a, bool prev) {
        const long long t = tick_now();
        if (a.ok && (a.status == 200 || a.status == 201)) {
            netFails = 0;
            UploadInfo u;
            if (site_answer(a, u)) { siteId = u.id; siteUrl = u.url; }
            if (prev) { resendPrev = false; return; }
            accepted = inflight; haveAccepted = true; haveInflight = false;
            seq = accepted.seq + 1; first = accepted.first + accepted.count; ++acceptedCount;
            return;
        }
        if (a.ok && a.status == 409 && a.code == "seq" && a.haveJson && !prev) {
            const long long expect = a.json.i("expect", -1), events = a.json.i("events", -1);
            if (haveAccepted && expect == accepted.seq && events == accepted.first) { resendPrev = true; nextTry = t; return; }
            stop("seq"); return;
        }
        if (a.ok && (a.status == 429 || a.status == 503)) { nextTry = t + (a.retryAfter >= 0 ? a.retryAfter : 60) * 1000; return; }
        if (!a.ok || a.status >= 500) {
            if (++netFails >= 10) { stop(a.ok ? "unavailable" : "network"); return; }
            nextTry = t + 30000; return;
        }
        if (a.ok && a.status == 410) { stop("deleted"); return; }
        if (a.ok && a.status == 401) { stop("unlinked"); return; }
        stop(a.code.empty() ? "rejected" : a.code);
    }

    // Sends one request (a resend, the chunk in flight, or a new one). False when nothing was sent.
    bool send_one(const http::Endpoint& ep, const std::string& auth, bool keepNames, int& status, Chunk* sentOut = nullptr) {
        bool prev = false;
        const Chunk* c = nullptr;
        if (resendPrev && haveAccepted) { c = &accepted; prev = true; }
        else {
            if (!haveInflight) { if (!build(keepNames, inflight)) return false; haveInflight = true; }
            c = &inflight;
        }
        auto hdr = upload_headers(auth, log.str("id"), "auto", visibility);
        hdr.push_back({ "X-RTX-Live-Seq", std::to_string(c->seq) });
        const Chunk sent = *c;
        const http::Response r = http::Post(ep, kLivePath, hdr, c->gz);
        lastSend = tick_now();
        status = r.ok ? r.status : 0;
        if (sentOut) *sentOut = sent;
        answer(to_answer(r), prev);
        return true;
    }
};

struct LiveSession {
    std::uint32_t pid = 0; std::string logId;
    std::vector<std::string> inbox; std::size_t inboxEvents = 0;   // under g_live_mu
    bool closing = false, overflow = false, gap = false;           // under g_live_mu
    LiveLog L;                                                     // the upload thread's
    // shown and decided under g_live_mu
    bool done = false, stopped = false, rowPending = false, decided = false;
    long long rowEndedAt = 0, doneAt = 0;
    int seqShown = 0; long long eventsShown = 0; std::string id, url, code;
};
std::mutex g_live_mu;
std::map<std::string, std::unique_ptr<LiveSession>> g_live;
std::set<std::string> g_live_skip;                     // logs whose first lines were not seen here
std::set<std::string> g_live_gone;                     // logs the site answered 410 for while live
std::atomic<long long> g_liveWantedAt{ -100000 };
std::atomic<bool> g_liveWanted{ false };

bool event_line(const std::string& l) { return l.size() > 1 && l[0] == '[' && (l[1] != '"' || l.compare(0, 7, "[\"mech\"") == 0); }

bool live_busy(const std::string& logId) {
    std::lock_guard<std::mutex> lk(g_live_mu);
    auto it = g_live.find(logId);
    return it != g_live.end() && !it->second->done;
}

// A marker that a log had live chunks accepted, so its final upload is still decided after a restart.
std::filesystem::path live_mark_locked(const std::string& id, bool create) { return root_locked(create) / (L"live_" + to_wide(id) + L".mark"); }
void live_mark(const std::string& id) {
    if (!id_chars(id, 22)) return;
    std::lock_guard<std::mutex> lk(g_mu);
    write_file(live_mark_locked(id, true), "1");
}
bool live_marked(const std::string& id) {
    if (!id_chars(id, 22)) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    std::error_code ec;
    return std::filesystem::exists(live_mark_locked(id, false), ec);
}
void live_unmark(const std::string& id) {
    if (!id_chars(id, 22)) return;
    std::lock_guard<std::mutex> lk(g_mu);
    std::error_code ec;
    std::filesystem::remove(live_mark_locked(id, false), ec);
}

void queue_row(const std::string& id, bool final) {
    if (update_row(id, [&](IndexRow& r) { r.state = UploadState{}; r.state.state = "queued"; r.state.final = final; }))
        rtx::log::Launcher("combat: upload " + id + " queued");
    kick();
}
// After a log closed: the final upload when live chunks went out and an upload setting is still on, else the
// auto rule.
void decide_after_close(const std::string& id, long long endedAt, int liveAccepted, bool gone) {
    live_unmark(id);
    if (gone) { update_row(id, [&](IndexRow& r) { r.state = UploadState{}; r.state.state = "skipped"; r.state.code = "deleted"; r.state.error = error_text("deleted"); }); return; }
    if (liveAccepted > 0 && (UploadLive() || UploadAuto())) { queue_row(id, true); return; }
    if (UploadAuto() && !link::AuthHeader().empty() && endedAt >= UploadAutoSince()) queue_row(id, false);
}

void live_pass() {
    std::vector<LiveSession*> list;
    {
        std::lock_guard<std::mutex> lk(g_live_mu);
        const long long t = tick_now();
        for (auto it = g_live.begin(); it != g_live.end();) {
            LiveSession& s = *it->second;
            if (s.done && (s.decided || t - s.doneAt > 5ll * 3600 * 1000)) { it = g_live.erase(it); continue; }
            if (!s.done) list.push_back(&s);
            ++it;
        }
    }
    const std::string auth = link::AuthHeader();
    for (LiveSession* s : list) {
        std::vector<std::string> in; bool closing = false;
        bool overflow = false, gap = false;
        { std::lock_guard<std::mutex> lk(g_live_mu); in.swap(s->inbox); s->inboxEvents = 0; closing = s->closing; overflow = s->overflow; gap = s->gap; }
        LiveLog& L = s->L;
        if (!L.stopped && !UploadLive()) L.stop("off");                // switched off: nothing more goes out
        if (!L.stopped && gap) L.stop("gap");                           // lines were dropped: the final upload repairs the log
        if (!L.stopped) L.feed(in);
        if (!L.stopped && (overflow || L.pending.size() > kLivePending)) L.stop("live_too_long");
        if (L.ended) closing = true;
        const long long t = tick_now();
        const bool fought = L.open || !L.closed.empty();               // nothing goes out before the first fight
        if (closing && !fought && !L.haveInflight) { L.pending.clear(); L.headDirty = false; }
        if (!L.stopped && !auth.empty() && fought && t >= L.nextTry && t - L.lastSend >= 11000) {
            bool due;
            if (L.haveInflight || L.resendPrev) due = true;
            else if (L.idle()) due = false;
            else if (closing) due = true;
            else if (L.open) due = t - L.lastSend >= 15000;
            else due = t - L.lastSend >= 60000 || (L.fightEndAt > 0 && t - L.fightEndAt >= 10000 && L.lastSend < L.fightEndAt + 10000);
            if (due) {
                int status = 0; Chunk sent;
                const bool wasPrev = L.resendPrev;
                const int acceptedBefore = L.acceptedCount;
                if (L.send_one(http::Endpoint{ kUploadHost }, auth, KeepNames(), status, &sent))
                    rtx::log::Launcher("combat: live " + s->logId + " chunk " + std::to_string(sent.seq) + ", " + std::to_string(sent.count) + " events, " +
                                       std::to_string(status) + (wasPrev ? " (resent)" : "") + (L.stopped ? " stopped " + L.stopCode : ""));
                if (acceptedBefore == 0 && L.acceptedCount > 0) live_mark(s->logId);
                else if (closing) { L.pending.clear(); L.headDirty = false; }   // what is left waits for actor rows that never came
                if (L.stopped && L.stopCode == "unlinked") link::Verify();
            }
        }
        if (auth.empty() && closing) L.stop("unlinked");
        const bool finished = L.stopped || (closing && !L.haveInflight && !L.resendPrev && L.idle());
        bool decideNow = false; long long endedAt = 0; int acc = 0; bool gone = false;
        {
            std::lock_guard<std::mutex> lk(g_live_mu);
            s->seqShown = L.seq; s->eventsShown = L.first; s->id = L.siteId; s->url = L.siteUrl;
            s->stopped = L.stopped; s->code = L.stopCode;
            if (L.stopped && L.stopCode == "deleted") g_live_gone.insert(s->logId);
            if (finished) {
                s->done = true; s->doneAt = t;
                if (s->rowPending) { decideNow = true; s->decided = true; endedAt = s->rowEndedAt; acc = L.acceptedCount; gone = L.stopCode == "deleted"; }
            }
        }
        if (decideNow) decide_after_close(s->logId, endedAt, acc, gone);
    }
}

void upload_pass() {
    const std::string auth = link::AuthHeader();
    const long long now = wall_now();
    IndexRow row; std::filesystem::path file; bool found = false;
    // the flags lock g_mu themselves: read before taking it
    const bool autoOn = UploadAuto(), liveOn = UploadLive();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto rows = index_read_locked();
        bool changed = false;
        const std::size_t h = std::hash<std::string>{}(auth);
        for (auto& r : rows) {
            const bool pending = r.state.state == "queued" || r.state.state == "sending" || r.state.state == "unlinked";
            // only a log queued by hand outlives the upload settings: an automatic or final upload goes when they go
            const bool wanted = r.state.manual || autoOn || (r.state.final && liveOn);
            if (pending && !wanted) { r.state = UploadState{}; changed = true; continue; }
            if (r.state.state == "unlinked" && !auth.empty() && h != g_unlinkedAuth) { r.state.state = "queued"; r.state.next = 0; r.state.error.clear(); r.state.code.clear(); changed = true; }
        }
        IndexRow* best = nullptr;
        for (auto& r : rows) {
            if (r.state.state != "queued" && r.state.state != "sending") continue;
            if (r.state.next > now || (r.state.final && live_busy(r.id))) continue;
            if (!best || r.startedAt < best->startedAt) best = &r;
        }
        if (best) {
            if (auth.empty()) { best->state.state = "unlinked"; best->state.code = "unlinked"; best->state.error = error_text("unlinked"); changed = true; }
            else if (tick_now() - g_lastFinalTick >= 21000) { best->state.state = "sending"; row = *best; file = root_locked(false) / to_wide(best->file); found = true; changed = true; }
        }
        if (changed) index_write_locked(rows);
    }
    if (!found) return;
    std::string gz, body, err; combatprep::Stats st;
    if (!read_file(file, gz) || !prepare_body(gz, KeepNames(), body, st, err) || body.size() > kMaxBody) {
        const std::string code = body.size() > kMaxBody || err == "too_large" ? "too_large" : "bad_log";
        update_row(row.id, [&](IndexRow& r) { r.state.state = "failed"; r.state.code = code; r.state.error = error_text(code); });
        rtx::log::Launcher("combat: upload " + row.id + " failed, 0 " + code);
        return;
    }
    const char* mode = row.state.final ? "final" : row.state.manual ? "manual" : "auto";
    const http::Response r = http::Post(http::Endpoint{ kUploadHost }, kLogsPath, upload_headers(auth, row.id, mode, row.uploadAs.empty() ? UploadVisibility() : row.uploadAs), body);
    g_lastFinalTick = tick_now();
    const Answer a = to_answer(r);
    UploadInfo info;
    const Outcome o = classify(a, row.state.tries, info);
    if (o.verify) g_unlinkedAuth = std::hash<std::string>{}(auth);
    const bool kept = update_row(row.id, [&](IndexRow& x) {
        if (o.sent) { x.upload = info; x.state = UploadState{}; return; }
        const int tries = x.state.tries + (o.retry ? 1 : 0);
        const bool manual = x.state.manual, final = x.state.final;
        x.state = UploadState{};
        x.state.state = o.state; x.state.code = o.code; x.state.tries = tries; x.state.manual = manual; x.state.final = final;
        x.state.next = o.delayMs > 0 ? wall_now() + o.delayMs : 0;
        if (o.state != "queued" || o.code == "daily_limit" || o.code == "disabled") x.state.error = error_text(o.code.empty() ? "unavailable" : o.code);
    });
    const std::string status = std::to_string(a.ok ? a.status : 0);
    if (!kept) rtx::log::Launcher("combat: upload " + row.id + " answered " + status + " after the log was deleted");
    else if (o.sent) rtx::log::Launcher("combat: upload " + row.id + " sent, " + status + ", " + std::to_string(body.size()) + " bytes");
    else rtx::log::Launcher("combat: upload " + row.id + " failed, " + status + " " + (o.code.empty() ? std::string("retry") : o.code));
    if (o.verify) link::Verify();
}

void upload_loop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(g_up_mu);
            g_up_cv.wait_for(lk, std::chrono::seconds(5), [] { return g_up_kick; });
            g_up_kick = false;
        }
        try { live_pass(); } catch (...) { rtx::log::Launcher("combat: live pass failed"); }
        try { upload_pass(); } catch (...) { rtx::log::Launcher("combat: upload pass failed"); }
    }
}

std::string sha256_hex(const std::string& d) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char out[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return hex;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        if (BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(d.data())), (ULONG)d.size(), 0) == 0 && BCryptFinishHash(h, out, 32, 0) == 0) {
            static const char* k = "0123456789abcdef";
            for (unsigned char b : out) { hex.push_back(k[b >> 4]); hex.push_back(k[b & 15]); }
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

// http://127.0.0.1:<port> or http://localhost:<port>, nothing else.
bool loopback_base(const std::wstring& base, http::Endpoint& ep) {
    std::wstring b = base;
    if (!b.empty() && b.back() == L'/') b.pop_back();
    for (const wchar_t* host : { L"127.0.0.1", L"localhost" }) {
        const std::wstring pre = std::wstring(L"http://") + host + L":";
        if (b.compare(0, pre.size(), pre) != 0) continue;
        const std::wstring port = b.substr(pre.size());
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return false;
        const long v = std::wcstol(port.c_str(), nullptr, 10);
        if (v < 1 || v > 65535) return false;
        ep = http::Endpoint{ host, (unsigned short)v, false };
        return true;
    }
    return false;
}
bool dev_token(std::string& auth) {
    wchar_t buf[256] = {};
    const DWORD n = GetEnvironmentVariableW(L"RTX_DEV_TOKEN", buf, 256);
    if (n == 0 || n >= 256) return false;
    std::string t;
    for (DWORD i = 0; i < n; ++i) { if (buf[i] > 0x7E || buf[i] < 0x21) return false; t.push_back((char)buf[i]); }
    if (t.compare(0, 5, "rtxd_") != 0) return false;
    auth = "Bearer " + t;
    return true;
}
std::string stats_report(const combatprep::Stats& st) {
    return "players=" + std::to_string(st.playersRenamed) + "\nnpcFixed=" + std::to_string(st.npcNamesFixed) + "\nseqDropped=" + std::to_string(st.seqNamesDropped) +
           "\nhitmarkNamesDropped=" + std::to_string(st.hitmarkNamesDropped) + "\nseqinfo=" + std::to_string(st.seqinfo) + "\ndevNames=" + std::to_string(st.devNamesDropped) +
           "\nnameStrings=" + std::to_string(st.nameStringsCleared) + "\nxpDropped=" + std::to_string(st.xpDropped) + "\n";
}
std::string outcome_text(const Outcome& o) {
    if (o.sent) return "sent";
    return o.state + (o.code.empty() ? std::string() : " " + o.code) + (o.delayMs > 0 ? " after " + std::to_string(o.delayMs / 1000) + " s" : std::string());
}
bool read_wfile(const std::wstring& p, std::string& out) { return read_file(std::filesystem::path(p), out); }
// log.id of a log object, reading only the top level up to "log".
std::string log_id_of(const std::string& json) {
    combatprep::detail::Parser ps; ps.p = json.data(); ps.e = json.data() + json.size();
    ps.ws();
    if (ps.p >= ps.e || *ps.p != '{') return {};
    ++ps.p;
    for (;;) {
        ps.ws();
        std::string key;
        if (!ps.str(&key)) return {};
        ps.ws(); if (ps.p >= ps.e || *ps.p != ':') return {};
        ++ps.p;
        if (key == "log") { combatprep::detail::Node n; if (!ps.val(n) || n.k != combatprep::detail::Node::Obj) return {}; return n.str("id"); }
        if (!ps.skip()) return {};
        ps.ws();
        if (ps.p < ps.e && *ps.p == ',') { ++ps.p; continue; }
        return {};
    }
}

}  // namespace

void StartUploads() {
    if (g_headless) return;
    std::lock_guard<std::mutex> lk(g_up_mu);
    if (g_up_started) return;
    g_up_started = true;
    std::thread([] { upload_loop(); }).detach();
}
void SetHeadless() { g_headless = true; }

std::string UploadRequestJson(const std::string& logId) {
    if (!id_chars(logId, 22)) return "{\"ok\":false,\"state\":\"missing\",\"error\":\"Log not found.\"}";
    IndexRow r; std::filesystem::path file;
    if (!find_row(logId, r, file)) return "{\"ok\":false,\"state\":\"missing\",\"error\":\"Log not found.\"}";
    if (!r.upload.id.empty()) return "{\"ok\":true,\"state\":\"sent\",\"id\":" + jstr(r.upload.id) + ",\"url\":" + jstr(r.upload.url) + "}";
    if (link::AuthHeader().empty()) return "{\"ok\":false,\"state\":\"unlinked\",\"error\":" + jstr(error_text("unlinked")) + "}";
    const bool ok = update_row(logId, [&](IndexRow& x) { x.state = UploadState{}; x.state.state = "queued"; x.state.manual = true; });
    if (!ok) return "{\"ok\":false,\"state\":\"missing\",\"error\":\"Log not found.\"}";
    rtx::log::Launcher("combat: upload " + logId + " queued");
    StartUploads();
    kick();
    return "{\"ok\":true,\"state\":\"queued\"}";
}

std::string UploadStatusJson(const std::string& logId) {
    if (logId.empty()) return link::AuthHeader().empty() ? "{\"state\":\"unlinked\"}" : "{\"state\":\"ready\"}";
    IndexRow r; std::filesystem::path file;
    std::string state = "none", id, url, error;
    if (id_chars(logId, 22) && find_row(logId, r, file)) {
        if (!r.upload.id.empty()) { state = "sent"; id = r.upload.id; url = r.upload.url; }
        else if (!r.state.state.empty()) { state = r.state.state; error = r.state.error; }
    }
    return "{\"state\":" + jstr(state) + ",\"id\":" + jstr(id) + ",\"url\":" + jstr(url) + ",\"error\":" + jstr(error) + "}";
}

void AfterCompact(const IndexRow& row) {
    if (g_headless) return;
    bool decide = true, gone = false; int acc = 0;
    {
        std::lock_guard<std::mutex> lk(g_live_mu);
        gone = g_live_gone.count(row.id) > 0;
        if (auto it = g_live.find(row.id); it != g_live.end()) {
            LiveSession& s = *it->second;
            if (s.done) { acc = s.L.acceptedCount; s.decided = true; }
            else { s.rowPending = true; s.rowEndedAt = row.endedAt; decide = false; }
        }
    }
    if (decide && acc == 0 && live_marked(row.id)) acc = 1;
    if (decide) decide_after_close(row.id, row.endedAt, acc, gone);
}

bool LiveWanted() {
    if (g_headless) return false;
    const long long t = tick_now();
    if (t - g_liveWantedAt.load() >= 2000) {
        g_liveWanted = UploadLive() && !link::AuthHeader().empty();
        g_liveWantedAt = t;
    }
    return g_liveWanted.load();
}

void LiveFeed(std::uint32_t pid, const std::string& logId, const std::vector<std::string>& lines) {
    if (g_headless || lines.empty() || logId.empty()) return;
    std::size_t evs = 0;
    for (const auto& l : lines) if (event_line(l)) ++evs;
    std::lock_guard<std::mutex> lk(g_live_mu);
    auto it = g_live.find(logId);
    if (it == g_live.end()) {
        if (g_live_skip.count(logId)) return;
        std::size_t at = 0;
        while (at < lines.size() && (lines[at].empty() || lines[at][0] != '{')) ++at;
        if (at == lines.size()) { g_live_skip.insert(logId); return; }   // live was turned on in the middle of this log
        auto s = std::make_unique<LiveSession>();
        s->pid = pid; s->logId = logId;
        s->inbox.assign(lines.begin() + (std::ptrdiff_t)at, lines.end());
        for (const auto& l : s->inbox) if (event_line(l)) ++s->inboxEvents;
        g_live.emplace(logId, std::move(s));
        return;
    }
    LiveSession& s = *it->second;
    if (s.done || s.stopped || s.closing || s.overflow || s.gap) return;
    // the upload thread drains the inbox; this only bounds what waits for it
    if (s.inboxEvents + evs > kLivePending) { s.inbox.clear(); s.inboxEvents = 0; s.overflow = true; return; }
    s.inbox.insert(s.inbox.end(), lines.begin(), lines.end());
    s.inboxEvents += evs;
}

void LiveClose(std::uint32_t, const std::string& logId) {
    {
        std::lock_guard<std::mutex> lk(g_live_mu);
        auto it = g_live.find(logId);
        if (it == g_live.end()) return;
        it->second->closing = true;
    }
    kick();
}

void LiveGap(const std::string& logId) {
    if (g_headless || logId.empty()) return;
    {
        std::lock_guard<std::mutex> lk(g_live_mu);
        auto it = g_live.find(logId);
        if (it == g_live.end() || it->second->done || it->second->gap) return;
        it->second->gap = true;
        it->second->inbox.clear(); it->second->inboxEvents = 0;
    }
    kick();
}

std::string LiveStateJson() {
    std::string o = std::string("{\"enabled\":") + (UploadLive() ? "true" : "false") + ",\"logs\":[";
    std::lock_guard<std::mutex> lk(g_live_mu);
    bool first = true;
    for (const auto& kv : g_live) {
        const LiveSession& s = *kv.second;
        if (s.done && (!s.stopped || s.code == "off")) continue;      // turned off on purpose: not an error
        o += first ? "" : ","; first = false;
        o += "{\"pid\":" + std::to_string(s.pid) + ",\"logId\":" + jstr(s.logId) + ",\"id\":" + jstr(s.id) + ",\"url\":" + jstr(s.url) +
             ",\"seq\":" + std::to_string(s.seqShown) + ",\"events\":" + std::to_string(s.eventsShown) +
             ",\"state\":\"" + (s.stopped ? "stopped" : "live") + "\",\"error\":" + jstr(s.stopped ? error_text(s.code) : std::string()) + "}";
    }
    return o + "]}";
}

// ---- headless test paths ----
int CliPrep(const std::wstring& in, const std::wstring& outPath, bool keep, std::string& report) {
    std::string gz, body, err; combatprep::Stats st;
    if (!read_wfile(in, gz)) { report = "error=cannot read the input\n"; return 1; }
    if (!prepare_body(gz, keep, body, st, err)) { report = "error=" + err + "\n"; return 1; }
    if (!write_file(std::filesystem::path(outPath), body)) { report = "error=cannot write the output\n"; return 1; }
    report = "bytes=" + std::to_string(body.size()) + "\nsha256=" + sha256_hex(body) + "\n" + stats_report(st) + "events=" + std::to_string(st.events) + "\n";
    return 0;
}

int CliUpload(const std::wstring& in, const std::wstring& base, bool keep, std::string& report) {
    http::Endpoint ep; std::string auth;
    if (!loopback_base(base, ep)) { report = "error=the base url must be http://127.0.0.1:<port> or http://localhost:<port>\n"; return 2; }
    if (!dev_token(auth)) { report = "error=RTX_DEV_TOKEN must hold a device token\n"; return 2; }
    std::string gz, body, err; combatprep::Stats st;
    if (!read_wfile(in, gz)) { report = "error=cannot read the input\n"; return 1; }
    if (!prepare_body(gz, keep, body, st, err)) { report = "error=" + err + "\n"; return 1; }
    std::string json; Gunzip(gz, json, kMaxRaw);
    const std::string logId = log_id_of(json);
    report = "bytes=" + std::to_string(body.size()) + "\nsha256=" + sha256_hex(body) + "\n" + stats_report(st);
    if (body.size() > kMaxBody) { report = "status=0\n" + report + "result=failed too_large\nbody=\n"; return 1; }
    const http::Response r = http::Post(ep, kLogsPath, upload_headers(auth, logId, "manual"), body);
    const Answer a = to_answer(r);
    UploadInfo info;
    const Outcome o = classify(a, 0, info);
    std::string answerBody = a.body;
    for (char& ch : answerBody) if (ch == '\r' || ch == '\n') ch = ' ';
    report = "status=" + std::to_string(a.ok ? a.status : 0) + "\n" + report + "result=" + outcome_text(o) + "\nbody=" + answerBody + "\n";
    return o.sent ? 0 : 1;
}

int CliLiveReplay(const std::wstring& in, const std::wstring& base, int chunkEvents, bool final, std::string& report) {
    using combatprep::detail::Node;
    http::Endpoint ep; std::string auth;
    if (!loopback_base(base, ep)) { report = "error=the base url must be http://127.0.0.1:<port> or http://localhost:<port>\n"; return 2; }
    if (!dev_token(auth)) { report = "error=RTX_DEV_TOKEN must hold a device token\n"; return 2; }
    if (chunkEvents < 1) chunkEvents = 500;
    std::string gz, json;
    if (!read_wfile(in, gz) || !Gunzip(gz, json, kMaxRaw)) { report = "error=cannot read the input\n"; return 1; }
    Node doc; combatprep::detail::Parser ps; ps.p = json.data(); ps.e = json.data() + json.size();
    if (!ps.val(doc) || doc.k != Node::Obj) { report = "error=bad json\n"; return 1; }
    const Node* log = doc.get("log"); const Node* clock = doc.get("clock"); const Node* actors = doc.get("actors");
    const Node* dict = doc.get("dict"); const Node* fights = doc.get("fights"); const Node* events = doc.get("events");
    if (!log || !clock || !actors || !dict || !fights || !events || actors->k != Node::Arr || events->k != Node::Arr || fights->k != Node::Arr) { report = "error=not a log\n"; return 1; }
    auto w = [](const Node& n) { std::string o; combatprep::detail::write(n, o); return o; };
    auto field = [&](const Node& obj, const char* k) { const Node* v = obj.get(k); return v ? w(*v) : std::string("null"); };
    // the recorder's lines, in the order it writes them: actor and dict rows before the first event that needs them
    std::vector<std::vector<std::string>> batches;     // lines per chunk, `chunkEvents` events each
    std::vector<std::string> cur; int curEvents = 0;
    cur.push_back("{\"format\":1,\"log\":{\"id\":" + field(*log, "id") + ",\"character\":" + field(*log, "character") + ",\"launcher\":" + field(*log, "launcher") +
                  ",\"client\":" + field(*log, "client") + ",\"startedAt\":" + field(*log, "startedAt") + ",\"companion\":false},\"clock\":" + w(*clock) + "}");
    std::size_t actorsOut = 0;
    auto needActor = [&](long long i, std::vector<std::string>& o) {
        while (i >= 0 && (long long)actorsOut <= i && actorsOut < actors->c.size()) {
            const Node& a = actors->c[actorsOut++];
            std::string t = "[\"actor\",{"; bool f1 = true;
            for (const auto& m : a.c) { if (m.key == "last") continue; if (!f1) t += ","; f1 = false; t += m.keyRaw + ":" + w(m); }
            o.push_back(t + "}]");
        }
    };
    std::set<std::pair<std::string, std::string>> dictOut;
    auto needDict = [&](const char* kind, const std::string& id, std::vector<std::string>& o) {
        const Node* d = dict->get(kind);
        if (!d || !dictOut.insert({ kind, id }).second) return;
        const Node* v = d->get(id.c_str());
        if (!v) return;
        o.push_back("[\"dict\",\"" + std::string(kind) + "\"," + id + "," + w(*v) + "]");
        if (std::strcmp(kind, "abilities") == 0) if (const Node* fam = v->get("family"); fam && fam->k == Node::Arr) for (const auto& m : fam->c) {
            const Node* fv = d->get(m.raw.c_str());
            if (fv && dictOut.insert({ kind, m.raw }).second) o.push_back("[\"dict\",\"abilities\"," + m.raw + "," + w(*fv) + "]");
        }
    };
    std::size_t nextFight = 0;
    auto fightLine = [&](const Node& f) {
        std::string t = "[\"fight\",{"; bool f1 = true;
        for (const auto& m : f.c) { if (m.key == "startMs" || m.key == "summary") continue; if (!f1) t += ","; f1 = false; t += m.keyRaw + ":" + w(m); }
        return t + "}]";
    };
    combatprep::detail::EventView v;
    for (const auto& e : events->c) {
        const std::string line = w(e);
        long long c = 0;
        if (e.k == Node::Arr && e.c.size() >= 2) c = (long long)e.c[1].num;
        while (nextFight < fights->c.size()) {
            long long end = 0; fights->c[nextFight].num_of("end", end);
            if (c <= end) break;
            cur.push_back(fightLine(fights->c[nextFight++]));
        }
        if (combatprep::detail::view_event(line.data(), line.size(), v)) {
            auto num = [&](std::size_t k) { return k < e.c.size() ? (long long)e.c[k].num : -1; };
            auto key = [&](std::size_t k) { return k < e.c.size() ? e.c[k].raw : std::string(); };
            needActor(max_ref(line, v), cur);
            switch (v.type) {
            case 0: needDict("hitmarks", key(3), cur); if (num(5) >= 0) needDict("hitmarks", key(5), cur); break;
            case 1: needDict("abilities", key(2), cur); break;
            case 2: if (num(3) >= 0) needDict("seqs", key(3), cur); break;
            case 7: needDict("buffs", key(2), cur); break;
            case 9: needDict("trackers", key(2), cur); break;
            case 12: if (num(2) >= 0) needDict("encounters", key(2), cur); break;
            case 1000: needDict("mechs", key(2), cur); break;
            default: break;
            }
        }
        cur.push_back(line);
        if (++curEvents >= chunkEvents) { batches.push_back(std::move(cur)); cur.clear(); curEvents = 0; }
    }
    while (nextFight < fights->c.size()) cur.push_back(fightLine(fights->c[nextFight++]));
    needActor((long long)actors->c.size() - 1, cur);
    static const char* kinds[] = { "abilities", "buffs", "hitmarks", "seqs", "encounters", "trackers", "mechs" };
    for (const char* k : kinds) if (const Node* d = dict->get(k)) for (const auto& m : d->c) needDict(k, m.key, cur);
    cur.push_back("[\"end\",{\"endedAt\":" + field(*log, "endedAt") + ",\"endBy\":" + field(*log, "endBy") + ",\"readFails\":" + field(*log, "readFails") +
                  (log->get("reads") ? ",\"reads\":" + field(*log, "reads") : std::string()) +
                  ",\"gaps\":" + field(*log, "gaps") + ",\"phase\":" + field(*clock, "phase") + ",\"events\":" + std::to_string(events->c.size()) + "}]");
    batches.push_back(std::move(cur));

    LiveLog L;
    bool ok = true; int sends = 0;
    auto wait_ms = [](long long ms) { if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    for (std::size_t b = 0; b < batches.size() && ok; ++b) {
        L.feed(batches[b]);
        const bool last = b + 1 == batches.size();
        for (int guard = 0; guard < 50 && ok; ++guard) {
            if (!L.haveInflight && !L.resendPrev && L.idle()) break;
            if (sends > 0) wait_ms(std::max<long long>(11000 - (tick_now() - L.lastSend), L.nextTry - tick_now()));
            int status = 0; Chunk sent;
            const bool prev = L.resendPrev;
            if (!L.send_one(ep, auth, false, status, &sent)) {
                if (last && !L.pending.empty()) { report += "held " + std::to_string(L.pending.size()) + " events waiting for actor rows\n"; ok = false; }
                break;
            }
            ++sends;
            report += "chunk " + std::to_string(sent.seq) + " first " + std::to_string(sent.first) + " events " + std::to_string(sent.count) +
                      " status " + std::to_string(status) + (prev ? " resent" : "") + (L.stopped ? " stopped " + L.stopCode : "") + "\n";
            if (L.stopped) ok = false;
        }
    }
    report += "accepted " + std::to_string(L.acceptedCount) + " chunks, " + std::to_string(L.first) + " events, id " + L.siteId + "\n";
    if (ok && final) {
        wait_ms(11000);
        std::string body, err; combatprep::Stats st;
        if (!prepare_body(gz, false, body, st, err)) { report += "final error=" + err + "\n"; return 1; }
        const http::Response r = http::Post(ep, kLogsPath, upload_headers(auth, log->str("id"), "final"), body);
        const Answer a = to_answer(r);
        UploadInfo info;
        const Outcome o = classify(a, 0, info);
        std::string answerBody = a.body;
        for (char& ch : answerBody) if (ch == '\r' || ch == '\n') ch = ' ';
        report += "final status=" + std::to_string(a.ok ? a.status : 0) + " result=" + outcome_text(o) + " body=" + answerBody + "\n";
        ok = o.sent;
    }
    return ok ? 0 : 1;
}

}  // namespace rtx::launcher::fights
