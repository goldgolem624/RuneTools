#include "LuaHost.h"
#include "LuaJson.h"
#include "BridgeUtil.h"
#include "Update.h"
#include "../shared/Log.h"

#include "../lua/vendor/lua-5.4.7/lua.hpp"

#include <chrono>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <utility>

namespace rtx::launcher::lua {
namespace {

constexpr std::size_t kMemLimit      = 64 * 1024 * 1024;   // per plugin
constexpr long long   kInstrBudget   = 40'000'000;          // per dispatch (one tick, one event)
// Instructions between two budget checks. One instruction can be slow on its own (joining or comparing
// long strings), so this is kept short enough that a run of them cannot hold the thread for long.
constexpr int         kHookInterval  = 1'000;
constexpr double      kWallBudgetMs  = 1500.0;              // per dispatch
constexpr int         kMaxErrors     = 8;                   // consecutive dispatch errors before the plugin stops
// A plugin also stops after kMaxErrors dispatches ran past the budget within this window, even with
// good ticks between them (a good tick resets only the consecutive count).
constexpr std::chrono::minutes kOverrunWindow{5};
constexpr std::size_t kLogKeep       = 200;
constexpr std::size_t kFileMax       = 2 * 1024 * 1024;
// Caps for the standard-library calls that loop in C where the instruction hook never fires. These
// are far above any real plugin's use; they only stop input crafted to keep one call running for a
// long time on the launcher UI thread.
constexpr lua_Integer  kTableSpanMax  = 4 * 1024 * 1024;    // elements one table.move/insert/remove/concat walks (more than the memory cap holds)
constexpr lua_Unsigned kPlainFindMax  = 4ull << 30;         // subject bytes times needle bytes for one plain string.find

struct Plugin {
    std::string id;
    std::filesystem::path root;
    std::vector<std::string> scopes;
    lua_State* L = nullptr;
    std::size_t memUsed = 0;
    long long instrUsed = 0;
    bool overBudget = false;              // this dispatch ran out; every further instruction raises
    int activeDepth = 0;                  // dispatches of this plugin now running (a nested one shares the outer budget)
    std::deque<std::chrono::steady_clock::time_point> overruns;   // recent dispatches that ran past the budget
    std::chrono::steady_clock::time_point dispatchStart{};
    std::chrono::steady_clock::time_point started{};
    std::deque<std::string> log;          // kept history
    std::vector<std::string> fresh;       // not yet handed to the page
    std::string uiJson = "null";
    unsigned uiVersion = 0, uiSent = 0;
    std::string lastError;
    int consecutiveErrors = 0, errorsTotal = 0;
    bool failed = false;
    bool wantsState = false;
    unsigned dispatches = 0;
    double cpuMs = 0;
    int trampolineRef = LUA_NOREF;        // the argument-building trampoline, kept in the registry
};

std::recursive_mutex g_mu;   // a plugin call can re-enter the host (ui.settings pushes a settings event back)
// States are kept per client as well as per plugin: each docked game has its own page, grants and
// account storage, so the same plugin running in two clients gets two states. The client is the game
// pid of the page calling in (see current_client), 0 when there is no page (the self test).
using Key = std::pair<std::uint32_t, std::string>;
std::map<Key, std::unique_ptr<Plugin>> g_plugins;
CallHandler g_call;
thread_local JSContextRef t_ctx = nullptr;
thread_local Plugin* t_active = nullptr;   // the plugin whose code is running (hook and natives)
thread_local std::jmp_buf* t_panic = nullptr;   // recovery point for an unprotected Lua error, if any

// ---- memory accounting -------------------------------------------------------------------------
void* l_alloc(void* ud, void* ptr, std::size_t osize, std::size_t nsize) {
    auto* p = static_cast<Plugin*>(ud);
    if (nsize == 0) { if (ptr) { p->memUsed -= osize; std::free(ptr); } return nullptr; }
    std::size_t old = ptr ? osize : 0;
    if (nsize > old && p->memUsed + (nsize - old) > kMemLimit) return nullptr;   // out of memory for the plugin
    void* np = std::realloc(ptr, nsize);
    if (!np) return nullptr;
    p->memUsed = p->memUsed - old + nsize;
    return np;
}

void l_hook(lua_State* L, lua_Debug*) {
    Plugin* p = t_active;
    if (!p) return;
    // Past the budget the hook fires on every instruction and raises each time, so code that catches the
    // error (pcall, a coroutine) is thrown out again at its next instruction until the host has control.
    if (p->overBudget) {
        lua_sethook(L, l_hook, LUA_MASKCOUNT, 1);
        luaL_error(L, "execution budget exceeded");
    }
    const int n = lua_gethookcount(L);
    if (n != kHookInterval) lua_sethook(L, l_hook, LUA_MASKCOUNT, kHookInterval);   // a coroutine left on the per-instruction hook
    p->instrUsed += n;
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p->dispatchStart).count();
    if (p->instrUsed > kInstrBudget || ms > kWallBudgetMs) {
        p->overBudget = true;
        lua_sethook(L, l_hook, LUA_MASKCOUNT, 1);
        // lua_pushfstring knows only plain conversions: %I takes a lua_Integer, and has no width or precision
        if (ms > kWallBudgetMs) luaL_error(L, "execution budget exceeded (%I ms in one tick)", (lua_Integer)ms);
        luaL_error(L, "execution budget exceeded (%I instructions in one tick)", (lua_Integer)kInstrBudget);
    }
}

// Last-resort guard. Lua calls this when an error propagates with no protected call to catch it, and
// otherwise ends the run with abort(), which would take the launcher and every docked game client
// down. A dispatch sets a recovery point (t_panic) before it builds its arguments, so such an error
// jumps back there and becomes a plugin error instead. With no recovery point set we return 0 and let
// Lua abort as before, but the dispatch paths that could hit the memory cap always set one.
int l_atpanic(lua_State*) {
    if (t_panic) std::longjmp(*t_panic, 1);
    return 0;
}

// Log records are JSON objects {"l":level,"t":text,"tag":tag}; the page feeds them to rtxConsole and
// to the plugin's own console strip.
void plog(Plugin* p, const char* level, const std::string& msg, const std::string& tag = std::string()) {
    std::string text = msg;
    if (text.size() > 4000) text.resize(4000);
    std::string tg = tag.size() > 24 ? tag.substr(0, 24) : tag;
    std::string line = "{\"l\":\"" + std::string(level) + "\",\"t\":\"" + json_escape(text) + "\"" +
                       (tg.empty() ? std::string() : ",\"tag\":\"" + json_escape(tg) + "\"") + "}";
    p->log.push_back(line);
    while (p->log.size() > kLogKeep) p->log.pop_front();
    p->fresh.push_back(line);
    if (p->fresh.size() > kLogKeep) p->fresh.erase(p->fresh.begin());
    if (std::strcmp(level, "error") == 0) {
        // the capped text, on one line: one huge message would otherwise use up the log's size budget
        std::string one = text.size() > 400 ? text.substr(0, 400) + "..." : text;
        for (auto& c : one) if (c == '\r' || c == '\n') c = ' ';
        rtx::log::Launcher("lua plugin " + p->id + ": " + one);
    }
}

// Reads a file under the plugin root, refusing anything that escapes it.
bool read_plugin_file(Plugin* p, const std::string& rel, std::string& out) {
    out.clear();
    if (rel.empty() || rel.size() > 260) return false;
    if (rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos || rel[0] == '/' ||
        rel.find(':') != std::string::npos) return false;
    std::error_code ec;
    auto full = std::filesystem::weakly_canonical(p->root / rel, ec);
    if (ec) return false;
    auto root = std::filesystem::weakly_canonical(p->root, ec);
    if (ec) return false;
    auto fs = full.native(), rs = root.native();
    if (fs.size() < rs.size() || fs.compare(0, rs.size(), rs) != 0) return false;
    std::ifstream f(full, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    if (out.size() > kFileMax) { out.clear(); return false; }
    return true;
}

Plugin* self(lua_State* L) { return static_cast<Plugin*>(lua_touserdata(L, lua_upvalueindex(1))); }

void check_budget(lua_State* L);

// ---- host natives (reachable from the prelude only; it captures them in locals) ---------------
// The ones that do real work check the budget on entry, as the library wrappers below do: a loop
// calling one directly (rtx.json.decode is one) makes hundreds of calls between two hook checks.
int h_call(lua_State* L) {
    check_budget(L);
    Plugin* p = self(L);
    const char* method = luaL_checkstring(L, 1);
    std::string args = "[]";
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) args = luajson::Dump(L, 2);
    std::string reply;
    if (g_call) {
        try { reply = g_call(p->id, method, args); }
        catch (const std::exception& e) { reply = "{\"ok\":false,\"e\":\"" + json_escape(e.what()) + "\"}"; }
        catch (...) { reply = "{\"ok\":false,\"e\":\"host call threw\"}"; }
    } else {
        reply = "{\"ok\":false,\"e\":\"host unavailable\"}";
    }
    luajson::Push(L, reply);
    return 1;
}

int h_log(lua_State* L) {
    check_budget(L);
    Plugin* p = self(L);
    const char* level = luaL_optstring(L, 1, "info");
    std::size_t n = 0;
    const char* s = luaL_optlstring(L, 2, "", &n);
    const char* tag = luaL_optstring(L, 3, "");
    const char* lv = (std::strcmp(level, "warn") == 0) ? "warn" : (std::strcmp(level, "error") == 0) ? "error"
                   : (std::strcmp(level, "debug") == 0) ? "debug" : "info";
    // "host" marks the runtime's own notices (unthrottled, shown as the launcher's); a plugin may not use it
    plog(p, lv, std::string(s, n), _stricmp(tag, "host") == 0 ? "plugin" : tag);
    return 0;
}

int h_ui(lua_State* L) {
    check_budget(L);
    Plugin* p = self(L);
    std::string j = (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) ? luajson::Dump(L, 1) : "null";
    if (j.size() > 512 * 1024) { plog(p, "warn", "ui tree ignored: larger than 512 KB"); return 0; }
    if (j != p->uiJson) { p->uiJson = std::move(j); ++p->uiVersion; }
    return 0;
}

int h_now(lua_State* L) {
    Plugin* p = self(L);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p->started).count();
    lua_pushnumber(L, ms);
    return 1;
}

