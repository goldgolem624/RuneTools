#include "HealthAuto.h"
#include "Process.h"
#include "../reader/Reader.h"
#include "../reader/HealthRun.h"
#include "../reader/CodeScan.h"
#include "../reader/BootLog.h"
#include "../cache/CacheReader.h"
#include "../shared/Log.h"

#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace rtx::launcher::healthauto {
namespace {

constexpr ULONGLONG kTickMs    = 2000;      // readiness is looked at this often
constexpr ULONGLONG kSettleMs  = 10000;     // in world and booted for this long before the run
constexpr ULONGLONG kTimeoutMs = 180000;    // then the run goes ahead as incomplete

struct Client {
    std::uint32_t pid = 0;
    bool fingerprinting = false, fingerprinted = false;
    std::string version, stamp, flavour;
    std::string trigger, why;         // the queued run; empty when none
    ULONGLONG pendingMs = 0, readyMs = 0;
};

std::mutex g_mu;
std::vector<Client> g_clients;
bool g_running = false;
std::uint64_t g_seq = 0;
std::string g_result, g_lastTrigger;
ULONGLONG g_lastTickMs = 0;

Client* find_locked(std::uint32_t pid) {
    for (auto& c : g_clients) if (c.pid == pid) return &c;
    return nullptr;
}

// The first "key":"value" after `from` in a JSON text.
std::string json_field(const std::string& j, std::size_t from, const char* key) {
    const std::string k = std::string("\"") + key + "\":\"";
    const std::size_t at = j.find(k, from);
    if (at == std::string::npos) return {};
    const std::size_t b = at + k.size(), e = j.find('"', b);
    return e == std::string::npos ? std::string() : j.substr(b, e - b);
}

// The sampler's client version for a pid ("950-1"), empty until it has one.
std::string client_version(std::uint32_t pid) {
    const std::string j = rtx::reader::SamplesJson();
    const std::size_t at = j.find("\"pid\":" + std::to_string(pid) + ",");
    return at == std::string::npos ? std::string() : json_field(j, at, "client_version");
}

std::wstring exe_path(std::uint32_t pid) {
    for (const auto& p : rtx::launcher::process::ScanRsClients()) if (p.pid == pid) return p.path;
    return {};
}

// Cache revisions for the keys of a recorded fingerprint ("2/5=..,3=.."), in the order and form the
// reader records them, so the current cache can be matched against builds.txt.
std::string revs_like(const std::string& recorded) {
    std::string out;
    std::size_t at = 0;
    while (at < recorded.size()) {
        std::size_t e = recorded.find(',', at);
        if (e == std::string::npos) e = recorded.size();
        const std::string kv = recorded.substr(at, e - at);
        at = e + 1;
        const std::size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq);
        const std::size_t sl = key.find('/');
        int r = -1;
        if (sl == std::string::npos) {
            const rtx::cache::IndexFacts f = rtx::cache::IndexInfo(std::atoi(key.c_str()));
            r = f.open ? f.revision : -1;
        } else {
            r = rtx::cache::ArchiveRevision(std::atoi(key.substr(0, sl).c_str()), std::atoi(key.substr(sl + 1).c_str()));
        }
        out += (out.empty() ? "" : ",") + key + "=" + std::to_string(r);
    }
    return out;
}

// Exe and cache fingerprint of a client against builds.txt; queues a run when either is new or the
// build is not reviewed yet. Off the UI thread: it reads the exe on disk.
void fingerprint(std::uint32_t pid, std::string version) {
    const rtx::codescan::ExeFacts ef = rtx::codescan::Facts(exe_path(pid));
    char st[16]; std::snprintf(st, sizeof(st), "%08x", ef.stamp);
    std::string trigger, why;
    if (!ef.ok) {
        why = "exe not readable";
    } else {
        rtx::health::BuildState bs = rtx::health::BuildsLookup(version, st, ef.flavour, "");
        if (!bs.exeSeen) {
            trigger = "new exe"; why = version + " " + st + " not seen before";
        } else {
            bs = rtx::health::BuildsLookup(version, st, ef.flavour, revs_like(bs.prevRevs));
            if (!bs.known) { trigger = "cache update"; why = "cache revisions differ from the last run on " + version + " " + st; }
            else if (!bs.reviewed) { trigger = "new exe"; why = version + " " + st + " not reviewed yet (first seen " + bs.firstSeen + ")"; }
        }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    Client* c = find_locked(pid);
    if (!c) return;
    c->fingerprinting = false; c->fingerprinted = true;
    c->version = version; c->stamp = st; c->flavour = ef.flavour;
    if (trigger.empty()) {
        rtx::log::Launcher("health: pid " + std::to_string(pid) + " on " + version + " " + st + (ef.ok ? " is a reviewed build, no automatic run" : ": " + why));
        return;
    }
    c->trigger = trigger; c->why = why; c->pendingMs = GetTickCount64(); c->readyMs = 0;
    rtx::log::Launcher("health: auto run queued (" + trigger + " " + st + "): " + why + "; runs once in world");
}

bool in_world(std::uint32_t pid) {
    for (const auto& p : rtx::reader::LastPresence()) if (p.pid == pid) return p.status == 30 && p.in_world;
    return false;
}

// The reader takes the trigger once it records it (ReaderHealthJson(pid, pins, trigger)).
template <class Reader>
std::string run_with(Reader reader, std::uint32_t pid, const std::string& pins, const std::string& trigger) {
    if constexpr (std::is_invocable_v<Reader, std::uint32_t, const std::string&, const std::string&>) return reader(pid, pins, trigger);
    else return reader(pid, pins);
}

std::string counts_of(const std::string& json) {
    rtx::health::JVal v;
    if (!rtx::health::ParseJson(json, v)) return "no result";
    const rtx::health::JVal* s = v.get("summary");
    const rtx::health::JVal* b = v.get("build");
    if (!s) return "no summary";
    return std::to_string(s->num("pass")) + " pass " + std::to_string(s->num("fail")) + " fail " + std::to_string(s->num("warn")) +
           " warn " + std::to_string(s->num("unchecked")) + " not checked" + (b ? " (" + v.str("version") + " " + b->str("stamp") + ")" : "");
}

std::string quoted(const std::string& s) { return "\"" + rtx::health::Escape(s) + "\""; }

}  // namespace

