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
    std::string via;             // "sig" or "anchor" when the line says how the hit was found
    std::uint32_t sigRva = 0;    // the signature's address when an anchor found the hit instead
    std::string anchor;          // "0x..", "agrees", "ambiguous(n)" or "none": what the anchor said
};

// One self-check the companion ran after resolving its targets (grammar in Signatures.h):
// "check: <name> OK|FAIL|SKIP kind=<..> exp=<..> got=<..> features=<a|b> need=<..> ; <detail>", "-" for an empty value.
struct Check {
    std::string name;            // scene-root, player-entity, ground-stacks, framer-conn, markers, menu-record, hover-object, highlight, packets, engine-ops
    std::string state;           // OK, FAIL, SKIP
    std::string kind, exp, got, features, need, text;
};

struct Boot {
    bool found = false;
    std::string at;              // the boot line's clock
    std::vector<Hook> hooks;
    std::vector<Check> checks;
    int attached = -1, missing = -1;   // the closing summary line; -1 when the record has none
    std::vector<std::string> notes;    // other start-up lines (engine ops, components, compositor)
    std::vector<std::string> beats;    // heartbeat lines, oldest first (the last few)
};

Boot Read(std::uint32_t pid);

// The self-checks every companion built from this source reports (Signatures.h kCheckNames); one
// missing from a boot record was not run (an older companion, or a module that never got that far).
const std::vector<std::string>& ExpectedChecks();

}  // namespace rtx::bootlog
