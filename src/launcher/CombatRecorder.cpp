#include "CombatRecorder.h"
#include "../reader/BuffVars.h"
#include "../reader/Hitmarks.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unordered_set>

namespace rtx::launcher::combat {
namespace {

constexpr int kUnknown = -0x7fffffff;
constexpr int kVarpLp = 13537, kVarpLpMax = 13538, kVarpAdren = 679, kVarpPrayer = 3274, kVarpEncounter = 10946;
constexpr int kGcdStart = 2091, kGcdEnd = 2092, kGcdStruct = 14881;   // the global cooldown's dummy struct

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

std::uint64_t stateKey(int type, long long a, long long b = 0) {
    return ((std::uint64_t)(std::uint32_t)type << 56) ^ ((std::uint64_t)(std::uint32_t)a << 24) ^ (std::uint64_t)(std::uint32_t)b;
}
std::uint64_t dictKey(int kind, int id) { return ((std::uint64_t)kind << 40) ^ (std::uint64_t)(std::uint32_t)id; }

long long bossTotal(const std::unordered_map<int, int>& vp, const int* t) {
    if (t[0] <= 0) return 0;
    auto it = vp.find(t[0]);
    if (it == vp.end()) return 0;
    const unsigned long long v = (unsigned long long)(std::uint32_t)it->second;
    const int width = t[2] - t[1] + 1;
    const unsigned long long mask = width >= 32 ? 0xFFFFFFFFull : ((1ull << width) - 1);
    return (long long)((v >> t[1]) & mask);
}

}  // namespace

void WantedVars(const Config& cfg, std::vector<int>& varps, std::vector<int>& varcs) {
    varps = { kVarpLp, kVarpLpMax, kVarpAdren, kVarpPrayer, kVarpEncounter };
    varcs = { kGcdStart, kGcdEnd };
    for (const auto& a : cfg.abilities) { if (a.startVarc > 0) varcs.push_back(a.startVarc); if (a.endVarc > 0) varcs.push_back(a.endVarc); }
    for (const auto& b : cfg.bosses) { if (b.kc[0] > 0) varps.push_back(b.kc[0]); if (b.pr[0] > 0) varps.push_back(b.pr[0]); }
    auto want = [&](const rtx::buffvars::Entry& e) {
        if (e.kind == 1) varcs.push_back(e.var);
        else if (e.kind == 2) varps.push_back(e.var);
        else if (e.kind == 3) for (const auto& d : cfg.varbits) if (d.varbit == e.var) { varps.push_back(d.varp); break; }
    };
    for (const auto& e : rtx::buffvars::kTimer) want(e);
    for (const auto& e : rtx::buffvars::kCount) want(e);
    auto tidy = [](std::vector<int>& v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    tidy(varps); tidy(varcs);
}

void Recorder::Configure(const Config& cfg) {
    cfg_ = cfg;
    casts_.clear(); buffs_.clear();
    casts_[kGcdStart] = CastVar{ kGcdStruct, kGcdEnd };
    for (const auto& a : cfg.abilities) {
        if (a.startVarc <= 0 || a.startVarc == kGcdStart) continue;
        auto it = casts_.find(a.startVarc);
        if (it == casts_.end()) casts_[a.startVarc] = CastVar{ a.structId, a.endVarc };
        else if (a.structId < it->second.structId) it->second.structId = a.structId;   // the family head
    }
    std::unordered_set<long long> seenVar;
    for (const auto& e : rtx::buffvars::kTimer) {
        if (!seenVar.insert(((long long)e.kind << 32) | (std::uint32_t)e.var).second) continue;   // one row per shared var
        BuffVar b; b.structId = e.structId; b.kind = e.kind; b.var = e.var;
        if (const auto* ce = rtx::buffvars::FindCount(e.structId)) { b.countKind = ce->kind; b.countVar = ce->var; }
        buffs_.push_back(b);
    }
}

std::uint64_t Recorder::ringKey(int slot, const rtx::reader::CombatHitRec& r) {
    std::uint64_t k = (std::uint64_t)(std::uint32_t)r.expiry;
    k = k * 1000003ull ^ (std::uint32_t)r.value;
    k = k * 1000003ull ^ (std::uint32_t)r.hitmark;
    k = k * 1000003ull ^ (std::uint32_t)slot;
    return k;
}

int Recorder::indexOfUid(int uid) const {
    auto it = byUid_.find(uid);
    return it == byUid_.end() ? -1 : it->second;
}

int Recorder::varValue(const std::unordered_map<int, int>& vp, const std::unordered_map<int, int>& vc, int kind, int var, bool& ok) const {
    ok = false;
    if (kind == 1) { auto it = vc.find(var); if (it == vc.end()) return 0; ok = true; return it->second; }
    if (kind == 2) { auto it = vp.find(var); if (it == vp.end()) return 0; ok = true; return it->second; }
    if (kind == 3) {
        for (const auto& d : cfg_.varbits) {
            if (d.varbit != var) continue;
            auto it = vp.find(d.varp);
            if (it == vp.end() || d.lsb < 0 || d.msb < d.lsb || d.msb > 31) return 0;
            const unsigned width = (unsigned)(d.msb - d.lsb + 1) & 31u;
            ok = true; return ((int)((unsigned)it->second >> d.lsb)) & (int)((1u << width) - 1u);
        }
    }
    return 0;
}

// The actor row for a sampled actor: the live one for its uid, or a new row when the uid is new, was
// reused for another actor, or has been gone for over five minutes.
int Recorder::actorIndex(const rtx::reader::CombatActorSample& a, long long c) {
    auto it = byUid_.find(a.uid);
    if (it != byUid_.end()) {
        ActorState& s = actors_[(std::size_t)it->second];
        const bool same = (s.row.type == (a.self ? "self" : a.type == 1 ? "npc" : "player")) && s.row.id == a.id && s.row.name == a.name;
        if (same && (s.present || c - s.leftC <= 15000)) return it->second;
    }
    ActorState s;
    s.row.i = (int)actors_.size(); s.row.type = a.self ? "self" : (a.type == 1 ? "npc" : "player");
    s.row.uid = a.uid; s.row.id = a.id; s.row.name = a.name; s.row.first = c; s.row.lpMax = a.lpMax; s.row.vis = a.vis;
    actors_.push_back(std::move(s));
    byUid_[a.uid] = (int)actors_.size() - 1;
    if (a.self) selfIdx_ = (int)actors_.size() - 1;
    return (int)actors_.size() - 1;
}

void Recorder::push(std::vector<Ev>& evs, int type, long long c, std::initializer_list<long long> f, int a1, int a2, const std::string& text) {
    Ev e; e.type = type; e.c = c; e.f.assign(f.begin(), f.end()); e.a1 = a1; e.a2 = a2; e.text = text;
    switch (type) {
    case 2: case 3: case 4: case 11: e.key = stateKey(type, a1); break;
    case 5: case 6: case 12: e.key = stateKey(type, 0); break;
    case 7: e.key = stateKey(7, e.f[0]); break;
    case 9: e.key = stateKey(9, (e.f[0] << 16) | (e.f[1] << 8) | e.f[2]); break;
    case 17: e.key = stateKey(17, a1, e.f[1]); break;
    case 18: e.key = stateKey(18, a1, e.f[1]); break;
    default: e.key = 0; break;
    }
    evs.push_back(std::move(e));
}

void Recorder::preroll(Ev&& e) {
    preroll_.push_back(std::move(e));
    while (!preroll_.empty() && preroll_.front().c < lastC_ - kPrerollCycles) {
        Ev& f = preroll_.front();
        if (f.key) baseline_[f.key] = std::move(f);
        preroll_.pop_front();
    }
}

void Recorder::route(std::vector<Ev>& evs) {
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.c < b.c; });
    for (auto& e : evs) {
        if (inFight_) writeEvent(e);
        else preroll(std::move(e));
    }
    evs.clear();
}

