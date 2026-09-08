#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// AES-256-GCM vault at %USERPROFILE%\RuneToolsX\accounts.dat, key derived from the API key
// via PBKDF2 (Crypto.h). Stores captured JX_ env blocks so the launcher can re-spawn an account.

namespace rtx::launcher::accounts {

struct Account {
    std::string id;             // character_id when present, else "acct_<ms>"
    std::string display_name;   // JX_DISPLAY_NAME
    std::string character_id;   // JX_CHARACTER_ID
    std::string captured_at;    // ISO-8601 UTC

    // Full JX_ env block; values are bearer credentials.
    std::unordered_map<std::string, std::string> env;
};

bool HasVault();
bool IsUnlocked();
bool Create(const std::string& passphrase);
bool Unlock(const std::string& passphrase);
void Lock();
bool ChangePassphrase(const std::string& new_passphrase);

// Rename a vault the current key cannot open to accounts.dat.bak so a fresh one can be created.
bool DiscardVault();

std::vector<Account> List();
bool                 Get(const std::string& id, Account& out);
std::string          Upsert(Account a);   // returns assigned id, empty on failure
bool                 Remove(const std::string& id);

// Remove suppresses auto-capture for the account until its client is gone. Ids are hashed on disk.
void SuppressCapture(const std::string& id);
bool IsCaptureSuppressed(const std::string& id);
void PruneCaptureSuppressions(const std::vector<std::string>& live_ids);

// Build an Account from a captured JX_ env block.
Account FromEnv(std::unordered_map<std::string, std::string> env);

}  // namespace rtx::launcher::accounts