int h_readfile(lua_State* L) {
    check_budget(L);
    Plugin* p = self(L);
    std::string rel = luaL_checkstring(L, 1);
    std::string s;
    if (!read_plugin_file(p, rel, s)) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int h_json_encode(lua_State* L) {
    check_budget(L);
    luaL_checkany(L, 1);
    std::string s = luajson::Dump(L, 1);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int h_json_decode(lua_State* L) {
    check_budget(L);
    std::size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    if (!luajson::Push(L, s, n)) { lua_pushnil(L); lua_pushstring(L, "invalid JSON"); return 2; }
    return 1;
}

int h_want_state(lua_State* L) {
    self(L)->wantsState = lua_toboolean(L, 1) != 0;
    return 0;
}

int h_memory(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)self(L)->memUsed);
    return 1;
}

// ---- bounded standard-library wrappers ----------------------------------------------------------
// The instruction and wall budget in l_hook is checked only between VM instructions, never inside a C
// library call. A few standard functions can loop for a long time in C on hostile input, which would
// freeze the launcher UI thread with no way for the budget to fire. These wrappers check the budget on
// entry, reject input that would keep one call running for long, and hand the rest to the original
// function (upvalue 1). The pattern matcher bounds its own work per call.

// Raises the budget error once this dispatch has run out. Each wrapped call is bounded, but a loop can
// make thousands of them between two hook checks, so the wrappers check the clock here as well.
void check_budget(lua_State* L) {
    Plugin* p = t_active;
    if (!p) return;
    if (p->overBudget) {
        lua_sethook(L, l_hook, LUA_MASKCOUNT, 1);
        luaL_error(L, "execution budget exceeded");
    }
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p->dispatchStart).count();
    if (ms > kWallBudgetMs) {
        p->overBudget = true;
        lua_sethook(L, l_hook, LUA_MASKCOUNT, 1);
        luaL_error(L, "execution budget exceeded (%I ms in one tick)", (lua_Integer)ms);
    }
}

// Runs the original library function (upvalue 1) in this call's own frame rather than through
// lua_call, so its errors name the function and the caller's line exactly as they do unwrapped. None
// of the stock library functions wrapped here reads upvalues of its own.
int forward_upvalue(lua_State* L) {
    return lua_tocfunction(L, lua_upvalueindex(1))(L);
}

// For library functions whose single call is already bounded: only the budget check on entry.
int w_checked(lua_State* L) {
    check_budget(L);
    return forward_upvalue(L);
}

// string.rep(s, n [, sep]): n copies of s joined by sep. When s and sep are both empty the result is
// always empty however large n is, but the stock builder still spins the counter n times copying
// nothing, so short circuit that. Otherwise refuse a size the plugin's memory cap could never hold.
int w_string_rep(lua_State* L) {
    check_budget(L);
    std::size_t l = 0, lsep = 0;
    luaL_checklstring(L, 1, &l);
    lua_Integer n = luaL_checkinteger(L, 2);
    luaL_optlstring(L, 3, "", &lsep);
    if (n > 0) {
        std::size_t unit = l + lsep;
        if (unit == 0) { lua_pushliteral(L, ""); return 1; }
        if ((lua_Unsigned)n > (lua_Unsigned)(kMemLimit / unit)) return luaL_error(L, "resulting string too large");
    }
    return forward_upvalue(L);
}

// string.find(s, p [, init [, plain]]): a plain search (the fourth argument, or a pattern with no
// special characters) runs memchr and memcmp, which in the worst case compare most of the needle at
// every position of the subject. Bound that product.
int w_string_find(lua_State* L) {
    check_budget(L);
    std::size_t ls = 0, lp = 0;
    luaL_checklstring(L, 1, &ls);
    const char* pat = luaL_checklstring(L, 2, &lp);
    bool plain = lua_toboolean(L, 4) || std::string_view(pat, lp).find_first_of("^$*+?.([%-") == std::string_view::npos;
    if (plain && lp > 1 && lp <= ls && (lua_Unsigned)(ls - lp + 1) * lp > kPlainFindMax)
        return luaL_error(L, "string too long to search");
    return forward_upvalue(L);
}

