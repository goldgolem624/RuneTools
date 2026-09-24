#include "LuaJson.h"

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../lua/vendor/lua-5.4.7/lua.hpp"

namespace rtx::launcher::luajson {
namespace {

struct Parser {
    lua_State* L;
    const char* p;
    const char* end;
    int depth = 0;

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }

    bool lit(const char* s, std::size_t n) {
        if ((std::size_t)(end - p) < n || std::memcmp(p, s, n) != 0) return false;
        p += n; return true;
    }

    static void put_utf8(std::string& o, std::uint32_t cp) {
        if (cp < 0x80) o.push_back((char)cp);
        else if (cp < 0x800) { o.push_back((char)(0xC0 | (cp >> 6))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { o.push_back((char)(0xE0 | (cp >> 12))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        else { o.push_back((char)(0xF0 | (cp >> 18))); o.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
    }

    bool hex4(std::uint32_t& v) {
        if (end - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i]; v <<= 4;
            if (c >= '0' && c <= '9') v |= (std::uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (std::uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (std::uint32_t)(c - 'A' + 10);
            else return false;
        }
        p += 4; return true;
    }

    // Parses a string body after the opening quote into `out`; leaves p after the closing quote.
    bool str(std::string& out) {
        out.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (p >= end) return false;
            char e = *p++;
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                std::uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {              // surrogate pair
                    std::uint32_t lo = 0;
                    if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else cp = 0xFFFD;
                    } else cp = 0xFFFD;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
                put_utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool number() {
        const char* s = p;
        if (p < end && *p == '-') ++p;
        if (p >= end) return false;
        if (*p == '0') ++p;
        else if (*p >= '1' && *p <= '9') { while (p < end && *p >= '0' && *p <= '9') ++p; }
        else return false;
        bool isFloat = false;
        if (p < end && *p == '.') { isFloat = true; ++p; if (p >= end || *p < '0' || *p > '9') return false; while (p < end && *p >= '0' && *p <= '9') ++p; }
        if (p < end && (*p == 'e' || *p == 'E')) {
            isFloat = true; ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            if (p >= end || *p < '0' || *p > '9') return false;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        std::string tok(s, (std::size_t)(p - s));
        if (!isFloat && tok.size() <= 19) {
            char* ep = nullptr;
            long long v = std::strtoll(tok.c_str(), &ep, 10);
            if (ep && *ep == '\0' && tok.size() < 19) { lua_pushinteger(L, (lua_Integer)v); return true; }
            if (ep && *ep == '\0') {
                // 19 digits: may overflow; fall back to double when strtoll saturated
                if (v != LLONG_MAX && v != LLONG_MIN) { lua_pushinteger(L, (lua_Integer)v); return true; }
            }
        }
        lua_pushnumber(L, (lua_Number)std::strtod(tok.c_str(), nullptr));
        return true;
    }

    bool value() {
        ws();
        if (p >= end) return false;
        if (depth > 128) return false;
        // Each level holds a table and a key on the stack; a C function is only promised LUA_MINSTACK slots
        // and pushes are unchecked in this build, so make room before going deeper.
        if (!lua_checkstack(L, 4)) return false;
        char c = *p;
        if (c == '{') {
            ++p; ++depth;
            lua_newtable(L);
            ws();
            if (p < end && *p == '}') { ++p; --depth; return true; }
            std::string key;
            for (;;) {
                ws();
                if (p >= end || *p != '"') return false;
                ++p;
                if (!str(key)) return false;
                ws();
                if (p >= end || *p != ':') return false;
                ++p;
                lua_pushlstring(L, key.data(), key.size());
                if (!value()) { lua_pop(L, 1); return false; }
                if (lua_isnil(L, -1)) lua_pop(L, 2);           // null: leave the key absent
                else lua_rawset(L, -3);
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; --depth; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p; ++depth;
            lua_newtable(L);
            ws();
            if (p < end && *p == ']') { ++p; --depth; return true; }
            lua_Integer i = 1;
            for (;;) {
                if (!value()) return false;
                lua_rawseti(L, -2, i++);
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; --depth; return true; }
                return false;
            }
        }
        if (c == '"') {
            ++p;
            std::string s;
            if (!str(s)) return false;
            lua_pushlstring(L, s.data(), s.size());
            return true;
        }
        if (c == 't') { if (!lit("true", 4)) return false; lua_pushboolean(L, 1); return true; }
        if (c == 'f') { if (!lit("false", 5)) return false; lua_pushboolean(L, 0); return true; }
        if (c == 'n') { if (!lit("null", 4)) return false; lua_pushnil(L); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        return false;
    }
};

void esc(std::string& o, const char* s, std::size_t n) {
    o.push_back('"');
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o.push_back((char)c);
        }
    }
    o.push_back('"');
}

// Output past this size makes the whole value null. It also bounds the work: a table shared at every level
// ({t, t} nested) is not a cycle and would otherwise expand exponentially.
constexpr std::size_t kDumpMaxBytes = 8u << 20;

void dump(lua_State* L, int idx, std::string& o, int depth, std::set<const void*>& seen) {
    if (o.size() > kDumpMaxBytes) return;
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNIL: o += "null"; return;
    case LUA_TBOOLEAN: o += lua_toboolean(L, idx) ? "true" : "false"; return;
    case LUA_TNUMBER: {
        if (lua_isinteger(L, idx)) { o += std::to_string((long long)lua_tointeger(L, idx)); return; }
        double d = (double)lua_tonumber(L, idx);
        if (std::isnan(d) || std::isinf(d)) { o += "null"; return; }
        char b[40]; std::snprintf(b, sizeof(b), "%.17g", d); o += b; return;
    }
    case LUA_TSTRING: { std::size_t n = 0; const char* s = lua_tolstring(L, idx, &n); esc(o, s, n); return; }
    case LUA_TTABLE: {
        const void* ptr = lua_topointer(L, idx);
        if (depth > 64 || seen.count(ptr) || !lua_checkstack(L, 4)) { o += "null"; return; }
        seen.insert(ptr);
        // Array test: every key is an integer in 1..n where n = number of keys.
        lua_Integer count = 0; bool arr = true; lua_Integer maxk = 0;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            ++count;
            if (arr) {
                if (lua_isinteger(L, -2)) { lua_Integer k = lua_tointeger(L, -2); if (k < 1) arr = false; else if (k > maxk) maxk = k; }
                else arr = false;
            }
            lua_pop(L, 1);
        }
        if (arr && count > 0 && maxk == count) {
            o.push_back('[');
            for (lua_Integer i = 1; i <= count; ++i) {
                if (i > 1) o.push_back(',');
                lua_rawgeti(L, idx, i);
                dump(L, -1, o, depth + 1, seen);
                lua_pop(L, 1);
            }
            o.push_back(']');
        } else {
            o.push_back('{');
            bool first = true;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                int vt = lua_type(L, -1);
                if (vt == LUA_TFUNCTION || vt == LUA_TUSERDATA || vt == LUA_TTHREAD || vt == LUA_TLIGHTUSERDATA) { lua_pop(L, 1); continue; }
                if (!first) o.push_back(',');
                first = false;
                if (lua_type(L, -2) == LUA_TSTRING) { std::size_t n = 0; const char* s = lua_tolstring(L, -2, &n); esc(o, s, n); }
                else if (lua_isinteger(L, -2)) { std::string k = std::to_string((long long)lua_tointeger(L, -2)); esc(o, k.data(), k.size()); }
                else if (lua_type(L, -2) == LUA_TNUMBER) { char b[40]; std::snprintf(b, sizeof(b), "%.17g", (double)lua_tonumber(L, -2)); esc(o, b, std::strlen(b)); }
                else if (lua_type(L, -2) == LUA_TBOOLEAN) { const char* k = lua_toboolean(L, -2) ? "true" : "false"; esc(o, k, std::strlen(k)); }
                else { esc(o, "?", 1); }
                o.push_back(':');
                dump(L, -1, o, depth + 1, seen);
                lua_pop(L, 1);
            }
            o.push_back('}');
        }
        seen.erase(ptr);
        return;
    }
    default: o += "null"; return;
    }
}

}  // namespace

bool Push(lua_State* L, const char* text, std::size_t len) {
    Parser ps{L, text, text + len};
    int top = lua_gettop(L);
    if (!ps.value()) { lua_settop(L, top); lua_pushnil(L); return false; }
    ps.ws();
    if (ps.p != ps.end) { lua_settop(L, top); lua_pushnil(L); return false; }
    return true;
}

std::string Dump(lua_State* L, int idx) {
    std::string o;
    std::set<const void*> seen;
    dump(L, idx, o, 0, seen);
    if (o.size() > kDumpMaxBytes) return "null";
    return o;
}

}  // namespace rtx::launcher::luajson
