#include "BridgeUtil.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace rtx::launcher {

std::string js_to_utf8(JSContextRef ctx, JSValueRef v) {
    JSStringRef s = JSValueToStringCopy(ctx, v, nullptr);
    if (!s) return {};
    size_t bytes = JSStringGetMaximumUTF8CStringSize(s);
    std::string out(bytes, '\0');
    size_t n = JSStringGetUTF8CString(s, out.data(), bytes);
    JSStringRelease(s);
    if (n) out.resize(n - 1);
    return out;
}

JSValueRef utf8_to_js(JSContextRef ctx, const std::string& s) {
    JSStringRef js = JSStringCreateWithUTF8CString(s.c_str());
    JSValueRef out = JSValueMakeString(ctx, js);
    JSStringRelease(js);
    return out;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out; out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void log_js_throw(JSContextRef ctx, JSObjectRef fn, const char* what) noexcept {
    try {
        JSStringRef key = JSStringCreateWithUTF8CString("name");
        JSValueRef name = fn ? JSObjectGetProperty(ctx, fn, key, nullptr) : nullptr;
        JSStringRelease(key);
        rtx::log::Launcher("bridge " + (name ? js_to_utf8(ctx, name) : std::string("?")) +
                           ": call threw: " + what);
    } catch (...) {}
}

std::string get_string_arg(JSContextRef ctx, size_t argc,
                           const JSValueRef argv[], size_t idx) {
    if (idx >= argc) return {};
    return js_to_utf8(ctx, argv[idx]);
}

const JsonValue* JsonValue::get(const std::string& key) const {
    if (kind != Object) return nullptr;
    for (std::size_t i = keys.size(); i-- > 0;)
        if (keys[i] == key) return &items[i];
    return nullptr;
}

std::string JsonValue::str(const std::string& key) const {
    const JsonValue* v = get(key);
    return (v && v->kind == String) ? v->text : std::string();
}

namespace {

struct JsonReader {
    const char* p;
    const char* end;

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }

    bool lit(const char* s) {
        std::size_t n = std::strlen(s);
        if ((std::size_t)(end - p) < n || std::memcmp(p, s, n) != 0) return false;
        p += n; return true;
    }

    bool digits() {
        if (p >= end || *p < '0' || *p > '9') return false;
        while (p < end && *p >= '0' && *p <= '9') ++p;
        return true;
    }

    static void put_utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o.push_back((char)cp);
        else if (cp < 0x800) { o.push_back((char)(0xC0 | (cp >> 6))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { o.push_back((char)(0xE0 | (cp >> 12))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        else { o.push_back((char)(0xF0 | (cp >> 18))); o.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
    }

    // Moves past the four digits only when all of them are hex.
    bool hex4(unsigned& v) {
        if (end - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i]; v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        p += 4; return true;
    }

    // A string body after its opening quote; leaves p after the closing one.
    bool str(std::string& out) {
        while (p < end) {
            unsigned char c = (unsigned char)*p++;
            if (c == '"') return true;
            if (c < 0x20) return false;                 // control characters must be escaped
            if (c != '\\') { out.push_back((char)c); continue; }
            if (p >= end) return false;
            switch (*p++) {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    const char* at = p;
                    unsigned lo;
                    p += 2;
                    if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else p = at;                        // not a pair: the next escape is read on its own
                }
                // A lone surrogate is written as U+FFFD, as the website's UTF-8 encoding writes it.
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                put_utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool number(std::string& out) {
        const char* s = p;
        if (p < end && *p == '-') ++p;
        if (p < end && *p == '0') ++p;
        else if (!digits()) return false;
        if (p < end && *p == '.') { ++p; if (!digits()) return false; }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            if (!digits()) return false;
        }
        out.assign(s, p);
        return true;
    }

    // A member name and the colon after it; leaves p at the member's value.
    bool key(std::string& k) {
        ws();
        if (p >= end || *p != '"') return false;
        ++p;
        k.clear();
        if (!str(k)) return false;
        ws();
        if (p >= end || *p != ':') return false;
        ++p; return true;
    }

    // Checks a value nested deeper than the levels kept, with a stack on the heap instead of
    // recursion, so any nesting JSON.parse accepts is accepted here too. Nothing of it is kept.
    bool skip() {
        std::string name;
        std::vector<char> open;                         // the closing bracket of each container still open
        for (;;) {
            ws();
            if (p >= end) return false;
            if (*p == '{' || *p == '[') {
                const char close = (*p++ == '{') ? '}' : ']';
                ws();
                if (p < end && *p == close) ++p;
                else {
                    open.push_back(close);
                    if (close == '}' && !key(name)) return false;
                    continue;                           // on to the first member's value
                }
            } else {
                JsonValue leaf;                         // a string, number or literal: no recursion
                if (!value(leaf, 0)) return false;
            }
            // After a value: close what ends here, then go on to the next member.
            for (;;) {
                if (open.empty()) return true;
                ws();
                if (p >= end) return false;
                if (*p == open.back()) { ++p; open.pop_back(); continue; }
                if (*p != ',') return false;
                ++p;
                if (open.back() == '}' && !key(name)) return false;
                break;
            }
        }
    }

    bool value(JsonValue& v, int depth) {
        ws();
        if (p >= end) return false;
        if (depth > 64) return skip();                  // checked, but read as null
        const char c = *p;
        if (c == '{' || c == '[') {
            const bool obj = (c == '{');
            const char close = obj ? '}' : ']';
            v.kind = obj ? JsonValue::Object : JsonValue::Array;
            ++p; ws();
            if (p < end && *p == close) { ++p; return true; }
            for (;;) {
                if (obj) {
                    v.keys.emplace_back();
                    if (!key(v.keys.back())) return false;
                }
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == close) { ++p; return true; }
                return false;
            }
        }
        if (c == '"') { ++p; v.kind = JsonValue::String; return str(v.text); }
        if (c == '-' || (c >= '0' && c <= '9')) { v.kind = JsonValue::Number; return number(v.text); }
        if (lit("true"))  { v.kind = JsonValue::Bool; v.flag = true; return true; }
        if (lit("false")) { v.kind = JsonValue::Bool; return true; }
        if (lit("null"))  return true;
        return false;
    }
};

}  // namespace

bool json_parse(const std::string& text, JsonValue& out) {
    out = JsonValue();
    JsonReader r{text.data(), text.data() + text.size()};
    bool ok = r.value(out, 0);
    if (ok) { r.ws(); ok = (r.p == r.end); }
    if (!ok) out = JsonValue();
    return ok;
}

}  // namespace rtx::launcher