void Recorder::emitLine(const std::string& line) {
    logBytes_ += (long long)line.size() + 1;
    pending_.push_back(line);
}

std::string Recorder::eventJson(const Ev& e) const {
    std::string o = "[" + std::to_string(e.type) + "," + std::to_string(e.c);
    for (long long v : e.f) { o += ","; o += std::to_string(v); }
    if (!e.text.empty()) o += "," + jstr(e.text);
    o += "]";
    return o;
}

std::string Recorder::actorJson(const ActorRow& r) const {
    return "[\"actor\",{\"i\":" + std::to_string(r.i) + ",\"type\":\"" + r.type + "\",\"uid\":" + std::to_string(r.uid) +
           ",\"id\":" + std::to_string(r.id) + ",\"name\":" + jstr(r.name) + ",\"first\":" + std::to_string(r.first) +
           ",\"lpMax\":" + std::to_string(r.lpMax) + ",\"vis\":" + std::to_string(r.vis) + "}]";
}

std::string Recorder::fightJson(const FightRow& f) const {
    std::string t;
    for (std::size_t i = 0; i < f.targets.size(); ++i) { if (i) t += ","; t += std::to_string(f.targets[i]); }
    return "[\"fight\",{\"n\":" + std::to_string(f.n) + ",\"start\":" + std::to_string(f.start) + ",\"end\":" + std::to_string(f.end) +
           ",\"kind\":\"" + f.kind + "\",\"boss\":" + (f.boss.empty() ? std::string("null") : jstr(f.boss)) +
           ",\"targets\":[" + t + "],\"kills\":" + std::to_string(f.kills) + ",\"deaths\":" + std::to_string(f.deaths) +
           ",\"startBy\":\"" + f.startBy + "\",\"endBy\":\"" + f.endBy + "\"}]";
}

std::string Recorder::headerJson() const {
    return "{\"format\":" + std::to_string(kFormat) + ",\"log\":{\"id\":" + jstr(logId_) + ",\"character\":" + jstr(cfg_.character) +
           ",\"launcher\":" + jstr(cfg_.launcher) + ",\"client\":" + jstr(cfg_.client) + ",\"startedAt\":" + std::to_string(startedAt_) +
           ",\"companion\":false},\"clock\":{\"c0\":" + std::to_string(c0_) + ",\"wall0\":" + std::to_string(wall0_) +
           ",\"phase\":" + std::to_string(phase_) + ",\"tick0\":-1}}";
}

