#include "Pins.h"
#include "Calibrate.h"
#include "../cache/CacheReader.h"
#include "../cache/Names.h"
#include "../cache/IdSplit.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

namespace rtx::pins {
namespace {

std::mutex g_mu;
std::wstring g_key, g_file, g_override;
std::shared_ptr<std::vector<Line>> g_lines = std::make_shared<std::vector<Line>>();
std::string g_made, g_error;

std::wstring ExeDir() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    const std::size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? std::wstring() : s.substr(0, sl);
}

bool Exists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Locate() {
    if (!g_override.empty()) return g_override;
    const std::wstring dir = ExeDir();
    const std::wstring a = dir + L"\\rtx_pins.tsv";
    if (Exists(a)) return a;
    const std::wstring b = dir + L"\\..\\..\\ui-assets\\rtx_pins.tsv";   // a build folder in the source tree
    if (Exists(b)) return b;
    return {};
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (true) {
        const std::size_t e = s.find(sep, at);
        out.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return out;
}

void LoadLocked() {
    const std::wstring path = Locate();
    const std::wstring key = path.empty() ? std::wstring() : rtx::calib::FileKey(path);
    if (!g_key.empty() && key == g_key) return;
    g_key = key; g_file = path; g_made.clear(); g_error.clear();
    auto lines = std::make_shared<std::vector<Line>>();
    g_lines = lines;
    if (path.empty()) { g_error = "file missing"; return; }
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) { g_error = "file not readable"; return; }
    std::string line; int no = 0; bool header = false;
    while (std::getline(f, line)) {
        ++no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("#pins\t", 0) == 0) header = true;
            else if (line.rfind("#made\t", 0) == 0) { g_made = line.substr(6); for (char& c : g_made) if (c == '\t') c = ' '; }
            continue;
        }
        auto c = Split(line, '\t');
        if (c.size() < 3) continue;
        Line l; l.kind = c[0]; l.key = c[1]; l.expect = c[2]; l.features = c.size() > 3 ? c[3] : std::string(); l.no = no;
        lines->push_back(std::move(l));
    }
    if (!header) g_error = "not a pins file";
}

// ---- content comparison ----
const char* const kContentKinds[] = { "varbit", "varp", "varc", "var", "enum", "enumhash", "struct", "param", "dbtable",
                                      "iface", "inv", "sprite", "script", "model", "spotanim", "archive", "item", "npc", "loc" };
bool IsContentKind(const std::string& k) {
    for (const char* c : kContentKinds) if (k == c) return true;
    return false;
}

// How a changed field counts: soft fields are content growth (a fact), warn fields still work or
// only need a table regenerated, the rest break what reads them.
enum Weight { kHard, kWarnW, kSoft };
Weight WeightOf(const std::string& kind, const std::string& field) {
    if (field == "n" && (kind == "enum" || kind == "enumhash" || kind == "struct")) return kSoft;
    if (field == "cols" && kind == "dbtable") return kSoft;
    if (field == "comps" && kind == "iface") return kSoft;
    if (kind == "iface" && field == "kids") return kWarnW;
    if (kind == "inv" && field == "size") return kWarnW;     // storages grow with content; their slots are read as they are
    if (kind == "archive") return (field == "crc") ? kWarnW : kSoft;
    if (kind == "script") return kWarnW;
    if (kind == "sprite") return kWarnW;
    if (kind == "item" || kind == "npc" || kind == "loc") return kWarnW;
    return kHard;
}

std::vector<std::pair<std::string, std::string>> Fields(const std::string& e) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& part : Split(e, ';')) {
        if (part.empty()) continue;
        const std::size_t eq = part.find('=');
        if (eq == std::string::npos) out.emplace_back(part, std::string());
        else out.emplace_back(part.substr(0, eq), part.substr(eq + 1));
    }
    return out;
}

