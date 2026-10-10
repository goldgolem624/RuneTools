#pragma once
// Combat recorder: turns the reader's sampling passes and the companion's packets into the combat log's
// events, matches boss mechanics, segments the events into fights and produces the JSONL lines of the
// open log. Nothing here touches a process or a file: the bridge feeds the samples and the packets, and
// the store (Fights.h) writes what TakeLines hands out. One clock: every cycle is CLIENTCLOCK (50/s, 30
// per server tick). Log layout: docs/combatlog/DESIGN.md sections 1 and 2.
#include "../reader/Reader.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtx::launcher::combat {

constexpr int kFormat = 1;
constexpr int kIdleTicks = 25;                 // a fight ends after this many ticks without a hit or a cast at a target
constexpr int kPrerollCycles = 1500;           // 30 s of state kept before the first hit
constexpr long long kRotateMs = 60LL * 60 * 1000, kRotateHardMs = 90LL * 60 * 1000;
constexpr long long kRotateBytes = 8LL * 1024 * 1024, kRotateEvents = 40000;
constexpr std::size_t kTailMax = 4000;         // events kept in memory for the live follow
constexpr int kGfxReticle1 = 9104, kGfxReticle2 = 9124;   // the target reticle, on every target: never logged
constexpr long long kTileRepeatMs = 3000;      // a graphic or sound on the same tile within this is the same one

// Boss mechanics the matcher looks for: BuiltinMechanics() copies the generated table, the tests pass
// their own. kind: 1 npc_anim, 2 gfx_actor, 3 gfx_tile, 4 projectile, 5 sound, 6 hint_arrow,
// 7 var_change, 8 npc_spawn. id: seq, graphic, sound, var or NPC id (hint arrow: the NPC pointed at,
// 0 any). boss: the boss's main NPC id. domain (var_change): 1 varp, 2 varbit, 3 varc, 0 any of them.
struct MechRow { int kind = 0, id = 0, boss = 0; std::string key, label; int tactic = 0, windowMs = 0, domain = 0; };
struct MechBoss { int npc = 0; std::vector<int> phases; int encounter = 0; };
void BuiltinMechanics(std::vector<MechRow>& rows, std::vector<MechBoss>& bosses);

// One server packet the recorder uses, from the companion's event ring.
struct NetEv {
    enum Kind { Gfx = 1, Proj = 2, Sound = 3, Hint = 4, Varp = 5, Varbit = 6, Varc = 7, Script = 8 };
    int kind = 0;
    long long wallMs = 0;                      // capture time, epoch ms
    int id = -1;                               // graphic, sound or var id
    int ref = 0, index = -1;                   // the actor it is on or points at: 1 player, 2 NPC (index = its uid), 0 none
    bool tile = false; int x = 0, y = 0, plane = 0;   // its tile: a graphic or sound on a tile, a projectile's source
    bool resend = false;                       // a tile item sent again after its zone was cleared
    int value = 0, form = 0;                   // var value; projectile wire length
    int a[5] = {};                             // Script: its int arguments in signature order (id = the script)
    std::string text;                          // Script: its string argument
};
// Decodes the packets NetEv covers. Zone items are relative to the zone base the stream set last, and
// the zone base to the loaded map's base tile (SetMapBase), so records must be fed in ring order.
class NetDecoder {
public:
    static bool Wanted(int op);
    static long long FullWall(std::uint32_t low, long long near);   // the ring keeps the low 32 bits of the epoch ms
    void SetMapBase(int x, int y) { baseX_ = x; baseY_ = y; haveBase_ = true; }
    void Decode(int op, const std::uint8_t* b, std::uint32_t n, long long wallMs, std::vector<NetEv>& out);
private:
    void item(int sub, const std::uint8_t* b, std::uint32_t n, long long wallMs, std::vector<NetEv>& out);
    bool tileOf(std::uint8_t pos, NetEv& e) const;
    void zone(int x, int y, int plane) { zx_ = x; zy_ = y; zp_ = plane; zset_ = haveBase_; }
    int baseX_ = 0, baseY_ = 0; bool haveBase_ = false;
    int zx_ = 0, zy_ = 0, zp_ = 0; bool zset_ = false;
    std::unordered_map<std::uint64_t, long long> cleared_;   // zone -> wall ms of its last clear
};

