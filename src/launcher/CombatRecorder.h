#pragma once
// Combat recorder: turns the reader's sampling passes into the combat log's events, segments them into
// fights and produces the JSONL lines of the open log. Nothing here touches a process or a file: the
// bridge feeds the samples and the store (Fights.h) writes what TakeLines hands out. One clock: every
// cycle is CLIENTCLOCK (50/s, 30 per server tick). Log layout: docs/combatlog/DESIGN.md sections 1 and 2.
#include "../reader/Reader.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtx::launcher::combat {

constexpr int kFormat = 1;
constexpr int kIdleTicks = 25;                 // a fight ends after this many ticks without a hit or a cast at a target
constexpr int kPrerollCycles = 1500;           // 30 s of state kept before the first hit
constexpr long long kRotateMs = 60LL * 60 * 1000, kRotateHardMs = 90LL * 60 * 1000;
constexpr long long kRotateBytes = 8LL * 1024 * 1024, kRotateEvents = 40000;
constexpr std::size_t kTailMax = 4000;         // events kept in memory for the live follow

// One sampling pass (CombatSample) plus what the bridge knows about the client.
struct Tick {
    bool ok = true;                            // false: the client could not be read this pass
    long long wallMs = 0;
    std::uint32_t clock = 0;
    int status = 30;                           // 30 = in the game world; anything else closes the log
    int localUid = -1;
    std::vector<rtx::reader::CombatActorSample> actors;
    std::vector<std::pair<int, int>> varps, varcs;
    bool haveTrackers = false;
    std::vector<rtx::reader::CombatTrackerCell> trackers;
};

struct AbilityDef { int structId = 0; std::string name; int icon = 0, style = 0, cdTicks = 0; int startVarc = 0, endVarc = 0; };
struct BossDef { std::string name; int kc[3] = {}; int pr[3] = {}; };   // kill-count fields {varp, lsb, msb}; total = kc + 60000 * pr
struct VarbitDef { int varbit = 0, varp = 0, lsb = 0, msb = 0; };
struct Names {                                 // every lookup optional (the test harness leaves them empty)
    std::function<std::string(const char* kind, int id)> official;    // "seq", "hitmark", "struct", "npc"
    std::function<std::string(int structId, int param)> structStr;    // 2794 ability or buff name, 8849 encounter name
    std::function<int(int structId, int param, int def)> structInt;   // 8109 buff type
};
struct Config {
    std::string character, launcher, client;
    std::vector<AbilityDef> abilities;         // every ability struct with a cooldown pair; the GCD pair as struct 0
    std::vector<BossDef> bosses;
    std::vector<VarbitDef> varbits;            // definitions of the varbits BuffVars.h names
    Names names;
    std::function<std::string()> newLogId;
};

struct ActorRow { int i = 0; std::string type; int uid = -1, id = -1; std::string name; long long first = 0; int lpMax = -1, vis = -1; bool written = false; };
struct FightRow {
    int n = 0; long long start = 0, end = -1;
    std::string kind = "kills", boss, startBy, endBy;
    std::vector<int> targets; int kills = 0, deaths = 0;
};

// The var ids a Tick must carry for the recorder's tables: the cooldown pairs, the buff timers and
// counts, the vitals, the encounter and the boss kill counts.
void WantedVars(const Config& cfg, std::vector<int>& varps, std::vector<int>& varcs);

class Recorder {
public:
    void Configure(const Config& cfg);
    void Feed(const Tick& t);                                                   // one sampling pass
    void FeedLocal(std::uint32_t clock, int anim, int targetUid, long long wallMs);   // the local player between passes
    void Close(const char* why);                                                // logout | stop | rotation | recovered
    bool TakeLines(std::vector<std::string>& out);                              // JSONL lines since the last call

