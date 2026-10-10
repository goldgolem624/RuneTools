#include "CombatRecorder.h"
#include "BossMechanics.h"
#include "../reader/BuffVars.h"
#include "../reader/Hitmarks.h"
#include "../../companion/ServerOps.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <unordered_set>

namespace rtx::launcher::combat {
namespace {

constexpr int kUnknown = -0x7fffffff;
constexpr int kVarpLp = 13537, kVarpLpMax = 13538, kVarpAdren = 679, kVarpPrayer = 3274, kVarpEncounter = 10946;
constexpr int kGcdStart = 2091, kGcdEnd = 2092, kGcdStruct = 14881;   // the global cooldown's dummy struct
constexpr int kScriptCooldown = 6570, kScriptChannel = 18766, kScriptBuffTimer = 4252;   // 4252: (struct, ticks left)   // (struct, start tick, end tick, 1, 1); (side, ticks, id, name)
constexpr long long kCastMatch = 15;
constexpr long long kBuffNameWaitPasses = 600;       // about a minute of 100 ms passes                 // a cooldown varc and script 6570 this many cycles apart are one cast
constexpr long long kRestoreSlack = 45;              // a 6570 record this much older than the tick offset is a cooldown restore
bool isGcdStruct(int st) { return st == 14881 || st == 14882 || st == 29145; }
constexpr int kTypeSound = 19, kTypeItem = 20, kTypePerks = 21, kTypeEof = 22, kTypeMech = 100;   // kTypeItem: [20, c, container, slot, item, count]
                                                    // kTypeEof: [22, c, container, slot, weapon] the special attack an Essence of Finality stores (-1 none)
                                                    // kTypePerks: [21, c, slot, perk, rank x 4] for a worn item      // kTypeMech is written as ["mech", c, boss, key, kind, id, actor]
constexpr long long kResendMs = 1200;                // tile items this soon after their zone was cleared are the zone sent again

std::uint16_t nu16(const std::uint8_t* b) { return (std::uint16_t)((b[0] << 8) | b[1]); }
std::uint32_t nu32(const std::uint8_t* b) { return ((std::uint32_t)b[0] << 24) | ((std::uint32_t)b[1] << 16) | ((std::uint32_t)b[2] << 8) | b[3]; }
std::uint64_t zoneKey(int x, int y, int plane) { return ((std::uint64_t)(plane & 3) << 48) ^ ((std::uint64_t)(std::uint32_t)(x & 0xFFFFFF) << 24) ^ (std::uint32_t)(y & 0xFFFFFF); }
std::uint64_t mechKey(int kind, int id) { return ((std::uint64_t)(std::uint32_t)kind << 32) | (std::uint32_t)id; }
// Zone item body length by sub id; -1: a length byte follows the id.
int itemLen(int sub) {
    static const int k[18] = { 6, 11, 14, 5, 10, 21, 8, -1, -1, 8, 4, 11, 2, 7, -1, 20, 28, 29 };
    return sub >= 0 && sub < 18 ? k[sub] : -2;
}

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
std::string plainText(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '<') {
            const std::size_t e = s.find('>', i);
            if (e == std::string::npos) break;
            if (s.compare(i, e - i + 1, "<nbsp>") == 0) o.push_back(' ');
            i = e;
        } else if (c == 0xA0) o.push_back(' ');
        else if (c >= 0x20 && c < 0x80) o.push_back((char)c);
    }
    return o;
}

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

void BuiltinMechanics(std::vector<MechRow>& rows, std::vector<MechBoss>& bosses) {
    namespace bm = rtx::bossmech;
    rows.clear(); bosses.clear();
    for (const auto& r : bm::kRows) {
        MechRow m; m.kind = (int)r.kind; m.id = r.id; m.boss = r.boss; m.key = r.key; m.label = r.label; m.tactic = r.tactic; m.windowMs = r.window_ms;
        if (m.kind == 7) m.domain = 2;                  // the table's var rows are player varbits
        rows.push_back(std::move(m));
    }
    for (const auto& b : bm::kBosses) {
        MechBoss x; x.npc = b.npc; x.encounter = b.encounter;
        for (const auto p : b.phases) if (p > 0) x.phases.push_back(p);
        bosses.push_back(std::move(x));
    }
}

// ---- packets ----

bool NetDecoder::Wanted(int op) {
    namespace s = rtx::sops;
    switch (op) {
    case s::kZoneBase: case s::kZoneClear: case s::kZoneUpdate: case s::kSpotAnim: case s::kSpotAnim2: case s::kSpotAnimActor: case s::kSpotAnimActor2:
    case s::kProjectile: case s::kProjectile20: case s::kProjectile28: case s::kProjectile29: case s::kSound: case s::kAreaSound: case s::kAreaSoundAbs:
    case s::kHintArrow: case s::kVarpInt: case s::kVarpByte: case s::kVarpLong: case s::kVarbitVarint: case s::kVarbitByte: case s::kVarbitInt:
    case s::kVarcInt: case s::kVarcByte: case s::kVarcLong: case s::kRunClientScript:
        return true;
    default:
        return false;
    }
}

long long NetDecoder::FullWall(std::uint32_t low, long long near) {
    long long v = (near & ~0xFFFFFFFFLL) | (long long)low;
    if (v - near > 0x80000000LL) v -= 0x100000000LL;
    else if (near - v > 0x80000000LL) v += 0x100000000LL;
    return v;
}

// A zone-local position byte (x << 4 | y) on the current zone; x = -1 when no zone base was seen.
bool NetDecoder::tileOf(std::uint8_t pos, NetEv& e) const {
    e.tile = true;
    if (!zset_) { e.x = e.y = e.plane = -1; return false; }
    e.x = zx_ + ((pos >> 4) & 7); e.y = zy_ + (pos & 7); e.plane = zp_;
    return true;
}

void NetDecoder::item(int sub, const std::uint8_t* b, std::uint32_t n, long long wallMs, std::vector<NetEv>& out) {
    const int need = itemLen(sub);
    if (need < 0 || (int)n < need) return;
    NetEv e; e.wallMs = wallMs;
    switch (sub) {
    case 0x0B: case 0x02:   // graphic on a tile: position b0, graphic u16 BE at 1 (0xFFFF removes)
        e.kind = NetEv::Gfx; e.id = nu16(b + 1); tileOf(b[0], e); break;
    case 0x05:              // projectile: graphic u16 BE at 10
        e.kind = NetEv::Proj; e.id = nu16(b + 10); e.form = need; break;
    case 0x0F: case 0x10:   // projectile: graphic u16 BE at 6
        e.kind = NetEv::Proj; e.id = nu16(b + 6); e.form = need; break;
    case 0x11:              // projectile: source position b0, graphic u16 BE at 10
        e.kind = NetEv::Proj; e.id = nu16(b + 10); e.form = need; tileOf(b[0], e); break;
    case 0x04: case 0x01:   // sound on a tile: position b0, id u32 BE at 1
        e.kind = NetEv::Sound; e.id = (int)nu32(b + 1); tileOf(b[0], e); break;
    default:
        return;
    }
    if (e.kind != NetEv::Sound && e.id == 0xFFFF) return;
    if (e.tile && zset_) {
        auto it = cleared_.find(zoneKey(zx_, zy_, zp_));
        e.resend = it != cleared_.end() && wallMs >= it->second && wallMs - it->second <= kResendMs;
    }
    out.push_back(e);
}

