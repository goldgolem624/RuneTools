#include "LuaHost.h"
#include "LuaJson.h"
#include "BridgeUtil.h"
#include "Update.h"
#include "../shared/Log.h"

#include "../lua/vendor/lua-5.4.7/lua.hpp"

#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace rtx::launcher::lua {
namespace {

constexpr std::size_t kMemLimit      = 64 * 1024 * 1024;   // per plugin
constexpr long long   kInstrBudget   = 40'000'000;          // per dispatch (one tick, one event)
constexpr int         kHookInterval  = 10'000;
constexpr double      kWallBudgetMs  = 1500.0;              // per dispatch
constexpr int         kMaxErrors     = 8;                   // consecutive dispatch errors before the plugin stops
constexpr std::size_t kLogKeep       = 200;
constexpr std::size_t kFileMax       = 2 * 1024 * 1024;

struct Plugin {
    std::string id;
    std::filesystem::path root;
    std::vector<std::string> scopes;
    lua_State* L = nullptr;
    std::size_t memUsed = 0;
    long long instrUsed = 0;
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
};

std::recursive_mutex g_mu;   // a plugin call can re-enter the host (ui.settings pushes a settings event back)
std::map<std::string, std::unique_ptr<Plugin>> g_plugins;
CallHandler g_call;
thread_local JSContextRef t_ctx = nullptr;
thread_local Plugin* t_active = nullptr;   // the plugin whose code is running (hook and natives)

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
    p->instrUsed += kHookInterval;
    if (p->instrUsed > kInstrBudget) luaL_error(L, "execution budget exceeded (%lld instructions in one tick)", (long long)kInstrBudget);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p->dispatchStart).count();
    if (ms > kWallBudgetMs) luaL_error(L, "execution budget exceeded (%.0f ms in one tick)", ms);
}

void plog(Plugin* p, const char* level, const std::string& msg) {
    std::string line = std::string(level) + ": " + msg;
    if (line.size() > 2000) line.resize(2000);
    p->log.push_back(line);
    while (p->log.size() > kLogKeep) p->log.pop_front();
    p->fresh.push_back(line);
    if (p->fresh.size() > kLogKeep) p->fresh.erase(p->fresh.begin());
    if (std::strcmp(level, "error") == 0) rtx::log::Launcher("lua plugin " + p->id + ": " + msg);
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

// ---- host natives (reachable from the prelude only; it captures them in locals) ---------------
int h_call(lua_State* L) {
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
    Plugin* p = self(L);
    const char* level = luaL_optstring(L, 1, "info");
    std::size_t n = 0;
    const char* s = luaL_optlstring(L, 2, "", &n);
    const char* lv = (std::strcmp(level, "warn") == 0) ? "warn" : (std::strcmp(level, "error") == 0) ? "error" : "info";
    plog(p, lv, std::string(s, n));
    return 0;
}

int h_ui(lua_State* L) {
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
    Plugin* p = self(L);
    std::string rel = luaL_checkstring(L, 1);
    std::string s;
    if (!read_plugin_file(p, rel, s)) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int h_json_encode(lua_State* L) {
    luaL_checkany(L, 1);
    std::string s = luajson::Dump(L, 1);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int h_json_decode(lua_State* L) {
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

// ---- the prelude: builds the rtx table and returns the dispatch function -------------------------
// Runs once per plugin with (host, id, scopes, methods, version, source). Everything the plugin can
// reach is defined here; the host table itself is never exposed.
const char kPrelude[] = R"LUA(
local host, PLUGIN_ID, SCOPES, METHODS, VERSION = ...
local rawload, rawpcall, rawtype, rawpairs, rawipairs, rawtostring, rawselect = load, pcall, type, pairs, ipairs, tostring, select
local rawerror, rawsetmt, rawnext = error, setmetatable, next
local tinsert, tconcat, tremove = table.insert, table.concat, table.remove

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
function rtx.log(...) host.log("info", fmtArgs(...)) end
function rtx.warn(...) host.log("warn", fmtArgs(...)) end
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
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);
    // os: clock, time, date and difftime only (no exit, getenv, remove, rename, execute, tmpname)
    luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 0);
    lua_newtable(L);
    for (const char* k : {"clock", "time", "date", "difftime"}) {
        lua_getfield(L, -2, k); lua_setfield(L, -2, k);
    }
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

struct Active {
    Plugin* prev;
    explicit Active(Plugin* p) : prev(t_active) {
        t_active = p;
        p->instrUsed = 0;
        p->dispatchStart = std::chrono::steady_clock::now();
    }
    ~Active() {
        if (t_active) t_active->cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_active->dispatchStart).count();
        t_active = prev;
    }
};

// Runs the function on top of the stack with nargs; records errors. Returns true on success.
bool run_protected(Plugin* p, int nargs) {
    Active act(p);
    ++p->dispatches;
    int rc = lua_pcall(p->L, nargs, 0, 0);
    if (rc == LUA_OK) { p->consecutiveErrors = 0; return true; }
    std::string err = lua_isstring(p->L, -1) ? lua_tostring(p->L, -1) : (rc == LUA_ERRMEM ? "out of memory" : "runtime error");
    lua_pop(p->L, 1);
    p->lastError = err;
    ++p->errorsTotal;
    plog(p, "error", err);
    if (++p->consecutiveErrors >= kMaxErrors) {
        p->failed = true;
        plog(p, "error", "plugin stopped after " + std::to_string(kMaxErrors) + " consecutive errors; save a change (dev) or reinstall to restart it");
    }
    return false;
}

bool push_dispatch(Plugin* p) {
    lua_getfield(p->L, LUA_REGISTRYINDEX, "rtx.dispatch");
    if (!lua_isfunction(p->L, -1)) { lua_pop(p->L, 1); return false; }
    return true;
}

std::string result_json(Plugin* p, bool ok) {
    std::string o = "{\"ok\":";
    o += ok ? "true" : "false";
    o += ",\"error\":";
    if (p->lastError.empty()) o += "null"; else { o += '"'; o += json_escape(p->lastError); o += '"'; }
    o += ",\"failed\":"; o += p->failed ? "true" : "false";
    o += ",\"wantsState\":"; o += p->wantsState ? "true" : "false";
    o += ",\"log\":[";
    for (std::size_t i = 0; i < p->fresh.size(); ++i) { if (i) o += ','; o += '"'; o += json_escape(p->fresh[i]); o += '"'; }
    p->fresh.clear();
    o += "]";
    if (p->uiSent != p->uiVersion) { o += ",\"ui\":"; o += p->uiJson; p->uiSent = p->uiVersion; }
    o += ",\"memory\":" + std::to_string((unsigned long long)p->memUsed);
    o += ",\"version\":\"" + std::string(RuntimeVersion()) + "\"}";
    return o;
}

Plugin* find(const std::string& id) {
    auto it = g_plugins.find(id);
    return it == g_plugins.end() ? nullptr : it->second.get();
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
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    return find(id) != nullptr;
}

void Unload(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    auto it = g_plugins.find(id);
    if (it == g_plugins.end()) return;
    if (it->second->L) lua_close(it->second->L);
    g_plugins.erase(it);
}

std::string Load(const std::string& id, const std::filesystem::path& root,
                 const std::vector<std::string>& scopes, const std::string& methodsJson) {
    if (!g_call) g_call = default_call;
    Unload(id);
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    auto up = std::make_unique<Plugin>();
    Plugin* p = up.get();
    p->id = id; p->root = root; p->scopes = scopes;
    p->started = std::chrono::steady_clock::now();
    g_plugins[id] = std::move(up);

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
    lua_sethook(L, l_hook, LUA_MASKCOUNT, kHookInterval);
    open_sandbox(L);

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
    else plog(p, "info", "loaded " + mainFile + " (" + RuntimeVersion() + ")");
    return result_json(p, ok);
}

std::string Tick(const std::string& id, const std::string& eventsJson, const std::string& stateJson) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(id);
    if (!p) return missing(id);
    if (p->failed || !p->L) return result_json(p, false);
    lua_State* L = p->L;
    p->lastError.clear();
    bool ok = true;
    if (push_dispatch(p)) { lua_pushstring(L, "tick"); ok = run_protected(p, 1) && ok; }
    if (!p->failed && !eventsJson.empty() && push_dispatch(p)) {
        lua_pushstring(L, "events");
        if (!luajson::Push(L, eventsJson)) { lua_pop(L, 2); }
        else ok = run_protected(p, 2) && ok;
    }
    if (!p->failed && p->wantsState && !stateJson.empty() && push_dispatch(p)) {
        lua_pushstring(L, "state");
        if (!luajson::Push(L, stateJson)) { lua_pop(L, 2); }
        else ok = run_protected(p, 2) && ok;
    }
    lua_gc(L, LUA_GCSTEP, 0);
    return result_json(p, ok);
}

