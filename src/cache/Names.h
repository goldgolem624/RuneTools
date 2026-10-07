#pragma once
// Jagex's own names for content ids (vars, npcs, objs, locs, interfaces, ...), read from cache index 67 at
// run time. When the game's cache has no index 67, a second cache that has one (the beta install, or the
// cache root in RTX_NAMES_CACHE) lends a name to a game id only when that id's definition is the same in
// both caches and the definition identifies the id. Built once in the background, then kept under
// %USERPROFILE%\RuneToolsX\names keyed by both caches' reference tables.

#include <string>
#include <vector>

namespace rtx::names {

// The name of one id, or "" (none, or not ready yet). Any call here starts the build on first use.
// Kinds: varp, varbit, varc, varnpc, varobj, varclan, varclansetting, vargroup, npc, obj, loc, enum,
// struct, seq, achievement, param, dbrow, dbtable, inv, quest, bas, cursor, mapelement, hitmark, headbar,
// material, interface, component (group << 16 | comp), sprite, model, sound, music, font, stylesheet.
std::string Name(const std::string& kind, int id);

struct KindStat {
    std::string kind;
    int named = 0;   // ids that carry a name
    int live = 0;    // ids of this kind in the game's cache
    int same = 0;    // of those, ids whose definition is the same in the name source
    int held = 0;    // same and named there, but not taken: the definition does not identify the id
    int cut = -1;    // from this id up each cache gave ids out on its own (-1: no such range)
};

struct State {
    std::string source;   // "live" (the game's own table), "transfer" (matched against a second cache), "" none
    std::string root;     // cache the table came from
    bool ready = false, building = false, fromFile = false;
    long long ms = 0;     // last build or load
    int total = 0;
    std::string error;
    std::vector<KindStat> kinds;
    std::vector<std::string> skipped;   // "kind: why"
};

// Never blocks unless wait_ms > 0: then waits up to that long for a build in flight.
State Status(int wait_ms = 0);

// {"ready":..,"source":..,"total":n,"counts":{..},"<kind>":{"<id>":"NAME",..}} for a comma list of kinds.
std::string Json(const std::string& kinds);

// Builds (or loads the kept file) in the calling thread, then writes <kind>.json per kind and stats.json
// to outdir. `rebuild` ignores the kept file. Returns the number of names, -1 on failure.
int Dump(const std::wstring& outdir, std::string& log, bool rebuild);

}  // namespace rtx::names
