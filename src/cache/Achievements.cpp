#include "Achievements.h"
#include "Probe.h"
#include <algorithm>

#include "Constants.h"
#include "Store.h"
#include "SqliteIndexFile.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtx::cache {

Store* CacheStore();
std::mutex& AchievementsMutex();
void EnsureCacheInit();

namespace {

using std::uint8_t; using std::uint16_t; using std::uint32_t;

struct Reader {
    const uint8_t* d; std::size_t n, p = 0;
    bool eof() const { return p >= n; }
    uint8_t  u8()  { return p < n ? d[p++] : 0; }
    uint16_t u16() { uint16_t v = (uint16_t)u8() << 8; v |= u8(); return v; }
    uint32_t u24() { uint32_t v = u8(); v = (v << 8) | u8(); v = (v << 8) | u8(); return v; }
    uint32_t u32v(){ uint32_t v = u8(); for (int i = 0; i < 3; ++i) v = (v << 8) | u8(); return v; }
    uint16_t usmart() { uint16_t i = u8(); if (i >= 0x80) { i -= 0x80; return (uint16_t)((i << 8) | u8()); } return i; }
    // smart32: high bit set -> 4 bytes & 0x7fffffff; else 2 bytes (0x7fff sentinel -> 0).
    uint32_t smart32() { if (p < n && (d[p] & 0x80)) return u32v() & 0x7FFFFFFFu; uint32_t v = u16(); return v == 0x7FFF ? 0 : v; }
    std::string pstr() {
        static const uint32_t kHigh[32] = {
            0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
            0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
            0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
            0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178 };
        u8();   // skip the pad byte
        std::string s;
        while (p < n && d[p] != 0) {
            unsigned ch = d[p++];
            uint32_t cp = (ch < 0x80) ? ch : (ch < 0xA0) ? kHigh[ch - 0x80] : ch;
            if (cp < 0x80) s += (char)cp;
            else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
            else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        }
        if (p < n) ++p;   // skip the terminating 0
        return s;
    }
};

// Every completion entry starts with its requirement group byte. A group is met when op 30's count for it
// is reached (all of its entries without op 30), child achievements count in their group, and op 31 says
// how many groups are needed (all of them without it).
struct Req { uint32_t value; std::string desc; std::vector<int> varbits; int group = 0; };
struct SkillReq { int skill; int level; int group = 0; };
struct BitReq { int id; int bit; std::string desc; int group = 0; };   // op 23: bit of VARP id; op 25/36: bit of VARBIT id's value
struct VarpReq { std::vector<int> varps; uint32_t value = 0; std::string desc; int group = 0; };   // op 13: SUM of live VARPs >= value
struct GroupId { int group; int id; };   // u24 = group byte + u16 id
struct Ach {
    int id; std::string name, desc, reward;
    int cat = -1, subcat = -1, sprite = -1, points = 0, hidden = 0, combatMastery = -1;
    int needGroups = -1, grace = -1;
    // op 19 present = free-to-play; absent = members.
    bool members = true; bool named = false; bool disabled = false;
    std::vector<Req> reqs;
    std::vector<GroupId> subach;    // op 15: child achievements
    std::vector<GroupId> prereqs;   // op 11: achievements that unlock this one (never part of completion)
    std::vector<GroupId> quests;    // op 21: quests to complete
    std::vector<SkillReq> skills;   // op 12
    std::vector<BitReq> bitreqs23;  // op 23 (varp bits)
    std::vector<BitReq> bitreqs25;  // op 25 / 36 (varbit bits)
    std::vector<VarpReq> varpreqs;  // op 13
    std::vector<int> subreqCount;   // op 30: entries needed, per group
};
GroupId group_id(uint32_t v) { return { (int)(v >> 16), (int)(v & 0xFFFF) }; }
// Stop "opcodes" for a misread record: every record ends with op 0 exactly at its last byte.
constexpr int kStopOverrun = 256;    // ran off the end before op 0 (a field read wider than it is)
constexpr int kStopTrailing = 258;   // op 0 with bytes still to come (a field read narrower than it is)

Ach decode_one(int id, const std::vector<uint8_t>& b, int* stop_op = nullptr) {
    Ach a; a.id = id;
    if (stop_op) *stop_op = 0;
    Reader r{ b.data(), b.size() };
    for (;;) {
        if (r.eof()) { probe::g_stop = (int)r.p; if (stop_op) *stop_op = kStopOverrun; break; }
        int op = r.u8();
        if (op == 0) {
            // An end marker with bytes still to come means a field was read with the wrong shape.
            if (r.p < r.n) { probe::g_stop = (int)r.p; if (stop_op) *stop_op = kStopTrailing; }
            break;
        }
        switch (op) {
            case 1: a.name = r.pstr(); a.named = true; break;
            case 2: { int cnt = r.u8(); if (cnt < 0) cnt = 0; if (cnt > 16) cnt = 16;
                      for (int i = 0; i < cnt; ++i) { r.u8(); std::string s = r.pstr(); if (i == 0) a.desc = std::move(s); }
                      break; }
            case 3: a.cat = r.u16(); break;
            case 4: a.sprite = (int)r.smart32(); break;
            case 5: a.points = r.u8(); break;
            case 6: a.grace = r.u16(); break;   // day number until which the game still counts it for parents
            case 7: a.reward = r.pstr(); break;
            case 8: { int c = r.usmart(); for (int i = 0; i < c; ++i) { r.u8(); r.u8(); r.pstr(); r.u8(); r.u16(); } break; }  // skill req (ironman)
            case 9: case 10: { int c = r.u8(); for (int i = 0; i < c; ++i) { r.u8(); r.smart32(); r.pstr(); r.u8(); r.u16(); } break; }
            case 11: { int c = r.u8(); for (int i = 0; i < c; ++i) a.prereqs.push_back(group_id(r.u24())); break; }
            case 12: { int c = r.usmart(); for (int i = 0; i < c; ++i) { int g = r.u8(); int lvl = r.u8(); r.pstr(); r.u8(); int sk = r.u16(); a.skills.push_back({ sk, lvl, g }); } break; }
            // op 13: op 14's shape but the u16 ids are VARP ids; multi-id entries SUM.
            case 13: { int c = r.usmart(); for (int i = 0; i < c; ++i) { VarpReq q; q.group = r.u8(); q.value = r.smart32(); q.desc = r.pstr(); int m = r.u8(); for (int j = 0; j < m; ++j) q.varps.push_back(r.u16()); a.varpreqs.push_back(std::move(q)); } break; }
            // op 14 = varbit counters with u16 ids; op 35 (since 950-1) the same with u24 ids.
            case 14: case 35: { int c = r.usmart(); for (int i = 0; i < c; ++i) { Req q; q.group = r.u8(); q.value = r.smart32(); q.desc = r.pstr(); int m = r.u8(); for (int j = 0; j < m; ++j) q.varbits.push_back(op == 35 ? (int)r.u24() : (int)r.u16()); a.reqs.push_back(std::move(q)); } break; }
            case 15: { int c = r.usmart(); for (int i = 0; i < c; ++i) a.subach.push_back(group_id(r.u24())); break; }
            case 16: a.subcat = r.u16(); break;
            case 17: a.disabled = true; break;   // retired: the game no longer judges it
            case 18: a.hidden = r.u8(); break;
            case 19: a.members = false; break;   // op 19 present = free-to-play
            case 20: { int c = r.u8(); for (int i = 0; i < c; ++i) r.u24(); break; }   // more unlocking achievements
            case 21: { int c = r.u8(); for (int i = 0; i < c; ++i) a.quests.push_back(group_id(r.u24())); break; }
            // op 23 = bit of VARP id; op 25 = bit of VARBIT id's value; op 36 (since 950-1) = op 25 with a u24 id.
            case 23: case 25: case 36: { int c = r.usmart(); for (int i = 0; i < c; ++i) { BitReq q; q.group = r.u8(); q.id = (op == 36) ? (int)r.u24() : (int)r.u16(); r.u8(); q.desc = r.pstr(); q.bit = r.u8(); (op == 23 ? a.bitreqs23 : a.bitreqs25).push_back(std::move(q)); } break; }
            case 26: a.combatMastery = (int)r.u16(); r.u8(); a.name = r.pstr(); a.named = true; break;
            case 27: break;
            case 28: { int c = r.u8(); for (int i = 0; i < c; ++i) r.u8(); break; }
            case 29: r.u8(); break;
            case 30: { int c = r.u8(); for (int i = 0; i < c; ++i) a.subreqCount.push_back((int)r.usmart()); break; }  // how many subreqs needed
            case 31: a.needGroups = r.u8(); break;
            case 32: r.u8(); r.u8(); r.u8(); break;
            // op 33 = what unlocks the achievement (varbit counters, u24 ids), never part of completion.
            case 33: { int c = r.usmart(); for (int i = 0; i < c; ++i) { r.u8(); r.smart32(); r.pstr(); int m = r.u8(); for (int j = 0; j < m; ++j) r.u24(); } break; }
            case 37: r.u8(); break;
            case 38: r.u8(); break;
            default:
                if (op == probe::g_op && r.p + (std::size_t)probe::g_len <= r.n) { r.p += (std::size_t)probe::g_len; break; }   // unknown-opcode probe (Probe.h)
                probe::g_stop = (int)r.p; probe::g_tail = (int)(r.n - r.p);
                if (stop_op) *stop_op = op; return a;   // unknown trailing opcode -> stop, keep what we have
        }
    }
    probe::g_tail = (int)(r.n - r.p);
    return a;
}

void json_str(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (u < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", u); out += b; }
                else out += c;
        }
    }
    out += '"';
}

