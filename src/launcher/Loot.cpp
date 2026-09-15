#include "Loot.h"
#include "LootBosses.h"
#include "BridgeUtil.h"
#include "Http.h"
#include "Link.h"
#include "Update.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace rtx::launcher::loot {
namespace {

constexpr wchar_t kHeartbeatPath[] = L"/api/client/loot/heartbeat";
constexpr wchar_t kLeavePath[] = L"/api/client/loot/leave";
constexpr wchar_t kEnterPath[] = L"/api/client/loot/enter";
constexpr int kBeatSeconds = 60;
constexpr int kXpSampleMs = 500;      // XP drops arrive at most every game tick (600 ms)
constexpr int kBossSampleMs = 5000;   // kill counts change once per kill; five seconds is plenty

// What one character has done since the last successful heartbeat, plus running totals for the UI.
struct CharState {
    int xp_now[29] = {};                      // latest skill XP read (milestones and level-ups are judged on the site)
    bool have_xp_now = false;
    int xp_events = 0;                        // XP drops not yet reported
    std::map<std::string, int> kills;         // boss key -> kills not yet reported
    long long xp_total = 0, kills_total = 0, caches_total = 0;
    bool capped = false;
    std::deque<std::string> pending_drops;    // cache names earned and not yet announced in game
};

// Per client process: the last XP array and the last boss totals, so rises can be counted.
struct PidState {
    std::string name;
    bool have_xp = false;
    int xp[29] = {};
    bool have_kc = false;
    std::map<std::string, long long> kc;      // boss key -> total kills
    std::chrono::steady_clock::time_point last_kc{};
};

std::mutex g_mu;
std::map<std::string, CharState> g_chars;     // by display name
std::map<std::string, std::chrono::steady_clock::time_point> g_in_world;   // name -> last sampled in the game world
constexpr int kLeaveAfterMs = 10000;         // gone this long (not a loading screen blip) = left
std::map<std::uint32_t, PidState> g_pids;
long long g_unopened = 0;
std::atomic<bool> g_started{ false };
std::mutex g_en_mu;
bool g_enabled = false, g_enabled_loaded = false;

std::filesystem::path enabled_path() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) > 0) {
        auto dir = std::filesystem::path(up) / L"RuneToolsX";
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        return dir / L"rune_caches.txt";
    }
    return L"runetoolsx-rune-caches.txt";
}

bool active() { return Enabled() && !link::AuthHeader().empty(); }

// Tells the site characters entered or left the game world, so the page and the session follow at once
// instead of waiting for the heartbeats to start or go quiet.
void post_names(const wchar_t* path, const char* what, const std::vector<std::string>& names) {
    if (names.empty() || !Enabled()) return;
    std::string auth = link::AuthHeader();
    if (auth.empty()) return;
    std::string body = "{\"characters\":[";
    for (size_t i = 0; i < names.size(); ++i) { if (i) body += ","; body += "\"" + json_escape(names[i]) + "\""; }
    body += "]}";
    std::vector<http::Header> hdrs = {
        { "Content-Type", "application/json" },
        { "User-Agent", "RuneToolsX/" + running_version() },
        { "X-RTX-Version", running_version() },
        { "Authorization", auth },
    };
    auto r = http::PostJson(kUpdateHost, path, hdrs, body);
    if (r.ok && r.status == 200) rtx::log::Launcher(std::string("loot: ") + what + " the game world (" + std::to_string(names.size()) + ")");
}
void post_leave(const std::vector<std::string>& names) { post_names(kLeavePath, "left", names); }
void post_enter(const std::vector<std::string>& names) { post_names(kEnterPath, "entered", names); }

// ---- boss kill counts -------------------------------------------------------------------------
std::string boss_varps_csv() {
    static std::string csv;
    if (!csv.empty()) return csv;
    std::vector<int> ids;
    auto add = [&](const int* t) { if (t[0] > 0) { for (int v : ids) if (v == t[0]) return; ids.push_back(t[0]); } };
    for (const auto& b : kBosses) { add(b.kc); add(b.pr); add(b.kc2); add(b.pr2); }
    for (size_t i = 0; i < ids.size(); ++i) { if (i) csv += ","; csv += std::to_string(ids[i]); }
    return csv;
}

