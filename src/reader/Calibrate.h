#pragma once
// Client offsets found from the client itself, so a game update moves them without a rebuild.
//
// The client registers every scripting-engine operation in one routine, each with the same two
// instructions: the operation's number and the address of its handler. That gives every handler.
// The numbers are scrambled per build, but the cache names them: the export the launcher already
// keeps (cs2\opcodes.json) maps each name to this build's number. A handler reads the client's
// state through fixed displacements from the root, and those displacements are the offsets the
// reader needs. Each rule below names an operation and the instruction shape to look for in its
// handler; the displacement in that instruction is the offset. A rule whose shape is found more
// than once, or not at all, yields nothing, and the compiled constant stays in force.
//
// Static read of the exe on disk: no process is touched.
#include <cstdint>
#include <string>
#include <vector>

namespace rtx::calib {

struct Found {
    const char*  name;       // the constant's name in Reader.cpp
    std::uint32_t compiled;  // what the build was compiled with
    std::uint32_t found;     // what the exe says; 0 = not found
    const char*  op;         // the operation that proved it
};

// Runs once per exe file (path + size + write time); later calls return the same table.
// `opcodesJson` is the launcher's cs2\opcodes.json. Empty result: the exe or the table could not
// be read, or the registrar was not recognised, and nothing is calibrated.
const std::vector<Found>& Run(const std::wstring& exePath, const std::wstring& opcodesJson);

// The offset to use for `name`: the found one when there is one, else `compiled`.
std::uint32_t Use(const char* name, std::uint32_t compiled);

// One line per rule, for the health report and the log.
std::string Report();

}  // namespace rtx::calib
