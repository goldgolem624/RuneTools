#include "Accounts.h"
#include "Crypto.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

namespace rtx::launcher::accounts {

namespace {

// File layout. Header bytes (magic..nonce) are bound into the GCM tag
// as AAD so KDF-parameter tampering is detected at unlock.
//   magic u32 'RTXA' | version u32 | iterations u32 | salt[16] |
//   nonce[12] | ct_len u32 | ciphertext | tag[16]

constexpr std::uint32_t kFileMagic   = 0x41585452u;   // 'RTXA' little-endian
constexpr std::uint32_t kFileVersion = 1;
constexpr std::size_t   kHeaderLen   =
    4 + 4 + 4 + crypto::kSaltBytes + crypto::kNonceBytes;

std::mutex                                     g_mu;
std::vector<Account>                           g_accounts;
std::array<std::uint8_t, crypto::kKeyBytes>    g_key{};
bool                                           g_unlocked = false;

std::filesystem::path accounts_path() {
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH) > 0) {
        auto dir = std::filesystem::path(up) / L"RuneToolsX";
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        return dir / L"accounts.dat";
    }
    return L"runetoolsx-accounts.dat";
}

// ---- capture suppression ----
//
// Removing an account is an explicit act, but auto-capture re-adds any account whose client is
// running the moment the client list is next enumerated. The account therefore reappeared
// immediately, which read as "Remove does nothing".
//
// A removed account is suppressed so capture will not put it straight back. The suppression is
// lifted as soon as that account's client is gone (see PruneCaptureSuppressions), so a fresh
// login is captured normally: the rule is "removing it now actually removes it", not "never
// save this account again", which would be unclearable given capture is the only way in.
//
// Ids are stored HASHED. The vault is encrypted precisely so account identifiers are not
// sitting in the clear next to it, and a suppression list of plaintext character ids would
// hand back exactly that.
std::filesystem::path suppress_path() {
    auto p = accounts_path();
    p.replace_filename(L"account_suppress.txt");
    return p;
}

std::mutex                 g_sup_mu;
std::set<std::string>      g_suppressed;      // hex HMAC-SHA256(install secret, account id)
constexpr const char*      kSuppressHeader = "#rtx-suppress-v2";
bool                       g_sup_loaded = false;

// The suppression list is keyed by HMAC-SHA256(install secret, account id), not a bare hash:
// account ids are character names, so an unkeyed digest next to the vault was a brute-forceable
// list of which characters this machine has. The 32-byte secret is minted once per install and
// kept under per-user DPAPI (suppress.key beside the list), so the file alone reveals nothing.
std::filesystem::path suppress_key_path() {
    auto p = accounts_path();
    p.replace_filename(L"suppress.key");
    return p;
}

// Thread-safe one-time initialisation (callers run outside g_sup_mu): a failed first attempt
// yields an empty secret for the rest of the process and is retried on the next launch.
std::string suppress_secret_locked() {
    static const std::string cached = []() -> std::string {
    const auto path = suppress_key_path();
    {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::stringstream ss; ss << f.rdbuf();
            const auto blob = ss.str();
            if (!blob.empty()) {
                std::vector<std::uint8_t> bytes(blob.begin(), blob.end());
                std::string existing = crypto::UnprotectForCurrentUser(bytes);
                if (!existing.empty()) return existing;
            }
        }
    }
    std::uint8_t raw[32];
    if (!crypto::RandomBytes(raw, sizeof(raw))) return {};
    std::string secret(reinterpret_cast<const char*>(raw), sizeof(raw));
    SecureZeroMemory(raw, sizeof(raw));
    auto sealed = crypto::ProtectForCurrentUser(secret);
    if (sealed.empty()) return {};
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return {};
    o.write(reinterpret_cast<const char*>(sealed.data()), (std::streamsize)sealed.size());
    o.flush();
    if (!o) return {};
    return secret;
    }();
    return cached;
}