// {"4534":123,"4535":-1,...} -> map
std::map<int, long long> parse_varps(const std::string& j) {
    std::map<int, long long> out;
    size_t i = 0;
    while ((i = j.find('"', i)) != std::string::npos) {
        size_t e = j.find('"', i + 1);
        if (e == std::string::npos) break;
        int id = std::atoi(j.substr(i + 1, e - i - 1).c_str());
        size_t c = j.find(':', e);
        if (c == std::string::npos) break;
        out[id] = std::strtoll(j.c_str() + c + 1, nullptr, 10);
        i = c + 1;
    }
    return out;
}

long long field(const std::map<int, long long>& vp, const int* t) {
    auto it = vp.find(t[0]);
    if (it == vp.end()) return 0;
    const unsigned long long v = (unsigned long long)(std::uint32_t)it->second;
    const int width = t[2] - t[1] + 1;
    const unsigned long long mask = width >= 32 ? 0xFFFFFFFFull : ((1ull << width) - 1);
    return (long long)((v >> t[1]) & mask);
}

std::map<std::string, long long> boss_totals(const std::map<int, long long>& vp) {
    std::map<std::string, long long> out;
    for (const auto& b : kBosses) {
        std::string k1 = b.name; if (b.m1[0]) { k1 += "|"; k1 += b.m1; }
        out[k1] = field(vp, b.kc) + 60000 * field(vp, b.pr);
        if (b.kc2[0] > 0) {
            std::string k2 = std::string(b.name) + "|" + b.m2;
            out[k2] = field(vp, b.kc2) + 60000 * field(vp, b.pr2);
        }
    }
    return out;
}

// ---- sampler -------------------------------------------------------------------------------------
void sample_once() {
    if (!active()) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::uint32_t> seen;
    std::vector<std::string> in_world;
    for (const auto& s : rtx::reader::SampleAll()) {
        if (s.status != 30 || !s.in_world || s.display_name.empty()) continue;   // 30 = In-game
        seen.push_back(s.pid);
        in_world.push_back(s.display_name);
        int xp[29];
        const bool okxp = rtx::reader::SkillsXp(s.pid, xp);
        bool read_kc = false;
        std::map<int, long long> vp;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            auto& p = g_pids[s.pid];
            read_kc = !p.have_kc || now - p.last_kc >= std::chrono::milliseconds(kBossSampleMs);
        }
        if (read_kc) vp = parse_varps(rtx::reader::VarpsJson(s.pid, boss_varps_csv()));

        std::lock_guard<std::mutex> lk(g_mu);
        auto& p = g_pids[s.pid];
        if (p.name != s.display_name) {                   // new character on this client: start fresh
            p = PidState(); p.name = s.display_name;
        }
        auto& c = g_chars[s.display_name];
        if (okxp) {
            if (p.have_xp) {
                bool rose = false;
                for (int i = 0; i < 29; ++i) if (xp[i] >= 0 && p.xp[i] >= 0 && xp[i] > p.xp[i]) rose = true;
                if (rose) { c.xp_events += 1; c.xp_total += 1; }
            }
            std::memcpy(p.xp, xp, sizeof(xp)); p.have_xp = true;
            std::memcpy(c.xp_now, xp, sizeof(xp)); c.have_xp_now = true;
        }
        if (read_kc && !vp.empty()) {
            auto totals = boss_totals(vp);
            if (p.have_kc) {
                for (const auto& kv : totals) {
                    auto it = p.kc.find(kv.first);
                    if (it == p.kc.end()) continue;
                    const long long d = kv.second - it->second;
                    if (d > 0 && d <= 50) { c.kills[kv.first] += (int)d; c.kills_total += d; }   // a jump beyond 50 is a re-read glitch, not kills
                }
            }
            p.kc = std::move(totals); p.have_kc = true; p.last_kc = now;
        }
    }
    std::vector<std::string> left, entered;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (const auto& n : in_world) { if (!g_in_world.count(n)) entered.push_back(n); g_in_world[n] = now; }
        for (auto it = g_in_world.begin(); it != g_in_world.end();) {
            if (now - it->second >= std::chrono::milliseconds(kLeaveAfterMs)) { left.push_back(it->first); it = g_in_world.erase(it); }
            else ++it;
        }
    }
    if (!entered.empty()) std::thread([entered] { guarded("loot enter", [&] { post_enter(entered); }); }).detach();
    if (!left.empty()) std::thread([left] { guarded("loot leave", [&] { post_leave(left); }); }).detach();
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto it = g_pids.begin(); it != g_pids.end();) {
        bool alive = false;
        for (auto pid : seen) if (pid == it->first) alive = true;
        it = alive ? std::next(it) : g_pids.erase(it);
    }
}

