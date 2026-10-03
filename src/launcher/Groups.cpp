#include "Groups.h"
#include "BridgeUtil.h"
#include "Http.h"
#include "Link.h"
#include "LootBosses.h"
#include "Update.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace rtx::launcher::groups {
namespace {

constexpr std::size_t kQueueCap = 512;
constexpr std::size_t kBodyCap  = 64 * 1024;
constexpr std::size_t kAnswerCap = 2 * 1024 * 1024;

std::mutex g_mu;
std::deque<std::string> g_out;
std::atomic<std::uint32_t> g_nextId{ 1 };
std::atomic<int> g_epoch{ 0 };
std::atomic<bool> g_want{ false };

// Drops invalid UTF-8 (and cuts a trailing partial sequence) so a line always parses as JSON text.
std::string clean_utf8(const std::string& s) {
    std::string out; out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) { ++i; continue; }
        bool ok = true;
        for (std::size_t k = 1; k < n; ++k) if (((unsigned char)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
        if (ok) out.append(s, i, n);
        i += ok ? n : 1;
    }
    return out;
}

void push(std::string line) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_out.size() >= kQueueCap) g_out.pop_front();
    g_out.push_back(std::move(line));
}

std::vector<http::Header> headers(bool json) {
    std::vector<http::Header> h = {
        { "Accept", "application/json" },
        { "User-Agent", "RuneToolsX/" + running_version() },
        { "X-RTX-Version", running_version() },
    };
    if (json) h.push_back({ "Content-Type", "application/json" });
    std::string auth = link::AuthHeader();
    if (!auth.empty()) h.push_back({ "Authorization", auth });
    return h;
}

bool path_ok(const std::string& p) {
    if (p.rfind("/api/groups/", 0) != 0 || p.size() > 512) return false;
    for (unsigned char c : p) if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '#') return false;
    return p.find("..") == std::string::npos;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// The body as JSON when it is JSON, else null plus the text, so the page never has to guess.
void push_result(std::uint32_t id, const http::Response& r) {
    std::string out = "{\"kind\":\"result\",\"id\":" + std::to_string(id) + ",\"status\":" + std::to_string(r.status) +
                      ",\"ok\":" + (r.ok ? "true" : "false") + ",\"body\":";
    JsonValue v;
    if (r.body.size() <= kAnswerCap && json_parse(r.body, v) && (v.kind == JsonValue::Object || v.kind == JsonValue::Array)) out += r.body;
    else { out += "null,\"text\":\"" + json_escape(clean_utf8(r.body.substr(0, 300))) + "\""; if (!r.detail.empty()) out += ",\"detail\":\"" + json_escape(clean_utf8(r.detail)) + "\""; }
    out += "}";
    push(std::move(out));
}

void events_loop(int epoch) {
    int backoff = 3000;
    while (g_epoch.load() == epoch && g_want.load()) {
        auto hdrs = headers(false);
        for (auto& h : hdrs) if (h.name == "Accept") h.value = "text/event-stream";
        std::string buf, cur_ev; bool gotData = false; std::size_t bytes = 0; int events = 0;
        const auto t0 = std::chrono::steady_clock::now();
        // the channel is pinned to the account at connect: linking or unlinking drops the stream and the next one carries the new identity
        const std::string openedAuth = link::AuthHeader();
        rtx::log::Launcher("groups: event stream opening (epoch " + std::to_string(epoch) + ", " + (openedAuth.empty() ? "anonymous" : "linked") + ")");
        auto r = http::Stream(kUpdateHost, L"/api/groups/events", hdrs,
            [&](const char* d, std::size_t n) -> bool {
                if (g_epoch.load() != epoch || !g_want.load()) return false;
                if (link::AuthHeader() != openedAuth) return false;
                gotData = true; bytes += n;
                buf.append(d, n);
                std::size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("event:", 0) == 0) {
                        cur_ev = line.substr(6);
                        if (!cur_ev.empty() && cur_ev.front() == ' ') cur_ev.erase(cur_ev.begin());
                    } else if (line.rfind("data:", 0) == 0) {
                        std::string dat = line.substr(5);
                        if (!dat.empty() && dat.front() == ' ') dat.erase(dat.begin());
                        JsonValue v;
                        bool okJson = dat.size() < 65536 && json_parse(dat, v) && (v.kind == JsonValue::Object || v.kind == JsonValue::Array);
                        if (!cur_ev.empty() && cur_ev.size() < 32) ++events;
                        if (!cur_ev.empty() && cur_ev.size() < 32)
                            push("{\"kind\":\"event\",\"event\":\"" + json_escape(cur_ev) + "\",\"data\":" + (okJson ? dat : std::string("null")) + "}");
                    } else if (line.empty()) cur_ev.clear();
                }
                if (buf.size() > 128 * 1024) buf.clear();
                return true;
            },
            [](int status) { return status == 200; });
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        rtx::log::Launcher("groups: event stream ended after " + std::to_string(ms) + " ms: " + std::to_string(bytes) + " bytes, " + std::to_string(events) +
                           " events, status " + std::to_string(r.status) + (r.ok ? ", ok" : ", not ok") + (r.detail.empty() ? "" : ", " + r.detail) +
                           (g_epoch.load() != epoch ? ", superseded" : "") + (g_want.load() ? "" : ", unsubscribed") +
                           (link::AuthHeader() != openedAuth ? ", account changed" : ""));
        if (g_epoch.load() != epoch || !g_want.load()) break;
        if (link::AuthHeader() != openedAuth) { backoff = 3000; }
        // The server ends every stream after 14 minutes (the edge would cut it at 15): a stream that carried data
        // and closed cleanly is reopened at once, without telling the page it was ever down. Anything else backs off.
        const bool rotated = gotData && r.ok && r.status == 200;
        backoff = rotated ? 300 : (gotData || (r.ok && r.status == 200)) ? 3000 : (backoff * 2 > 60000 ? 60000 : backoff * 2);
        if (!rotated) push("{\"kind\":\"event\",\"event\":\"stream\",\"data\":{\"connected\":false}}");
        for (int slept = 0; slept < backoff && g_epoch.load() == epoch && g_want.load(); slept += 250)
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