// HMAC-SHA256 over SHA-256 (64-byte block), hex encoded. Empty when no secret is available,
// which callers treat as "cannot suppress" (fail open to normal capture, never to a fixed key).
std::string id_digest(const std::string& id) {
    const std::string key = suppress_secret_locked();
    if (key.empty()) return {};
    std::uint8_t k[64] = {};
    std::memcpy(k, key.data(), key.size() < sizeof(k) ? key.size() : sizeof(k));
    std::string inner; inner.reserve(64 + id.size());
    for (int i = 0; i < 64; ++i) inner.push_back((char)(k[i] ^ 0x36));
    inner += id;
    std::uint8_t ih[32];
    if (!crypto::Sha256((const std::uint8_t*)inner.data(), inner.size(), ih)) return {};
    std::string outer; outer.reserve(64 + 32);
    for (int i = 0; i < 64; ++i) outer.push_back((char)(k[i] ^ 0x5c));
    outer.append(reinterpret_cast<const char*>(ih), sizeof(ih));
    std::uint8_t d[32];
    if (!crypto::Sha256((const std::uint8_t*)outer.data(), outer.size(), d)) return {};
    SecureZeroMemory(k, sizeof(k));
    static const char* hex = "0123456789abcdef";
    std::string out; out.reserve(64);
    for (int i = 0; i < 32; ++i) { out += hex[d[i] >> 4]; out += hex[d[i] & 0xF]; }
    return out;
}

void suppress_load_locked() {
    if (g_sup_loaded) return;
    g_sup_loaded = true;
    std::ifstream f(suppress_path());
    if (!f) return;
    std::string line;
    bool versioned = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line == kSuppressHeader) { versioned = true; continue; }
        if (versioned && line.size() == 64) g_suppressed.insert(line);
    }
}

void suppress_save_locked() {
    auto path = suppress_path();
    if (g_suppressed.empty()) { std::error_code ec; std::filesystem::remove(path, ec); return; }
    auto tmp = path; tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open()) return;
        f << kSuppressHeader << "\n";
        for (const auto& h : g_suppressed) f << h << "\n";
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(path, ec); std::filesystem::rename(tmp, path, ec); }
}

// ---- length-prefixed binary plaintext ----

void put_u16(std::string& buf, std::uint16_t v) {
    buf.push_back((char)(v & 0xFF));
    buf.push_back((char)((v >> 8) & 0xFF));
}
void put_u32(std::string& buf, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back((char)((v >> (i*8)) & 0xFF));
}
void put_str_u16(std::string& buf, const std::string& s) {
    auto n = (std::uint16_t)((s.size() > 0xFFFF) ? 0xFFFF : s.size());
    put_u16(buf, n);
    buf.append(s.data(), n);
}
void put_str_u32(std::string& buf, const std::string& s) {
    auto n = (std::uint32_t)((s.size() > 0xFFFFFFFFu) ? 0xFFFFFFFFu : s.size());
    put_u32(buf, n);
    buf.append(s.data(), n);
}

bool get_u16(const std::string& buf, size_t& pos, std::uint16_t& out) {
    if (pos + 2 > buf.size()) return false;
    out = (std::uint8_t)buf[pos] | ((std::uint16_t)(std::uint8_t)buf[pos+1] << 8);
    pos += 2; return true;
}
bool get_u32(const std::string& buf, size_t& pos, std::uint32_t& out) {
    if (pos + 4 > buf.size()) return false;
    out = 0;
    for (int i = 0; i < 4; ++i)
        out |= ((std::uint32_t)(std::uint8_t)buf[pos+i]) << (i*8);
    pos += 4; return true;
}
bool get_str_u16(const std::string& buf, size_t& pos, std::string& out) {
    std::uint16_t n = 0;
    if (!get_u16(buf, pos, n)) return false;
    if (pos + n > buf.size()) return false;
    out.assign(buf.data() + pos, n); pos += n; return true;
}
bool get_str_u32(const std::string& buf, size_t& pos, std::string& out) {
    std::uint32_t n = 0;
    if (!get_u32(buf, pos, n)) return false;
    if (pos + n > buf.size()) return false;
    out.assign(buf.data() + pos, n); pos += n; return true;
}

std::string serialize_plain(const std::vector<Account>& accounts) {
    std::string buf;
    put_u32(buf, (std::uint32_t)accounts.size());
    for (const auto& a : accounts) {
        put_str_u16(buf, a.id);
        put_str_u16(buf, a.display_name);
        put_str_u16(buf, a.character_id);
        put_str_u16(buf, a.captured_at);
        put_u16(buf, (std::uint16_t)a.env.size());
        for (const auto& [k, v] : a.env) {
            put_str_u16(buf, k);
            put_str_u32(buf, v);
        }
    }
    return buf;
}

