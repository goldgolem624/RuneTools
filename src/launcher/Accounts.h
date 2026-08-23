#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// AES-256-GCM vault at %USERPROFILE%\RuneToolsX\accounts.dat; key
// derived from the API key via PBKDF2 (see Crypto.h). Stores the
// captured JX_ env block so the launcher can re-spawn an account.

namespace rtx::launcher::accounts {

struct Account {
    std::string id;             // character_id when present, else "acct_<ms>"
    std::string display_name;   // JX_DISPLAY_NAME
    std::string character_id;   // JX_CHARACTER_ID
    std::string captured_at;    // ISO-8601 UTC

    // Full JX_ env block as captured. Treat values as bearer credentials.
    std::unordered_map<std::string, std::string> env;
};

bool HasVault();
bool IsUnlocked();
bool Create(const std::string& passphrase);
bool Unlock(const std::string& passphrase);
void Lock();
bool ChangePassphrase(const std::string& new_passphrase);

// Set aside an on-disk vault the current key can't open (renamed to accounts.dat.bak)
// and clear in-memory state so a fresh vault can be created for this key. Without it,
// a vault sealed by a different key leaves account-saving no-opping for the session.
bool DiscardVault();

std::vector<Account> List();
bool                 Get(const std::string& id, Account& out);
std::string          Upsert(Account a);   // returns assigned id, empty on failure
bool                 Remove(const std::string& id);

// Auto-capture would re-add a removed account the instant its still-running client is
// enumerated again, so Remove suppresses that account. The suppression is lifted once the
// account's client is gone, which keeps removal meaningful now without permanently blocking a
// later login from being captured. Ids are hashed on disk.
void SuppressCapture(const std::string& id);
bool IsCaptureSuppressed(const std::string& id);
void PruneCaptureSuppressions(const std::vector<std::string>& live_ids);

// Build an Account from a captured JX_ env block: display_name + character_id
// are picked out of the map, the rest of the env becomes the bearer payload.
Account FromEnv(std::unordered_map<std::string, std::string> env);

}  // namespace rtx::launcher::accounts
