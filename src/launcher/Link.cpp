#include "Link.h"
#include "BridgeUtil.h"
#include "Crypto.h"
#include "Http.h"
#include "Update.h"
#include "../shared/Log.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace rtx::launcher::link {
namespace {

constexpr wchar_t kStartPath[]  = L"/api/client/link/start";
constexpr wchar_t kPollPath[]   = L"/api/client/link/poll";
constexpr wchar_t kStatusPath[] = L"/api/client/link/status";
constexpr wchar_t kUnlinkPath[] = L"/api/client/link/unlink";

enum class State { Idle, Starting, Pending, Linked, Error };

struct Store {                       // what link.bin holds (sealed with DPAPI)
    std::string token, device_id, username, display_name, device_name;
};

struct St {
    State       state = State::Idle;
    Store       s;                   // valid when state == Linked
    int         linked_count = 0;
    bool        verified = false;    // token confirmed with the site during this run
    std::string user_code, verify_url, device_code;
    long long   expires_at = 0;
    int         interval = 3;
    std::string error, note;
    unsigned    gen = 0;             // bumped by Cancel/Unlink so a stale poller stops
    bool        loaded = false;
};

std::mutex g_mu;
St         g_st;
std::atomic<bool> g_busy{ false };   // one network flow at a time (start / verify / unlink)

std::filesystem::path link_path() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) > 0) {
        auto dir = std::filesystem::path(up) / L"RuneToolsX";
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        return dir / L"link.bin";
    }
    return L"runetoolsx-link.bin";
}

std::string computer_name() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buf, &n) || n == 0) return "Windows PC";
    return wide_to_utf8(std::wstring(buf, n));
}

long long json_ll(const std::string& body, const char* key) {
    std::string pat = std::string("\"") + key + "\":";
    size_t k = body.find(pat);
    if (k == std::string::npos) return 0;
    k += pat.size();
    while (k < body.size() && body[k] == ' ') ++k;
    bool neg = k < body.size() && body[k] == '-';
    if (neg) ++k;
    long long v = 0; bool any = false;
    while (k < body.size() && body[k] >= '0' && body[k] <= '9') { v = v * 10 + (body[k] - '0'); ++k; any = true; }
    if (!any) return 0;
    return neg ? -v : v;
}

std::string store_json(const Store& s) {
    return "{\"token\":\"" + json_escape(s.token) + "\",\"deviceId\":\"" + json_escape(s.device_id) +
           "\",\"username\":\"" + json_escape(s.username) + "\",\"displayName\":\"" + json_escape(s.display_name) +
           "\",\"deviceName\":\"" + json_escape(s.device_name) + "\"}";
}

// Sealed for the current Windows user: another account on the same PC, or a copy of the file
// taken elsewhere, cannot open it.
bool save_store(const Store& s) {
    auto sealed = crypto::ProtectForCurrentUser(store_json(s));
    if (sealed.empty()) return false;
    std::ofstream o(link_path(), std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(sealed.data()), (std::streamsize)sealed.size());
    o.flush();
    return (bool)o;
}

void erase_store() {
    std::error_code ec;
    std::filesystem::remove(link_path(), ec);
}