void NetDecoder::Decode(int op, const std::uint8_t* b, std::uint32_t n, long long wallMs, std::vector<NetEv>& out) {
    namespace s = rtx::sops;
    auto var = [&](int kind, int id, int value) { NetEv e; e.kind = kind; e.wallMs = wallMs; e.id = id; e.value = value; out.push_back(e); };
    switch (op) {
    case s::kZoneBase:      // y offset, -x offset (zones from the map base), plane + 0x80
        if (n >= 3) zone(baseX_ - (int)(std::int8_t)b[1] * 8, baseY_ + (int)(std::int8_t)b[0] * 8, (b[2] + 0x80) & 0xFF);
        return;
    case s::kZoneClear:     // plane + 0x80, x offset, y offset
        if (n < 3) return;
        zone(baseX_ + (int)(std::int8_t)b[1] * 8, baseY_ + (int)(std::int8_t)b[2] * 8, (b[0] + 0x80) & 0xFF);
        if (zset_) {
            cleared_[zoneKey(zx_, zy_, zp_)] = wallMs;
            if (cleared_.size() > 512) for (auto it = cleared_.begin(); it != cleared_.end();) it = wallMs - it->second > 60000 ? cleared_.erase(it) : std::next(it);
        }
        return;
    case s::kZoneUpdate: {  // 0x80 - plane, -x offset, y offset, then [sub id][body] items
        if (n < 3) return;
        zone(baseX_ - (int)(std::int8_t)b[1] * 8, baseY_ + (int)(std::int8_t)b[2] * 8, (0x80 - b[0]) & 0xFF);
        std::uint32_t p = 3;
        while (p < n) {
            const int sub = b[p++]; int len = itemLen(sub);
            if (len == -1) { if (p >= n) break; len = b[p++]; }
            if (len < 0 || p + (std::uint32_t)len > n) break;    // an unknown sub id or a short body ends the walk
            item(sub, b + p, (std::uint32_t)len, wallMs, out);
            p += (std::uint32_t)len;
        }
        return;
    }
    case s::kRunClientScript: {    // [sig NUL][args in reverse signature order: s = NUL string, else i32 BE][script i32 BE]
        std::uint32_t p = 0; char sig[8]; int ns = 0;
        while (p < n && b[p] != 0) { if (ns >= 7) return; sig[ns++] = (char)b[p++]; }
        if (p >= n) return;
        ++p;
        const bool cd = ns == 5 && std::memcmp(sig, "iiiii", 5) == 0, ch = ns == 4 && std::memcmp(sig, "iiis", 4) == 0;
        const bool bt = ns == 2 && sig[0] == 'i' && sig[1] == 'i';
        if (!cd && !ch && !bt) return;
        NetEv e; e.kind = NetEv::Script; e.wallMs = wallMs;
        for (int i = ns - 1; i >= 0; --i) {
            if (sig[i] == 's') {
                const std::uint32_t s0 = p;
                while (p < n && b[p] != 0) ++p;
                if (p >= n) return;
                e.text.assign(reinterpret_cast<const char*>(b + s0), p - s0);
                ++p;
            } else {
                if (p + 4 > n) return;
                e.a[i] = (int)nu32(b + p); p += 4;
            }
        }
        if (p + 4 > n) return;
        e.id = (int)nu32(b + p);
        if ((cd && e.id == kScriptCooldown) || (ch && e.id == kScriptChannel) || (bt && e.id == kScriptBuffTimer)) out.push_back(std::move(e));
        return;
    }
    case s::kSpotAnim:      item(0x0B, b, n, wallMs, out); return;
    case s::kSpotAnim2:     item(0x02, b, n, wallMs, out); return;
    case s::kProjectile:    item(0x05, b, n, wallMs, out); return;
    case s::kProjectile20:  item(0x0F, b, n, wallMs, out); return;
    case s::kProjectile28:  item(0x10, b, n, wallMs, out); return;
    case s::kProjectile29:  item(0x11, b, n, wallMs, out); return;
    case s::kAreaSound:     item(0x04, b, n, wallMs, out); return;
    case s::kSpotAnimActor:
    case s::kSpotAnimActor2: {
        // 12 bytes: ref u32 BE at 5, graphic (b9 - 0x80 low, b10 high); 15 bytes: ref b2 b3 b0 b1, graphic (b11 - 0x80 low, b12 high)
        std::uint32_t ref = 0; unsigned g = 0;
        if (op == s::kSpotAnimActor) { if (n < 12) return; ref = nu32(b + 5); g = (unsigned)(((b[9] - 0x80) & 0xFF) | (b[10] << 8)); }
        else { if (n < 15) return; ref = ((std::uint32_t)b[2] << 24) | ((std::uint32_t)b[3] << 16) | ((std::uint32_t)b[0] << 8) | b[1]; g = (unsigned)(((b[11] - 0x80) & 0xFF) | (b[12] << 8)); }
        if (g == 0xFFFF) return;
        NetEv e; e.kind = NetEv::Gfx; e.wallMs = wallMs; e.id = (int)g;
        if (ref < 0x20000000u) { e.ref = 1; e.index = (int)(ref & 0xFFFF); }
        else if (ref < 0x40000000u) { e.ref = 2; e.index = (int)(ref & 0xFFFF); }
        else { e.tile = true; e.x = (int)((ref >> 14) & 0x3FFF); e.y = (int)(ref & 0x3FFF); e.plane = (int)((ref >> 28) & 3); }
        out.push_back(e);
        return;
    }
    case s::kSound: {       // id u32 BE
        if (n < 8) return;
        NetEv e; e.kind = NetEv::Sound; e.wallMs = wallMs; e.id = (int)nu32(b);
        out.push_back(e);
        return;
    }
    case s::kAreaSoundAbs: {    // id b2 b1 b3 b0, packed tile b7 b8 b5 b6
        if (n < 11) return;
        NetEv e; e.kind = NetEv::Sound; e.wallMs = wallMs;
        e.id = (int)(((std::uint32_t)b[2] << 24) | ((std::uint32_t)b[1] << 16) | ((std::uint32_t)b[3] << 8) | b[0]);
        const std::uint32_t pk = ((std::uint32_t)b[7] << 24) | ((std::uint32_t)b[8] << 16) | ((std::uint32_t)b[5] << 8) | b[6];
        e.tile = true;
        if (pk == 0xFFFFFFFFu) e.x = e.y = e.plane = -1;
        else { e.x = (int)((pk >> 14) & 0x3FFF); e.y = (int)(pk & 0x3FFF); e.plane = (int)((pk >> 28) & 3); }
        out.push_back(e);
        return;
    }
    case s::kHintArrow: {   // slot << 5 | type; type 0 clears, 1 NPC and 10 player (index u16 BE at 2), 2..6 tile (plane b2, x and y u16 BE at 3 and 5)
        if (n < 7) return;
        const int type = b[0] & 31;
        NetEv e; e.kind = NetEv::Hint; e.wallMs = wallMs;
        if (type == 1 || type == 10) { e.ref = type == 1 ? 2 : 1; e.index = nu16(b + 2); }
        else if (type >= 2 && type <= 6) { e.tile = true; e.plane = b[2]; e.x = nu16(b + 3); e.y = nu16(b + 5); }
        else return;
        out.push_back(e);
        return;
    }
    case s::kVarpInt:       // id ((b0 - 0x80) & 0xFF) | b1 << 8, value b4 b5 b2 b3
        if (n >= 6) var(NetEv::Varp, ((b[0] - 0x80) & 0xFF) | (b[1] << 8), (int)(((std::uint32_t)b[4] << 24) | ((std::uint32_t)b[5] << 16) | ((std::uint32_t)b[2] << 8) | b[3]));
        return;
    case s::kVarpByte:      // value i8 b0, id ((b2 - 0x80) & 0xFF) | b1 << 8
        if (n >= 3) var(NetEv::Varp, ((b[2] - 0x80) & 0xFF) | (b[1] << 8), (int)(std::int8_t)b[0]);
        return;
    case s::kVarpLong:      // low word b5 b4 b7 b6, id u16 BE at 8
        if (n >= 10) var(NetEv::Varp, (b[8] << 8) | b[9], (int)(((std::uint32_t)b[5] << 24) | ((std::uint32_t)b[4] << 16) | ((std::uint32_t)b[7] << 8) | b[6]));
        return;
    case s::kVarbitVarint: {    // two LEB128 varints: varbit id, value
        std::uint32_t p = 0; std::uint64_t v[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k) {
            for (int sh = 0;; sh += 7) {
                if (p >= n || sh > 56) return;
                const std::uint8_t c = b[p++];
                v[k] |= (std::uint64_t)(c & 0x7F) << sh;
                if (c < 0x80) break;
            }
        }
        var(NetEv::Varbit, (int)v[0], (int)(std::uint32_t)v[1]);
        return;
    }
    case s::kVarbitByte:    // value (b0 + 0x80) & 0xFF, id u16 LE at 1
        if (n >= 3) var(NetEv::Varbit, b[1] | (b[2] << 8), (b[0] + 0x80) & 0xFF);
        return;
    case s::kVarbitInt:     // value i32 BE, id u16 LE at 4
        if (n >= 6) var(NetEv::Varbit, b[4] | (b[5] << 8), (int)nu32(b));
        return;
    case s::kVarcInt:       // id ((b1 - 0x80) & 0xFF) | b0 << 8, value b3 b2 b5 b4
        if (n >= 6) var(NetEv::Varc, ((b[1] - 0x80) & 0xFF) | (b[0] << 8), (int)(((std::uint32_t)b[3] << 24) | ((std::uint32_t)b[2] << 16) | ((std::uint32_t)b[5] << 8) | b[4]));
        return;
    case s::kVarcByte:      // id u16 LE, value (int8)(0x80 - b2)
        if (n >= 3) var(NetEv::Varc, b[0] | (b[1] << 8), (int)(std::int8_t)((0x80 - b[2]) & 0xFF));
        return;
    case s::kVarcLong:      // low word u32 LE at 4, id u16 LE at 8
        if (n >= 10) var(NetEv::Varc, b[8] | (b[9] << 8), (int)((std::uint32_t)b[4] | ((std::uint32_t)b[5] << 8) | ((std::uint32_t)b[6] << 16) | ((std::uint32_t)b[7] << 24)));
        return;
    default:
        return;
    }
}

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
    casts_.clear(); buffs_.clear(); abilityAt_.clear();
    castQ_.clear(); netCasts_.clear(); tickOffs_.clear(); coSent_.clear(); haveTickOff_ = false; lastStyle_ = 0;
    for (std::size_t i = 0; i < cfg.abilities.size(); ++i) abilityAt_.emplace(cfg.abilities[i].structId, (int)i);
    casts_[kGcdStart] = CastVar{ kGcdStruct, kGcdEnd };
    for (const auto& a : cfg.abilities) {
        if (a.startVarc <= 0 || a.startVarc == kGcdStart) continue;
        auto it = casts_.find(a.startVarc);
        if (it == casts_.end()) casts_[a.startVarc] = CastVar{ a.structId, a.endVarc };
        else if (a.structId < it->second.structId) it->second.structId = a.structId;   // the family head
    }
    std::unordered_map<long long, int> buffOfVar;
    buffOfStruct_.clear(); buffQ_.clear();
    for (const auto& e : rtx::buffvars::kTimer) {
        const long long vk = ((long long)e.kind << 32) | (std::uint32_t)e.var;
        auto seen = buffOfVar.find(vk);
        if (seen != buffOfVar.end()) {                                    // one row per shared var; the struct joins its group
            BuffVar& g = buffs_[(std::size_t)seen->second];
            if (std::find(g.group.begin(), g.group.end(), e.structId) == g.group.end()) g.group.push_back(e.structId);
            buffOfStruct_[e.structId] = seen->second;
            continue;
        }
        BuffVar b; b.structId = e.structId; b.kind = e.kind; b.var = e.var; b.group = { e.structId };
        if (const auto* ce = rtx::buffvars::FindCount(e.structId)) { b.countKind = ce->kind; b.countVar = ce->var; }
        buffOfVar[vk] = (int)buffs_.size(); buffOfStruct_[e.structId] = (int)buffs_.size();
        buffs_.push_back(b);
    }
    mechIdx_.clear(); bossOfNpc_.clear(); bossOfEnc_.clear(); active_.clear(); mechLast_.clear(); varLast_.clear();
    auto add = [](std::vector<int>& v, int boss) { if (std::find(v.begin(), v.end(), boss) == v.end()) v.push_back(boss); };
    for (std::size_t r = 0; r < cfg.mechRows.size(); ++r) {
        const MechRow& m = cfg.mechRows[r];
        if (m.kind < 1 || m.kind > 8 || m.boss <= 0 || m.key.empty()) continue;
        mechIdx_[mechKey(m.kind, m.id)].push_back((int)r);
        add(bossOfNpc_[m.boss], m.boss);
    }
    for (const auto& b : cfg.mechBosses) {
        if (b.npc <= 0) continue;
        add(bossOfNpc_[b.npc], b.npc);
        for (int p : b.phases) if (p > 0) add(bossOfNpc_[p], b.npc);
        if (b.encounter > 0) add(bossOfEnc_[b.encounter], b.npc);
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
    case kTypeItem: e.key = stateKey(kTypeItem, e.f[0], e.f[1]); break;
    case kTypePerks: e.key = stateKey(kTypePerks, e.f[0]); break;
    case kTypeEof: e.key = stateKey(kTypeEof, e.f[0], e.f[1]); break;
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

// Packets arrive after the pass that may have closed their fight: the ones inside it still go to the log.
void Recorder::routeLate(std::vector<Ev>& evs) {
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.c < b.c; });
    for (auto& e : evs) {
        const bool closed = !inFight_ && logOpen_ && !fights_.empty() && e.c >= fights_.back().start && e.c <= fights_.back().end;
        if (inFight_ || closed) writeEvent(e);
        else preroll(std::move(e));
    }
    evs.clear();
}