// ---- snapshot ------------------------------------------------------------------------------------
const char* const kSkills[29] = { "Attack", "Defence", "Strength", "Constitution", "Ranged", "Prayer", "Magic", "Cooking",
    "Woodcutting", "Fletching", "Fishing", "Firemaking", "Crafting", "Smithing", "Mining", "Herblore", "Agility", "Thieving",
    "Slayer", "Farming", "Runecrafting", "Hunter", "Construction", "Summoning", "Dungeoneering", "Divination", "Invention",
    "Archaeology", "Necromancy" };
const int kSkillCap[29] = { 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 120, 99, 99, 120, 120, 99, 99, 99, 99,
    120, 99, 0, 120, 120 };   // 0 = not shown (the elite curve differs)

int level_for_xp(long long xp, int cap) {
    if (cap <= 0 || xp < 0) return 0;
    double acc = 0; int level = 1;
    for (int n = 1; n < cap; ++n) {
        acc += std::floor(n + 300.0 * std::pow(2.0, n / 7.0));
        if ((long long)std::floor(acc / 4.0) <= xp) level = n + 1; else break;
    }
    return level;
}

long long field(const std::vector<int>& ids, const std::vector<int>& vals, const int* t) {
    if (t[0] <= 0) return 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] != t[0]) continue;
        const unsigned long long v = (unsigned long long)(std::uint32_t)vals[i];
        const int width = t[2] - t[1] + 1;
        const unsigned long long mask = width >= 32 ? 0xFFFFFFFFull : ((1ull << width) - 1);
        return (long long)((v >> t[1]) & mask);
    }
    return 0;
}

std::string kills_json(std::uint32_t pid) {
    std::vector<int> ids;
    auto add = [&](const int* t) { if (t[0] > 0) { for (int v : ids) if (v == t[0]) return; ids.push_back(t[0]); } };
    for (const auto& b : loot::kBosses) { add(b.kc); add(b.pr); add(b.kc2); add(b.pr2); }
    std::vector<int> vals;
    if (!rtx::reader::Varps(pid, ids, vals) || vals.size() != ids.size()) return "{}";
    std::string out = "{"; bool first = true;
    auto put = [&](const std::string& key, long long n) {
        if (!first) out.push_back(',');
        first = false;
        out += "\"" + json_escape(key) + "\":" + std::to_string(n);
    };
    for (const auto& b : loot::kBosses) {
        std::string k1 = b.name; if (b.m1[0]) { k1 += "|"; k1 += b.m1; }
        put(k1, field(ids, vals, b.kc) + 60000 * field(ids, vals, b.pr));
        if (b.kc2[0] > 0) put(std::string(b.name) + "|" + b.m2, field(ids, vals, b.kc2) + 60000 * field(ids, vals, b.pr2));
    }
    return out + "}";
}

// "Precise 6" from the perk reader back to "Precise" plus the rank the card draws as pips.
std::string perk_base_name(const std::string& name, int rank, int ranks) {
    if (ranks > 1 && rank > 0) {
        const std::string suffix = " " + std::to_string(rank);
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            return name.substr(0, name.size() - suffix.size());
    }
    return name;
}

long long num(const JsonValue* v) { return v && v->kind == JsonValue::Number ? std::atoll(v->text.c_str()) : 0; }

}  // namespace