// ---- heartbeat -----------------------------------------------------------------------------------
// Reads {"n":"...","d":N,"dl":["Melee Cache",...],"x":N,"b":N,"k":N,"c":N} objects out of the
// "chars" array. Each object is flat apart from the dl string list.
void apply_response(const std::string& body) {
    size_t arr = body.find("\"chars\":[");
    if (arr == std::string::npos) return;
    std::lock_guard<std::mutex> lk(g_mu);
    size_t pos = arr + 9;
    while (true) {
        size_t o = body.find('{', pos);
        if (o == std::string::npos) break;
        size_t c = body.find('}', o);
        if (c == std::string::npos) break;
        std::string obj = body.substr(o, c - o + 1);
        std::string name = json_str(obj, "n");
        if (name.empty()) break;                  // past the chars array
        auto& st = g_chars[name];
        auto num = [&](const char* k) -> long long {
            std::string pat = std::string("\"") + k + "\":";
            size_t p = obj.find(pat);
            if (p == std::string::npos) return -1;
            p += pat.size();
            long long v = 0; bool any = false;
            while (p < obj.size() && obj[p] >= '0' && obj[p] <= '9') { v = v * 10 + (obj[p] - '0'); ++p; any = true; }
            return any ? v : -1;
        };
        const long long caches = num("k"), cap = num("c");
        if (caches >= 0) st.caches_total = caches;
        st.capped = cap == 1;
        size_t dl = obj.find("\"dl\":[");
        if (dl != std::string::npos) {
            size_t lb = obj.find('[', dl);                // scan the names from the list's opening bracket,
            size_t e = obj.find(']', lb);                 // not from the "dl" key's own quotes
            size_t q = lb;
            while (e != std::string::npos && (q = obj.find('"', q + 1)) != std::string::npos && q < e) {
                size_t q2 = obj.find('"', q + 1);
                if (q2 == std::string::npos || q2 > e) break;
                st.pending_drops.push_back(obj.substr(q + 1, q2 - q - 1));
                rtx::log::Launcher("loot: " + obj.substr(q + 1, q2 - q - 1) + " dropped for " + name);
                q = q2;
            }
        }
        pos = c + 1;
    }
    std::string pat = "\"unopened\":";
    size_t u = body.find(pat);
    if (u != std::string::npos) {
        u += pat.size();
        long long v = 0; bool any = false;
        while (u < body.size() && body[u] >= '0' && body[u] <= '9') { v = v * 10 + (body[u] - '0'); ++u; any = true; }
        if (any) g_unopened = v;
    }
}

void beat_once() {
    if (!Enabled()) return;                              // opt-in: nothing is reported while off
    std::string auth = link::AuthHeader();
    if (auth.empty()) return;
    std::vector<std::string> names;
    for (const auto& s : rtx::reader::SampleAll()) {
        if (s.status != 30 || !s.in_world || s.display_name.empty()) continue;   // 30 = In-game
        bool dup = false;
        for (const auto& n : names) if (n == s.display_name) { dup = true; break; }
        if (!dup) names.push_back(s.display_name);
    }
    if (names.empty()) return;
    // Snapshot the counts to report; they are cleared only once the site has accepted them.
    struct Rep { std::string name; int xp; std::map<std::string, int> kills; bool have_sx; int sx[29]; };
    std::vector<Rep> reps;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (const auto& n : names) {
            auto& c = g_chars[n];
            Rep r{ n, c.xp_events, c.kills, c.have_xp_now, {} };
            std::memcpy(r.sx, c.xp_now, sizeof(r.sx));
            reps.push_back(std::move(r));
        }
    }
    std::string body = "{\"characters\":[";
    for (size_t i = 0; i < reps.size(); ++i) {
        if (i) body += ",";
        body += "{\"n\":\"" + json_escape(reps[i].name) + "\",\"x\":" + std::to_string(reps[i].xp);
        if (reps[i].have_sx) {
            body += ",\"sx\":[";
            for (int s = 0; s < 29; ++s) { if (s) body += ","; body += std::to_string(reps[i].sx[s]); }
            body += "]";
        }
        body += ",\"k\":[";
        bool first = true;
        for (const auto& kv : reps[i].kills) {
            if (kv.second <= 0) continue;
            body += std::string(first ? "" : ",") + "[\"" + json_escape(kv.first) + "\"," + std::to_string(kv.second) + "]";
            first = false;
        }
        body += "]}";
    }
    body += "]}";
    std::vector<http::Header> hdrs = {
        { "Content-Type", "application/json" },
        { "User-Agent", "RuneToolsX/" + running_version() },
        { "X-RTX-Version", running_version() },
        { "Authorization", auth },
    };
    {
        std::string line = "loot beat:";
        for (const auto& rep : reps) {
            int kills = 0; for (const auto& kv : rep.kills) kills += kv.second;
            line += " " + rep.name + " xp_drops=" + std::to_string(rep.xp) + " kills=" + std::to_string(kills);
        }
        rtx::log::Launcher(line);
    }
    auto r = http::PostJson(kUpdateHost, kHeartbeatPath, hdrs, body);
    if (!r.ok) return;                                   // offline: keep counting, try next minute
    if (r.status == 401) { link::Verify(); return; }     // revoked or unlinked: let the link module find out
    if (r.status != 200) {
        rtx::log::Launcher("loot heartbeat: status " + std::to_string(r.status) + " " + r.detail);
        return;
    }
    // too soon after another beat from this PC (a second launcher, a restart): nothing was credited, so
    // keep the counts for the next beat instead of clearing them
    if (r.body.find("\"slowDown\":true") != std::string::npos) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (const auto& rep : reps) {
            auto& c = g_chars[rep.name];
            c.xp_events -= rep.xp; if (c.xp_events < 0) c.xp_events = 0;
            for (const auto& kv : rep.kills) { c.kills[kv.first] -= kv.second; if (c.kills[kv.first] <= 0) c.kills.erase(kv.first); }
        }
    }
    apply_response(r.body);
}

}  // namespace