// The iterator string.gmatch returns runs the matcher in C once per call, so it checks the budget each
// time too. The stock iterator keeps its match state in its upvalues: the wrapper closure takes copies
// of them, in order, and runs the stock iterator function (the same one for every state) in its frame.
lua_CFunction g_gmatchIter = nullptr;

int w_gmatch_step(lua_State* L) {
    check_budget(L);
    return g_gmatchIter(L);
}

int w_string_gmatch(lua_State* L) {
    check_budget(L);
    forward_upvalue(L);                                  // pushes the stock iterator
    g_gmatchIter = lua_tocfunction(L, -1);
    int n = 0;
    while (lua_getupvalue(L, -1 - n, n + 1)) ++n;
    lua_pushcclosure(L, w_gmatch_step, n);
    return 1;
}

// utf8.codes returns the stock iterator, which skips a run of continuation bytes in C on each call, so
// a loop calling it directly would go unchecked. Hand back a checked copy of it instead.
int w_utf8_codes(lua_State* L) {
    check_budget(L);
    const int n = forward_upvalue(L);                    // iterator, subject, 0
    lua_pushvalue(L, -n);
    lua_pushcclosure(L, w_checked, 1);
    lua_replace(L, -n - 1);
    return n;
}

// table.move(a1, f, e, t [, a2]): copies a1[f..e] into the destination at t. The stock loop runs
// e-f+1 times in C even when every source slot is nil, so a huge range hangs the thread. Cap the
// count; the stock function still guards the extreme wrap-around case itself.
int w_table_move(lua_State* L) {
    check_budget(L);
    lua_Integer f = luaL_checkinteger(L, 2);
    lua_Integer e = luaL_checkinteger(L, 3);
    (void)luaL_checkinteger(L, 4);
    if (e >= f && (lua_Unsigned)e - (lua_Unsigned)f >= (lua_Unsigned)kTableSpanMax)
        return luaL_error(L, "too many elements to move");
    return forward_upvalue(L);
}

// table.insert(t, [pos,] v) and table.remove(t [, pos]) shift t[pos..#t] by one slot in C. #t comes
// from __len or from the table's border, and either can be made huge. The stock functions read it
// themselves (a __len can answer differently the second time), so for tables the shift is done here,
// from one read of the length, with the stock logic and a cap. Other values go to the stock function,
// which raises the type error.
int w_table_insert(lua_State* L) {
    check_budget(L);
    if (lua_type(L, 1) != LUA_TTABLE) return forward_upvalue(L);
    lua_Integer e = luaL_intop(+, luaL_len(L, 1), 1);   // first empty element
    lua_Integer pos;
    switch (lua_gettop(L)) {
    case 2:                                              // append
        pos = e;
        break;
    case 3:
        pos = luaL_checkinteger(L, 2);
        luaL_argcheck(L, (lua_Unsigned)pos - 1u < (lua_Unsigned)e, 2, "position out of bounds");
        if (e > pos && (lua_Unsigned)e - (lua_Unsigned)pos > (lua_Unsigned)kTableSpanMax)
            return luaL_error(L, "too many elements to shift");
        for (lua_Integer i = e; i > pos; i--) {          // t[i] = t[i - 1]
            lua_geti(L, 1, i - 1);
            lua_seti(L, 1, i);
        }
        break;
    default:
        return luaL_error(L, "wrong number of arguments to 'insert'");
    }
    lua_seti(L, 1, pos);                                 // t[pos] = v
    return 0;
}

int w_table_remove(lua_State* L) {
    check_budget(L);
    if (lua_type(L, 1) != LUA_TTABLE) return forward_upvalue(L);
    lua_Integer size = luaL_len(L, 1);
    lua_Integer pos = luaL_optinteger(L, 2, size);
    if (pos != size)                                     // a given position must be in [1, size + 1]
        luaL_argcheck(L, (lua_Unsigned)pos - 1u <= (lua_Unsigned)size, 2, "position out of bounds");
    if (pos < size && (lua_Unsigned)size - (lua_Unsigned)pos > (lua_Unsigned)kTableSpanMax)
        return luaL_error(L, "too many elements to shift");
    lua_geti(L, 1, pos);                                 // result = t[pos]
    for (; pos < size; pos++) {                          // t[pos] = t[pos + 1]
        lua_geti(L, 1, pos + 1);
        lua_seti(L, 1, pos);
    }
    lua_pushnil(L);
    lua_seti(L, 1, pos);                                 // remove t[pos]
    return 1;
}

// table.concat(t [, sep [, i [, j]]]) walks t[i..j] in C, j defaulting to #t. Cap the span and hand
// the stock function the bounds read here, so it cannot read a different #t.
int w_table_concat(lua_State* L) {
    check_budget(L);
    if (lua_type(L, 1) != LUA_TTABLE) return forward_upvalue(L);
    lua_Integer last = luaL_len(L, 1);
    luaL_optlstring(L, 2, "", nullptr);
    lua_Integer i = luaL_optinteger(L, 3, 1);
    last = luaL_optinteger(L, 4, last);
    if (i <= last && (lua_Unsigned)last - (lua_Unsigned)i >= (lua_Unsigned)kTableSpanMax)
        return luaL_error(L, "too many elements to concatenate");
    lua_settop(L, 2);
    lua_pushinteger(L, i);
    lua_pushinteger(L, last);
    return forward_upvalue(L);
}

// Comparator handed to the stock sort: checks the budget, then compares as the stock sort would with
// the plugin's comparator (upvalue 1), or with "<" when there is none.
int sort_compare(lua_State* L) {
    check_budget(L);
    if (lua_isnil(L, lua_upvalueindex(1))) {
        lua_pushboolean(L, lua_compare(L, 1, 2, LUA_OPLT));
        return 1;
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, 2, 1);
    return 1;
}

// table.sort(t [, comp]) runs its quicksort in C over #t elements, and #t can be made huge; a single
// comparison of two long strings is slow as well. Every comparison goes through sort_compare, so the
// budget still applies inside the sort. A comparator of the wrong type goes to the stock function,
// which raises the type error.
int w_table_sort(lua_State* L) {
    check_budget(L);
    if (lua_type(L, 1) == LUA_TTABLE && (lua_isnoneornil(L, 2) || lua_type(L, 2) == LUA_TFUNCTION)) {
        lua_settop(L, 2);
        lua_pushcclosure(L, sort_compare, 1);           // takes the comparator (or nil) as its upvalue
    }
    return forward_upvalue(L);
}

// Replaces field `name` on the library table at the top of the stack with `wrapper`, handing the
// wrapper the original function as its first upvalue.
void wrap_library_fn(lua_State* L, const char* name, lua_CFunction wrapper) {
    lua_getfield(L, -1, name);
    lua_pushcclosure(L, wrapper, 1);
    lua_setfield(L, -2, name);
}

