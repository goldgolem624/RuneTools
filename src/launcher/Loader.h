#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rtx::launcher::loader {

// Search the standard install locations for an RS3 client executable.
// Empty string if none of the well-known paths exist.
std::wstring DefaultRsClientPath();

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
