#include "Pins.h"
#include "Calibrate.h"
#include "../cache/CacheReader.h"
#include "../cache/Names.h"
#include "../cache/IdSplit.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

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

bool IsContentKind(const std::string& k);

// Removes one "k=v" field from an expectation and returns its value, empty when absent.
std::string TakeField(std::string& expect, const std::string& name) {
    std::string rest, val;
    bool found = false;
    for (const auto& part : Split(expect, ';')) {
        if (!found && part.size() > name.size() && part.compare(0, name.size(), name) == 0 && part[name.size()] == '=') {
            val = part.substr(name.size() + 1);
            found = true;
            continue;
        }
        rest += (rest.empty() ? "" : ";") + part;
    }
    if (found) expect = rest;
    return val;
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
        if (IsContentKind(l.kind)) l.official = TakeField(l.expect, "official");
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

std::string Label(const Line& l) {
    const std::string sub = Sub(l.features);
    return l.kind + " " + l.key + (sub.empty() ? "" : " (" + sub + ")");
}

// The tool that rebuilds a shipped table from the cache, by the file the pin's feature names.
std::string Rebuilder(const std::string& features) {
    static const struct { const char* file; const char* tool; } kTools[] = {
        { "BuffVars.h", "tools/rtx_buffvars.py" }, { "rtx-gametext.js", "tools/rtx_gametext.py" }, { "panel_abilitytips.js", "tools/extract_abtips.py" },
        { "ThievingLevels.h", "tools/rtx_thieving.py" }, { "Hitmarks.h", "tools/rtx_hitmarks.py" }, { "quest_tracks.js", "tools/gen_quest_varp_doc.py" },
    };
    for (const auto& t : kTools) if (features.find(t.file) != std::string::npos) return std::string("rerun ") + t.tool;
    const std::size_t a = features.find('('), b = features.find(')');
    if (a != std::string::npos && b != std::string::npos && b > a) return "regenerate " + features.substr(a + 1, b - a - 1);
    return "regenerate the table built from it";
}

PinResult Compare(const Line& l, const std::string& now) {
    PinResult r{ &l, rtx::health::kPass, now, {} };
    const std::string label = Label(l);
    if (now == "?") { r.ok = rtx::health::kUnchecked; r.text = "UNVERIFIED: " + label + ": cache not readable"; return r; }
    if (now.empty()) { r.ok = rtx::health::kFail; r.text = "GONE: " + label + " is gone (was " + Readable(l.kind, l.expect) + ")"; return r; }
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
    if (l.kind == "varbit") r.text = "MOVED: " + label + " was " + Readable(l.kind, l.expect) + ", now " + Readable(l.kind, now);
    else if (l.kind == "script" || l.kind == "archive") r.text = "FORMAT: " + label + " changed since the table was built: " + Rebuilder(l.features);
    else r.text = "MOVED: " + label + " was " + was + ", now " + is;
    return r;
}

// ---- official names ----
// The names kind and id of a content pin; false for kinds that carry no name.
bool NameKey(const std::string& kind, const std::string& key, std::string& nk, int& id) {
    static const char* const kSame[] = { "varbit", "varp", "varc", "enum", "struct", "param", "dbtable", "inv", "sprite", "model", "npc", "loc" };
    id = std::atoi(key.c_str());
    if (kind == "enumhash") { nk = "enum"; return true; }
    if (kind == "item") { nk = "obj"; return true; }
    if (kind == "iface") {   // "group" or "group:comp"
        const std::size_t c = key.find(':');
        if (c == std::string::npos) nk = "interface";
        else { nk = "component"; id = (id << 16) | std::atoi(key.c_str() + c + 1); }
        return true;
    }
    if (kind == "var") {     // "archive:id"
        static const struct { int archive; const char* kind; } kVars[] = {
            { 60, "varp" }, { 61, "varnpc" }, { 62, "varc" }, { 65, "varobj" }, { 66, "varclan" }, { 67, "varclansetting" }, { 75, "vargroup" } };
        const std::size_t c = key.find(':');
        if (c == std::string::npos) return false;
        for (const auto& v : kVars) if (v.archive == id) { nk = v.kind; id = std::atoi(key.c_str() + c + 1); return true; }
        return false;
    }
    for (const char* s : kSame) if (kind == s) { nk = s; return true; }
    return false;
}

struct NameSource { bool on = false, live = false; std::string source; std::set<std::string> kinds; };

// Off when there is no names source or it is not ready within the wait.
NameSource NameSourceNow(int waitMs) {
    NameSource ns;
    const rtx::names::State s = rtx::names::Status(waitMs);
    ns.source = !s.ready ? std::string("not ready") : s.source.empty() ? std::string("none") : s.source;
    if (!s.ready || s.source.empty()) return ns;
    ns.on = true;
    ns.live = s.source == "live" && s.error.empty();   // only the game's own full table says an id lost its name
    for (const auto& k : s.kinds) if (k.named > 0) ns.kinds.insert(k.kind);
    return ns;
}

enum NameCheck { kNameNone, kNameOk, kNameBad, kNameWithheld };

// The pinned official name against the one the id carries now. Another name fails the pin; no name
// warns when the game's own table says so, and passes when a second cache withholds it (the
// definition differs there, which the pin's other fields check).
NameCheck CheckName(const Line& l, const std::string& now, const NameSource& ns, PinResult& r, std::string& cur) {
    if (!ns.on || l.official.empty() || now.empty() || now == "?") return kNameNone;
    std::string nk; int id = 0;
    if (!NameKey(l.kind, l.key, nk, id) || !ns.kinds.count(nk)) return kNameNone;
    cur = rtx::names::Name(nk, id);
    if (cur == l.official) return kNameOk;
    if (cur.empty() && !ns.live) return kNameWithheld;
    const int ok = cur.empty() ? rtx::health::kWarn : rtx::health::kFail;
    const std::string what = cur.empty() ? "no longer named (was " + l.official + ")" : "is now " + cur + " (was " + l.official + ")";
    if (r.ok == rtx::health::kPass) r.text = (cur.empty() ? "GONE: " : "NEW: ") + Label(l) + " " + what;
    else r.text += "; name " + what;
    if (ok == rtx::health::kFail || r.ok == rtx::health::kPass) r.ok = ok;
    return kNameBad;
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

// The word a reason list leads with: anything gone over renamed over moved.
void LeadWord(std::string& d, const std::vector<std::string>& reasons) {
    bool gone = false, renamed = false;
    for (const auto& r : reasons) { if (r.rfind("GONE", 0) == 0) gone = true; else if (r.rfind("NEW", 0) == 0) renamed = true; }
    d = std::string(gone ? "GONE: " : renamed ? "NEW: " : "MOVED: ") + d;
}

// Per-type opcode counts from the cache module's full sweep (empty until it finished), and the
// reads its silent decoders cut short on an opcode they do not know.
using OpCount = rtx::cache::CacheOpCount;
std::vector<OpCount> OpHistNow() { return rtx::cache::CacheOpHist(); }
std::vector<OpCount> UnknownOpsNow() { return rtx::cache::CacheUnknownOps(); }
// The reference table facts an index line is judged on: flags byte (-1 unread), consumed exactly
// (-1 unread), why the first archive failed.
struct IndexExtra { int flags = -1; int exact = -1; int leftover = 0; std::string firstFail; };
IndexExtra IndexExtraOf(const rtx::cache::IndexFacts& f) {
    IndexExtra x;
    x.flags = f.flags;
    x.exact = f.flags < 0 ? -1 : (f.exact ? 1 : 0);
    x.leftover = f.leftover;
    x.firstFail = f.firstFail;
    return x;
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

void CheckContent(rtx::health::Run& run, const std::string& runtimeJson, int namesWaitMs) {
    using namespace rtx::health;
    const char* G = "Content";
    {
        const std::string err = Error();
        if (!err.empty()) { run.Add(G, "content.pins", "Content pins", kFail, std::string(err == "not a pins file" ? "FORMAT: " : "GONE: ") + err + ": every content row below is unchecked", "Update check", "rtx_pins.tsv", err); return; }
    }
    struct Gen { int worst = kPass; std::vector<std::string> bad; std::set<std::string> panels; int total = 0; };
    Gen gen;
    const auto linesPtr = Lines();
    const auto& lines = *linesPtr;
    struct Feat { int total = 0, pass = 0, worst = kPass, unchecked = 0, unrecorded = 0, names = 0, namesOk = 0, withheld = 0; std::vector<std::string> bad; };
    std::map<std::string, Feat> feats;
    std::set<std::string> recorded;
    int total = 0, failed = 0, namesPinned = 0, namesOk = 0, namesWithheld = 0;
    const NameSource ns = NameSourceNow(namesWaitMs);
    for (const auto& l : lines) {
        if (!IsContentKind(l.kind)) continue;
        recorded.insert(l.kind + "\t" + l.key);
        const std::string now = FingerprintMemo(l.kind, l.key, WantOf(l.kind, l.expect));
        PinResult r = Compare(l, now);
        const int fieldsOk = r.ok;
        std::string curName;
        const NameCheck nc = CheckName(l, now, ns, r, curName);
        if (nc != kNameNone) { ++namesPinned; if (nc == kNameOk) ++namesOk; else if (nc == kNameWithheld) ++namesWithheld; }
        if (nc == kNameBad) run.Fact("pin." + l.kind + "." + l.key + ".official", l.official + " -> " + (curName.empty() ? std::string("(none)") : curName));
        ++total;
        if (r.ok == kFail) ++failed;
        if (fieldsOk != kPass && fieldsOk != kUnchecked) run.Fact("pin." + l.kind + "." + l.key, l.expect + " -> " + (now.empty() ? std::string("(gone)") : now));
        std::set<std::string> panels;
        for (const auto& f : Split(l.features.empty() ? std::string("Unlabelled") : l.features, '|'))
            if (!f.empty()) panels.insert(Panel(f));
        if (l.kind == "script" || l.kind == "archive") {   // a source of a shipped table
            ++gen.total;
            for (const auto& p : panels) gen.panels.insert(p);
            if (fieldsOk != kPass && fieldsOk != kUnchecked) { gen.worst = kFail; gen.bad.push_back(r.text); }
            else if (fieldsOk == kUnchecked && gen.worst == kPass) gen.worst = kUnchecked;
        }
        for (const auto& p : panels) {
            Feat& ft = feats[p];
            ++ft.total;
            if (nc != kNameNone) { ++ft.names; if (nc == kNameOk) ++ft.namesOk; else if (nc == kNameWithheld) ++ft.withheld; }
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
                        if (now.empty()) { ft.worst = kFail; ft.bad.push_back("GONE: " + kd.first + " " + key + " is gone (not recorded)"); continue; }
                        ++ft.pass; ++ft.unrecorded;
                    }
                }
            }
        }
    }
    run.Fact("pins.content", std::to_string(total));
    run.Fact("pins.content.failed", std::to_string(failed));
    run.Fact("pins.names.source", ns.source);
    if (ns.on) run.Fact("pins.names", std::to_string(namesOk) + "/" + std::to_string(namesPinned) + (namesWithheld ? ", " + std::to_string(namesWithheld) + " withheld" : std::string()));
    for (const auto& kv : feats) {
        const Feat& ft = kv.second;
        int ok = ft.worst;
        std::string d, nm;
        if (ft.names) {
            nm = "; " + std::to_string(ft.namesOk) + "/" + std::to_string(ft.names) + " names match";
            if (ft.withheld) nm += ", " + std::to_string(ft.withheld) + " withheld by the second cache";
        }
        if (ft.unchecked == ft.total) { ok = kUnchecked; d = "cache not readable"; }
        else if (ft.bad.empty()) d = std::to_string(ft.pass) + "/" + std::to_string(ft.total) + " pins match" + nm;
        else {
            d = std::to_string(ft.pass) + "/" + std::to_string(ft.total) + " match" + nm + "; ";
            for (std::size_t i = 0; i < ft.bad.size() && i < 6; ++i) d += (i ? "; " : "") + ft.bad[i];
            if (ft.bad.size() > 6) d += "; +" + std::to_string(ft.bad.size() - 6) + " more";
            LeadWord(d, ft.bad);
        }
        if (ft.unrecorded) {
            d += "; " + std::to_string(ft.unrecorded) + " ids not recorded: regenerate the pins file";
            if (ok == kPass) { ok = kWarn; d = "NEW: " + d; }
        }
        rtx::health::Row& row = run.Add(G, "content." + Slug(kv.first), kv.first, ok, d, kv.first, {}, {}, "content.cache");
        if (ok == kUnchecked) row.need = "an open cache";
    }
    // the tables shipped with the app (BuffVars.h, game text, ability tips, thieving levels, hitmarks)
    // against the scripts and archives they were generated from: a changed source is a break until
    // the tool runs again
    if (gen.total) {
        std::string feats;
        for (const auto& p : gen.panels) feats += (feats.empty() ? "" : "|") + p;
        std::string d;
        if (gen.worst == kUnchecked) d = "cache not readable";
        else if (gen.bad.empty()) d = std::to_string(gen.total) + " source scripts and archives unchanged since the tables were built";
        else {
            for (std::size_t i = 0; i < gen.bad.size() && i < 6; ++i) d += (i ? "; " : "") + gen.bad[i];
            if (gen.bad.size() > 6) d += "; +" + std::to_string(gen.bad.size() - 6) + " more";
            d = "FORMAT: " + std::to_string(gen.bad.size()) + " of " + std::to_string(gen.total) + " sources changed: " + d;
        }
        rtx::health::Row& row = run.Add(G, "content.generated", "Generated tables", gen.worst, d, feats, std::to_string(gen.total) + " sources unchanged",
                                        std::to_string(gen.total - (int)gen.bad.size()) + " unchanged", "content.cache");
        if (gen.worst == kUnchecked) row.need = "an open cache";
        if (gen.worst == kFail) row.kind = "stale";
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
            if (!f.open) { run.Add(G, rid, "Index " + l.key + " (" + nameOf(id) + ")", kFail, "GONE: index not readable", l.features, l.expect, "not open"); continue; }
            const IndexExtra x = IndexExtraOf(f);
            const int wantA = std::atoi(Field(l.expect, "archives").c_str());
            const int wantF = std::atoi(Field(l.expect, "maxfile").c_str());
            const std::string wantFlags = Field(l.expect, "flags");
            std::string why, word; int ok = kPass;
            auto fail = [&](const char* w, const std::string& text) { ok = kFail; if (word.empty()) word = w; why += text + "; "; };
            if (f.protocol < 5 || f.protocol > 7) fail("FORMAT", "reference table protocol " + std::to_string(f.protocol));
            if (x.exact == 0) fail("FORMAT", "reference table left " + std::to_string(x.leftover) + " bytes: layout changed");
            if (x.flags >= 0 && !wantFlags.empty() && std::atoi(wantFlags.c_str()) != x.flags) fail("FORMAT", "flags " + wantFlags + " -> " + std::to_string(x.flags));
            if (wantA > 0 && (f.archives * 10 < wantA * 8 || f.archives * 10 > wantA * 12)) fail(f.archives < wantA ? "GONE" : "NEW", "archives " + std::to_string(f.archives) + " (was " + std::to_string(wantA) + ")");
            // only a split index breaks on file ids: past its capacity, or a full archive no longer full (width changed)
            const int bits = rtx::cache::IdSplitBits(id), cap = bits > 0 ? 1 << bits : 0;
            std::string note;
            if (cap && f.maxFile >= cap) fail("FORMAT", "largest file id " + std::to_string(f.maxFile) + ", the readers split ids by " + std::to_string(cap));
            else if (!Field(l.expect, "maxfile").empty() && f.maxFile != wantF) {
                if (cap && wantF == cap - 1) fail("FORMAT", "largest file id " + std::to_string(f.maxFile) + " (was " + std::to_string(wantF) + "): the split width changed");
                else note = "; largest file id " + std::to_string(wantF) + " -> " + std::to_string(f.maxFile) + (cap ? ", within the split" : f.maxFile > wantF ? ", new content" : ", ids removed");
            }
            if (f.failed > 0) {
                if (ok == kPass) { ok = kWarn; word = "GONE"; }
                why += std::to_string(f.failed) + " archives failed to read" + (x.firstFail.empty() ? "" : " (" + x.firstFail + ")") + "; ";
            }
            run.Fact("cache.rev." + l.key, std::to_string(f.revision));
            run.Fact("cache.archives." + l.key, std::to_string(f.archives));
            if (x.flags >= 0) run.Fact("cache.flags." + l.key, std::to_string(x.flags));
            char got[200];
            std::snprintf(got, sizeof(got), "archives=%d;maxfile=%d;proto=%d", f.archives, f.maxFile, f.protocol);
            std::string gotS = got;
            if (x.flags >= 0) gotS += ";flags=" + std::to_string(x.flags);
            if (x.exact >= 0) gotS += ";exact=" + std::to_string(x.exact);
            run.Add(G, rid, "Index " + l.key + " (" + nameOf(id) + ")", ok,
                    ok == kPass ? gotS + note : word + ": " + why.substr(0, why.size() >= 2 ? why.size() - 2 : 0), l.features, l.expect, gotS);
        } else if (l.kind == "ceiling") {
            const int cap = std::atoi(Field(l.expect, "cap").c_str());
            const int mx = rtx::cache::MaxId(l.key);
            run.Fact("maxid." + l.key, std::to_string(mx));
            const std::string cid = "cache.ceiling." + l.key + "." + std::to_string(cap);
            if (mx < 0 || cap <= 0) {
                run.Add(G, cid, "Id ceiling: " + l.key, kUnchecked, mx < 0 ? "UNVERIFIED: no largest id for " + l.key + " (cache not readable, or the cache module does not count this kind)" : "UNVERIFIED: no cap recorded", l.features)
                   .kind = mx < 0 ? "precondition" : "unrecorded";
                continue;
            }
            // an id class fills over years and the cache format rows catch a wider id: a pass with a note
            // from 90 % of the cap, a warn from 99 %, a fail past it
            const int ok = mx > cap ? kFail : (mx * 100 >= cap * 99 ? kWarn : kPass);
            const bool nearCap = ok == kPass && mx * 10 >= cap * 9;
            run.Add(G, cid, "Id ceiling: " + l.key, ok,
                    std::string(ok == kPass ? "" : "NEW: ") + l.key + " ids at " + std::to_string(mx) + " of " + std::to_string(cap) + (ok == kPass ? (nearCap ? " (within 10 %; a warn from 99 %)" : "") : ok == kWarn ? " (within 1 %): the next id class is near" : " (past it): ids the code cannot hold"),
                    l.features, "<= " + std::to_string(cap), std::to_string(mx));
        }
    }
    // opcode histogram of each decoder's full sweep against the recorded baselines; the sweep is
    // started here for the command line (a health run started it with the run) and given a moment
    {
        rtx::cache::CacheFullSweepStart();
        for (int i = 0; i < 300 && !rtx::cache::CacheFullSweepReady() && rtx::cache::CacheFullSweepRunning(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const std::vector<OpCount> hist = OpHistNow();
        std::map<std::string, std::map<int, long long>> now;      // type -> op -> count
        for (const auto& e : hist) now[e.type][e.op] += e.count;
        for (const auto& e : hist) run.Fact("ophist." + e.type + "." + std::to_string(e.op), std::to_string(e.count));
        struct Base { std::map<int, long long> ops; std::string features; };
        std::map<std::string, Base> base;                        // type -> baseline
        for (const auto& l : *linesPtr) {
            if (l.kind != "ophist") continue;
            const std::size_t sl = l.key.find('/');
            if (sl == std::string::npos) continue;
            Base& b = base[l.key.substr(0, sl)];
            b.ops[std::atoi(l.key.c_str() + sl + 1)] = std::atoll(Field(l.expect, "n").c_str());
            if (b.features.empty()) b.features = l.features;
        }
        for (const auto& kv : base) {
            const std::string& type = kv.first;
            const std::string rid = "cache.ophist." + Slug(type);
            auto it = now.find(type);
            if (hist.empty() || it == now.end()) {
                run.Add(G, rid, "Opcodes: " + type, kUnchecked, hist.empty() ? "UNVERIFIED: the full sweep has not run yet (opcode counts come from it)" : "UNVERIFIED: the sweep reports no opcodes for " + type, kv.second.features)
                   .need = "the cache sweep to finish";
                continue;
            }
            int ok = kPass; std::string gone, low, extra;
            for (const auto& op : kv.second.ops) {
                const auto n = it->second.find(op.first);
                const long long c = n == it->second.end() ? 0 : n->second;
                if (op.second > 0 && c == 0) gone += (gone.empty() ? "" : ", ") + std::to_string(op.first) + " (was " + std::to_string(op.second) + ")";
                else if (op.second > 0 && c * 2 < op.second) low += (low.empty() ? "" : ", ") + std::to_string(op.first) + " " + std::to_string(c) + "/" + std::to_string(op.second);
            }
            int extraN = 0;
            for (const auto& op : it->second) {
                if (kv.second.ops.count(op.first) || op.second <= 0) continue;
                ++extraN;
                if (extra.size() < 60) extra += (extra.empty() ? "" : ", ") + std::to_string(op.first) + " on " + std::to_string(op.second) + " records";
            }
            std::string d;
            if (!gone.empty()) { ok = kFail; d = "MOVED: opcode " + gone + " gone from " + type + ": the field moved to another opcode"; }
            if (!low.empty()) { if (ok == kPass) ok = kWarn; d += (d.empty() ? "MOVED: " : "; ") + std::string("under half the baseline: ") + low; }
            if (extraN) { if (ok == kPass) ok = kWarn; d += (d.empty() ? "NEW: " : "; ") + std::to_string(extraN) + " opcodes not in the decoder's table: " + extra; }
            if (d.empty()) d = std::to_string(kv.second.ops.size()) + " baseline opcodes present, none new";
            run.Add(G, rid, "Opcodes: " + type, ok, d, kv.second.features, std::to_string(kv.second.ops.size()) + " opcodes", std::to_string(it->second.size()) + " opcodes");
        }
    }
    // reads the silent decoders cut short on an opcode they do not know
    {
        const std::vector<OpCount> unk = UnknownOpsNow();
        long long total = 0; std::string list;
        for (const auto& e : unk) {
            if (e.count <= 0) continue;
            total += e.count;
            if (list.size() < 120) list += (list.empty() ? "" : "; ") + std::to_string(e.count) + " " + e.type + " reads cut short at opcode " + std::to_string(e.op);
        }
        run.Add(G, "cache.unknown", "Unknown opcodes in silent readers", total ? kFail : kPass,
                total ? "FORMAT: " + list + " since the cache opened" : "no read stopped on an unknown opcode since the cache opened",
                "Varbit reads|Enums|Structs|Map colours|Map scenes|DB rows", "0 reads cut short", std::to_string(total));
    }
    {   // Jagex's own names for the game's ids; waits a little for a build in flight
        const rtx::names::State s = rtx::names::Status(15000);
        const std::string src = s.source.empty() ? std::string("none") : s.source;
        int ok = kPass;
        std::string d, need;
        if (!s.ready) { ok = kUnchecked; d = s.building ? "still building" : (s.error.empty() ? "not built" : s.error); need = "the names build to finish"; }
        else if (s.source.empty()) { ok = kUnchecked; d = "no index 67 in the game cache or a second cache"; need = "a cache with index 67"; }
        else {
            d = (s.source == "live" ? std::string("game cache") : "matched against " + s.root) + ", " + std::to_string(s.total) + " names";
            for (const auto& k : s.kinds)
                if (k.kind == "varp" || k.kind == "varbit" || k.kind == "varc" || k.kind == "npc" || k.kind == "obj" || k.kind == "loc" || k.kind == "interface")
                    d += "; " + k.kind + " " + std::to_string(k.named);
            if (s.total == 0) { ok = kWarn; d = "GONE: " + d; }
            if (!s.error.empty()) { ok = kWarn; d += "; " + s.error; if (d.rfind("GONE", 0) != 0) d = "FORMAT: " + d; }
        }
        run.Fact("names.source", src);
        run.Fact("names.total", std::to_string(s.total));
        run.Add(G, "cache.names", "Official names", ok, d, "Vars watcher|Cache Explorer", {}, "source=" + src + ";names=" + std::to_string(s.total)).need = need;
    }
}

int Record(const std::wstring& idsTsv, const std::wstring& outTsv, std::string& log) {
    std::ifstream in(idsTsv.c_str(), std::ios::binary);
    if (!in) { log += "ids file not readable\n"; return -1; }
    // the ophist counts come from the full sweep: start it and give it a moment
    rtx::cache::CacheFullSweepStart();
    for (int i = 0; i < 300 && !rtx::cache::CacheFullSweepReady() && rtx::cache::CacheFullSweepRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
        } else if (kind == "ophist") {
            // "<type>/<op>": the count from the full sweep now; a line whose opcode the sweep never saw is left out
            const std::size_t sl = key.find('/');
            long long n = -1;
            if (sl != std::string::npos)
                for (const auto& e : OpHistNow()) if (e.type == key.substr(0, sl) && e.op == std::atoi(key.c_str() + sl + 1)) n = e.count;
            if (n < 0) { ++dropped; log += "no count from the sweep, left out: " + key + "\n"; continue; }
            body.push_back(kind + "\t" + key + "\tn=" + std::to_string(n) + "\t" + features);
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
    CheckContent(run, {}, 15000);
    return rtx::health::SummaryText(run.Json("", 0));
}

}  // namespace rtx::pins