void ClientAttached(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (find_locked(pid)) return;
    Client c; c.pid = pid;
    g_clients.push_back(c);
}

void ClientGone(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto it = g_clients.begin(); it != g_clients.end(); ++it)
        if (it->pid == pid) { g_clients.erase(it); return; }
}

void CacheChanged(std::uint64_t generation) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& c : g_clients) {
        if (!c.fingerprinted) continue;   // the fingerprint, once done, sees the new cache
        c.trigger = "cache update"; c.why = "cache generation " + std::to_string(generation);
        c.pendingMs = GetTickCount64(); c.readyMs = 0;
        rtx::log::Launcher("health: auto run queued (cache update " + c.stamp + "): " + c.why + "; runs once in world");
    }
}

void Tick() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastTickMs < kTickMs) return;
    g_lastTickMs = now;
    std::vector<std::pair<std::uint32_t, std::string>> toFingerprint;
    std::uint32_t duePid = 0;
    std::string dueTrigger, dueWhy;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_running) return;
        for (auto& c : g_clients) {
            if (!c.fingerprinted) {
                if (c.fingerprinting) continue;
                const std::string v = client_version(c.pid);
                if (v.empty()) continue;
                c.fingerprinting = true;
                toFingerprint.emplace_back(c.pid, v);
                continue;
            }
            if (c.trigger.empty() || duePid) continue;
            // in world with the companion booted, then a settle so the live rows are not unchecked
            bool ready = in_world(c.pid) && rtx::bootlog::Read(c.pid).found;
            if (!ready) c.readyMs = 0;
            else if (!c.readyMs) c.readyMs = now;
            const bool settled = c.readyMs && now - c.readyMs >= kSettleMs;
            const bool timedOut = now - c.pendingMs >= kTimeoutMs;
            if (!settled && !timedOut) continue;
            duePid = c.pid; dueTrigger = c.trigger;
            dueWhy = c.why + (settled ? "" : "; not in world after 3 min, run incomplete");
            c.trigger.clear(); c.why.clear(); c.readyMs = 0;
        }
    }
    for (const auto& f : toFingerprint) std::thread(fingerprint, f.first, f.second).detach();
    if (duePid) {
        rtx::log::Launcher("health: auto run (" + dueTrigger + "): " + dueWhy);
        Start(duePid, "{}", dueTrigger);
    }
}

std::uint64_t Start(std::uint32_t pid, const std::string& pinsJson, const std::string& trigger) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_running) return g_seq;
        g_running = true;
        g_lastTrigger = trigger;
    }
    std::thread([pid, pinsJson, trigger] {
        std::string out;
        try {
            out = run_with([](auto&&... a) -> decltype(rtx::reader::ReaderHealthJson(a...)) { return rtx::reader::ReaderHealthJson(a...); },
                           pid, pinsJson, trigger);
        } catch (const std::exception& e) { rtx::log::Launcher(std::string("[health] ") + e.what()); }
        rtx::log::Launcher("health: " + trigger + " run done: " + counts_of(out));
        std::lock_guard<std::mutex> lk(g_mu);
        g_result = std::move(out);
        ++g_seq;
        g_running = false;
    }).detach();
    std::lock_guard<std::mutex> lk(g_mu);
    return g_seq;
}

std::string PollJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    return std::string("{\"running\":") + (g_running ? "true" : "false") + ",\"seq\":" + std::to_string(g_seq) +
           ",\"run\":" + (g_result.empty() ? std::string("null") : g_result) + "}";
}

std::string LatestJson(std::uint64_t since) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string pending = "null";
    for (const auto& c : g_clients) {
        if (c.trigger.empty()) continue;
        pending = "{\"pid\":" + std::to_string(c.pid) + ",\"trigger\":" + quoted(c.trigger) + ",\"why\":" + quoted(c.why) +
                  ",\"version\":" + quoted(c.version) + ",\"stamp\":" + quoted(c.stamp) + "}";
        break;
    }
    return "{\"seq\":" + std::to_string(g_seq) + ",\"running\":" + (g_running ? "true" : "false") +
           ",\"trigger\":" + quoted(g_lastTrigger) + ",\"pending\":" + pending +
           ",\"run\":" + ((g_seq != since && !g_result.empty()) ? g_result : std::string("null")) + "}";
}

std::string SummaryText(const std::string& name) {
    std::string json;
    if (name.empty()) { std::lock_guard<std::mutex> lk(g_mu); json = g_result; }
    else json = rtx::health::HistoryRead(name);
    if (json.empty()) return "no health run yet\n";
    return rtx::health::SummaryText(json);
}

}  // namespace rtx::launcher::healthauto
