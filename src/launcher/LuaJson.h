#pragma once
// JSON <-> Lua value conversion for the Lua plugin host. Every value that crosses between a Lua
// plugin and the page's PLUGIN_API travels as JSON text, exactly like the JavaScript plugin
// broker's postMessage payloads, so both runtimes see identical shapes.
#include <cstddef>
#include <string>

struct lua_State;

namespace rtx::launcher::luajson {

// Parses `text` and pushes the equivalent Lua value: objects and arrays become tables, null
// becomes nil, numbers become integers when they have no fraction or exponent and fit in 64 bits.
// Returns false (and pushes nil) when the text is not valid JSON or nests deeper than 128 levels.
bool Push(lua_State* L, const char* text, std::size_t len);
inline bool Push(lua_State* L, const std::string& s) { return Push(L, s.data(), s.size()); }

// Serialises the Lua value at `idx`. A table whose keys are exactly 1..n is an array; any other
// table is an object with its keys stringified. Functions, userdata, threads, NaN, infinities,
// cycles and depth beyond 64 become null. Never throws; returns "null" for unsupported input and for
// a value whose text would pass 8 MB.
std::string Dump(lua_State* L, int idx);

}  // namespace rtx::launcher::luajson