std::string Event(const std::string& id, const std::string& kind, const std::string& payloadJson) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(id);
    if (!p) return missing(id);
    if (p->failed || !p->L) return result_json(p, false);
    p->lastError.clear();
    if (!push_dispatch(p)) return result_json(p, false);
    lua_pushstring(p->L, kind.c_str());
    if (payloadJson.empty() || !luajson::Push(p->L, payloadJson)) { lua_pop(p->L, 2); return result_json(p, true); }
    bool ok = run_protected(p, 2);
    return result_json(p, ok);
}

std::string UiEvent(const std::string& id, const std::string& widget, const std::string& valueJson) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(id);
    if (!p) return missing(id);
    if (p->failed || !p->L) return result_json(p, false);
    p->lastError.clear();
    if (!push_dispatch(p)) return result_json(p, false);
    lua_pushstring(p->L, "ui");
    lua_pushstring(p->L, widget.c_str());
    if (valueJson.empty() || !luajson::Push(p->L, valueJson)) lua_pushnil(p->L);
    bool ok = run_protected(p, 3);
    return result_json(p, ok);
}

std::string Info(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    Plugin* p = find(id);
    if (!p) return "{\"loaded\":false}";
    std::string o = "{\"loaded\":true,\"version\":\"" + std::string(RuntimeVersion()) + "\"";
    o += ",\"memory\":" + std::to_string((unsigned long long)p->memUsed);
    o += ",\"dispatches\":" + std::to_string(p->dispatches);
    o += ",\"cpuMs\":" + std::to_string(p->cpuMs);
    o += ",\"errors\":" + std::to_string(p->errorsTotal);
    o += ",\"failed\":"; o += p->failed ? "true" : "false";
    o += ",\"log\":[";
    bool first = true;
    for (auto& l : p->log) { if (!first) o += ','; first = false; o += '"'; o += json_escape(l); o += '"'; }
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
