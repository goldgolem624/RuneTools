#include "Names.h"

#include "CachePath.h"
#include "CacheReader.h"
#include "FileContainer.h"
#include "JagexContainer.h"
#include "vendor/sqlite/sqlite3.h"
#include "../shared/Log.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace rtx::names {
namespace {

constexpr int kTableIndex = 67;
constexpr int kFileFormat = 4;   // bump when the file layout or the matching rules change
constexpr const char* kSecondRoot = "C:/ProgramData/Jagex/RuneScape-BETA";

using Bytes = std::vector<std::uint8_t>;

std::uint64_t Fnv(const std::uint8_t* p, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
std::uint64_t Fnv(const Bytes& b) { return Fnv(b.data(), b.size()) ^ (std::uint64_t)b.size(); }
std::uint64_t Fnv(const std::string& s) { return Fnv((const std::uint8_t*)s.data(), s.size()); }

std::string Hex(std::uint64_t v, int digits = 16) {
    char b[32]; std::snprintf(b, sizeof(b), "%0*llx", digits, (unsigned long long)v);
    return b;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((std::size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring EnvW(const wchar_t* name) {
    wchar_t buf[1024] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return (n > 0 && n < 1024) ? std::wstring(buf, n) : std::wstring();
}

std::wstring KeepDir() {
    const std::wstring up = EnvW(L"USERPROFILE");
    return up.empty() ? std::wstring() : up + L"\\RuneToolsX\\names";
}

bool HasFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
}

void JsonStr(std::string& o, const char* s) {
    o += '"';
    for (; *s; ++s) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    o += '"';
}

// ---- one jcache file, read only ----

class Js5 {
public:
    Js5(const std::string& root, int index) {
        const std::string path = root + "/js5-" + std::to_string(index) + ".jcache";
        if (!HasFile(path)) return;
        if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) { Close(); failed_ = true; return; }
        sqlite3_busy_timeout(db_, 4000);
    }
    ~Js5() { Close(); }
    Js5(const Js5&) = delete;
    Js5& operator=(const Js5&) = delete;

    bool ok() const { return db_ != nullptr; }
    // A read hit a locked, busy or unreadable file (as opposed to a row that is simply not there).
    bool failed() const { return failed_; }

    Bytes RefBlob() { return Blob("SELECT DATA FROM cache_index WHERE KEY = ?;", ref_, 1); }
    Bytes Row(int key) { return Blob("SELECT DATA FROM cache WHERE KEY = ?;", row_, key); }

private:
    void Close() {
        if (ref_) { sqlite3_finalize(ref_); ref_ = nullptr; }
        if (row_) { sqlite3_finalize(row_); row_ = nullptr; }
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
    }
    Bytes Blob(const char* sql, sqlite3_stmt*& st, int key) {
        Bytes out;
        if (!db_) return out;
        if (!st && sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { st = nullptr; failed_ = true; return out; }
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, key);
        const int rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            const void* p = sqlite3_column_blob(st, 0);
            const int n = sqlite3_column_bytes(st, 0);
            if (p && n > 0) out.assign((const std::uint8_t*)p, (const std::uint8_t*)p + n);
        } else if (rc != SQLITE_DONE) {
            failed_ = true;
        }
        sqlite3_reset(st);
        return out;
    }

    sqlite3* db_ = nullptr;
    sqlite3_stmt* ref_ = nullptr;
    sqlite3_stmt* row_ = nullptr;
    bool failed_ = false;
};

// ---- reference table: per archive crc, lengths and file ids ----

struct Rd {
    const std::uint8_t* p; std::size_t n; std::size_t i = 0; bool bad = false;
    bool Has(std::size_t k) { if (i + k > n) { bad = true; i = n; return false; } return true; }
    std::uint32_t U8() { return Has(1) ? p[i++] : 0; }
    std::uint32_t U16() { if (!Has(2)) return 0; std::uint32_t v = ((std::uint32_t)p[i] << 8) | p[i + 1]; i += 2; return v; }
    std::uint32_t U32() {
        if (!Has(4)) return 0;
        std::uint32_t v = ((std::uint32_t)p[i] << 24) | ((std::uint32_t)p[i + 1] << 16) | ((std::uint32_t)p[i + 2] << 8) | p[i + 3];
        i += 4; return v;
    }
    std::uint32_t Smart32() { if (!Has(1)) return 0; return (p[i] & 0x80) ? (U32() & 0x7FFFFFFFu) : U16(); }
    void Skip(std::size_t k) { if (Has(k)) i += k; }
};

struct Arch {
    bool present = false;
    std::uint32_t crc = 0, clen = 0, ulen = 0;
    std::vector<int> files;   // ascending
};

struct Ref {
    bool ok = false;
    std::uint32_t version = 0;
    std::uint64_t fp = 0;     // of the stored blob
    std::vector<Arch> a;      // by archive id
    const Arch* Get(int id) const { return (id >= 0 && id < (int)a.size() && a[id].present) ? &a[id] : nullptr; }
};

Ref ParseRef(const Bytes& blob) {
    Ref r;
    if (blob.empty()) return r;
    r.fp = Fnv(blob);
    const Bytes d = rtx::cache::Decompress(blob);
    if (d.empty()) return r;
    Rd w{ d.data(), d.size() };
    const int proto = (int)w.U8();
    if (proto < 5 || proto > 7) return r;
    if (proto >= 6) r.version = w.U32();
    const int flags = (int)w.U8();
    auto rd = [&]() -> std::uint32_t { return proto >= 7 ? w.Smart32() : w.U16(); };
    const std::uint32_t count = rd();
    if (count > 2000000u || w.bad) return r;
    std::vector<int> ids(count);
    std::uint32_t run = 0;
    for (std::uint32_t k = 0; k < count; ++k) {
        run += rd();
        if (run > 4000000u) return r;
        ids[k] = (int)run;
    }
    if (w.bad) return r;
    r.a.resize(count ? (std::size_t)ids.back() + 1 : 0);
    for (int id : ids) r.a[id].present = true;
    if (flags & 1) w.Skip((std::size_t)count * 4);
    for (int id : ids) r.a[id].crc = w.U32();
    if (flags & 8) w.Skip((std::size_t)count * 4);
    if (flags & 2) w.Skip((std::size_t)count * 64);
    if (flags & 4) for (int id : ids) { r.a[id].clen = w.U32(); r.a[id].ulen = w.U32(); }
    w.Skip((std::size_t)count * 4);   // per-archive versions
    std::vector<std::uint32_t> nf(count);
    for (std::uint32_t k = 0; k < count; ++k) { nf[k] = rd(); if (nf[k] > 2000000u) return r; }
    for (std::uint32_t k = 0; k < count; ++k) {
        auto& fs = r.a[ids[k]].files;
        fs.reserve(nf[k]);
        std::uint32_t f = 0;
        for (std::uint32_t j = 0; j < nf[k]; ++j) { f += rd(); fs.push_back((int)f); }
    }
    r.ok = !w.bad;
    return r;
}

// ---- one index 67 name table ----
// u32 format (1 dense, 2 sparse), u32 count, then count x u32 offset (dense: entry i names id i,
// 0xFFFFFFFF none) or count x {u32 id, u32 offset}, then NUL-terminated names; offsets are relative
// to the start of the names.

struct Table {
    Bytes d;
    bool ok = false, dense = true;
    std::uint32_t n = 0;
    std::size_t str = 0;

    std::uint32_t At(std::size_t o) const {
        return ((std::uint32_t)d[o] << 24) | ((std::uint32_t)d[o + 1] << 16) | ((std::uint32_t)d[o + 2] << 8) | d[o + 3];
    }
    int IdAt(std::uint32_t k) const { return dense ? (int)k : (int)At(8 + (std::size_t)k * 8); }
    const char* NameAt(std::uint32_t k) const {
        const std::uint32_t off = dense ? At(8 + (std::size_t)k * 4) : At(12 + (std::size_t)k * 8);
        if (off == 0xFFFFFFFFu || str + off >= d.size()) return nullptr;
        const char* s = (const char*)&d[str + off];
        return *s ? s : nullptr;
    }
    const char* Get(int id) const {
        if (!ok || id < 0) return nullptr;
        if (dense) return (std::uint32_t)id < n ? NameAt((std::uint32_t)id) : nullptr;
        std::uint32_t lo = 0, hi = n;
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            const int v = IdAt(mid);
            if (v == id) return NameAt(mid);
            if (v < id) lo = mid + 1; else hi = mid;
        }
        return nullptr;
    }
    // Var tables: plain var names first, then the varbits of that domain as "_NAME" from here.
    int VarbitBase() const {
        int base = 0;
        for (std::uint32_t k = 0; k < n; ++k) {
            const char* s = NameAt(k);
            if (s && s[0] != '_') base = std::max(base, IdAt(k) + 1);
        }
        return base;
    }
};

bool ParseTable(Bytes raw, Table& t) {
    t = Table{};
    if (raw.size() < 8) return false;
    t.d = std::move(raw);
    const std::uint32_t fmt = t.At(0);
    t.n = t.At(4);
    if (fmt != 1 && fmt != 2) return false;
    t.dense = fmt == 1;
    const std::uint64_t head = 8ull + (std::uint64_t)t.n * (t.dense ? 4 : 8);
    if (head > t.d.size() || t.d.back() != 0) return false;
    t.str = (std::size_t)head;
    t.ok = true;
    return true;
}

// ---- what is named, and how a game id is matched to the source's id ----

enum Mode { kConfig, kSplit, kArchive, kVar, kIface, kTableOnly };

struct Spec {
    const char* kind;
    int table;      // index 67 archive
    Mode mode;
    int index;      // where the definitions are
    int arg;        // kConfig / kVar: archive; kSplit: id bits per archive
    int domain;     // kVar: varbit domain of this table
};

constexpr Spec kSpecs[] = {
    { "varp",           61, kVar,      2, 60, 0 },
    { "varnpc",         59, kVar,      2, 61, 1 },
    { "varobj",         60, kVar,      2, 65, 5 },
    { "varclan",        55, kVar,      2, 66, 6 },
    { "varclansetting", 56, kVar,      2, 67, 7 },
    { "varc",           57, kConfig,   2, 62, -1 },
    { "vargroup",       80, kConfig,   2, 75, -1 },
    { "npc",            35, kSplit,   18, 7, -1 },
    { "obj",            36, kSplit,   19, 8, -1 },
    { "loc",            28, kSplit,   16, 8, -1 },
    { "enum",           16, kSplit,   17, 8, -1 },
    { "struct",         50, kSplit,   22, 5, -1 },
    { "seq",            44, kSplit,   20, 7, -1 },
    { "achievement",    89, kSplit,   57, 7, -1 },
    { "param",          37, kConfig,   2, 11, -1 },
    { "dbrow",          14, kConfig,   2, 41, -1 },
    { "dbtable",        15, kConfig,   2, 40, -1 },
    { "inv",            25, kConfig,   2, 5, -1 },
    { "quest",          41, kConfig,   2, 35, -1 },
    { "bas",             5, kConfig,   2, 32, -1 },
    { "cursor",         12, kConfig,   2, 33, -1 },
    { "mapelement",     29, kConfig,   2, 36, -1 },
    { "hitmark",        21, kConfig,   2, 46, -1 },
    { "headbar",        20, kConfig,   2, 72, -1 },
    { "material",       32, kConfig,  26, 0, -1 },
    { "interface",      24, kIface,    3, 0, -1 },
    { "sprite",         49, kArchive,  8, 0, -1 },
    { "model",          34, kArchive, 47, 0, -1 },
    { "sound",          64, kArchive, 14, 0, -1 },
    { "music",          69, kArchive, 40, 0, -1 },
    { "font",           90, kArchive, 58, 0, -1 },
    { "stylesheet",     92, kArchive, 60, 0, -1 },
    // No definitions to compare: named from the game's own table only.
    { "category",        9, kTableOnly, -1, 0, -1 },
    { "uianimcurve",    96, kTableOnly, -1, 0, -1 },
    { "uianim",         97, kTableOnly, -1, 0, -1 },
};
constexpr int kComponentTable = 0;      // interface components, id = group << 16 | comp
constexpr int kVarbitArchive = 69;      // index 2

// Kinds in output order: the specs, then the two derived ones.
std::vector<std::string> KindNames() {
    std::vector<std::string> v;
    for (const auto& s : kSpecs) v.push_back(s.kind);
    v.push_back("varbit");
    v.push_back("component");
    return v;
}

// ---- the result ----

struct Names {
    std::string key, source, root, error;
    bool complete = true;
    std::string pool;
    struct K { std::string kind; std::vector<std::pair<int, std::uint32_t>> rows; KindStat st; };
    std::vector<K> kinds;
    std::unordered_map<std::string, int> at;
    std::vector<std::string> skipped;

