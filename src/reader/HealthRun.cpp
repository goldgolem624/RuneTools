#include "HealthRun.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace rtx::health {

std::string Escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back((char)c);
    }
    return o;
}

std::string Hex(std::uint64_t v) {
    char b[24]; std::snprintf(b, sizeof(b), "0x%llX", (unsigned long long)v);
    return b;
}

void Run::Add(const char* group, const std::string& id, const std::string& key, int ok, const std::string& detail,
              const std::string& features, const std::string& exp, const std::string& got, const std::string& dep) {
    Row r;
    r.group = group ? group : "";
    r.id = id; r.key = key; r.ok = ok; r.detail = detail;
    r.features = features; r.exp = exp; r.got = got; r.dep = dep;
    rows_.push_back(std::move(r));
}

void Run::Fact(const std::string& key, const std::string& value) { facts_[key] = value; }

void Run::Section(const std::string& name, const std::string& json) {
    for (auto& s : sections_) if (s.first == name) { s.second = json; return; }
    sections_.emplace_back(name, json);
}

const Row* Run::Find(const std::string& id) const {
    for (const auto& r : rows_) if (r.id == id) return &r;
    return nullptr;
}

int Run::StatusOf(const std::string& id) const {
    const Row* r = Find(id);
    return r ? r->ok : -1;
}

int Run::Count(int ok) const {
    int n = 0;
    for (const auto& r : rows_) if (r.ok == ok) ++n;
    return n;
}

std::string Run::FirstFail() const {
    for (const auto& r : rows_) {
        if (r.ok != kFail) continue;
        if (r.dep.empty() || StatusOf(r.dep) != kFail) return r.id.empty() ? r.key : r.id;
    }
    return {};
}

std::string Run::Json(const std::string& version, long long ms) const {
    std::string o = "{\"v\":2,\"version\":\"" + Escape(version) + "\"";
    for (const auto& s : sections_) o += ",\"" + s.first + "\":" + (s.second.empty() ? std::string("{}") : s.second);
    char b[200];
    std::snprintf(b, sizeof(b), ",\"summary\":{\"pass\":%d,\"fail\":%d,\"warn\":%d,\"unchecked\":%d,\"ms\":%lld,\"firstFail\":\"",
                  Count(kPass), Count(kFail), Count(kWarn), Count(kUnchecked), ms);
    o += b; o += Escape(FirstFail()); o += "\"},\"checks\":[";
    bool first = true;
    for (const auto& r : rows_) {
        if (!first) o.push_back(',');
        first = false;
        o += "{\"g\":\"" + Escape(r.group) + "\",\"k\":\"" + Escape(r.key) + "\",\"ok\":" + std::to_string(r.ok) +
             ",\"d\":\"" + Escape(r.detail) + "\"";
        if (!r.id.empty()) o += ",\"id\":\"" + Escape(r.id) + "\"";
        if (!r.dep.empty()) o += ",\"dep\":\"" + Escape(r.dep) + "\"";
        if (!r.features.empty()) {
            o += ",\"f\":[";
            std::size_t at = 0; bool ff = true;
            while (at <= r.features.size()) {
                std::size_t e = r.features.find('|', at);
                if (e == std::string::npos) e = r.features.size();
                if (e > at) { o += (ff ? "\"" : ",\"") + Escape(r.features.substr(at, e - at)) + "\""; ff = false; }
                at = e + 1;
            }
            o += "]";
        }
        if (!r.exp.empty()) o += ",\"exp\":\"" + Escape(r.exp) + "\"";
        if (!r.got.empty()) o += ",\"got\":\"" + Escape(r.got) + "\"";
        o += "}";
    }
    o += "],\"facts\":{";
    first = true;
    for (const auto& f : facts_) {
        if (!first) o.push_back(',');
        first = false;
        o += "\"" + Escape(f.first) + "\":\"" + Escape(f.second) + "\"";
    }
    o += "}}";
    return o;
}

