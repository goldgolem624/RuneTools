#include "Achievements.h"

#include "Constants.h"
#include "Store.h"
#include "SqliteIndexFile.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtx::cache {

// Shared cache state, exposed by CacheReader.cpp so this decoder reuses the one Store + lock.
Store* CacheStore();
std::mutex& AchievementsMutex();
void EnsureCacheInit();

namespace {

using std::uint8_t; using std::uint16_t; using std::uint32_t;

// Big-endian reader over a cache file, mirroring the reference decode's Buf helpers exactly so the
// opcode offsets never drift. Bounds-checked: past the end every read yields 0 / "" and the
// decode loop stops on the next opcode read.
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
    // Padded string -> UTF-8. Cache strings are CP-1252; a lone high byte (curly apostrophe,
    // em-dash, accented letter) is invalid UTF-8 and would make JSStringCreateWithUTF8CString
    // reject the WHOLE achievements JSON -> empty panel. Convert here, same map as
    // InputStream::ReadString.
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

struct Req { uint32_t value; std::string desc; std::vector<int> varbits; };
struct SkillReq { int skill; int level; };
struct BitReq { int id; int bit; std::string desc; };   // op 23: bit of VARP id; op 25: bit of VARBIT id's value
struct VarpReq { std::vector<int> varps; uint32_t value = 0; std::string desc; };   // op 13: SUM of live VARPs >= value
struct Ach {
    int id; std::string name, desc, reward;
    int cat = -1, subcat = -1, sprite = -1, points = 0, hidden = 0, combatMastery = -1;
    // members defaults TRUE: op 19's presence marks an achievement free-to-play,
    // so entries without it are members content.
    bool members = true; bool named = false;
    std::vector<Req> reqs; std::vector<int> subach; std::vector<int> prereqs;
    std::vector<SkillReq> skills;   // op 12
    std::vector<BitReq> bitreqs23;  // op 23 (varp bits)
    std::vector<BitReq> bitreqs25;  // op 25 (varbit bits)
    std::vector<VarpReq> varpreqs;  // op 13
    std::vector<int> subreqCount;   // op 30: how many subreqs must be satisfied (per group)
};

// Decode one achievement file (the reference decode opcode loop, re-validated in full on build 949:
// 5009/5009 decode clean and the Quest Cape's 273 sub-achievement names all match quest
// config names). On an unknown opcode keep what was parsed and stop; stop_op (optional)
// receives that opcode (0 = clean end) for the cache parse-health check.
Ach decode_one(int id, const std::vector<uint8_t>& b, int* stop_op = nullptr) {
    Ach a; a.id = id;
    if (stop_op) *stop_op = 0;
    Reader r{ b.data(), b.size() };
    for (;;) {
        if (r.eof()) break;
        int op = r.u8();
        if (op == 0) break;
        switch (op) {
            case 1: a.name = r.pstr(); a.named = true; break;
            // Descriptions: COUNT(u8), then count x [tag(u8) + padded string]. First = standard
            // description; the rest are group-ironman variants.
            case 2: { int cnt = r.u8(); if (cnt < 0) cnt = 0; if (cnt > 16) cnt = 16;
                      for (int i = 0; i < cnt; ++i) { r.u8(); std::string s = r.pstr(); if (i == 0) a.desc = std::move(s); }
                      break; }
            case 3: a.cat = r.u16(); break;
            case 4: a.sprite = (int)r.smart32(); break;
            case 5: a.points = r.u8(); break;
            case 6: r.u16(); break;
            case 7: a.reward = r.pstr(); break;
            case 8: { int c = r.usmart(); for (int i = 0; i < c; ++i) { r.u8(); r.u8(); r.pstr(); r.u8(); r.u16(); } break; }  // skill req (ironman)
            case 9: case 10: { int c = r.u8(); for (int i = 0; i < c; ++i) { r.u8(); r.smart32(); r.pstr(); r.u8(); r.u16(); } break; }
            case 11: { int c = r.u8(); for (int i = 0; i < c; ++i) a.prereqs.push_back((int)r.u24()); break; }   // previous achievements (judged by the game's req walk)
            // skill req: (u8 ?, u8 LEVEL, name, u8 ?, u16 SKILL).
            case 12: { int c = r.usmart(); for (int i = 0; i < c; ++i) { r.u8(); int lvl = r.u8(); r.pstr(); r.u8(); int sk = r.u16(); a.skills.push_back({ sk, lvl }); } break; }
            // op 13 = same SHAPE as op 14 but the u16 ids are VARP ids, not varbits
            // (data-verified: Very Important tiers need values 400-4500, far beyond any
            // varbit those ids could be). Multi-id entries SUM, like op 14: Treasure
            // Seeker I-III sum clue counts across varps 7802-7806.
            case 13: { int c = r.usmart(); for (int i = 0; i < c; ++i) { VarpReq q; r.u8(); q.value = r.smart32(); q.desc = r.pstr(); int m = r.u8(); for (int j = 0; j < m; ++j) q.varps.push_back(r.u16()); a.varpreqs.push_back(std::move(q)); } break; }
            case 14: { int c = r.usmart(); for (int i = 0; i < c; ++i) { Req q; r.u8(); q.value = r.smart32(); q.desc = r.pstr(); int m = r.u8(); for (int j = 0; j < m; ++j) q.varbits.push_back(r.u16()); a.reqs.push_back(std::move(q)); } break; }
            case 15: { int c = r.usmart(); for (int i = 0; i < c; ++i) a.subach.push_back((int)r.u24()); break; }
            case 16: a.subcat = r.u16(); break;
            case 17: break;
            case 18: a.hidden = r.u8(); break;
            case 19: a.members = false; break;   // op 19 present = free-to-play
            case 20: { int c = r.u8(); for (int i = 0; i < c; ++i) r.u24(); break; }
            case 21: { int c = r.u8(); for (int i = 0; i < c; ++i) r.u24(); break; }
            // packed bit req: (u8 ?, u16 id, u8 stepsize, name, u8 BIT). The u16's id SPACE
            // depends on the opcode:
            //   op 23 = bit BIT of VARP id (Archaeology mysteries: varp 9302/9303 bitmasks,
            //           the hand-verified ARCH_MYSTERIES table in panel_metalbank.js)
            //   op 25 = bit BIT of VARBIT id's VALUE (ach 1787 "I Can't Stop Eating Them!":
            //           varbit 43924 = varp 8525 [0-6], live value 127 = all 7 flavours;
            //           "A Bit TOO Familiar" packs bits 0-11 of the 12-bit varbit 42410)
            case 23: case 25: { int c = r.usmart(); for (int i = 0; i < c; ++i) { BitReq q; r.u8(); q.id = r.u16(); r.u8(); q.desc = r.pstr(); q.bit = r.u8(); (op == 23 ? a.bitreqs23 : a.bitreqs25).push_back(std::move(q)); } break; }
            // combat_mastery_category: tier id (Easy..Grandmaster), then (build 949) a byte
            // and the achievement NAME as a padded string -- combat-mastery achievements
            // carry their name HERE instead of op 1.
            case 26: a.combatMastery = (int)r.u16(); r.u8(); a.name = r.pstr(); a.named = true; break;
            case 27: break;
            case 28: { int c = r.u8(); for (int i = 0; i < c; ++i) r.u8(); break; }
            case 29: r.u8(); break;
            case 30: { int c = r.u8(); for (int i = 0; i < c; ++i) a.subreqCount.push_back((int)r.usmart()); break; }  // how many subreqs needed
            case 31: r.u8(); break;
            case 32: r.u8(); r.u8(); r.u8(); break;
            case 35: break;
            case 37: r.u8(); break;
            case 38: r.u8(); break;
            default: if (stop_op) *stop_op = op; return a;   // unknown trailing opcode -> stop, keep what we have
        }
    }
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

const std::string& AchievementsJson() {
    std::lock_guard<std::mutex> lk(AchievementsMutex());
    if (g_ach_built) return g_ach_json;
    g_ach_built = true;
    EnsureCacheInit();

    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    if (!idx) { g_ach_json = "[]"; return g_ach_json; }

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
                    out += "]}";
                }
                out += "]";
            }
            // Skill and prerequisite requirements: the game's requirement walk (script19623 /
            // ACHIEVEMENT_REQSTATE) judges SEVEN kinds; these two were decoded and then
            // dropped, so the panel could neither list nor judge them and any achievement
            // mixing them with var-based reqs misjudged its completion.
            if (!a.skills.empty()) {
                out += ",\"skills\":[";
                for (std::size_t i = 0; i < a.skills.size(); ++i) {
                    if (i) out += ',';
                    out += "[" + std::to_string(a.skills[i].skill) + "," + std::to_string(a.skills[i].level) + "]";
                }
                out += "]";
            }
            if (!a.prereqs.empty()) {
                out += ",\"prev\":[";
                for (std::size_t i = 0; i < a.prereqs.size(); ++i) { if (i) out += ','; out += std::to_string(a.prereqs[i]); }
                out += "]";
            }
            if (!a.subach.empty()) {
                out += ",\"subach\":[";
                for (std::size_t i = 0; i < a.subach.size(); ++i) { if (i) out += ','; out += std::to_string(a.subach[i]); }
                out += "]";
            }
            if (!a.skills.empty()) {
                out += ",\"skills\":[";
                for (std::size_t i = 0; i < a.skills.size(); ++i) {
                    if (i) out += ',';
                    out += "{\"s\":" + std::to_string(a.skills[i].skill) +
                           ",\"l\":" + std::to_string(a.skills[i].level) + "}";
                }
                out += "]";
            }
            // op 23 goes out as "reqsvpb" (varp bits) and op 25 as "reqs25" (varbit bits).
            // The legacy merged "reqs23" key is retired: panels heuristically classify it
            // when reading output from pre-split builds, so it must not reappear here.
            if (!a.bitreqs23.empty()) {
                out += ",\"reqsvpb\":[";
                for (std::size_t i = 0; i < a.bitreqs23.size(); ++i) {
                    if (i) out += ',';
                    out += "{\"vp\":" + std::to_string(a.bitreqs23[i].id) +
                           ",\"bit\":" + std::to_string(a.bitreqs23[i].bit) + ",\"n\":";
                    json_str(out, a.bitreqs23[i].desc);
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
                    // "vp" = first id; "vps" = the full sum group when there is more than one.
                    out += "{\"vp\":" + std::to_string(q.varps[0]);
                    if (q.varps.size() > 1) {
                        out += ",\"vps\":[";
                        for (std::size_t i = 0; i < q.varps.size(); ++i) { if (i) out += ','; out += std::to_string(q.varps[i]); }
                        out += "]";
                    }
                    out += ",\"v\":" + std::to_string(q.value);
                    if (!q.desc.empty()) { out += ",\"n\":"; json_str(out, q.desc); }
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

// Parse-health sweep over every achievement def. Lock-free (caller holds the shared
// cache mutex). See header.
void AchievementsParseHealth(int& ok, int& total, int& stop_op, int& stop_n) {
    ok = total = stop_n = 0; stop_op = -1;
    EnsureCacheInit();
    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    if (!idx) return;
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

// Names of the quests required for the Quest Cape (achievement id 1) = the authoritative current
// quest set. Lock-free (caller holds the shared cache mutex). See header.
void QuestCapeQuestNames(std::vector<std::string>& out) {
    EnsureCacheInit();
    auto* idx = CacheStore() ? CacheStore()->Get(kIndexAchievements) : nullptr;
    if (!idx) return;
    auto readAch = [&](int id) { return idx->ReadFile(id >> 7, id & 0x7f); };   // index 57 addressing
    Ach cape = decode_one(1, readAch(1));            // achievement id 1 = Quest Cape (category 4745)
    for (int sub : cape.subach) {
        Ach q = decode_one(sub, readAch(sub));
        if (q.named && !q.name.empty()) out.push_back(q.name);
    }
}

}  // namespace rtx::cache