void Recorder::ensureActor(int i) {
    if (i < 0 || i >= (int)actors_.size()) return;
    ActorRow& r = actors_[(std::size_t)i].row;
    if (r.written) return;
    r.written = true;
    emitLine(actorJson(r));
}

void Recorder::dictLine(const char* kind, int id, const std::string& json) {
    dictJson_[kind][id] = json;
    emitLine("[\"dict\",\"" + std::string(kind) + "\"," + std::to_string(id) + "," + json + "]");
}

// One ability row, and one for every member of its cooldown family so the viewer can pick the member
// the cast animation names. Params: 2806 style, 2801/8884/8885 channel, 5335/8366/3740 damage over
// time, 2842 area.
void Recorder::abilityDict(int st) {
    if (st <= 0 || !dict_.emplace(dictKey(1, st), true).second) return;
    const AbilityDef* d = nullptr;
    for (const auto& a : cfg_.abilities) if (a.structId == st) { d = &a; break; }
    auto sint = [&](int p, int def) { return cfg_.names.structInt ? cfg_.names.structInt(st, p, def) : def; };
    std::string name = d ? d->name : (cfg_.names.structStr ? cfg_.names.structStr(st, 2794) : std::string());
    if (name.empty() && st == kGcdStruct) name = "Global cooldown";
    const int stat = sint(2806, d ? d->style : 0);
    const char* style = stat == 1 ? "melee" : stat == 3 ? "ranged" : stat == 4 ? "magic" : stat == 6 ? "typeless" : stat == 29 ? "necromancy" : "";
    std::string o = "{\"name\":" + jstr(name) + ",\"icon\":" + std::to_string(d ? d->icon : sint(2802, 0)) + ",\"style\":\"" + style +
                    "\",\"cd\":" + std::to_string(d ? d->cdTicks : 0) + ",\"varc\":" + std::to_string(d ? d->startVarc : 0);
    std::vector<int> fam;
    if (d && d->startVarc > 0) {
        for (const auto& a : cfg_.abilities) if (a.startVarc == d->startVarc) fam.push_back(a.structId);
        std::sort(fam.begin(), fam.end()); fam.erase(std::unique(fam.begin(), fam.end()), fam.end());
        if (fam.size() > 1) { o += ",\"family\":["; for (std::size_t i = 0; i < fam.size(); ++i) { if (i) o += ","; o += std::to_string(fam[i]); } o += "]"; }
    }
    if (sint(2801, 0)) o += ",\"channel\":[" + std::to_string(sint(8884, 1)) + "," + std::to_string(sint(8885, 1)) + "]";
    if (sint(5335, 0) || sint(8366, 0)) o += ",\"dot\":" + std::to_string(sint(3740, 0));
    if (sint(2842, 0)) o += ",\"aoe\":1";
    o += "}";
    dictLine("abilities", st, o);
    if (fam.size() > 1) for (int m : fam) abilityDict(m);
}

void Recorder::ensureDict(const Ev& e) {
    auto once = [&](int kind, long long id) { return dict_.emplace(dictKey(kind, (int)id), true).second; };
    auto official = [&](const char* kind, int id) { return cfg_.names.official ? cfg_.names.official(kind, id) : std::string(); };
    switch (e.type) {
    case 0: {
        const int hm = (int)e.f[1];
        if (!once(0, hm)) return;
        const auto* info = rtx::hitmarks::Find(hm);
        const std::string kind = info ? info->kind : "unknown";
        const bool crit = kind.find("crit") != std::string::npos;
        dictLine("hitmarks", hm, "{\"kind\":" + jstr(kind) + ",\"other\":" + ((info && info->other) ? "true" : "false") +
                 ",\"crit\":" + (crit ? "true" : "false") + ",\"name\":" + jstr(official("hitmark", hm)) + "}");
        break;
    }
    case 1: abilityDict((int)e.f[0]); break;
    case 2: {
        const int seq = (int)e.f[1];
        if (seq < 0 || !once(2, seq)) return;
        dictLine("seqs", seq, jstr(official("seq", seq)));
        break;
    }
    case 7: {
        const int st = (int)e.f[0];
        if (!once(7, st)) return;
        std::string name = cfg_.names.structStr ? cfg_.names.structStr(st, 2794) : std::string();
        const std::size_t cut = name.find('<');
        if (cut != std::string::npos) name = name.substr(0, cut);
        const int type = cfg_.names.structInt ? cfg_.names.structInt(st, 8109, -1) : -1;
        const int icon = cfg_.names.structInt ? cfg_.names.structInt(st, 2802, 0) : 0;
        dictLine("buffs", st, "{\"name\":" + jstr(name) + ",\"type\":" + std::to_string(type) + ",\"icon\":" + std::to_string(icon) + "}");
        break;
    }
    case 9: {
        const int g = (int)e.f[0];
        if (!once(9, g)) return;
        const char* name = g == 1 ? "Skills" : g == 2 ? "Combat" : g == 3 ? "Loot" : "";
        dictLine("trackers", g, "{\"name\":\"" + std::string(name) + "\"}");
        break;
    }
    case 12: {
        const int st = (int)e.f[0];
        if (st < 0 || !once(12, st)) return;
        dictLine("encounters", st, jstr(cfg_.names.structStr ? cfg_.names.structStr(st, 8849) : std::string()));
        break;
    }
    default: break;
    }
}