    Names() {
        for (const auto& k : KindNames()) { at[k] = (int)kinds.size(); kinds.push_back(K{ k, {}, {} }); kinds.back().st.kind = k; }
    }
    K* Kind(const std::string& k) { auto it = at.find(k); return it == at.end() ? nullptr : &kinds[it->second]; }
    const K* Kind(const std::string& k) const { auto it = at.find(k); return it == at.end() ? nullptr : &kinds[it->second]; }
    void Add(K& k, int id, const char* s) {
        k.rows.push_back({ id, (std::uint32_t)pool.size() });
        pool.append(s);
        pool.push_back('\0');
    }
    void Finish() {
        for (auto& k : kinds) {
            std::sort(k.rows.begin(), k.rows.end());
            k.rows.erase(std::unique(k.rows.begin(), k.rows.end(),
                                     [](const auto& a, const auto& b) { return a.first == b.first; }), k.rows.end());
            k.st.named = (int)k.rows.size();
        }
    }
    const char* Find(const std::string& kind, int id) const {
        const K* k = Kind(kind);
        if (!k) return nullptr;
        auto it = std::lower_bound(k->rows.begin(), k->rows.end(), std::make_pair(id, 0u));
        return (it != k->rows.end() && it->first == id) ? pool.c_str() + it->second : nullptr;
    }
    int Total() const { int t = 0; for (const auto& k : kinds) t += (int)k.rows.size(); return t; }
};

// ---- the two caches ----

struct Cache {
    std::string root;
    std::map<int, std::unique_ptr<Js5>> db;
    std::map<int, Ref> refs;
    bool failed = false;   // some read hit a busy or unreadable file