bool deserialize_plain(const std::string& buf, std::vector<Account>& out) {
    size_t pos = 0;
    std::uint32_t count = 0;
    if (!get_u32(buf, pos, count)) return false;
    if (count > 1024) return false;

    out.clear(); out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Account a;
        if (!get_str_u16(buf, pos, a.id))           return false;
        if (!get_str_u16(buf, pos, a.display_name)) return false;
        if (!get_str_u16(buf, pos, a.character_id)) return false;
        if (!get_str_u16(buf, pos, a.captured_at))  return false;
        std::uint16_t ec = 0;
        if (!get_u16(buf, pos, ec))                 return false;
        for (std::uint16_t j = 0; j < ec; ++j) {
            std::string k, v;
            if (!get_str_u16(buf, pos, k))          return false;
            if (!get_str_u32(buf, pos, v))          return false;
            a.env.emplace(std::move(k), std::move(v));
        }
        out.push_back(std::move(a));
    }
    return true;
}

struct Header {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t iterations;
    std::uint8_t  salt[crypto::kSaltBytes];
    std::uint8_t  nonce[crypto::kNonceBytes];
};

void write_header(std::string& out, const Header& h) {
    put_u32(out, h.magic);
    put_u32(out, h.version);
    put_u32(out, h.iterations);
    out.append((const char*)h.salt,  crypto::kSaltBytes);
    out.append((const char*)h.nonce, crypto::kNonceBytes);
}

bool read_header(const std::string& blob, Header& h) {
    if (blob.size() < kHeaderLen) return false;
    size_t pos = 0;
    if (!get_u32(blob, pos, h.magic) || h.magic != kFileMagic)         return false;
    if (!get_u32(blob, pos, h.version) || h.version != kFileVersion)   return false;
    if (!get_u32(blob, pos, h.iterations) || h.iterations < 50'000)    return false;
    std::memcpy(h.salt,  blob.data() + pos, crypto::kSaltBytes);  pos += crypto::kSaltBytes;
    std::memcpy(h.nonce, blob.data() + pos, crypto::kNonceBytes); pos += crypto::kNonceBytes;
    return true;
}

// Saves keep the salt stable and rotate only the nonce; the salt rotates on
// Create/ChangePassphrase, where the key is re-derived.
struct SessionMeta {
    std::array<std::uint8_t, crypto::kSaltBytes> salt{};
    std::uint32_t iterations = crypto::kKdfDefaultIterations;
    bool          have_salt  = false;
};
SessionMeta g_session;

bool save_with_cached_key() {
    if (!g_unlocked || !g_session.have_salt) return false;

    Header h{};
    h.magic = kFileMagic;
    h.version = kFileVersion;
    h.iterations = g_session.iterations;
    std::memcpy(h.salt, g_session.salt.data(), crypto::kSaltBytes);
    if (!crypto::RandomBytes(h.nonce, crypto::kNonceBytes)) return false;

    std::string header_bytes;
    write_header(header_bytes, h);
    auto plain = serialize_plain(g_accounts);

    crypto::AeadOut aead;
    if (!crypto::EncryptAesGcm(
            g_key.data(), g_key.size(),
            h.nonce, crypto::kNonceBytes,
            (const std::uint8_t*)header_bytes.data(), header_bytes.size(),
            (const std::uint8_t*)plain.data(), plain.size(),
            aead)) {
        return false;
    }

    std::string file_bytes = std::move(header_bytes);
    put_u32(file_bytes, (std::uint32_t)aead.ciphertext.size());
    file_bytes.append((const char*)aead.ciphertext.data(), aead.ciphertext.size());
    file_bytes.append((const char*)aead.tag.data(),        aead.tag.size());

    auto path = accounts_path();
    auto tmp  = path; tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(file_bytes.data(), (std::streamsize)file_bytes.size());
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

std::string iso8601_now() {
    auto t  = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string make_id(const Account& a) {
    if (!a.character_id.empty()) return a.character_id;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "acct_%lld", (long long)ms);
    return buf;
}

}  // namespace

bool HasVault() {
    std::error_code ec;
    return std::filesystem::exists(accounts_path(), ec);
}

bool IsUnlocked() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_unlocked;
}