std::string                          g_ach_json;
bool                                 g_ach_built = false;

}  // namespace

// Called by CacheReader with g_mu (AchievementsMutex) held when the game cache updates.
void AchievementsResetLocked() {
    g_ach_json.clear();
    g_ach_built = false;
}

const std::string& AchievementsJson() {
    std::lock_guard<std::mutex> lk(AchievementsMutex());
    if (g_ach_built) return g_ach_json;
    EnsureCacheInit();

    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    // Not memoised: the index can still open, or its reference table become readable, later.
    if (!idx || !idx->ready()) { g_ach_json = "[]"; return g_ach_json; }
    g_ach_built = true;

    std::string out = "[";
    bool first = true;
    const auto& entries = idx->ref().entries();
    for (int arc = 0; arc < (int)entries.size(); ++arc) {
        for (int fid : entries[arc].valid_file_ids) {
            auto b = idx->ReadFile(arc, fid);
            if (b.empty()) continue;
            int id = (arc << 7) | fid;
            Ach a = decode_one(id, b);
            if (!a.named) continue;   // skip nameless/meta-only entries

            if (!first) out += ',';
            first = false;
            out += "{\"id\":" + std::to_string(a.id);
            out += ",\"name\":"; json_str(out, a.name);
            if (!a.desc.empty())   { out += ",\"desc\":";   json_str(out, a.desc); }
            if (!a.reward.empty()) { out += ",\"reward\":"; json_str(out, a.reward); }
            if (a.cat >= 0)    out += ",\"cat\":" + std::to_string(a.cat);
            if (a.subcat >= 0) out += ",\"subcat\":" + std::to_string(a.subcat);
            if (a.sprite >= 0) out += ",\"sprite\":" + std::to_string(a.sprite);
            if (a.points > 0)  out += ",\"points\":" + std::to_string(a.points);
            if (a.hidden > 0)  out += ",\"hidden\":" + std::to_string(a.hidden);
            if (a.members)     out += ",\"members\":1";
            if (a.combatMastery >= 0) out += ",\"cm\":" + std::to_string(a.combatMastery);
            if (a.disabled)           out += ",\"disabled\":1";
            if (a.grace >= 0)         out += ",\"grace\":" + std::to_string(a.grace);
            if (a.needGroups >= 0)    out += ",\"needGroups\":" + std::to_string(a.needGroups);
            auto grp = [&](int g) { if (g) out += ",\"g\":" + std::to_string(g); };
            // ids with a parallel group list, written only when some group is not 0
            auto ids = [&](const char* key, const char* gkey, const std::vector<GroupId>& v) {
                if (v.empty()) return;
                out += std::string(",\"") + key + "\":[";
                bool anyG = false;
                for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += ','; out += std::to_string(v[i].id); if (v[i].group) anyG = true; }
                out += "]";
                if (!anyG) return;
                out += std::string(",\"") + gkey + "\":[";
                for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += ','; out += std::to_string(v[i].group); }
                out += "]";
            };
            if (!a.reqs.empty()) {
                out += ",\"reqs\":[";
                for (std::size_t i = 0; i < a.reqs.size(); ++i) {
                    if (i) out += ',';
                    out += "{\"value\":" + std::to_string(a.reqs[i].value) + ",\"desc\":";
                    json_str(out, a.reqs[i].desc);
                    out += ",\"varbits\":[";
                    for (std::size_t j = 0; j < a.reqs[i].varbits.size(); ++j) {
                        if (j) out += ',';
                        out += std::to_string(a.reqs[i].varbits[j]);
                    }
                    out += "]";
                    grp(a.reqs[i].group);
                    out += "}";
                }
                out += "]";
            }
            if (!a.skills.empty()) {
                out += ",\"skills\":[";
                for (std::size_t i = 0; i < a.skills.size(); ++i) {
                    if (i) out += ',';
                    out += "[" + std::to_string(a.skills[i].skill) + "," + std::to_string(a.skills[i].level);
                    if (a.skills[i].group) out += "," + std::to_string(a.skills[i].group);
                    out += "]";
                }
                out += "]";
            }
            ids("prev", "prevG", a.prereqs);
            ids("subach", "subachG", a.subach);
            ids("quests", "questsG", a.quests);
            if (!a.bitreqs23.empty()) {
                out += ",\"reqsvpb\":[";
                for (std::size_t i = 0; i < a.bitreqs23.size(); ++i) {
                    if (i) out += ',';
                    out += "{\"vp\":" + std::to_string(a.bitreqs23[i].id) +
                           ",\"bit\":" + std::to_string(a.bitreqs23[i].bit) + ",\"n\":";
                    json_str(out, a.bitreqs23[i].desc);
                    grp(a.bitreqs23[i].group);
                    out += "}";
                }
                out += "]";
            }
            if (!a.bitreqs25.empty()) {
                out += ",\"reqs25\":[";
                for (std::size_t i = 0; i < a.bitreqs25.size(); ++i) {
                    if (i) out += ',';
                    out += "{\"vb\":" + std::to_string(a.bitreqs25[i].id) +
                           ",\"bit\":" + std::to_string(a.bitreqs25[i].bit) + ",\"n\":";
                    json_str(out, a.bitreqs25[i].desc);
                    grp(a.bitreqs25[i].group);
                    out += "}";
                }
                out += "]";
            }
            if (!a.varpreqs.empty()) {
                out += ",\"reqsvp\":[";
                bool f2 = true;
                for (const auto& q : a.varpreqs) {
                    if (q.varps.empty()) continue;
                    if (!f2) out += ',';
                    f2 = false;
                    out += "{\"vp\":" + std::to_string(q.varps[0]);
                    if (q.varps.size() > 1) {
                        out += ",\"vps\":[";
                        for (std::size_t i = 0; i < q.varps.size(); ++i) { if (i) out += ','; out += std::to_string(q.varps[i]); }
                        out += "]";
                    }
                    out += ",\"v\":" + std::to_string(q.value);
                    if (!q.desc.empty()) { out += ",\"n\":"; json_str(out, q.desc); }
                    grp(q.group);
                    out += "}";
                }
                out += "]";
            }
            if (!a.subreqCount.empty()) {
                out += ",\"needN\":[";
                for (std::size_t i = 0; i < a.subreqCount.size(); ++i) { if (i) out += ','; out += std::to_string(a.subreqCount[i]); }
                out += "]";
            }
            out += "}";
        }
    }
    out += "]";
    g_ach_json = std::move(out);
    return g_ach_json;
}