std::string Recorder::dictJson() const {
    static const char* kinds[] = { "abilities", "buffs", "hitmarks", "seqs", "encounters", "trackers" };
    std::string o = "{";
    for (int k = 0; k < 6; ++k) {
        if (k) o += ",";
        o += "\""; o += kinds[k]; o += "\":{";
        auto it = dictJson_.find(kinds[k]);
        bool first = true;
        if (it != dictJson_.end()) for (const auto& kv : it->second) { o += first ? "" : ","; first = false; o += "\"" + std::to_string(kv.first) + "\":" + kv.second; }
        o += "}";
    }
    return o + "}";
}

void Recorder::writeEvent(const Ev& e) {
    ensureActor(e.a1); ensureActor(e.a2);
    ensureDict(e);
    const std::string line = eventJson(e);
    emitLine(line);
    ++written_;
    tail_.push_back({ ++seq_, line });
    while (tail_.size() > kTailMax) tail_.pop_front();
}

void Recorder::openLog(long long c, long long wallMs, const std::vector<Ev>& pending) {
    logId_ = cfg_.newLogId ? cfg_.newLogId() : std::to_string(wallMs);
    startedAt_ = wallMs; c0_ = c; wall0_ = wallMs;
    written_ = 0; logBytes_ = 0; gaps_ = 0; readFails_ = 0; seq_ = 0;
    tail_.clear(); dict_.clear(); dictJson_.clear(); fights_.clear();
    for (auto& a : actors_) a.row.written = false;
    logOpen_ = true;
    emitLine(headerJson());
    // The state before the pre-roll window, for keys the window and the baseline do not carry.
    std::unordered_set<std::uint64_t> have;
    for (const auto& kv : baseline_) have.insert(kv.first);
    for (const auto& e : preroll_) if (e.key) have.insert(e.key);
    for (const auto& e : pending) if (e.key) have.insert(e.key);
    std::vector<Ev> snap;
    const long long sc = c - kPrerollCycles - 1;
    auto add = [&](int type, long long at, std::initializer_list<long long> f, int a1 = -1) {
        std::vector<Ev> one; push(one, type, at, f, a1);
        if (!have.count(one[0].key)) snap.push_back(std::move(one[0]));
    };
    for (const auto& a : actors_) {
        if (!a.present) continue;
        add(11, a.row.first < sc ? a.row.first : sc, { a.row.i, 1 }, a.row.i);
        if (a.anim != kUnknown) add(2, sc, { a.row.i, a.anim }, a.row.i);
        if (a.target != kUnknown) add(3, sc, { a.row.i, a.target }, a.row.i);
        if (a.lp >= 0 && a.row.type != "self") add(4, sc, { a.row.i, a.lp, a.lpMax }, a.row.i);
    }
    if (selfIdx_ >= 0 && lp_ >= 0) add(4, sc, { selfIdx_, lp_, lpMax_ }, selfIdx_);
    if (adren_ >= 0) add(5, sc, { adren_ });
    if (prayer_ != kUnknown) add(6, sc, { prayer_ & 0xFFFF, (prayer_ >> 16) & 0x7FFF });
    if (enc_ != kUnknown) add(12, sc, { enc_ });
    for (const auto& b : buffs_) if (b.known && b.on) add(7, sc, { b.structId, 1, -1, b.last, -1 });
    for (const auto& kv : trackers_) add(9, sc, { kv.first >> 16, (kv.first >> 8) & 0xFF, kv.first & 0xFF, kv.second });
    std::vector<Ev> base;
    for (auto& kv : baseline_) base.push_back(std::move(kv.second));
    baseline_.clear();
    std::stable_sort(snap.begin(), snap.end(), [](const Ev& a, const Ev& b) { return a.c < b.c; });
    std::stable_sort(base.begin(), base.end(), [](const Ev& a, const Ev& b) { return a.c < b.c; });
    for (const auto& e : snap) writeEvent(e);
    for (const auto& e : base) writeEvent(e);
    for (const auto& e : preroll_) writeEvent(e);
    preroll_.clear();
}

void Recorder::openFight(long long c, const char* by, long long wallMs, const std::vector<Ev>& pending) {
    if (!logOpen_) openLog(c, wallMs, pending);
    else {
        std::vector<Ev> base;
        for (auto& kv : baseline_) base.push_back(std::move(kv.second));
        baseline_.clear();
        std::stable_sort(base.begin(), base.end(), [](const Ev& a, const Ev& b) { return a.c < b.c; });
        for (const auto& e : base) writeEvent(e);
        for (const auto& e : preroll_) writeEvent(e);
        preroll_.clear();
    }
    cur_ = FightRow{};
    cur_.n = (int)fights_.size(); cur_.start = c; cur_.startBy = by;
    if (enc_ != kUnknown && enc_ != -1) { cur_.kind = "encounter"; cur_.boss = cfg_.names.structStr ? cfg_.names.structStr(enc_, 8849) : std::string(); }
    lastActionC_ = c;
    inFight_ = true;
    for (auto& a : actors_) a.hitByMe = false;
    Ev m; m.type = 16; m.c = c; m.f = { 0 }; m.text = by;
    writeEvent(m);
}

