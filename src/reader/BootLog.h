#pragma once
// The companion's boot record in %USERPROFILE%\rtx_ring.log: every hook it attached or could not
// find when it loaded into a game client, read the same way from the launcher and the command line.
#include <cstdint>
#include <string>
#include <vector>

namespace rtx::bootlog {

struct Hook {
    std::string name;            // the shared signature name (t4-display, framer, ...)
    std::string state;           // ATTACHED, NOT FOUND, AMBIGUOUS, REFUSED, FAILED
    std::uint32_t rva = 0;
    std::string note;
};

struct Boot {
    bool found = false;
    std::string at;              // the boot line's clock
    std::vector<Hook> hooks;
    int attached = -1, missing = -1;   // the closing summary line; -1 when the record has none
    std::vector<std::string> notes;    // other start-up lines (engine ops, components, compositor)
    std::vector<std::string> beats;    // heartbeat lines, oldest first (the last few)
};

Boot Read(std::uint32_t pid);

}  // namespace rtx::bootlog