bool Enabled() {
    std::lock_guard<std::mutex> lk(g_en_mu);
    if (!g_enabled_loaded) {
        g_enabled_loaded = true;
        std::ifstream f(enabled_path());
        int v = 0;
        if (f && (f >> v)) g_enabled = (v != 0);
    }
    return g_enabled;
}

void SetEnabled(bool on) {
    {
        std::lock_guard<std::mutex> lk(g_en_mu);
        g_enabled_loaded = true;
        g_enabled = on;
        std::ofstream f(enabled_path(), std::ios::trunc);
        if (f) f << (on ? 1 : 0);
    }
    if (!on) {
        // Turning it off drops anything counted and not yet reported, and anything not yet announced.
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& kv : g_chars) { kv.second.pending_drops.clear(); kv.second.xp_events = 0; kv.second.kills.clear(); }
    }
    rtx::log::Launcher(std::string("loot: Rune Caches ") + (on ? "enabled" : "disabled"));
}

void Start() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        guarded("loot sampler", [] {
            std::this_thread::sleep_for(std::chrono::seconds(15));   // let the readers attach first
            while (true) {
                sample_once();
                std::this_thread::sleep_for(std::chrono::milliseconds(kXpSampleMs));
            }
        });
    }).detach();
    std::thread([] {
        guarded("loot heartbeat", [] {
            std::this_thread::sleep_for(std::chrono::seconds(20));
            while (true) {
                beat_once();
                std::this_thread::sleep_for(std::chrono::seconds(kBeatSeconds));
            }
        });
    }).detach();
}

void Shutdown() {
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (const auto& kv : g_in_world) names.push_back(kv.first);
        g_in_world.clear();
    }
    post_leave(names);
}

std::string PollJson(std::uint32_t pid) {
    std::string name = rtx::reader::AccountKey(pid);
    bool linked = !link::Token().empty();
    bool enabled = Enabled();
    std::lock_guard<std::mutex> lk(g_mu);
    std::ostringstream os;
    os << "{\"linked\":" << (linked ? "true" : "false") << ",\"enabled\":" << (enabled ? "true" : "false")
       << ",\"name\":\"" << json_escape(name) << "\"";
    auto it = name.empty() ? g_chars.end() : g_chars.find(name);
    if (it != g_chars.end()) {
        auto& st = it->second;
        std::string drop;
        if (!st.pending_drops.empty()) { drop = st.pending_drops.front(); st.pending_drops.pop_front(); }
        os << ",\"xp\":" << st.xp_total << ",\"kills\":" << st.kills_total << ",\"caches\":" << st.caches_total
           << ",\"capped\":" << (st.capped ? "true" : "false") << ",\"drop\":" << (drop.empty() ? "false" : "true")
           << ",\"dropName\":\"" << json_escape(drop) << "\"";
    } else {
        os << ",\"xp\":0,\"kills\":0,\"caches\":0,\"capped\":false,\"drop\":false,\"dropName\":\"\"";
    }
    os << ",\"unopened\":" << g_unopened << "}";
    return os.str();
}

}  // namespace rtx::launcher::loot