    explicit Cache(std::string r) : root(std::move(r)) {}
    Js5* Db(int idx) {
        auto& p = db[idx];
        if (!p) p = std::make_unique<Js5>(root, idx);
        if (p->failed()) failed = true;
        return p->ok() ? p.get() : nullptr;
    }
    bool HasIndex(int idx) const { return HasFile(root + "/js5-" + std::to_string(idx) + ".jcache"); }
    const Ref& RefOf(int idx) {
        auto it = refs.find(idx);
        if (it != refs.end()) return it->second;
        Ref r;
        if (Js5* d = Db(idx)) {
            r = ParseRef(d->RefBlob());
            if (d->failed()) failed = true;
        }
        return refs[idx] = std::move(r);
    }
    Bytes Row(int idx, int key) {
        Js5* d = Db(idx);
        if (!d) return {};
        Bytes b = d->Row(key);
        if (d->failed()) failed = true;
        return b;
    }
};

// One id's definition in the game's cache, and how it compares with the source's.
struct Rec {
    int id = 0;
    std::uint64_t h = 0;   // of the game's bytes (or crc + lengths for whole archives)
    bool both = false;     // the source has this id
    bool same = false;     // and its definition is the same
    int dom = -1, var = -1;   // varbits: domain and base var
    std::vector<int> links;   // objs: the objs a record made only of links points at
};

// NPC definitions in the source may carry opcodes 26 and 27 (u16 each) that the game's do not.
// Collects their byte ranges up to the end marker or the first opcode it cannot size; false then.
bool NpcExtraOps(const Bytes& b, std::vector<std::pair<std::size_t, std::size_t>>& cuts) {
    Rd c{ b.data(), b.size() };
    auto str = [&] { while (c.i < c.n && c.p[c.i]) ++c.i; if (c.i >= c.n) c.bad = true; else ++c.i; };
    auto bigsmart = [&] { if (!c.Has(1)) return; c.Skip((c.p[c.i] & 0x80) ? 4 : 2); };
    auto usmart = [&]() -> int { if (!c.Has(1)) return 0; return c.p[c.i] >= 0x80 ? (int)c.U16() - 0x8000 : (int)c.U8(); };
    for (;;) {
        const std::size_t at = c.i;
        const int op = (int)c.U8();
        if (c.bad) return false;
        if (op == 0) return true;
        switch (op) {
        case 26: case 27: c.Skip(2); cuts.push_back({ at, at + 3 }); break;
        case 1: case 60: { const int k = (int)c.U8(); for (int j = 0; j < k && !c.bad; ++j) bigsmart(); break; }
        case 2: str(); break;
        case 12: case 39: case 100: case 101: case 119: case 125: case 128: case 140: case 163:
        case 165: case 168: case 180: case 183: case 184: case 253: c.Skip(1); break;
        case 40: case 41: { const int k = (int)c.U8(); c.Skip((std::size_t)k * 4); break; }
        case 42: { const int k = (int)c.U8(); c.Skip((std::size_t)k); break; }
        case 44: case 45: case 95: case 97: case 98: case 103: case 123: case 127: case 137: case 142: case 252:
            c.Skip(2); break;
        case 93: case 99: case 107: case 109: case 111: case 141: case 143: case 158: case 159: case 162:
        case 169: case 178: case 182: case 185: break;
        case 106: case 118: { c.Skip(op == 118 ? 6 : 4); const int k = usmart(); c.Skip((std::size_t)(k + 1) * 2); break; }
        case 113: case 155: case 164: c.Skip(4); break;
        case 114: c.Skip(2); break;
        case 121: { const int k = (int)c.U8(); c.Skip((std::size_t)k * 4); break; }
        case 134: c.Skip(9); break;
        case 135: case 136: case 181: c.Skip(3); break;
        case 138: case 139: bigsmart(); break;
        case 102: for (int m = (int)c.U8(); m != 0 && !c.bad; m >>= 1) if (m & 1) { bigsmart(); usmart(); } break;
        case 160: { const int k = (int)c.U8(); c.Skip((std::size_t)k * 2); break; }
        case 179: for (int j = 0; j < 6; ++j) usmart(); break;
        case 186: {
            c.Skip(6);
            const int fl = (int)c.U8();
            if (fl & 1) {
                const int l1 = (int)c.U8();
                for (int i = 0; i < l1 && !c.bad; ++i) {
                    c.U8();
                    const int l2 = (int)c.U8();
                    for (int j = 0; j < l2 && !c.bad; ++j) { c.Skip(4); bigsmart(); const int k = (int)c.U8(); c.Skip((std::size_t)std::min(k, 3)); }
                }
            }
            if (fl & 2) {
                const int l1 = (int)c.U8();
                for (int i = 0; i < l1 && !c.bad; ++i) {
                    c.U8();
                    const int l2 = (int)c.U8();
                    for (int j = 0; j < l2 && !c.bad; ++j) { c.Skip(4); bigsmart(); }
                }
            }
            for (int bit : { 4, 8 }) {
                if (!(fl & bit)) continue;
                const int l1 = (int)c.U8();
                for (int i = 0; i < l1 && !c.bad; ++i) { c.U8(); const int l2 = (int)c.U8(); c.Skip((std::size_t)l2 * 8); }
            }
            if (fl & 16) { const int l1 = (int)c.U8(); c.Skip((std::size_t)l1 * 9); }
            c.Skip(2);
            break;
        }
        case 187: case 188: { c.Skip(op == 188 ? 7 : 5); const int k = usmart(); c.Skip((std::size_t)(k + 1) * 2); break; }
        case 189: { const int len = (int)c.U16(); c.Skip((std::size_t)len); break; }
        case 92: { c.Skip(3); str(); c.Skip(1); const int k = (int)c.U8(); for (int j = 0; j < k && !c.bad; ++j) bigsmart(); break; }
        case 249: {
            const int k = (int)c.U8();
            for (int j = 0; j < k && !c.bad; ++j) { const bool s = c.U8() == 1; c.Skip(3); if (s) str(); else c.Skip(4); }
            break;
        }
        default:
            if ((op >= 30 && op <= 34) || (op >= 150 && op <= 154)) { str(); break; }
            if (op >= 170 && op <= 175) { c.Skip(2); break; }
            return false;
        }
        if (c.bad) return false;
    }
}

// Same definition once those opcodes are taken out. A walk that stops early still counts: what is
// left must then equal the game's bytes exactly.
bool NpcSame(const Bytes& src, const Bytes& game) {
    if (src == game) return true;
    std::vector<std::pair<std::size_t, std::size_t>> cuts;
    NpcExtraOps(src, cuts);
    if (cuts.empty()) return false;
    Bytes s;
    s.reserve(src.size());
    std::size_t at = 0;
    for (const auto& c : cuts) { s.insert(s.end(), src.begin() + at, src.begin() + c.first); at = c.second; }
    s.insert(s.end(), src.begin() + at, src.end());
    return s == game;
}

// An obj definition made only of links to other objs (note, lent, bought: opcodes 201..208, u24 each).
void ObjLinks(const Bytes& b, std::vector<int>& to) {
    std::vector<int> v;
    std::size_t i = 0;
    while (i < b.size()) {
        const int op = b[i++];
        if (op == 0) { if (i == b.size()) to = std::move(v); return; }
        if (op < 201 || op > 208 || i + 3 > b.size()) return;
        v.push_back((b[i] << 16) | (b[i + 1] << 8) | b[i + 2]);
        i += 3;
    }
}

// Domain and base var of a varbit definition (opcode 1: u8 domain, u16 var).
void VarbitOf(const Bytes& b, Rec& r) {
    if (b.size() >= 4 && b[0] == 1) { r.dom = b[1]; r.var = (b[2] << 8) | b[3]; }
}

int MaxFile(const Arch& a) { return a.files.empty() ? -1 : a.files.back(); }

// Every file of one archive, game cache against the source. `base` is OR'd into the file id.
void CompareArchive(Cache& game, Cache& src, int index, int archive, int base, bool npc, bool varbit, bool obj, std::vector<Rec>& out) {
    const Arch* ga = game.RefOf(index).Get(archive);
    if (!ga) return;
    const Bytes graw = game.Row(index, archive);
    if (graw.empty()) return;
    const Bytes gdec = rtx::cache::Decompress(graw);
    if (gdec.empty()) return;
    const auto gf = rtx::cache::SplitArchive(gdec, ga->files, MaxFile(*ga));
    const Arch* sa = src.RefOf(index).Get(archive);
    bool rawSame = false;
    std::vector<Bytes> sf;
    if (sa) {
        const Bytes sraw = src.Row(index, archive);
        if (sraw.empty()) {        // not downloaded there: the reference tables still tell
            rawSame = sa->crc == ga->crc && sa->clen == ga->clen && sa->ulen == ga->ulen && sa->files == ga->files;
        } else {
            rawSame = sraw == graw && sa->files == ga->files;
            if (!rawSame) {
                const Bytes sdec = rtx::cache::Decompress(sraw);
                if (!sdec.empty()) sf = rtx::cache::SplitArchive(sdec, sa->files, MaxFile(*sa));
            }
        }
    }
    for (int f : ga->files) {
        if (f < 0 || (std::size_t)f >= gf.size()) continue;
        Rec r;
        r.id = base | f;
        const Bytes& gb = gf[f];
        r.h = Fnv(gb);
        if (varbit) VarbitOf(gb, r);
        if (obj) ObjLinks(gb, r.links);
        if (rawSame) {
            r.both = r.same = true;
        } else if (sa && (std::size_t)f < sf.size() && std::binary_search(sa->files.begin(), sa->files.end(), f)) {
            r.both = true;
            r.same = npc ? NpcSame(sf[f], gb) : sf[f] == gb;
        }
        out.push_back(r);
    }
}

// Whole archives (one id each): crc, lengths and file list from the two reference tables.
void CompareArchives(Cache& game, Cache& src, int index, std::vector<Rec>& out) {
    const Ref& gr = game.RefOf(index);
    const Ref& sr = src.RefOf(index);
    for (int id = 0; id < (int)gr.a.size(); ++id) {
        const Arch* g = gr.Get(id);
        if (!g) continue;
        Rec r;
        r.id = id;
        r.h = Fnv((const std::uint8_t*)&g->crc, 4, Fnv((const std::uint8_t*)&g->ulen, 4, Fnv((const std::uint8_t*)&g->clen, 4)));
        if (const Arch* s = sr.Get(id)) {
            r.both = true;
            r.same = s->crc == g->crc && s->clen == g->clen && s->ulen == g->ulen && s->files == g->files;
        }
        out.push_back(r);
    }
}

// Ids from the returned one up were handed out by each cache on its own: the lowest differing id
// above which at least a quarter of the shared ids differ. INT_MAX when there is no such range.
int CutOf(const std::vector<Rec>& recs) {
    int nc = 0, nd = 0, cut = INT_MAX;
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
        if (!it->both) continue;
        ++nc;
        if (!it->same) { ++nd; if (nd * 4 >= nc) cut = it->id; }
    }
    return cut;
}

