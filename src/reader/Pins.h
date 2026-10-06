#pragma once
// rtx_pins.tsv: what this build of the app expects of the game. One record per line: kind, key,
// expect, features ('#' starts a comment). Cache kinds record what the code relies on about an id
// (a varbit's var and bits, an enum's types and the values read); per-build kinds (build, rva,
// sig, disp, op) record what a known client exe looked like; live lines hold predicates on values
// read from a running client. The update check compares each with what is there now and names
// what moved and the features it breaks.
#include "HealthRun.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rtx::pins {

struct Line { std::string kind, key, expect, features; int no = 0; };

// The manifest, reloaded when the file changes. Looked for beside the exe, then in the source
// tree's ui-assets when running from a build folder.
std::shared_ptr<const std::vector<Line>> Lines();
void UseFile(const std::wstring& path);   // a manifest other than the shipped one (tests, --pins-check)
bool Overridden();               // UseFile is in force: a test run, kept out of the history and builds.txt
std::wstring File();             // empty when not found
std::string Made();              // the "#made" header: date, build, cache
std::string Error();             // why it could not be read, empty when fine

// "k=v;k=v" field, empty when absent.
std::string Field(const std::string& expect, const std::string& name);
// Per-stamp value in "6a9986f8=0xD98BB0;6a998810=0xC70BB0", empty when the stamp has none.
std::string StampValue(const std::string& expect, std::uint32_t stamp);
// An RVA the manifest pins for this exe (kind rva, key `name`), 0 when none.
std::uint32_t PinnedRva(const char* name, std::uint32_t stamp);
// The manifest's build line for this stamp, empty when the exe is not a validated one.
std::string BuildLine(std::uint32_t stamp);
Line Find(const std::string& kind, const std::string& key);   // kind empty when there is none

// Content: every cache pin compared with the cache now, one row per feature. `runtimeJson` is the
// panels' own declaration ({"Feature":{"varbit":[..],..},..}): ids it names that the manifest does
// not record get an existence check.
void CheckContent(rtx::health::Run& run, const std::string& runtimeJson);
// Cache format: index lines (archive counts, file split, protocol) and id ceilings.
void CheckCacheFormat(rtx::health::Run& run);

// --pins-record: for each "kind key want features" line of `idsTsv`, the expect column from the
// cache now; ids that do not resolve are left out (and listed in `log`). Lines of other kinds
// pass through unchanged. Returns the number of records written, -1 when the cache is closed.
int Record(const std::wstring& idsTsv, const std::wstring& outTsv, std::string& log);
// --pins-check: the content and cache format groups as text, no game needed.
std::string CheckText(const std::wstring& manifest);

}  // namespace rtx::pins
