#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtx::launcher::process {

struct Info {
    std::uint32_t pid;
    std::wstring  name;
    std::wstring  path;
    bool          x64;
    // False when the client cannot be opened for read (typically launched elevated).
    bool          accessible = true;
};

// Every running RS3 client process, sorted by PID.
std::vector<Info> ScanRsClients();

// Hard-terminate by PID.
bool TerminateByPid(std::uint32_t pid);

// JX_* environment of the target, read from its PEB. Empty on failure; never throws.
std::unordered_map<std::string, std::string> ReadJxEnv(std::uint32_t pid);

}  // namespace rtx::launcher::process