// The selection a fingerprint is asked for, from the fields the manifest recorded: k<key> for
// enum values, p<param> for struct params, c<col> for dbtable columns.
std::string WantOf(const std::string& kind, const std::string& expect) {
    const char pre = kind == "enum" ? 'k' : kind == "struct" ? 'p' : kind == "dbtable" ? 'c' : 0;
    if (!pre) return {};
    std::string out;
    for (const auto& f : Fields(expect)) {
        if (f.first.size() < 2 || f.first[0] != pre || !(f.first[1] >= '0' && f.first[1] <= '9')) continue;
        out += (out.empty() ? "" : ",") + f.first.substr(1);
    }
    return out;
}

struct PinResult { const Line* line; int ok; std::string now, text; };

std::string Readable(const std::string& kind, const std::string& fp) {
    if (kind == "varbit") {   // dom=0;var=3240;lo=0;hi=5 -> varp 3240 [0..5]
        const auto f = Fields(fp);
        std::string dom, var, lo, hi;
        for (const auto& kv : f) { if (kv.first == "dom") dom = kv.second; else if (kv.first == "var") var = kv.second; else if (kv.first == "lo") lo = kv.second; else if (kv.first == "hi") hi = kv.second; }
        if (!var.empty()) return (dom == "0" ? "varp " : "domain " + dom + " var ") + var + " [" + lo + ".." + hi + "]";
    }
    std::string s = fp;
    for (char& c : s) if (c == ';') c = ' ';
    return s;
}

std::string Sub(const std::string& features) {
    // "Dailies: challenge slot 1|Health" -> "challenge slot 1"
    const std::string first = features.substr(0, features.find('|'));
    const std::size_t c = first.find(": ");
    return c == std::string::npos ? std::string() : first.substr(c + 2);
}

PinResult Compare(const Line& l, const std::string& now) {
    PinResult r{ &l, rtx::health::kPass, now, {} };
    const std::string sub = Sub(l.features);
    const std::string label = l.kind + " " + l.key + (sub.empty() ? "" : " (" + sub + ")");
    if (now == "?") { r.ok = rtx::health::kUnchecked; r.text = label + ": cache not readable"; return r; }
    if (now.empty()) { r.ok = rtx::health::kFail; r.text = label + " is gone (was " + Readable(l.kind, l.expect) + ")"; return r; }
    if (l.expect == "exists") return r;
    const auto want = Fields(l.expect), got = Fields(now);
    std::map<std::string, std::string> gm(got.begin(), got.end());
    std::string was, is;
    int worst = rtx::health::kPass;
    for (const auto& kv : want) {
        auto it = gm.find(kv.first);
        const std::string cur = it == gm.end() ? std::string("-") : it->second;
        if (cur == kv.second) continue;
        const Weight w = WeightOf(l.kind, kv.first);
        if (w == kSoft) continue;
        worst = (w == kHard) ? rtx::health::kFail : (worst == rtx::health::kFail ? worst : rtx::health::kWarn);
        was += (was.empty() ? "" : " ") + kv.first + "=" + kv.second;
        is  += (is.empty()  ? "" : " ") + kv.first + "=" + cur;
    }
    if (worst == rtx::health::kPass) return r;
    r.ok = worst;
    if (l.kind == "varbit") r.text = label + " was " + Readable(l.kind, l.expect) + ", now " + Readable(l.kind, now);
    else if (l.kind == "script" || l.kind == "archive") r.text = label + " changed: regenerate the table built from it";
    else r.text = label + " was " + was + ", now " + is;
    return r;
}

struct Memo { std::uint64_t gen = 0; std::wstring key; std::map<std::string, std::string> fp; };
Memo g_memo;

std::string FingerprintMemo(const std::string& kind, const std::string& key, const std::string& want) {
    const std::string mk = kind + "\t" + key + "\t" + want;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const std::uint64_t gen = rtx::cache::CacheGeneration();
        if (g_memo.gen != gen) { g_memo = Memo{}; g_memo.gen = gen; }
        auto it = g_memo.fp.find(mk);
        if (it != g_memo.fp.end()) return it->second;
    }
    const std::string v = rtx::cache::PinFingerprint(kind, key, want);
    std::lock_guard<std::mutex> lk(g_mu);
    if (v != "?") g_memo.fp[mk] = v;
    return v;
}

