#pragma once
// Lua plugin runtime. Each Lua plugin runs in its own sandboxed Lua 5.4 state inside the launcher,
// on the UI thread, driven by the same 250 ms plugin push that feeds JavaScript plugins. Every
// rtx.* call a Lua plugin makes is forwarded to the page's PLUGIN_API through __rtxLuaCall, so scope
// checks, rate limits and data sources are shared with the JavaScript SDK one to one.
//
// The JavaScriptCore wrappers (luaLoad, luaTick, ...) live in Bridge.cpp; this header is the plain
// C++ API they call, which the --lua-selftest command line mode also drives without a page.
#include <JavaScriptCore/JavaScript.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rtx::launcher::lua {

// Answers a plugin's rtx.* call: (pluginId, method, argsJson) -> reply JSON. Replies are
// {"ok":true,"v":<value>}, {"ok":false,"e":"<reason>"} or {"pending":true} (an asynchronous
// PLUGIN_API method whose value arrives on a later call). The default handler invokes the page's
// window.__rtxLuaCall in the context set by ScopedContext; the self test installs a stub.
using CallHandler = std::function<std::string(const std::string& pluginId, const std::string& method,
                                              const std::string& argsJson)>;
void SetCallHandler(CallHandler h);

// Makes a JavaScript context available to the default call handler for the lifetime of the
// scope. Every bridge wrapper that can run plugin code creates one.
struct ScopedContext {
    explicit ScopedContext(JSContextRef ctx);
    ~ScopedContext();
};

// Creates the plugin's state, loads its main chunk and returns a result JSON:
//   {"ok":bool,"error":string|null,"log":[...],"ui":<tree>|null,"wantsState":bool,"version":"Lua 5.4.7"}
// `methodsJson` lists the PLUGIN_API methods as [{"name":"state.player","scope":"state.read"|null},...]
// so the Lua namespaces mirror the JavaScript SDK exactly. `scopes` are the granted scopes.
std::string Load(const std::string& id, const std::filesystem::path& root,
                 const std::vector<std::string>& scopes, const std::string& methodsJson);
void Unload(const std::string& id);
bool IsLoaded(const std::string& id);

// One host tick: fires timers and tick handlers, then the events batch (JSON array or "") and the
// state snapshot (JSON object or ""). Returns the same result shape as Load plus "failed".
std::string Tick(const std::string& id, const std::string& eventsJson, const std::string& stateJson);
// Delivers a host event ("settings", "theme", ...) with a JSON payload.
std::string Event(const std::string& id, const std::string& kind, const std::string& payloadJson);
// Delivers a widget interaction from the page (button click, toggle, input) to the plugin.
std::string UiEvent(const std::string& id, const std::string& widget, const std::string& valueJson);
// Diagnostics: {"loaded":bool,"memory":bytes,"dispatches":n,"cpuMs":x,"errors":n,"log":[last 200]}
std::string Info(const std::string& id);

// Runtime version string, "Lua 5.4.7".
const char* RuntimeVersion();

// Loads the plugin at `pluginDir` against a stub host, runs a few ticks and interactions, and
// returns 0 when no error was raised. `report` receives a human readable transcript.
int SelfTest(const std::filesystem::path& pluginDir, std::string& report);

}  // namespace rtx::launcher::lua
