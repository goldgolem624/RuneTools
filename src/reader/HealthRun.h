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

// Every non-pass detail starts with one word: MOVED (the same thing at another offset, id, opcode
// or address), FORMAT (the same place, bytes read differently), GONE (not found), NEW (more than
// recorded) or UNVERIFIED (not checkable here, with what would check it). `kind` carries that word
// in lower case, or one of fallback (a compiled value in use because the client proved nothing),
// stale (a generated table older than its source), precondition (unchecked for a missing game
// state) and unrecorded (unchecked because nothing is recorded for this exe or cache).
struct Row {
    std::string group, id, key, detail;
    std::string features;   // '|' separated, each "Panel" or "Panel: detail"
    std::string exp, got, dep;
    std::string kind;       // empty on pass
    std::string need;       // what would check an unchecked row ("hover an item", "log in")
    int ok = kUnchecked;
};

// The game build a run saw, against the builds the app knows: for the impact section.
struct BuildChange {
    std::string exe = "same";     // same (validated and seen), new (first time seen), unvalidated (not in the manifest)
    std::string cache = "same";   // same, changed
    std::string from, to;         // "950-1 6a9986f8" labels; `from` empty when nothing earlier is known
    std::string archives;         // '|' separated names of the cache archives that changed
    std::string firstSeen;        // date the fingerprint was first recorded
};

class Run {
public:
    // Returns the row so the caller can set kind and need. A non-pass detail's leading word sets
    // the kind; an unchecked row whose detail has none is prefixed UNVERIFIED with kind precondition.
    Row& Add(const char* group, const std::string& id, const std::string& key, int ok, const std::string& detail,
             const std::string& features = {}, const std::string& exp = {}, const std::string& got = {},
             const std::string& dep = {});
    void Fact(const std::string& key, const std::string& value);
    void Section(const std::string& name, const std::string& json);   // top-level object, raw JSON
    void SetBuild(const BuildChange& b) { build_ = b; }
    void SetTrigger(const std::string& t) { trigger_ = t; }
    void SetComplete(bool c) { complete_ = c; }
    bool Complete() const { return complete_; }
    const std::vector<Row>& Rows() const { return rows_; }
    const Row* Find(const std::string& id) const;
    int StatusOf(const std::string& id) const;   // -1 when there is no such row
    int Count(int ok) const;
    std::string FirstFail() const;               // earliest failing row whose own dependency does not fail
    // The impact section: build change, complete, trigger, then broken / moved / format / unverified
    // lists grouped by feature (panel) with the first row's detail as the cause, and the pass count.
    std::string ImpactJson() const;
    // One line for the log: "broken: Buffs (code.shift), Quests (cache.ophist.quests); moved: 2; format: 0; unverified: 4".
    std::string ImpactLine() const;
    std::string Json(const std::string& version, long long ms) const;

private:
    std::vector<Row> rows_;
    std::map<std::string, std::string> facts_;
    std::vector<std::pair<std::string, std::string>> sections_;
    BuildChange build_;
    std::string trigger_ = "manual";
    bool complete_ = false;
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
std::string HistoryListJson();                     // [{"name","pass","fail","warn","unchecked","flavour","version","complete","trigger"}], newest first
std::string HistoryRead(const std::string& name);  // the run's JSON, empty when absent
// The run to compare history run `skipName` with, among the runs of `flavour` before it: the newest
// complete run without failures ("last clean"), else a clean run that was not complete ("last clean,
// incomplete"); else the newest on another build fingerprint, exe stamp or cache revisions
// ("previous build"); else the newest with no more failures ("previous run"); else the newest
// ("earlier run"). `against` gets which one, empty when there is none.
std::string LastGood(const std::string& flavour, const std::string& skipName, std::string* against = nullptr);

// What changed between two runs: rows whose status changed, facts whose value changed. `against`
// labels what the older run is.
std::string DiffJson(const std::string& older, const std::string& newer, const std::string& against = {});
std::string DiffText(const std::string& older, const std::string& newer, const std::string& against = {});

// Grouped, human readable form of one run's JSON; the impact comes first.
std::string SummaryText(const std::string& json);

// builds.txt: one line per game fingerprint seen (version, PE stamp, flavour, cache revisions).
struct BuildState {
    bool known = false;          // this exact fingerprint was seen before
    bool exeSeen = false;        // the exe (version + stamp) was seen before
    bool reviewed = false;       // a clean complete run or "mark reviewed" since it first appeared
    std::string firstSeen;       // date
    std::string prevRevs;        // cache revisions of the previous fingerprint of this exe
    std::string prevLabel;       // build label from the old single-line file, when migrating
};
BuildState BuildsLookup(const std::string& version, const std::string& stamp, const std::string& flavour,
                        const std::string& revs);
// `reviewed`: count the fingerprint as reviewed from the start (the build the app last ran on).
// `complete`: the run checked every group; only a complete clean run marks a build reviewed.
void BuildsRecord(const std::string& version, const std::string& stamp, const std::string& flavour,
                  const std::string& revs, int fails, bool reviewed = false, bool complete = true);
bool BuildsMarkReviewed(const std::string& version, const std::string& stamp);

}  // namespace rtx::health