// Which ids the source's name may go to: the same definition, and either a definition no other id
// of the kind shares or an id below the range each cache gave out on its own. Fills the counts.
std::vector<char> Identified(std::vector<Rec>& recs, KindStat& st) {
    std::sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) { return a.id < b.id; });
    std::unordered_map<std::uint64_t, int> seen;
    seen.reserve(recs.size() * 2);
    for (const auto& r : recs) ++seen[r.h];
    const int cut = CutOf(recs);
    st.live = (int)recs.size();
    st.cut = cut == INT_MAX ? -1 : cut;
    std::vector<char> ok(recs.size(), 0);
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const Rec& r = recs[i];
        if (!r.same) continue;
        ++st.same;
        ok[i] = (seen[r.h] == 1 || r.id < cut) ? 1 : 0;
    }
    return ok;
}

// From the cut up, an obj made only of links is the same obj only when the objs it links to are: there
// each cache gave the linked id to its own content, and the link record alone stays byte for byte the same.
void HoldLinks(const std::vector<Rec>& recs, std::vector<char>& ok, int cut) {
    if (cut < 0) return;
    std::unordered_map<int, std::size_t> at;
    for (std::size_t i = 0; i < recs.size(); ++i) at[recs[i].id] = i;
    for (std::size_t i = 0; i < recs.size(); ++i) {
        if (!ok[i] || recs[i].id < cut || recs[i].links.empty()) continue;
        for (int to : recs[i].links) {
            auto it = at.find(to);
            if (it == at.end() || !ok[it->second]) { ok[i] = 0; break; }
        }
    }
}