// One sampling pass (CombatSample) plus what the bridge knows about the client.
struct Tick {
    bool ok = true;                            // false: the client could not be read this pass
    long long wallMs = 0;
    std::uint32_t clock = 0;
    int status = 30;                           // 30 = in the game world; anything else closes the log
    int localUid = -1;
    bool haveMapBase = false; int mapBaseX = 0, mapBaseY = 0;   // the loaded map's base tile: a new one is a new scene
    bool haveItems = false; std::vector<std::pair<int, int>> inv, equip;   // per slot (item, count), item -1 empty
    std::vector<std::array<int, 8>> equipPerks;                                // per equipment slot: perk id, rank x 4
    std::vector<int> invEof, equipEof;         // per slot: -2 no Essence of Finality, -3 unread, else its stored special index (0 none)
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
    std::function<std::string(int itemId)> itemName;                  // inventory and equipment rows
    std::function<std::string(int perkId)> perkName;                  // worn items' perks
    std::function<int(int perkId)> perkRanks;                         // 1: a single-rank perk, printed without a rank
    std::function<int(int index)> eofWeapon;                          // an Essence of Finality's stored special -> its weapon item (-1 none)
};
struct Config {
    std::string character, launcher, client;
    std::vector<AbilityDef> abilities;         // every ability struct with a cooldown pair; the GCD pair as struct 0
    std::vector<BossDef> bosses;
    std::vector<VarbitDef> varbits;            // definitions of the varbits BuffVars.h names
    std::vector<MechRow> mechRows;
    std::vector<MechBoss> mechBosses;
    Names names;
    std::function<std::string()> newLogId;
    std::function<std::string()> uploadAs;     // the upload visibility a new log takes ("" = none)
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
    // Packets since the last call; `clock` and `wallMs` are one paired read (the last pass's) for the cycles.
    // Casts: script 6570 names the exact ability; a cooldown varc change the packets do not explain is
    // written one pass later from the varcs alone.
    void FeedNet(const std::vector<NetEv>& evs, std::uint32_t clock, long long wallMs);
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
    // prev: on an item, perks or stored-special row, the fields of the state it replaced (empty = none), so a log
    // that opens after such a change still starts from what was worn and carried before it
    struct Ev { int type = 0; long long c = 0; std::vector<long long> f; std::string text; int a1 = -1, a2 = -1; std::uint64_t key = 0; int mech = -1; std::vector<long long> prev; };
    struct ActorState {
        ActorRow row; bool present = false; long long lastSeenC = 0, leftC = 0;
        int anim = -0x7fffffff, target = -0x7fffffff, lp = -2, lpMax = -2, vis = -2, animCount = -0x7fffffff;
        int tx = -1, ty = -1, tplane = -1;
        bool haveStats = false; int stats[7] = {}, base[7] = {};
        int bar[4] = { -1, -1, -1, -1 };
        long long lastHitC = -1, deathC = -1; bool hitByMe = false;
    };
    struct RingMemo { std::vector<std::uint64_t> keys; bool seen = false; long long at = 0; };
    struct CastVar { int structId = 0, endVarc = 0; bool known = false; int last = 0; };
    // One timer var; several buff structs can share it (anti-poison and poisoned, the overload kinds), so the
    // struct of a row is the one the server named for the var last (script 4252), else the table's first.
    struct BuffVar { int structId = 0, kind = 0, var = 0, countKind = 0, countVar = 0; bool known = false, on = false; int last = 0;
                     std::vector<int> group; int owner = 0, cur = 0; };
    struct QueuedBuff { int buff = 0; long long c = 0; int on = 0; long long start = -1; int end = 0, stacks = -1; long long pass = 0; };