// Gives every function of the table at the top of the stack that has no wrapper yet the entry check
// alone (w_checked). Each such call is bounded, but long strings make some slow (upper, utf8.len,
// os.date) and a loop makes hundreds of calls between two hook checks. A stock library function has
// no upvalues; a wrapper holds the original as one.
void wrap_rest(lua_State* L) {
    lua_pushnil(L);
    while (lua_next(L, -2)) {                            // table, key, value
        bool stock = lua_iscfunction(L, -1) != 0;
        if (stock && lua_getupvalue(L, -1, 1)) { lua_pop(L, 1); stock = false; }
        if (stock) {
            lua_pushcclosure(L, w_checked, 1);           // table, key, wrapper
            lua_pushvalue(L, -2);
            lua_insert(L, -2);                           // table, key, key, wrapper
            lua_rawset(L, -4);                           // an existing field, so the traversal goes on
        } else {
            lua_pop(L, 1);
        }
    }
}

// ---- the prelude: builds the rtx table and returns the dispatch function -------------------------
// Runs once per plugin with (host, id, scopes, methods, version, source). Everything the plugin can
// reach is defined here; the host table itself is never exposed.
const char kPrelude[] = R"LUA(
local host, PLUGIN_ID, SCOPES, METHODS, VERSION = ...
local rawload, rawpcall, rawtype, rawpairs, rawipairs, rawtostring, rawselect = load, pcall, type, pairs, ipairs, tostring, select
local rawerror, rawsetmt, rawnext, rawgetf = error, setmetatable, next, rawget
local tinsert, tconcat, tremove = table.insert, table.concat, table.remove

-- Finalizers run whenever the collector gets to them, outside any dispatch and its budget, so a plugin
-- may not register one. A metatable marks its table for finalization only if __gc is present when set.
_G.setmetatable = function(t, mt)
  if rawtype(mt) == "table" and rawgetf(mt, "__gc") ~= nil then rawerror("setmetatable: __gc is not available to plugins", 2) end
  return rawsetmt(t, mt)
end

local rtx = {}
_G.rtx = rtx

rtx.json = { encode = host.jsonEncode, decode = host.jsonDecode }

local scopeSet = {}
for _, s in rawipairs(SCOPES) do scopeSet[s] = true end

local readyFired = false
local handlers = { ready = {}, tick = {}, state = {}, events = {}, settings = {}, theme = {} }

rtx.plugin = {
  id = function() return PLUGIN_ID end,
  apiVersion = function() return "1.0" end,
  runtime = function() return "lua" end,
  runtimeVersion = function() return VERSION end,
  grantedScopes = function() local t = {} for i, s in rawipairs(SCOPES) do t[i] = s end return t end,
  hasScope = function(s) return scopeSet[s] == true end,
  ready = function(fn) if rawtype(fn) == "function" then tinsert(handlers.ready, fn) end end,
  readFile = function(rel) return host.readfile(rel) end,
}

-- rtx.call: the one path every namespace method uses (mirrors the JavaScript broker exactly)
rtx.lastError = nil
local warned = {}
local function call(method, args)
  local r = host.call(method, args)
  if rawtype(r) ~= "table" then rtx.lastError = "host unavailable"; return nil, rtx.lastError end
  if r.pending then rtx.lastError = nil; return nil, "pending" end
  if r.ok then rtx.lastError = nil; return r.v end
  rtx.lastError = r.e or "call failed"
  if not warned[method] then warned[method] = true; host.log("warn", method .. ": " .. rtx.lastError) end
  return nil, rtx.lastError
end
rtx.call = call

-- namespaces generated from the host's PLUGIN_API table: rtx.state.player(), rtx.cache.itemInfo(id), ...
for _, m in rawipairs(METHODS) do
  local ns, fn = m.name:match("^([%w_]+)%.([%w_]+)$")
  if ns and fn then
    rtx[ns] = rtx[ns] or {}
    local name = m.name
    rtx[ns][fn] = function(...) return call(name, {...}) end
  end
end

-- host events: ready, tick, state, events, settings, theme
function rtx.on(name, fn)
  local h = handlers[name]
  if not h then rawerror("rtx.on: unknown event '" .. rawtostring(name) .. "'", 2) end
  if rawtype(fn) ~= "function" then rawerror("rtx.on: handler must be a function", 2) end
  tinsert(h, fn)
  if name == "state" then host.wantState(true) end
  return fn
end
function rtx.off(name, fn)
  local h = handlers[name]
  if not h then return false end
  for i = #h, 1, -1 do if h[i] == fn then tremove(h, i) return true end end
  return false
end

-- game events: rtx.events.on(kind, fn) with "*" for every kind
local subs = {}
rtx.events = rtx.events or {}
function rtx.events.on(kind, fn)
  if rawtype(kind) ~= "string" or rawtype(fn) ~= "function" then rawerror("rtx.events.on(kind, fn)", 2) end
  subs[kind] = subs[kind] or {}
  tinsert(subs[kind], fn)
  return fn
end
function rtx.events.off(kind, fn)
  local l = subs[kind]
  if not l then return false end
  for i = #l, 1, -1 do if l[i] == fn then tremove(l, i) return true end end
  return false
end

-- timers, resolved on the host tick (about 4 times a second)
local timers, nextTimer = {}, 1
rtx.timer = {}
local function addTimer(seconds, fn, every)
  if rawtype(seconds) ~= "number" or seconds < 0 then rawerror("rtx.timer: seconds must be a non-negative number", 3) end
  if rawtype(fn) ~= "function" then rawerror("rtx.timer: callback must be a function", 3) end
  local id = nextTimer; nextTimer = nextTimer + 1
  timers[id] = { at = host.now() + seconds * 1000, fn = fn, every = every and seconds * 1000 or nil }
  return id
end
function rtx.timer.after(seconds, fn) return addTimer(seconds, fn, false) end
function rtx.timer.every(seconds, fn) return addTimer(seconds, fn, true) end
function rtx.timer.cancel(id) timers[id] = nil end

-- logging: print goes to the plugin console in the panel
local function fmtArgs(...)
  local n = rawselect("#", ...)
  local parts = {}
  for i = 1, n do
    local v = rawselect(i, ...)
    if rawtype(v) == "table" then parts[i] = host.jsonEncode(v) else parts[i] = rawtostring(v) end
  end
  return tconcat(parts, " ")
end
-- rtx.console mirrors rtx.plugin.console of the JavaScript SDK: debug/info/warn/error, and
-- scoped(tag) for a logger whose lines carry a tag the Console panel can filter on.
local function mkConsole(tag)
  return {
    debug = function(...) host.log("debug", fmtArgs(...), tag) end,
    info  = function(...) host.log("info",  fmtArgs(...), tag) end,
    log   = function(...) host.log("info",  fmtArgs(...), tag) end,
    warn  = function(...) host.log("warn",  fmtArgs(...), tag) end,
    error = function(...) host.log("error", fmtArgs(...), tag) end,
  }