// The actor a graphic or hint arrow names: ref 1 a player, 2 an NPC, by uid; -1 when it is not in the scene.
int Recorder::actorOfRef(int ref, int index) const {
    if (index < 0 || (ref != 1 && ref != 2)) return -1;
    auto fits = [&](const ActorState& s) { return s.present && s.row.uid == index && ((ref == 2) == (s.row.type == "npc")); };
    const int at = indexOfUid(index);
    if (at >= 0 && fits(actors_[(std::size_t)at])) return at;
    for (std::size_t i = actors_.size(); i-- > 0;) if (fits(actors_[i])) return (int)i;
    return -1;
}

// The one actor standing on a tile (the local player first, then NPCs, then other players); -1 when none or several.
int Recorder::actorOnTile(int x, int y, int plane) const {
    int found = -1, rank = 9, ties = 0;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const ActorState& s = actors_[i];
        if (!s.present || s.tx != x || s.ty != y || s.tplane != plane) continue;
        const int r = s.row.type == "self" ? 0 : s.row.type == "npc" ? 1 : 2;
        if (r < rank) { rank = r; found = (int)i; ties = 1; }
        else if (r == rank) ++ties;
    }
    return ties == 1 ? found : -1;
}

// The same graphic or sound on the same tile again: within kTileRepeatMs of the last sight (a zone sent
// again every second or two keeps it suppressed), or sent again right after its zone was cleared.
bool Recorder::tileRepeat(int kind, int id, const NetEv& n) {
    const std::uint64_t k = ((std::uint64_t)(kind & 0xFF) << 56) ^ ((std::uint64_t)(std::uint32_t)(id & 0xFFFFFF) << 32) ^
                            ((std::uint64_t)(n.plane & 3) << 30) ^ ((std::uint64_t)(n.x & 0x7FFF) << 15) ^ (std::uint64_t)(n.y & 0x7FFF);
    auto it = tileSeen_.find(k);
    const bool rep = it != tileSeen_.end() && (n.wallMs - it->second < kTileRepeatMs || n.resend);
    tileSeen_[k] = n.wallMs;
    if (tileSeen_.size() > 8192) for (auto j = tileSeen_.begin(); j != tileSeen_.end();) j = n.wallMs - j->second > 600000 ? tileSeen_.erase(j) : std::next(j);
    return rep;
}