std::string Slug(const std::string& s) {
    std::string o;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o.push_back(c);
        else if (c >= 'A' && c <= 'Z') o.push_back((char)(c - 'A' + 'a'));
        else if (!o.empty() && o.back() != '-') o.push_back('-');
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    return o;
}

std::string Panel(const std::string& feature) {
    const std::size_t c = feature.find(": ");
    return c == std::string::npos ? feature : feature.substr(0, c);
}

}  // namespace

std::shared_ptr<const std::vector<Line>> Lines() {
    std::lock_guard<std::mutex> lk(g_mu);
    LoadLocked();
    return g_lines;
}

std::wstring File() { std::lock_guard<std::mutex> lk(g_mu); LoadLocked(); return g_file; }
std::string Made() { std::lock_guard<std::mutex> lk(g_mu); LoadLocked(); return g_made; }
std::string Error() { std::lock_guard<std::mutex> lk(g_mu); LoadLocked(); return g_error; }

void UseFile(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_override = path; g_key.clear();
}

bool Overridden() { std::lock_guard<std::mutex> lk(g_mu); return !g_override.empty(); }

std::string Field(const std::string& expect, const std::string& name) {
    for (const auto& kv : Fields(expect)) if (kv.first == name) return kv.second;
    return {};
}

std::string StampValue(const std::string& expect, std::uint32_t stamp) {
    char st[16]; std::snprintf(st, sizeof(st), "%08x", stamp);
    for (const auto& kv : Fields(expect)) if (kv.first == st) return kv.second;
    return {};
}

Line Find(const std::string& kind, const std::string& key) {
    const auto lines = Lines();
    for (const auto& l : *lines) if (l.kind == kind && l.key == key) return l;
    return Line{};
}

std::uint32_t PinnedRva(const char* name, std::uint32_t stamp) {
    const Line l = Find("rva", name);
    if (l.kind.empty()) return 0;
    std::string v = StampValue(l.expect, stamp);
    const std::size_t comma = v.find(',');
    if (comma != std::string::npos) v = v.substr(0, comma);
    return v.empty() ? 0 : (std::uint32_t)std::strtoul(v.c_str(), nullptr, 0);
}

std::string BuildLine(std::uint32_t stamp) {
    char st[16]; std::snprintf(st, sizeof(st), "%08x", stamp);
    const Line l = Find("build", st);
    return l.expect;
}

