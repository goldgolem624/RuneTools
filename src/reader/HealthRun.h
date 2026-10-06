#pragma once
// One health check run: rows grouped in dependency order, measured facts, and the run history the
// launcher compares against. Rows keep the old {k, ok, d} fields; the rest is added beside them.
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rtx::health {

// ok: 1 pass, 0 fail, 2 warn (works, on a fallback or a moved value), 3 not checked (a
// precondition is missing: not in the world, panel closed, launcher only).
enum Status : int { kFail = 0, kPass = 1, kWarn = 2, kUnchecked = 3 };

struct Row {
    std::string group, id, key, detail;
    std::string features;   // '|' separated
    std::string exp, got, dep;
    int ok = kUnchecked;
};

class Run {
public:
    void Add(const char* group, const std::string& id, const std::string& key, int ok, const std::string& detail,
             const std::string& features = {}, const std::string& exp = {}, const std::string& got = {},
             const std::string& dep = {});
    void Fact(const std::string& key, const std::string& value);
    void Section(const std::string& name, const std::string& json);   // top-level object, raw JSON
    const std::vector<Row>& Rows() const { return rows_; }
    const Row* Find(const std::string& id) const;
    int StatusOf(const std::string& id) const;   // -1 when there is no such row
    int Count(int ok) const;
    std::string FirstFail() const;               // earliest failing row whose own dependency does not fail
    std::string Json(const std::string& version, long long ms) const;

private:
    std::vector<Row> rows_;
    std::map<std::string, std::string> facts_;
    std::vector<std::pair<std::string, std::string>> sections_;
};

std::string Escape(const std::string& s);

// A small JSON reader for the run files and arguments this module reads.
struct JVal {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    std::string s;                                   // string, number text, bool text
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
    const JVal* get(const char* k) const;
    std::string str(const char* k) const;
    long long num(const char* k, long long def = 0) const;
};
bool ParseJson(const std::string& text, JVal& out);
std::string Hex(std::uint64_t v);

// Run history: %USERPROFILE%\RuneToolsX\health\<yyyymmdd-hhmmss>-<stamp>.json, the newest 30 kept.
std::wstring HistoryDir();
std::string SaveHistory(const std::string& json, const std::string& stamp);   // the file name written
std::string HistoryListJson();                     // [{"name","pass","fail","warn","unchecked","flavour"}], newest first
std::string HistoryRead(const std::string& name);  // the run's JSON, empty when absent
// The run to compare history run `skipName` with, among the runs of `flavour` before it: the newest
// without failures ("last clean"); else the newest on another build fingerprint, exe stamp or cache
// revisions ("previous build"); else the newest with no more failures ("previous run"); else the
// newest ("earlier run"). `against` gets which one, empty when there is none.
std::string LastGood(const std::string& flavour, const std::string& skipName, std::string* against = nullptr);

// What changed between two runs: rows whose status changed, facts whose value changed. `against`
// labels what the older run is.
std::string DiffJson(const std::string& older, const std::string& newer, const std::string& against = {});
std::string DiffText(const std::string& older, const std::string& newer, const std::string& against = {});

// Grouped, human readable form of one run's JSON.
std::string SummaryText(const std::string& json);

// builds.txt: one line per game fingerprint seen (version, PE stamp, flavour, cache revisions).
struct BuildState {
    bool known = false;          // this exact fingerprint was seen before
    bool exeSeen = false;        // the exe (version + stamp) was seen before
    bool reviewed = false;       // a clean run or "mark reviewed" since it first appeared
    std::string firstSeen;       // date
    std::string prevRevs;        // cache revisions of the previous fingerprint of this exe
    std::string prevLabel;       // build label from the old single-line file, when migrating
};
BuildState BuildsLookup(const std::string& version, const std::string& stamp, const std::string& flavour,
                        const std::string& revs);
// `reviewed`: count the fingerprint as reviewed from the start (the build the app last ran on).
void BuildsRecord(const std::string& version, const std::string& stamp, const std::string& flavour,
                  const std::string& revs, int fails, bool reviewed = false);
bool BuildsMarkReviewed(const std::string& version, const std::string& stamp);

}  // namespace rtx::health