bool Create(const std::string& passphrase) {
    if (passphrase.empty()) return false;
    if (HasVault())         return false;

    std::lock_guard<std::mutex> lk(g_mu);
    g_accounts.clear();
    g_session.iterations = crypto::kKdfDefaultIterations;
    if (!crypto::RandomBytes(g_session.salt.data(), g_session.salt.size())) return false;
    g_session.have_salt = true;

    if (!crypto::DeriveKey(passphrase,
                           g_session.salt.data(), g_session.salt.size(),
                           g_session.iterations,
                           g_key.data())) {
        g_session = {};
        return false;
    }
    g_unlocked = true;

    if (!save_with_cached_key()) {
        SecureZeroMemory(g_key.data(), g_key.size());
        g_session = {};
        g_unlocked = false;
        return false;
    }
    return true;
}

bool Unlock(const std::string& passphrase) {
    if (passphrase.empty()) return false;

    auto path = accounts_path();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    std::stringstream ss; ss << f.rdbuf();
    auto blob = ss.str();

    Header h{};
    if (!read_header(blob, h)) return false;

    if (blob.size() < kHeaderLen + 4 + crypto::kTagBytes) return false;

    size_t pos = kHeaderLen;
    std::uint32_t ct_len = 0;
    if (!get_u32(blob, pos, ct_len)) return false;
    if (pos + ct_len + crypto::kTagBytes != blob.size()) return false;

    const std::uint8_t* ct  = (const std::uint8_t*)blob.data() + pos;
    const std::uint8_t* tag = ct + ct_len;

    std::array<std::uint8_t, crypto::kKeyBytes> key{};
    if (!crypto::DeriveKey(passphrase,
                           h.salt, crypto::kSaltBytes,
                           h.iterations, key.data())) {
        return false;
    }

    std::string header_bytes;
    write_header(header_bytes, h);

    std::vector<std::uint8_t> plain;
    bool ok = crypto::DecryptAesGcm(
        key.data(), key.size(),
        h.nonce, crypto::kNonceBytes,
        (const std::uint8_t*)header_bytes.data(), header_bytes.size(),
        ct, ct_len,
        tag, crypto::kTagBytes,
        plain);
    if (!ok) {
        SecureZeroMemory(key.data(), key.size());
        return false;
    }

    std::vector<Account> accounts;
    std::string plain_s((const char*)plain.data(), plain.size());
    SecureZeroMemory(plain.data(), plain.size());
    if (!deserialize_plain(plain_s, accounts)) {
        SecureZeroMemory(key.data(), key.size());
        SecureZeroMemory((void*)plain_s.data(), plain_s.size());
        return false;
    }
    SecureZeroMemory((void*)plain_s.data(), plain_s.size());

    std::lock_guard<std::mutex> lk(g_mu);
    g_accounts = std::move(accounts);
    std::memcpy(g_key.data(), key.data(), g_key.size());
    SecureZeroMemory(key.data(), key.size());
    g_session.iterations = h.iterations;
    std::memcpy(g_session.salt.data(), h.salt, crypto::kSaltBytes);
    g_session.have_salt = true;
    g_unlocked = true;
    return true;
}

void Lock() {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& a : g_accounts) {
        for (auto& [k, v] : a.env) SecureZeroMemory((void*)v.data(), v.size());
    }
    g_accounts.clear();
    SecureZeroMemory(g_key.data(), g_key.size());
    g_session = {};
    g_unlocked = false;
}

bool ChangePassphrase(const std::string& new_passphrase) {
    if (new_passphrase.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_unlocked) return false;

    auto saved_salt  = g_session.salt;
    auto saved_iters = g_session.iterations;
    auto saved_key   = g_key;

    if (!crypto::RandomBytes(g_session.salt.data(), g_session.salt.size())) {
        g_session.salt = saved_salt;
        return false;
    }
    g_session.iterations = crypto::kKdfDefaultIterations;
    if (!crypto::DeriveKey(new_passphrase,
                           g_session.salt.data(), g_session.salt.size(),
                           g_session.iterations,
                           g_key.data())) {
        g_session.salt = saved_salt;
        g_session.iterations = saved_iters;
        std::memcpy(g_key.data(), saved_key.data(), g_key.size());
        SecureZeroMemory(saved_key.data(), saved_key.size());
        return false;
    }

    if (!save_with_cached_key()) {
        g_session.salt = saved_salt;
        g_session.iterations = saved_iters;
        std::memcpy(g_key.data(), saved_key.data(), g_key.size());
        SecureZeroMemory(saved_key.data(), saved_key.size());
        return false;
    }
    SecureZeroMemory(saved_key.data(), saved_key.size());
    return true;
}