void Recorder::endFight(long long c, const char* by) {
    if (!inFight_) return;
    if (c < cur_.start) c = cur_.start;
    cur_.end = c; cur_.endBy = by;
    Ev m; m.type = 16; m.c = c; m.f = { 1 }; m.text = by;
    writeEvent(m);
    emitLine(fightJson(cur_));
    fights_.push_back(cur_);
    inFight_ = false;
}

void Recorder::resetScene() {
    actors_.clear(); byUid_.clear(); baseline_.clear(); preroll_.clear(); dict_.clear();
    selfIdx_ = -1;
}

void Recorder::Close(const char* why) {
    if (inFight_) endFight(lastC_, why);
    if (logOpen_) {
        Ev m; m.type = 16; m.c = lastC_; m.f = { std::strcmp(why, "rotation") == 0 ? 3 : 4 }; m.text = why;
        writeEvent(m);
        emitLine("[\"end\",{\"endedAt\":" + std::to_string(lastWallMs_) + ",\"endBy\":\"" + std::string(why) + "\",\"readFails\":" + std::to_string(readFails_) +
                 ",\"gaps\":" + std::to_string(gaps_) + ",\"phase\":" + std::to_string(phase_) + ",\"events\":" + std::to_string(written_) + "}]");
        logOpen_ = false;
    }
    resetScene();
}