// ---- a small JSON reader for the run files this module writes ----
const JVal* JVal::get(const char* k) const {
    for (const auto& kv : o) if (kv.first == k) return &kv.second;
    return nullptr;
}
std::string JVal::str(const char* k) const { const JVal* v = get(k); return v ? v->s : std::string(); }
long long JVal::num(const char* k, long long def) const {
    const JVal* v = get(k); if (!v || v->s.empty()) return def;
    return std::atoll(v->s.c_str());
}

namespace {

struct JParser {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool parse(JVal& v, int depth = 0) {
        if (depth > 64) return false;
        ws();
        if (p >= e) return false;
        if (*p == '{') {
            v.t = JVal::Obj; ++p; ws();
            if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws();
                JVal k; if (!parseStr(k.s)) return false;
                ws(); if (p >= e || *p != ':') return false; ++p;
                JVal val; if (!parse(val, depth + 1)) return false;
                v.o.emplace_back(std::move(k.s), std::move(val));
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[') {
            v.t = JVal::Arr; ++p; ws();
            if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                JVal val; if (!parse(val, depth + 1)) return false;
                v.a.push_back(std::move(val));
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"') { v.t = JVal::Str; return parseStr(v.s); }
        if (e - p >= 4 && !std::strncmp(p, "true", 4)) { v.t = JVal::Bool; v.s = "true"; p += 4; return true; }
        if (e - p >= 5 && !std::strncmp(p, "false", 5)) { v.t = JVal::Bool; v.s = "false"; p += 5; return true; }
        if (e - p >= 4 && !std::strncmp(p, "null", 4)) { v.t = JVal::Null; p += 4; return true; }
        const char* s0 = p;
        while (p < e && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) ++p;
        if (p == s0) return false;
        v.t = JVal::Num; v.s.assign(s0, p);
        return true;
    }
    bool parseStr(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                switch (*p) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'u': {
                        if (e - p < 5) return false;
                        char h[5] = { p[1], p[2], p[3], p[4], 0 };
                        unsigned cp = (unsigned)std::strtoul(h, nullptr, 16);
                        if (cp < 0x80) out.push_back((char)cp);
                        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                        else { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
                        p += 4; break;
                    }
                    default: out.push_back(*p);
                }
                ++p;
            } else out.push_back(*p++);
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
};

std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::wstring UserDir() {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return {};
    return std::wstring(up) + L"\\RuneToolsX";
}

std::string Today() {
    std::time_t t = std::time(nullptr); std::tm tm{}; localtime_s(&tm, &t);
    char b[16]; std::strftime(b, sizeof(b), "%Y-%m-%d", &tm);
    return b;
}

std::string RowKey(const JVal& r) {
    std::string id = r.str("id");
    return id.empty() ? r.str("k") : id;
}

const char* StatusWord(long long ok) {
    switch (ok) { case 1: return "PASS"; case 0: return "FAIL"; case 2: return "WARN"; default: return "----"; }
}

std::string Narrow(const std::wstring& w) {   // run file names are ASCII
    std::string s; s.reserve(w.size());
    for (wchar_t c : w) s.push_back(static_cast<char>(c));
    return s;
}

bool SafeName(const std::string& n) {
    if (n.empty() || n.size() > 80) return false;
    for (char c : n) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return n.find("..") == std::string::npos;
}

}  // namespace

bool ParseJson(const std::string& text, JVal& out) {
    JParser jp{ text.data(), text.data() + text.size() };
    return jp.parse(out);
}

std::wstring HistoryDir() {
    const std::wstring u = UserDir();
    return u.empty() ? u : u + L"\\health";
}

std::string SaveHistory(const std::string& json, const std::string& stamp) {
    const std::wstring dir = HistoryDir();
    if (dir.empty()) return {};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::time_t t = std::time(nullptr); std::tm tm{}; localtime_s(&tm, &t);
    char b[32]; std::strftime(b, sizeof(b), "%Y%m%d-%H%M%S", &tm);
    std::string st;
    for (char c : stamp) if (std::isalnum((unsigned char)c)) st.push_back(c);
    const std::string name = std::string(b) + "-" + (st.empty() ? "nostamp" : st) + ".json";
    {
        std::ofstream f((dir + L"\\" + std::wstring(name.begin(), name.end())).c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return {};
        f << json;
    }
    std::vector<std::wstring> files;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec))
        if (de.path().extension() == L".json") files.push_back(de.path().filename().wstring());
    std::sort(files.begin(), files.end());
    while (files.size() > 30) {
        std::filesystem::remove(std::filesystem::path(dir) / files.front(), ec);
        files.erase(files.begin());
    }
    return name;
}

std::string HistoryListJson() {
    const std::wstring dir = HistoryDir();
    std::vector<std::wstring> files;
    std::error_code ec;
    if (!dir.empty())
        for (const auto& de : std::filesystem::directory_iterator(dir, ec))
            if (de.path().extension() == L".json") files.push_back(de.path().filename().wstring());
    std::sort(files.rbegin(), files.rend());
    std::string o = "[";
    bool first = true;
    for (const auto& f : files) {
        JVal v;
        if (!ParseJson(ReadAll(dir + L"\\" + f), v)) continue;
        const JVal* s = v.get("summary");
        const JVal* b = v.get("build");
        char line[256];
        std::snprintf(line, sizeof(line), "%s{\"name\":\"%s\",\"pass\":%lld,\"fail\":%lld,\"warn\":%lld,\"unchecked\":%lld,\"flavour\":\"%s\",\"version\":\"%s\"}",
                      first ? "" : ",", Narrow(f).c_str(),
                      s ? s->num("pass") : 0, s ? s->num("fail") : 0, s ? s->num("warn") : 0, s ? s->num("unchecked") : 0,
                      b ? Escape(b->str("flavour")).c_str() : "", Escape(v.str("version")).c_str());
        o += line; first = false;
    }
    return o + "]";
}

std::string HistoryRead(const std::string& name) {
    if (!SafeName(name)) return {};
    const std::wstring dir = HistoryDir();
    if (dir.empty()) return {};
    return ReadAll(dir + L"\\" + std::wstring(name.begin(), name.end()));
}

namespace {

// The build a run was made on: exe stamp and the cache revisions it read. Empty without a build.
std::string BuildPrint(const JVal& v) {
    const JVal* b = v.get("build");
    if (!b || b->str("stamp").empty()) return {};
    std::string s = b->str("stamp");
    if (const JVal* c = v.get("cache"))
        if (const JVal* r = c->get("rev"))
            for (const auto& kv : r->o) s += ";" + kv.first + "=" + kv.second.s;
    return s;
}

}  // namespace

std::string LastGood(const std::string& flavour, const std::string& skipName, std::string* against) {
    if (against) against->clear();
    const std::wstring dir = HistoryDir();
    std::vector<std::wstring> files;
    std::error_code ec;
    if (!dir.empty())
        for (const auto& de : std::filesystem::directory_iterator(dir, ec))
            if (de.path().extension() == L".json") files.push_back(de.path().filename().wstring());
    std::sort(files.rbegin(), files.rend());
    long long curFails = -1;
    std::string curPrint;
    {
        JVal cur;
        if (!skipName.empty() && ParseJson(HistoryRead(skipName), cur)) {
            if (const JVal* s = cur.get("summary")) curFails = s->num("fail", -1);
            curPrint = BuildPrint(cur);
        }
    }
    std::string otherBuild, notWorse, newest;
    for (const auto& f : files) {
        const std::string name = Narrow(f);
        if (name == skipName) continue;
        if (!skipName.empty() && name > skipName) continue;      // names sort by time: only runs before it
        const std::string text = ReadAll(dir + L"\\" + f);
        JVal v;
        if (!ParseJson(text, v)) continue;
        const JVal* s = v.get("summary");
        const JVal* b = v.get("build");
        if (!s) continue;
        if (!flavour.empty() && (!b || b->str("flavour") != flavour)) continue;
        const long long fails = s->num("fail", -1);
        if (fails == 0) { if (against) *against = "last clean"; return text; }
        const std::string print = BuildPrint(v);
        if (otherBuild.empty() && !curPrint.empty() && !print.empty() && print != curPrint) otherBuild = text;
        if (notWorse.empty() && curFails >= 0 && fails >= 0 && fails <= curFails) notWorse = text;
        if (newest.empty()) newest = text;
    }
    if (!otherBuild.empty()) { if (against) *against = "previous build"; return otherBuild; }
    if (!notWorse.empty()) { if (against) *against = "previous run"; return notWorse; }
    if (!newest.empty()) { if (against) *against = "earlier run"; return newest; }
    return {};
}

namespace {

struct DiffOut {
    struct RowChange { std::string id, key, group; long long was = -1, now = -1; std::string dwas, dnow; };
    std::vector<RowChange> rows;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> facts;   // key, (was, now)
};

bool Diff(const std::string& older, const std::string& newer, DiffOut& out) {
    JVal a, b;
    if (!ParseJson(older, a) || !ParseJson(newer, b)) return false;
    std::map<std::string, const JVal*> ra;
    if (const JVal* c = a.get("checks")) for (const auto& r : c->a) ra[RowKey(r)] = &r;
    std::set<std::string> seen;
    if (const JVal* c = b.get("checks")) {
        for (const auto& r : c->a) {
            const std::string k = RowKey(r);
            seen.insert(k);
            auto it = ra.find(k);
            DiffOut::RowChange ch; ch.id = k; ch.key = r.str("k"); ch.group = r.str("g");
            ch.now = r.num("ok", -1); ch.dnow = r.str("d");
            if (it == ra.end()) { out.rows.push_back(ch); continue; }
            ch.was = it->second->num("ok", -1); ch.dwas = it->second->str("d");
            if (ch.was != ch.now) out.rows.push_back(ch);
        }
    }
    for (const auto& kv : ra) {
        if (seen.count(kv.first)) continue;
        DiffOut::RowChange ch; ch.id = kv.first; ch.key = kv.second->str("k"); ch.group = kv.second->str("g");
        ch.was = kv.second->num("ok", -1); ch.dwas = kv.second->str("d");
        out.rows.push_back(ch);
    }
    std::map<std::string, std::string> fa, fb;
    if (const JVal* f = a.get("facts")) for (const auto& kv : f->o) fa[kv.first] = kv.second.s;
    if (const JVal* f = b.get("facts")) for (const auto& kv : f->o) fb[kv.first] = kv.second.s;
    for (const auto& kv : fb) {
        auto it = fa.find(kv.first);
        const std::string was = it == fa.end() ? std::string("(none)") : it->second;
        if (was != kv.second) out.facts.push_back({ kv.first, { was, kv.second } });
    }
    for (const auto& kv : fa) if (!fb.count(kv.first)) out.facts.push_back({ kv.first, { kv.second, "(none)" } });
    return true;
}

}  // namespace

std::string DiffJson(const std::string& older, const std::string& newer, const std::string& against) {
    DiffOut d;
    if (!Diff(older, newer, d)) return "{\"ok\":false}";
    std::string o = "{\"ok\":true,\"against\":\"" + Escape(against) + "\",\"rows\":[";
    bool first = true;
    for (const auto& r : d.rows) {
        o += (first ? "" : ",");
        o += "{\"id\":\"" + Escape(r.id) + "\",\"k\":\"" + Escape(r.key) + "\",\"g\":\"" + Escape(r.group) +
             "\",\"was\":" + std::to_string(r.was) + ",\"now\":" + std::to_string(r.now) +
             ",\"dwas\":\"" + Escape(r.dwas) + "\",\"dnow\":\"" + Escape(r.dnow) + "\"}";
        first = false;
    }
    o += "],\"facts\":[";
    first = true;
    for (const auto& f : d.facts) {
        o += (first ? "" : ",");
        o += "{\"k\":\"" + Escape(f.first) + "\",\"was\":\"" + Escape(f.second.first) + "\",\"now\":\"" + Escape(f.second.second) + "\"}";
        first = false;
    }
    return o + "]}";
}

std::string DiffText(const std::string& older, const std::string& newer, const std::string& against) {
    DiffOut d;
    if (!Diff(older, newer, d)) return "not a health run\n";
    std::string o;
    if (!against.empty()) o += "against: " + against + "\n";
    o += "rows changed: " + std::to_string(d.rows.size()) + "\n";
    for (const auto& r : d.rows) {
        o += "  " + std::string(r.was < 0 ? "(new)" : StatusWord(r.was)) + " -> " + (r.now < 0 ? "(gone)" : StatusWord(r.now)) +
             "  " + r.group + ": " + r.key + "\n";
        if (r.was >= 0 && !r.dwas.empty()) o += "      was: " + r.dwas + "\n";
        if (r.now >= 0 && !r.dnow.empty()) o += "      now: " + r.dnow + "\n";
    }
    o += "facts changed: " + std::to_string(d.facts.size()) + "\n";
    for (const auto& f : d.facts) o += "  " + f.first + ": " + f.second.first + " -> " + f.second.second + "\n";
    return o;
}

std::string SummaryText(const std::string& json) {
    JVal v;
    if (!ParseJson(json, v)) return "not a health run\n";
    std::string o;
    const JVal* b = v.get("build");
    const JVal* s = v.get("summary");
    o += "RuneTools health check  " + v.str("version");
    if (b) o += "  " + b->str("flavour") + " " + b->str("stamp") + (b->str("known") == "true" ? " (validated)" : " (new build)");
    o += "\n";
    if (s) {
        o += "pass " + std::to_string(s->num("pass")) + "  fail " + std::to_string(s->num("fail")) +
             "  warn " + std::to_string(s->num("warn")) + "  not checked " + std::to_string(s->num("unchecked")) +
             "  " + std::to_string(s->num("ms")) + " ms\n";
        const std::string ff = s->str("firstFail");
        if (!ff.empty()) o += "first failing link: " + ff + "\n";
    }
    if (const JVal* c = v.get("context")) {
        o += "context:";
        for (const auto& kv : c->o) o += " " + kv.first + "=" + kv.second.s;
        o += "\n";
    }
    std::string group;
    if (const JVal* checks = v.get("checks")) {
        for (const auto& r : checks->a) {
            const std::string g = r.str("g");
            if (g != group) { group = g; o += "\n== " + (g.empty() ? std::string("Other") : g) + " ==\n"; }
            o += "  " + std::string(StatusWord(r.num("ok", 3))) + "  " + r.str("k");
            const std::string d = r.str("d");
            if (!d.empty()) {
                std::string one = d;
                for (char& ch : one) if (ch == '\n') ch = ' ';
                o += "  |  " + one;
            }
            o += "\n";
            const std::string e = r.str("exp"), g2 = r.str("got");
            if ((!e.empty() || !g2.empty()) && r.num("ok", 3) != 1) o += "        expected " + e + ", found " + g2 + "\n";
            if (const JVal* f = r.get("f")) {
                if (!f->a.empty() && r.num("ok", 3) == 0) {
                    o += "        breaks:";
                    for (std::size_t i = 0; i < f->a.size(); ++i) o += (i ? ", " : " ") + f->a[i].s;
                    o += "\n";
                }
            }
        }
    }
    if (const JVal* f = v.get("facts")) {
        o += "\n== Facts ==\n";
        for (const auto& kv : f->o) o += "  " + kv.first + " = " + kv.second.s + "\n";
    }
    return o;
}

// ---- builds.txt ----
namespace {

struct BuildLine { std::string version, stamp, flavour, revs, firstSeen, verdict; bool reviewed = false; };

std::wstring BuildsPath() {
    const std::wstring u = UserDir();
    return u.empty() ? u : u + L"\\builds.txt";
}

std::vector<BuildLine> BuildsLoad() {
    std::vector<BuildLine> out;
    std::ifstream f(BuildsPath().c_str());
    std::string line;
    while (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> c;
        std::size_t at = 0;
        while (true) {
            std::size_t e = line.find('\t', at);
            c.push_back(line.substr(at, e == std::string::npos ? std::string::npos : e - at));
            if (e == std::string::npos) break;
            at = e + 1;
        }
        if (c.size() < 7) continue;
        BuildLine b{ c[0], c[1], c[2], c[3], c[4], c[5], c[6] == "1" };
        out.push_back(b);
    }
    return out;
}

void BuildsSave(const std::vector<BuildLine>& lines) {
    const std::wstring p = BuildsPath();
    if (p.empty()) return;
    const std::wstring tmp = p + L".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << "# version\tstamp\tflavour\tcache revisions\tfirst seen\tlast verdict\treviewed\n";
        for (const auto& b : lines)
            f << b.version << '\t' << b.stamp << '\t' << b.flavour << '\t' << b.revs << '\t' << b.firstSeen << '\t'
              << b.verdict << '\t' << (b.reviewed ? "1" : "0") << '\n';
    }
    MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace

BuildState BuildsLookup(const std::string& version, const std::string& stamp, const std::string& flavour,
                        const std::string& revs) {
    BuildState st;
    const auto lines = BuildsLoad();
    for (const auto& b : lines) {
        if (b.version != version || b.stamp != stamp) continue;
        st.exeSeen = true;
        if (b.flavour == flavour && b.revs == revs) {
            st.known = true; st.reviewed = b.reviewed; st.firstSeen = b.firstSeen;
        } else if (b.flavour == flavour) {
            st.prevRevs = b.revs;        // the last one listed is the most recent
            if (!st.known) st.firstSeen = b.firstSeen;
        }
    }
    if (lines.empty()) {
        const std::wstring u = UserDir();
        if (!u.empty()) {
            std::ifstream f((u + L"\\lastbuild.txt").c_str());
            std::string prev;
            if (f) std::getline(f, prev);
            while (!prev.empty() && (prev.back() == '\r' || prev.back() == ' ')) prev.pop_back();
            st.prevLabel = prev;
        }
    }
    return st;
}

void BuildsRecord(const std::string& version, const std::string& stamp, const std::string& flavour,
                  const std::string& revs, int fails, bool reviewed) {
    auto lines = BuildsLoad();
    const std::string verdict = fails == 0 ? "clean" : ("fails " + std::to_string(fails));
    for (auto& b : lines) {
        if (b.version == version && b.stamp == stamp && b.flavour == flavour && b.revs == revs) {
            b.verdict = verdict;
            if (fails == 0) b.reviewed = true;
            BuildsSave(lines);
            return;
        }
    }
    BuildLine b{ version, stamp, flavour, revs, Today(), verdict, fails == 0 || reviewed };
    lines.push_back(b);
    BuildsSave(lines);
}

bool BuildsMarkReviewed(const std::string& version, const std::string& stamp) {
    auto lines = BuildsLoad();
    bool any = false;
    for (auto& b : lines)
        if (b.version == version && b.stamp == stamp) { b.reviewed = true; any = true; }
    if (any) BuildsSave(lines);
    return any;
}

}  // namespace rtx::health