// Takes the names a kind may have; counts the rest as held.
void Take(Names& out, Names::K& k, const std::vector<Rec>& recs, const std::vector<char>& ok, const Table& t, int limit) {
    for (std::size_t i = 0; i < recs.size(); ++i) {
        if (!recs[i].same || recs[i].id >= limit) continue;
        const char* s = t.Get(recs[i].id);
        if (!s) continue;
        if (ok[i]) out.Add(k, recs[i].id, s); else ++k.st.held;
    }
}

struct Plan {
    std::string source, root, key;
};

constexpr int kDefIndexes[] = { 2, 3, 8, 14, 16, 17, 18, 19, 20, 22, 26, 40, 47, 57, 58, 60 };

// Where the names come from, and the key of the kept file.
Plan MakePlan(Cache& game, std::unique_ptr<Cache>& src) {
    Plan p;
    std::string key = "f" + std::to_string(kFileFormat);
    auto part = [&](const char* tag, Cache& c, int idx) {
        const Ref& r = c.RefOf(idx);
        key += std::string("|") + tag + std::to_string(idx) + "=" + (r.ok ? std::to_string(r.version) + "/" + Hex(r.fp) : std::string("-"));
    };
    if (game.HasIndex(kTableIndex) && game.RefOf(kTableIndex).ok) {
        p.source = "live";
        p.root = game.root;
        key += "|live";
        part("g", game, kTableIndex);
    } else {
        std::string r = Narrow(EnvW(L"RTX_NAMES_CACHE"));
        if (r.empty()) r = kSecondRoot;
        while (!r.empty() && (r.back() == '/' || r.back() == '\\')) r.pop_back();
        src = std::make_unique<Cache>(r);
        if (!src->HasIndex(kTableIndex) || !src->RefOf(kTableIndex).ok) { src.reset(); return p; }
        p.source = "transfer";
        p.root = r;
        key += "|transfer";
        part("s", *src, kTableIndex);
        for (int idx : kDefIndexes) { part("g", game, idx); part("s", *src, idx); }
    }
    p.key = key;
    return p;
}

bool LoadTables(Cache& c, std::map<int, Table>& tables, bool& complete) {
    const Ref& r = c.RefOf(kTableIndex);
    if (!r.ok) return false;
    auto load = [&](int a) {
        if (!r.Get(a)) return;
        Bytes raw = c.Row(kTableIndex, a);
        if (raw.empty()) { complete = false; return; }
        Table t;
        if (ParseTable(rtx::cache::Decompress(raw), t)) tables[a] = std::move(t);
        else complete = false;
    };
    for (const auto& s : kSpecs) load(s.table);
    load(kComponentTable);
    return true;
}

// The game cache has its own table: every name as it stands.
void BuildLive(const std::map<int, Table>& tables, Names& out) {
    for (const auto& s : kSpecs) {
        auto it = tables.find(s.table);
        if (it == tables.end()) { out.skipped.push_back(std::string(s.kind) + ": no table"); continue; }
        const Table& t = it->second;
        Names::K& k = *out.Kind(s.kind);
        const int base = s.mode == kVar ? t.VarbitBase() : INT_MAX;
        for (std::uint32_t i = 0; i < t.n; ++i) {
            const char* nm = t.NameAt(i);
            if (!nm) continue;
            const int id = t.IdAt(i);
            if (id < base) out.Add(k, id, nm);
            else if (nm[0] == '_' && nm[1]) out.Add(*out.Kind("varbit"), id - base, nm + 1);
        }
        k.st.live = (int)k.rows.size();
    }
    auto ct = tables.find(kComponentTable);
    if (ct != tables.end()) {
        Names::K& k = *out.Kind("component");
        for (std::uint32_t i = 0; i < ct->second.n; ++i)
            if (const char* nm = ct->second.NameAt(i)) out.Add(k, ct->second.IdAt(i), nm);
        k.st.live = (int)k.rows.size();
    }
    Names::K& kv = *out.Kind("varbit");
    kv.st.live = (int)kv.rows.size();
}