bool Recorder::TakeLines(std::vector<std::string>& out) {
    if (pending_.empty()) return false;
    out.insert(out.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
    return true;
}

bool Recorder::NeedsRotation(long long nowMs) const {
    if (!logOpen_) return false;
    const long long age = nowMs - startedAt_;
    if (age >= kRotateHardMs) return true;
    return !inFight_ && (age >= kRotateMs || logBytes_ >= kRotateBytes || written_ >= kRotateEvents);
}

void Recorder::FeedLocal(std::uint32_t clock, int anim, int targetUid, long long wallMs) {
    if (selfIdx_ < 0 || selfIdx_ >= (int)actors_.size()) return;
    const long long c = (long long)clock;
    if (c > lastC_) lastC_ = c;
    lastWallMs_ = wallMs;
    ActorState& s = actors_[(std::size_t)selfIdx_];
    std::vector<Ev> evs;
    if (anim != s.anim) { s.anim = anim; push(evs, 2, c, { s.row.i, anim }, s.row.i); }
    int tgt = targetUid < 0 ? -1 : indexOfUid(targetUid);
    if (targetUid >= 0 && (tgt < 0 || !actors_[(std::size_t)tgt].present)) tgt = -targetUid - 2;
    if (tgt != s.target) { s.target = tgt; push(evs, 3, c, { s.row.i, tgt }, s.row.i); }
    route(evs);
}

void Recorder::Feed(const Tick& t) {
    if (!t.ok) { ++readFails_; return; }
    if (t.status != 30) { if (logOpen_ || inFight_) Close("logout"); lastWallMs_ = t.wallMs; return; }
    const long long c = (long long)t.clock;
    std::vector<Ev> evs;
    if (lastWallMs_ && t.wallMs - lastWallMs_ > 1000) { ++gaps_; push(evs, 16, c, { 2 }, -1, -1, "gap " + std::to_string(t.wallMs - lastWallMs_) + " ms"); }
    lastWallMs_ = t.wallMs; lastC_ = c;
    std::unordered_map<int, int> vp, vc;
    for (const auto& kv : t.varps) vp[kv.first] = kv.second;
    for (const auto& kv : t.varcs) vc[kv.first] = kv.second;

    // actors: presence, animation, target, life points, stats, bars, hits
    std::vector<int> myHits; bool hitOnSelf = false; long long actionC = -1;
    std::vector<char> seen(actors_.size(), 0);
    std::vector<std::pair<int, bool>> hitThisPass;   // (actor index, by me)
    for (const auto& a : t.actors) {
        const int i = actorIndex(a, c);
        if ((std::size_t)i >= seen.size()) seen.resize((std::size_t)i + 1, 0);
        seen[(std::size_t)i] = 1;
        ActorState& s = actors_[(std::size_t)i];
        if (!s.present) { s.present = true; push(evs, 11, c, { i, 1 }, i); }
        s.lastSeenC = c;
        if (a.self) selfIdx_ = i;
        if (a.anim != s.anim) {
            s.anim = a.anim; push(evs, 2, c, { i, a.anim }, i);
            if (a.type == 1 && a.anim >= 0 && cfg_.names.official && s.deathC < c - 300) {
                const std::string nm = cfg_.names.official("seq", a.anim);
                if (nm.size() > 6 && nm.compare(nm.size() - 6, 6, "_DEATH") == 0) {
                    s.deathC = c; push(evs, 10, c, { i, 1 }, i);
                    if (inFight_ && (s.hitByMe || std::find(cur_.targets.begin(), cur_.targets.end(), i) != cur_.targets.end())) ++cur_.kills;
                }
            }
        }
        if (a.type == 2) {
            int tgt = a.targetUid < 0 ? -1 : indexOfUid(a.targetUid);
            if (a.targetUid >= 0 && tgt < 0) tgt = -a.targetUid - 2;
            if (tgt != s.target) { s.target = tgt; push(evs, 3, c, { i, tgt }, i); }
        } else {
            int tgt = a.npcTarget < 0 ? -1 : indexOfUid(a.npcTarget);
            if (a.npcTarget >= 0 && (tgt < 0 || actors_[(std::size_t)tgt].row.type == "npc")) tgt = -a.npcTarget - 2;
            if (tgt != s.target) { s.target = tgt; push(evs, 3, c, { i, tgt }, i); }
            if (a.haveStats) {
                if (a.lp != s.lp || a.lpMax != s.lpMax) {
                    const int prev = s.lp;
                    s.lp = a.lp; s.lpMax = a.lpMax;
                    if (a.lpMax > s.row.lpMax) s.row.lpMax = a.lpMax;
                    push(evs, 4, c, { i, a.lp, a.lpMax }, i);
                    if (prev > 0 && a.lp == 0 && s.lastHitC >= c - 60 && s.deathC < c - 300) {
                        s.deathC = c; push(evs, 10, c, { i, 0 }, i);
                        if (inFight_ && (s.hitByMe || std::find(cur_.targets.begin(), cur_.targets.end(), i) != cur_.targets.end())) ++cur_.kills;
                    }
                }
                for (int k = 0; k < 7; ++k) {
                    if (k == 3) continue;
                    if (!s.haveStats || a.stats[k] != s.stats[k] || a.base[k] != s.base[k]) push(evs, 18, c, { i, k, a.stats[k], a.base[k] }, i);
                }
                std::memcpy(s.stats, a.stats, sizeof(s.stats)); std::memcpy(s.base, a.base, sizeof(s.base)); s.haveStats = true;
                if (a.vis != s.vis) { s.vis = a.vis; s.row.vis = a.vis; }
            }
        }
        for (int k = 0; k < a.nbars && k < 4; ++k) {
            const int stamp = a.barStamp[k], fill = a.barFill[k];
            if (stamp <= 0 || stamp > c + 50 || c - stamp > 30000 || fill < 0 || fill > 255) continue;
            if (fill != s.bar[k]) { s.bar[k] = fill; push(evs, 17, c, { i, k, fill }, i); }
        }
        if (a.haveRing) {
            RingMemo& m = rings_[a.uid];
            m.at = c;
            std::vector<std::uint64_t> now; now.reserve(6);
            for (int slot = 0; slot < 6; ++slot) {
                const auto& r = a.ring[slot];
                if (r.hitmark < 0) continue;
                const std::uint64_t key = ringKey(slot, r);
                now.push_back(key);
                if (std::find(m.keys.begin(), m.keys.end(), key) != m.keys.end()) continue;
                if (!m.seen) { if (r.expiry <= c) continue; }                     // first sight: only live records
                else if (r.expiry < c - 3000 || r.expiry > c + 120) continue;      // stale or implausible
                const long long cr = (long long)r.expiry - r.dur;
                if (phase_ < 0) phase_ = (int)(((cr % 30) + 30) % 30);
                const long long delay = ((cr - phase_) % 30 + 30) % 30;
                push(evs, 0, cr, { i, r.hitmark, r.value, r.hm2, r.value2, delay }, i);
                if (cr > s.lastHitC) s.lastHitC = cr;
                const auto* info = rtx::hitmarks::Find(r.hitmark);
                const bool mine = !(info && info->other);
                if (a.self) { hitOnSelf = true; if (cr > actionC) actionC = cr; }
                else if (mine && a.type == 1) { myHits.push_back(i); s.hitByMe = true; if (cr > actionC) actionC = cr; }
                hitThisPass.push_back({ i, mine });
            }
            // keys of records no longer in the ring have been overwritten and cannot come back
            m.keys = std::move(now);
            m.seen = true;
        }
    }
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        ActorState& s = actors_[i];
        if (s.present && (i >= seen.size() || !seen[i])) { s.present = false; s.leftC = c; push(evs, 11, c, { (long long)i, 0 }, (int)i); }
    }
    if (rings_.size() > 4096) for (auto it = rings_.begin(); it != rings_.end();) it = (it->second.at < c - 15000) ? rings_.erase(it) : std::next(it);

    // the local player's vitals
    bool selfDied = false;
    {
        auto lp = vp.find(kVarpLp), mx = vp.find(kVarpLpMax);
        if (lp != vp.end() && selfIdx_ >= 0) {
            const int v = lp->second, m = mx != vp.end() ? mx->second : lpMax_;
            if (v != lp_ || m != lpMax_) {
                if (lp_ > 0 && v == 0) selfDied = true;
                lp_ = v; lpMax_ = m;
                push(evs, 4, c, { selfIdx_, v, m }, selfIdx_);
            }
        }
        auto ad = vp.find(kVarpAdren);
        if (ad != vp.end() && ad->second != adren_) { adren_ = ad->second; push(evs, 5, c, { adren_ }); }
        auto pr = vp.find(kVarpPrayer);
        if (pr != vp.end() && pr->second != prayer_) { prayer_ = pr->second; push(evs, 6, c, { prayer_ & 0xFFFF, (prayer_ >> 16) & 0x7FFF }); }
    }
    if (selfDied && selfIdx_ >= 0) { push(evs, 10, c, { selfIdx_, 2 }, selfIdx_); if (inFight_) ++cur_.deaths; }

    // encounter
    bool encEnded = false, encBegan = false;
    {
        auto en = vp.find(kVarpEncounter);
        if (en != vp.end() && en->second != enc_) {
            const int prev = enc_;
            enc_ = en->second;
            push(evs, 12, c, { enc_ });
            encEnded = (prev != kUnknown && prev != -1 && enc_ == -1);
            encBegan = (enc_ != -1 && (prev == kUnknown || prev == -1));
            if (inFight_ && enc_ != -1) { cur_.kind = "encounter"; cur_.boss = cfg_.names.structStr ? cfg_.names.structStr(enc_, 8849) : std::string(); }
        }
    }

    // casts: a START varc moving to a new stamp
    bool castAtTarget = false; std::vector<long long> castCs; long long gcdC = -1;
    for (const auto& kv : t.varcs) {
        auto it = casts_.find(kv.first);
        if (it == casts_.end()) continue;
        CastVar& cs = it->second;
        if (!cs.known) {
            // the var is created at an ability's first use: a node first seen with a stamp in the last 3 s is that cast
            cs.known = true; cs.last = kv.second;
            if (kv.second <= 0 || c - kv.second > 150 || kv.second > c + 30) continue;
        } else {
            if (kv.second == cs.last) continue;
            cs.last = kv.second;
            if (kv.second <= 0 || std::llabs((long long)kv.second - c) > 3000) continue;
        }
        if (kv.first == kGcdStart) { gcdC = kv.second; continue; }
        auto en = vc.find(cs.endVarc);
        push(evs, 1, kv.second, { cs.structId, en != vc.end() ? en->second : -1, 1 });
        castCs.push_back(kv.second);
        phase_ = (int)(((kv.second % 30) + 30) % 30);
    }
    if (gcdC >= 0) {
        bool paired = false;
        for (long long x : castCs) if (std::llabs(x - gcdC) <= 1) paired = true;
        if (!paired) { auto en = vc.find(kGcdEnd); push(evs, 1, gcdC, { kGcdStruct, en != vc.end() ? en->second : -1, 3 }); castCs.push_back(gcdC); phase_ = (int)(((gcdC % 30) + 30) % 30); }
    }
    if (!castCs.empty() && selfIdx_ >= 0) {
        const int tgt = actors_[(std::size_t)selfIdx_].target;
        if (tgt >= 0 && tgt < (int)actors_.size() && actors_[(std::size_t)tgt].row.type == "npc" && actors_[(std::size_t)tgt].present) {
            castAtTarget = true;
            for (long long x : castCs) if (x > actionC) actionC = x;
            if (inFight_ && std::find(cur_.targets.begin(), cur_.targets.end(), tgt) == cur_.targets.end()) cur_.targets.push_back(tgt);
        }
    }

    // buffs: the end cycle of every timer var the stores hold
    for (auto& b : buffs_) {
        bool ok = false;
        const int v = varValue(vp, vc, b.kind, b.var, ok);
        if (!ok) continue;
        bool cok = false; const int stacks = b.countVar ? varValue(vp, vc, b.countKind, b.countVar, cok) : 0;
        if (!b.known) {                                   // first sight: a running timer is an on row with its start unknown
            b.known = true; b.last = v; b.on = v > c;
            if (b.on) push(evs, 7, c, { b.structId, 1, -1, v, cok ? stacks : -1 });
            continue;
        }
        if (v == b.last) { if (b.on && v <= c) b.on = false; continue; }
        b.last = v;
        const bool on = v > c;
        push(evs, 7, c, { b.structId, on ? 1 : 0, (on && !b.on) ? c : -1, v, cok ? stacks : -1 });
        b.on = on;
    }

    // trackers
    if (t.haveTrackers) {
        for (const auto& cell : t.trackers) {
            if (cell.group < 0 || cell.group > 255 || cell.row < 0 || cell.row > 255 || cell.col < 0 || cell.col > 255) continue;
            const int key = (cell.group << 16) | (cell.row << 8) | cell.col;
            auto it = trackers_.find(key);
            if (it != trackers_.end() && it->second == cell.value) continue;
            trackers_[key] = cell.value;
            push(evs, 9, c, { cell.group, cell.row, cell.col, cell.value });
        }
    }

    // boss kill counts: one boss rising by a kill or two is a kill; anything else is the log loading
    std::string kcRise;
    if (!cfg_.bosses.empty() && !vp.empty()) {
        std::map<std::string, long long> totals;
        for (const auto& b : cfg_.bosses) if (b.kc[0] > 0 && vp.count(b.kc[0])) totals[b.name] = bossTotal(vp, b.kc) + 60000 * bossTotal(vp, b.pr);
        if (!totals.empty()) {
            if (kcKnown_) {
                int rises = 0; bool big = false, baseZero = true;
                for (const auto& kv : kc_) if (kv.second != 0) { baseZero = false; break; }
                for (const auto& kv : totals) {
                    auto it = kc_.find(kv.first);
                    if (it == kc_.end()) continue;
                    const long long d = kv.second - it->second;
                    if (d > 0) { ++rises; kcRise = kv.first; if (d > 3) big = true; }
                }
                if (rises != 1 || big || baseZero) kcRise.clear();
            }
            kc_ = std::move(totals); kcKnown_ = true;
        }
    }

    // fight segmentation
    if (!inFight_) {
        const char* by = nullptr; long long startC = c;
        if (!myHits.empty() || hitOnSelf || castAtTarget) {
            by = !myHits.empty() ? "hit" : hitOnSelf ? "taken" : "cast";
            startC = c;
            for (const auto& e : evs) if ((e.type == 0 && (e.a1 == selfIdx_ || std::find(myHits.begin(), myHits.end(), e.a1) != myHits.end())) || e.type == 1) startC = std::min(startC, e.c);
        } else if (enc_ != kUnknown && enc_ != -1 && (encBegan || !logOpen_)) by = "encounter";
        if (by) {
            openFight(startC, by, t.wallMs, evs);
            if (selfIdx_ >= 0) {
                const int tgt = actors_[(std::size_t)selfIdx_].target;
                if (tgt >= 0 && tgt < (int)actors_.size() && actors_[(std::size_t)tgt].row.type == "npc") cur_.targets.push_back(tgt);
            }
        }
    }
    if (inFight_) {
        if (actionC > lastActionC_) lastActionC_ = actionC;
        for (int i : myHits) if (std::find(cur_.targets.begin(), cur_.targets.end(), i) == cur_.targets.end()) cur_.targets.push_back(i);
        if (!kcRise.empty()) { cur_.kind = "boss"; cur_.boss = kcRise; ++cur_.kills; }
        route(evs);
        if (selfDied) endFight(c, "death");
        else if (!kcRise.empty()) endFight(c, "kill");
        else if (encEnded) endFight(c, "encounter");
        else if (c - lastActionC_ >= (long long)kIdleTicks * 30) endFight(lastActionC_, "idle");
    } else {
        route(evs);
    }
}