bool load_store(Store& out) {
    std::ifstream f(link_path(), std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const auto blob = ss.str();
    if (blob.empty()) return false;
    std::vector<std::uint8_t> bytes(blob.begin(), blob.end());
    std::string json = crypto::UnprotectForCurrentUser(bytes);
    if (json.empty()) { rtx::log::Launcher("link: stored token not unprotectable by this user; ignoring"); return false; }
    out.token        = json_str(json, "token");
    out.device_id    = json_str(json, "deviceId");
    out.username     = json_str(json, "username");
    out.display_name = json_str(json, "displayName");
    out.device_name  = json_str(json, "deviceName");
    return out.token.rfind("rtxd_", 0) == 0 && out.token.size() >= 40;
}

void ensure_loaded_locked() {
    if (g_st.loaded) return;
    g_st.loaded = true;
    Store s;
    if (load_store(s)) { g_st.s = s; g_st.state = State::Linked; g_st.linked_count = 0; }
}

std::vector<http::Header> base_headers() {
    return { { "Content-Type", "application/json" },
             { "User-Agent", "RuneToolsX/" + running_version() },
             { "X-RTX-Version", running_version() } };
}

void forget_locked(const std::string& note) {
    g_st.s = Store{};
    g_st.state = State::Idle;
    g_st.verified = false;
    g_st.linked_count = 0;
    g_st.note = note;
    g_st.error.clear();
    erase_store();
}

// Polls /poll until the site says approved / denied / expired, or the generation moves on.
void poll_loop(unsigned gen, std::string device_code, int interval_s, long long expires_at) {
    guarded("link poll", [&] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(interval_s < 2 ? 2 : interval_s));
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (g_st.gen != gen || g_st.state != State::Pending) return;
                long long now = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (now > expires_at) {
                    g_st.state = State::Idle;
                    g_st.note = "The code expired before it was approved. Start again when you are ready.";
                    g_st.user_code.clear(); g_st.device_code.clear(); g_st.verify_url.clear();
                    return;
                }
            }
            std::string body = "{\"deviceCode\":\"" + json_escape(device_code) + "\"}";
            auto r = http::PostJson(kUpdateHost, kPollPath, base_headers(), body);
            if (!r.ok) continue;                       // offline blip: keep waiting
            if (r.status == 429) { std::this_thread::sleep_for(std::chrono::seconds(5)); continue; }
            std::string status = json_str(r.body, "status");
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_st.gen != gen || g_st.state != State::Pending) return;
            if (status == "pending" || status == "slow_down") continue;
            if (status == "approved") {
                Store s;
                s.token        = json_str(r.body, "token");
                s.device_id    = json_str(r.body, "deviceId");
                s.username     = json_str(r.body, "username");
                s.display_name = json_str(r.body, "displayName");
                s.device_name  = json_str(r.body, "deviceName");
                if (s.token.rfind("rtxd_", 0) != 0 || s.username.empty()) {
                    g_st.state = State::Error; g_st.error = "The site answered with something unexpected. Try again.";
                    return;
                }
                if (!save_store(s)) {
                    g_st.state = State::Error; g_st.error = "Linked, but the token could not be saved on this PC.";
                    return;
                }
                g_st.s = s; g_st.state = State::Linked; g_st.verified = true; g_st.linked_count = 0;
                g_st.user_code.clear(); g_st.device_code.clear(); g_st.verify_url.clear();
                g_st.note.clear(); g_st.error.clear();
                rtx::log::Launcher("link: this PC is now linked to " + s.username);
                return;
            }
            if (status == "denied") {
                g_st.state = State::Idle; g_st.note = "The code was cancelled on the website. Nothing was linked.";
            } else if (status == "expired") {
                g_st.state = State::Idle; g_st.note = "The code expired before it was approved. Start again when you are ready.";
            } else {
                g_st.state = State::Error; g_st.error = "The site no longer recognises this code. Start again.";
            }
            g_st.user_code.clear(); g_st.device_code.clear(); g_st.verify_url.clear();
            return;
        }
    });
}

}  // namespace

std::string StatusJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    ensure_loaded_locked();
    const char* st = "idle";
    switch (g_st.state) {
        case State::Starting: st = "starting"; break;
        case State::Pending:  st = "pending";  break;
        case State::Linked:   st = "linked";   break;
        case State::Error:    st = "error";    break;
        default: break;
    }
    std::ostringstream os;
    os << "{\"state\":\"" << st << "\"";
    if (g_st.state == State::Pending) {
        os << ",\"userCode\":\"" << json_escape(g_st.user_code) << "\",\"verifyUrl\":\"" << json_escape(g_st.verify_url)
           << "\",\"expiresAt\":" << g_st.expires_at << ",\"interval\":" << g_st.interval;
    }
    if (g_st.state == State::Linked) {
        os << ",\"username\":\"" << json_escape(g_st.s.username) << "\",\"displayName\":\"" << json_escape(g_st.s.display_name)
           << "\",\"deviceId\":\"" << json_escape(g_st.s.device_id) << "\",\"deviceName\":\"" << json_escape(g_st.s.device_name)
           << "\",\"linkedCount\":" << g_st.linked_count << ",\"verified\":" << (g_st.verified ? "true" : "false");
    }
    os << ",\"error\":\"" << json_escape(g_st.error) << "\",\"note\":\"" << json_escape(g_st.note) << "\"}";
    return os.str();
}