end
rtx.console = mkConsole("")
rtx.console.scoped = function(tag) return mkConsole(rawtostring(tag or ""):sub(1, 24)) end
rtx.log, rtx.debug, rtx.warn, rtx.error = rtx.console.info, rtx.console.debug, rtx.console.warn, rtx.console.error
_G.print = rtx.log

-- ui: a widget tree the page renders; callbacks are kept here and dispatched by widget id
local uiHandlers = {}
local autoId = 0
local function prepare(node, depth, count)
  if rawtype(node) ~= "table" then return nil, count end
  if depth > 8 or count > 500 then return nil, count end
  count = count + 1
  local out = {}
  local id = node.id
  if rawtype(id) ~= "string" or id == "" then id = nil end
  for k, v in rawpairs(node) do
    if rawtype(v) == "function" then
      if not id then autoId = autoId + 1; id = "w" .. autoId end
      uiHandlers[id .. ":" .. k] = v
      out[k] = true
    elseif k == "children" and rawtype(v) == "table" then
      local kids = {}
      for i, c in rawipairs(v) do local pc; pc, count = prepare(c, depth + 1, count); if pc then kids[#kids + 1] = pc end end
      out.children = kids
    elseif rawtype(v) ~= "table" or k == "options" or k == "rows" or k == "columns" or k == "items" then
      out[k] = v
    end
  end
  if id then out.id = id end
  return out, count
end
rtx.ui = rtx.ui or {}
function rtx.ui.render(tree)
  uiHandlers = {}
  autoId = 0
  if tree == nil then host.ui(nil) return end
  if rawtype(tree) ~= "table" then rawerror("rtx.ui.render expects a table (a node or a list of nodes)", 2) end
  local list = tree
  if tree.type ~= nil then list = { tree } end
  local nodes, count = {}, 0
  for i, n in rawipairs(list) do local pn; pn, count = prepare(n, 1, count); if pn then nodes[#nodes + 1] = pn end end
  host.ui(nodes)
end
function rtx.ui.clear() rtx.ui.render(nil) end
function rtx.ui.setTitle(t) return call("ui.setTitle", { t }) end
function rtx.ui.settings(schema) return call("ui.settings", { schema }) end

rtx.settings = rtx.settings or {}
function rtx.settings.get() return call("settings.get", {}) or {} end
function rtx.settings.on(fn) return rtx.on("settings", fn) end

-- require: plugin-local modules only ("./util", "lib.colors", "sub/mod")
local loaded = {}
local function modulePath(name)
  if rawtype(name) ~= "string" or name == "" then rawerror("require expects a module name", 3) end
  local p = name:gsub("\\", "/")
  if p:sub(1, 2) == "./" then p = p:sub(3) end
  if p:sub(1, 1) == "/" or p:find("%.%.") or p:find(":") then rawerror("require: module path must stay inside the plugin folder: " .. name, 3) end
  if not p:find("/") and not p:find("%.lua$") then p = p:gsub("%.", "/") end
  if not p:find("%.lua$") then p = p .. ".lua" end
  return p
end
function _G.require(name)
  local path = modulePath(name)
  local cached = loaded[path]
  if cached ~= nil then return cached end
  local src = host.readfile(path)
  if not src then rawerror("module not found: " .. name .. " (looked for " .. path .. ")", 2) end
  local chunk, err = rawload(src, "=" .. path, "t")
  if not chunk then rawerror(err, 2) end
  local res = chunk(name)
  if res == nil then res = true end
  loaded[path] = res
  return res
end
_G.load = function(chunk, name, mode, env) return rawload(chunk, name, "t", env) end
_G.dofile = nil
_G.loadfile = nil
_G.loadstring = nil

-- dispatch: called by the host with (kind, a, b)
local function safe(fn, ...)
  local ok, err = rawpcall(fn, ...)
  if not ok then
    local msg = rawtostring(err)
    if msg:find("execution budget exceeded", 1, true) then rawerror(msg, 0) end
    host.log("error", msg)
  end
  return ok
end
local function dispatch(kind, a, b)
  if kind == "tick" then
    if not readyFired then readyFired = true; for _, fn in rawipairs(handlers.ready) do safe(fn) end end
    local now = host.now()
    for id, t in rawpairs(timers) do
      if now >= t.at then
        if t.every then t.at = now + t.every else timers[id] = nil end
        safe(t.fn)
      end
    end
    for _, fn in rawipairs(handlers.tick) do safe(fn) end
  elseif kind == "state" then
    for _, fn in rawipairs(handlers.state) do safe(fn, a) end
  elseif kind == "events" then
    if rawtype(a) == "table" then
      for _, ev in rawipairs(a) do
        local l = subs[ev.kind]
        if l then for _, fn in rawipairs(l) do safe(fn, ev) end end
        local all = subs["*"]
        if all then for _, fn in rawipairs(all) do safe(fn, ev) end end
      end
      for _, fn in rawipairs(handlers.events) do safe(fn, a) end
    end
  elseif kind == "settings" then
    for _, fn in rawipairs(handlers.settings) do safe(fn, a) end
  elseif kind == "theme" then
    for _, fn in rawipairs(handlers.theme) do safe(fn, a) end
  elseif kind == "ui" then
    local h = uiHandlers[a]
    if h then safe(h, b) end
  end
end
return dispatch
)LUA";

// ---- state construction ------------------------------------------------------------------------
void open_sandbox(lua_State* L) {
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    // the base functions one call of which can be slow (compiling a long chunk, a full collection,
    // converting or comparing long strings, next walking a sparse table). This runs before the
    // prelude takes its own copies, so those are checked too.
    for (const char* k : {"load", "collectgarbage", "tostring", "tonumber", "select", "next", "rawequal"})
        wrap_library_fn(L, k, w_checked);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);   // also the string metatable's __index
    wrap_library_fn(L, "rep", w_string_rep);
    wrap_library_fn(L, "find", w_string_find);
    wrap_library_fn(L, "gmatch", w_string_gmatch);
    wrap_rest(L);                                          // match, gsub and the rest
    lua_pop(L, 1);
    lua_pushliteral(L, "");                                // the string metatable's arithmetic converts long strings
    if (lua_getmetatable(L, -1)) { wrap_rest(L); lua_pop(L, 1); }
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    wrap_library_fn(L, "move", w_table_move);
    wrap_library_fn(L, "insert", w_table_insert);
    wrap_library_fn(L, "remove", w_table_remove);
    wrap_library_fn(L, "concat", w_table_concat);
    wrap_library_fn(L, "sort", w_table_sort);
    wrap_rest(L);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    wrap_library_fn(L, "codes", w_utf8_codes);
    wrap_rest(L);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);
    // os: clock, time, date and difftime only (no exit, getenv, remove, rename, execute, tmpname)
    luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 0);
    lua_newtable(L);
    for (const char* k : {"clock", "time", "date", "difftime"}) {
        lua_getfield(L, -2, k); lua_setfield(L, -2, k);
    }
    wrap_rest(L);                                          // os.date walks its whole format string
    lua_setglobal(L, "os");
    lua_pop(L, 1);
    // no io, debug or package libraries; no dofile/loadfile
    for (const char* k : {"dofile", "loadfile", "io", "debug", "package", "require"}) {
        lua_pushnil(L); lua_setglobal(L, k);
    }
}