// Bosses in scope: one of their NPCs (the boss or a phase) is in the scene, or their encounter bar runs.
void Recorder::updateScope() {
    active_.clear();
    if (bossOfNpc_.empty()) return;
    for (const auto& s : actors_) {
        if (!s.present || s.row.type != "npc") continue;
        auto it = bossOfNpc_.find(s.row.id);
        if (it != bossOfNpc_.end()) active_.insert(it->second.begin(), it->second.end());
    }
    if (enc_ != kUnknown && enc_ != -1) {
        auto it = bossOfEnc_.find(enc_);
        if (it != bossOfEnc_.end()) active_.insert(it->second.begin(), it->second.end());
    }
}

// A mech event for every row of (kind, id) whose boss is in scope (and in `before` when given), once per
// row within its window; an animation counts only on the boss's own NPCs. True when such a row exists,
// matched or held back.
bool Recorder::matchMech(std::vector<Ev>& evs, int kind, int id, long long c, int actor, int domain, const std::unordered_set<int>* before) {
    auto it = mechIdx_.find(mechKey(kind, id));
    if (it == mechIdx_.end()) return false;
    const std::vector<int>* own = nullptr;
    if (kind == 1) {
        if (actor < 0 || actor >= (int)actors_.size()) return false;
        auto b = bossOfNpc_.find(actors_[(std::size_t)actor].row.id);
        if (b == bossOfNpc_.end()) return false;
        own = &b->second;
    }
    bool any = false;
    for (int r : it->second) {
        const MechRow& m = cfg_.mechRows[(std::size_t)r];
        if (!active_.count(m.boss) || (before && !before->count(m.boss))) continue;
        if (own &&std::find(own->begin(), own->end(), m.boss) == own->end()) continue;
        if (kind == 7 && m.domain && m.domain != domain) continue;
        any = true;
        auto last = mechLast_.find(r);
        const long long w = m.windowMs > 0 ? m.windowMs / 20 : 0;
        if (last != mechLast_.end() && (w > 0 ? std::llabs(c - last->second) < w : c == last->second)) continue;
        mechLast_[r] = c;
        Ev e; e.type = kTypeMech; e.c = c; e.f = { m.boss, m.kind, m.id, actor }; e.text = m.key; e.a1 = actor; e.mech = r;
        evs.push_back(std::move(e));
        ++mechs_;
    }
    return any;
}

// dict.mechs[boss] holds every key the log has used, so each new key rewrites the boss's object.
void Recorder::mechDict(const Ev& e) {
    if (e.mech < 0 || e.mech >= (int)cfg_.mechRows.size() || !dict_.emplace(dictKey(30, e.mech), true).second) return;
    const MechRow& m = cfg_.mechRows[(std::size_t)e.mech];
    auto& keys = mechDict_[m.boss];
    keys[m.key] = "{\"label\":" + jstr(m.label) + ",\"tactic\":" + std::to_string(m.tactic) + ",\"kind\":" + std::to_string(m.kind) + "}";
    std::string o = "{";
    for (const auto& kv : keys) { if (o.size() > 1) o += ","; o += jstr(kv.first) + ":" + kv.second; }
    dictLine("mechs", m.boss, o + "}");
}

void Recorder::emitLine(const std::string& line) {
    logBytes_ += (long long)line.size() + 1;
    pending_.push_back(line);
}

