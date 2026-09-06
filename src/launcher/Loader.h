#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rtx::launcher::loader {

// RuneScape.exe to launch: the user's saved override when one is set and still exists,
// otherwise the auto-detected install. Empty when neither resolves.
std::wstring DefaultRsClientPath();

// The auto-detected RuneScape.exe only (registry, Steam libraries, drive scan). Empty if none.
std::wstring AutoRsClientPath();

// User override, persisted in the RuneToolsX data folder. Get returns "" when unset or the
// file has since vanished; Set with "" clears it. Set rejects anything that is not an
// existing RuneScape.exe.
std::wstring CustomRsClientPath();
// Returns "" on success, otherwise a user-facing reason (wrong file, not signed by Jagex, ...).
std::string  SetCustomRsClientPath(const std::wstring& path);

// Authenticode check: the file carries a valid, trusted signature whose signer is Jagex.
// `subject` receives the signer's organisation (or common name) when one could be read,
// signed or not, so a refusal can say who actually signed the file.
struct SignerCheck {
    bool        ok;        // valid chain and a Jagex signer
    bool        signed_;   // some valid signature was present
    std::string subject;   // signer name as read from the certificate
    std::string reason;    // user-facing reason when !ok
};
SignerCheck VerifyGameSigner(const std::wstring& path);

struct LaunchResult {
    bool          success;
    std::string   detail;
    std::uint32_t pid;       // 0 when the process didn't start
};

// CreateProcess(rs_client.exe). Empty path uses the default.
LaunchResult LaunchClient(const std::wstring& rs_client_path);

// Same, with JX_ env-var overrides (everything else inherits from the
// current process). Used to spawn a saved account.
LaunchResult LaunchClientWithEnv(
    const std::wstring& rs_client_path,
    const std::unordered_map<std::string, std::string>& env_overrides);

}  // namespace rtx::launcher::loader