void push_host_table(lua_State* L, Plugin* p) {
    lua_newtable(L);
    struct { const char* n; lua_CFunction f; } fns[] = {
        {"call", h_call}, {"log", h_log}, {"ui", h_ui}, {"now", h_now}, {"readfile", h_readfile},
        {"jsonEncode", h_json_encode}, {"jsonDecode", h_json_decode}, {"wantState", h_want_state}, {"memory", h_memory},
    };
    for (auto& e : fns) {
        lua_pushlightuserdata(L, p);
        lua_pushcclosure(L, e.f, 1);
        lua_setfield(L, -2, e.n);
    }
}

// A dispatch that a host call of the same plugin brings about (the page answering it may send an event
// back) runs inside that plugin's own dispatch and shares its budget; starting a fresh one there would
// let a plugin reset its clock at will.
struct Active {
    Plugin* prev;
    Plugin* p;
    explicit Active(Plugin* pl) : prev(t_active), p(pl) {
        t_active = p;
        if (p->activeDepth++ == 0) {
            p->instrUsed = 0;
            p->overBudget = false;
            p->dispatchStart = std::chrono::steady_clock::now();
        }
    }
    ~Active() {
        if (--p->activeDepth == 0) {
            p->cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p->dispatchStart).count();
            if (p->overBudget && p->L) lua_sethook(p->L, l_hook, LUA_MASKCOUNT, kHookInterval);
        }
        t_active = prev;
    }
};

// Records the outcome of a protected run and returns whether it counts as a success. `rc` is a
// lua_pcall status, or LUA_ERRMEM when a panic was recovered (there is then no error object on the
// stack, so the state is left untouched). Called while the run's Active scope is still open.
bool record_result(Plugin* p, int rc, bool recovered) {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - p->dispatchStart).count();
    // The hook and the library wrappers read the clock only before more plugin code runs, so a run
    // whose last step was one slow but bounded library call (its error caught by the plugin) can end
    // past the wall budget unseen. That counts like a budget error.
    const bool overrun = p->overBudget || ms > kWallBudgetMs;
    std::string err;
    if (rc == LUA_OK) {
        if (!overrun) { p->consecutiveErrors = 0; return true; }
        err = "execution budget exceeded (" + std::to_string((long long)ms) + " ms in one tick)";
    } else if (recovered) {
        err = "out of memory";
    } else {
        err = lua_isstring(p->L, -1) ? lua_tostring(p->L, -1) : (rc == LUA_ERRMEM ? "out of memory" : "runtime error");
        lua_pop(p->L, 1);
    }
    p->lastError = err;
    ++p->errorsTotal;
    plog(p, "error", err);
    if (overrun) {
        // Every overrun held the UI thread, and a good tick between two of them resets the consecutive
        // count, so they are also counted over a window.
        p->overruns.push_back(now);
        while (now - p->overruns.front() > kOverrunWindow) p->overruns.pop_front();
    }
    if (recovered) {
        // an unprotected error leaves the state in an unknown condition; stop the plugin for good
        p->failed = true;
        plog(p, "error", "plugin stopped: out of memory", "host");
    } else if (++p->consecutiveErrors >= kMaxErrors) {
        p->failed = true;
        plog(p, "error", "plugin stopped after " + std::to_string(kMaxErrors) + " consecutive errors; save a change (dev) or reinstall to restart it", "host");
    } else if (p->overruns.size() >= (std::size_t)kMaxErrors) {
        p->failed = true;
        plog(p, "error", "plugin stopped: it ran past its time budget " + std::to_string(kMaxErrors) + " times within " +
             std::to_string(kOverrunWindow.count()) + " minutes; save a change (dev) or reinstall to restart it", "host");
    }
    return false;
}

// Runs the function on top of the stack with nargs; records errors. Returns true on success.
bool run_protected(Plugin* p, int nargs) {
    Active act(p);
    ++p->dispatches;
    int rc = lua_pcall(p->L, nargs, 0, 0);
    return record_result(p, rc, false);
}

// One dispatch to deliver to the plugin's dispatch function. Its arguments are built inside a
// protected call (see run_dispatch) so an allocation refused at the memory cap becomes a caught
// error rather than an unprotected panic that would abort the launcher.
struct DispatchCall {
    const char* kind;              // "tick", "events", "state", "settings", "theme", "ui", ...
    const std::string* json;       // JSON payload pushed as an argument, or nullptr for none
    const char* widget;            // ui event only: widget id pushed before the value
};

// Runs on the plugin state under lua_pcall. Reads its DispatchCall from a light userdata argument and
// builds the Lua arguments here, where an out-of-memory raised at the cap is caught by the pcall.
int l_dispatch_trampoline(lua_State* L) {
    const DispatchCall* c = static_cast<const DispatchCall*>(lua_touserdata(L, 1));
    lua_getfield(L, LUA_REGISTRYINDEX, "rtx.dispatch");
    if (!lua_isfunction(L, -1)) return 0;
    lua_pushstring(L, c->kind);
    int nargs = 1;
    if (c->widget) {                                       // ui: (kind, widget, value|nil)
        lua_pushstring(L, c->widget);
        if (c->json && !c->json->empty()) luajson::Push(L, *c->json);   // leaves a nil when the JSON is bad
        else lua_pushnil(L);
        nargs = 3;
    } else if (c->json) {                                  // (kind, payload); skip on none or bad JSON
        if (c->json->empty() || !luajson::Push(L, *c->json)) return 0;
        nargs = 2;
    }
    lua_call(L, nargs, 0);
    return 0;
}

// Builds and runs one dispatch entirely inside a protected call, so a plugin sitting at its memory
// cap raises a caught error instead of an unprotected out-of-memory that would abort the process. The
// two pushes made before the pcall (a prebuilt trampoline fetched from the registry, and a light
// userdata) never allocate, so the memory cap can only be reached inside the pcall.
bool run_dispatch(Plugin* p, const DispatchCall& c) {
    lua_State* L = p->L;
    Active act(p);
    ++p->dispatches;
    if (!lua_checkstack(L, 6)) { record_result(p, LUA_ERRMEM, true); return false; }
    std::jmp_buf jb;
    std::jmp_buf* prev = t_panic;
    volatile int rc = LUA_OK;
    volatile bool recovered = false;
    if (setjmp(jb) == 0) {
        t_panic = &jb;
        lua_rawgeti(L, LUA_REGISTRYINDEX, p->trampolineRef);      // the trampoline, already built
        lua_pushlightuserdata(L, const_cast<DispatchCall*>(&c));
        rc = lua_pcall(L, 1, 0, 0);
    } else {
        recovered = true;                                        // a panic jumped back here
        rc = LUA_ERRMEM;
    }
    t_panic = prev;
    return record_result(p, (int)rc, recovered);
}