// Interfaces: a group whose archive is the same keeps its name and every component's. A group edited
// in place keeps its name when at least half of its components that no other component repeats are
// the same byte for byte; then only its components that are the same are named.
void BuildInterfaces(Cache& game, Cache& src, const Table& groups, const std::map<int, Table>& tables, Names& out) {
    auto ct = tables.find(kComponentTable);
    const Table* comps = ct == tables.end() ? nullptr : &ct->second;
    if (!comps) out.skipped.push_back("component: no table");
    Names::K& kg = *out.Kind("interface");
    Names::K& kc = *out.Kind("component");
    const Ref& gr = game.RefOf(3);
    const Ref& sr = src.RefOf(3);
    struct Group { int id; bool same; std::vector<Rec> comps; };
    std::vector<Group> gs;
    std::unordered_map<std::uint64_t, int> seen;
    for (int g = 0; g < (int)gr.a.size(); ++g) {
        const Arch* a = gr.Get(g);
        if (!a) continue;
        Group grp{ g, false, {} };
        if (const Arch* b = sr.Get(g)) grp.same = a->crc == b->crc && a->clen == b->clen && a->ulen == b->ulen && a->files == b->files;
        CompareArchive(game, src, 3, g, g << 16, false, false, false, grp.comps);
        for (const auto& r : grp.comps) ++seen[r.h];
        gs.push_back(std::move(grp));
    }
    kg.st.live = (int)gs.size();
    for (const auto& grp : gs) {
        int uniq = 0, usame = 0;
        for (const auto& r : grp.comps) if (seen[r.h] == 1) { ++uniq; if (r.same) ++usame; }
        const bool keep = grp.same || (usame > 0 && usame * 2 >= uniq);
        if (grp.same) ++kg.st.same;
        kc.st.live += (int)grp.comps.size();
        if (const char* gn = groups.Get(grp.id)) { if (keep) out.Add(kg, grp.id, gn); else ++kg.st.held; }
        for (const auto& r : grp.comps) {
            if (!r.same) continue;
            ++kc.st.same;
            const char* nm = comps ? comps->Get(r.id) : nullptr;
            if (!nm) continue;
            if (keep) out.Add(kc, r.id, nm); else ++kc.st.held;
        }
    }
}

// The game cache has no table: names from a second cache, id by id.
void BuildTransfer(Cache& game, Cache& src, const std::map<int, Table>& tables, Names& out) {
    struct VarDomain { const Table* t = nullptr; int base = INT_MAX; std::unordered_set<int> ok; };
    std::map<int, VarDomain> domains;
    for (const auto& s : kSpecs) {
        if (s.mode == kTableOnly) { out.skipped.push_back(std::string(s.kind) + ": no definitions to compare"); continue; }
        auto it = tables.find(s.table);
        if (it == tables.end()) { out.skipped.push_back(std::string(s.kind) + ": no table"); continue; }
        if (!game.HasIndex(s.index) || !src.HasIndex(s.index)) { out.skipped.push_back(std::string(s.kind) + ": index " + std::to_string(s.index) + " missing"); continue; }
        const Table& t = it->second;
        if (s.mode == kIface) { BuildInterfaces(game, src, t, tables, out); continue; }
        Names::K& k = *out.Kind(s.kind);
        std::vector<Rec> recs;
        if (s.mode == kConfig || s.mode == kVar) {
            CompareArchive(game, src, s.index, s.arg, 0, false, false, false, recs);
        } else if (s.mode == kSplit) {
            const Ref& gr = game.RefOf(s.index);
            for (int a = 0; a < (int)gr.a.size(); ++a)
                if (gr.Get(a)) CompareArchive(game, src, s.index, a, a << s.arg, s.index == 18, false, s.index == 19, recs);
        } else {
            CompareArchives(game, src, s.index, recs);
        }
        std::vector<char> ok = Identified(recs, k.st);
        if (s.index == 19) HoldLinks(recs, ok, k.st.cut);
        const int base = s.mode == kVar ? t.VarbitBase() : INT_MAX;
        Take(out, k, recs, ok, t, base);
        if (s.mode == kVar) {
            VarDomain& d = domains[s.domain];
            d.t = &t;
            d.base = base;
            for (std::size_t i = 0; i < recs.size(); ++i) if (ok[i]) d.ok.insert(recs[i].id);
        }
    }
    // Varbits: the same definition, on a base var that matched, named in its domain's table.
    Names::K& kv = *out.Kind("varbit");
    std::vector<Rec> recs;
    CompareArchive(game, src, 2, kVarbitArchive, 0, false, true, false, recs);
    const std::vector<char> ok = Identified(recs, kv.st);
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const Rec& r = recs[i];
        if (!r.same) continue;
        auto d = domains.find(r.dom);
        if (d == domains.end() || !d->second.t) continue;
        const char* nm = d->second.t->Get(d->second.base + r.id);
        if (!nm || nm[0] != '_' || !nm[1]) continue;
        if (ok[i] && d->second.ok.count(r.var)) out.Add(kv, r.id, nm + 1); else ++kv.st.held;
    }
}

// ---- kept file ----
// #rtxnames <format> / #key / #source <source> <root> / #skip <text> / #kind <kind> <named> <live>
// <same> <held> <cut>, then "<id>\t<name>" rows of that kind.

std::wstring KeepPath(const std::string& key) {
    const std::wstring dir = KeepDir();
    if (dir.empty()) return {};
    const std::string h = Hex(Fnv(key));
    return dir + L"\\names-" + std::wstring(h.begin(), h.end()) + L".tsv";
}