std::string Recorder::eventJson(const Ev& e) const {
    if (e.type == kTypeMech && e.f.size() == 4)
        return "[\"mech\"," + std::to_string(e.c) + "," + std::to_string(e.f[0]) + "," + jstr(e.text) + "," + std::to_string(e.f[1]) + "," +
               std::to_string(e.f[2]) + "," + std::to_string(e.f[3]) + "]";
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
           (uploadAs_.empty() ? std::string() : ",\"uploadAs\":" + jstr(uploadAs_)) + ",\"companion\":false},\"clock\":{\"c0\":" + std::to_string(c0_) + ",\"wall0\":" + std::to_string(wall0_) +
           ",\"phase\":" + std::to_string(phase_) + ",\"tick0\":" + std::to_string(haveTickOff_ ? tickOff_ : -1) + "}}";
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
    if (auto it = abilityAt_.find(st); it != abilityAt_.end()) d = &cfg_.abilities[(std::size_t)it->second];
    auto sint = [&](int p, int def) { return cfg_.names.structInt ? cfg_.names.structInt(st, p, def) : def; };
    std::string name = plainText(d ? d->name : (cfg_.names.structStr ? cfg_.names.structStr(st, 2794) : std::string()));
    if (name.empty() && st == kGcdStruct) name = "Global cooldown";
    const int stat = sint(2806, d ? d->style : 0);
    const char* style = stat == 1 ? "melee" : stat == 3 ? "ranged" : stat == 4 ? "magic" : stat == 6 ? "typeless" : stat == 29 ? "necromancy" : "";
    std::string o = "{\"name\":" + jstr(name) + ",\"icon\":" + std::to_string(d ? d->icon : sint(2802, 0)) + ",\"style\":\"" + style +
                    "\",\"cd\":" + std::to_string(d ? d->cdTicks : sint(2796, 0)) + ",\"varc\":" + std::to_string(d ? d->startVarc : 0);
    std::vector<int> fam;
    if (d && d->startVarc > 0) for (const auto& a : cfg_.abilities) if (a.startVarc == d->startVarc) fam.push_back(a.structId);
    if (auto cs = coSent_.find(st); cs != coSent_.end()) { fam.push_back(st); fam.insert(fam.end(), cs->second.begin(), cs->second.end()); }
    std::sort(fam.begin(), fam.end()); fam.erase(std::unique(fam.begin(), fam.end()), fam.end());
    if (fam.size() > 1) { o += ",\"family\":["; for (std::size_t i = 0; i < fam.size(); ++i) { if (i) o += ","; o += std::to_string(fam[i]); } o += "]"; }
    if (const int anim = sint(2914, 0)) o += ",\"anim\":" + std::to_string(anim);
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
        const std::string raw = cfg_.names.structStr ? cfg_.names.structStr(st, 2794) : std::string();
        std::string name = raw;
        const std::size_t cut = name.find('<');
        if (cut != std::string::npos) name = name.substr(0, cut);
        const int type = cfg_.names.structInt ? cfg_.names.structInt(st, 8109, -1) : -1;
        const int icon = cfg_.names.structInt ? cfg_.names.structInt(st, 2802, 0) : 0;
        const int item = !icon && cfg_.names.structInt ? cfg_.names.structInt(st, 4677, 0) : 0;   // no sprite: the item it comes from
        // a buff whose only name is its effect ("Melee basic abilities generate 1.5x adrenaline") goes by the ability
        // that draws the same icon (Meteor Strike); the effect is kept as its description
        std::string desc;
        int words = 0; bool inWord = false;
        for (char ch : name) { const bool sp = ch == ' '; if (!sp && !inWord) ++words; inWord = !sp; }
        const bool sentence = cut != std::string::npos || (!name.empty() && name.back() == '.') || words > 5;
        if (icon > 0 && sentence)
            for (const auto& a : cfg_.abilities)
                if (a.icon == icon && !a.name.empty()) {
                    for (std::size_t i = 0; i < raw.size(); ++i) {   // the full text, a line break read as a sentence break
                        if (raw.compare(i, 4, "<br>") == 0) { if (!desc.empty() && desc.back() != '.') desc += '.'; desc += ' '; i += 3; continue; }
                        if (raw[i] == '<') { const std::size_t e = raw.find('>', i); if (e == std::string::npos) break; i = e; continue; }
                        desc += raw[i];
                    }
                    name = a.name;
                    break;
                }
        dictLine("buffs", st, "{\"name\":" + jstr(name) + ",\"type\":" + std::to_string(type) + ",\"icon\":" + std::to_string(icon) +
                 (item > 0 ? ",\"item\":" + std::to_string(item) : std::string()) + (desc.empty() ? std::string() : ",\"desc\":" + jstr(desc)) + "}");
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
    case kTypeItem: case kTypeEof: {
        const int item = (int)e.f[2];
        if (item < 0 || !once(kTypeItem, item)) return;
        dictLine("items", item, "{\"name\":" + jstr(plainText(cfg_.names.itemName ? cfg_.names.itemName(item) : std::string())) + "}");
        break;
    }
    case kTypePerks:
        for (int k = 1; k <= 7; k += 2) {
            const int perk = (int)e.f[(std::size_t)k];
            if (perk <= 0 || !once(kTypePerks, perk)) continue;
            const int ranks = cfg_.names.perkRanks ? cfg_.names.perkRanks(perk) : 0;
            dictLine("perks", perk, "{\"name\":" + jstr(plainText(cfg_.names.perkName ? cfg_.names.perkName(perk) : std::string())) +
                     ",\"ranks\":" + std::to_string(ranks) + "}");
        }
        break;
    case kTypeMech: mechDict(e); break;
    default: break;
    }
}

std::string Recorder::dictJson() const {
    static const char* kinds[] = { "abilities", "buffs", "hitmarks", "seqs", "encounters", "trackers", "mechs", "items", "perks" };
    std::string o = "{";
    for (int k = 0; k < 9; ++k) {
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
    uploadAs_ = cfg_.uploadAs ? cfg_.uploadAs() : std::string();
    startedAt_ = wallMs; c0_ = c; wall0_ = wallMs;
    written_ = 0; logBytes_ = 0; gaps_ = 0; readFails_ = 0; reads_ = 0; seq_ = 0;
    tail_.clear(); dict_.clear(); dictJson_.clear(); mechDict_.clear(); fights_.clear();
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
    for (const auto& b : buffs_) {
        if (!b.known || !b.on) continue;
        // the buff the server named for the var; a shared var nothing named yet waits for its queued row
        const int st = b.cur ? b.cur : b.owner ? b.owner : b.group.size() > 1 ? 0 : b.structId;
        if (st) add(7, sc, { st, 1, -1, b.last, -1 });
    }
    for (const auto& kv : trackers_) add(9, sc, { kv.first >> 16, (kv.first >> 8) & 0xFF, kv.first & 0xFF, kv.second });
    std::vector<Ev> gearPrev[3];                         // item, perks, stored special
    {
        std::unordered_set<std::uint64_t> seen;
        auto take = [&](const Ev& e) {
            const int g = e.type == kTypeItem ? 0 : e.type == kTypePerks ? 1 : e.type == kTypeEof ? 2 : -1;
            if (g < 0 || !e.key || baseline_.count(e.key) || !seen.insert(e.key).second || e.prev.empty()) return;
            Ev s; s.type = e.type; s.c = sc; s.f = e.prev; s.key = e.key;
            gearPrev[g].push_back(std::move(s));
        };
        for (const auto& e : preroll_) take(e);
        for (const auto& e : pending) take(e);
    }
    for (const auto& kv : items_) if (kv.second.first >= 0) add(kTypeItem, sc, { kv.first >> 8, kv.first & 0xFF, kv.second.first, kv.second.second });
    for (auto& e : gearPrev[0]) snap.push_back(std::move(e));
    for (const auto& kv : perks_) {
        const auto& pk = kv.second;
        if (std::any_of(pk.begin(), pk.end(), [](int v) { return v != 0; }))
            add(kTypePerks, sc, { kv.first, pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], pk[6], pk[7] });
    }
    for (auto& e : gearPrev[1]) snap.push_back(std::move(e));
    for (const auto& kv : eof_) if (kv.second >= -1) add(kTypeEof, sc, { kv.first >> 8, kv.first & 0xFF, kv.second });
    for (auto& e : gearPrev[2]) snap.push_back(std::move(e));
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
    actors_.clear(); byUid_.clear(); baseline_.clear(); preroll_.clear(); dict_.clear(); mechDict_.clear();
    selfIdx_ = -1;
    sceneFresh_ = true; active_.clear(); tileSeen_.clear(); mechLast_.clear(); varLast_.clear(); items_.clear(); perks_.clear(); eof_.clear(); eofInit_ = false;
    haveMap_ = false; selfX_ = selfY_ = -1;
}