void CheckContent(rtx::health::Run& run, const std::string& runtimeJson) {
    using namespace rtx::health;
    const char* G = "Content";
    {
        const std::string err = Error();
        if (!err.empty()) { run.Add(G, "content.pins", "Content pins", kFail, err + ": every content row below is unchecked", "Update check", "rtx_pins.tsv", err); return; }
    }
    const auto linesPtr = Lines();
    const auto& lines = *linesPtr;
    struct Feat { int total = 0, pass = 0, worst = kPass, unchecked = 0, unrecorded = 0; std::vector<std::string> bad; };
    std::map<std::string, Feat> feats;
    std::set<std::string> recorded;
    int total = 0, failed = 0;
    for (const auto& l : lines) {
        if (!IsContentKind(l.kind)) continue;
        recorded.insert(l.kind + "\t" + l.key);
        const std::string now = FingerprintMemo(l.kind, l.key, WantOf(l.kind, l.expect));
        const PinResult r = Compare(l, now);
        ++total;
        if (r.ok == kFail) ++failed;
        if (r.ok != kPass && r.ok != kUnchecked) run.Fact("pin." + l.kind + "." + l.key, l.expect + " -> " + (now.empty() ? std::string("(gone)") : now));
        std::set<std::string> panels;
        for (const auto& f : Split(l.features.empty() ? std::string("Unlabelled") : l.features, '|'))
            if (!f.empty()) panels.insert(Panel(f));
        for (const auto& p : panels) {
            Feat& ft = feats[p];
            ++ft.total;
            if (r.ok == kPass) ++ft.pass;
            else if (r.ok == kUnchecked) ++ft.unchecked;
            else {
                if (r.ok == kFail || ft.worst != kFail) ft.worst = r.ok == kFail ? kFail : (ft.worst == kFail ? kFail : kWarn);
                ft.bad.push_back(r.text);
            }
        }
    }
    // ids the panels declare at run time that the manifest has no record of: existence only
    if (!runtimeJson.empty()) {
        JVal rt;
        if (ParseJson(runtimeJson, rt) && rt.t == JVal::Obj) {
            for (const auto& fe : rt.o) {
                for (const auto& kd : fe.second.o) {
                    if (!IsContentKind(kd.first)) continue;
                    for (const auto& idv : kd.second.a) {
                        const std::string key = idv.s;
                        if (key.empty() || recorded.count(kd.first + "\t" + key)) continue;
                        const std::string now = FingerprintMemo(kd.first, key, "");
                        Feat& ft = feats[Panel(fe.first)];
                        ++ft.total;
                        if (now == "?") { ++ft.unchecked; continue; }
                        if (now.empty()) { ft.worst = kFail; ft.bad.push_back(kd.first + " " + key + " is gone (not recorded)"); continue; }
                        ++ft.pass; ++ft.unrecorded;
                    }
                }
            }
        }
    }
    run.Fact("pins.content", std::to_string(total));
    run.Fact("pins.content.failed", std::to_string(failed));
    for (const auto& kv : feats) {
        const Feat& ft = kv.second;
        int ok = ft.worst;
        std::string d;
        if (ft.unchecked == ft.total) { ok = kUnchecked; d = "cache not readable"; }
        else if (ft.bad.empty()) d = std::to_string(ft.pass) + "/" + std::to_string(ft.total) + " pins match";
        else {
            d = std::to_string(ft.pass) + "/" + std::to_string(ft.total) + " match; ";
            for (std::size_t i = 0; i < ft.bad.size() && i < 6; ++i) d += (i ? "; " : "") + ft.bad[i];
            if (ft.bad.size() > 6) d += "; +" + std::to_string(ft.bad.size() - 6) + " more";
        }
        if (ft.unrecorded) {
            d += "; " + std::to_string(ft.unrecorded) + " ids not recorded: regenerate the pins file";
            if (ok == kPass) ok = kWarn;
        }
        run.Add(G, "content." + Slug(kv.first), kv.first, ok, d, kv.first, {}, {}, "content.cache");
    }
}