void Start() {
    unsigned gen;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        ensure_loaded_locked();
        if (g_st.state == State::Pending || g_st.state == State::Starting || g_st.state == State::Linked) return;
        g_st.state = State::Starting;
        g_st.error.clear(); g_st.note.clear();
        gen = ++g_st.gen;
    }
    std::thread([gen] {
        guarded("link start", [&] {
            std::string body = "{\"deviceName\":\"" + json_escape(computer_name()) +
                               "\",\"version\":\"" + json_escape(running_version()) + "\"}";
            auto r = http::PostJson(kUpdateHost, kStartPath, base_headers(), body);
            std::string user_code = json_str(r.body, "userCode");
            std::string device_code = json_str(r.body, "deviceCode");
            std::string verify_url = json_str(r.body, "verifyUrl");
            long long expires_at = json_ll(r.body, "expiresAt");
            int interval = (int)json_ll(r.body, "interval");
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (g_st.gen != gen || g_st.state != State::Starting) return;
                if (!r.ok || r.status != 200 || user_code.empty() || device_code.empty()) {
                    g_st.state = State::Error;
                    g_st.error = !r.ok ? "Could not reach runetools.io. Check your connection and try again."
                               : r.status == 429 ? "Too many attempts from this address. Wait a minute and try again."
                               : "runetools.io could not start linking (status " + std::to_string(r.status) + ").";
                    rtx::log::Launcher("link start failed: status " + std::to_string(r.status) + " " + r.detail);
                    return;
                }
                g_st.state = State::Pending;
                g_st.user_code = user_code; g_st.device_code = device_code;
                g_st.verify_url = verify_url.rfind("https://runetools.io/", 0) == 0 ? verify_url : "https://runetools.io/link";
                g_st.expires_at = expires_at; g_st.interval = interval > 0 ? interval : 3;
            }
            poll_loop(gen, device_code, interval > 0 ? interval : 3, expires_at);
        });
    }).detach();
}

void Cancel() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_st.state != State::Pending && g_st.state != State::Starting && g_st.state != State::Error) return;
    ++g_st.gen;
    g_st.state = State::Idle;
    g_st.user_code.clear(); g_st.device_code.clear(); g_st.verify_url.clear();
    g_st.error.clear(); g_st.note.clear();
}

void Unlink() {
    std::string token;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        ensure_loaded_locked();
        if (g_st.state != State::Linked) return;
        token = g_st.s.token;
        ++g_st.gen;
        forget_locked("This PC was unlinked. Link it again any time.");
    }
    // Local forget first (already done): the token is gone from this PC whatever the network says.
    http::Enqueue([token] {
        auto hdrs = base_headers();
        hdrs.push_back({ "Authorization", "Bearer " + token });
        auto r = http::PostJson(kUpdateHost, kUnlinkPath, hdrs, "{}");
        if (!r.ok || (r.status != 200 && r.status != 401))
            rtx::log::Launcher("link: unlink on site failed: status " + std::to_string(r.status) + " " + r.detail +
                               " (revoke it from runetools.io/settings if it still shows there)");
    });
}

void Verify() {
    std::string token; unsigned gen;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        ensure_loaded_locked();
        if (g_st.state != State::Linked) return;
        token = g_st.s.token; gen = g_st.gen;
    }
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return;
    http::Enqueue([token, gen] {
        guarded("link verify", [&] {
            auto hdrs = base_headers();
            hdrs.push_back({ "Authorization", "Bearer " + token });
            auto r = http::Get(kUpdateHost, kStatusPath, hdrs);
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_st.gen != gen || g_st.state != State::Linked) return;
            if (!r.ok) return;                                    // offline: keep the stored link, unverified
            if (r.status == 200) {
                std::string u = json_str(r.body, "username");
                if (!u.empty()) g_st.s.username = u;
                std::string dn = json_str(r.body, "displayName");
                if (!dn.empty()) g_st.s.display_name = dn;
                std::string dev = json_str(r.body, "deviceName");
                if (!dev.empty()) g_st.s.device_name = dev;
                g_st.linked_count = (int)json_ll(r.body, "linkedCount");
                g_st.verified = true;
                g_st.note.clear();
                return;
            }
            if (r.status == 401) {
                std::string code = json_str(r.body, "code");
                rtx::log::Launcher("link: site rejected this PC's token (" + code + "); forgetting it");
                forget_locked(code == "inactive" ? "Your RuneTools account is not active, so this PC was unlinked."
                                                 : "This PC was unlinked from your RuneTools account on the website.");
            }
        });
        g_busy = false;
    });
}

std::string Token() {
    std::lock_guard<std::mutex> lk(g_mu);
    ensure_loaded_locked();
    return g_st.state == State::Linked ? g_st.s.token : std::string();
}

std::string AuthHeader() {
    std::string t = Token();
    return t.empty() ? std::string() : "Bearer " + t;
}

}  // namespace rtx::launcher::link
