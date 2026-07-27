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
    // False when the client is out of reach: it could not be opened for read (access denied),
    // almost always because it was launched elevated (Jagex Launcher as administrator) against
    // a medium-integrity launcher. Such a client can't be read or embedded.
    bool          accessible = true;
};

// Snapshot every running RS3 client process (rs2client.exe /
// RuneScape.exe / rs3client.exe), sorted ascending by PID. Empty vector
// if none are running.
std::vector<Info> ScanRsClients();

// Hard-terminate by PID. Returns false if the process is gone /
// inaccessible / rights are insufficient.
bool TerminateByPid(std::uint32_t pid);

// Walk the target PEB for its JX_ env block (display name, character
// id, session tokens). Empty map on any failure; never throws.
std::unordered_map<std::string, std::string> ReadJxEnv(std::uint32_t pid);

}  // namespace rtx::launcher::process