bool Save(const Names& n) {
    const std::wstring path = KeepPath(n.key);
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(KeepDir(), ec);
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << "#rtxnames\t" << kFileFormat << "\n#key\t" << n.key << "\n#source\t" << n.source << "\t" << n.root << "\n";
        for (const auto& s : n.skipped) f << "#skip\t" << s << "\n";
        for (const auto& k : n.kinds) {
            f << "#kind\t" << k.kind << "\t" << k.st.named << "\t" << k.st.live << "\t" << k.st.same << "\t" << k.st.held << "\t" << k.st.cut << "\n";
            for (const auto& r : k.rows) f << r.first << "\t" << (n.pool.c_str() + r.second) << "\n";
        }
        if (!f.good()) return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    // keep the two newest sets: another cache root may still want the other one
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> old;
    for (const auto& e : std::filesystem::directory_iterator(KeepDir(), ec)) {
        const auto name = e.path().filename().wstring();
        if (name.rfind(L"names-", 0) == 0 && e.path().extension() == L".tsv") old.push_back({ e.last_write_time(ec), e.path() });
    }
    std::sort(old.begin(), old.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 2; i < old.size(); ++i) std::filesystem::remove(old[i].second, ec);
    return true;
}

std::shared_ptr<Names> Load(const std::string& key) {
    const std::wstring path = KeepPath(key);
    if (path.empty()) return nullptr;
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return nullptr;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto n = std::make_shared<Names>();
    n->pool.reserve(text.size());
    Names::K* cur = nullptr;
    bool head = false, keyOk = false;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::vector<std::string> c;
            std::size_t p = 0;
            for (;;) { const std::size_t t = line.find('\t', p); c.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p)); if (t == std::string::npos) break; p = t + 1; }
            if (c[0] == "#rtxnames") head = c.size() > 1 && std::atoi(c[1].c_str()) == kFileFormat;
            else if (c[0] == "#key") keyOk = c.size() > 1 && c[1] == key;
            else if (c[0] == "#source" && c.size() > 2) { n->source = c[1]; n->root = c[2]; }
            else if (c[0] == "#skip" && c.size() > 1) n->skipped.push_back(c[1]);
            else if (c[0] == "#kind" && c.size() > 6) {
                cur = n->Kind(c[1]);
                if (cur) { cur->st.live = std::atoi(c[3].c_str()); cur->st.same = std::atoi(c[4].c_str()); cur->st.held = std::atoi(c[5].c_str()); cur->st.cut = std::atoi(c[6].c_str()); }
            }
            continue;
        }
        if (!head || !keyOk || !cur) return nullptr;
        const std::size_t t = line.find('\t');
        if (t == std::string::npos || t + 1 >= line.size()) return nullptr;
        n->Add(*cur, std::atoi(line.c_str()), line.c_str() + t + 1);
    }
    if (!head || !keyOk) return nullptr;
    n->key = key;
    n->Finish();
    return n;
}

// Builds or loads one set. Null when there is nothing to build from or a read failed (then `err`).
std::shared_ptr<Names> BuildOrLoad(bool rebuild, bool& fromFile, std::string& err, const std::string& haveKey) {
    fromFile = false;
    std::string gameRoot;
    try { gameRoot = rtx::cache::ResolveCacheRoot(); } catch (...) {}
    if (gameRoot.empty()) { err = "game cache not found"; return nullptr; }
    while (!gameRoot.empty() && (gameRoot.back() == '/' || gameRoot.back() == '\\')) gameRoot.pop_back();
    Cache game(gameRoot);
    std::unique_ptr<Cache> src;
    const Plan p = MakePlan(game, src);
    if (p.source.empty()) {
        auto n = std::make_shared<Names>();   // nothing to read names from: an empty, ready set
        n->key = "none";
        err.clear();
        n->Finish();
        return n;
    }
    if (game.failed || (src && src->failed)) { err = "cache busy"; return nullptr; }
    if (!rebuild && p.key == haveKey) return nullptr;            // unchanged: keep the set in hand
    if (!rebuild) {
        if (auto n = Load(p.key)) { fromFile = true; return n; }
    }
    Cache& tableCache = p.source == "live" ? game : *src;
    std::map<int, Table> tables;
    auto n = std::make_shared<Names>();
    n->key = p.key; n->source = p.source; n->root = p.root;
    if (!LoadTables(tableCache, tables, n->complete)) { err = "name table not readable"; return nullptr; }
    if (p.source == "live") BuildLive(tables, *n);
    else BuildTransfer(game, *src, tables, *n);
    if (game.failed || (src && src->failed)) { err = "cache busy"; return nullptr; }
    n->Finish();
    if (n->complete) Save(*n);
    else n->error = "name table incomplete";
    return n;
}

// ---- shared state ----

std::mutex g_mu;
std::condition_variable g_cv;
std::shared_ptr<const Names> g_names;
bool g_building = false, g_started = false, g_fromFile = false;
long long g_ms = 0;
std::string g_error;
std::uint64_t g_gen = 0;
std::chrono::steady_clock::time_point g_retryAt{};

void Worker(std::string haveKey) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    const auto t0 = std::chrono::steady_clock::now();
    std::shared_ptr<Names> n;
    bool fromFile = false;
    std::string err;
    try { n = BuildOrLoad(false, fromFile, err, haveKey); }
    catch (const std::exception& e) { err = e.what(); }
    catch (...) { err = "build failed"; }
    const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (n) { g_names = n; g_fromFile = fromFile; g_ms = ms; }
        if (n || !err.empty()) g_error = n ? n->error : err;
        if (!n && !err.empty()) g_retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        g_building = false;
    }
    g_cv.notify_all();
    if (n) rtx::log::Launcher("[names] " + (n->source.empty() ? std::string("no name table") : n->source + " " + n->root) + ": " +
                              std::to_string(n->Total()) + " names, " + (fromFile ? "loaded" : "built") + " in " + std::to_string(ms) + " ms");
    else if (!err.empty()) rtx::log::Launcher("[names] " + err);
}

// Starts a build on first use, after a failed one has waited, or when the game's cache changed.
void Kick() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_building) return;
    const std::uint64_t gen = rtx::cache::CacheGeneration();
    const auto now = std::chrono::steady_clock::now();
    const bool want = !g_started || gen != g_gen || (!g_names && now >= g_retryAt);
    if (!want) return;
    g_started = true;
    g_gen = gen;
    g_building = true;
    const std::string have = g_names ? g_names->key : std::string();
    try { std::thread(Worker, have).detach(); }
    catch (...) { g_building = false; }
}

