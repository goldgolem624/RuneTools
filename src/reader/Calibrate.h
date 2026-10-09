#pragma once
// Client offsets found from the client itself, so a game update moves them without a rebuild.
//
// The client registers every scripting-engine operation in one routine, each with the same two
// instructions: the operation's number and the address of its handler. That gives every handler.
// The numbers are scrambled per build, but the cache names them: the export the launcher already
// keeps (cs2\opcodes.json) maps each name to this build's number, and is only used when the build
// the launcher labelled it with (cs2\client_version.txt) is the exe's own and five handlers that
// name themselves in their error text carry the numbers the table gives them. A handler reads the
// client's state through fixed displacements from the root, and those displacements are the
// offsets the reader needs. Each rule below names an operation and the instruction shape to look
// for in its handler; the displacement in that instruction is the offset. A rule whose shape is
// found with two different displacements, or not at all, yields nothing, and the compiled
// constant stays in force.
//
// Static read of the exe on disk: no process is touched.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtx::calib {

enum class Outcome : int { Found, Moved, NotFound, Ambiguous, NoOp, NoHandler };

struct Found {
    const char*  name;       // the constant's name in Reader.cpp
    std::uint32_t compiled;  // what the build was compiled with
    std::uint32_t found;     // what the exe says; 0 = not found
    const char*  op;         // the operation that proved it
    Outcome      status = Outcome::NotFound;
    bool         ref = false;   // a compiled table's offset: reported against its compiled value
    bool         applied = false;   // read through Use() by the reader: the found value is in force
    const char*  what = nullptr;    // what the client keeps there, when the name alone does not say
};

// Runs once per exe file and table (path + size + write time); later calls return the same table.
// `opcodesJson` is the launcher's cs2\opcodes.json, with client_version.txt beside it. Empty
// result: the exe or the table could not be read, the table is from another build, a handler that
// names itself sits under another number, or the registrar was not recognised; nothing is
// calibrated then and Why() says which.
const std::vector<Found>& Run(const std::wstring& exePath, const std::wstring& opcodesJson);

// The offset to use for `name`: the found one when there is one, else `compiled`. A rule read this
// way counts as applied: Results() lists it and References() no longer does.
std::uint32_t Use(const char* name, std::uint32_t compiled);

// One line per rule, for the log.
std::string Report();

// The rules as the last Run left them (compiled values filled in by Use): the reader's own offsets
// and every table offset read through Use(). References() are the rules for offsets the compiled
// tables (MainDataOffsets.h, SceneOffsets.h, literals in the companion) still carry unapplied: what
// the client says, against the compiled value, ready for an update.
std::vector<Found> Results();
std::vector<Found> References();

// --calib-check: every rule against an exe and an operation table on disk, as text. The table's
// build label is not required (the self-naming handlers still have to agree); nothing is applied.
std::string CheckText(const std::wstring& exePath, const std::wstring& opcodesJson);

// Why nothing was calibrated, empty when the rules ran. Kept with the last Run.
std::string Why();

// The operation table's own state at the last Run.
struct TableState {
    int handlers = 0, names = 0;
    std::string label;           // client_version.txt as written
    std::string exeVersion;      // the exe's version resource
    std::uint32_t exeStamp = 0;  // the exe's PE time stamp
    bool usable = false;
};
TableState Table();

// The handlers that name themselves in their error text, checked against the table's numbers.
struct SpotCheck { std::string text, op; int number = -1; std::string owners; int ok = -1; };   // ok: 1 agrees, 0 disagrees, -1 not checkable
std::vector<SpotCheck> SpotChecks();

// Every handler the exe on disk registers, by this build's operation number: number -> handler RVA.
// Read now, not cached. Empty when the exe or its registrar cannot be read.
std::map<std::uint32_t, std::uint32_t> ExeHandlers(const std::wstring& exePath);

// ---- PE helpers shared with the code check ----
struct Section { std::string name; std::uint32_t rva = 0, vsize = 0, raw = 0, rawSize = 0, flags = 0; };
struct Pe {
    const std::uint8_t* base = nullptr; std::size_t size = 0;
    std::uint64_t imageBase = 0;
    std::uint32_t textRva = 0, textRaw = 0, textSize = 0;
    std::uint32_t stamp = 0, sizeOfImage = 0;
    std::uint32_t importRva = 0, importSize = 0, pdataRva = 0, pdataSize = 0;
    std::vector<Section> sections;
};
bool ReadFile(const std::wstring& path, std::vector<std::uint8_t>& out);
bool ParsePe(const std::vector<std::uint8_t>& f, Pe& pe);
const std::uint8_t* At(const Pe& pe, std::uint32_t rva, std::size_t n);   // file bytes at an RVA, null when outside the raw data
const Section* SectionOf(const Pe& pe, std::uint32_t rva);
bool Handlers(const Pe& pe, std::map<std::uint32_t, std::uint32_t>& outByOp);   // op -> handler RVA
bool OpTable(const std::wstring& path, std::map<std::string, std::uint32_t>& out);
std::string ExeBuild(const std::wstring& path);
std::wstring FileKey(const std::wstring& path);

// Handlers whose body takes the address of the string ending in `text` (lea reg,[rip+..]).
std::vector<std::uint32_t> HandlersNaming(const Pe& pe, const std::map<std::uint32_t, std::uint32_t>& byOp, const char* text);

}  // namespace rtx::calib