void AchievementsProbeUnknown(std::string& log) {
    EnsureCacheInit();
    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    if (!idx || !idx->ready()) { log += "achievements: index not open\n"; return; }
    std::vector<std::vector<uint8_t>> recs; int total = 0; std::unordered_map<int, int> stops;
    probe::g_op = -1;
    const auto& entries = idx->ref().entries();
    for (int arc = 0; arc < (int)entries.size(); ++arc)
        for (int fid : entries[arc].valid_file_ids) {
            auto b = idx->ReadFile(arc, fid);
            if (b.empty()) continue;
            ++total; int st = 0; (void)decode_one((arc << 7) | fid, b, &st);
            if (st > 0) { ++stops[st]; if (recs.size() < 400) recs.push_back(std::move(b)); }
        }
    log += "achievements: " + std::to_string(total) + " records, " + std::to_string(recs.size()) + " failing\n";
    for (const auto& kv : stops) log += "  stop opcode " + std::to_string(kv.first) + " x" + std::to_string(kv.second) + "\n";
    for (const auto& kv : stops) {
        const int op = kv.first;
        std::vector<const std::vector<uint8_t>*> mine;
        for (const auto& b : recs) { probe::g_op = -1; int st = 0; (void)decode_one(0, b, &st); if (st == op) mine.push_back(&b); }
        log += "  opcode " + std::to_string(op) + ": " + std::to_string(mine.size()) + " records; first records (hex from the opcode byte):\n";
        for (size_t i = 0; i < mine.size() && i < 24; ++i) {
            probe::g_op = -1; int st = 0; (void)decode_one(0, *mine[i], &st);
            int from = probe::g_stop > 0 ? probe::g_stop - 1 : 0; char hx[6];
            log += "    stop@" + std::to_string(from) + "/" + std::to_string(mine[i]->size()) + " whole record: ";
            for (int k = 0; k < (int)mine[i]->size() && k < 400; ++k) { std::snprintf(hx, sizeof(hx), (k == from ? "|%02x " : "%02x "), (*mine[i])[k]); log += hx; }
            log += "\n";
        }
        if (op > 255) continue;   // an overrun or trailing bytes, not an unknown opcode: no payload to size
        std::vector<std::pair<int, int>> res;
        for (int L = 0; L <= 200; ++L) {
            probe::g_op = op; probe::g_len = L; int okc = 0, okAny = 0;
            for (const auto* b : mine) { int st = 0; (void)decode_one(0, *b, &st); if (st == 0) { ++okAny; if (probe::g_tail == 0) ++okc; } }
            res.push_back({ okc * 1000 + okAny, L });
        }
        std::sort(res.rbegin(), res.rend());
        for (int i = 0; i < 6 && i < (int)res.size(); ++i)
            log += "    L=" + std::to_string(res[i].second) + " exact=" + std::to_string(res[i].first / 1000) + " clean=" + std::to_string(res[i].first % 1000) + " of " + std::to_string(mine.size()) + "\n";
    }
    probe::g_op = -1;
}