std::string result_json(Plugin* p, bool ok) {
    std::string o = "{\"ok\":";
    o += ok ? "true" : "false";
    o += ",\"error\":";
    if (p->lastError.empty()) o += "null"; else { o += '"'; o += json_escape(p->lastError); o += '"'; }
    o += ",\"failed\":"; o += p->failed ? "true" : "false";
    o += ",\"wantsState\":"; o += p->wantsState ? "true" : "false";
    o += ",\"log\":[";
    for (std::size_t i = 0; i < p->fresh.size(); ++i) { if (i) o += ','; o += p->fresh[i]; }   // already JSON objects
    p->fresh.clear();
    o += "]";
    if (p->uiSent != p->uiVersion) { o += ",\"ui\":"; o += p->uiJson; p->uiSent = p->uiVersion; }
    o += ",\"memory\":" + std::to_string((unsigned long long)p->memUsed);
    o += ",\"version\":\"" + std::string(RuntimeVersion()) + "\"}";
    return o;
}

// The client whose page made the bridge call now running: the game pid the launcher puts on that
// page's window as __rtx_pid (read only, set before any page script runs). 0 without a page context.
std::uint32_t current_client() {
    JSContextRef ctx = t_ctx;
    if (!ctx) return 0;
    JSStringRef name = JSStringCreateWithUTF8CString("__rtx_pid");
    JSValueRef v = JSObjectGetProperty(ctx, JSContextGetGlobalObject(ctx), name, nullptr);
    JSStringRelease(name);
    if (!v || !JSValueIsNumber(ctx, v)) return 0;
    double n = JSValueToNumber(ctx, v, nullptr);
    return (n >= 1 && n <= 4294967295.0) ? (std::uint32_t)n : 0;
}

Plugin* find(std::uint32_t client, const std::string& id) {
    auto it = g_plugins.find(Key(client, id));
    return it == g_plugins.end() ? nullptr : it->second.get();
}

void unload_state(std::uint32_t client, const std::string& id) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    auto it = g_plugins.find(Key(client, id));
    if (it == g_plugins.end()) return;
    if (it->second->L) lua_close(it->second->L);
    g_plugins.erase(it);
}

std::string missing(const std::string& id) {
    return "{\"ok\":false,\"error\":\"plugin not loaded: " + json_escape(id) + "\",\"failed\":true,\"log\":[]}";
}

std::string default_call(const std::string& id, const std::string& method, const std::string& args) {
    JSContextRef ctx = t_ctx;
    if (!ctx) return "{\"ok\":false,\"e\":\"host unavailable\"}";
    JSObjectRef global = JSContextGetGlobalObject(ctx);
    JSStringRef name = JSStringCreateWithUTF8CString("__rtxLuaCall");
    JSValueRef fv = JSObjectGetProperty(ctx, global, name, nullptr);
    JSStringRelease(name);
    if (!fv || !JSValueIsObject(ctx, fv)) return "{\"ok\":false,\"e\":\"host unavailable\"}";
    JSObjectRef fn = JSValueToObject(ctx, fv, nullptr);
    if (!fn || !JSObjectIsFunction(ctx, fn)) return "{\"ok\":false,\"e\":\"host unavailable\"}";
    JSValueRef argv[3] = { utf8_to_js(ctx, id), utf8_to_js(ctx, method), utf8_to_js(ctx, args) };
    JSValueRef exc = nullptr;
    JSValueRef r = JSObjectCallAsFunction(ctx, fn, nullptr, 3, argv, &exc);
    if (exc || !r) return "{\"ok\":false,\"e\":\"call failed\"}";
    return js_to_utf8(ctx, r);
}

}  // namespace

// ---- public API ----------------------------------------------------------------------------------
void SetCallHandler(CallHandler h) { g_call = std::move(h); }

ScopedContext::ScopedContext(JSContextRef ctx) { t_ctx = ctx; if (!g_call) g_call = default_call; }
ScopedContext::~ScopedContext() { t_ctx = nullptr; }

const char* RuntimeVersion() { return LUA_RELEASE; }

bool IsLoaded(const std::string& id) {
    const std::uint32_t client = current_client();
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    return find(client, id) != nullptr;
}

void Unload(const std::string& id) {
    unload_state(current_client(), id);
}

void UnloadClient(std::uint32_t client) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    for (auto it = g_plugins.begin(); it != g_plugins.end();) {
        if (it->first.first != client) { ++it; continue; }
        if (it->second->L) lua_close(it->second->L);
        it = g_plugins.erase(it);
    }
}

