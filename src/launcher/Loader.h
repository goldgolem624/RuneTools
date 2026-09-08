#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rtx::launcher::loader {

std::wstring DefaultRsClientPath();

std::wstring AutoRsClientPath();

std::wstring CustomRsClientPath();
std::string  SetCustomRsClientPath(const std::wstring& path);

// Authenticode check: valid trusted signature with a Jagex signer.
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

LaunchResult LaunchClient(const std::wstring& rs_client_path);

LaunchResult LaunchClientWithEnv(
    const std::wstring& rs_client_path,
    const std::unordered_map<std::string, std::string>& env_overrides);

}  // namespace rtx::launcher::loader