void AchievementsParseHealth(int& ok, int& total, int& stop_op, int& stop_n) {
    ok = total = stop_n = 0; stop_op = -1;
    EnsureCacheInit();
    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    if (!idx || !idx->ready()) return;
    std::unordered_map<int, int> stops;
    const auto& entries = idx->ref().entries();
    for (int arc = 0; arc < (int)entries.size(); ++arc) {
        for (int fid : entries[arc].valid_file_ids) {
            auto b = idx->ReadFile(arc, fid);
            if (b.empty()) continue;
            ++total;
            int st = 0;
            (void)decode_one((arc << 7) | fid, b, &st);
            if (st == 0) ++ok; else ++stops[st];
        }
    }
    for (const auto& kv : stops)
        if (kv.second > stop_n) { stop_op = kv.first; stop_n = kv.second; }
}

void QuestCapeQuestNames(std::vector<std::string>& out) {
    EnsureCacheInit();
    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    if (!idx) return;
    auto readAch = [&](int id) { return idx->ReadFile(id >> 7, id & 0x7f); };   // index 57 addressing
    Ach cape = decode_one(1, readAch(1));            // achievement id 1 = Quest Cape (category 4745)
    for (const auto& sub : cape.subach) {
        Ach q = decode_one(sub.id, readAch(sub.id));
        if (q.named && !q.name.empty()) out.push_back(q.name);
    }
}

}  // namespace rtx::cache