std::string Load(const std::string& id, const std::filesystem::path& root,
                 const std::vector<std::string>& scopes, const std::string& methodsJson) {
    if (!g_call) g_call = default_call;
    const std::uint32_t client = current_client();
    unload_state(client, id);
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    auto up = std::make_unique<Plugin>();
    Plugin* p = up.get();
    p->id = id; p->root = root; p->scopes = scopes;
    p->started = std::chrono::steady_clock::now();
    g_plugins[Key(client, id)] = std::move(up);

    std::string manifest;
    if (!read_plugin_file(p, "manifest.json", manifest) || manifest.empty()) {
        p->lastError = "manifest.json missing";
        p->failed = true;
        return result_json(p, false);
    }
    std::string mainFile = json_str(manifest, "main");
    if (mainFile.empty()) mainFile = "main.lua";
    if (mainFile.find('/') != std::string::npos || mainFile.find('\\') != std::string::npos ||
        mainFile.find("..") != std::string::npos || mainFile.size() < 5 || mainFile.compare(mainFile.size() - 4, 4, ".lua") != 0) {
        p->lastError = "manifest main must be a bare .lua filename";
        p->failed = true;
        return result_json(p, false);
    }
    std::string src;
    if (!read_plugin_file(p, mainFile, src)) {
        p->lastError = "cannot read " + mainFile;
        p->failed = true;
        return result_json(p, false);
    }

    p->L = lua_newstate(l_alloc, p);
    if (!p->L) { p->lastError = "cannot create Lua state"; p->failed = true; return result_json(p, false); }
    lua_State* L = p->L;
    lua_atpanic(L, l_atpanic);
    lua_sethook(L, l_hook, LUA_MASKCOUNT, kHookInterval);
    open_sandbox(L);
    lua_pushcfunction(L, l_dispatch_trampoline);
    p->trampolineRef = luaL_ref(L, LUA_REGISTRYINDEX);   // fetched without allocating on each dispatch

    // prelude(host, id, scopes, methods, version) -> dispatch
    if (luaL_loadbufferx(L, kPrelude, sizeof(kPrelude) - 1, "=rtx", "t") != LUA_OK) {
        p->lastError = std::string("prelude failed: ") + lua_tostring(L, -1);
        lua_pop(L, 1); p->failed = true;
        return result_json(p, false);
    }
    push_host_table(L, p);
    lua_pushstring(L, id.c_str());
    lua_newtable(L);
    for (std::size_t i = 0; i < scopes.size(); ++i) { lua_pushstring(L, scopes[i].c_str()); lua_rawseti(L, -2, (lua_Integer)i + 1); }
    if (!luajson::Push(L, methodsJson)) { lua_pop(L, 1); lua_newtable(L); }
    lua_pushstring(L, RuntimeVersion());
    {
        Active act(p);
        if (lua_pcall(L, 5, 1, 0) != LUA_OK) {
            p->lastError = std::string("prelude failed: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
            lua_pop(L, 1); p->failed = true;
            plog(p, "error", p->lastError);
            return result_json(p, false);
        }
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "rtx.dispatch");

    // main chunk
    std::string chunkName = "=" + mainFile;
    if (luaL_loadbufferx(L, src.data(), src.size(), chunkName.c_str(), "t") != LUA_OK) {
        p->lastError = lua_isstring(L, -1) ? lua_tostring(L, -1) : "syntax error";
        lua_pop(L, 1); p->failed = true;
        plog(p, "error", p->lastError);
        return result_json(p, false);
    }
    bool ok = run_protected(p, 0);
    if (!ok) p->failed = true;   // a main chunk that throws never gets ticks
    else plog(p, "info", "loaded " + mainFile + " (" + RuntimeVersion() + ")", "host");
    return result_json(p, ok);
}

std::string Tick(const std::string& id, const std::string& eventsJson, const std::string& stateJson) {
    const std::uint32_t client = current_client();
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(client, id);
    if (!p) return missing(id);
    if (p->failed || !p->L) return result_json(p, false);
    p->lastError.clear();
    bool ok = true;
    { DispatchCall c{"tick", nullptr, nullptr}; ok = run_dispatch(p, c) && ok; }
    if (!p->failed && !eventsJson.empty()) { DispatchCall c{"events", &eventsJson, nullptr}; ok = run_dispatch(p, c) && ok; }
    if (!p->failed && p->wantsState && !stateJson.empty()) { DispatchCall c{"state", &stateJson, nullptr}; ok = run_dispatch(p, c) && ok; }
    lua_gc(p->L, LUA_GCSTEP, 0);
    return result_json(p, ok);
}

std::string Event(const std::string& id, const std::string& kind, const std::string& payloadJson) {
    const std::uint32_t client = current_client();
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(client, id);
    if (!p) return missing(id);
    if (p->failed || !p->L) return result_json(p, false);
    p->lastError.clear();
    DispatchCall c{kind.c_str(), &payloadJson, nullptr};
    bool ok = run_dispatch(p, c);
    return result_json(p, ok);
}

std::string UiEvent(const std::string& id, const std::string& widget, const std::string& valueJson) {
    const std::uint32_t client = current_client();
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(client, id);
    if (!p) return missing(id);
    if (p->failed || !p->L) return result_json(p, false);
    p->lastError.clear();
    DispatchCall c{"ui", &valueJson, widget.c_str()};
    bool ok = run_dispatch(p, c);
    return result_json(p, ok);
}

std::string Info(const std::string& id) {
    const std::uint32_t client = current_client();
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(client, id);
    if (!p && !t_ctx) {
        // asked with no page to name a client (diagnostics): the first client's copy
        for (auto& kv : g_plugins) if (kv.first.second == id) { p = kv.second.get(); break; }
    }
    if (!p) return "{\"loaded\":false}";
    std::string o = "{\"loaded\":true,\"version\":\"" + std::string(RuntimeVersion()) + "\"";
    o += ",\"memory\":" + std::to_string((unsigned long long)p->memUsed);
    o += ",\"dispatches\":" + std::to_string(p->dispatches);
    o += ",\"cpuMs\":" + std::to_string(p->cpuMs);
    o += ",\"errors\":" + std::to_string(p->errorsTotal);
    o += ",\"failed\":"; o += p->failed ? "true" : "false";
    o += ",\"log\":[";
    bool first = true;
    for (auto& l : p->log) { if (!first) o += ','; first = false; o += l; }
    o += "]}";
    return o;
}

// ---- self test -------------------------------------------------------------------------------------
int SelfTest(const std::filesystem::path& pluginDir, std::string& report) {
    std::ostringstream r;
    int calls = 0;
    SetCallHandler([&](const std::string& pid, const std::string& method, const std::string& args) -> std::string {
        ++calls;
        r << "  call " << pid << " " << method << " " << args << "\n";
        if (method == "state.inventory") return "{\"ok\":true,\"v\":{\"items\":[[0,995,1000],[1,1511,1]],\"count\":2}}";
        if (method == "state.player") return "{\"ok\":true,\"v\":{\"name\":\"Tester\",\"x\":3222,\"y\":3218,\"plane\":0,\"in_world\":true}}";
        if (method == "settings.get") return "{\"ok\":true,\"v\":{\"alertFull\":true}}";
        if (method == "ui.settings") return "{\"ok\":true,\"v\":{\"alertFull\":true}}";
        if (method == "state.quests") return "{\"pending\":true}";
        if (method.rfind("overlay.", 0) == 0 || method.rfind("storage.", 0) == 0 || method.rfind("ui.", 0) == 0) return "{\"ok\":true,\"v\":true}";
        return "{\"ok\":false,\"e\":\"stub: no such method\"}";
    });
    const std::string id = "selftest";
    const std::string methods = "[{\"name\":\"state.player\",\"scope\":\"state.read\"},{\"name\":\"state.inventory\",\"scope\":\"state.read\"},"
        "{\"name\":\"state.quests\",\"scope\":\"state.read\"},{\"name\":\"overlay.toast\",\"scope\":\"overlay\"},{\"name\":\"storage.get\",\"scope\":\"storage\"},"
        "{\"name\":\"storage.set\",\"scope\":\"storage\"},{\"name\":\"ui.setTitle\",\"scope\":null},{\"name\":\"ui.settings\",\"scope\":null},{\"name\":\"settings.get\",\"scope\":null}]";
    r << "plugin dir: " << pluginDir.string() << "\n";
    std::string res = Load(id, pluginDir, {"state.read", "overlay", "storage"}, methods);
    r << "load: " << res << "\n";
    bool ok = res.find("\"ok\":true") != std::string::npos;
    for (int i = 0; i < 3 && ok; ++i) {
        res = Tick(id, "[{\"kind\":\"gameTick\",\"tick\":" + std::to_string(100 + i) + ",\"dtMs\":600},{\"kind\":\"skill_update\",\"skill\":0,\"name\":\"Attack\",\"level\":99,\"xp\":13034431}]",
                   "{\"in_world\":true,\"display_name\":\"Tester\"}");
        r << "tick " << i << ": " << res << "\n";
        ok = res.find("\"ok\":true") != std::string::npos;
    }
    if (ok) {
        // click the first button in the published tree, if any
        std::string info = Info(id);
        res = UiEvent(id, "w1:onClick", "null");
        r << "ui w1:onClick: " << res << "\n";
        ok = res.find("\"ok\":true") != std::string::npos;
        res = Event(id, "settings", "{\"alertFull\":false}");
        r << "settings: " << res << "\n";
        ok = ok && res.find("\"ok\":true") != std::string::npos;
    }
    r << "host calls: " << calls << "\n";
    r << Info(id) << "\n";
    Unload(id);
    r << (ok ? "RESULT: PASS" : "RESULT: FAIL") << "\n";
    report = r.str();
    return ok ? 0 : 1;
}

}  // namespace rtx::launcher::lua