// The scene and the local vitals as the recorder holds them, fight or no fight: the headless switch
// prints it so the reads can be judged without a log.
std::string Recorder::DiagJson() const {
    int castsSeen = 0, buffsSeen = 0, buffsOn = 0;
    for (const auto& kv : casts_) if (kv.second.known) ++castsSeen;
    for (const auto& b : buffs_) { if (b.known) ++buffsSeen; if (b.on) ++buffsOn; }
    std::string o = "{\"clock\":" + std::to_string(lastC_) + ",\"phase\":" + std::to_string(phase_) + ",\"lp\":" + std::to_string(lp_) + ",\"lpMax\":" + std::to_string(lpMax_) +
                    ",\"adren\":" + std::to_string(adren_) + ",\"prayer\":" + std::to_string(prayer_ == kUnknown ? -1 : prayer_) + ",\"encounter\":" + std::to_string(enc_ == kUnknown ? -2 : enc_) +
                    ",\"castVarsSeen\":" + std::to_string(castsSeen) + ",\"buffVarsSeen\":" + std::to_string(buffsSeen) + ",\"buffsOn\":" + std::to_string(buffsOn) +
                    ",\"trackerCells\":" + std::to_string(trackers_.size()) + ",\"preroll\":" + std::to_string(preroll_.size()) + ",\"baseline\":" + std::to_string(baseline_.size()) + ",\"actors\":[";
    bool first = true;
    for (const auto& a : actors_) {
        if (!a.present) continue;
        o += first ? "" : ","; first = false;
        o += "{\"type\":\"" + a.row.type + "\",\"uid\":" + std::to_string(a.row.uid) + ",\"id\":" + std::to_string(a.row.id) + ",\"name\":" + jstr(a.row.name) +
             ",\"anim\":" + std::to_string(a.anim == kUnknown ? -1 : a.anim) + ",\"target\":" + std::to_string(a.target == kUnknown ? -1 : a.target) +
             ",\"lp\":" + std::to_string(a.lp) + ",\"lpMax\":" + std::to_string(a.lpMax) + ",\"bars\":[" + std::to_string(a.bar[0]) + "," + std::to_string(a.bar[1]) + "]}";
    }
    return o + "]}";
}