std::shared_ptr<const Names> Current() {
    Kick();
    std::lock_guard<std::mutex> lk(g_mu);
    return g_names;
}

State StateOf(const std::shared_ptr<const Names>& n) {
    State s;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        s.building = g_building;
        s.fromFile = g_fromFile;
        s.ms = g_ms;
        s.error = g_error;
    }
    if (!n) return s;
    s.ready = true;
    s.source = n->source;
    s.root = n->root;
    s.total = n->Total();
    s.skipped = n->skipped;
    for (const auto& k : n->kinds) s.kinds.push_back(k.st);
    return s;
}

std::string StateJson(const State& s) {
    std::string o = "{\"ready\":";
    o += s.ready ? "true" : "false";
    o += ",\"building\":"; o += s.building ? "true" : "false";
    o += ",\"source\":"; JsonStr(o, s.source.c_str());
    o += ",\"root\":"; JsonStr(o, s.root.c_str());
    o += ",\"fromFile\":"; o += s.fromFile ? "true" : "false";
    o += ",\"ms\":" + std::to_string(s.ms);
    o += ",\"total\":" + std::to_string(s.total);
    o += ",\"error\":"; JsonStr(o, s.error.c_str());
    o += ",\"kinds\":[";
    for (std::size_t i = 0; i < s.kinds.size(); ++i) {
        const auto& k = s.kinds[i];
        if (i) o += ',';
        o += "{\"kind\":"; JsonStr(o, k.kind.c_str());
        o += ",\"named\":" + std::to_string(k.named) + ",\"live\":" + std::to_string(k.live) + ",\"same\":" + std::to_string(k.same) +
             ",\"held\":" + std::to_string(k.held) + ",\"cut\":" + std::to_string(k.cut) + "}";
    }
    o += "],\"skipped\":[";
    for (std::size_t i = 0; i < s.skipped.size(); ++i) { if (i) o += ','; JsonStr(o, s.skipped[i].c_str()); }
    o += "]}";
    return o;
}

std::string KindJson(const Names& n, const Names::K& k) {
    std::string o;
    o.reserve(k.rows.size() * 32 + 2);
    o += '{';
    for (std::size_t i = 0; i < k.rows.size(); ++i) {
        if (i) o += ',';
        o += '"'; o += std::to_string(k.rows[i].first); o += "\":";
        JsonStr(o, n.pool.c_str() + k.rows[i].second);
    }
    o += '}';
    return o;
}

}  // namespace

std::string Name(const std::string& kind, int id) {
    const auto n = Current();
    if (!n) return {};
    const char* s = n->Find(kind, id);
    return s ? std::string(s) : std::string();
}

State Status(int wait_ms) {
    auto n = Current();
    if (wait_ms > 0) {
        std::unique_lock<std::mutex> lk(g_mu);
        g_cv.wait_for(lk, std::chrono::milliseconds(wait_ms), [] { return !g_building; });
        n = g_names;
    }
    return StateOf(n);
}

std::string Json(const std::string& kinds) {
    const auto n = Current();
    const State s = StateOf(n);
    std::string o = StateJson(s);
    if (!n) return o;
    o.pop_back();
    o += ",\"counts\":{";
    for (std::size_t i = 0; i < n->kinds.size(); ++i) {
        if (i) o += ',';
        JsonStr(o, n->kinds[i].kind.c_str());
        o += ':' + std::to_string(n->kinds[i].rows.size());
    }
    o += '}';
    std::size_t at = 0;
    std::unordered_set<std::string> done;
    while (at <= kinds.size()) {
        std::size_t e = kinds.find(',', at);
        if (e == std::string::npos) e = kinds.size();
        std::string k = kinds.substr(at, e - at);
        k.erase(std::remove(k.begin(), k.end(), ' '), k.end());
        at = e + 1;
        const Names::K* kk = k.empty() ? nullptr : n->Kind(k);
        if (!kk || !done.insert(k).second) continue;
        o += ',';
        JsonStr(o, k.c_str());
        o += ':' + KindJson(*n, *kk);
    }
    o += '}';
    return o;
}

int Dump(const std::wstring& outdir, std::string& log, bool rebuild) {
    const auto t0 = std::chrono::steady_clock::now();
    bool fromFile = false;
    std::string err;
    std::shared_ptr<Names> n;
    try { n = BuildOrLoad(rebuild, fromFile, err, std::string()); }
    catch (const std::exception& e) { err = e.what(); }
    const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (!n) { log += "failed: " + err + "\n"; return -1; }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_names = n; g_fromFile = fromFile; g_ms = ms; g_error = n->error; g_started = true;
        g_gen = rtx::cache::CacheGeneration();
    }
    std::error_code ec;
    std::filesystem::create_directories(outdir, ec);
    for (const auto& k : n->kinds) {
        std::ofstream f((outdir + L"\\" + std::wstring(k.kind.begin(), k.kind.end()) + L".json").c_str(), std::ios::binary | std::ios::trunc);
        f << KindJson(*n, k);
    }
    const State s = StateOf(n);
    { std::ofstream f((outdir + L"\\stats.json").c_str(), std::ios::binary | std::ios::trunc); f << StateJson(s); }
    log += "source: " + (s.source.empty() ? std::string("none") : s.source + " " + s.root) + "\n";
    log += std::string(fromFile ? "loaded" : "built") + " in " + std::to_string(ms) + " ms, " + std::to_string(s.total) + " names\n";
    if (!n->error.empty()) log += "note: " + n->error + "\n";
    if (!s.source.empty()) log += "kept file: " + Narrow(KeepPath(n->key)) + "\n";
    char line[200];
    std::snprintf(line, sizeof(line), "%-16s %8s %8s %8s %6s %8s\n", "kind", "named", "live", "same", "held", "cut");
    log += line;
    for (const auto& k : s.kinds) {
        std::snprintf(line, sizeof(line), "%-16s %8d %8d %8d %6d %8d\n", k.kind.c_str(), k.named, k.live, k.same, k.held, k.cut);
        log += line;
    }
    for (const auto& sk : s.skipped) log += "skipped " + sk + "\n";
    return s.total;
}

}  // namespace rtx::names