void CheckCacheFormat(rtx::health::Run& run) {
    using namespace rtx::health;
    const char* G = "Cache format";
    static const struct { int id; const char* name; } kNames[] = {
        { 2, "configs" }, { 3, "interfaces" }, { 5, "maps" }, { 8, "sprites" }, { 12, "scripts" }, { 14, "sound effects" },
        { 16, "locs" }, { 17, "enums" }, { 18, "npcs" }, { 19, "items" }, { 20, "sequences" }, { 21, "spot animations" },
        { 22, "structs" }, { 23, "world map" }, { 40, "music" }, { 47, "models" }, { 57, "achievements" },
    };
    auto nameOf = [&](int id) -> std::string {
        for (const auto& n : kNames) if (n.id == id) return n.name;
        return "index " + std::to_string(id);
    };
    const auto linesPtr = Lines();
    for (const auto& l : *linesPtr) {
        if (l.kind == "index") {
            const int id = std::atoi(l.key.c_str());
            const rtx::cache::IndexFacts f = rtx::cache::IndexInfo(id);
            const std::string rid = "cache.index." + l.key;
            if (!f.open) { run.Add(G, rid, "Index " + l.key + " (" + nameOf(id) + ")", kFail, "not readable", l.features, l.expect, "not open"); continue; }
            const int wantA = std::atoi(Field(l.expect, "archives").c_str());
            const int wantF = std::atoi(Field(l.expect, "maxfile").c_str());
            std::string why; int ok = kPass;
            if (f.protocol < 5 || f.protocol > 7) { ok = kFail; why += "protocol " + std::to_string(f.protocol) + "; "; }
            if (wantA > 0 && (f.archives * 10 < wantA * 8 || f.archives * 10 > wantA * 12)) { ok = kFail; why += "archives " + std::to_string(f.archives) + " (was " + std::to_string(wantA) + "); "; }
            // only a split index breaks on file ids: past its capacity, or a full archive no longer full (width changed)
            const int bits = rtx::cache::IdSplitBits(id), cap = bits > 0 ? 1 << bits : 0;
            std::string note;
            if (cap && f.maxFile >= cap) { ok = kFail; why += "largest file id " + std::to_string(f.maxFile) + ", the readers split ids by " + std::to_string(cap) + "; "; }
            else if (!Field(l.expect, "maxfile").empty() && f.maxFile != wantF) {
                if (cap && wantF == cap - 1) { ok = kFail; why += "largest file id " + std::to_string(f.maxFile) + " (was " + std::to_string(wantF) + "): the split width changed; "; }
                else note = "; largest file id " + std::to_string(wantF) + " -> " + std::to_string(f.maxFile) + (cap ? ", within the split" : f.maxFile > wantF ? ", new content" : ", ids removed");
            }
            if (f.failed > 0) { if (ok == kPass) ok = kWarn; why += std::to_string(f.failed) + " archives failed to read; "; }
            run.Fact("cache.rev." + l.key, std::to_string(f.revision));
            run.Fact("cache.archives." + l.key, std::to_string(f.archives));
            char got[160];
            std::snprintf(got, sizeof(got), "archives=%d;maxfile=%d;proto=%d", f.archives, f.maxFile, f.protocol);
            run.Add(G, rid, "Index " + l.key + " (" + nameOf(id) + ")", ok,
                    ok == kPass ? std::string(got) + note : why.substr(0, why.size() >= 2 ? why.size() - 2 : 0), l.features, l.expect, got);
        } else if (l.kind == "ceiling") {
            const int cap = std::atoi(Field(l.expect, "cap").c_str());
            const int mx = rtx::cache::MaxId(l.key);
            run.Fact("maxid." + l.key, std::to_string(mx));
            const std::string cid = "cache.ceiling." + l.key + "." + std::to_string(cap);
            if (mx < 0 || cap <= 0) { run.Add(G, cid, "Id ceiling: " + l.key, kUnchecked, mx < 0 ? "cache not readable" : "no cap recorded", l.features); continue; }
            const int ok = mx > cap ? kFail : (mx * 10 >= cap * 9 ? kWarn : kPass);
            run.Add(G, cid, "Id ceiling: " + l.key, ok,
                    "largest " + std::to_string(mx) + ", code handles up to " + std::to_string(cap) + (ok == kPass ? "" : ok == kWarn ? " (within 10 %)" : " (past it)"),
                    l.features, "<= " + std::to_string(cap), std::to_string(mx));
        }
    }
    {   // Jagex's own names for the game's ids; waits a little for a build in flight
        const rtx::names::State s = rtx::names::Status(15000);
        const std::string src = s.source.empty() ? std::string("none") : s.source;
        int ok = kPass;
        std::string d;
        if (!s.ready) { ok = kUnchecked; d = s.building ? "still building" : (s.error.empty() ? "not built" : s.error); }
        else if (s.source.empty()) { ok = kUnchecked; d = "no index 67 in the game cache or a second cache"; }
        else {
            d = (s.source == "live" ? std::string("game cache") : "matched against " + s.root) + ", " + std::to_string(s.total) + " names";
            for (const auto& k : s.kinds)
                if (k.kind == "varp" || k.kind == "varbit" || k.kind == "varc" || k.kind == "npc" || k.kind == "obj" || k.kind == "loc" || k.kind == "interface")
                    d += "; " + k.kind + " " + std::to_string(k.named);
            if (s.total == 0) ok = kWarn;
            if (!s.error.empty()) { ok = kWarn; d += "; " + s.error; }
        }
        run.Fact("names.source", src);
        run.Fact("names.total", std::to_string(s.total));
        run.Add(G, "cache.names", "Official names", ok, d, "Vars watcher|Cache Explorer", {}, "source=" + src + ";names=" + std::to_string(s.total));
    }
}