std::uint32_t Call(const std::string& method, const std::string& path, const std::string& body) {
    const std::uint32_t id = g_nextId++;
    const bool post = method == "POST";
    if ((!post && method != "GET") || !path_ok(path) || body.size() > kBodyCap) {
        push("{\"kind\":\"result\",\"id\":" + std::to_string(id) + ",\"status\":0,\"ok\":false,\"body\":null,\"text\":\"bad request\"}");
        return id;
    }
    http::Enqueue([id, post, path, body]() {
        http::Response r = post ? http::PostJson(kUpdateHost, widen(path), headers(true), body.empty() ? "{}" : body)
                                : http::Fetch(kUpdateHost, widen(path), headers(false), kAnswerCap);
        push_result(id, r);
    });
    return id;
}

std::string Take() {
    std::deque<std::string> got;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        got.swap(g_out);
    }
    std::string out = "[";
    for (std::size_t i = 0; i < got.size(); ++i) { if (i) out.push_back(','); out += got[i]; }
    out += "]";
    return out;
}

void Subscribe(bool on) {
    g_want.store(on);
    const int epoch = ++g_epoch;      // retires any loop still running
    if (!on) return;
    std::thread([epoch] { guarded("groups events", [&] { events_loop(epoch); }); }).detach();
}

std::string SnapshotJson(std::uint32_t pid) {
    std::string rsn; int xp[29]; bool haveXp = false;
    for (const auto& p : rtx::reader::LastPresence()) {
        if (p.pid != pid) continue;
        rsn = p.display_name; haveXp = p.have_xp;
        std::memcpy(xp, p.xp, sizeof(xp));
        break;
    }
    if (rsn.empty()) rsn = rtx::reader::AccountKey(pid);
    if (rsn.empty()) return "null";

    JsonValue social, player, equip, perks;
    json_parse(rtx::reader::SocialJson(pid), social);
    json_parse(rtx::reader::PlayerInfoJson(pid), player);
    json_parse(rtx::reader::EquipmentJson(pid), equip);
    json_parse(rtx::reader::PerksJson(pid), perks);

    long long world = num(social.get("world")); if (world < 0) world = 0;
    long long combat = num(player.get("combat")); if (combat < 0) combat = 0;
    const JsonValue* inv = player.get("in");
    const bool inWorld = inv && inv->kind == JsonValue::Bool && inv->flag;
    const JsonValue* eqp = equip.get("present");
    const bool equipRead = eqp && eqp->kind == JsonValue::Bool && eqp->flag;

    std::string out = "{\"rsn\":\"" + json_escape(rsn) + "\",\"world\":" + std::to_string(world) +
                      ",\"combat\":" + std::to_string(combat) + ",\"in\":" + (inWorld ? "true" : "false") +
                      ",\"equipmentRead\":" + (equipRead ? "true" : "false") +
                      ",\"clientVersion\":\"" + json_escape(running_version()) + "\"";

    out += ",\"levels\":{";
    if (haveXp) {
        bool first = true;
        for (int i = 0; i < 29; ++i) {
            if (!kSkillCap[i] || xp[i] < 0) continue;
            if (!first) out.push_back(','); first = false;
            out += "\"" + std::string(kSkills[i]) + "\":" + std::to_string(level_for_xp(xp[i], kSkillCap[i]));
        }
    }
    out += "}";

    // perks by worn slot, then every worn item with its perks
    std::map<long long, std::string> perkBySlot;
    if (const JsonValue* items = perks.get("items")) {
        for (const auto& it : items->items) {
            if (num(it.get("container")) != 94) continue;
            std::string ps;
            if (const JsonValue* pl = it.get("perks")) {
                for (const auto& p : pl->items) {
                    const long long rank = num(p.get("rank")), ranks = num(p.get("ranks"));
                    const std::string name = perk_base_name(p.str("name"), (int)rank, (int)ranks);
                    if (name.empty()) continue;
                    if (!ps.empty()) ps.push_back(',');
                    ps += "{\"name\":\"" + json_escape(name) + "\",\"rank\":" + std::to_string(rank) + ",\"ranks\":" + std::to_string(ranks) + "}";
                }
            }
            perkBySlot[num(it.get("slot"))] = ps;
        }
    }
    out += ",\"equipment\":[";
    if (const JsonValue* items = equip.get("items")) {
        bool first = true;
        for (const auto& it : items->items) {
            if (it.kind != JsonValue::Array || it.items.size() < 4) continue;   // [slot, id, stack, name]
            const long long slot = num(&it.items[0]), id = num(&it.items[1]);
            if (id <= 0) continue;
            if (!first) out.push_back(','); first = false;
            out += "{\"slot\":" + std::to_string(slot) + ",\"id\":" + std::to_string(id) + ",\"name\":\"" +
                   json_escape(it.items[3].kind == JsonValue::String ? it.items[3].text : std::string()) + "\",\"perks\":[";
            auto pk = perkBySlot.find(slot);
            if (pk != perkBySlot.end()) out += pk->second;
            out += "]}";
        }
    }
    out += "],\"kills\":" + kills_json(pid) + "}";
    return out;
}

void Shutdown() {
    g_want.store(false);
    ++g_epoch;
}

}  // namespace rtx::launcher::groups