    bool LogOpen() const { return logOpen_; }
    const std::string& LogId() const { return logId_; }
    bool InFight() const { return inFight_; }
    bool NeedsRotation(long long nowMs) const;
    long long LogBytes() const { return logBytes_; }
    long long LogEvents() const { return written_; }
    long long StartedAt() const { return startedAt_; }
    int FightCount() const { return (int)fights_.size() + (inFight_ ? 1 : 0); }
    const std::vector<FightRow>& Fights() const { return fights_; }
    std::string CurrentJson(std::uint64_t since) const;                        // {logId, seq, header, dict, actors, fights, events}
    std::string DiagJson() const;                                              // what the recorder holds now (the headless report)

private:
    struct Ev { int type = 0; long long c = 0; std::vector<long long> f; std::string text; int a1 = -1, a2 = -1; std::uint64_t key = 0; };
    struct ActorState {
        ActorRow row; bool present = false; long long lastSeenC = 0, leftC = 0;
        int anim = -0x7fffffff, target = -0x7fffffff, lp = -2, lpMax = -2, vis = -2;
        bool haveStats = false; int stats[7] = {}, base[7] = {};
        int bar[4] = { -1, -1, -1, -1 };
        long long lastHitC = -1, deathC = -1; bool hitByMe = false;
    };
    struct RingMemo { std::vector<std::uint64_t> keys; bool seen = false; long long at = 0; };
    struct CastVar { int structId = 0, endVarc = 0; bool known = false; int last = 0; };
    struct BuffVar { int structId = 0, kind = 0, var = 0, countKind = 0, countVar = 0; bool known = false, on = false; int last = 0; };

    static std::uint64_t ringKey(int slot, const rtx::reader::CombatHitRec& r);
    int actorIndex(const rtx::reader::CombatActorSample& a, long long c);
    int indexOfUid(int uid) const;
    int varValue(const std::unordered_map<int, int>& vp, const std::unordered_map<int, int>& vc, int kind, int var, bool& ok) const;
    void push(std::vector<Ev>& evs, int type, long long c, std::initializer_list<long long> f, int a1 = -1, int a2 = -1, const std::string& text = {});
    void route(std::vector<Ev>& evs);
    void preroll(Ev&& e);
    void openLog(long long c, long long wallMs, const std::vector<Ev>& pending);
    void openFight(long long c, const char* by, long long wallMs, const std::vector<Ev>& pending);
    void endFight(long long c, const char* by);
    void writeEvent(const Ev& e);
    void ensureActor(int i);
    void ensureDict(const Ev& e);
    void abilityDict(int structId);
    void dictLine(const char* kind, int id, const std::string& json);
    void emitLine(const std::string& line);
    void resetScene();
    std::string eventJson(const Ev& e) const;
    std::string actorJson(const ActorRow& r) const;
    std::string fightJson(const FightRow& f) const;
    std::string headerJson() const;
    std::string dictJson() const;

    Config cfg_;
    std::vector<ActorState> actors_;
    std::unordered_map<int, int> byUid_;                 // uid -> actor index (live or recently left)
    std::unordered_map<int, RingMemo> rings_;            // uid -> hitsplat records already logged
    std::unordered_map<int, CastVar> casts_;             // START varc -> family head
    std::vector<BuffVar> buffs_;
    std::unordered_map<int, int> trackers_;              // group << 16 | row << 8 | col -> value
    std::map<std::string, long long> kc_; bool kcKnown_ = false;
    int selfIdx_ = -1;
    int lp_ = -2, lpMax_ = -2, adren_ = -2, prayer_ = -0x7fffffff, enc_ = -0x7fffffff;
    int phase_ = -1;
    long long lastWallMs_ = 0, lastC_ = 0;
    // fight
    bool inFight_ = false; FightRow cur_; long long lastActionC_ = 0;
    std::vector<FightRow> fights_;
    // log
    bool logOpen_ = false; std::string logId_; long long startedAt_ = 0, c0_ = 0, wall0_ = 0;
    long long written_ = 0, logBytes_ = 0, gaps_ = 0, readFails_ = 0;
    std::deque<Ev> preroll_;
    std::unordered_map<std::uint64_t, Ev> baseline_;     // the last state row per key that left the pre-roll window
    std::vector<std::string> pending_;
    std::deque<std::pair<std::uint64_t, std::string>> tail_; std::uint64_t seq_ = 0;
    std::unordered_map<std::uint64_t, bool> dict_;
    std::map<std::string, std::map<int, std::string>> dictJson_;   // kind -> id -> object, for the live follow
};

}  // namespace rtx::launcher::combat