std::string Recorder::CurrentJson(std::uint64_t since) const {
    // logId is empty once the log closed: the page then drops the live view and lists the saved file
    std::string o = "{\"logId\":" + jstr(logOpen_ ? logId_ : std::string()) + ",\"open\":" + (logOpen_ ? "true" : "false") + ",\"inFight\":" + (inFight_ ? "true" : "false") +
                    ",\"seq\":" + std::to_string(seq_) + ",\"header\":" + (logOpen_ ? headerJson() : std::string("null")) + ",\"dict\":" + dictJson() + ",\"actors\":[";
    bool first = true;
    for (const auto& a : actors_) {
        if (!a.row.written) continue;
        const std::string line = actorJson(a.row);      // ["actor",{...}]
        o += first ? "" : ","; first = false;
        o += line.substr(9, line.size() - 10);
    }
    o += "],\"fights\":[";
    first = true;
    auto addFight = [&](const FightRow& f) { const std::string line = fightJson(f); o += first ? "" : ","; first = false; o += line.substr(9, line.size() - 10); };
    for (const auto& f : fights_) addFight(f);
    if (inFight_) addFight(cur_);
    o += "],\"events\":[";
    first = true;
    for (const auto& kv : tail_) {
        if (kv.first <= since) continue;
        o += first ? "" : ","; first = false;
        o += kv.second;
    }
    o += "]}";
    return o;
}

}  // namespace rtx::launcher::combat