std::vector<Account> List() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_unlocked) return {};
    return g_accounts;
}

bool Get(const std::string& id, Account& out) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_unlocked) return false;
    for (const auto& a : g_accounts) {
        if (a.id == id) { out = a; return true; }
    }
    return false;
}

std::string Upsert(Account a) {
    bool needs_save = false;
    std::string assigned_id;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_unlocked) return {};
        if (a.id.empty()) a.id = make_id(a);

        auto same = [&](const Account& other) {
            if (!a.character_id.empty() && !other.character_id.empty())
                return a.character_id == other.character_id;
            return a.display_name == other.display_name;
        };

        bool replaced = false;
        for (auto& other : g_accounts) {
            if (other.id == a.id || same(other)) {
                // No-op when nothing changed so the periodic scan doesn't re-encrypt every tick.
                if (other.display_name == a.display_name &&
                    other.character_id == a.character_id &&
                    other.env          == a.env) {
                    return other.id;
                }
                // Preserve id so in-flight launches stay valid.
                a.id = other.id;
                other = a;
                replaced = true;
                break;
            }
        }
        if (!replaced) g_accounts.push_back(a);
        needs_save  = true;
        assigned_id = a.id;
    }
    if (needs_save && !save_with_cached_key()) return {};
    return assigned_id;
}

bool Remove(const std::string& id) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_unlocked) return false;
        auto it = std::remove_if(g_accounts.begin(), g_accounts.end(),
                                 [&](const Account& a) { return a.id == id; });
        if (it != g_accounts.end()) {
            g_accounts.erase(it, g_accounts.end());
            changed = true;
        }
    }
    if (!changed) return false;
    // Report the SAVE, not the in-memory erase. This returned true whenever the account was
    // found, even when writing the vault failed, so the row vanished from the list and came
    // back on the next load with nothing having said the removal did not stick.
    if (!save_with_cached_key()) return false;
    SuppressCapture(id);
    return true;
}

void SuppressCapture(const std::string& id) {
    if (id.empty()) return;
    auto h = id_digest(id);
    if (h.empty()) return;
    std::lock_guard<std::mutex> lk(g_sup_mu);
    suppress_load_locked();
    if (g_suppressed.insert(h).second) suppress_save_locked();
}

bool IsCaptureSuppressed(const std::string& id) {
    if (id.empty()) return false;
    auto h = id_digest(id);
    if (h.empty()) return false;
    std::lock_guard<std::mutex> lk(g_sup_mu);
    suppress_load_locked();
    return g_suppressed.count(h) != 0;
}

void PruneCaptureSuppressions(const std::vector<std::string>& live_ids) {
    std::set<std::string> live;
    for (const auto& id : live_ids) { auto h = id_digest(id); if (!h.empty()) live.insert(h); }
    std::lock_guard<std::mutex> lk(g_sup_mu);
    suppress_load_locked();
    bool changed = false;
    for (auto it = g_suppressed.begin(); it != g_suppressed.end(); ) {
        if (live.count(*it)) { ++it; continue; }
        it = g_suppressed.erase(it);          // its client is gone: a fresh login may be captured
        changed = true;
    }
    if (changed) suppress_save_locked();
}

bool DiscardVault() {
    std::lock_guard<std::mutex> lk(g_mu);
    auto path = accounts_path();
    auto bak  = path; bak += L".bak";
    std::error_code ec;
    std::filesystem::remove(bak, ec);           // drop any previous backup
    std::filesystem::rename(path, bak, ec);     // keep the orphaned vault recoverable
    if (ec) { std::error_code ec2; std::filesystem::remove(path, ec2); }  // else just delete
    for (auto& a : g_accounts)
        for (auto& [k, v] : a.env) SecureZeroMemory((void*)v.data(), v.size());
    g_accounts.clear();
    SecureZeroMemory(g_key.data(), g_key.size());
    g_session = {};
    g_unlocked = false;
    return true;
}

Account FromEnv(std::unordered_map<std::string, std::string> env) {
    Account a;
    auto it = env.find("JX_DISPLAY_NAME");
    if (it != env.end()) a.display_name = it->second;
    it = env.find("JX_CHARACTER_ID");
    if (it != env.end()) a.character_id = it->second;
    a.captured_at = iso8601_now();
    a.env = std::move(env);
    return a;
}

}  // namespace rtx::launcher::accounts