void Recorder::Close(const char* why) {
    if (inFight_) endFight(lastC_, why);
    if (logOpen_) {
        Ev m; m.type = 16; m.c = lastC_; m.f = { std::strcmp(why, "rotation") == 0 ? 3 : 4 }; m.text = why;
        writeEvent(m);
        emitLine("[\"end\",{\"endedAt\":" + std::to_string(lastWallMs_) + ",\"endBy\":\"" + std::string(why) + "\",\"readFails\":" + std::to_string(readFails_) +
                 ",\"reads\":" + std::to_string(reads_) + ",\"gaps\":" + std::to_string(gaps_) + ",\"phase\":" + std::to_string(phase_) + ",\"events\":" + std::to_string(written_) + "}]");
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

// One script 6570 record (struct, start tick, end tick, 1, 1). An ability's record is its cast; records with the
// same ticks in one batch are one cast of abilities sharing a cooldown (the head is the one whose style
// matches the last cast's); a record with start == end for a struct already rowed in that tick resets it.
// The global cooldown's records and other structs make no row: the varc path covers the stamp.
void Recorder::netCast(std::vector<Ev>& evs, const NetEv& n, long long c, std::vector<NetRow>& rows) {
    const int st = n.a[0], start = n.a[1], end = n.a[2];
    if (st <= 0 || start <= 0 || end < start || isGcdStruct(st)) return;
    auto ab = abilityAt_.find(st);
    if (ab == abilityAt_.end()) return;
    const AbilityDef& d = cfg_.abilities[(std::size_t)ab->second];
    if (haveTickOff_ && c - (long long)start * 30 - tickOff_ > kRestoreSlack) { ++restores_; return; }
    for (auto& r : rows) {
        if (r.startTick != start) continue;
        if (r.head == st && start == end) return;                                    // a reset of the row just made
        if (r.endTick != end) continue;
        auto& co = coSent_[r.head];
        if (std::find(co.begin(), co.end(), st) == co.end()) { co.push_back(st); dict_.erase(dictKey(1, r.head)); }
        if (r.ev < evs.size() && r.style0 && d.style == r.style0) {
            const auto hi = abilityAt_.find(r.head);
            const int h = hi != abilityAt_.end() ? cfg_.abilities[(std::size_t)hi->second].style : 0;
            if (h != r.style0) {                                                     // this one is the cast
                auto& nco = coSent_[st];
                if (std::find(nco.begin(), nco.end(), r.head) == nco.end()) nco.push_back(r.head);
                evs[r.ev].f[0] = st; r.head = st; r.name = plainText(d.name); lastStyle_ = d.style;
            }
        }
        return;
    }
    // the cooldown varc change of this cast, seen by this pass or the one before: its cycles are exact
    long long at = c; int ready = (int)(c + (long long)(end - start) * 30);
    for (auto it = castQ_.begin(); it != castQ_.end(); ++it) {
        if (it->src != 1 || d.startVarc <= 0 || it->startVarc != d.startVarc || std::llabs(it->c - c) > kCastMatch) continue;
        at = it->c; if (it->ready > 0) ready = it->ready;
        castQ_.erase(it);
        break;
    }
    rows.push_back({ start, end, evs.size(), st, at, plainText(d.name), lastStyle_ });
    push(evs, 1, at, { st, ready, 0 });
    netCasts_.push_back({ at, st, d.startVarc, start, end });
    while (!netCasts_.empty() && netCasts_.front().c < at - 3000) netCasts_.pop_front();
    if (d.style) lastStyle_ = d.style;
    phase_ = (int)(((at % 30) + 30) % 30);
    ++netCasts0_;
}

// Graphics (13), projectiles (14) and sounds (19) from the packets, and the mechanics they and the
// hint arrows and var sets match. Cycles come from the capture time against the pass's paired read.
void Recorder::FeedNet(const std::vector<NetEv>& in, std::uint32_t clock, long long wallMs) {
    if (in.empty()) return;
    std::vector<Ev> evs;
    auto cycleOf = [&](const NetEv& n) {
        const long long d = n.wallMs - wallMs;
        return (long long)clock + (d >= 0 ? d / 20 : -((19 - d) / 20));
    };
    // the server tick offset: the smallest (cycle - tick * 30) of the last two minutes, this batch included;
    // a cast's own record sits on it, a cooldown restored at login or a teleport is older
    long long newest = 0;
    for (const NetEv& n : in) {
        if (n.kind != NetEv::Script || n.id != kScriptCooldown || n.a[1] <= 0) continue;
        const long long c = cycleOf(n);
        tickOffs_.push_back({ c, c - (long long)n.a[1] * 30 });
        if (c > newest) newest = c;
    }
    while (!tickOffs_.empty() && (tickOffs_.front().first < newest - 6000 || tickOffs_.size() > 32)) tickOffs_.pop_front();
    if (!tickOffs_.empty()) {
        tickOff_ = tickOffs_.front().second;
        for (const auto& t : tickOffs_) tickOff_ = std::min(tickOff_, t.second);
        haveTickOff_ = true;
    }
    std::vector<NetRow> rows;                        // the ability rows this call made, for co-sent records and channels
    bool namedBuff = false;
    for (const NetEv& n : in) if (n.kind == NetEv::Script && n.id == kScriptBuffTimer) { namedBuff = true; break; }
    for (const NetEv& n : in) {
        ++netSeen_;
        const long long c = cycleOf(n);
        if (n.kind == NetEv::Script && n.id == kScriptCooldown) { netCast(evs, n, c, rows); continue; }
        if (n.kind == NetEv::Script && n.id == kScriptBuffTimer) {   // which struct a shared timer var now belongs to
            auto bs = buffOfStruct_.find(n.a[0]);
            if (bs != buffOfStruct_.end()) {
                BuffVar& b = buffs_[(std::size_t)bs->second];
                if (b.group.size() > 1) b.owner = n.a[0];
            }
            continue;
        }
        if (n.kind == NetEv::Script && n.id == kScriptChannel) {
            const std::string name = plainText(n.text);
            long long at = c;
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) if (it->name == name) { at = it->c; break; }
            push(evs, 8, at, { n.a[0], n.a[1] }, selfIdx_, -1, name);
            continue;
        }
        switch (n.kind) {
        case NetEv::Gfx: {
            if (n.id < 0 || n.id == kGfxReticle1 || n.id == kGfxReticle2) break;
            if (n.ref) {
                const int ai = actorOfRef(n.ref, n.index);
                push(evs, 13, c, { ai, n.id }, ai); ++netGfx_;
                if (!matchMech(evs, 2, n.id, c, ai)) matchMech(evs, 3, n.id, c, ai);
            } else {
                if (tileRepeat(13, n.id, n)) { ++netDup_; break; }
                push(evs, 13, c, { -1, n.id }); ++netGfx_;
                if (!matchMech(evs, 3, n.id, c, -1)) matchMech(evs, 2, n.id, c, -1);
            }
            break;
        }
        case NetEv::Proj: {
            if (n.id < 0) break;
            const int from = (n.tile && n.x >= 0) ? actorOnTile(n.x, n.y, n.plane) : -1;
            push(evs, 14, c, { from, -1, n.id }, from); ++netProj_;
            matchMech(evs, 4, n.id, c, from);
            break;
        }
        case NetEv::Sound: {
            if (n.id < 0) break;
            if (n.tile && tileRepeat(kTypeSound, n.id, n)) { ++netDup_; break; }
            push(evs, kTypeSound, c, { n.id, n.tile ? 1 : 0 }); ++netSound_;
            matchMech(evs, 5, n.id, c, -1);
            break;
        }
        case NetEv::Hint: {         // rows name the NPC pointed at, or 0 for any arrow
            const int ai = n.ref ? actorOfRef(n.ref, n.index) : -1;
            if (ai >= 0 && actors_[(std::size_t)ai].row.type == "npc") matchMech(evs, 6, actors_[(std::size_t)ai].row.id, c, ai);
            matchMech(evs, 6, 0, c, ai);
            break;
        }
        case NetEv::Varp: case NetEv::Varbit: case NetEv::Varc: {
            if (mechIdx_.find(mechKey(7, n.id)) == mechIdx_.end()) break;
            const int dom = n.kind == NetEv::Varp ? 1 : n.kind == NetEv::Varbit ? 2 : 3;
            const std::uint64_t vk = ((std::uint64_t)dom << 32) | (std::uint32_t)n.id;
            auto lv = varLast_.find(vk);
            const bool changed = lv == varLast_.end() || lv->second != n.value;
            varLast_[vk] = n.value;
            if (changed && n.value != 0) matchMech(evs, 7, n.id, c, -1, dom);   // back to 0 is the mechanic ending or a reset
            break;
        }
        default: break;
        }
    }
    if (namedBuff) flushBuffs(evs, true);          // shared-var rows of this pass now carry the named struct
    routeLate(evs);
}

// A buff row. A var one struct owns is written now; a shared var's on row waits for the struct the server names
// (4252 lands in the same packet pass, drained after this one) and is written by FeedNet or the next pass.
void Recorder::pushBuff(std::vector<Ev>& evs, BuffVar& b, const QueuedBuff& q) {
    if (b.group.size() > 1 && q.on) { buffQ_.push_back(q); return; }
    if (!q.on) {   // a timer that ends before anything named it: its waiting row is dropped and so is the end
        bool waiting = false;
        for (auto it = buffQ_.begin(); it != buffQ_.end();) {
            if (it->buff == q.buff) { waiting = true; it = buffQ_.erase(it); } else ++it;
        }
        if (waiting && !b.cur) return;
    }
    const int st = q.on ? (b.owner ? b.owner : b.structId) : (b.cur ? b.cur : (b.owner ? b.owner : b.structId));
    if (q.on) b.cur = st;
    push(evs, 7, q.c, { st, q.on, q.start, q.end, q.stacks });
}

// Queued shared-var rows: every one when `all`, else the ones from an earlier pass, under the named struct or the default.
void Recorder::flushBuffs(std::vector<Ev>& evs, bool all) {
    std::vector<QueuedBuff> keep;
    for (const auto& q : buffQ_) {
        BuffVar& b = buffs_[(std::size_t)q.buff];
        // a shared timer already running when recording began was named before it: wait for the server to name
        // it again (it repeats every few seconds) rather than guess, up to a minute, then the table's first
        const bool unnamed = q.start < 0 && !b.cur && !b.owner;
        if (unnamed && passes_ - q.pass < kBuffNameWaitPasses) { keep.push_back(q); continue; }
        if (!all && q.pass >= passes_) { keep.push_back(q); continue; }
        const int st = b.owner ? b.owner : b.structId;
        if (b.cur && b.cur != st && q.start < 0) push(evs, 7, q.c, { b.cur, 0, -1, q.c, -1 });   // the var changed hands while on
        b.cur = st;
        push(evs, 7, q.c, { st, 1, q.start, q.end, q.stacks });
    }
    buffQ_ = std::move(keep);
}

// Varc changes from an earlier pass that no 6570 row took: the ability's family head (src 1); the global
// cooldown stamp (src 3) only when no ability row sits within a tick of it.
void Recorder::flushCasts(std::vector<Ev>& evs) {
    std::vector<QueuedCast> keep;
    for (const auto& q : castQ_) {
        if (q.pass >= passes_) { keep.push_back(q); continue; }
        if (q.src == 3) {
            bool cast = false;
            for (const auto& nc : netCasts_) if (std::llabs(nc.c - q.c) <= 30) { cast = true; break; }
            for (const auto& o : castQ_) if (o.src == 1 && std::llabs(o.c - q.c) <= 30) { cast = true; break; }
            if (cast) continue;
        }
        push(evs, 1, q.c, { q.structId, q.ready, q.src });
        if (q.src == 1) ++varcCasts_;
    }
    castQ_ = std::move(keep);
}

void Recorder::Feed(const Tick& t) {
    ++reads_;
    if (!t.ok) { ++readFails_; return; }
    if (t.status != 30) { if (logOpen_ || inFight_) Close("logout"); lastWallMs_ = t.wallMs; return; }
    const long long c = (long long)t.clock;
    std::vector<Ev> evs;
    if (lastWallMs_ && t.wallMs - lastWallMs_ > 1000) { ++gaps_; push(evs, 16, c, { 2 }, -1, -1, "gap " + std::to_string(t.wallMs - lastWallMs_) + " ms"); }
    lastWallMs_ = t.wallMs; lastC_ = c;
    ++passes_;
    flushCasts(evs);
    flushBuffs(evs, false);
    std::unordered_map<int, int> vp, vc;
    for (const auto& kv : t.varps) vp[kv.first] = kv.second;
    for (const auto& kv : t.varcs) vc[kv.first] = kv.second;

    // actors: presence, animation, target, life points, stats, bars, hits
    std::vector<int> myHits; bool hitOnSelf = false; long long actionC = -1;
    std::vector<char> seen(actors_.size(), 0);
    std::vector<std::pair<int, bool>> hitThisPass;   // (actor index, by me)
    struct Cue { int kind, id; long long c; int actor; };
    std::vector<Cue> cues;                           // NPC animation starts and spawns, matched once the scope is known
    // a rebuilt map or a long jump of the local player (a teleport, an instance) shows a new scene at once
    if (t.haveMapBase) {
        if (haveMap_ && (t.mapBaseX != mapX_ || t.mapBaseY != mapY_)) sceneFresh_ = true;
        haveMap_ = true; mapX_ = t.mapBaseX; mapY_ = t.mapBaseY;
    }
    for (const auto& a : t.actors) {
        if (!a.self) continue;
        if (selfX_ >= 0 && (std::abs(a.tx - selfX_) > 32 || std::abs(a.ty - selfY_) > 32)) sceneFresh_ = true;
        selfX_ = a.tx; selfY_ = a.ty;
        break;
    }
    const bool fresh = sceneFresh_;                  // the scene's first pass: nothing in it spawned or started now
    for (const auto& a : t.actors) {
        const int i = actorIndex(a, c);
        if ((std::size_t)i >= seen.size()) seen.resize((std::size_t)i + 1, 0);
        seen[(std::size_t)i] = 1;
        ActorState& s = actors_[(std::size_t)i];
        if (!s.present) { s.present = true; push(evs, 11, c, { i, 1 }, i); if (a.type == 1 && !fresh) cues.push_back({ 8, a.id, c, i }); }
        s.lastSeenC = c; s.tx = a.tx; s.ty = a.ty; s.tplane = a.plane;
        if (a.self) selfIdx_ = i;
        // an NPC's start count moves on every animation start, a repeat of the same one included; the
        // start cycle is used when it is near the pass
        bool started = false; long long sc = c;
        if (a.type == 1 && a.haveAnimStart) {
            if (s.animCount != kUnknown && a.animCount != s.animCount && a.anim >= 0) {
                started = true;
                if (a.animCycle >= c - 300 && a.animCycle <= c + 300) sc = a.animCycle;
            }
            s.animCount = a.animCount;
        }
        if (a.anim != s.anim || started) {
            const bool firstSight = s.anim == kUnknown;
            s.anim = a.anim; push(evs, 2, sc, { i, a.anim }, i);
            if (a.type == 1 && a.anim >= 0 && !(fresh && firstSight)) cues.push_back({ 1, a.anim, sc, i });
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

    // boss mechanics seen on the actors; a spawn counts for a boss already in scope before it (a boss and
    // its adds walking into view together are no spawn)
    const std::unordered_set<int> before = std::move(active_);
    updateScope();
    if (!t.actors.empty()) sceneFresh_ = false;
    for (const auto& q : cues) matchMech(evs, q.kind, q.id, q.c, q.actor, 0, q.kind == 8 ? &before : nullptr);

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
        castCs.push_back(kv.second);
        phase_ = (int)(((kv.second % 30) + 30) % 30);
        bool named = false;                          // a 6570 row already made this cast
        for (const auto& nc : netCasts_) if (nc.startVarc == kv.first && std::llabs(nc.c - kv.second) <= kCastMatch) { named = true; break; }
        if (named) continue;
        auto en = vc.find(cs.endVarc);
        castQ_.push_back({ cs.structId, kv.second, en != vc.end() ? en->second : -1, kv.first, 1, passes_ });
    }
    if (gcdC >= 0) {
        bool paired = false;
        for (long long x : castCs) if (std::llabs(x - gcdC) <= 1) paired = true;
        if (!paired) {
            auto en = vc.find(kGcdEnd);
            castQ_.push_back({ kGcdStruct, gcdC, en != vc.end() ? en->second : -1, kGcdStart, 3, passes_ });
            castCs.push_back(gcdC); phase_ = (int)(((gcdC % 30) + 30) % 30);
        }
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
        const int bi = (int)(&b - &buffs_[0]);
        if (!b.known) {                                   // first sight: a running timer is an on row with its start unknown
            b.known = true; b.last = v; b.on = v > c;
            if (b.on) pushBuff(evs, b, { bi, c, 1, -1, v, cok ? stacks : -1, passes_ });
            continue;
        }
        if (v == b.last) { if (b.on && v <= c) b.on = false; continue; }
        b.last = v;
        const bool on = v > c;
        pushBuff(evs, b, { bi, c, on ? 1 : 0, (on && !b.on) ? c : -1, v, cok ? stacks : -1, passes_ });
        b.on = on;
    }

    // inventory (93) and equipment (94): one row per slot that changed; the first read only sets the state
    if (t.haveItems) {
        const bool first = items_.empty();
        std::set<int> itemMoved;                       // (container << 8) | slot whose item changed this pass
        auto scan = [&](int cont, const std::vector<std::pair<int, int>>& slots) {
            for (std::size_t s = 0; s < slots.size() && s < 64; ++s) {
                const int key = (cont << 8) | (int)s;
                const auto now = slots[s];
                auto it = items_.find(key);
                if (it != items_.end() && it->second == now) continue;
                const bool had = it != items_.end();
                const std::pair<int, int> old = had ? it->second : std::pair<int, int>{ -1, 0 };
                items_[key] = now;
                itemMoved.insert(key);
                if (first || (!had && now.first < 0)) continue;
                push(evs, kTypeItem, c, { cont, (long long)s, now.first, now.second });
                if (old.first >= 0) evs.back().prev = { cont, (long long)s, old.first, old.second };
            }
        };
        if (!t.equip.empty()) scan(94, t.equip);
        if (!t.inv.empty()) scan(93, t.inv);
        // the perks of each worn item: a swap between two copies of one item shows only here
        const bool firstPerks = perks_.empty();
        for (std::size_t s = 0; s < t.equipPerks.size() && s < 64; ++s) {
            const auto& pk = t.equipPerks[s];
            auto it = perks_.find((int)s);
            if (it != perks_.end() && it->second == pk) continue;
            const bool had = it != perks_.end();
            const std::array<int, 8> old = had ? it->second : std::array<int, 8>{};
            perks_[(int)s] = pk;
            const bool none = std::all_of(pk.begin(), pk.end(), [](int v) { return v == 0; });
            if (firstPerks || (!had && none)) continue;
            push(evs, kTypePerks, c, { (long long)s, pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], pk[6], pk[7] });
            if (std::any_of(old.begin(), old.end(), [](int v) { return v != 0; }))
                evs.back().prev = { (long long)s, old[0], old[1], old[2], old[3], old[4], old[5], old[6], old[7] };
        }
        // the special each Essence of Finality stores, worn or carried; written after the slot's item row, and only
        // while an amulet is there (an item row for the slot ends what the slot stored)
        const bool firstEof = !eofInit_;
        auto scanEof = [&](int cont, const std::vector<int>& idx) {
            for (std::size_t s = 0; s < idx.size() && s < 64; ++s) {
                if (idx[s] == -3) continue;
                const int w = idx[s] == -2 ? -2 : idx[s] > 0 && cfg_.names.eofWeapon ? std::max(-1, cfg_.names.eofWeapon(idx[s])) : -1;
                const int key = (cont << 8) | (int)s;
                auto it = eof_.find(key);
                const int was = it != eof_.end() ? it->second : -2;
                if (was == w && !itemMoved.count(key)) continue;
                eof_[key] = w;
                if (firstEof || w == -2) continue;
                push(evs, kTypeEof, c, { cont, (long long)s, w });
                if (was >= -1) evs.back().prev = { cont, (long long)s, was };
            }
        };
        if (!t.equipEof.empty()) scanEof(94, t.equipEof);
        if (!t.invEof.empty()) scanEof(93, t.invEof);
        if (!t.equipEof.empty() || !t.invEof.empty()) eofInit_ = true;
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
            if (castAtTarget) for (long long x : castCs) startC = std::min(startC, x);   // their rows come a pass later
        } else if (enc_ != kUnknown && enc_ != -1 && (encBegan || !logOpen_)) by = "encounter";
        if (by) {
            // a cast or hit stamped before the last fight ended (the kill registers a few seconds after the boss
            // dies) belongs to that fight's tail: the new fight starts where it ended
            if (!fights_.empty() && startC < fights_.back().end) startC = fights_.back().end;
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
                    ",\"itemSlots\":" + std::to_string(items_.size()) + ",\"itemsHeld\":" + std::to_string(std::count_if(items_.begin(), items_.end(), [](const auto& kv) { return kv.second.first >= 0; })) +
                    ",\"castVarsSeen\":" + std::to_string(castsSeen) + ",\"buffVarsSeen\":" + std::to_string(buffsSeen) + ",\"buffsOn\":" + std::to_string(buffsOn) +
                    ",\"casts\":{\"script\":" + std::to_string(netCasts0_) + ",\"varc\":" + std::to_string(varcCasts_) + ",\"restores\":" + std::to_string(restores_) +
                    ",\"tick0\":" + std::to_string(haveTickOff_ ? tickOff_ : -1) + "}" +
                    ",\"trackerCells\":" + std::to_string(trackers_.size()) + ",\"preroll\":" + std::to_string(preroll_.size()) + ",\"baseline\":" + std::to_string(baseline_.size()) +
                    ",\"net\":{\"seen\":" + std::to_string(netSeen_) + ",\"gfx\":" + std::to_string(netGfx_) + ",\"proj\":" + std::to_string(netProj_) +
                    ",\"sound\":" + std::to_string(netSound_) + ",\"repeats\":" + std::to_string(netDup_) + ",\"mechs\":" + std::to_string(mechs_) + ",\"bosses\":[";
    {
        std::vector<int> b(active_.begin(), active_.end()); std::sort(b.begin(), b.end());
        for (std::size_t k = 0; k < b.size(); ++k) { if (k) o += ","; o += std::to_string(b[k]); }
    }
    o += "]},\"actors\":[";
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