    static std::uint64_t ringKey(int slot, const rtx::reader::CombatHitRec& r);
    int actorIndex(const rtx::reader::CombatActorSample& a, long long c);
    int indexOfUid(int uid) const;
    int varValue(const std::unordered_map<int, int>& vp, const std::unordered_map<int, int>& vc, int kind, int var, bool& ok) const;
    void push(std::vector<Ev>& evs, int type, long long c, std::initializer_list<long long> f, int a1 = -1, int a2 = -1, const std::string& text = {});
    void route(std::vector<Ev>& evs);
    void routeLate(std::vector<Ev>& evs);
    struct NetRow { int startTick, endTick; std::size_t ev; int head; long long c; std::string name; int style0; };   // style0: the last cast's style before this row
    void netCast(std::vector<Ev>& evs, const NetEv& n, long long c, std::vector<NetRow>& rows);
    void flushCasts(std::vector<Ev>& evs);
    void flushBuffs(std::vector<Ev>& evs, bool all);
    void pushBuff(std::vector<Ev>& evs, BuffVar& b, const QueuedBuff& q);
    void preroll(Ev&& e);
    int actorOfRef(int ref, int index) const;
    int actorOnTile(int x, int y, int plane) const;
    bool tileRepeat(int kind, int id, const NetEv& n);
    void updateScope();
    bool matchMech(std::vector<Ev>& evs, int kind, int id, long long c, int actor, int domain = 0, const std::unordered_set<int>* before = nullptr);
    void mechDict(const Ev& e);
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
    std::unordered_map<int, int> abilityAt_;             // struct -> index in cfg_.abilities
    // casts: varc changes waiting one pass for the packet that names them, the rows script 6570 made
    // (a varc change matching one is the same cast), and the server tick offset (cycle = tick * 30 + offset)
    struct QueuedCast { int structId = 0; long long c = 0; int ready = -1; int startVarc = 0; int src = 1; long long pass = 0; };
    struct NetCast { long long c = 0; int structId = 0, startVarc = 0; int startTick = 0, endTick = 0; };
    std::vector<QueuedCast> castQ_;
    std::vector<QueuedBuff> buffQ_;
    std::map<int, std::pair<int, int>> items_;           // container << 8 | slot -> (item, count) as last written
    std::map<int, int> eof_;                             // (container << 8) | slot -> stored special weapon: -2 no amulet there, -1 nothing stored
    bool eofInit_ = false;
    // your summoned familiar: the pouch it came from (varp 1831), the uid of the NPC that showed to be it (one
    // whose name the pouch names, seen targeting you), and the actor index last written for it (-2 not written)
    int famPouch_ = 0, famUid_ = -1, famIdx_ = -2;
    std::map<int, std::array<int, 8>> perks_;            // equipment slot -> its item's perks as last written                      // shared-var buff rows waiting for the struct the server names
    std::unordered_map<int, int> buffOfStruct_;          // buff struct -> index in buffs_
    std::deque<NetCast> netCasts_;
    std::deque<std::pair<long long, long long>> tickOffs_;   // (cycle, cycle - tick * 30) of recent 6570 records
    long long tickOff_ = 0; bool haveTickOff_ = false;
    int lastStyle_ = 0;                                  // param 2806 of the last cast script 6570 named
    std::map<int, std::vector<int>> coSent_;             // struct -> abilities sent with it for one cast (a shared cooldown)
    long long passes_ = 0, netCasts0_ = 0, varcCasts_ = 0, restores_ = 0;
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
    bool logOpen_ = false; std::string logId_, uploadAs_; long long startedAt_ = 0, c0_ = 0, wall0_ = 0;
    long long written_ = 0, logBytes_ = 0, gaps_ = 0, readFails_ = 0, reads_ = 0;
    std::deque<Ev> preroll_;
    std::unordered_map<std::uint64_t, Ev> baseline_;     // the last state row per key that left the pre-roll window
    std::vector<std::string> pending_;
    std::deque<std::pair<std::uint64_t, std::string>> tail_; std::uint64_t seq_ = 0;
    std::unordered_map<std::uint64_t, bool> dict_;
    std::map<std::string, std::map<int, std::string>> dictJson_;   // kind -> id -> object, for the live follow
    // packets and boss mechanics
    bool sceneFresh_ = true;                             // the next pass is the first sight of the scene
    bool haveMap_ = false; int mapX_ = 0, mapY_ = 0, selfX_ = -1, selfY_ = -1;   // the last pass's map base and local tile
    std::unordered_map<std::uint64_t, long long> tileSeen_;   // kind, id and tile -> last wall ms
    std::unordered_map<std::uint64_t, std::vector<int>> mechIdx_;   // kind << 32 | id -> rows
    std::unordered_map<int, std::vector<int>> bossOfNpc_, bossOfEnc_;
    std::unordered_set<int> active_;                     // bosses in scope: one of their NPCs is present or their encounter runs
    std::unordered_map<int, long long> mechLast_;        // row -> cycle of its last event
    std::unordered_map<std::uint64_t, int> varLast_;     // domain << 32 | var -> last value, for vars the rows name
    std::map<int, std::map<std::string, std::string>> mechDict_;   // boss -> key -> object
    long long netSeen_ = 0, netGfx_ = 0, netProj_ = 0, netSound_ = 0, netDup_ = 0, mechs_ = 0;
};

}  // namespace rtx::launcher::combat
