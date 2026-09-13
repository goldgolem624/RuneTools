#include "Loot.h"
#include "BridgeUtil.h"
#include "Http.h"
#include "Link.h"
#include "Update.h"
#include "../reader/Reader.h"
#include "../shared/Log.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace rtx::launcher::loot {
namespace {

constexpr wchar_t kHeartbeatPath[] = L"/api/client/loot/heartbeat";
constexpr int kBeatSeconds = 60;

struct CharState {
    long long seconds = 0;
    long long eligible = 1200, guaranteed = 3600;
    bool capped = false;
    int pending_drops = 0;        // caches earned and not yet announced in game
};

std::mutex g_mu;
std::map<std::string, CharState> g_chars;   // by display name
long long g_unopened = 0;
std::atomic<bool> g_started{ false };

// Reads {"n":"...","s":N,"d":N,"e":N,"g":N,"c":N} objects out of the "chars" array. The server
// emits flat objects with no nesting, so a scan for each '{' ... '}' pair is enough.
void apply_response(const std::string& body) {
    size_t arr = body.find("\"chars\":[");
    if (arr == std::string::npos) return;
    size_t end = body.find(']', arr);
    if (end == std::string::npos) return;
    std::lock_guard<std::mutex> lk(g_mu);
    size_t pos = arr;
    while (true) {
        size_t o = body.find('{', pos);
        if (o == std::string::npos || o > end) break;
        size_t c = body.find('}', o);
        if (c == std::string::npos) break;
        std::string obj = body.substr(o, c - o + 1);
        std::string name = json_str(obj, "n");
        if (!name.empty()) {
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
            long long s = num("s"), d = num("d"), e = num("e"), g = num("g"), cap = num("c");
            if (s >= 0) st.seconds = s;
            if (e > 0) st.eligible = e;
            if (g > 0) st.guaranteed = g;
            st.capped = cap == 1;
            if (d == 1) { st.pending_drops += 1; rtx::log::Launcher("loot: Rune Cache dropped for " + name); }
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
    std::string body = "{\"characters\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) body += ",";
        body += "\"" + json_escape(names[i]) + "\"";
    }
    body += "]}";
    std::vector<http::Header> hdrs = {
        { "Content-Type", "application/json" },
        { "User-Agent", "RuneToolsX/" + running_version() },
        { "X-RTX-Version", running_version() },
        { "Authorization", auth },
    };
    auto r = http::PostJson(kUpdateHost, kHeartbeatPath, hdrs, body);
    if (!r.ok) return;                                   // offline: nothing to credit, try next minute
    if (r.status == 401) { link::Verify(); return; }     // revoked or unlinked: let the link module find out
    if (r.status != 200) {
        rtx::log::Launcher("loot heartbeat: status " + std::to_string(r.status) + " " + r.detail);
        return;
    }
    apply_response(r.body);
}

}  // namespace

void Start() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        guarded("loot heartbeat", [] {
            std::this_thread::sleep_for(std::chrono::seconds(20));   // let the readers attach first
            while (true) {
                beat_once();
                std::this_thread::sleep_for(std::chrono::seconds(kBeatSeconds));
            }
        });
    }).detach();
}

std::string PollJson(std::uint32_t pid) {
    std::string name = rtx::reader::AccountKey(pid);
    bool linked = !link::Token().empty();
    std::lock_guard<std::mutex> lk(g_mu);
    std::ostringstream os;
    os << "{\"linked\":" << (linked ? "true" : "false") << ",\"name\":\"" << json_escape(name) << "\"";
    auto it = name.empty() ? g_chars.end() : g_chars.find(name);
    if (it != g_chars.end()) {
        auto& st = it->second;
        bool drop = st.pending_drops > 0;
        if (drop) st.pending_drops -= 1;
        os << ",\"seconds\":" << st.seconds << ",\"eligible\":" << st.eligible << ",\"guaranteed\":" << st.guaranteed
           << ",\"capped\":" << (st.capped ? "true" : "false") << ",\"drop\":" << (drop ? "true" : "false");
    } else {
        os << ",\"seconds\":0,\"eligible\":1200,\"guaranteed\":3600,\"capped\":false,\"drop\":false";
    }
    os << ",\"unopened\":" << g_unopened << "}";
    return os.str();
}

}  // namespace rtx::launcher::loot