int Record(const std::wstring& idsTsv, const std::wstring& outTsv, std::string& log) {
    std::ifstream in(idsTsv.c_str(), std::ios::binary);
    if (!in) { log += "ids file not readable\n"; return -1; }
    std::ostringstream out;
    std::time_t t = std::time(nullptr); std::tm tm{}; localtime_s(&tm, &t);
    char date[16]; std::strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    std::string line; int written = 0, dropped = 0; bool header = false, made = false;
    std::vector<std::string> body;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { body.push_back(line); continue; }
        if (line[0] == '#') {
            if (line.rfind("#pins\t", 0) == 0) { header = true; body.push_back(line); continue; }
            if (line.rfind("#made\t", 0) == 0) { made = true; body.push_back(std::string("#made\t") + date + line.substr(5)); continue; }
            body.push_back(line); continue;
        }
        auto c = Split(line, '\t');
        if (c.size() < 3) { body.push_back(line); continue; }
        const std::string& kind = c[0];
        const std::string& key = c[1];
        const std::string& want = c[2];
        const std::string features = c.size() > 3 ? c[3] : std::string();
        if (IsContentKind(kind)) {
            const std::string fp = rtx::cache::PinFingerprint(kind, key, (want == "exists" || want == "-") ? std::string() : want);
            if (fp == "?") { log += "cache not readable\n"; return -1; }
            if (fp.empty()) { ++dropped; log += "not in the cache now, left out: " + kind + " " + key + " (" + features + ")\n"; continue; }
            body.push_back(kind + "\t" + key + "\t" + (want == "exists" ? std::string("exists") : fp) + "\t" + features);
            ++written;
        } else if (kind == "index") {
            const rtx::cache::IndexFacts f = rtx::cache::IndexInfo(std::atoi(key.c_str()));
            if (!f.open) { ++dropped; log += "index not readable, left out: " + key + "\n"; continue; }
            body.push_back(kind + "\t" + key + "\tarchives=" + std::to_string(f.archives) + ";maxfile=" + std::to_string(f.maxFile) +
                           ";proto=" + std::to_string(f.protocol) + "\t" + features);
            ++written;
        } else if (kind == "ceiling") {
            const int mx = rtx::cache::MaxId(key);
            body.push_back(kind + "\t" + key + "\tmax=" + std::to_string(mx) + ";cap=" + want + "\t" + features);
            ++written;
        } else {
            body.push_back(line);           // per-build and live lines are complete already
            ++written;
        }
    }
    std::ofstream o(outTsv.c_str(), std::ios::binary | std::ios::trunc);
    if (!o) { log += "output not writable\n"; return -1; }
    if (!header) o << "#pins\t2\n";
    if (!made) o << "#made\t" << date << "\n";
    for (const auto& b : body) o << b << "\n";
    log += std::to_string(written) + " records, " + std::to_string(dropped) + " ids left out\n";
    return written;
}

std::string CheckText(const std::wstring& manifest) {
    if (!manifest.empty()) UseFile(manifest);
    rtx::health::Run run;
    CheckCacheFormat(run);
    CheckContent(run, {});
    return rtx::health::SummaryText(run.Json("", 0));
}

}  // namespace rtx::pins
